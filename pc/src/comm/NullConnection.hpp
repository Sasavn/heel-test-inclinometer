#pragma once
// Канал «прибора нет»: порт не открывается никогда (стенд снимков — состояние «нет связи» без обращения к
// настоящим COM-портам ПК).
#include <string>

#include "Connection.hpp"

class NullConnection final : public IConnection
{
public:
    void Tick(std::int64_t) override {}
    bool IsOpen() override { return false; }
    bool Write(const std::string&) override { return false; }
    void SetEnabled(bool on) override { enabled_ = on; }
    bool Enabled() const override { return enabled_; }
    void Reject(std::int64_t, bool) override {}
    void Reopen(std::int64_t) override {}
    std::string PortName() override { return {}; }
    std::uint64_t OpenCount() override { return 0; }
    std::vector<PortInfo> Ports() override { return {}; }
    void SetPortChoice(const std::string&) override {}
    bool IsDemo() const override { return false; }
    std::string Problem() override { return "прибор не найден: нет COM-порта с VID:PID 0483:5740 (подключите USB)"; }

private:
    bool enabled_ = true;
};
