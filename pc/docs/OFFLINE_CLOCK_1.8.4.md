# PC 1.8.4: офлайн-часы от ПК

Проверено 2026-10-26. Правки только в pc/ обеих рабочих копий. Без commit/push,
Telegram, GUI, COM, устройств и внешней сети. TCP-тесты только 127.0.0.1.

## Поведение

Прежняя кнопка «Синхронизировать время с ПК» отправляет местное время ПК по
текущему соединению. При явном status boolean time_tz_offset=true добавляется
` tz=N` (минуты текущего UTC offset с DST, −720…840 с шагом 15; 0 допустим).
Legacy STM32 и старый Tab5 получают только `set time YYYY-MM-DD HH:MM:SS`.
Новый backend Tab5 отвечает OK time лишь после system+RTC+persist. PC показывает
его ERR полностью; отдельный generic OK не подтверждает установку времени.
Legacy `(rtc no)` явно показан как частичный результат. Подпись RTC нейтральная.

Capability используется только после status текущего соединения, чтобы кэш
Tab5 не попал в STM32 после переподключения. Автосинхронизация не добавлялась.
Расхождение часов при capability+timezone_minutes считается через canonical UTC:
naive calendar UTC минус offset. Legacy сохраняет прежнюю семантику PC local.

## Файлы в каждой pc/

- CMakeLists.txt: версия 1.8.4 и регистрация clock_sync_test.
- src/ui/App.hpp: согласованная kAppVersion 1.8.4.
- src/ui/App.cpp: запись истории 1.8.4.
- src/ui/AppSettings.cpp: прежняя кнопка, pending/ACK/error, RTC label, пояс и UTC drift.
- src/core/Protocol.hpp: явный capability и проверенный optional offset.
- src/core/ClockSync.hpp: построение команды, offset/DST, status UTC, классификация ACK.
- tests/clock_sync_test.cpp: одинаковый новый headless test в обеих копиях.
- README.md: версия и описание протокола/офлайн-операции.
- docs/OFFLINE_CLOCK_1.8.4.md: этот отчёт.

## Проверки

Чистые сборки и финальная incremental-пересборка: MSVC v143 14.44, Release,
Win7 x64/x86, static deps; VCPKG_MANIFEST_INSTALL=OFF, Tab5 использует main cache.
Все четыре проверки импортов Win7 PASS; это не проверка запуска на реальной Win7.
GUI exe и ui_shots не запускались; существующие снимки сохранены.

CTest без ui_shots: main x64 10/10, main x86 10/10,
Tab5 x64 13/13, Tab5 x86 13/13, всего 46/46 PASS.
Новый clock_sync_test: main 92 проверки на архитектуру; Tab5 112 на архитектуру.
Покрыты explicit boolean capability, UTC0/invalid offset, DST, fractional zones,
переход суток/года, canonical drift разных поясов, stale status, USB SimConnection
и SimDevice через DeviceLink, ACK/RTC error/generic OK/timeout/disconnect/retry.
Tab5 дополнительно: production TcpConnection+DeviceLink с authenticated loopback
peer, fragmented ACK, timezone suffix, RTC error. Реальный USB adapter/железо не
тестировались; производственные SerialConnection/TcpConnection не изменялись.

Первый параллельный общий CTest упёрся в общие имена старых тестов в %TEMP%
(link/process, включая segfault при гонке файлов). Финальный повтор использует
отдельные TEMP/TMP внутри каждой build-папки: все тесты PASS без изменения их кода.

Логи в каждой pc/build-win7/ и pc/build-win7-x86/:
clock-build.log (clean), clock-build-final.log (final),
clock-ctest.log (первый конкурентный прогон), clock-ctest-isolated.log (final PASS).

Защищённые исходники src/tests/docs сверены с исходными SHA256: вне списка
изменились только согласованные App.cpp/App.hpp для версии. TCP/OTA/ROM/encoder,
другие тесты и изображения не изменены. STM и Tab5 firmware не редактировались.

## Исполняемые файлы

- `C:\Projects\ClaudeExp\diplom_project\pc\build-win7\krenomer.exe` — 5886976 bytes; SHA256 `a8ef4fd91ecd94efab5dcb4e355de7c08ff201a5f008abb37d24f650b8a18d13`
- `C:\Projects\ClaudeExp\diplom_project\pc\build-win7-x86\krenomer.exe` — 5471744 bytes; SHA256 `42dbc97a581b699b7c4bf8ef27da677bbe0cb2ee44dc7988d50cc0a1a733cc44`
- `C:\Projects\ClaudeExp\diplom_project_tab5\pc\build-win7\krenomer-esp32.exe` — 6018048 bytes; SHA256 `338be53bee49c7b68d82223637f52334f640237f00567e8d8b6b7e1489f58a76`
- `C:\Projects\ClaudeExp\diplom_project_tab5\pc\build-win7-x86\krenomer-esp32.exe` — 5584384 bytes; SHA256 `5dcbc35b3df531c8017507d2f9255de0fdb66f3488788c08ade0acbc8aa5ca0c`
