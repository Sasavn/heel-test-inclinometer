"""Bounded label measurement regression against forced measuring, current UI.

python tests/test_native_text_measure.py [--profile] [--cc gcc]
No git history, frozen sources, hardware, firmware build or repository writes.
Compile current production UI/fonts/renderer in a temporary snapshot. The second
build disables only cache eligibility, so geometry and every RGB565 frame must
match forced original measurement. --profile optionally times SETTINGS rendering
on this host (3 alternating runs, 52 warmup frames per theme), never hardware.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--profile", action="store_true")
    args = parser.parse_args()
    cc = shutil.which(args.cc)
    if not cc:
        parser.error("C11 compiler required")
    root = Path(__file__).resolve().parents[1]
    main = root / "tab5/main"
    common = [main / name for name in ("tab5_native_port.c", "tab5_ui.c", "tab5_measure.c",
              "tab5_ui_graph.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c",
              "native/font_native_fallback14.c")]
    common += sorted(main.glob("font_tab5_*.c"))
    renderer_name = "tab5/main/native/native_ui.c"
    harness_name = "tests/test_native_text_measure.c"
    sources = [root / renderer_name, root / harness_name, *common, *main.rglob("*.h")]
    contents = {p.relative_to(root).as_posix(): p.read_bytes() for p in sources}
    renderer = contents[renderer_name]
    print("renderer SHA256", hashlib.sha256(renderer).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="tab5-text-measure-") as directory:
        snap = Path(directory)
        for name, data in contents.items():
            path = snap / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        source = renderer.decode()
        guard = "bool cache_label=is_kind(o,1);"
        assert source.count(guard) == 1, "Update forced-measurement test guard"
        forced = snap / "native_ui_forced_measure.c"
        forced.write_text(source.replace(guard, "bool cache_label=false;"), encoding="utf-8")
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror=misleading-indentation",
                 "-UNDEBUG", "-DTAB5_NATIVE_INPUT_ONLY=1", "-DNUI_COLOR_DEPTH=16",
                 "-DNUI_PROFILE_FINE_COUNTERS=0", "-DNUI_PROFILE_PRIMITIVE_TIMING=0",
                 "-I" + str(snap / "tab5/main"), "-I" + str(snap / "tab5/main/native")]
        objects = []
        for i, path in enumerate(common):
            obj = snap / f"common-{i}.o"
            subprocess.run([cc, *flags, "-c", str(snap / path.relative_to(root)), "-o", str(obj)],
                           check=True, timeout=60)
            objects.append(str(obj))
        executables = {}
        for mode in ("cached", "forced"):
            extra = []
            if mode == "forced":
                extra = ["-DTEST_MEASURE_FORCED=1", '-DTEXT_MEASURE_RENDERER="' + forced.as_posix() + '"']
            obj = snap / (mode + ".o")
            subprocess.run([cc, *flags, *extra, "-c", str(snap / harness_name), "-o", str(obj)],
                           check=True, timeout=60)
            exe = snap / (mode + ".exe")
            subprocess.run([cc, str(obj), *objects, "-lm", "-o", str(exe)], check=True, timeout=60)
            executables[mode] = exe
            run = subprocess.run([str(exe), str(snap / (mode + ".rgb565")), "proof"],
                                 capture_output=True, text=True, timeout=60)
            print(mode + ": " + run.stdout.strip(), flush=True)
            if run.stderr:
                print(run.stderr, end="", flush=True)
            if run.returncode:
                raise SystemExit(f"{mode} regression failed: exit {run.returncode}")
        digest = hashlib.sha256()
        with (snap / "cached.rgb565").open("rb") as a, (snap / "forced.rgb565").open("rb") as b:
            while True:
                x, y = a.read(1024 * 1024), b.read(1024 * 1024)
                assert x == y, "Cached/forced geometry or RGB565 differs"
                if not x:
                    break
                digest.update(x)
        print("PASS byte-exact geometry/RGB565 cached==forced; stream SHA256", digest.hexdigest(), flush=True)
        if args.profile:
            profile(executables, snap)
    assert (root / renderer_name).read_bytes() == renderer, "Renderer changed during regression"
    print("PASS production renderer unchanged during test; no device or firmware build", flush=True)


def profile(executables, snap):
    try:
        import psutil
    except ImportError:
        print("Host profile skipped: optional psutil compiler monitor unavailable")
        return
    pattern = re.compile(r"^(ninja|cmake|.*gcc|g\+\+|cc1|cc1plus|clang|.*-ld|ld|rustc|cl|msbuild)(\.exe)?$", re.I)
    def compilers():
        return [p.info for p in psutil.process_iter(["pid", "name"]) if pattern.match(p.info["name"] or "")]
    results = []
    for repeat in range(3):
        for mode in (("forced", "cached") if repeat % 2 == 0 else ("cached", "forced")):
            if compilers():
                print("Host profile skipped: another build is active; regression remains PASS")
                return
            done, contamination = threading.Event(), []
            def monitor():
                while not done.wait(.1):
                    contamination.extend(compilers())
            thread = threading.Thread(target=monitor)
            thread.start()
            path = snap / f"{mode}-{repeat}.csv"
            try:
                subprocess.run([str(executables[mode]), str(path), "time", "time"], check=True, timeout=60)
            finally:
                done.set()
                thread.join()
            contamination.extend(compilers())
            if contamination:
                print("Host profile discarded: concurrent build detected; regression remains PASS")
                return
            rows = list(csv.DictReader(path.open()))
            for theme in ("0", "1"):
                active = [r for r in rows if r["theme"] == theme and int(r["frame"]) >= 52]
                results.append({"mode": mode, "repeat": repeat, "theme": theme, "frames": len(active),
                    "render_us": statistics.mean(float(r["render_us"]) for r in active),
                    "input_us": statistics.mean(float(r["input_us"]) for r in active)})
    print("HOST ONLY release O2, 3 alternating repeats, render excludes input/IO/transport:", flush=True)
    print(json.dumps(results, indent=2), flush=True)
    for theme in ("0", "1"):
        old = statistics.mean(r["render_us"] for r in results if r["theme"] == theme and r["mode"] == "forced")
        new = statistics.mean(r["render_us"] for r in results if r["theme"] == theme and r["mode"] == "cached")
        print(f"HOST theme={theme}: forced={old:.3f}us cached={new:.3f}us reduction={100*(1-new/old):.2f}%", flush=True)


if __name__ == "__main__":
    main()
