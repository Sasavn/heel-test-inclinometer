"""Validate actual CLI fixtures and compare the existing Tab5 REL header."""
from pathlib import Path
import ast
import json

ROOT = Path(__file__).resolve().parents[2]
def reject_nonfinite(value):
    raise AssertionError("Non-finite JSON token: " + value)
def wire(name, ack):
    lines = (ROOT / "build_host" / name).read_text(encoding="utf-8").splitlines()
    assert len(lines) == 2 and lines[1] == ack, lines
    return json.loads(lines[0], parse_constant=reject_nonfinite)

j = wire("relative_status.txt", "OK relative")
keys = "mode reference object ready quality skew_us ref_us obj_us ref_seq obj_seq rel_offset generation hardware_sync heads_supported axes_confirmed raw_x raw_y x y offset_x offset_y ref_x ref_y obj_x obj_y ref_age_us obj_age_us max_age_us max_skew_us".split()
assert all(k in j for k in keys)
assert j["ready"] and j["hardware_sync"] is False
assert j["raw_x"] == 1 and j["raw_y"] == -1 and j["max_age_us"] == 500000 and j["max_skew_us"] == 50000
invalid = wire("relative_invalid.txt", "OK relative")
assert invalid["ready"] is False and invalid["rel_offset"] is True
assert all(invalid[k] is None for k in ["raw_x", "raw_y", "x", "y"])
status = wire("instrument_status.txt", "OK")
assert all(status[k] is True for k in ["dual_mode_cap", "instrument_control_cap", "relative_data_cap"])
assert status["board"] == "stm32f411" and status["proto"] == 2 and status["instrument"] == "level"
def header(path, macro):
    line = path.read_text(encoding="utf-8").split("#define " + macro + " ", 1)[1].splitlines()[0]
    return ast.literal_eval(line)
local = header(ROOT / "Core/Inc/relative.h", "RELATIVE_CSV_HEADER")
reference = header(ROOT.parent / "tab5/main/tab5_services.h", "TAB5_SERVICE_RELATIVE_HEADER")
assert local == reference and len(local.strip().split(";")) == 28
print("PASS wire JSON29, explicit capabilities, null measurements, Tab5 REL header28 exact")
