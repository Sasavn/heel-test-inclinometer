"""Bounded production native UI regression; no windows, network or device.

--old-source compiles the exact same probe against saved original UI bytes and
requires a behavioral assertion failure as a negative control. Shared renderer,
fonts, input FIFO and backlight sources are compiled once, then linked into each
UI variant. All build/evidence output stays below --out.
"""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import shutil
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "tab5/main"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--old-source", type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("gcc")
    if not compiler:
        parser.error("gcc is required")
    sources = [MAIN / s for s in (
        "tab5_native_port.c", "tab5_backlight.c", "tab5_ui_graph.c",
        "tab5_measure.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c",
        "native/native_ui.c", "native/font_native_fallback14.c")]
    sources += sorted(MAIN.glob("font_tab5_*.c"))
    probe = ROOT / "tab5/tests/test_popup_focus.c"
    ui = MAIN / "tab5_ui.c"
    tracked = [*sources, probe, ui, Path(__file__), *MAIN.rglob("*.h")]
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    flags = ["-std=c11", "-O2", "-Werror=misleading-indentation",
             "-DTAB5_NATIVE_INPUT_ONLY=1", "-DTAB5_RELEASE_CAPTURE_BUILD=1",
             "-DNUI_PROFILE_FINE_COUNTERS=0", "-DNUI_PROFILE_PRIMITIVE_TIMING=0",
             "-I" + str(MAIN)]

    def run(command, label, timeout=60):
        result = subprocess.run(command, capture_output=True, timeout=timeout)
        (out / (label + ".log")).write_bytes(result.stdout + result.stderr)
        return result

    def compile_shared(item):
        i, source = item
        obj = out / f"popup_focus_shared_{i}.o"
        result = run([compiler, *flags, "-c", str(source), "-o", str(obj)], f"compile_shared_{i}")
        if result.returncode:
            raise RuntimeError(f"Compile failed: {source}; see {out}")
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_shared, enumerate(sources)))
    results = {}
    variants = [("new", ui)]
    if args.old_source:
        variants.insert(0, ("old", args.old_source.resolve()))
    for name, source in variants:
        exe = out / f"popup_focus_{name}.exe"
        define = '-DPOPUP_FOCUS_UI_SOURCE="' + source.as_posix() + '"'
        result = run([compiler, *flags, define, str(probe), *map(str, objects),
                      "-lm", "-o", str(exe)], "compile_" + name)
        if result.returncode:
            raise RuntimeError(f"{name} compile failed; see {out}")
        captures = out / ("frames_" + name)
        captures.mkdir(exist_ok=True)
        result = run([str(exe), str(captures)], "run_" + name, timeout=45)
        output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        results[name] = {"returncode": result.returncode, "output": output,
                         "ui_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
        print(name.upper(), "PASS" if result.returncode == 0 else "FAIL", output.strip(), flush=True)
    stable = all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p, h in before.items())
    negative = (not args.old_source or (results["old"]["returncode"] != 0 and
                "Assertion failed" in results["old"]["output"]))
    passed = stable and negative and results["new"]["returncode"] == 0
    frames = {}
    for raw in sorted((out / "frames_new").glob("*.rgb565")):
        data = raw.read_bytes()
        assert len(data) == 1280 * 720 * 2
        png = raw.with_suffix(".png")
        Image.frombytes("RGB", (1280,720), data, "raw", "BGR;16").save(png)
        frames[png.name] = {"rgb565_sha256": hashlib.sha256(data).hexdigest(),
                            "png_sha256": hashlib.sha256(png.read_bytes()).hexdigest()}
    passed = passed and len(frames) == 16
    report = {"passed": passed, "source_stable": stable, "negative_control": negative,
              "results": results, "source_sha256": before, "compiler": compiler,
              "flags": flags, "frames": frames,
              "scope": "Host production UI/input + native RGB565 captures only; no hardware verification"}
    (out / "popup_focus_result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
