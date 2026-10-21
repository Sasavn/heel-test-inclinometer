# Tab5 platform services

Implementation: main/tab5_services.c; public API: main/tab5_services.h.
Wire contract: PROTOCOL.md.

Main initializes NVS and BSP, supplies copied configuration, and owns console
transport and application state. Reply callbacks borrow context synchronously and
receive one line without LF. Network callbacks enter main with no service lock held.

Storage uses two independent 64-sample bounded queues and separate workers/mutexes.
Submit takes only a zero-wait state lock and never performs I/O; a queue overflow
increments that mirror's dropped count. Each session resets written/dropped/queued
counters. A failed/full/removed mirror stops writing; the other continues. Files
are flushed and fsynced at least once per worker's one-second cycle and on stop.
A failed mirror cannot rejoin a recording. Missing SD/USB media is optional. Retry
runs outside recording/stopping/file transfers at two-second intervals; insertion
is mounted after recording stops. Manual tab5_services_retry is also available.
SD uses slot 0, separate from ESP-Hosted C6 SDIO slot 1. Both mounts disable format
and reject non-FAT32 volumes. Exclusive creation never truncates existing files.
Partial files after power loss are retained rather than rewritten or repaired.

Wi-Fi enables the Tab5 BSP feature and configures ESP-Hosted through esp_wifi.
AP, STA or AP+STA are supported. USB host/MSC, netif, Wi-Fi and TCP listener
initialization failures are optional: init returns success after essential core
startup and exposes the peripheral error in the corresponding snapshot. Thus main
can initialize UI/console/commands and record on SD despite a failed USB driver.
USB retries preserve the original startup driver error while the driver is down.
With AP and STA both disabled, startup skips netif, C6/Wi-Fi and listener creation;
network_configure also avoids C6 initialization for a disabled configuration.
Core synchronization/queue allocation and storage/network task creation failures
remain fatal and propagate from init. Absent media and unavailable peripherals
do not select rollback or prevent main's later hardware health confirmation. TCP publish is a bounded zero-wait queue; only
authenticated clients receive it. Each client's output is bounded, so a slow client
cannot allocate unbounded memory. Socket output, file transfer callbacks and main
command callbacks occur outside shared service locks.

OTA buffers and validates the image prefix before erase, writes only inactive flash,
checks SHA-256 and exact length, runs esp_ota_end before boot selection, and requires
main's health confirmation for rollback. USB and TCP response drain notification
arms reboot after 500 ms. Files/get are cooperative; never bulk-enqueue a whole file.
Call tab5_services_poll only with room for a complete 512-byte output line. Main's
USB queue watermark and TCP pending-byte watermark provide transport backpressure.

## Validation

Run `python tab5/tests/test_services_run.py` from the project root. The runner
compiles tests/test_services.c, which includes the actual production services.c.
Only OS locks/tasks, media fault boundaries and OTA flash APIs are substituted.
SHA-256 uses Windows BCrypt, not a fake checksum. Tests use temporary local FAT-like
files and injected ENOSPC/removal errors; no physical device, serial port, GUI,
network connection or flash is accessed. Tests cover CSV bytes/rounding, ownership,
recording exclusion, hot removal, mirror isolation, full medium, retry restrictions,
exclusive create, corrupt/wrong-target/revision/project/version/hash rejection,
retry, arbitrary chunk boundaries, disconnect/idle abort, failed verification,
boot-selection failure, blank otadata baseline and response-drain reboot timing.
The injected boot backend models rollback; it does not execute the ESP bootloader.
Physical SD/USB hotplug, C6 connectivity and real power-cut rollback require hardware
validation and are not claimed as performed by host tests.

## Startup isolation regression

The runner also builds test_services_startup.c. Both ESP and host execute the same
production tab5_services_init/startup orchestration; only allocation, driver,
netif/socket and task boundaries are injected. The USB-driver-failure case checks
successful caller UI/command continuation, actual SD file creation and sample write,
unchanged original USB error across retry, and no boot_failed/rollback selection.
Further cases cover disabled networking (zero network/C6 attempts), optional netif,
Wi-Fi and listener failure, successful networking and fatal core/task allocation.

## Runtime network enable/recovery

Initial startup and later UI network_configure use the same serialized production
network orchestration. Successful Wi-Fi configuration ensures a TCP listener/task
exists, including off-to-on and recovery from initial C6/netif/socket errors.
A dedicated network mutex prevents duplicate listener/task creation; subsequent
successful configure calls reuse the existing task. Disabling Wi-Fi after a listener
exists keeps that listener/task available for reuse when the radio is reenabled.
A device booting with AP+STA disabled still initializes no network/C6/listener.
Every configure return updates last_error, including invalid config, BSP, event
registration, netif, Wi-Fi and bind failures. Partial Wi-Fi setup retains existing
netifs/initialization and retries missing event registrations without duplication.
Startup regression tests cover off-to-on, repeated configure, on-off-on, initial
Wi-Fi/netif/bind failure followed by recovery, and cleared/preserved error snapshots.
