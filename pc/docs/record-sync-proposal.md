# PC recording synchronization 1.8.6

Approved API: status rec_sync_cap:true, rec_sync:boolean, rec_sw:boolean actual
recording. set recsync 0|1 returns OK recsync only after durable NVS save;
ERR leaves backend unchanged. Single device option, default OFF; no PC INI copy.
PC settings checkbox requires a connected capable backend with known state.

START and STOP synchronize both ways. Successful local actions queue existing
rec on/off. Backend edges call local actions with mirrored=true (no echo).
Connection generation, status sequence, freshness, first-snapshot baseline and
expected command feedback guard against repeats and reconnect restarts.
Unknown or stale state invalidates baseline. Local recording remains open over
link loss under existing recorder behavior; reconnect does not open new CSVs.
Failed remote actions leave real local recording intact and show a persistent
partial-recording warning. Failed local starts never claim success or retry
from repeated status. Manual PC actions replace the expected remote outcome.

Zero warning uses actual local recorder state or fresh connected device status,
not stream/samples activity or stale disconnected state. Simulator supports
rec on/off, backend setting, persistence failure, NOCARD and clock failure.
record_sync_test exercises App + DeviceLink + Simulator without GUI/hardware.

This document describes the recording subtask; the combined change also includes TcpConnection and firmware/UI updates. STM sources are untouched. Header edits require clean builds
because source mtimes are preserved. Build/test evidence is recorded separately.

## Verified 2026-10-30

Both MSVC v143 Release architectures rebuilt with --clean-first (53 steps).
Windows 7 import checks passed. EXE resource version is 1.8.6.0.
CTest: x64 16/16 PASS (30.67 s); x86 16/16 PASS (41.60 s).
Includes record_sync_test (App/DeviceLink/parser/Simulator), tcp_transport_test,
and ui_shots (452 checks, 103 software-rendered PNGs, no GUI/hardware).
Cases cover both directions, OFF, absent capability, no echo, repeated status,
reconnect baseline, stale status, failed local start, failed persistence,
NOCARD, clock error, fast START/STOP, and old-generation ACK consumption.
PNG evidence: build-win7/shots/09_settings.png,
record_sync_zero_idle.png, record_sync_zero_recording.png; same for x86.
Full CTest logs: each build directory's Testing/Temporary/LastTest.log.
Existing modified source mtimes preserved. Final combined builds include TCP quality and diagnostic layout fixes.
No hardware/NVS persistence claim: the PC suite uses the simulated backend.
