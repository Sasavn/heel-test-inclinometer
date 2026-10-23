# Storage 1.6.15-esp32

Дата фиксации документации: 23.10.2026. Аппаратных испытаний этой версии,
подключения устройств и прошивки в рамках проверки не было.

## Изменения

Экран и ввод запускаются до optional storage/radio. Монтирование SD выполняет
отдельный worker: проблемная карта больше не удерживает запуск UI в синхронном
mount перед созданием экрана. Тип ФС определяется открытием корня через
`f_opendir`, настоящий `FF_DIR.obj.fs->fs_type` и `f_closedir`, без `f_getfree`
и сканирования таблицы свободных кластеров.

SD-транзакции используют timeout до 1000 мс и deadline попытки 4000 мс;
истёкший бюджет не допускает следующие команды. Это не hard total wall time:
внутренние повторы, ожидания и файловые операции SDK не становятся целиком
прерываемыми. Асинхронный mount отделяет их от запуска интерфейса.

На SDMMC и USB-A MSC поддерживаются FAT32 и exFAT. FAT12/16 отклоняются;
автоформатирования нет. Автоматического обновления с SD нет никогда: ни boot,
ни mount/retry, ни файлы `Firmware*.bin` не запускают OTA. Файл-маркер
`no_auto_update` не требуется. Обновление возможно через явную последовательность
команд [протокола](../../PROTOCOL.md).

## FatFs и единый ABI

Проектный компонент `tab5/components/fatfs` проверяет SHA256 pinned SDK 5.5.2
и генерирует `ff.c`, `ff.h`, `ffconf.h`, `ffunicode.c`, `diskio.h`.
Изменение upstream `ffconf.h`: hardcoded `FF_FS_EXFAT=0` заменён переключателем
`CONFIG_FATFS_EXFAT`. Общие generated заголовки используются VFS, diskio и
приложением; SDK/src не должен попадать в include-path и подмешивать другой ABI.
Production config: exFAT=1, LFN heap (`FF_USE_LFN=3`), UTF-8 API (`2`), max255,
`sizeof(FSIZE_t)=8`, `CONFIG_FATFS_USE_LABEL=1`. LABEL включён штатной Kconfig:
в exFAT-ветке upstream `ff.c:2332`, `dir_read`, использует `FF_USE_LABEL` в C-выражении.
Тест не подставляет отсутствующие CONFIG-макросы.

64-битный размер в FatFs не расширяет wire protocol. `files/get` проверяют
исходный `FILINFO.fsize` до сужения VFS: предел UINT32_MAX и дополнительно
INT32_MAX при `sizeof(off_t)<=4`; на signed 32-bit offset это 2,147,483,647 байт.
Слишком большой файл пропускается в `files`, `get` возвращает `ERR file`.
Поддержка exFAT не является проверкой записи CSV или передачи файлов за этим
пределом; отсутствие wraparound на уровне библиотеки проверено только размером
типа, а не созданием многогигабайтного файла.

## Проверки

Финальные обе сборки ESP-IDF и пакеты `1.6.15-esp32` прошли проверку:
ревизии P4 100–199 и 300–399, без LVGL, FINE=0/TIMING=0 во всех шести
командах компиляции нативного движка. На профиль проверены 23 фактических
потребителя FatFs-заголовков, включая MSC. Полные хеши исходных managed
деревьев esp_hosted 2.13.0 и wifi_remote 0.14.5 совпадают с базовой 1.6.14.
Код отрисовки 1.6.14 не менялся (`unchanged-renderer.json`).

`host/HOST_GATES.json`: PASS для 14 сценариев сервисов, реальной цепочки
запуска приложения, потребителя UI, deadline SD, прежних тестов сервисов,
запуска USB host, генератора FatFs и actual MSC register/getter/unregister.
При fatal init задачи-потребители не создаются. Зависший mount не мешает
OTA maintenance и записи на другом носителе, включая stop/drain/restart.
Проверена защита от FSIZE64=4GiB+3 при POSIX-размере 3.

MSC 1.3.0 включён в проект с неизменёнными исходниками, лицензией и SHA256;
getter читает pdrv настоящего зарегистрированного VFS handle. Его проверка
выполнена также в отдельном временном дереве без managed MSC. Сборка
не зависит от наличия старой скачанной копии MSC. Проект фиксирует точные
версии четырёх прямых зависимостей. В репозитории оставлен переносимый
registry lock базовой версии; Component Manager при configure может
пересоздать запись локального компонента с абсолютным путём текущего ПК —
этот машинный путь в репозиторий не переносится. Это не меняет исходники
проверенных зависимостей или бинарные пакеты.

Архив и SHA256 частей описаны в `packages.json`. Доказательства сборок:
`rev1-native-final-build-proof.json`, `rev3-native-final-build-proof.json`,
`package-verification.json`, `fatfs-build-verification.json`.

Из корня репозитория после production reconfigure:

```text
python tab5/tests/run_fatfs_images.py
```

Используются фактические `tab5/build/config/sdkconfig.h` и
`tab5/build/tab5-fatfs-generated`; хеши всех пяти generated файлов сравниваются
с результатом production `prepare.py`. Actual-config прогон: **4/4 PASS**.
На RAM-образах FAT32/exFAT проверены mount, создание CSV, точные байты,
UTF-8 long filename, `f_sync`, close/remount, listing/readback, append/retry
и создание/удаление probe. Для FAT12/16 точное тело production `supported_drive()`
отказывает без disk_write и с побайтовой неизменностью образа.

Локальные доказательства находятся в `.tools/storage-1.6.15/fatfs-images/`:
`proof.json` содержит source/config/generator SHA256 и ABI, `compile.log`,
`run.log`, `dependencies.log` — логи. Compiler dependency trace подтверждает,
что `ff.h`, `ffconf.h`, `diskio.h` взяты только из generated-root, SDK/src
отсутствует. Preview-логи сохранены отдельно и не являются финальным доказательством.
Это не проверка электрического питания, конкретных карт/флешек, hotplug,
SDK wall time или записи на физическое устройство.

## USB-C: граница аппаратной схемы

Штатный USB-C J8 не поддерживает обычный пассивный накопитель в этом релизе.
В опубликованной схеме (лист 4) CC1/CC2 имеют R52/R53 по 5,1 кОм к GND —
фиксированные sink Rd. VBUS через FU1 идёт на `USB_5VIN`; лист 5 показывает
U34 MAX40200 от входа USB-C в систему, а не управляемый источник VBUS к J8.
Управляемое питание через U27 относится к USB-A. Поэтому программная смена
режима контроллера сама по себе не создаёт CC/source-VBUS цепи на USB-C.
Маркетинговое обозначение OTG не подтверждает эту конкретную топологию.

USB-A остаётся HS MSC host; USB-C сохраняет Serial/JTAG и протокол ПК.
Вывод относится к опубликованной схеме: физическая ревизия платы и переходники
не проверялись. Работоспособность конкретного USB-A носителя требует HIL.

## Первичные источники

- [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5).
- [Схема Tab5, листы 4–5](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf).
- [Infineon: CC Rp/Rd](https://community.infineon.com/t5/Knowledge-Base-Articles/USB-Type-C-connector-Rp-Rd-and-Ra-termination-resistors/ta-p/253544).
- [Analog Devices MAX40200](https://www.analog.com/media/en/technical-documentation/data-sheets/max40200.pdf).
- [FatFs ESP-IDF v5.5.2](https://github.com/espressif/esp-idf/tree/v5.5.2/components/fatfs).
- [ESP-IDF 5.5.2 USB Serial/JTAG](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-guides/usb-serial-jtag-console.html).
- [Фактический managed USB Host 1.4.1](https://components.espressif.com/components/espressif/usb/versions/1.4.1/readme)
  и [MSC 1.3.0](https://components.espressif.com/components/espressif/usb_host_msc/versions/1.3.0/readme).

Источники реализации: `tab5/main/main.c`, `tab5/main/tab5_services.c`,
`tab5/components/fatfs/prepare.py`, `tab5/components/fatfs/upstream.json`,
`tab5/sdkconfig.defaults`. Аппаратный разбор и локальные копии первоисточников:
`.tools/storage-1.6.15/research/USB-C-FEASIBILITY.md` и `provenance.json`.
