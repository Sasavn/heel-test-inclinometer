"""Full-scene CPU reference vs real deferred surfaces + exact GPU composition.

python tests/test_native_surface_defer.py [--cc gcc]
Standard Python + C11 only. Real UI/fonts/renderer; temporary host build only.
Reference disables cache/defer and fully renders each scene. GPU run retains a
portrait canonical buffer, copies only actual CPU dirty-minus-view pixels, then
does strided portrait copies from the pinned views. Complete portraits compare.
"""
import argparse
import hashlib
from pathlib import Path
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
    common = [main / name for name in ("tab5_native_port.c", "tab5_ui.c", "tab5_measure.c", "tab5_ui_graph.c",
              "tab5_time.c", "tab5_plot.c", "tab5_level.c", "native/font_native_fallback14.c")]
    common += sorted(main.glob("font_tab5_*.c"))
    harness = "tests/test_native_surface_defer.c"
    native = "tab5/main/native/native_ui.c"
    header = "tab5/main/native/native_ui.h"
    sources = [root / harness, root / "tests/test_native_layer_cache.c", root / native, *common, *main.rglob("*.h")]
    contents = {p.relative_to(root).as_posix(): p.read_bytes() for p in sources}
    for name in (native, header):
        print(name, "SHA256", hashlib.sha256(contents[name]).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="tab5-surface-defer-") as directory:
        snapshot = Path(directory)
        for name, data in contents.items():
            p = snapshot / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror=misleading-indentation", "-UNDEBUG",
                 "-DTAB5_NATIVE_INPUT_ONLY=1", "-DNUI_PROFILE_FINE_COUNTERS=0", "-DNUI_PROFILE_PRIMITIVE_TIMING=0",
                 "-I" + str(snapshot / "tab5/main"), "-I" + str(snapshot / "tab5/main/native")]
        objects = []
        for i, p in enumerate([root / harness, *common]):
            obj = snapshot / f"{i}.o"
            subprocess.run([cc, *flags, "-c", str(snapshot / p.relative_to(root)), "-o", str(obj)], check=True, timeout=60)
            objects.append(str(obj))
        exe = snapshot / "deferred-proof.exe"
        subprocess.run([cc, *objects, "-lm", "-o", str(exe)], check=True, timeout=60)
        for mode in ("reference", "gpu", "double", "gpu1", "double1"):
            run = subprocess.run([str(exe), mode, str(snapshot / (mode + ".rgb565"))],
                                 capture_output=True, text=True, timeout=120)
            print(mode + ":\n" + run.stdout, end="", flush=True)
            if run.stderr:
                print(run.stderr, end="", flush=True)
            if run.returncode:
                raise SystemExit(f"{mode} proof failed: {run.returncode}")
        for mode in ("gpu", "double", "gpu1", "double1"):
            digest = hashlib.sha256()
            offset = 0
            with (snapshot / "reference.rgb565").open("rb") as a, (snapshot / (mode + ".rgb565")).open("rb") as b:
                while True:
                    x, y = a.read(1024 * 1024), b.read(1024 * 1024)
                    if x != y:
                        first = next((i for i, (u, v) in enumerate(zip(x, y)) if u != v), min(len(x), len(y)))
                        raise AssertionError(f"Full portrait mismatch at byte {offset + first}")
                    if not x:
                        break
                    digest.update(x)
                    offset += len(x)
            print(f"PASS {mode}: independent full CPU == persistent GPU0/GPU90 composition, {offset} bytes, SHA256 {digest.hexdigest()}", flush=True)
    for name in (native, header):
        assert (root / name).read_bytes() == contents[name], "Native source/header changed during test"


if __name__ == "__main__":
    main()
