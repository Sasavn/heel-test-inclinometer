"""Run focused production service capacity tests for variable/fixed sectors."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="tab5-capacity-") as directory:
    for sector_max in (4096, 512):
        case = Path(directory) / str(sector_max)
        case.mkdir()
        exe = case / "capacity.exe"
        subprocess.run([
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
            f"-DFF_MAX_SS={sector_max}", "-I" + str(ROOT / "tab5/main"),
            str(ROOT / "tab5/tests/test_services_capacity.c"),
            "-lbcrypt", "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], cwd=case, check=True)
