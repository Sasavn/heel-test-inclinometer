# Tab5 protocol 2 — authoritative implementation contract

UTF-8 commands and replies are LF delimited (CR before LF is accepted). The input
buffer is 1200 bytes including NUL; output lines fit 3072 bytes including NUL.
USB serial/JTAG is owner 1; TCP connections have distinct owners >= 2. The TCP
task has a 16 KiB stack. Main implements ver/status/stream/samples/zero/settings/rec
and console transport. Services intercept OTA and files/get before the main parser.
Callbacks run after releasing service locks.

## Authentication and network

TCP listens on port 5000. Send `auth <token>`; successful authentication replies
exactly `OK`. A failed authentication replies `ERR auth` and closes the connection.
The unique token is the device AP password supplied by main's persisted config.
Unauthenticated connections cannot execute commands or receive published samples.
Wi-Fi uses BSP_FEATURE_WIFI and ESP-Hosted esp_wifi AP/STA APIs. Console access
is local and its authentication policy belongs to main.

## OTA commands

| Command | Successful response |
| --- | --- |
| `ota info` | One JSON line described below, then `OK` |
| `ota begin tab5 esp32p4 2 <version> <size> <sha256hex>` | `OK ota offset=0` |
| `ota chunk <offset> <hex>` | `OK ota offset=<next>` |
| `ota end` | `OK ota committed scheduled reboot` |
| `ota abort` | `OK ota aborted` |
| `ota status` | `OK ota offset=N active=1` or `active=0` |

`ota info` JSON keys are `board:"tab5"`, `chip:"esp32p4"`, `proto:2`,
`chip_revision:<actual numeric eFuse revision>`, `version:"1.5.0-esp32"`,
`max_size:<inactive partition size>`, `recording:<boolean>`, and
`boot_confirmed:<boolean>`. When idle, recording is false. Errors are terminal
`ERR ...` lines, with no trailing OK.

Sizes and offsets are decimal bytes. Hash is exactly 64 hexadecimal SHA-256
characters. Each chunk contains 1..256 decoded bytes. Offsets must be sequential.
An identical retransmission of the most recently accepted chunk is acknowledged
without another flash write. Different bytes at the previous offset are rejected.
The acknowledged initial prefix can be buffered in volatile RAM until validated.
The 544-byte prefix accommodates arbitrary chunk boundaries (including 255+32+256).

The only accepted image is a raw ESP application for ESP32-P4, descriptor project
`krenomer-esp32`, with descriptor version identical to the begin version. Version
must end in `-esp32` and its numeric triple must be >= the running image's version.
Numeric overflow is rejected. Version identity comes from the IDF application
descriptor, keeping protocol and image metadata in agreement. Same-version reinstall
is allowed. Header magic, segment count, first descriptor segment size, descriptor
magic, chip ID, and chip revision range are checked before flash erase/write.
Full revision bounds are header bytes 15..18; legacy minimum byte 14 is multiplied
by 100 when full minimum is zero. The inactive application partition must fit the
exact declared size and must differ from the running partition. Only after complete
byte count, matching SHA-256 and successful esp_ota_end is boot selection changed.
ESP-IDF performs full image integrity validation in esp_ota_end. No C6 application,
STM32 image, or merged full-flash factory image is an OTA application.

Recording and OTA are mutually exclusive. A running image must be confirmed
before a new OTA can begin. Disconnect of the owning TCP or USB transport aborts
an active update. USB COM close alone is not detectable from USB SOF; a 30-second
idle timeout aborts it. Reboot is armed only after the owning transport reports
its response drained through `tab5_services_response_flushed(owner)`, with at least
500 ms additional delay. Main calls this after USB TX drain succeeds. OTA abort
cannot cancel an image that has already been committed.

Main calls confirm_boot only after startup self-tests. For a blank first-boot
otadata, services first selects the healthy running partition and then marks it
valid. This provides the rollback baseline for the first update. boot_confirmed
is true only for actual VALID metadata or successful local health confirmation;
an API read error never implies confirmation. There is no battery threshold:
normal operation uses external power.

## Files

Files and get are forbidden during recording and OTA. SD is preferred; USB is the
fallback if SD is unavailable or failed. One transfer at a time blocks record
start. Basenames exclude paths, traversal, whitespace and control characters.
`files` returns `F,<name>,<bytes>,<date time>` for each CSV, followed by `OK`;
the date field may be empty, but its leading comma is always present.
`get <name>` returns `G,<name>,<size>,0`, then `D,<base64>` chunks, then
`E,<bytes>,<CRC32 eight hex digits>` and `OK`. CRC-32 is the usual reflected
0xEDB88320 CRC (initial/final complement), compatible with the PC DownloadSink.
No offset resume is implemented. `get abort` returns `OK get aborted`.

Transfers are cooperative: services_poll emits at most one line per call. Main
pumps USB only when its TX queue has room; TCP pumps below its pending-output
watermark. Disconnect closes transfer state. Five seconds without pumping progress
releases the transfer. A read failure returns ERR rather than a successful checksum.

## Recording format

Files are `<YYYY-MM-DD_MNNN>_D2.CSV` and `_D3.CSV`, independently mirrored to
SDMMC and USB-A MSC FAT32. Exclusive create prevents overwriting. No autoformat.
Each fresh sample belongs to sensor address 2 or 3 and carries DD.MM.YYYY date.
CSV header is exactly (LF ending):

```
Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms
```

Rows use LF, semicolons and decimal commas: Raw X/Y have 2 places, offsets and
calculated X/Y have 3, battery has 1. Round ties to even into integer centi/milli/
decivolts; Calc = roundedRawCenti * 10 - roundedOffsetMilli. Integer formatting
avoids negative zero. Nonfinite battery maps to 0,0. Main's R lines share
`tab5_services_scaled` with CSV.

## Startup fault isolation

USB host/MSC and network peripheral initialization failures are reported through
storage/network status and do not fail service-core initialization. Main may keep
UI, console commands and SD recording available and perform its usual self-test.
Only core allocation/task creation failures abort initialization. With AP and STA
disabled, no network stack, C6 Wi-Fi or TCP listener is initialized. A USB driver
startup error remains the reported USB error until that driver is available;
periodic mount retry cannot replace it with a generic missing-device error.

Later successful network configuration starts TCP even if boot networking was
disabled or initial radio/network setup failed. Repeated configuration reuses the
single listener/task. All configure failures update network.last_error; a successful
retry clears it. The initial optional-peripheral failure policy remains unchanged.
