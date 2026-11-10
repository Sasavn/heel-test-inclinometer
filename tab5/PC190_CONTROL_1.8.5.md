# Tab5 1.8.5 — управление из PC 1.9.0

Рабочая копия: `C:/Projects/ClaudeExp/dp745101-esp`, ветка
`feat/tab5-pc-level-745101`. Изменения ограничены `tab5/`.
Контракт: `inclinometer-feedback/2026-11-11-universal-745101/CONTRACT.md`
и уточнения владельца от 2026-11-11. Публикация и тег `tab5-v1.8.5` — у parent.

## Что реализовано

- USB/TCP: `instrument heel|level`, `relative roles 2 3|3 2`,
  `relative axes confirm|reset`, `relative zero`, `relative reset`,
  `relative status`. Режим подтверждается обычным статусом после `OK instrument`.
- В обычном статусе рядом с `dual_mode_cap:true` добавлены
  `instrument_control_cap:true` и `relative_data_cap:true`.
- Все прежние поля relative JSON сохранены. Добавлены подтверждение осей,
  исходные координаты, разность до/после REL, отдельный offset, возраст пары
  и пределы 500000/50000 мкс. Числа берутся из `r.source`, `r.live`,
  `r.relative`; разности доступны только при `ready`. NaN/Inf и неизвестный
  возраст представлены `null`. Синхронизация аппаратуры не заявляется.
- Мутации проходят через существующую очередь `actions` и основной поток.
  Используются прежние `apply_dual_action` и атомарная reservation
  `tab5_services_control_begin/end`. USB/TCP не записывают dual-state напрямую.
  В очереди лежит оболочка UI-действия либо указатель на ожидающий запрос.
  Отправитель ждёт завершения без `app_mutex`; ответ отправляет сам отправитель.
- Запись, STOP/draining, transfer, активная/завершённая OTA и занятые блокировки
  запрещают изменения. Relative status разрешён во время busy.
  Неверные команды, переполнение очереди, отказ выделения semaphore, ошибки
  NVS и недоступный REL-нуль не меняют конфигурацию, offset и generation.
- Wire-режим сохраняется как startup mode, роли/оси — в прежнем `dual_cfg`.
  REL-нуль остаётся transient и не меняет нули головок. Изменения создают
  границу generation, после которой нужны новые ответы обеих головок.
  Прежняя UI-семантика active mode / отдельного startup mode и UI REL capture
  сохранена. Алгоритм приёма/публикации пар не изменялся.
- Дополнительно: `files sd|usb`, `get sd|usb <basename>` без fallback при
  явном выборе недоступного носителя. Старые `files`, `get <basename>`,
  `get abort` и F/G/D/E-ответы сохранены, включая SD-first fallback.

Файлы реализации: `main/main.c`, `main/tab5_ui.h`, `main/tab5_services.c`.
Версия: `CMakeLists.txt`, `1.8.5-esp32`. Wire-описание: `PROTOCOL.md`.
`build.cmd` теперь уважает заданные `IDF_PATH`/`IDF_TOOLS_PATH`, сохраняя
прежние пути по умолчанию. Три native test runner принимают `--out`, чтобы
все результаты можно было держать внутри этой рабочей копии.

`main/tab5_ui.c`, renderer, measurement/relative core и прежний
`TAB5_SERVICE_RELATIVE_HEADER` не менялись. Старые REL CSV не требуют миграции.

## Проверки

Новый тест:

```powershell
python tab5/tests/run_pc_dual_protocol.py --out tab5/build-host/pc-dual
```

Он компилирует извлечённые без изменения логики production-функции:
USB loop, общий `command`, parser, основной queue consumer, dual actions,
NVS, обычный status и relative JSON. Для TCP исполняется точная строка
authenticated dispatch из `network_task` с source guard её положения после
аутентификации. Service core используется настоящий, cJSON — из установленного
SDK; заменены только OS/NVS/media/driver boundaries. TCP sockets не открываются.

Проверены оба транспорта: строгий синтаксис/адреса, NVS write/commit rollback,
reboot, transient REL, независимость от head zero, generation/fresh boundary,
принятая пара при обновлении только одной головки, неизменные RX timestamps/
sequence, NaN/±Inf → null, stale/not-ready → null, все busy guards, статус при
busy, очередь/semaphore failure, UI/physical-record envelope и отказ очереди.
Явные SD/USB get проверяют разные payload одного basename; недоступный SD
не подменяется USB, старый неявный выбор продолжает fallback.

Регрессии:

| Проверка | Результат / локальный журнал |
| --- | --- |
| `tests/run-host.cmd` | PASS: measurement/time/resources, 4 package tests; `build-host/run-host-final.log` |
| `run_relative.py` | PASS; `build-host/regressions/run_relative/` |
| `run_dual_runtime.py` | PASS: UI actions/NVS/record route/workspace; `build-host/final-owner-regressions/run_dual_runtime/` |
| `run_dual_startup.py` | PASS: 412544 checks, exact app_main startup + services; `build-host/final-owner-regressions/run_dual_startup/` |
| `run_relative_services.py` | PASS: запись/старый заголовок CSV; `build-host/regressions/run_relative_services/` |
| `test_services_run.py` | PASS: 234853 core checks, 96 startup checks, retry/backoff; `build-host/additional/services.log` |
| `run_workspace.py` | PASS: 51 checks; `build-host/regressions/run_workspace/` |
| workspace router/freshness/epoch | PASS; `build-host/regressions/`, final freshness: `build-host/final-owner-regressions/` |
| `run_measure_events.py`, `run_coherent_pair.py` | PASS: faults, both-new publication, regression negative control; `build-host/regressions/` |
| `run_native_dual.py` | PASS: 85 checks, две темы, partial/full equality; `build-host/regressions/native-dual-current-ui-final/` |
| `run_native_workspace.py` | PASS: 1323 checks, no growth; `build-host/regressions/run_native_workspace/` |
| `run_native_snapshot_owner.py` | PASS; `build-host/regressions/run_native_snapshot_owner/` |
| `run_native_dual_performance.py` | PASS: 830 checks, idle snapshot без redraw; `build-host/regressions/run_native_dual_performance/` |
| `run_native_ui_test.py --opt-group all` | PASS: press/dirty/cache-state/lifetime/cache-budget; `build-host/native-optimization/` |
| `test_theme_action.py` | PASS: 64 backend-confirmed transitions; `build-host/additional/theme.log` |
| `run_level_test.py` | PASS; `build-host/additional/level.log` |
| P4 `run_ui_stack_guard.py --build tab5/build` | PASS: app_main 1872 ≤ 2048; selected chain 3424 ≤ 6144; `build-host/additional/ui-stack-final/` |

Старый native-dual fixture ожидал окно прибора по четвёртому пункту страницы
измерений. На исходном HEAD он тоже падал: этот пункт переключает единицы,
настройки прибора находятся на странице настроек. Сохранён отрицательный
контроль `build-host/native-dual-baseline/`; исправлен только fixture под
текущую навигацию, без изменения production UI. Первоначальные журналы ошибок
сохранены рядом с финальными результатами.

Рост кадра app_main от оболочки очереди устранён: оболочка static и принадлежит
только main. Лимиты stack guard не повышались. Это проверка собственных кадров
P4 compiler, а не доказательство runtime stack high-water.

## Сборка и ограничения

Сборка выполняется установленным ESP-IDF 5.5.2 / RISC-V GCC 14.2.0, без установки
SDK или получения неизвестных исходников. `managed_components` скопированы из
разрешённого original tree в `tab5/managed_components` этой рабочей копии.
Чужие CMake build caches не перенастраивались.

```powershell
$env:IDF_PATH='C:\Projects\ClaudeExp\diplom_project_tab5\.tools\esp-idf'
$env:IDF_TOOLS_PATH='C:\Projects\ClaudeExp\diplom_project_tab5\.tools\idf-tools'
$env:IDF_COMPONENT_MANAGER_OFFLINE='1'
cmd /c tab5\build.cmd
```

Полная конфигурация/компиляция default profile и упаковка выполнены. После
изменения хранения queue envelope объект `main.c.obj` в собственной сборке
явно инвалидирован для финальной пересборки при сохранённых timestamp исходника.
Артефакты и окончательные хеши — в `build/package/` и `build-host/verification-1.8.5.json`.

Проверки выполнялись только на хосте; COM/оборудование/прошивка не использовались.
Host QPC не является аппаратной скоростью UI. Полная старая 98-frame fidelity
матрица не запускалась; выполнены перечисленные native/UI/partial-full проверки.
Собран default profile для ESP32-P4 revision 100..199; отдельные rev3/controls
профили здесь не собирались. Публикации, push и создания release tag нет.

Финальный образ `build/package/krenomer-esp32.bin`: 1869184 байт,
SHA-256 `2d37cc2843e5ac3e1e98f1621eba9304c95567093cb72caae611626e58d1b2af`. Версия app descriptor проверена:
`1.8.5-esp32`. Все SHA-256 частей package сверены с фактическими файлами.
