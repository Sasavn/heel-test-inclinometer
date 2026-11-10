"""Real production role() versus accepted sequential semantics; requires CPU GO."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".tools/native-1.6.14/atomic-role")
    parser.add_argument("--benchmark-gate", type=Path, default=ROOT / ".tools/native-opt/BENCHMARK_GATE")
    args = parser.parse_args()
    if args.benchmark_gate.exists():
        parser.error("Benchmark gate active; no build/run")
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("Use new output directory")
    out.mkdir(parents=True, exist_ok=True)
    main = ROOT / "tab5/main"
    sources = [ROOT / "tab5/tests/test_native_atomic_role.c"] + [main / n for n in ("tab5_ui_graph.c", "tab5_measure.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c", "native/native_ui.c", "native/font_native_fallback14.c")]
    sources += sorted(main.glob("font_tab5_*.c"))
    closure = sorted(set(sources + [main / "tab5_ui.c", Path(__file__).resolve()] + list(main.rglob("*.h"))))
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = {str(p.relative_to(ROOT)): digest(p) for p in closure}
    report = {"status": "RUNNING", "source_sha256": hashes, "results": []}
    for fine in (0, 1):
        if args.benchmark_gate.exists():
            parser.error("Benchmark gate active before build/run")
        exe = out / f"atomic-role-fine{fine}.exe"
        command = [shutil.which("gcc"), "-std=c11", "-O2", "-fno-builtin-calloc", f"-DNUI_PROFILE_FINE_COUNTERS={fine}", "-DNUI_PROFILE_PRIMITIVE_TIMING=0", "-DNUI_COLOR_DEPTH=16", "-I"+str(main), *map(str, sources), "-Wl,--wrap=calloc", "-lm", "-o", str(exe)]
        built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (out / f"build-fine{fine}.log").write_bytes(built.stdout)
        if built.returncode:
            report.update(status="FAIL", failed_build=command);break
        if args.benchmark_gate.exists():
            parser.error("Benchmark gate active before test")
        run = subprocess.run([str(exe)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (out / f"atomic-role-fine{fine}.log").write_bytes(run.stdout)
        stable = all(digest(ROOT / n) == h for n, h in hashes.items())
        report["results"].append({"fine": fine, "returncode": run.returncode, "source_stable": stable})
        if run.returncode or not stable:
            report["status"] = "FAIL";break
    if report["status"] != "FAIL":
        report["status"] = "PASS"
    (out / "ATOMIC_ROLE_GATE.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("ATOMIC_ROLE", report["status"])
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
