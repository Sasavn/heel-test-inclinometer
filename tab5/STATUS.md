# Tab5: ускорение 22.10.2026

Firmware **1.6.3-esp32** записана на P4 revision103/ST7121 через USB.
ROM MD5:4/4, NVS сохранена, boot_confirmed=true, uptime167с.
Главная страница: средний измеренный refresh **116.2 → 15.3мс**;
подготовка UI **18.1 → 2.7мс**. PPA и performance optimization включены;
повторные invalidation dashboard/graph устранены.76 native-снимков без отличий.
RTC и AP работают; реальные измерения и запись пока не подтверждены.
Два IDF-профиля и28 actual-image mock-прогонов прошли; аппаратно проверен103.

[Замеры и ограничения](PERFORMANCE_2026-10-22.md),
[первый аппаратный запуск](HARDWARE_2026-10-22.md).
STM32, PC production, математика и services не изменены. Публикации не было.
