"""Test current production four-level text AA; no hardware, git or old sources.

Run: python tests/test_native_text_aa.py [--cc gcc]
Requires only Python stdlib and a C11 GCC/Clang compiler. Temporary snapshots
compile the actual renderer unchanged, then force its generic branch for 4bpp
in a test-only copy. All RGB565 frames must match each other and independent
mathematical expectations in the C harness. No repository files are written.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc", help="C11 compiler executable")
    cc_name = parser.parse_args().cc
    cc = shutil.which(cc_name)
    if not cc:
        parser.error(f"C11 compiler not found: {cc_name}")
    root = Path(__file__).resolve().parents[1]
    names = ["tests/test_native_text_aa.c"] + [
        "tab5/main/native/" + name for name in (
            "native_ui.c", "native_ui.h", "native_gradient.h",
            "font_native_fallback14.c")]
    contents = {name: (root / name).read_bytes() for name in names}
    renderer = contents["tab5/main/native/native_ui.c"]
    print("production native_ui.c SHA256:", hashlib.sha256(renderer).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix="tab5-text-aa-") as directory:
        snapshot = Path(directory)
        for name, data in contents.items():
            path = snapshot / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        source = renderer.decode("utf-8")
        guard = "if(d.bpp==4){"
        if source.count(guard) != 1:
            raise AssertionError("Update test-only generic-path guard for this renderer")
        slow = snapshot / "native_ui_generic_test.c"
        slow.write_text(source.replace(guard, "if(0&&d.bpp==4){"), encoding="utf-8")
        outputs = []
        for mode in ("fast", "generic"):
            exe = snapshot / (mode + ".exe")
            bitmap = snapshot / (mode + ".rgb565")
            flags = []
            if mode == "generic":
                flags = ['-DTEXT_AA_RENDERER="' + slow.as_posix() + '"']
            subprocess.run([
                cc, "-std=c11", "-O2", "-Wall", "-Wextra",
                "-Werror=misleading-indentation", "-UNDEBUG",
                "-DNUI_COLOR_DEPTH=16", "-DNUI_PROFILE_FINE_COUNTERS=0",
                "-DNUI_PROFILE_PRIMITIVE_TIMING=0", *flags,
                "-I" + str(snapshot / "tab5/main/native"),
                str(snapshot / names[0]), str(snapshot / names[-1]),
                "-lm", "-o", str(exe)], check=True, timeout=60)
            run = subprocess.run([str(exe), str(bitmap)], capture_output=True,
                                 text=True, timeout=30)
            print(f"{mode}: {run.stdout.strip()}", flush=True)
            if run.stderr:
                print(run.stderr, end="", flush=True)
            if run.returncode:
                raise SystemExit(f"Text AA {mode} failed: exit {run.returncode}")
            outputs.append(run.stdout)
        if outputs[0] != outputs[1]:
            raise AssertionError("Fast/generic test results differ")
        if (snapshot / "fast.rgb565").read_bytes() != (snapshot / "generic.rgb565").read_bytes():
            raise AssertionError("Fast4/generic4 RGB565 bitmaps differ")
    changed = [name for name, data in contents.items()
               if (root / name).read_bytes() != data]
    if changed:
        raise AssertionError(f"Production closure changed during test: {changed}")
    print("PASS all 3840 frame bitmaps: fast4 == generic4; production unchanged")


if __name__ == "__main__":
    main()
