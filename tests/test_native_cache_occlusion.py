"""Pixel/callback/ROI proof for production180 vs cache-on179 occlusion semantics.

python tests/test_native_cache_occlusion.py
Reference builds the same current production source with only logical cache
occlusion disabled. Real UI/fonts, runtime cache enabled in both; no git or
old source is required. No device, firmware build or repository writes.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    cc = shutil.which(parser.parse_args().cc)
    if not cc:
        parser.error("C11 compiler required")
    root = Path(__file__).resolve().parents[1]
    main = root / "tab5/main"
    common = [main / name for name in ("tab5_native_port.c", "tab5_backlight.c", "tab5_ui.c", "tab5_measure.c", "tab5_ui_graph.c",
              "tab5_time.c", "tab5_plot.c", "tab5_level.c", "native/font_native_fallback14.c")]
    common += sorted(main.glob("font_tab5_*.c"))
    harness = "tests/test_native_cache_occlusion.c"
    renderer = "tab5/main/native/native_ui.c"
    header = "tab5/main/native/native_ui.h"
    paths = [root / harness, root / "tests/test_native_layer_cache.c", root / renderer, *common, *main.rglob("*.h")]
    contents = {p.relative_to(root).as_posix(): p.read_bytes() for p in paths}
    for name in (renderer, header):
        print(name, "SHA256", hashlib.sha256(contents[name]).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="tab5-cache-occlusion-") as directory:
        snap = Path(directory)
        for name, data in contents.items():
            p = snap / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror=misleading-indentation", "-UNDEBUG",
                 "-DTAB5_NATIVE_INPUT_ONLY=1", "-DNUI_PROFILE_FINE_COUNTERS=1", "-DNUI_PROFILE_PRIMITIVE_TIMING=0",
                 "-I" + str(snap / "tab5/main"), "-I" + str(snap / "tab5/main/native")]
        objects = []
        for i, p in enumerate(common):
            obj = snap / f"common-{i}.o"
            subprocess.run([cc, *flags, "-c", str(snap / p.relative_to(root)), "-o", str(obj)], check=True, timeout=60)
            objects.append(str(obj))
        stores = {}
        for name, value in (("reference", 0), ("optimized", 1)):
            exe, obj = snap / (name + ".exe"), snap / (name + ".o")
            subprocess.run([cc, *flags, f"-DNUI_LAYER_CACHE_OCCLUSION={value}", "-c", str(snap / harness), "-o", str(obj)], check=True, timeout=60)
            subprocess.run([cc, str(obj), *objects, "-lm", "-o", str(exe)], check=True, timeout=60)
            run = subprocess.run([str(exe), str(snap / (name + ".bin")), "proof"], text=True, capture_output=True, timeout=90)
            print(name + ":\n" + run.stdout, end="", flush=True)
            if run.stderr:
                print(run.stderr, end="", flush=True)
            if run.returncode:
                raise SystemExit(f"{name} proof failed: {run.returncode}")
            stores[name] = {int(t): int(fill) for t, fill in re.findall(r"STOREPROOF theme=(\d+) fill_pixels=(\d+)", run.stdout)}
        digest = hashlib.sha256()
        total = 0
        with (snap / "reference.bin").open("rb") as a, (snap / "optimized.bin").open("rb") as b:
            while True:
                x, y = a.read(1024 * 1024), b.read(1024 * 1024)
                if x != y:
                    at = next((i for i, (u, v) in enumerate(zip(x, y)) if u != v), min(len(x), len(y)))
                    raise AssertionError(f"RGB565/callback/ROI mismatch at byte {total + at}")
                if not x:
                    break
                digest.update(x)
                total += len(x)
        for theme in (0, 1):
            saved = stores["reference"][theme] - stores["optimized"][theme]
            # The two interior fills must disappear; legacy occlusion can also
            # remove some ancestor background on the disjoint side strips.
            assert saved >= 2 * 524892, (theme, stores, saved)
            print(f"PASS theme={theme}: {saved} fill-pixel writes / {saved * 2} destination bytes removed per warm SETTINGS frame", flush=True)
        print(f"PASS byte-exact RGB565 + callback count/order/clips + rendered ROI, {total} bytes, SHA256 {digest.hexdigest()}", flush=True)
    for name in (renderer, header):
        assert (root / name).read_bytes() == contents[name], "Native source/header changed during proof"


if __name__ == "__main__":
    main()
