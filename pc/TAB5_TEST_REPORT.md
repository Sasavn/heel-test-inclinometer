# Tab5 PC: отчёт проверки, 21.10.2026

Ветка `tab5-esp32`, изолированный worktree `diplom_project_tab5`.
Изменения PC ограничены `pc/`. Работа сохраняется локальными коммитами;
push и releases не выполнялись.
Код PC готов к интеграции и заморожен после перечисленных проверок.
Артефакты прошивки ниже зафиксированы SHA256: последующие пересборки firmware
не считаются проверенными автоматически.

## Изменения

- Ручной TCP IPv4/порт5000, неблокирующий Winsock, очередь частичных записей,
  авторизация до `ver`; токен скрыт из журнала и не сохраняется.
- P4 USB Serial/JTAG CDC (VID303a) выбирается вручную. Автопоиск ESP не добавлен.
- Идентификация `board=tab5 chip=esp32p4 proto=2`, совместимость samples1.5,
  команды записи `rec on/off`, запись SD или USB учитывается независимо.
- Неизвестные CPU/rate/counters отображаются как неизвестные; отсутствующая
  батарея не превращается в0% или тревогу.
- Отдельные STM32 DFU, ESP OTA приложения и factory/ROM recovery. OTA проверяет
  архитектуру, descriptor/project, границы/контрольную сумму сегментов, SHA256,
  точные offset/ACK. Есть отмена, повтор потерянного ACK и новая сессия после
  разрыва. Успех требует нового подключения, ожидаемой версии и boot_confirmed.
  Одновременные ACK commit и disconnect обрабатываются как ожидаемая перезагрузка.
- Native ROM адаптер на Win32 overlapped serial,1152008N1, явный COM. Проверка
  четырёх частей до открытия порта, NVS сохраняется, OtaData8192FF пишется последней.
  Системного Python/esptool для запуска exe не требуется.
- Переносимый `build-tab5.cmd` собирает локально x64/x86 и запускает все8 suites;
  существующий vcpkg можно передать вторым параметром без изменения его файлов.

## Финальные CTest: обе архитектуры, 8/8 PASS

| Suite | x64 checks | x86 checks |
|---|---:|---:|
| protocol_test |261|261|
| link_test |157|157|
| firmware_test |33|33|
| process_test |1280|1280|
| esp32_test |72|72|
| esprom_test |1171|1171|
| recovery_test |12|12|
| ui_shots |394|394|
| Всего |3380|3380|

Последние полные логи: `build-tab5-x64/Testing/Temporary/LastTest.log` и
`build-tab5-x86/Testing/Temporary/LastTest.log`. Все8 имеют `Test Passed.`.
Старый LastTestsFailed.log не является итогом последнего запуска.
В UI исправлен найденный выход старой STM32 страницы за допустимую высоту.

Проверка импортов Windows7 прошла для обоих exe: x64 —341 функций,
x86 —334. Это проверка импортов, а не физический запуск на Windows7.

## Production C bridge

`tests/run-service-integration.cmd`: **77 checks,0 failures**. PC state machine
работает с настоящим `tab5_services.c`; заменены только границы flash/media.
Пройдены begin/chunk/end, scheduled reboot, reconnect, boot confirmation.
GCC нужен только для этой дополнительной разработки/проверки, не для runtime exe.

## Настоящие образы SDK, без исправления заголовков

На x64 и x86 пройдены `esp32_test --image`:73 checks на каждый профиль;
`recovery_test --manifest`:13 checks на каждый профиль,4 настоящие части.

| Профиль | App bytes | Silicon range | App SHA256 |
|---|---:|---|---|
|tab5/build|1258720|100..199|`2ef63ffb0e718153fbea52517b21359fcfdea99e95fe0f78abe2a938dfa08dd8`|
|tab5/build-rev3|1260064|300..399|`df0c9e04a111f22376b1ea8294c133880ebf373b018cee3f675fefab1dc686bb`|

Полный ROM MOCK с фактическими bootloader/app/partitions/otadata на **x64 и x86**:

| Package | Mock silicon | Результат |
|---|---:|---|
| v1, headers100..199 |100|4 MD5,1293296 verified bytes,1264 DATA packets, NVS preserved, ota0, END1;8 checks|
| v1, headers100..199 |300|WrongRevision: erase/DATA/register writes/END0, NVS и ota1 сохранены;4 checks|
| rev3, headers300..399 |300|4 MD5,1294704 verified bytes,1265 DATA packets, NVS preserved, ota0, END1;8 checks|
| rev3, headers300..399 |100|WrongRevision: erase/DATA/register writes/END0, NVS и ota1 сохранены;4 checks|

Команда: `esprom_test --package PACKAGE_FOLDER DEVICE_REVISION`.
Версия1.5.0-esp32 одинакова у этих двух отдельно собранных SDK profiles.
Таблица обновлена после исправления startup USB/network: повторены все проверки
реальных образов и manifest, все четыре ROM-сценария на x64 и x86, production C
bridge (77/77). Последняя проверка сервисов: 234844 assertions основного набора
и 86 проверок старта. Это не аппаратные испытания.
ROM Options maxRevision65535; реальные min/max обоих image headers обязательны.
Для 301/302 реализована официальная последовательность питания flash и
отдельная ветвь ECO7 с DOWNLOAD_MODE_XPD_ON: см. ROM_POWER_SEQUENCE.md.
Это не общий произвольный предел max300.

## Артефакты PC

- `build-tab5-x64/krenomer-esp32.exe`, SHA256 `ebd6285018115d9f1167faa38713f569dd9d0663b92e2c633b93eab9fb64f7bf`
- `build-tab5-x86/krenomer-esp32.exe`, SHA256 `944390d14a6c72b404684d8ba0cab5f4410ce0d35e5f0543ad2e1908107cc6cf`

## Дополнительная приёмка ROM 301/302 и русского интерфейса

Обе программы пересобраны, Win7 imports повторно прошли (341/334 функции).
Полные ctest x64/x86 — 8/8. В ROM-наборе 1171 проверка: точный порядок
read/write/delay, все состояния младших битов DATE на ECO7, отсутствие eFuse
write, отказы/таймаут/отмена каждого шага до attach/erase, повреждение каждого
из четырёх MD5. Источник и алгоритм — [ROM_POWER_SEQUENCE.md](ROM_POWER_SEQUENCE.md).

На x64 и x86 повторены 28 artifact-прогонов: оба настоящих пакета, image и
manifest, ROM 100/300/301/302 и 302 с `--xpd-on`. Rev3 пакет на 301/302
успешно проверяет все четыре части (1294704 байта), NVS сохранена. Пакет rev1
на этих виртуальных ревизиях отвергается до любых register/erase/DATA/END.
Прошивки не менялись и не пересобирались на этом этапе; их SHA256 выше прежние.

Подписи подключения, OTA и первой прошивки переведены на русский; wire-команды
не изменены. Исходная техническая причина ошибки доступна в подсказке.
Две страницы проверены без горизонтальной и вертикальной прокрутки на
1280×800 и 1024×700; четыре PNG просмотрены после последней сборки:
`tab5_firmware.png`, `tab5_factory.png` и их варианты `_1024x700` в `docs/img`.

Снимки для документации (виртуальный прибор, программная растеризация):

- [OTA](docs/img/tab5_firmware.png)
- [Factory / ROM](docs/img/tab5_factory.png)

Полное описание использования: [TAB5.md](TAB5.md).

## Ограничения и границы доказательств

Ни один COM-порт или физический прибор не открывался; приложение с GUI не
запускалось. TCP проверялся только на loopback, UI — без окна и OpenGL.
Реальная прошивка, power-cut, reset DTR/RTS, радио и Win7 с COM-драйвером не
проверены на железе. Физическая пригодность из mock результатов не выводится.

ROM подтверждает P4 chip/revision/security и валидирует layout пакета. Он не
доказывает физическую модель Tab5: board-ID fuse нет. Прибор и COM выбирает
пользователь. OTA дополнительно сверяет firmware-reported board/chip/protocol.
Win7/8 требует работающего USB Serial/JTAG COM-драйвера по официальному руководству
Espressif; ничего не устанавливалось. TCP сейчас принимает числовой IPv4;
signed-image trailers в OTA намеренно не поддержаны.
