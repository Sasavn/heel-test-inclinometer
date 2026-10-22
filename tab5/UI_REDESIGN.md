# Tab5 UI: предоставленный дизайн, итог 22.10.2026

Firmware1.6.0-esp32, база d49218b. Перенесён именно предоставленный
[SPEC](design/SPEC.md), [HTML](design/mockup.html), [14 эталонных PNG](design/png/).
Ранее отвергнутый dashboard и его37-кадровый отчёт не являются этим результатом.
Настоящие итоговые снимки: [docs/img](docs/img/), не изображения HTML-макета.

## Реализация

- 1280×720, rail104px с пятью страницами: измерения, график, запись, настройки,
  сеть. Баннер128px, две карточки564×376, уровни200×200, углы64px, четыре нижние
  карточки. Обе темы предоставленной палитры; оранжевый старт и красный стоп.
- Нативные LVGL формы, шрифты16/20/24/28/40/64, векторные пиктограммы.
  Steppers с удержанием, числовой modal600×616, RU/EN-клавиатура, календарная
  проверка даты, подтверждение нуля и неприменённых изменений.
- В состоянии UI явные known-флаги. Имя/длительность записи, состояния
  носителей, число клиентов, часы и panel trace приходят из main. Неизвестные
  capacity/recent/peer address показаны неизвестными, без примеров из макета.
- История графика30с/2мин/10мин строится по новым timestamp измерений через
  отдельный production helper. На снимке chart — синтетические host-отсчёты
  с разрывом; в production нет генератора кривой.
- Backend notice виден в закрываемом уведомлении. Serial/kind/success результата
  действия независим от текста: ошибка запуска записи/RTC/сети/NVS не выглядит
  успехом. Pending снимается при отказе, черновик остаётся доступен для повтора,
  в том числе когда runtime уже изменился, а NVS не сохранилась. Повтор той же
  ошибки с новым serial показывается снова после закрытия уведомления.

## Что проверено

```
python tab5/tests/run_ui_test.py --out tab5/tests/output_ui/provided
python tab5/tools/gen_fonts.py --check
python tab5/tests/test_ui_graph_run.py
```

Последний независимый запуск main после IDF-format/type fixes: **76 сценариев,
30 callback, 0 geometry failures**. Настоящий LVGL9.3 software renderer, pixels
получены через display flush, реальные координатные клики через pointer indev.
Окно ОС, SDL и аппаратные порты не используются. Runner даёт LVGL8MiB heap;
это тестовый лимит, не измерение production heap на P4.

Тест проверяет координаты основных блоков по отдельным SPEC-значениям (±4px),
границы экрана и touch-target не меньше56px, каждый ключ клавиатуры, ширину
glyph advances крупных углов. Стресс: ±89.99° на обеих осях обоих датчиков,
разность±179.98°, SSID32байта, длинные строки, offline, запись с ошибкой носителя.
Функционально проверены start/stop/zero/reset, steppers/удержание, ввод и отказ
75Гц, темы, RU/EN ввод, пароль, dirty-navigation, Gregorian date и24:00 отказ,
settings/network actual-state ack, typed configure/NVS failure, retry и success,
повтор одинаковой ошибки с новым serial и закрытие уведомления.

Независимый внешний pixel checker parent и main прошёл **10/10 page/theme**:
rail/background palette и границы всех заданных карточек±4px. Эти проверки не
доказывают отсутствие любого возможного пересечения текста; итоговые ключевые
снимки дополнительно просмотрены main и parent. Parent принял визуальный итог.
Исправлены найденные обрезания углов и chart legend, перекрытие TCP текста,
невидимая риска порога, пустой fixture графика и потерянный backend feedback.

Шрифты:209 glyphs в каждом16/20/24/28/40 и18 в64px; cmap и используемые UI
символы проверены, пропусков нет. Graph:14 групп/44661 независимая проверка;
подробности в [GRAPH_TESTS](GRAPH_TESTS.md). IDF после финала собран для обеих
silicon-групп без warnings, artifact-регрессии — в [общем отчёте](UPDATE_2026-10-22.md).

## Снимки для просмотра

- [Измерения, светлая](docs/img/measure_light.png), [тёмная](docs/img/measure_dark.png).
- [Запись/ошибка](docs/img/measure_recording_error_dark.png), [крайние углы](docs/img/measure_extremes_light.png).
- [Накопленная история](docs/img/chart_dark.png), [носители](docs/img/recording_light.png).
- [Настройки](docs/img/settings_light.png), [ввод числа](docs/img/numeric_light.png).
- [Сеть](docs/img/network_light.png), [RU-клавиатура](docs/img/keyboard_ru_dark.png).
- [Негативный NVS ack и retry](docs/img/network_error_dark.png), [повтор ошибки записи](docs/img/record_error_dark.png).

Полные76 кадров и geometry.txt находятся в ignored tests/output_ui/provided/.
В docs/img сохранены24 выбранных кадра обеих тем плюс совместимое network.png.

## Ограничения и фиксация исходников

Не измерены физический экран/тач, доступная память и FPS на Tab5. Не проверены
пальцем реальные попадания, Wi-Fi/C6, SD/USB, RTC и аппаратное питание. Host
callbacks и snapshots не заменяют HIL. Математика, Modbus, wire/CSV и PC
production-код не менялись. Main-owned интеграция даёт состояние и typed ack;
UI не выполняет запись файлов, flash или сетевые операции самостоятельно.

SHA-256 финального исходного среза (после IDF warnings fixes и повторного UI run):

| Файл | SHA-256 |
|---|---|
| `tab5/main/tab5_ui.c` | `023eb365519acd6b76b11f9c1d6d482933c3bdf9a84f5b7c26c6448d60285932` |
| `tab5/main/tab5_ui.h` | `30245cfc299330c0cd4c3205854e391299e9863b4cc036a0ca064be520007410` |
| `tab5/main/tab5_ui_graph.c` | `daf0501d5fba10fab7c3e9884c48163578993317102b803760f88014241dfbe7` |
| `tab5/main/tab5_ui_graph.h` | `855f6bbd1008f9a25ab1a87b70b699e50aa8719c06101784abe399e9df7cb79b` |
| `tab5/tests/test_ui.c` | `3e6647f0e2ef3486cc9bd0ac077b2130f9f17f410b2b49b4f8da85d66512e2d5` |
| `tab5/tests/run_ui_test.py` | `b884bdf804eac812f097f8156c1fac8ee51747c7958006df6c10d9663ab97314` |
| `tab5/tools/gen_fonts.py` | `4a2712475ee8234df3dbed0f61638e74a60bad530ae126c48d6d6d3afbd323b9` |
