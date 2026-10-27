"""Production retained surface on/off proof; optional host-only timing.

python tests/test_native_layer_cache.py [--profile]
No git history, device or repository build. Real UI/fonts and native renderer
are frozen into a temporary directory. Baseline uses the same public APIs with
enabled=false. RGB565 comparison includes complete screen corners/edge strips.
"""
import argparse
import csv
import hashlib
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", action="store_true")
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    cc = shutil.which(args.cc)
    if not cc:
        parser.error("C11 compiler required")
    root = Path(__file__).resolve().parents[1]
    main = root / "tab5/main"
    common = [main / name for name in ("tab5_native_port.c", "tab5_ui.c", "tab5_measure.c", "tab5_ui_graph.c",
              "tab5_time.c", "tab5_plot.c", "tab5_level.c", "native/font_native_fallback14.c")]
    common += sorted(main.glob("font_tab5_*.c"))
    harness = "tests/test_native_layer_cache.c"
    renderer = "tab5/main/native/native_ui.c"
    header = "tab5/main/native/native_ui.h"
    paths = [root / harness, root / renderer, *common, *main.rglob("*.h")]
    contents = {p.relative_to(root).as_posix(): p.read_bytes() for p in paths}
    for name in (renderer, header):
        print(name, "SHA256", hashlib.sha256(contents[name]).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="tab5-layer-cache-") as directory:
        snap = Path(directory)
        for name, data in contents.items():
            path = snap / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror=misleading-indentation", "-UNDEBUG",
                 "-DTAB5_NATIVE_INPUT_ONLY=1", "-DNUI_PROFILE_FINE_COUNTERS=0", "-DNUI_PROFILE_PRIMITIVE_TIMING=0",
                 "-I" + str(snap / "tab5/main"), "-I" + str(snap / "tab5/main/native")]
        objects = []
        for i, path in enumerate([root / harness, *common]):
            obj = snap / f"{i}.o"
            subprocess.run([cc, *flags, "-c", str(snap / path.relative_to(root)), "-o", str(obj)], check=True, timeout=60)
            objects.append(str(obj))
        exe = snap / "layer-cache.exe"
        subprocess.run([cc, *objects, "-lm", "-o", str(exe)], check=True, timeout=60)
        for mode in ("off", "on"):
            run = subprocess.run([str(exe), mode, str(snap / (mode + ".rgb565")), "proof"],
                                 text=True, capture_output=True, timeout=90)
            print(mode + ":\n" + run.stdout, end="", flush=True)
            if run.stderr:
                print(run.stderr, end="", flush=True)
            if run.returncode:
                raise SystemExit(f"{mode} regression failed: exit {run.returncode}")
        digest = hashlib.sha256()
        total = 0
        with (snap / "off.rgb565").open("rb") as a, (snap / "on.rgb565").open("rb") as b:
            while True:
                x, y = a.read(1024 * 1024), b.read(1024 * 1024)
                if x != y:
                    at = next((i for i, (u, v) in enumerate(zip(x, y)) if u != v), min(len(x), len(y)))
                    raise AssertionError(f"off/on RGB565 mismatch at byte {total + at}")
                if not x:
                    break
                total += len(x)
                digest.update(x)
        print(f"PASS off==on byte-exact entire RGB565 stream, {total} bytes; SHA256 {digest.hexdigest()}", flush=True)
        if args.profile:
            profile(exe, snap)
    for name in (renderer, header):
        assert (root / name).read_bytes() == contents[name], "Native source/header changed during proof"


def profile(exe, snap):
    try:
        import psutil
    except ImportError:
        print("Host profile skipped: optional psutil unavailable")
        return
    pattern = re.compile(r"^(ninja|cmake|.*gcc|g\+\+|cc1|cc1plus|clang|.*-ld|ld|rustc|cl|msbuild)(\.exe)?$", re.I)
    def compilers():
        return [p.info for p in psutil.process_iter(["name", "pid"]) if pattern.match(p.info["name"] or "")]
    results = []
    for repeat in range(3):
        for mode in (("off", "on") if repeat % 2 == 0 else ("on", "off")):
            if compilers():
                print("Profile skipped: concurrent build; pixel-proof PASS remains valid")
                return
            done, contamination = threading.Event(), []
            def monitor():
                while not done.wait(.1):
                    contamination.extend(compilers())
            thread = threading.Thread(target=monitor)
            thread.start()
            path = snap / f"{mode}-{repeat}.csv"
            try:
                subprocess.run([str(exe), mode, str(path), "time"], check=True, timeout=60)
            finally:
                done.set()
                thread.join()
            contamination.extend(compilers())
            if contamination:
                print("Profile discarded: concurrent build; pixel-proof PASS remains valid")
                return
            rows = list(csv.DictReader(path.open()))
            for theme in ("0", "1"):
                active = [float(r["render_us"]) for r in rows if r["theme"] == theme and int(r["frame"]) >= 52]
                results.append((repeat, mode, theme, statistics.mean(active)))
    print("HOST ONLY render_us, O2, warm=52/theme; (repeat, mode, theme, mean_us):", results, flush=True)
    for theme in ("0", "1"):
        a = statistics.mean(r[3] for r in results if r[1] == "off" and r[2] == theme)
        b = statistics.mean(r[3] for r in results if r[1] == "on" and r[2] == theme)
        print(f"HOST theme={theme} off={a:.3f}us on={b:.3f}us reduction={100*(1-b/a):.2f}%", flush=True)


if __name__ == "__main__":
    main()
