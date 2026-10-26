# Пакеты Tab5 1.7.0-esp32 для предварительного релиза

Опубликован [tag `tab5-v1.7.0`](https://github.com/Sasavn/heel-test-inclinometer/releases/tag/tab5-v1.7.0),
**prerelease**, на коммите firmware `5f9b33b`. Отдельный tag в том же
репозитории сохраняет стабильный STM32/PC-релиз по `releases/latest`.
Документация benchmark добавляется последующим коммитом; firmware не менялась.

| ZIP | Допустимые silicon revisions ESP32-P4 |
|---|---|
| `Krenomer-Tab5-1.7.0-esp32-p4-rev1.zip` | 100–199 |
| `Krenomer-Tab5-1.7.0-esp32-p4-rev3.zip` | 300–399 |

Ревизия — свойство микроконтроллера; модель экрана и внешний вид планшета
не заменяют её проверку. Оба ZIP содержат файлы с одинаковыми именами;
их следует распаковывать в разные папки.

Состав каждого ZIP:

- `krenomer-esp32.bin` — **только этот BIN является OTA-приложением**;
- `krenomer-esp32.json` — версия, диапазон silicon revisions, SHA256,
  адреса factory parts и контрольные суммы;
- `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin` —
  отдельные части для явно выбранной factory/recovery-установки;
- `README.txt`, `SHA256SUMS.txt`.

NVS-образ и команда стирания flash не включены. Factory OTA-data меняет
выбор загрузочного слота; для обычного обновления нужен OTA-app путь.
Прошивка P4 не обновляет радио C6. Это не прошивка STM32.

Сборка и проверка ZIP без обращения к устройству:

```powershell
python tab5/tools/package_prerelease.py --rev1 build-native --rev3 build-native-rev3 --out .tools/delivery-tab5-native-1.7.0
```

Скрипт проверяет application descriptor, chip ID, имя проекта, версию,
диапазоны ревизий, все хеши и чтение каждого файла из готового ZIP.
Итоговые размеры/SHA256 публикуются в `delivery.json` и `SHA256SUMS.txt`
рядом с ZIP. После любых изменений прошивки пакеты нужно создать заново.

Для notes релиза: **hardware-unverified**. Сборки и host-тесты выполнены;
запуск этой версии, реальный FPS/latency, WiFi/NTP и удержание RTC на
плате не проверены. Аппаратная работа остаётся на паузе.
