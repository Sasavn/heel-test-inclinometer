"""Threaded production registration-wrapper regression; no devices or writes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="tab5-volume-registry-") as directory:
    tmp = Path(directory)
    exe = tmp / "registration.exe"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
        "-I" + str(ROOT / "tab5/main"),
        str(ROOT / "tab5/tests/test_services_volume_registration.c"),
        "-lbcrypt", "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=tmp, check=True)
