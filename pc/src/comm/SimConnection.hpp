#pragma once
// Канал до прибора-имитатора (демо-режим): «порт» ДЕМО открыт, пока имитатор «на USB» (после boot / reset он
// пропадает и возвращается, как настоящий прибор).
#include <atomic>
#include <memory>
#include <string>

#include "Connection.hpp"
#include "SimDevice.hpp"

class SimConnection final : public IConnection
{
public:
    explicit SimConnection(std::shared_ptr<SimDevice> dev) : dev_(std::move(dev))
    {
        dev_->SetEmitter([this](const std::string& line) { EmitLine(line); });
    }

    ~SimConnection() override { dev_->SetEmitter(nullptr); }

    void Tick(std::int64_t now) override
    {
        now_ = now;
        const bool present = enabled_ && dev_->UsbPresent(now);
        if (present && !open_)
        {
            open_ = true;
            openCount_++;
            dev_->PortOpened();
        }
        else if (!present && open_)
            open_ = false;
        if (open_)
            dev_->Tick(now);
    }

    bool IsOpen() override { return open_; }

    bool Write(const std::string& data) override
    {
        if (!open_)
            return false;
        std::size_t a = 0;
        while (a < data.size())
        {
            std::size_t b = data.find('\n', a);
            if (b == std::string::npos)
                b = data.size();
            std::string line = data.substr(a, b - a);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                dev_->Command(line, now_);
            a = b + 1;
        }
        return true;
    }

    void SetEnabled(bool on) override { enabled_ = on; }
    bool Enabled() const override { return enabled_; }
    void Reject(std::int64_t, bool) override { open_ = false; }
    void Reopen(std::int64_t) override { open_ = false; }
    std::string PortName() override { return open_ ? "ДЕМО" : ""; }
    std::uint64_t OpenCount() override { return openCount_; }
    std::vector<PortInfo> Ports() override { return {PortInfo{"ДЕМО", "Прибор-имитатор (демо-режим)", "sim", true}}; }
    void SetPortChoice(const std::string&) override {}
    bool IsDemo() const override { return true; }
    std::string Problem() override { return dev_->InDfu() ? "имитатор в загрузчике (DFU)" : "имитатор перезагружается"; }

    const std::shared_ptr<SimDevice>& Device() const { return dev_; }

private:
    std::shared_ptr<SimDevice> dev_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> open_{false};
    std::atomic<std::uint64_t> openCount_{0};
    std::int64_t now_ = 0;
};
