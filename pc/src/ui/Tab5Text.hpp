#pragma once
// Только представление: сообщения движков и строки протокола не изменяются.
#include <string>
#include <string_view>
#include <utility>

namespace ui
{
inline std::string Tab5Text(std::string_view message)
{
    if (message.empty())
        return {};
    static constexpr std::pair<std::string_view, std::string_view> texts[] = {
        {"TCP authentication failed", "Ключ доступа не принят прибором"},
        {"Set numeric IPv4, port 1..65535 and token", "Укажите IPv4-адрес, порт от 1 до 65535 и ключ доступа"},
        {"TCP connecting", "Подключение к Tab5 по TCP…"},
        {"TCP endpoint is not BWM427", "По этому адресу ответил другой прибор"},
        {"TCP disconnected; retrying", "Связь по TCP потеряна; повторное подключение…"},
        {"Committed; awaiting reboot and expected version", "Прошивка записана. Ожидаем перезапуск и проверку версии…"},
        {"Expected version is running as trial; awaiting self-test confirmation",
         "Новая версия запущена. Ожидаем результат самопроверки…"},
        {"Uploaded; expected version reconnected and boot self-test confirmed",
         "Прошивка обновлена: версия и самопроверка подтверждены"},
        {"Validating factory plan; native ROM recovery", "Проверяем комплект и выполняем восстановление…"},
        {"All factory parts verified by ROM; reboot requested. Reconnect to check boot confirmation.",
         "Комплект записан и проверен. Подключитесь к прибору для проверки запуска"},
        {"Cancelled verification; committed image may reboot",
         "Проверка отменена. Записанный прибор может перезапуститься"},
        {"Cancelled; restart upload from byte zero", "Обновление отменено. Повторная передача начнётся сначала"},
        {"Exact Tab5/ESP32-P4/proto=2 identity required", "Нужен подключённый Tab5 с ESP32-P4 и протоколом 2"},
        {"Stop device and PC recording first", "Сначала остановите запись на приборе и на ПК"},
        {"Cannot read OTA application", "Не удалось прочитать файл прошивки"},
        {"Invalid OTA application size", "Недопустимый размер файла прошивки"},
        {"ESP application size outside allowed range", "Размер прошивки выходит за допустимые границы"},
        {"Expected ESP32-P4 application, not STM32/C6/full flash", "Нужен файл приложения ESP32-P4 для Tab5"},
        {"Missing ESP application descriptor", "В файле нет описания приложения ESP32"},
        {"Wrong board project; expected krenomer-esp32", "Файл собран для другого проекта; нужен krenomer-esp32"},
        {"Invalid Tab5 version", "В файле указана неверная версия прошивки Tab5"},
        {"Invalid chip revision interval", "В файле неверно указан диапазон ревизий кристалла"},
        {"Reboot/version verification timed out; reconnect and inspect device rollback state",
         "Прибор не подтвердил новую версию вовремя. Подключитесь повторно и проверьте откат"},
        {"Device returned wrong platform/version (possible rollback)",
         "Прибор вернулся с другой версией или платформой; возможен откат"},
        {"Disconnected; device aborts session. Retry starts from zero",
         "Связь потеряна. Повторное обновление начнётся сначала"},
        {"Boot self-test confirmation timed out; trial image is not verified",
         "Самопроверка не подтверждена вовремя; новая прошивка не проверена"},
        {"Invalid OTA identity reply", "Прибор не подтвердил модель, кристалл и протокол обновления"},
        {"Running version mismatch; possible rollback", "Версия после запуска не совпала; возможен откат"},
        {"Missing boot confirmation field", "Прибор не передал результат самопроверки"},
        {"OTA device busy/capacity/version mismatch", "Прибор занят либо размер или версия прошивки не подходят"},
        {"Image incompatible with device chip revision", "Прошивка не подходит для ревизии кристалла этого прибора"},
        {"Running trial is not boot-confirmed; wait for self-test before updating",
         "Дождитесь подтверждения самопроверки текущей прошивки"},
        {"Unexpected OTA offset acknowledgement", "Прибор подтвердил неверное смещение данных; обновление остановлено"},
        {"Commit not acknowledged; inspect ota status before retry",
         "Прибор не подтвердил запись. Проверьте его состояние перед повтором"},
        {"Cannot read factory manifest (64 KiB maximum)", "Не удалось прочитать описание комплекта; предел — 64 КБ"},
        {"Expected Tab5 ESP32-P4 proto2 factory_parts manifest",
         "Выберите описание комплекта Tab5 для ESP32-P4 с протоколом 2"},
        {"Factory requires bootloader, partitions, initialized OTA metadata, application",
         "В комплекте нужны загрузчик, таблица разделов, данные выбора прошивки и приложение"},
        {"Manifest application version/size/SHA256 mismatch",
         "Версия, размер или SHA256 приложения не совпали с описанием комплекта"},
        {"Enter an explicit COM port; no automatic discovery", "Укажите COM-порт Tab5 вручную"},
        {"Explicit COM<number> required", "Укажите порт в формате COM и номер, например COM5"},
        {"Cannot open explicitly selected ROM port", "Не удалось открыть выбранный COM-порт Tab5"},
        {"Runtime transport did not close; recovery not started",
         "Рабочее соединение не закрылось; восстановление не начато"},
        {"Native ROM recovery requires Windows", "Первая прошивка и восстановление доступны в Windows"},
        {"ROM device disconnected", "Связь с загрузчиком потеряна"},
        {"ROM I/O failed", "Ошибка обмена с загрузчиком"},
    };
    for (const auto &[source, translated] : texts)
        if (message == source)
            return std::string(translated);
    // Ответ прибора оставляем побайтно: это данные протокола, не подпись интерфейса.
    constexpr std::string_view failed = "OTA failed: ";
    if (message.starts_with(failed))
        return "Ошибка обновления. Ответ прибора: " + std::string(message.substr(failed.size()));
    std::string key(message);
    for (char &c : key)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    if (key.find("cancel") != std::string::npos)
        return "Операция отменена; повторите запись комплекта целиком";
    if (key.find("revision") != std::string::npos)
        return "Комплект или способ записи не подходит для ревизии кристалла";
    if (key.find("sha256") != std::string::npos || key.find("checksum") != std::string::npos ||
        key.find("md5") != std::string::npos)
        return "Контрольная сумма не совпала. Проверьте комплект и повторите запись";
    if (key.find("segment") != std::string::npos || key.find("descriptor") != std::string::npos ||
        key.find("image") != std::string::npos)
        return "Файл прошивки повреждён или не подходит для этого режима записи";
    if (key.find("timeout") != std::string::npos || key.find("timed out") != std::string::npos)
        return "Истекло время ожидания ответа загрузчика";
    if (key.find("security") != std::string::npos || key.find("encryption") != std::string::npos ||
        key.find("secure boot") != std::string::npos)
        return "Настройки защиты прибора не поддерживаются этим способом записи";
    if (key.find("port") != std::string::npos || key.find("dtr") != std::string::npos ||
        key.find("rts") != std::string::npos)
        return "Не удалось настроить выбранный COM-порт или перевести прибор в загрузчик";
    if (key.find("partition") != std::string::npos || key.find("part") != std::string::npos ||
        key.find("factory") != std::string::npos || key.find("otadata") != std::string::npos)
        return "Неверный или неполный комплект прошивки Tab5";
    if (key.find("chip") != std::string::npos)
        return "Выбранный прибор не подтвердил кристалл ESP32-P4";
    return "Не удалось выполнить операцию. Проверьте комплект, подключение и режим загрузчика";
}
// Техническая причина остаётся побайтно исходной: адреса и коды не переводятся.
inline std::string Tab5TechnicalDetails(std::string_view message)
{
    return "Техническая причина (без изменений):\n" + std::string(message);
}
} // namespace ui
