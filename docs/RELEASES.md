# Каталог выпусков и компонентов

Снимок GitHub и локальных доказательств на **11.11.2026**. Номера старых
**bundle-тегов** не совпадают с версиями PC или прошивки. Существующие теги,
релизы и файлы сохранены: каталог не переименовывает и не заменяет историю.

<a id="pc-190"></a>
## PC 1.9.0: общий выпуск

| Компонент | Файлы | Назначение |
|---|---|---|
| [PC 1.9.0](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/pc-v1.9.0) | [x64 EXE](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.9.0/krenomer-pc-1.9.0-win-x64.exe), [x86 EXE](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.9.0/krenomer-pc-1.9.0-win-x86.exe), ZIP, SHA256SUMS | Общее приложение, автоопределение, Креномер / Угломер |
| [STM32 1.6.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/stm32-v1.6.1) | [Комплект BIN](https://github.com/Sasavn/heel-test-inclinometer/releases/download/stm32-v1.6.1/STM32-1.6.1-zero-help.zip) | Только STM32F411, два стартовых режима |
| [Tab5 1.8.6](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.8.6) | Factory и OTA: default/controls, P4 rev1/rev3 | Только ESP32-P4 / Tab5; выбрать правильную ревизию и органы управления |

Для прибора Сергея с энкодером и тумблером P4 rev1 предназначен
[Tab5-1.8.6-controls-P4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.8.6/Tab5-1.8.6-controls-P4-rev1.zip).
Прошивки опубликованы как предварительные: натурная проверка этого комплекта
на платах не проводилась. [Проверки](../pc/docs/VALIDATION_1.9.0.md).
PC-релиз содержит программу Windows; прошивки берутся из своего компонентного релиза.
Старый [PC 1.8.7](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/pc-v1.8.7) сохранён в истории.

[STM32](PROJECT.md#stm32) · [ESP/Tab5](PROJECT.md#tab5) · [Описание приборов](PROJECT.md).

## Обновление справки 11.11.2026

В STM32 **1.6.1** и Tab5 **1.8.6** добавлена справка самого прибора об
обычном нуле, REL-нуле, подготовке, единицах, качестве и сохранении.
[Расширенная инструкция с примером](UGLOMER_HELP.md).
Измерительные алгоритмы, протокол и форматы записи сохранены;
PC **1.9.0** остаётся актуальным приложением.

Предыдущие [STM32 1.6.0](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/stm32-v1.6.0)
и [Tab5 1.8.5](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.8.5)
сохранены без замены файлов. Новые прошивки проверены на программных стендах,
натурная установка в этой работе не выполнялась.

## Все опубликованные релизы

Ниже перечислены прежние выпуски. Текущие три компонента находятся выше,
а 14 восстановленных архивных выпусков — в отдельной таблице в конце.
Исторические файлы не пересобирались и не заменялись; контрольные суммы
архивов проверены после скачивания. Автоматические Source code ZIP/TAR
не являются дополнительным бинарным комплектом.

| Тег комплекта / релиза | PC в поставке | STM32 FW | Tab5 FW | Статус / assets |
|---|---|---|---|---|
| [pc-v1.8.7](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/pc-v1.8.7) | 1.8.7 | — | — | обычный, 5 файлов |
| [tab5-v1.8.4](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.8.4) | 1.8.6 | — | 1.8.4-esp32 | предварительный, 6 файлов |
| [tab5-v1.7.19](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.7.19) | 1.8.6 | — | 1.7.19-esp32 | предварительный, 7 файлов |
| [pc-tab5-v1.8.6](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/pc-tab5-v1.8.6) | 1.8.6 | — | — | предварительный, 4 файлов |
| [pc-tab5-v1.8.5](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/pc-tab5-v1.8.5) | 1.8.5 | — | — | предварительный, 4 файлов |
| [tab5-v1.7.18](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.7.18) | — | — | 1.7.18-esp32 | предварительный, 2 файлов |
| [tab5-v1.7.0](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.7.0) | 1.8.4 | — | 1.7.0-esp32 | предварительный, 6 файлов |
| [v2.0.2](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v2.0.2) | 1.8.3 | 1.5 | — | обычный, 5 файлов |
| [v2.0.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v2.0.1) | 1.8.1 | 1.5 | — | обычный, 5 файлов |
| [v2.0](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v2.0) | 1.8 | 1.5 | — | обычный, 5 файлов |
| [v1.9.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.9.1) | 1.7.1 | 1.5 | — | обычный, 5 файлов |
| [v1.9](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.9) | 1.7 | 1.5 | — | обычный, 5 файлов |
| [v1.8.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.8.1) | 1.6.1 | 1.5 | — | обычный, 5 файлов |
| [v1.8](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.8) | 1.6 | 1.5 | — | обычный, 5 файлов |
| [v1.7.2](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.7.2) | 1.5.2 | 1.5 | — | обычный, 5 файлов |
| [v1.7.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.7.1) | 1.5.1 | 1.5 | — | обычный, 5 файлов |
| [v1.7](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.7) | 1.5 | 1.5 | — | обычный, 5 файлов |
| [v1.6](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.6) | 1.4 | 1.5 | — | обычный, 5 файлов |
| [v1.5.3](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.5.3) | 1.3.2 | 1.5 | — | обычный, 5 файлов |
| [v1.5.2](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.5.2) | 1.3.1 | 1.5 | — | обычный, 5 файлов |
| [v1.5.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.5.1) | 1.3 | 1.5 | — | обычный, 5 файлов |
| [v1.5](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.5) | 1.2 | 1.5 | — | обычный, 5 файлов |
| [v1.4](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.4) | 1.1 | 1.4 | — | предварительный, 4 файлов |
| [v1.3](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/v1.3) | 1.0 | 1.3 | — | предварительный, 4 файлов |

«—» означает, что компонент не поставляется этим релизом; исходники другого
компонента могут присутствовать в commit. Версии таблицы сверены с файлами
`pc/src/ui/App.hpp`, `Core/Inc/version.h`, `tab5/CMakeLists.txt` в commit тега
и составом assets; для изменённых вложений `v2.0.2` действует уточнение ниже.
Совпадение версии не доказывает побайтную идентичность
разных сборок: для неё нужен SHA-256.

**Особый случай `v2.0.2`:** тег указывает на исходники **PC 1.8.2**.
EXE-вложения обновлены **24.10.2026** до **PC 1.8.3**; в release notes
указан отдельный [commit исходников вложений fd703a2](https://github.com/Sasavn/heel-test-inclinometer/commit/fd703a2e5c49884735c89dbdeab1bb57b69f71ee).
Локальная копия x64 имеет PE FileVersion **1.8.3.0** и SHA-256
`1a622699294ccd70d8d8b61d28cab80ae9243b2055ff9c47ccda2ef9fd54b6b3`,
совпадающий с GitHub asset. Source code ZIP этого тега не является
снимком исходников текущего PC-вложения. Оригинальные снятые EXE 1.8.2
не считаются восстановленными только по наличию похожих локальных сборок.

В `tab5-v1.8.4` поставлены **исторические официальные PC 1.8.6**, а не новый
PC с полным угломером. Внутренние README/manifest неизменённого PC ZIP могут
упоминать исходную связку Tab5 1.7.19: это не версия firmware всего комплекта.

## Файлы каждого релиза

Ниже — весь список прикреплённых файлов. SHA256SUMS и README следует брать
из того же релиза. Файлы разных платформ и ревизий не взаимозаменяемы.

### pc-v1.8.7

- [krenomer-pc-1.8.7-win-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.8.7/krenomer-pc-1.8.7-win-x64.exe) — 6 026 752 байт.
- [krenomer-pc-1.8.7-win-x64.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.8.7/krenomer-pc-1.8.7-win-x64.zip) — 2 989 946 байт.
- [krenomer-pc-1.8.7-win-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.8.7/krenomer-pc-1.8.7-win-x86.exe) — 5 595 136 байт.
- [krenomer-pc-1.8.7-win-x86.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.8.7/krenomer-pc-1.8.7-win-x86.zip) — 2 772 207 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.8.7/SHA256SUMS.txt) — 388 байт.

### tab5-v1.8.4

- [Krenomer-Tab5-1.8.4-PC-1.8.6.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.8.4/Krenomer-Tab5-1.8.4-PC-1.8.6.zip) — 6 631 461 байт.
- [krenomer-tab5-1.8.6-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.8.4/krenomer-tab5-1.8.6-x64.exe) — 6 026 240 байт.
- [krenomer-tab5-1.8.6-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.8.4/krenomer-tab5-1.8.6-x86.exe) — 5 594 624 байт.
- [README-RU.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.8.4/README-RU.txt) — 2 559 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.8.4/SHA256SUMS.txt) — 470 байт.
- [Tab5-1.8.4-controls-P4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.8.4/Tab5-1.8.4-controls-P4-rev1.zip) — 907 262 байт.

### tab5-v1.7.19

- [CONTROLS-WIRING.md](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.19/CONTROLS-WIRING.md) — 7 789 байт.
- [FIRMWARE-1.7.19-SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.19/FIRMWARE-1.7.19-SHA256SUMS.txt) — 543 байт.
- [Krenomer-Tab5-1.7.19-esp32-p4-rev1-controls.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.19/Krenomer-Tab5-1.7.19-esp32-p4-rev1-controls.zip) — 892 334 байт.
- [Krenomer-Tab5-1.7.19-esp32-p4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.19/Krenomer-Tab5-1.7.19-esp32-p4-rev1.zip) — 886 958 байт.
- [Krenomer-Tab5-1.7.19-esp32-p4-rev3-controls.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.19/Krenomer-Tab5-1.7.19-esp32-p4-rev3-controls.zip) — 893 870 байт.
- [Krenomer-Tab5-1.7.19-esp32-p4-rev3.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.19/Krenomer-Tab5-1.7.19-esp32-p4-rev3.zip) — 888 587 байт.
- [Krenomer-Tab5-1.7.19-PC-1.8.6.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.19/Krenomer-Tab5-1.7.19-PC-1.8.6.zip) — 9 262 481 байт.

### pc-tab5-v1.8.6

- [krenomer-tab5-1.8.6-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.6/krenomer-tab5-1.8.6-x64.exe) — 6 026 240 байт.
- [krenomer-tab5-1.8.6-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.6/krenomer-tab5-1.8.6-x86.exe) — 5 594 624 байт.
- [Krenomer-Tab5-PC-1.8.6.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.6/Krenomer-Tab5-PC-1.8.6.zip) — 5 739 055 байт.
- [PC-1.8.6-SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.6/PC-1.8.6-SHA256SUMS.txt) — 284 байт.

### pc-tab5-v1.8.5

- [krenomer-tab5-1.8.5-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.5/krenomer-tab5-1.8.5-x64.exe) — 6 018 048 байт.
- [krenomer-tab5-1.8.5-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.5/krenomer-tab5-1.8.5-x86.exe) — 5 584 384 байт.
- [Krenomer-Tab5-PC-1.8.5.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.5/Krenomer-Tab5-PC-1.8.5.zip) — 6 074 438 байт.
- [PC-1.8.5-SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-tab5-v1.8.5/PC-1.8.5-SHA256SUMS.txt) — 284 байт.

### tab5-v1.7.18

- [Krenomer-Tab5-1.7.18-esp32-p4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.18/Krenomer-Tab5-1.7.18-esp32-p4-rev1.zip) — 887 984 байт.
- [Tab5-1.7.18-SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.18/Tab5-1.7.18-SHA256SUMS.txt) — 106 байт.

### tab5-v1.7.0

- [Krenomer-Tab5-1.7.0-esp32-p4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.0/Krenomer-Tab5-1.7.0-esp32-p4-rev1.zip) — 856 766 байт.
- [Krenomer-Tab5-1.7.0-esp32-p4-rev3.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.0/Krenomer-Tab5-1.7.0-esp32-p4-rev3.zip) — 858 257 байт.
- [krenomer-tab5-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.0/krenomer-tab5-x64.exe) — 6 018 048 байт.
- [krenomer-tab5-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.0/krenomer-tab5-x86.exe) — 5 584 384 байт.
- [PC-SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.0/PC-SHA256SUMS.txt) — 178 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-v1.7.0/SHA256SUMS.txt) — 210 байт.

### v2.0.2

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.2/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.2/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.2/krenomer-x64.exe) — 5 876 224 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.2/krenomer-x86.exe) — 5 462 528 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.2/SHA256SUMS.txt) — 334 байт.

### v2.0.1

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.1/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.1/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.1/krenomer-x64.exe) — 5 812 224 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.1/krenomer-x86.exe) — 5 402 112 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0.1/SHA256SUMS.txt) — 330 байт.

### v2.0

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0/krenomer-x64.exe) — 5 796 352 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0/krenomer-x86.exe) — 5 382 656 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v2.0/SHA256SUMS.txt) — 330 байт.

### v1.9.1

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9.1/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9.1/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9.1/krenomer-x64.exe) — 5 711 872 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9.1/krenomer-x86.exe) — 5 304 832 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9.1/SHA256SUMS.txt) — 330 байт.

### v1.9

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9/krenomer-x64.exe) — 5 668 352 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9/krenomer-x86.exe) — 5 261 824 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.9/SHA256SUMS.txt) — 330 байт.

### v1.8.1

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8.1/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8.1/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8.1/krenomer-x64.exe) — 5 555 712 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8.1/krenomer-x86.exe) — 5 159 424 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8.1/SHA256SUMS.txt) — 330 байт.

### v1.8

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8/krenomer-x64.exe) — 5 548 032 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8/krenomer-x86.exe) — 5 153 280 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.8/SHA256SUMS.txt) — 330 байт.

### v1.7.2

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.2/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.2/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.2/krenomer-x64.exe) — 5 535 232 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.2/krenomer-x86.exe) — 5 139 456 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.2/SHA256SUMS.txt) — 330 байт.

### v1.7.1

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.1/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.1/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.1/krenomer-x64.exe) — 5 442 560 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.1/krenomer-x86.exe) — 5 053 440 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7.1/SHA256SUMS.txt) — 330 байт.

### v1.7

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7/krenomer-x64.exe) — 5 434 368 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7/krenomer-x86.exe) — 5 045 248 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.7/SHA256SUMS.txt) — 330 байт.

### v1.6

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.6/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.6/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.6/krenomer-x64.exe) — 5 386 752 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.6/krenomer-x86.exe) — 4 998 656 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.6/SHA256SUMS.txt) — 330 байт.

### v1.5.3

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.3/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.3/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.3/krenomer-x64.exe) — 5 289 984 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.3/krenomer-x86.exe) — 4 902 912 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.3/SHA256SUMS.txt) — 330 байт.

### v1.5.2

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.2/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.2/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.2/krenomer-x64.exe) — 5 063 168 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.2/krenomer-x86.exe) — 4 706 304 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.2/SHA256SUMS.txt) — 330 байт.

### v1.5.1

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.1/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.1/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.1/krenomer-x64.exe) — 5 020 160 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.1/krenomer-x86.exe) — 4 667 392 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5.1/SHA256SUMS.txt) — 330 байт.

### v1.5

- [bwm427-fw-1.5.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5/bwm427-fw-1.5.bin) — 223 012 байт.
- [bwm427-fw.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5/bwm427-fw.bin) — 223 012 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5/krenomer-x64.exe) — 4 913 664 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5/krenomer-x86.exe) — 4 567 552 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.5/SHA256SUMS.txt) — 250 байт.

### v1.4

- [bwm427-fw-1.4.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.4/bwm427-fw-1.4.bin) — 222 284 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.4/krenomer-x64.exe) — 4 568 576 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.4/krenomer-x86.exe) — 4 249 088 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.4/SHA256SUMS.txt) — 250 байт.

### v1.3

- [bwm427-fw-1.3.bin](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.3/bwm427-fw-1.3.bin) — 222 100 байт.
- [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.3/krenomer-x64.exe) — 4 566 016 байт.
- [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.3/krenomer-x86.exe) — 4 245 504 байт.
- [SHA256SUMS.txt](https://github.com/Sasavn/heel-test-inclinometer/releases/download/v1.3/SHA256SUMS.txt) — 250 байт.

## Опубликованные архивные релизы

Архивная публикация 11.11.2026: **14 релизов, 17 файлов**. Сохранены байты ранее
подготовленных ZIP и EXE; каждый опубликованный файл скачан обратно и проверен
по SHA-256. Все архивы — prerelease; latest остался **PC 1.8.7**.
Это новые страницы для исторических сборок. Они не означают новую сборку,
прошивку устройства или новую аппаратную проверку. Даты и receipts — в release notes.

| Историческая версия | Архивный релиз / точный commit | Сохранённые файлы |
|---|---|---|
| Tab5 1.6.1 | [tab5-archive-v1.6.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-1e5ba8f76d032dc98e07) / [`5c20490`](https://github.com/Sasavn/heel-test-inclinometer/commit/5c20490c255f1647cd0397d74a6777ad386d0ace) | [tab5-flash-161.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.1/tab5-flash-161.zip) |
| Tab5 1.6.2 | [tab5-archive-v1.6.2](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-8f1bfb4cf4af25439328) / [`91bc354`](https://github.com/Sasavn/heel-test-inclinometer/commit/91bc3543f3660ca612bab6a465a1df994eca04d2) | [tab5-flash-162.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.2/tab5-flash-162.zip) |
| Tab5 1.6.3 | [tab5-archive-v1.6.3](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-a3074dfeb4209bc965a7) / [`7c4e3fa`](https://github.com/Sasavn/heel-test-inclinometer/commit/7c4e3fa3c76bcb305f47055e702b6797f532cce2) | [perf-163.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.3/perf-163.zip) |
| Tab5 1.6.4 | [tab5-archive-v1.6.4](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-05b5e22706d12644e126) / [`99dce1f`](https://github.com/Sasavn/heel-test-inclinometer/commit/99dce1fab4e8b2e6e4d801294414185c345f4107) | [resources-164.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.4/resources-164.zip) |
| Tab5 1.6.12 | [tab5-archive-v1.6.12](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-964ac25ccd4e336169ed) / [`369c7fc`](https://github.com/Sasavn/heel-test-inclinometer/commit/369c7fcd435ffd09631e49c0118e102c66ea4d75) | [response-1612.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.12/response-1612.zip) |
| Tab5 1.6.13 | [tab5-archive-v1.6.13](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-7cf466db3726376a473e) / [`c49eacf`](https://github.com/Sasavn/heel-test-inclinometer/commit/c49eacfc5a48119afc7f158a8637e9d8ebe00457) | [Krenomer-Tab5-1.6.13-esp32.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.13/Krenomer-Tab5-1.6.13-esp32.zip) |
| Tab5 1.6.14 | [tab5-archive-v1.6.14](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-417e1b5ea3d90fa9dc7f) / [`d4b34e6`](https://github.com/Sasavn/heel-test-inclinometer/commit/d4b34e6e7e15feffa1642c58b40b8e5b4cf43113) | [Krenomer-Tab5-1.6.14-esp32.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.14/Krenomer-Tab5-1.6.14-esp32.zip) |
| Tab5 1.6.15 | [tab5-archive-v1.6.15](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-f6e85d8e7ae22d7606d0) / [`d48cf02`](https://github.com/Sasavn/heel-test-inclinometer/commit/d48cf027c2422cf5763c949805d0052c12c55893) | [Krenomer-Tab5-1.6.15-esp32.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.15/Krenomer-Tab5-1.6.15-esp32.zip) |
| Tab5 1.6.16 | [tab5-archive-v1.6.16](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-b646f6f281cb9b420389) / [`17f5bae`](https://github.com/Sasavn/heel-test-inclinometer/commit/17f5bae33a3507ed31dd1875626f4826cac53e8f) | [Krenomer-Tab5-1.6.16-esp32.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.6.16/Krenomer-Tab5-1.6.16-esp32.zip) |
| Tab5 1.8.0 | [tab5-archive-v1.8.0](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-408e82861bc07102fea2) / [`4e155f9`](https://github.com/Sasavn/heel-test-inclinometer/commit/4e155f98272f12ef0395a19c37c5a63a2b3ac962) | [Tab5-1.8.0-first-dual-mode.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.8.0/Tab5-1.8.0-first-dual-mode.zip) |
| Tab5 1.8.1 | [tab5-archive-v1.8.1](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-d12f00398b4a11e172c8) / [`7d9041b`](https://github.com/Sasavn/heel-test-inclinometer/commit/7d9041b9c66f7a93c4b8514291b1927931f6481b) | [Tab5-1.8.1-relative-workspace.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.8.1/Tab5-1.8.1-relative-workspace.zip) · [flash181.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.8.1/flash181.zip) |
| Tab5 1.8.2 | [tab5-archive-v1.8.2](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-af473be46bc334d8cb1f) / [`be37fef`](https://github.com/Sasavn/heel-test-inclinometer/commit/be37fef3cf0240462f53f533839a603a5fedb2b7) | [Tab5-1.8.2-P4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.8.2/Tab5-1.8.2-P4-rev1.zip) · [Tab5-1.8.2-controls-P4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.8.2/Tab5-1.8.2-controls-P4-rev1.zip) |
| Tab5 1.8.3 | [tab5-archive-v1.8.3](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-92b03595f5c63cbb6124) / [`c1798d8`](https://github.com/Sasavn/heel-test-inclinometer/commit/c1798d82d16bbd2f3edde0cd991d8de2281b8464) | [Tab5-1.8.3-controls-P4-rev1.zip](https://github.com/Sasavn/heel-test-inclinometer/releases/download/tab5-archive-v1.8.3/Tab5-1.8.3-controls-P4-rev1.zip) |
| PC 1.8.3 | [pc-v1.8.3](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/untagged-cebec540926eddb636e8) / [`fd703a2`](https://github.com/Sasavn/heel-test-inclinometer/commit/fd703a2e5c49884735c89dbdeab1bb57b69f71ee) | [krenomer-x64.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.8.3/krenomer-x64.exe) · [krenomer-x86.exe](https://github.com/Sasavn/heel-test-inclinometer/releases/download/pc-v1.8.3/krenomer-x86.exe) |

PC 1.8.3 архивирован на `fd703a2`; существующий `v2.0.2` и его вложения
сохранены. Для Tab5 1.6.12 архив содержит прежний LVGL-вариант (`369c7fc`).
ZIP 1.8.0 и общий ZIP 1.8.1 содержат OTA-приложения; factory/ROM-файлов в них нет.
Готовые версии без подтверждённой связи исходников и бинарников остаются
source-only и в эти архивные релизы не включены.

Ниже сохранён исходный инвентарь до архивной публикации. Наличие новых
страниц и файлов для перечисленных версий проверяйте по ссылкам выше.

## Версии, которые трудно найти по списку релизов

| Компонент | Что найдено | Как трактовать |
|---|---|---|
| PC 1.8.2 | Commit `9226894` и исходники тега `v2.0.2`; сохранены локальные EXE с PE FileVersion 1.8.2.0 | Текущие вложения `v2.0.2` уже 1.8.3. Локальный EXE 1.8.2 — кандидат: версия и hash известны, точная связь с исходниками и статус проверки не доказаны |
| PC 1.8.4 | Отдельного PC-тега нет; официальные EXE есть в [tab5-v1.7.0](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.7.0) | x64 локальная копия имеет тот же SHA-256, что GitHub asset. Локальные STM32-сборки той же версии имеют другие hash |
| STM32 FW 1.4.1 | Версия зафиксирована в Git между 1.4 и 1.5 | Отдельного релиза и доказанного оригинального BIN в просмотренных каталогах не найдено |
| Tab5 FW 1.8.0–1.8.3 | Сохранены исторические ZIP, app descriptor и receipts | Нет отдельных GitHub-релизов. Готовность архива не означает аппаратную проверку |

Другие версии Tab5 без опубликованного компонента (найдены в истории
исходников и/или embedded app descriptor): **1.5.0-esp32, 1.6.0-esp32, 1.6.1-esp32, 1.6.2-esp32, 1.6.3-esp32, 1.6.4-esp32, 1.6.5-esp32, 1.6.6-esp32, 1.6.7-esp32, 1.6.8-esp32, 1.6.9-esp32, 1.6.10-esp32, 1.6.11-esp32, 1.6.12-esp32, 1.6.13-esp32, 1.6.14-esp32, 1.6.15-esp32, 1.6.16-esp32, 1.7.1-esp32, 1.7.2-esp32, 1.7.4-esp32, 1.7.5-esp32, 1.7.6-esp32, 1.7.7-esp32, 1.7.8-esp32, 1.7.17-esp32, 1.8.0-esp32, 1.8.1-esp32, 1.8.2-esp32, 1.8.3-esp32**.
Это список наблюдаемых версий, не непрерывная последовательность и не
обещание готовых пакетов для каждой. Часть ранних файлов — рабочие BIN,
для них не установлен полный provenance релизного комплекта.

### Сохранённые локальные пакеты Tab5

Пути относительно локального `C:/Projects/ClaudeExp/inclinometer-feedback/`.
Они не являются ссылками на скачивание GitHub. SHA-256 относится к целому ZIP; аппликационная
версия дополнительно прочитана из бинарного app descriptor внутри архива.

| Версия app descriptor | Сохранённый пакет | SHA-256 ZIP |
|---|---|---|
| 1.8.0-esp32 | `2026-11-04-first-dual-mode/Tab5-1.8.0-first-dual-mode.zip` | `dea2624d849a61797b96bd07c475f0b6ccab771bfbc6aafb8981ac1be66a46fc` |
| 1.8.1-esp32 | `2026-11-04-relative-workspace/Tab5-1.8.1-relative-workspace.zip` | `6ed434c050bf87eeb938e0a0c04ad08ac179645b6d11bd2f8734501a42ffb723` |
| 1.8.2-esp32 | `2026-11-05-flash-743273/Tab5-1.8.2-P4-rev1.zip` | `d89f7b2dbbb9485abb9147f1cb44247f6f0cc1acdf68f6a0bf31b11f4198990e` |
| 1.8.2-esp32 | `2026-11-05-controls-743282/Tab5-1.8.2-controls-P4-rev1.zip` | `60c2d7125d64640eb7e2e9fb90d110ff349b09efd50fda3bffbf732609501977` |
| 1.8.3-esp32 | `2026-11-05-encoder-signals-743377/Tab5-1.8.3-controls-P4-rev1.zip` | `a15dae106c7afe533630b8ec20994020662aa798d5649af305390501b9442cdd` |

Receipts программной сборки 1.8.0/1.8.1 и 1.8.3 прямо содержат
`hardware_tested:false`. Отдельные более поздние flash receipts для
1.8.1/1.8.2/1.8.3/1.8.4 — исторические записи о конкретной прошивке,
а не подтверждение метрологии или универсального PC 1.9.0. В рамках
этой инвентаризации ни устройство, ни прошивка не запускались.

## Правила сохранения истории

Существующие теги, названия assets и оригинальные байты сохраняются.
Порядок наводится каталогом, явными ссылками и отдельными новыми релизами
компонентов. Если оригинальный файл не найден или происхождение не доказано,
это указывается прямо. Новая сборка старого commit не выдаётся за прежний
оригинал; при необходимости она получает отдельное обозначение и дату.

Полный машинный инвентарь (commit тега, id, даты, размеры, GitHub digest,
локальные SHA-256, source history, receipts) и план для выпускающего находятся
в локальном feedback: `2026-11-11-universal-745101/release-audit/`.
Публикация новых файлов, релизов и замена снимков выполняются после проверки
нового комплекта. Эта работа подготовила документацию и инвентарь.
