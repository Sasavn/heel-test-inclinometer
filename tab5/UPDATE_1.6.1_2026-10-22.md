# Tab5 1.6.1-esp32 — проверка Roboto-интерфейса, 22.10.2026

Дизайн уже был закоммичен в **f085c29** поверх a7a18e5: Roboto Regular/Medium/
Bold с лицензией Apache2.0, DejaVu для отсутствующих знаков, заголовки32px,
декор баннера, пиктограммы, индикаторы носителей, пунктир легенды и выбор темы.
Дизайн сохранён. Повторного или пустого коммита интерфейса не создавалось.

## Необходимые изменения и происхождение сборок

- Версия в PROJECT_VER повышена до **1.6.1-esp32**, чтобы отличать новый комплект
  от ранее переданного1.6.0. Native fixture и версия regression обновлены.
- Строгая сборка IDF нашла2 `-Werror=misleading-indentation` в новом UI. Разделены
  две строки операторов; C-токены и поведение относительноf085c29 неизменны.
- Исходные Roboto TTF, генератор и все8 generated fonts побайтно соответствуют
  f085c29. Математика, storage/OTA, main, PC production и STM32 не менялись.
- Обе прошивки реально пересобраны из текущего дерева. В ELF обеих есть новые
  font_tab5_16m/20m/32/64 и нет старого28; descriptor/manifest/actual-image test
  читают1.6.1-esp32. Это не старые a7a18e5 бинарники с переименованным файлом.
- [Машинный provenance с SHA исходников и частей](docs/BUILD_1.6.1_2026-10-22.json).

## Готовые комплекты

| Silicon P4 | Каталог от корня форка | Размер app, байт | SHA-256 app |
|---|---|---:|---|
| 100–199 | `tab5/build/package` | 1,474,224 | `3737bc29cebbb36232510f828ceeab2bd1d384abe62623967a8a521da5f182a6` |
| 300–399 | `tab5/build-rev3/package` | 1,475,648 | `a9360d940f85e94059f28086086d7858077925acb6d43328cb4ebb87d538b3df` |

Каждый package включает4 factory-части с прежними адресами, JSON и проверенный
SHA256SUMS.txt. Два диапазона silicon получены отдельными IDF-сборками,
заголовки вручную не исправлялись. Все3 профиля панели сохранены в каждом.

## Реальные выполненные проверки

- Настоящий offscreen LVGL: **76 сценариев/кадров,30 callbacks,0 geometry failures**.
  Проверены обе темы, крупные±89.99°, steppers/удержание, число/календарь,
  RU/EN-клавиатуры, confirmations, негативный backend/NVS ack и retry.
- Все8 шрифтов:210 glyphs в16/16m/20/20m/24/32/40,18glyphs в64; cmap PASS.
- Pure graph:14 групп/**44661** проверка; production BSP init-loop/guards/
  mutations PASS; measurement PASS; календарь20; package4/4.
- Сервисы:**234848** assertions, startup86; PC↔production C service bridge77/77.
  Добавлено доказательство отказа downgrade1.6.1→1.6.0.
- Готовые файлы: **28/28** запусков x64/x86×2package: полная OTA
  1.5.0→**1.6.1** с передачей всех байтов, проверкой reboot/boot confirmation,
  несовместимого silicon до begin; recovery manifest; ROM100/300/301/302 и302XPD_ON.
- PC x64/x86:8/8 ctest на каждой архитектуре (журналы .tools/pc-161-*.log).
  Исполняемые production файлы PC1.8.2 не изменены.
- Parent отдельно принял внешний вид и выполнил10/10 pixel boundary проверок;
  этот неизменившийся layout повторно не измерялся здесь.

## Новые снимки

24 curated снимка обеих тем в [docs/img](docs/img/) обновлены из настоящего
display flush `tests/output_ui/roboto161/`, версия fixture1.6.1. Это LVGL,
не PNG из HTML. [Измерения](docs/img/measure_light.png),
[тёмная тема](docs/img/measure_dark.png), [график](docs/img/chart_dark.png),
[настройки](docs/img/settings_light.png), [сеть](docs/img/network_light.png).
График использует явно синтетические timestamped samples с пропуском.

## Ограничения

Не открывались физические COM/USB, GUI и Telegram; не было push или flash.
Эти тесты не подтверждают реальную панель/тач, конкретный P4/C6, питание,
работу двух носителей, power-cut/rollback или Windows7 на аппаратном ПК.
Capacity/history/peer backend по-прежнему неизвестны; новый дизайн не добавляет
сканирования файлов и не означает, что после подключения появится история.
Упаковку и передачу выполняет parent; ранее отправленный1.6.0 архив не менялся.
