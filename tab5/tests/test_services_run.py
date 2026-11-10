"""Build/run production service core with local injected flash/media only."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GCC = shutil.which("gcc")
if not GCC:
    raise SystemExit("GCC is required for service host tests")
output = ROOT / "tab5/tests/output_services"
output.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="run-", dir=output) as folder:
    work = Path(folder)
    for name in ("test_services", "test_services_startup", "test_services_retry", "test_retry_backoff"):
        case = work / name
        case.mkdir()
        exe = case / f"{name}.exe"
        subprocess.run([
            GCC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
            "-I" + str(ROOT / "tab5/main"),
            str(ROOT / f"tab5/tests/{name}.c"), "-lbcrypt", "-lm", "-o", str(exe)
        ], cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=case, check=True)
