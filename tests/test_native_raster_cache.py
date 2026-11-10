"""Pixel-exact production raster/cache regression; no hardware or git required.

Run: python tests/test_native_raster_cache.py [--cc gcc]
Only Python's standard library and a C11 GCC/Clang toolchain are needed. A
temporary snapshot of the current native renderer, headers, fallback font and
C harness is compiled in fine0 and fine1 modes. Nothing is written to the repo.
The frozen reference math lives in the C harness, not an external baseline.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc", help="C11 compiler executable")
    args = parser.parse_args()
    cc = shutil.which(args.cc)
    if not cc:
        parser.error(f"C11 compiler not found: {args.cc}")
    root = Path(__file__).resolve().parents[1]
    names = ["tests/test_native_raster_cache.c"] + [
        "tab5/main/native/" + name for name in (
            "native_ui.c", "native_ui.h", "native_gradient.h",
            "font_native_fallback14.c")]
    contents = {name: (root / name).read_bytes() for name in names}
    renderer = contents["tab5/main/native/native_ui.c"]
    print("production native_ui.c SHA256:", digest(renderer), flush=True)
    print("normalized LF SHA256:", digest(renderer.replace(b"\r\n", b"\n")), flush=True)
    with tempfile.TemporaryDirectory(prefix="tab5-raster-cache-") as directory:
        snapshot = Path(directory)
        for name, data in contents.items():
            path = snapshot / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        outputs = []
        for fine in (0, 1):
            exe = snapshot / f"raster-fine{fine}.exe"
            command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra",
                       "-Werror=misleading-indentation", "-UNDEBUG",
                       "-DNUI_COLOR_DEPTH=16", f"-DNUI_PROFILE_FINE_COUNTERS={fine}",
                       "-DNUI_PROFILE_PRIMITIVE_TIMING=0",
                       str(snapshot / names[0]), str(snapshot / names[-1]),
                       "-lm", "-o", str(exe)]
            subprocess.run(command, check=True, timeout=90)
            run = subprocess.run([str(exe)], check=False, capture_output=True,
                                 text=True, timeout=120)
            print(f"fine{fine}: {run.stdout.strip()}", flush=True)
            if run.stderr:
                print(run.stderr, end="", flush=True)
            if run.returncode:
                raise SystemExit(f"Raster fine{fine} failed: exit {run.returncode}")
            outputs.append(run.stdout)
        if outputs[0] != outputs[1]:
            raise AssertionError("Release/diagnostic raster results differ")
    changed = [name for name, data in contents.items()
               if (root / name).read_bytes() != data]
    if changed:
        raise AssertionError(f"Production closure changed during test: {changed}")
    print("PASS current production snapshot; fine0 == fine1; source unchanged")


if __name__ == "__main__":
    main()
