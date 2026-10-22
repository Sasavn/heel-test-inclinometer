"""Isolated 1.6.14 cache/keyboard regressions. Run only after main CPU GO.

Every group runs release fine0 and diagnostic fine1, timing0. This does not
replace the immutable-v1 98-frame gate in run_native_ui_test.py.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=["keyboard", "cache", "custom", "numeric", "cold-memory", "all"], default="all")
    parser.add_argument("--out", type=Path, default=ROOT / ".tools/native-1.6.14/targeted")
    parser.add_argument("--benchmark-gate", type=Path, default=ROOT / ".tools/native-opt/BENCHMARK_GATE")
    args = parser.parse_args()
    if args.benchmark_gate.exists():
        parser.error("Benchmark gate active; no compilation or test started")
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("Use a new output directory to preserve earlier evidence")
    out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("gcc")
    if not compiler:
        parser.error("gcc required")
    main = ROOT / "tab5/main"
    sources = [ROOT / "tab5/tests/test_native_cache_keyboard.c"]
    sources += [main / name for name in ("tab5_ui.c", "tab5_ui_graph.c", "tab5_measure.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c", "native/native_ui.c", "native/font_native_fallback14.c")]
    sources += sorted(main.glob("font_tab5_*.c"))
    closure = sorted(set(sources + list(main.rglob("*.h")) + [Path(__file__).resolve()]))
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in closure}
    groups = ["keyboard", "cache", "custom", "numeric", "cold-memory"] if args.group == "all" else [args.group]
    results = []
    report = {"status": "RUNNING", "source_sha256": hashes, "results": results}
    def save():
        (out / "TARGETED_GATE.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    save()
    for fine in (0, 1):
        if args.benchmark_gate.exists():
            parser.error("Benchmark gate became active before build")
        exe = out / f"targeted-fine{fine}.exe"
        command = [compiler, "-std=c11", "-O2", f"-DNUI_PROFILE_FINE_COUNTERS={fine}", "-DNUI_PROFILE_PRIMITIVE_TIMING=0", "-DNUI_COLOR_DEPTH=16", "-I"+str(main), *map(str, sources), "-lm", "-o", str(exe)]
        built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (out / f"build-fine{fine}.log").write_bytes(built.stdout)
        if built.returncode:
            report.update(status="FAIL", failed_build=command);save();return 1
        for group in groups:
            if args.benchmark_gate.exists():
                parser.error("Benchmark gate became active before test")
            if any(sha(ROOT / p) != digest for p, digest in hashes.items()):
                parser.error("Source closure changed; freeze snapshot before testing")
            start = time.monotonic()
            run = subprocess.run([str(exe), group], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            log = f"{group}-fine{fine}.log";(out / log).write_bytes(run.stdout)
            stable = all(sha(ROOT / p) == digest for p, digest in hashes.items())
            results.append({"group": group, "fine": fine, "timing": 0, "returncode": run.returncode, "source_stable": stable, "log": log, "elapsed_seconds_nonbenchmark": time.monotonic()-start})
            print(group, f"fine{fine}", "PASS" if run.returncode == 0 and stable else "FAIL", flush=True)
            if run.returncode or not stable:
                report["status"] = "FAIL";save();return 1
            save()
    report["status"] = "PASS";save();return 0


if __name__ == "__main__":
    raise SystemExit(main())
