# Первое Wi-Fi подключение Tab5 — 30.10.2026

Причина обращения: stable `v2.0.2` отдаёт PC 1.8.3 для STM32,
`OriginalFilename=krenomer.exe`. Это не сетевой клиент Tab5.
В prerelease `tab5-v1.7.0` опубликован `krenomer-tab5-x64.exe`, PC 1.8.4,
`OriginalFilename=krenomer-esp32.exe`.

Скачанные SHA256:

- stable x64: `1a622699294ccd70d8d8b61d28cab80ae9243b2055ff9c47ccda2ef9fd54b6b3`;
- Tab5 x64 1.8.4: `338be53bee49c7b68d82223637f52334f640237f00567e8d8b6b7e1489f58a76`.

В 1.8.4 TCP спрятан в списке портов «Tab5 по сети (TCP)…».
В 1.8.5 добавляется отдельная кнопка Wi-Fi и режимы AP/общий роутер.
Автоматического UDP/mDNS-поиска нет, адрес вводится вручную.

AP: подключить ПК к точке доступа Tab5, указать `192.168.4.1:5000`.
Router: на приборе «Сеть» → включить Wi-Fi сеть (STA), задать SSID/пароль
роутера и применить. Полученный DHCP-IP взять из строки адреса STA на Tab5
и указать в программе, TCP 5000. Listener слушает INADDR_ANY.
Ключ TCP — пароль AP самого прибора; пароль роутера вводится только в STA.
Guest/client isolation/VLAN/firewall могут запрещать доступ внутри LAN.

Опубликованный EXE запущен с `--demo` на отдельном hidden bgdesk-сеансе:
окно GLFW 1.8.4 появилось, но PrintWindow возвращает белую OpenGL-поверхность.
Видимость его меню этим способом не подтверждена; свой процесс завершён.
Реальные COM/USB, прибор и сеть пользователя в проверке не использовались.

Транспорт проверяется production TcpConnection/DeviceLink на loopback;
UI — production offscreen-стендом без GLFW/OpenGL-окна. Это не аппаратная
проверка AP, STA или DHCP. Старые GitHub assets не перезаписываются.

## Итог новой сборки

PC 1.8.5.0, MSVC v143, x64/x86: полная очистка проектных объектов,
Win7 import gates PASS, CTest **14/14 на каждой архитектуре**.
`ui_shots` проверяет кнопку без COM, AP/роутер, открытие из демо,
отмену с сохранением Sim и переключение с Sim при Connect.
UI-стенд использует `noHardware`: вместо физического транспорта там Null,
а реальный TcpConnection/DeviceLink отдельно проверен loopback-тестом.

SHA256 готовых EXE:

- x64: `eda13f18bb763045c738ffd88a3df820889fe59f5c9741ce9b1fcfc4ec8c754d`;
- x86: `f85fec02bc637fa37b7a3b2b71e048276a4cb74696af0de13605650d2b260a99`.

Логи: `pc/build-win7/wifi-build.log`, `wifi-tests.log` и аналогичные
в `pc/build-win7-x86`. Для повторения — обычный CTest из build-папки;
если сохранялись timestamps изменённого App.hpp, сначала полная очистка
проектных объектов обязательна: иначе старые consumers имеют иной sizeof(App).

Скриншоты production offscreen: [AP](../docs/img/wifi_ap.png),
[роутер](../docs/img/wifi_router.png), [демо](../docs/img/wifi_demo_ap.png).
Публикация новой PC-версии требует отдельного решения владельца;
в этой работе выполнены только сборка, проверки и локальный коммит.
