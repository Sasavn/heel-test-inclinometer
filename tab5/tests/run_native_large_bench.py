"""Sequential QPC/O2 native-large benchmark. No IDF, window, device or network.

python tab5/tests/run_native_large_bench.py --frames 400 --warmup 40 --repeats 3
--fine-counters adds diagnostic primitive calls; default release timings use 0.
Inputs/flush quantization reuse native_baseline_contract.h v1. New UI scenes
are a v2 workload and must not be divided by old LVGL results as a speedup.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import threading
import time
import psutil

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / ".tools/native-large/performance"


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def compiler_processes():
    pattern = re.compile(r"^(ninja|cmake|.*gcc|g\+\+|cc1|cc1plus|clang|.*-ld|ld|rustc|cl|msbuild)(\.exe)?$", re.I)
    return [{"pid": p.info["pid"], "name": p.info["name"]} for p in psutil.process_iter(["pid", "name"])
            if pattern.match(p.info["name"] or "")]


def quiet():
    evidence = []
    for i in range(2):
        processes = compiler_processes()
        evidence.append({"time_ns": time.time_ns(), "compiler_processes": processes})
        if processes:
            raise RuntimeError("Heavy compiler/build processes are active; no timing started: " + str(processes))
        if not i:
            time.sleep(2)
    return evidence


def summarize(rows, active=True):
    result = {}
    keys = ["total_us", "update_us", "input_us", "render_us", "copy_us", "render_including_copy_us", "dirty_pixels"]
    for key in keys:
        values = sorted(float(r[key]) for r in rows)
        result[key] = {"p50": values[math.ceil(len(values)*.50)-1], "p95": values[math.ceil(len(values)*.95)-1],
                       "max": values[-1], "mean": statistics.fmean(values)}
    result["totals"] = {key: sum(int(r[key]) for r in rows) for key in
                        ("dirty_pixels", "dirty_rects", "flush_calls", "render_calls", "rendered_frames", "update_calls", "primitive_calls")}
    result["samples"] = len(rows)
    if active:
        rendered = [r for r in rows if int(r["rendered_frames"])]
        result["active_frames"] = summarize(rendered, False) if rendered else None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=400)
    parser.add_argument("--warmup", type=int, default=40)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--fine-counters", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This evidence requires Windows QPC")
    if not (100 <= args.frames <= 1000 and 0 <= args.warmup <= 100 and 1 <= args.repeats <= 10):
        parser.error("frames 100..1000, warmup 0..100, repeats 1..10")
    cc = shutil.which("gcc")
    if not cc:
        parser.error("gcc is required")
    sources = [ROOT / "tab5/tests/native_large_bench.c", ROOT / "tab5/main/tab5_native_port.c", ROOT / "tab5/main/tab5_backlight.c",
               ROOT / "tab5/main/native/native_ui.c", ROOT / "tab5/main/native/font_native_fallback14.c"]
    sources += [ROOT / "tab5/main" / n for n in ("tab5_ui.c", "tab5_measure.c", "tab5_ui_graph.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c")]
    fonts = sorted((ROOT / "tab5/main").glob("font_tab5_*.c"))
    sources += fonts
    headers = sorted((ROOT / "tab5/main").rglob("*.h")) + [ROOT / "tab5/tests/native_baseline_contract.h"]
    contents = {p.relative_to(ROOT).as_posix(): p.read_bytes() for p in sources + headers + [Path(__file__).resolve()]}
    hashes = {n: hashlib.sha256(data).hexdigest() for n, data in contents.items()}
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror=misleading-indentation", "-DTAB5_NATIVE_INPUT_ONLY=1",
             "-DNUI_COLOR_DEPTH=16", "-DNUI_PROFILE_PRIMITIVE_TIMING=0", f"-DNUI_PROFILE_FINE_COUNTERS={int(args.fine_counters)}"]
    version = subprocess.check_output([cc, "--version"], text=True).splitlines()[0]
    fingerprint = hashlib.sha256(json.dumps([version, flags, hashes], sort_keys=True).encode()).hexdigest()[:20]
    build = BASE / "build" / fingerprint
    snap = build / "source"
    for name, data in contents.items():
        p = snap / name
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists():
            p.write_bytes(data)
        assert p.read_bytes() == data
    flags += ["-I" + str(snap / "tab5/main"), "-I" + str(snap / "tab5/tests")]
    out = BASE / (time.strftime("%Y%m%d-%H%M%S") + f"-{time.time_ns()%1000000000:09d}")
    out.mkdir(parents=True)
    report = {"status": "building", "workload_contract": "native-large-v2; v1 numeric inputs and RGB565 mock-copy quantization",
              "compiler": cc, "compiler_version": version, "flags": flags, "host": platform.platform(), "cpu": platform.processor(),
              "source_sha256": hashes, "source_snapshot": str(snap), "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "fine_counters": int(args.fine_counters), "primitive_timing": 0,
              "frames": args.frames, "warmup": args.warmup, "repeats": args.repeats,
              "clock": "QueryPerformanceCounter, frequency queried once before timing", "units": "microseconds", "compiler_monitor_interval_ms": 250,
              "timing_boundary": "update + tick/queued input + render + synchronous exact RGB565 mock copy; generation/stats/IO/validation excluded",
              "limitations": ["Host CPU, not ESP32-P4/PSRAM/PPA/LCD latency", "17 new-geometry scenes per theme: no direct LVGL speed ratio", "CPU preemption can inflate max"]}

    def save():
        (out / "run.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        (BASE / "latest.json").write_text(json.dumps({"report": str(out / "run.json")}, indent=2), encoding="utf-8")

    def current():
        return all(sha(ROOT/n) == value for n, value in hashes.items()) and fonts == sorted((ROOT / "tab5/main").glob("font_tab5_*.c"))

    save()
    try:
        # Sequential compilation; measurements never overlap our compilation.
        objects = []
        for source in sources:
            obj = build / (hashlib.sha256(str(source).encode()).hexdigest()[:16] + ".o")
            if not obj.exists():
                subprocess.run([cc, *flags, "-c", str(snap / source.relative_to(ROOT)), "-o", str(obj)], check=True)
            objects.append(obj)
        exe = build / "native_large_bench.exe"
        rsp = build / "link.rsp"
        rsp.write_text("\n".join('"'+p.as_posix()+'"' for p in objects)+'\n-lm\n-o\n"'+exe.as_posix()+'"\n', encoding="utf-8")
        subprocess.run([cc, "@"+str(rsp)], check=True)
        assert current(), "Sources changed during build"
        if args.build_only:
            report.update(status="built_not_measured", executable=str(exe))
            save()
            print(out / "run.json")
            return 0
        report.update(status="running", executable=str(exe), quiet_checks=[])
        save()
        for repeat in range(args.repeats):
            report["quiet_checks"] += quiet()
            directory = out / f"repeat-{repeat+1}"
            directory.mkdir()
            command = [str(exe), str(args.warmup), str(args.frames), str(directory)]
            print(f"Quiet sequential repeat {repeat+1}/{args.repeats}: {args.frames} frames/scene", flush=True)
            stop = threading.Event()
            contamination = []

            def monitor():
                while not stop.wait(.25):
                    found = compiler_processes()
                    if found:
                        contamination.append({"time_ns": time.time_ns(), "compiler_processes": found})

            watcher = threading.Thread(target=monitor, daemon=True)
            watcher.start()
            try:
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240, check=False)
            finally:
                stop.set()
                watcher.join()
            (directory / "bench.log").write_bytes(result.stdout)
            print(result.stdout.decode("utf-8", errors="replace"), end="", flush=True)
            assert result.returncode == 0, f"Native benchmark contract failed ({result.returncode}); see {directory/'bench.log'}"
            after = compiler_processes()
            report["quiet_checks"].append({"time_ns": time.time_ns(), "compiler_processes": after})
            report["quiet_checks"] += contamination
            assert not contamination, "Build process detected during timing; reject contaminated run"
            assert not after, "Build process appeared at timing end; rerun quiet"
            assert current(), "Live source closure changed during timing"
        combined = {}
        per_repeat = {}
        for path in sorted(out.glob("repeat-*/*.csv")):
            with path.open(encoding="utf-8", newline="") as f:
                rows = list(csv.DictReader(f))
            combined.setdefault(path.stem, []).extend(rows)
            per_repeat.setdefault(path.stem, []).append(summarize(rows))
        assert len(combined) == 34
        report["scenes"] = {name: {**summarize(rows), "per_repeat": per_repeat[name]} for name, rows in combined.items()}
        report["contracts"] = [json.loads(p.read_text()) for p in sorted((out / "repeat-1").glob("*.json"))]
        assert all(r["partial_equals_full"] for r in report["contracts"])
        assert all(r["totals"]["rendered_frames"] == 0 for r in report["contracts"] if r["idle_asserted"])
        report.update(status="passed", source_current=current(), measured_samples=sum(len(rows) for rows in combined.values()), scene_theme_count=34)
        save()
        print("RESULT", out / "run.json", flush=True)
        for name in ("live_angles_dark", "live_graph_dark", "time_keyboard_dark", "idle_update_dark", "focus_move_dark", "angles_force_full_dark"):
            s = report["scenes"][name]
            print(name, "total_us", s["total_us"], "dirty_mean", s["dirty_pixels"]["mean"], "renders", s["totals"]["rendered_frames"])
        return 0
    except Exception as e:
        report.update(status="failed", error=str(e), source_current=current())
        save()
        print(str(e), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
