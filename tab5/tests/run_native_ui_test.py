"""Build/test the production native RGB565 UI without windows or hardware.

The frozen snapshot's run_ui_test.py belongs to the LVGL reference suite.
The working tree's legacy test entry points delegate to this native suite.
This entry point compiles main/native/native_ui.c, production UI and real fonts.
Optimization outputs/cache live under ignored .tools/native-opt/after.
Accepted native v1 is immutable; all 98 decoded RGB frames must match exactly.
Use --opt-group for isolated probes; full suite runs are reserved for gates.

Usage: python tab5/tests/run_native_ui_test.py [--baseline BASELINE_FRAMES]
Use --prepare-reference once to emit and validate exact reference manifests
from the frozen 369c7fc snapshot, without editing that snapshot. Default runs
use the separately frozen accepted native v1 geometry and immutable RGB frames.
--counter-mode equivalence runs diagnostic fine1 and release fine0 sequentially;
both disable primitive timing and must pass the same 98-state fidelity gate.
Baseline PNG names must match the original suite. Optional geometry_points.tsv
uses rows: case,path,type,x1,y1,x2,y2,font,line_height,base_line,text_w,text_h,text_hex.
textarea_points.tsv supplies actual semantic content records under /@text;
raw internal child labels are reported separately, never required as fake nodes.
Missing baseline evidence is reported as unavailable, never as a parity pass.
"""
import argparse
import concurrent.futures
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops, ImageDraw, ImageStat

ROOT = Path(__file__).resolve().parents[2]
PRIMARY_NATIVE_V1 = Path("C:/Projects/ClaudeExp/tab5-feedback/2026-10-22-native-opt/native-v1-reference/captures/native")


def compare_native_v1(out, baseline, captures):
    """Delivered immutable PNG hashes identify reference files; RGB bytes gate parity."""
    index = baseline.parents[1] / "tab5/docs/native/frames.json"
    if not index.exists():
        raise ValueError(f"Missing immutable native v1 frame index: {index}")
    manifest = json.loads(index.read_text(encoding="utf-8"))
    expected = {r["name"]: r for r in manifest["frames"]}
    if manifest["count"] != 98 or len(expected) != 98:
        raise ValueError("Native v1 must contain the 98 accepted states")
    actual = {p.name: p for p in captures}
    result = {"reference": str(baseline), "reference_index_sha256": hashlib.sha256(index.read_bytes()).hexdigest(),
              "accepted_commit": "c4cf2cf0410d89e588b908525a2ea288e18ebec1", "frames": [],
              "missing": sorted(expected.keys()-actual.keys()), "unexpected": sorted(actual.keys()-expected.keys())}
    for name, entry in expected.items():
        ref = baseline / name
        if hashlib.sha256(ref.read_bytes()).hexdigest() != entry["sha256"]:
            raise ValueError(f"Immutable reference PNG changed: {name}")
        if name not in actual:
            continue
        with Image.open(ref) as a, Image.open(actual[name]) as b:
            a, b = a.convert("RGB"), b.convert("RGB")
            row = {"name": name, "accepted_rgb_sha256": hashlib.sha256(a.tobytes()).hexdigest(),
                   "optimized_rgb_sha256": hashlib.sha256(b.tobytes()).hexdigest()}
            row["equal"] = a.size == b.size and row["accepted_rgb_sha256"] == row["optimized_rgb_sha256"]
            if a.size == b.size and not row["equal"]:
                delta = ImageChops.difference(a, b)
                row["changed_pixels"] = sum(v != (0, 0, 0) for v in delta.getdata())
                row["difference_bbox"] = delta.getbbox()
                row["mean_absolute_channel_error"] = sum(ImageStat.Stat(delta).mean)/3
            result["frames"].append(row)
    result["status"] = "PASS" if not result["missing"] and not result["unexpected"] and len(result["frames"]) == 98 and all(r["equal"] for r in result["frames"]) else "FAIL"
    (out / "native_v1_pixel_diff.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


def counter_mode_equivalence(args, out, baseline):
    """Two real production renderer builds, identical fixture inputs and source hashes."""
    statuses = {}
    for mode in ("diagnostic", "release"):
        command = [sys.executable, str(Path(__file__).resolve()), "--counter-mode", mode,
                   "--out", str(out / mode), "--baseline", str(baseline),
                   "--native-v1", str(args.native_v1.resolve()), "--benchmark-gate", str(args.benchmark_gate.resolve())]
        if args.baseline_points:
            command += ["--baseline-points", str(args.baseline_points.resolve())]
        statuses[mode] = subprocess.run(command, check=False).returncode
        if statuses[mode] != 0:
            (out / "counter_mode_equivalence.json").write_text(json.dumps({"status": "FAIL", "runs": statuses}, indent=2), encoding="utf-8")
            return statuses[mode]
    runs = {mode: json.loads((out / mode / "run.json").read_text(encoding="utf-8")) for mode in statuses}
    stable = runs["diagnostic"]["source_sha256"] == runs["release"]["source_sha256"] and runs["diagnostic"]["header_sha256"] == runs["release"]["header_sha256"]
    names = set(runs["diagnostic"]["captures"]) | set(runs["release"]["captures"])
    frames = []
    for name in sorted(names):
        paths = [out / mode / name for mode in statuses]
        hashes = []
        for path in paths:
            with Image.open(path) as im:
                hashes.append(hashlib.sha256(im.convert("RGB").tobytes()).hexdigest())
        frames.append({"name": name, "diagnostic_rgb_sha256": hashes[0], "release_rgb_sha256": hashes[1], "equal": hashes[0] == hashes[1]})
    v1_exact = all(r["native_v1_exact_rgb"] == "PASS" for r in runs.values())
    passed = stable and v1_exact and len(frames) == 98 and all(r["equal"] for r in frames)
    report = {"status": "PASS" if passed else "FAIL", "source_closure_identical": stable, "frames": frames,
              "diagnostic_flags": runs["diagnostic"]["flags"], "release_flags": runs["release"]["flags"],
              "both_modes_native_v1_exact_rgb": v1_exact}
    (out / "counter_mode_equivalence.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"COUNTER MODE EQUIVALENCE {report['status']}: diagnostic fine1 / release fine0, timing0, {len(frames)} frames, identical source closure={stable}")
    return 0 if passed else 1


def compare_input_contracts(out):
    actual_path = out / "input_contracts.jsonl"
    files = {kind: ROOT / f".tools/accepted-v1/{kind}-defaults.json" for kind in ("keyboard", "textarea")}
    if not actual_path.exists() or any(not p.exists() for p in files.values()):
        return {"status": "unavailable", "missing": [str(p) for p in [actual_path, *files.values()] if not p.exists()]}
    records = [json.loads(line) for line in actual_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    actual = {}
    for row in records:
        key = (row["kind"], row["theme"], row["context"], row.get("index", 0))
        if key in actual:
            raise ValueError(f"Duplicate input contract state: {key}")
        actual[key] = row
    differences, expected_keys = [], set()
    for kind, path in files.items():
        reference = json.loads(path.read_text(encoding="utf-8"))
        if reference["commit"] != "369c7fcd435ffd09631e49c0118e102c66ea4d75":
            raise ValueError(f"Unexpected {kind} defaults baseline")
        for theme in (0, 1):
            for row in reference["rows"]:
                key = (kind, theme, row["context"], row.get("index", 0))
                expected_keys.add(key)
                got = actual.get(key)
                if got is None:
                    differences.append({"state": key, "field": "state", "baseline": "present", "native": "missing"})
                    continue
                if kind == "keyboard":
                    expected = {"keyboard_rect": row["keyboard_rect"], "keys": [
                        {"id": k["id"], "screen_rect": k["screen_rect"], "width": k["width"], "height": k["height"],
                         "text_hex": k["text"].encode("utf-8").hex()} for k in row["keys"]]}
                else:
                    expected = {field: row[field] for field in ("rect", "width", "height", "pad_left", "pad_right",
                        "pad_top", "pad_bottom", "border_width", "radius", "opa", "bg_opa", "line_height", "base_line", "font_is_tab5_24")}
                for field, value in expected.items():
                    if got.get(field) != value:
                        if field == "keys":
                            for i in range(max(len(value), len(got.get(field, [])))):
                                a = value[i] if i < len(value) else None
                                b = got[field][i] if i < len(got[field]) else None
                                if a != b:
                                    differences.append({"state": key, "field": f"keys[{i}]", "baseline": a, "native": b})
                        else:
                            differences.append({"state": key, "field": field, "baseline": value, "native": got.get(field)})
    for key in actual.keys() - expected_keys:
        differences.append({"state": key, "field": "state", "baseline": "absent", "native": "unexpected"})
    result = {"status": "equal" if not differences else "different", "expected_states": len(expected_keys),
              "actual_states": len(actual), "differences": differences,
              "baseline_sha256": {kind: hashlib.sha256(path.read_bytes()).hexdigest() for kind, path in files.items()}}
    (out / "input_contract_diff.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


def prepare_reference(out, baseline):
    """Instrument a generated diagnostic copy; never write the frozen snapshot."""
    frozen = ROOT / ".tools/native-baseline/snapshot"
    snapshot = json.loads((ROOT / ".tools/native-baseline/snapshot.json").read_text(encoding="utf-8"))
    if snapshot["commit"] != "369c7fcd435ffd09631e49c0118e102c66ea4d75":
        raise ValueError("Unexpected reference commit")
    scratch = out / "reference"
    scratch.mkdir(parents=True, exist_ok=True)
    old = (frozen / "tab5/tests/test_ui.c").read_text(encoding="utf-8")
    new = (ROOT / "tab5/tests/test_native_ui.c").read_text(encoding="utf-8")
    start = new.index("/* Stable public-tree manifest:")
    end = new.index("static void capture(", start)
    helper = new[start:end].replace("nui_", "lv_").replace("NUI_", "LV_")
    wrappers = """static unsigned lv_buttonmatrix_get_button_count(lv_obj_t *k){return ((lv_buttonmatrix_t*)k)->btn_cnt;}
static bool lv_buttonmatrix_get_button_area(lv_obj_t *k,unsigned i,lv_area_t *out){lv_buttonmatrix_t *m=(lv_buttonmatrix_t*)k;if(i>=m->btn_cnt)return false;*out=m->button_areas[i];return true;}
"""
    old = old.replace("static FILE *report;", "static FILE *report,*points,*textarea_points;")
    old = old.replace("static void capture(const char *name){", wrappers+helper+"static void capture(const char *name){")
    old = old.replace('fprintf(report,"CASE %s\\n",name);geometry(name);',
                      'dump_points(lv_screen_active(),name,"0");fflush(textarea_points);fflush(points);fprintf(report,"CASE %s\\n",name);geometry(name);')
    old = old.replace('    rgb565_mode=getenv(',
                      '    snprintf(path,sizeof(path),"%s/geometry_points.tsv",output);points=fopen(path,"w");assert(points);\n    snprintf(path,sizeof(path),"%s/textarea_points.tsv",output);textarea_points=fopen(path,"w");assert(textarea_points);\n    rgb565_mode=getenv(')
    old = old.replace("fclose(report);", "fclose(textarea_points);fclose(points);fclose(report);")
    probe = scratch / "test_reference_geometry.c"
    probe.write_text("#define TAB5_REFERENCE_PROBE 1\n"+old, encoding="utf-8", newline="\n")
    runner = (frozen / "tab5/tests/run_ui_test.py").read_text(encoding="utf-8")
    runner = runner.replace("ROOT = Path(__file__).resolve().parents[2]", "ROOT = Path("+repr(str(frozen))+")")
    runner = runner.replace("ROOT/'tab5'/'tests'/'test_ui.c'", "Path("+repr(str(probe))+")")
    script = scratch / "run_reference_geometry.py"
    script.write_text(runner, encoding="utf-8", newline="\n")
    frames = scratch / "frames"
    subprocess.run([sys.executable, str(script), "--rgb565", "--out", str(frames)], check=True)
    files = sorted(p for p in frames.glob("*.png") if p.name != "contact_sheet.png")
    expected = {p.name for p in baseline.glob("*.png") if p.name != "contact_sheet.png"}
    if {p.name for p in files} != expected or len(files) != 98:
        raise ValueError("Reference probe did not reproduce all 98 frozen captures")
    for path in files:
        with Image.open(path) as a, Image.open(baseline / path.name) as b:
            if a.size != b.size or ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getbbox():
                raise ValueError(f"Reference diagnostic probe changed pixels: {path.name}")
    manifest = frames / "geometry_points.tsv"
    report = {"commit": snapshot["commit"], "captures": len(files), "pixel_identical_to_frozen": True,
              "geometry_points": len(manifest.read_text(encoding="utf-8").splitlines()),
              "manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest()}
    (scratch / "probe_validation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Frozen reference verified: {len(files)} exact captures; manifest {manifest}")


def compare(out, baseline, captures, baseline_points=None, suite_returncode=0, native_v1=None):
    result = {"baseline": str(baseline), "captures": [], "missing_baseline": [],
              "missing_native": [], "geometry": {"status": "unavailable"},
              "suite_returncode": suite_returncode}
    diffdir = out / "diff"
    diffdir.mkdir(exist_ok=True)
    for path in captures:
        ref = baseline / path.name
        if not ref.exists():
            result["missing_baseline"].append(path.name)
            continue
        with Image.open(ref) as old, Image.open(path) as new:
            old, new = old.convert("RGB"), new.convert("RGB")
            item = {"name": path.name, "baseline_size": list(old.size), "native_size": list(new.size)}
            if old.size != new.size:
                item["status"] = "size_mismatch"
            else:
                delta = ImageChops.difference(old, new)
                changed = sum(v != (0, 0, 0) for v in delta.getdata())
                item.update(status="equal" if changed == 0 else "different",
                            changed_pixels=changed, changed_fraction=changed/(old.width*old.height),
                            mean_absolute_channel_error=sum(ImageStat.Stat(delta).mean)/3,
                            maximum_channel_error=max(hi for lo, hi in delta.getextrema()),
                            difference_bbox=delta.getbbox())
                if changed:
                    delta.save(diffdir / path.name)
            result["captures"].append(item)
    names = {p.name for p in captures}
    result["missing_native"] = sorted(p.name for p in baseline.glob("*.png")
                                      if p.name != "contact_sheet.png" and p.name not in names)
    oldpoints = baseline_points or baseline / "geometry_points.tsv"
    newpoints = out / "geometry_points.tsv"
    result["geometry"]["baseline_manifest"] = str(oldpoints)
    if oldpoints.exists() and newpoints.exists():
        def rows(path):
            with path.open(encoding="utf-8", newline="") as f:
                records = list(csv.reader(f, delimiter="\t"))
            if any(len(r) != 13 for r in records):
                raise ValueError(f"Invalid 13-column geometry manifest: {path}")
            indexed = {(r[0], r[1]): r[2:] for r in records}
            if len(indexed) != len(records):
                raise ValueError(f"Duplicate geometry point keys: {path}")
            return indexed
        before, after = rows(oldpoints), rows(newpoints)
        fields = ["type", "x1", "y1", "x2", "y2", "font", "line_height", "base_line", "text_w", "text_h", "text_hex"]

        def point_diff(left, right):
            differences = []
            for key in sorted(left.keys() | right.keys()):
                a, b = left.get(key), right.get(key)
                if a != b:
                    differences.append({"case": key[0], "path": key[1],
                                        "changes": {field: {"baseline": a[i] if a else None,
                                                            "native": b[i] if b else None}
                                                    for i, field in enumerate(fields)
                                                    if a is None or b is None or a[i] != b[i]}})
            return {"status": "equal" if not differences else "different",
                    "baseline_points": len(left), "native_points": len(right),
                    "field_mismatch_counts": dict(Counter(k for r in differences for k in r["changes"])),
                    "differences": differences}

        result["raw_tree_geometry"] = point_diff(before, after)

        def semantic(raw, content_file):
            # Only an actual direct textarea child label is structural. Keep
            # every other tree node, coordinate, font and string unchanged.
            internal = {key for key, value in raw.items() if value[0] == "label"
                        and raw.get((key[0], key[1].rsplit("/", 1)[0]), [None])[0] == "textarea"}
            normalized = {k: v for k, v in raw.items() if k not in internal}
            content = rows(content_file)
            expected = {(case, path+"/@text") for (case, path), value in raw.items() if value[0] == "textarea"}
            invalid = sorted(expected.symmetric_difference(content))
            invalid += sorted(key for key, value in content.items() if value[0] != "textarea_content"
                              or "-2147483648" in value[1:5]
                              or value[5] in ("-", "other") or int(value[6]) <= 0)
            normalized.update(content)
            return normalized, sorted(internal), invalid

        oldcontent = oldpoints.with_name("textarea_points.tsv")
        newcontent = newpoints.with_name("textarea_points.tsv")
        if oldcontent.exists() and newcontent.exists():
            left, removed_old, invalid_old = semantic(before, oldcontent)
            right, removed_new, invalid_new = semantic(after, newcontent)
            result["geometry"] = point_diff(left, right)
            unavailable = [key for key, value in right.items()
                           if value[0] == "textarea_content" and "-2147483648" in value[1:5]]
            if unavailable or invalid_old or invalid_new:
                result["geometry"]["status"] = "unavailable_content_evidence"
            result["structure_exceptions"] = {
                "rule": "Textarea internal labels replaced by actual semantic content records; no fake children and no tolerance",
                "baseline_internal_labels": removed_old, "native_internal_labels": removed_new,
                "baseline_content_invalid_or_missing": invalid_old,
                "native_content_invalid_or_missing": invalid_new,
                "native_content_bounds_unavailable": unavailable}
        else:
            result["geometry"] = {"status": "unavailable_semantic_content",
                                  "missing_manifests": [str(p) for p in (oldcontent, newcontent) if not p.exists()]}
        expected_cases = {p.stem for p in captures}
        missing_cases = {"baseline": sorted(expected_cases - {k[0] for k in before}),
                         "native": sorted(expected_cases - {k[0] for k in after})}
        unknown_fonts = {"baseline": [k for k, v in before.items() if v[0] in ("label", "key") and v[5] in ("-", "other")],
                         "native": [k for k, v in after.items() if v[0] in ("label", "key") and v[5] in ("-", "other")]}
        result["manifest_evidence"] = {"missing_cases": missing_cases, "unknown_fonts": unknown_fonts}
        if any(missing_cases.values()):
            result["geometry"]["status"] = "unavailable_case_coverage"
        if any(unknown_fonts.values()):
            result["geometry"]["status"] = "unavailable_font_evidence"
    text_groups = Counter()
    for row in result["geometry"].get("differences", []):
        change = row["changes"].get("text_hex")
        if change:
            text_groups[tuple(bytes.fromhex(change[side]).decode("utf-8") if change[side] is not None else None
                              for side in ("baseline", "native"))] += 1
    result["semantic_text_mismatch_groups"] = [{"baseline": left, "native": right, "points": count}
                                               for (left, right), count in text_groups.most_common()]
    result["input_contracts"] = compare_input_contracts(out)
    result["automated_contracts_pass"] = suite_returncode == 0 and bool(result["captures"]) and not result["missing_baseline"] and not result["missing_native"] and all(c["status"] != "size_mismatch" for c in result["captures"]) and result["geometry"]["status"] == "equal" and result["input_contracts"]["status"] == "equal"
    if native_v1 is not None:
        result["native_v1_exact_rgb"] = native_v1["status"]
        result["automated_contracts_pass"] = result["automated_contracts_pass"] and native_v1["status"] == "PASS"
    result["pixel_exact"] = bool(result["captures"]) and all(c["status"] == "equal" for c in result["captures"])
    result["exact_parity"] = result["automated_contracts_pass"] and result["pixel_exact"]
    result["automated_status"] = "PASS" if result["automated_contracts_pass"] else "FAIL"
    result["manual_acceptance"] = ({"status": "not_a_substitute_for_native_v1_exact_rgb", "automatic": False,
        "policy": "Optimization stage requires exact decoded RGB vs accepted c4cf2cf v1. No new AA/manual waiver."} if native_v1 is not None else
        {"status": "external_review", "automatic": False, "policy": "Legacy LVGL comparison is informational; optimization gate separately requires native v1 exact RGB."})
    result["exact_geometry_difference_count"] = len(result["geometry"].get("differences", [])) if result["geometry"]["status"] in ("equal", "different") else None
    (out / "pixel_diff.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    lines = ["# Native RGB565 comparison", "", f"Baseline: {baseline}",
             f"Compared captures: {len(result['captures'])}",
             f"Missing baseline/native: {len(result['missing_baseline'])}/{len(result['missing_native'])}",
             f"Raw tree geometry/font points: {result.get('raw_tree_geometry', {}).get('status', 'unavailable')}",
             f"Exact semantic geometry/font points: {result['geometry']['status']}",
             f"All-mode keyboard/textarea default contracts: {result['input_contracts']['status']}",
             f"AUTOMATED CONTRACTS: {result['automated_status']}",
             f"Exact semantic geometry differences: {result['exact_geometry_difference_count']}",
             f"Pixel-exact identity (informational): {result['pixel_exact']}",
             f"Accepted native v1 exact RGB gate: {result.get('native_v1_exact_rgb', 'not checked in this diagnostic comparison')}",
             "No manual/AA waiver applies to optimization-stage native v1 differences.", "",
             "| Capture | Changed pixels | Mean absolute channel error |", "|---|---:|---:|"]
    for item in result["captures"]:
        lines.append(f"| {item['name']} | {item.get('changed_pixels', item['status'])} | {item.get('mean_absolute_channel_error', '-')} |")
    lines += ["", "Textarea child labels are implementation structure: raw differences remain in raw_tree_geometry. Semantic comparison replaces those labels with actual textarea content bounds/font/raw text metrics. Missing renderer introspection is unavailable evidence, never a pass."]
    lines += ["", "Semantic text mismatch groups:"]
    for group in result["semantic_text_mismatch_groups"]:
        lines.append("- "+json.dumps(group, ensure_ascii=True))
    for row in result["geometry"].get("differences", [])[:20]:
        lines.append(f"- {row['case']} `{row['path']}`: "+json.dumps(row["changes"], ensure_ascii=True))
    lines += ["", "See pixel_diff.json for exact point changes and missing evidence; diff/*.png contains unamplified absolute RGB differences."]
    (out / "pixel_diff.md").write_text("\n".join(lines)+"\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".tools/native-opt/after")
    parser.add_argument("--baseline", type=Path, default=ROOT / ".tools/accepted-v1/frames", help="Frozen accepted native semantic/geometry reference; old LVGL baseline stays separate")
    parser.add_argument("--native-v1", type=Path, default=PRIMARY_NATIVE_V1, help="Primary immutable delivered v1 captures; all 98 decoded RGB frames must match exactly")
    parser.add_argument("--opt-group", choices=["press", "dirty", "cache-state", "lifetime", "cache-budget", "all"], help="Run isolated real renderer optimization probes instead of the full 98-state gate")
    parser.add_argument("--benchmark-gate", type=Path, default=ROOT / ".tools/native-opt/BENCHMARK_GATE", help="If this marker exists, refuse build/run; parent owns timing gate")
    parser.add_argument("--counter-mode", choices=["diagnostic", "release", "equivalence"], default="diagnostic", help="Explicit fine1 functional/probe assertions, fine0 release captures, or both exact-equivalence 98-state gates; detailed timing always OFF")
    parser.add_argument("--baseline-points", type=Path, help="Exact 13-column geometry/font manifest from frozen reference diagnostic probe")
    parser.add_argument("--prepare-reference", action="store_true", help="Generate geometry/font probe outside frozen snapshot and verify its 98 PNGs against baseline; does not build native")
    parser.add_argument("--bench-only", action="store_true")
    parser.add_argument("--rgb565", action="store_true", help="Compatibility flag: native always uses production RGB565")
    parser.add_argument("--compare-only", action="store_true", help="Compare existing captures; never compiles or executes")
    parser.add_argument("--require-parity", action="store_true", help="Compatibility flag: native-v1 exact RGB and exact geometry/semantic/input contracts are mandatory for this optimization stage")
    args = parser.parse_args()
    out, baseline = args.out.resolve(), args.baseline.resolve()
    if args.benchmark_gate.exists() and not args.compare_only:
        parser.error(f"Benchmark gate active: {args.benchmark_gate}; no build or test process started")
    if out == baseline or out.is_relative_to(baseline):
        parser.error("Output must be outside the frozen baseline")
    for protected in (ROOT / ".tools/accepted-v1", ROOT / ".tools/native-baseline", args.native_v1.resolve().parents[1]):
        if out == protected.resolve() or out.is_relative_to(protected.resolve()):
            parser.error(f"Output must not modify immutable reference: {protected}")
    out.mkdir(parents=True, exist_ok=True)
    if args.counter_mode == "equivalence":
        if args.opt_group or args.bench_only or args.compare_only or args.prepare_reference:
            parser.error("Counter equivalence is an explicit two-mode full gate, not an isolated probe/benchmark")
        return counter_mode_equivalence(args, out, baseline)
    if args.counter_mode == "release" and args.opt_group:
        parser.error("Optimization primitive/cache probes require diagnostic fine1; release mode is capture equivalence only")
    if args.prepare_reference:
        prepare_reference(out, ROOT / ".tools/native-baseline/frames")
        return 0
    if args.baseline_points is None and baseline == (ROOT / ".tools/native-baseline/frames").resolve():
        probe_dir = ROOT / ".tools/native-after/reference"
        validation = probe_dir / "probe_validation.json"
        candidate = probe_dir / "frames/geometry_points.tsv"
        if validation.exists() and candidate.exists():
            evidence = json.loads(validation.read_text(encoding="utf-8"))
            if evidence.get("pixel_identical_to_frozen") is True and evidence.get("commit") == "369c7fcd435ffd09631e49c0118e102c66ea4d75" and evidence.get("manifest_sha256") == hashlib.sha256(candidate.read_bytes()).hexdigest():
                args.baseline_points = candidate
    status = 0
    capture_names = []
    if not args.compare_only:
        (out / "run.json").write_text(json.dumps({"status": "building", "captures": []}), encoding="utf-8")
        native = ROOT / "tab5/main/native/native_ui.c"
        compiler = shutil.which("gcc")
        if not compiler or not native.exists():
            parser.error("Need gcc on PATH and Huygens' main/native/native_ui.c; no LVGL fallback is used")
        sources = [ROOT / "tab5/tests/test_native_ui.c", native,
                   ROOT / "tab5/main/native/font_native_fallback14.c"]
        sources += [ROOT / "tab5/main" / name for name in
                    ("tab5_ui.c", "tab5_measure.c", "tab5_ui_graph.c", "tab5_time.c", "tab5_plot.c", "tab5_level.c")]
        sources += sorted((ROOT / "tab5/main").glob("font_tab5_*.c"))
        flags = ["-std=c11", "-O2", "-DNUI_COLOR_DEPTH=16", "-DNUI_PROFILE_PRIMITIVE_TIMING=0",
                 "-DNUI_PROFILE_FINE_COUNTERS="+("0" if args.counter_mode == "release" else "1"), "-I"+str(ROOT / "tab5/main")]
        if args.counter_mode == "release":
            flags.append("-DTAB5_RELEASE_CAPTURE_BUILD=1")
        if "nui_native_textarea_get_text_bounds" in native.with_suffix(".h").read_text(encoding="utf-8"):
            flags.append("-DNUI_TEST_HAS_TEXTAREA_BOUNDS=1")
        if "shadow_cache_capacity_bytes" in native.with_suffix(".h").read_text(encoding="utf-8"):
            flags.append("-DNUI_TEST_HAS_OPT_CACHE_STATS=1")
        headers = sorted((ROOT / "tab5/main").rglob("*.h"))
        source_hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        header_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in headers}
        fingerprint = hashlib.sha256((str(flags)+str(compiler)).encode())
        for header in headers:
            fingerprint.update(header.read_bytes())
        cache = out / "build" / fingerprint.hexdigest()[:16]
        cache.mkdir(parents=True, exist_ok=True)

        def compile_one(source):
            digest = hashlib.sha256(str(source).encode()+source.read_bytes()).hexdigest()
            obj = cache / (digest+".o")
            if not obj.exists():
                subprocess.run([compiler, *flags, "-c", str(source), "-o", str(obj)], check=True)
            return obj

        with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 2)) as pool:
            objects = list(pool.map(compile_one, sources))
        exe, rsp = cache / "test_native_ui.exe", cache / "link.rsp"
        rsp.write_text("\n".join('"'+str(p).replace("\\", "/")+'"' for p in objects)+f'\n-lm\n-o\n"{exe.as_posix()}"\n', encoding="utf-8")
        subprocess.run([compiler, "@"+str(rsp)], check=True)
        if any(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest for name, digest in source_hashes.items()) or any(hashlib.sha256(path.read_bytes()).hexdigest() != digest for path, digest in header_hashes.items()):
            parser.error("Production sources changed during build; rerun once the renderer edits settle")
        if args.benchmark_gate.exists():
            parser.error("Benchmark gate became active during build; test executable was not started")
        env = os.environ.copy()
        env.update(TAB5_TEST_RGB565="1", TAB5_TEST_BENCH_ONLY="1" if args.bench_only else "0", TAB5_OPT_GROUP=args.opt_group or "")
        started = time.time_ns()
        result = subprocess.run([str(exe), str(out)], env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180, check=False)
        (out / "native.log").write_bytes(result.stdout)
        print(result.stdout.decode("utf-8", errors="replace"), end="")
        if any(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest for name, digest in source_hashes.items()) or any(hashlib.sha256(path.read_bytes()).hexdigest() != digest for path, digest in header_hashes.items()):
            parser.error("Sources changed during execution; captures cannot certify the current source closure")
        status = result.returncode
        captures = sorted(p for p in out.glob("*.ppm") if p.stat().st_mtime_ns >= started)
        for ppm in captures:
            with Image.open(ppm) as im:
                im.save(ppm.with_suffix(".png"))
            capture_names.append(ppm.with_suffix(".png").name)
        (out / "run.json").write_text(json.dumps({"status": ("benchmark_passed" if args.bench_only else "actions_passed_fidelity_pending") if status == 0 else "failed", "renderer": "production native RGB565", "returncode": status,
                    "captures": capture_names, "compiler": compiler, "flags": flags, "counter_mode": args.counter_mode,
                    "source_sha256": source_hashes,
                    "header_sha256": {str(p.relative_to(ROOT)): digest for p, digest in header_hashes.items()}}, indent=2), encoding="utf-8")
        if args.opt_group:
            (out / "optimization_probes.json").write_text(json.dumps({"group": args.opt_group,
                "status": "PASS" if status == 0 else "FAIL", "returncode": status,
                "source_sha256": source_hashes, "log": "native.log"}, indent=2), encoding="utf-8")
            return status
    else:
        runfile = out / "run.json"
        if runfile.exists():
            run = json.loads(runfile.read_text(encoding="utf-8"))
            if "returncode" not in run or run.get("renderer") != "production native RGB565":
                parser.error("No completed real native execution in run.json")
            capture_names = run["captures"]
            status = run["returncode"]
        else:
            parser.error("--compare-only requires run.json from a real native run")
    captures = [out / name for name in capture_names]
    if captures:
        sheet = Image.new("RGB", (1536, 238*((len(captures)+3)//4)), "#e9edf4")
        draw = ImageDraw.Draw(sheet)
        for i, path in enumerate(captures):
            with Image.open(path) as im:
                im = im.convert("RGB"); im.thumbnail((384, 216))
                x, y = i % 4*384, i//4*238
                sheet.paste(im, (x, y)); draw.text((x+4, y+219), path.stem, fill="#1a1d24")
        sheet.save(out / "contact_sheet.png")
    if not args.bench_only:
        native_v1 = compare_native_v1(out, args.native_v1.resolve(), captures)
        report = compare(out, baseline, captures,
                         args.baseline_points.resolve() if args.baseline_points else None, status, native_v1)
        print(f"Functional suite returncode={report['suite_returncode']}; OPTIMIZATION GATE {report['automated_status']}; captures={len(captures)}, exact geometry={report['geometry']['status']}, exact differences={report['exact_geometry_difference_count']}; NATIVE V1 EXACT RGB {native_v1['status']}")
        if not report["automated_contracts_pass"]:
            status = status or 1
        runfile = out / "run.json"
        run = json.loads(runfile.read_text(encoding="utf-8"))
        run.pop("fidelity_status", None)  # Superseded zero-RGB policy is not a current acceptance verdict.
        run.update(status="optimization_gate_passed" if report["automated_contracts_pass"] else "optimization_gate_failed",
                   automated_status=report["automated_status"], manual_acceptance=report["manual_acceptance"], overall_returncode=status,
                   native_v1_exact_rgb=native_v1["status"],
                   exact_geometry_difference_count=report["exact_geometry_difference_count"])
        runfile.write_text(json.dumps(run, indent=2), encoding="utf-8")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
