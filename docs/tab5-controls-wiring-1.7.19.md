# Tab5 controls profile 1.7.19: wiring and evidence

## Подключение для оператора

Выберите **готовый архив `controls`** под свою ревизию ESP32-P4. Обычный
архив не включает внешние GPIO. Перед подключением отключите питание;
не используйте этот профиль вместе с расширением, занимающим те же контакты.

| Провод | GPIO микроконтроллера | Номер контакта M5-Bus |
|---|---:|---:|
| A энкодера | 3 | 19 |
| B энкодера | 4 | 20 |
| Кнопка энкодера | 2 | 21 |
| Отдельная кнопка записи с фиксацией | 48 | 22 |
| Общий контакт кнопок/энкодера | GND | 1, 3 или 5 |

Это **сухие контакты на землю**, с внутренней подтяжкой входов. Не подавайте
5 В, HVIN или BAT на GPIO. Для модуля энкодера с электроникой сначала нужна
проверка его уровней. Контакт M5-Bus №20 — это **GPIO4**, а не GPIO20 RS485.
Не определяйте стороны по повернутому экрану: ниже приведены официальный
вид разъёма, якоря GND/3V3 и порядок нумерации; ответная часть зеркалится.

Положение кнопки с фиксацией при загрузке принимается за исходное: ON не
запускает запись сам. После переключения ON запрашивается старт, OFF — стоп.
Удержание ON не запускает запись снова после остановки с ПК. При включённой
«Синхронизации записи на устройствах» передаются и старт, и стоп; без неё
действия независимы. Первое касание/действие энкодера после сна только будит
подсветку. Электрическая и аппаратная проверка этой сборки не выполнялась.

Verified from published primary sources on 2026-10-30; no physical hardware test.
The parent builds ready-to-use default and controls ZIPs for rev1/rev3, all version
1.7.19. This document does not assert those ZIPs have already been built or tested.
Sergey does not need to compile firmware. Package metadata must distinguish
controls_enabled=false/true and board profile; include this wiring document.
Default Kconfig remains CONFIG_TAB5_INPUT_GPIO_ENABLE=n. Controls profile explicitly
enables it; it is not enabled by version number or board detection.

## Exact agreed wiring

| Signal | ESP32-P4 GPIO | Tab5 BUS1 physical contact |
|---|---:|---:|
| Encoder A | 3 | 19 |
| Encoder B | 4 | 20 |
| Encoder push button | 2 | 21 |
| Separate latched recording switch | 48 | 22 |
| Contact common | GND | 1, 3 or 5 |

Passive dry contacts only: close each input to GND; open reads HIGH using the
configured pull-up, closed reads logical down/ON. No GPIO connection to 5V,
HVIN or BAT. Powered encoder modules need separate electrical verification.
M5-Bus contact20 is GPIO4, NOT GPIO20. RS485 GPIO20/21/34 remain untouched.

## Primary proof of GPIO48

- [Official Tab5 documentation, M5-Bus table](https://docs.m5stack.com/en/core/Tab5#pinmap)
  lists row21/22 as GPIO2/GPIO48 and row19/20 as GPIO3/GPIO4.
- [Official schematic PDF, page4](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf#page=4):
  net group PIBUS1022 / PID2201 / NLSTD0GPIO48 proves BUS1 contact22 connects
  to STD_GPIO48 with D22 ESD protection. Adjacent contact21 is STD_GPIO2.
  No other functional load on GPIO48 was found in the published schematic.
- IMPORTANT: BUS1 symbol retains old CORE2 GPIO aliases, explicitly noted
  "BUS same as CORE2 with old GPIO map". Its contact22 alias G19 is NOT
  the Tab5 SoC GPIO number. Follow connected net STD_GPIO48 and current table.
- [Espressif ESP32-P4 schematic checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/schematic-checklist-esp32p4.html)
  identifies strapping GPIO34/35/36/37/38. GPIO2/3/4/48 are not in that set.
  Do not transfer strapping rules from other ESP32 families.

## Connector orientation

[Official rear connector reference photo](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/tab5_m5_bus_extension_01.jpg)
was visually inspected. Use its LEFT panel: the bare rear M5-Bus socket is shown
between the mounting screws beside the printed M-BUS legend. The RIGHT panel
shows an attached module and is not the wiring access view.

Use electrical anchors rather than screen-left/right on a rotated tablet:
start at the 1/2 end identified by the first GND/G16 pair in the printed legend.
The column beginning with three GND contacts is the odd-numbered column
(1,3,5,...); the other column is even (2,4,6,...), including 3V3 at contact12.
Count pairs from that end, not from the HVIN/BAT end:

```text
odd column       even column
17 GPIO31        18 GPIO32
19 GPIO3   A     20 GPIO4   B
21 GPIO2   PUSH  22 GPIO48  REC
23 GPIO47        24 GPIO35  BOOT -- do not use
25 HVIN          26 GPIO51
```

Contact22 is the even contact in pair11, opposite contact21, between pairs19/20
and23/24. This is connector numbering referenced to the official legend, not
a claim about a mirrored cable or module mating-face view. Check pin1 and the
GND/3V3 anchors before attachment; do not mirror the drawing by guesswork.

## Board and occupied-pin limits

Official documentation lists panel changes ILI9881C/GT911 -> ST7123 on
2025-10-14 and ST7123 -> ST7121 on 2026-04-28. Panel label or software rev1/rev3
profile does not establish every physical mainboard net. Mapping above is for
the published Tab5 schematic; inspect an actual board with differing labels.
External stacked modules may occupy these expansion contacts and must be absent
or audited. No unconditional safety claim is made for unknown board revisions.

Local BSP and main were inspected: reserved functions include I2C31/32,
LCD22/touch23, audio26-30, camera36, C6 SDIO8-15 and SD39-44. No allocation of
GPIO2/3/4/48 was found outside the explicit new controls profile. BSP's
USB_POS20 label does not free RS48520. Dedicated MIPI pins remain untouched.

## Software contract and verification

Main's explicit profile: A3 B4 PRESS2 REC48, debounce30ms, poll5ms, guarded by
CONFIG_TAB5_INPUT_GPIO_ENABLE. Component has no implicit pin selection.
Callbacks run in polling task context and return bool accepted. Button reports
debounced down/up only; port/UI owns first-wake consumption and 600ms hold.
Encoder emits four-edge quadrature detents. Switch emits debounced level changes:
boot ON is only baseline; held ON never restarts recording after remote stop.
OFF queues a local stop; enabled recording synchronization propagates that stop
through the existing PC/backend recording policy, not directly from this driver.

False acceptance retries the original event and timestamp until accepted;
accepted events are not repeated. Sampling pauses under backpressure, so motion
during a stalled consumer may be lost. Polling cadence is quantized to RTOS ticks
and is not proof that all encoder edges are captured at arbitrary rotation speed.

Portable core and adapter CONFIG0/CONFIG1 host tests passed with GCC warnings as
errors. Target default/controls rev1/rev3 builds and package verification belong
to the parent release task. Hardware electrical and timing tests were not run.
