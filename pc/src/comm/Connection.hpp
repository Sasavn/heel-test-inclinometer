#pragma once
// Канал до прибора: COM-порт (SerialConnection) или прибор-имитатор демо-режима (SimConnection).
// Все методы, кроме Ports() / IsDemo(), вызывает только поток связи (DeviceLink::Step).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct PortInfo
{
    std::string name;        // COM5
    std::string description; // «Устройство с последовательным интерфейсом USB»
    std::string hwid;        // «0483:5740» (VID:PID) или как отдал драйвер
    bool ours = false;       // VID:PID прибора (ST Virtual COM Port 0483:5740)
};

class IConnection
{
public:
    using LineFn = std::function<void(std::string)>;

    virtual ~IConnection() = default;

    // Принятые строки (без \r\n) — из любого потока; DeviceLink складывает их в очередь.
    void SetLineHandler(LineFn fn) { onLine_ = std::move(fn); }

    // Поиск и открытие порта (COM) или работа имитатора (демо). Каждые ~10 мс.
    virtual void Tick(std::int64_t nowMs) = 0;
    virtual bool IsOpen() = 0;
    // Строка команды с "\r\n". false — не ушла (порт закрыт / ошибка записи).
    virtual bool Write(const std::string& data) = 0;
    // Пользователь: «Подключить» / «Отключить». Выключенный канал закрыт и порт не ищет.
    virtual void SetEnabled(bool on) = 0;
    virtual bool Enabled() const = 0;
    // Открытый порт — не прибор: закрыть. foreign — ответил, но не как BWM427 (точно чужое устройство: не трогать
    // до переподключения); иначе молчал — попробовать снова через 30 с.
    virtual void Reject(std::int64_t nowMs, bool foreign) = 0;
    // Связь зависла (прибор не отвечает): закрыть, через секунду открыть снова.
    virtual void Reopen(std::int64_t nowMs) = 0;
    virtual std::string PortName() = 0;
    // Растёт при каждом открытии порта (DeviceLink так видит переоткрытие).
    virtual std::uint64_t OpenCount() = 0;
    // Последний перечень COM-портов (для выбора в интерфейсе).
    virtual std::vector<PortInfo> Ports() = 0;
    // "" — автоматически (порты 0483:5740), иначе — только этот порт.
    virtual void SetPortChoice(const std::string& port) = 0;
    virtual bool IsDemo() const = 0;
    // Почему не подключено: «нет порта 0483:5740», «COM5 занят другой программой»…
    virtual std::string Problem() = 0;

protected:
    void EmitLine(std::string line)
    {
        if (onLine_)
            onLine_(std::move(line));
    }

    LineFn onLine_;
};
