"""Headless real native RGB565 UI, input FIFO and scenario runner (no IDF).

python tab5/tools/native_large_emulator.py
python tab5/tools/native_large_emulator.py --scenario events.txt --out .tools/large/manual

Scenario: RESET light|dark, ROT signed_detents [interval_ms], PRESS, LONG,
HOLD milliseconds (LONG at 600ms, no duplicate PRESS), WAIT milliseconds,
TAP x y, DRAG x1 y1 x2 y2, CLICK visible label, CAPTURE name,
EXPECT action_enum total, QUIT.
Key aliases: UP/LEFT = ROT -1; DOWN/RIGHT = ROT +1; ENTER = PRESS;
ESC/HOLD600 = LONG. KEYDOWN ENTER, WAIT 600, KEYUP ENTER sends LONG at
600ms with no PRESS on release; release before 600ms sends PRESS.
Use --interactive to type commands in your existing terminal (no new window).
All interactive input is consumed in C through the production port queue.
This tool never opens a window, a socket, a browser, or a physical device.
"""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def port_check(compiler):
    """Existing port fault/DMA probes, now including the actual ESP queue path.

    Only BSP/FreeRTOS/PPA are host stubs. Production port source is unchanged;
    ESP_PLATFORM is defined after stub headers, without invoking IDF tooling.
    """
    import re
    spec = importlib.util.spec_from_file_location("native_port_probes", ROOT / "tab5/tests/test_native_port.py")
    probes = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probes)
    header = (ROOT / "tab5/main/tab5_native_port.h").read_text(encoding="utf-8")
    contract = header.split("#define TAB5_NATIVE_INPUT_CAPACITY", 1)[1].split("#ifndef TAB5_NATIVE_INPUT_ONLY", 1)[0]
    source = (ROOT / "tab5/main/tab5_native_port.c").read_text(encoding="utf-8")
    source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.M)
    adapter = r'''
static unsigned encoder_events;
void tab5_ui_input(tab5_ui_input_event_t event,int32_t delta,uint32_t ms){
    (void)delta;(void)ms;assert(ui_held);
    if(event!=TAB5_UI_INPUT_IDLE){encoder_events++;dirty=1;}
}
void tab5_ui_touch(int32_t x,int32_t y,bool pressed,uint32_t ms){
    (void)ms;nui_native_touch(x,y,pressed);
}
'''
    extra = r'''
 fresh_runtime();auto_complete=true;
 unsigned previous_encoder_events=encoder_events;
 assert(tab5_native_port_post_input(TAB5_UI_INPUT_ROT,1,1000));
 assert(encoder_events==previous_encoder_events); /* producer never calls UI */
 run_loop(12);
 assert(encoder_events==previous_encoder_events+1&&stats.interactive_renders==1);
 tab5_native_input_stats_t q;tab5_native_port_input_stats(&q);
 assert(q.posted==1&&q.consumed==1&&!q.pending&&!q.rejected);
 puts("native ESP queue path: producer isolated, UI lock consumer, encoder interactive render PASS (host stubs)");
'''
    tests = probes.TEST.replace(" startup_cleanup(true,true);started=false;\n assert(!live", extra + " startup_cleanup(true,true);started=false;\n assert(!live")
    stub = re.sub(r"^void tab5_ui_(?:touch|input)[^\n]*\n", "", probes.STUB, flags=re.M)
    combined = stub + "\n#define ESP_PLATFORM 1\n" + adapter + source + tests
    out = ROOT / ".tools/native-large/port-check"
    out.mkdir(parents=True, exist_ok=True)
    cfile, exe = out / "port.c", out / "port.exe"
    cfile.write_text(combined, encoding="utf-8")
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-misleading-indentation", "-I" + str(ROOT / "tab5/main"), str(cfile), str(ROOT / "tab5/main/tab5_backlight.c"), "-o", str(exe)], check=True)
    result = subprocess.run([str(exe)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    (out / "port.log").write_bytes(result.stdout)
    print(result.stdout.decode("utf-8", errors="replace"), end="")
    (out / "run.json").write_text(json.dumps({"status": "passed" if result.returncode == 0 else "failed", "returncode": result.returncode,
        "evidence": "host BSP/FreeRTOS/PPA stubs, actual ESP_PLATFORM port and input queue; not a target IDF build",
        "source_sha256": digest(ROOT / "tab5/main/tab5_native_port.c"), "header_sha256": digest(ROOT / "tab5/main/tab5_native_port.h")}, indent=2), encoding="utf-8")
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".tools/native-large/captures")
    parser.add_argument("--scenario", type=Path, help="UTF-8 line protocol; default runs both-theme action suite")
    parser.add_argument("--interactive", action="store_true", help="Read commands in the existing terminal; QUIT exits; no GUI is opened")
    parser.add_argument("--compiler", default=shutil.which("gcc"))
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--pages-only", action="store_true", help="Six pages in both themes, focus-free and separate MENU frames (24 PNGs), each from a clean session")
    parser.add_argument("--require-current", action="store_true", help="Post-freeze gate: fail if live source hashes change during build or capture")
    parser.add_argument("--port-check", action="store_true", help="Run host fault/DMA/queue probes of the actual ESP_PLATFORM port source; no IDF")
    parser.add_argument("--timeout", type=int, default=240)
    args = parser.parse_args()
    if args.interactive and args.scenario:
        parser.error("Choose --interactive or --scenario")
    if not args.compiler:
        parser.error("gcc is required; no alternate renderer is used")
    if args.port_check:
        return port_check(args.compiler)
    main_dir = ROOT / "tab5/main"
    header = (main_dir / "tab5_ui.h").read_text(encoding="utf-8")
    if "tab5_ui_input_event_t" not in header:
        parser.error("UI agent has not installed the agreed tab5_ui_input API yet")
    sources = [ROOT / "tab5/tests/native_large_capture.c", main_dir / "tab5_native_port.c", main_dir / "tab5_backlight.c",
               main_dir / "native/native_ui.c", main_dir / "native/font_native_fallback14.c"]
    sources += [main_dir / n for n in ("tab5_ui.c", "tab5_measure.c", "tab5_ui_graph.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c")]
    font_sources = sorted(main_dir.glob("font_tab5_*.c"))
    sources += font_sources
    headers = sorted(main_dir.rglob("*.h"))
    contents = {str(p.relative_to(ROOT)): p.read_bytes() for p in sources + headers}
    closure = {name: hashlib.sha256(data).hexdigest() for name, data in contents.items()}
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-DTAB5_NATIVE_INPUT_ONLY=1",
             "-DNUI_COLOR_DEPTH=16", "-DNUI_PROFILE_FINE_COUNTERS=0", "-DNUI_PROFILE_PRIMITIVE_TIMING=0"]
    fingerprint = hashlib.sha256(json.dumps([args.compiler, flags, closure], sort_keys=True).encode()).hexdigest()[:20]
    build = ROOT / ".tools/native-large/build" / fingerprint
    build.mkdir(parents=True, exist_ok=True)
    snapshot = build / "source"
    for name, data in contents.items():
        path = snapshot / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    flags += ["-I" + str(snapshot / "tab5/main")]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Isolate each execution, so stale PNGs can never satisfy a new run.
    run_dir = out / (time.strftime("%Y%m%d-%H%M%S") + f"-{time.time_ns() % 1000000000:09d}")
    run_dir.mkdir(exist_ok=False)
    result = {"renderer": "production native RGB565", "size": [1280, 720],
              "input": "production tab5_native_port FIFO and UI-task consumer",
              "source_sha256": closure, "compiler": args.compiler, "flags": flags,
              "run_dir": str(run_dir), "snapshot": str(snapshot), "status": "building", "captures": []}
    report = run_dir / "run.json"

    def save():
        report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        (out / "latest.json").write_text(json.dumps({"report": str(report)}, indent=2), encoding="utf-8")

    def unchanged():
        return all(digest(ROOT / name) == value for name, value in closure.items()) and sorted(main_dir.glob("font_tab5_*.c")) == font_sources

    save()
    try:
        def compile_one(source):
            obj = build / (hashlib.sha256(str(source).encode()).hexdigest()[:20] + ".o")
            if not obj.exists():
                # Do not cache a partially written object if gcc fails.
                temporary = obj.with_suffix(f".{os.getpid()}.tmp.o")
                subprocess.run([args.compiler, *flags, "-c", str(snapshot / source.relative_to(ROOT)), "-o", str(temporary)], check=True)
                temporary.replace(obj)
            return obj

        print("Host GCC only; building real native RGB565 UI and queue", flush=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(6, os.cpu_count() or 2)) as pool:
            objects = list(pool.map(compile_one, sources))
        exe = build / "native_large_capture.exe"
        rsp = build / "link.rsp"
        rsp.write_text("\n".join('"' + p.as_posix() + '"' for p in objects) + '\n-lm\n-o\n"' + exe.as_posix() + '"\n', encoding="utf-8")
        subprocess.run([args.compiler, "@" + str(rsp)], check=True)
        result["source_current_at_start"] = unchanged()
        result["executable"] = str(exe)
        result["command"] = [str(exe), str(run_dir), "protocol" if args.scenario or args.interactive else "pages" if args.pages_only else "suite"]
        if args.build_only:
            result["status"] = "built_not_executed"
            save()
            print(exe)
            return 0
        result["status"] = "running"
        if args.scenario:
            scenario = args.scenario.read_bytes()
            result["scenario_sha256"] = hashlib.sha256(scenario).hexdigest()
            (run_dir / "scenario.txt").write_bytes(scenario)
        else:
            scenario = None
        save()
        if args.interactive:
            print("Commands: UP DOWN LEFT RIGHT ENTER ESC; KEYDOWN ENTER / WAIT 600 / KEYUP ENTER; TAP x y; CAPTURE name; RESET light|dark; QUIT", flush=True)
            run = subprocess.run(result["command"], check=False)
            (run_dir / "native.log").write_text("Interactive terminal session; events and assertions are recorded in events.jsonl\n", encoding="utf-8")
        else:
            run = subprocess.run(result["command"], input=scenario, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
            (run_dir / "native.log").write_bytes(run.stdout)
            print(run.stdout.decode("utf-8", errors="replace"), end="")
        result["returncode"] = run.returncode
        for ppm in sorted(run_dir.glob("*.ppm")):
            with Image.open(ppm) as im:
                im.load()
                if im.size != (1280, 720):
                    raise RuntimeError("Unexpected capture geometry")
                png = ppm.with_suffix(".png")
                im.save(png)
                result["captures"].append({"name": png.name, "sha256": digest(png),
                                           "rgb_sha256": hashlib.sha256(im.convert("RGB").tobytes()).hexdigest()})
        if result["captures"]:
            sheet = Image.new("RGB", (1280, 205 * ((len(result["captures"]) + 3) // 4)), "#e9edf4")
            draw = ImageDraw.Draw(sheet)
            for i, row in enumerate(result["captures"]):
                with Image.open(run_dir / row["name"]) as im:
                    im.thumbnail((320, 180))
                    x, y = i % 4 * 320, i // 4 * 205
                    sheet.paste(im, (x, y))
                    draw.text((x + 3, y + 183), row["name"], fill="#101827")
            sheet.save(run_dir / "contact_sheet.png")
        result["source_current_at_end"] = unchanged()
        if not result["source_current_at_start"] or not result["source_current_at_end"]:
            result["warning"] = "Live sources changed: captures belong to the recorded source snapshot; rerun after freeze for current validation"
        current = result["source_current_at_start"] and result["source_current_at_end"]
        result["source_current"] = current
        result["status"] = ("passed" if current else "passed_snapshot_only") if run.returncode == 0 else "failed"
        if args.require_current and not current:
            result["status"] = "failed_source_changed"
        result["visual_acceptance"] = "requires comparison to ENCODER.md and reference PNGs; not pixel-parity certified"
        save()
        print(f"{result['status']}: {len(result['captures'])} captures; {report}")
        return 0 if run.returncode == 0 and (current or not args.require_current) else 1
    except Exception as error:
        result.update(status="failed", error=str(error))
        save()
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
