#pragma once
// COM-порт прибора (USB CDC, ST Virtual COM Port VID:PID 0483:5740) через CSerialPort.
//
// Автоматически пробуются только порты с VID:PID 0483:5740 (выбранный пользователем — любой). VID:PID у ST общий,
// поэтому «наш» порт подтверждает DeviceLink ответом на ver (BWM427…); чужой — Reject(): не трогать 30 с.
// Порт пропал (прибор выдернули или он ушёл в загрузчик) — событие hot-plug или ошибка записи закрывают его;
// дальше поиск раз в секунду, и прибор подхватывается сам после перезагрузки или перепрошивки.
// Открытие/закрытие — только из Tick() (поток связи) и деструктора; события hot-plug лишь ставят флаги.
// (По образцу SerialLink из ShagomerPCModule; там же — про поток CSerialPort при вынутом порте.)
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <CSerialPort/SerialPort.h>
#include <CSerialPort/SerialPortInfo.h>

#include "Connection.hpp"
#include "LineSplitter.hpp"
#include "../core/TextUtil.hpp"

class SerialConnection final : public IConnection,
                               public itas109::CSerialPortListener,
                               public itas109::CSerialPortHotPlugListener
{
public:
    static constexpr const char* kHwId = "0483:5740";

    SerialConnection()
        : splitter_([this](std::string line) { EmitLine(std::move(line)); })
    {
        sp_.connectReadEvent(this);
        sp_.connectHotPlugEvent(this);
    }

    ~SerialConnection() override
    {
        sp_.disconnectReadEvent();
        {
            std::lock_guard lock(portMutex_);
            sp_.close();
            open_.clear();
        }
        // CSerialPort гасит фоновые потоки не мгновенно.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    SerialConnection(const SerialConnection&) = delete;
    SerialConnection& operator=(const SerialConnection&) = delete;

    void Tick(std::int64_t now) override
    {
        if (!enabled_)
        {
            if (IsOpen())
                ClosePort();
            return;
        }

        std::string removed;
        bool rescan = false;
        {
            std::lock_guard lock(hotMutex_);
            removed.swap(removed_);
            rescan = rescan_;
            rescan_ = false;
            for (const auto& name : added_)
            {
                retryAt_.erase(name); // новый (переподключённый) порт — пробовать сразу
                silentRejects_.erase(name);
            }
            added_.clear();
            if (resetRetries_)
            {
                resetRetries_ = false; // «Подключить»: все порты — заново
                retryAt_.clear();
                silentRejects_.clear();
                nextOpenMs_ = 0;
            }
            if (choiceChanged_)
            {
                choiceChanged_ = false;
                choice_ = pendingChoice_;
                retryAt_.clear();
                nextOpenMs_ = 0;
                const std::string open = PortName();
                if (!open.empty() && !choice_.empty() && !Same(open, choice_))
                    ClosePort();
            }
        }

        std::string open;
        {
            std::lock_guard lock(portMutex_);
            if (sp_.isOpen())
                open = open_;
        }

        // Перечень портов: без открытого — раз в секунду (и сразу по событию hot-plug), с открытым — раз в 3 с
        // (порт мог исчезнуть без события).
        if (rescan || now >= nextScanMs_)
        {
            Scan();
            nextScanMs_ = now + (open.empty() ? 1000 : 3000);
            if (!open.empty())
            {
                std::lock_guard lock(infoMutex_);
                const bool present = std::any_of(ports_.begin(), ports_.end(),
                                                 [&](const PortInfo& p) { return Same(p.name, open); });
                if (!present)
                    removed = open;
            }
        }

        if (!open.empty())
        {
            if (!removed.empty() && Same(removed, open))
            {
                ClosePort();
                retryAt_[open] = now + 500;
                SetProblem("порт " + open + " пропал (прибор отключён или перезагружается)");
            }
            return;
        }

        if (now < nextOpenMs_)
            return;
        nextOpenMs_ = now + 1000;

        // Кандидаты: выбранный порт или все порты 0483:5740, последний удачный — первым.
        std::vector<std::string> candidates;
        {
            std::lock_guard lock(infoMutex_);
            if (!choice_.empty())
                candidates.push_back(choice_);
            else
                for (const auto& p : ports_)
                    if (p.ours)
                    {
                        if (Same(p.name, lastGood_))
                            candidates.insert(candidates.begin(), p.name);
                        else
                            candidates.push_back(p.name);
                    }
        }
        if (candidates.empty())
        {
            SetProblem(choice_.empty() ? "прибор не найден: нет COM-порта с VID:PID 0483:5740 (подключите USB)"
                                       : "порт " + choice_ + " не найден");
            return;
        }
        for (const auto& name : candidates)
        {
            const auto it = retryAt_.find(name);
            if (it != retryAt_.end() && now < it->second)
                continue;
            if (OpenPort(name))
            {
                lastGood_ = name;
                SetProblem("");
                return;
            }
            retryAt_[name] = now + 3000;
            SetProblem("не удалось открыть " + name + " (занят другой программой?)");
        }
    }

    bool IsOpen() override
    {
        std::lock_guard lock(portMutex_);
        return sp_.isOpen();
    }

    bool Write(const std::string& data) override
    {
        bool ok = false;
        {
            std::lock_guard lock(portMutex_);
            if (sp_.isOpen())
                ok = sp_.writeData(data.data(), static_cast<int>(data.size())) == static_cast<int>(data.size());
        }
        if (!ok && IsOpen())
        {
            SetProblem("ошибка записи в порт");
            ClosePort();
        }
        return ok;
    }

    void SetEnabled(bool on) override
    {
        if (on && !enabled_)
        {
            std::lock_guard lock(hotMutex_);
            resetRetries_ = true;
        }
        enabled_ = on;
    }
    bool Enabled() const override { return enabled_; }

    void Reject(std::int64_t now, bool foreign) override
    {
        const std::string name = PortName();
        ClosePort();
        if (!name.empty())
        {
            // Чужое устройство с тем же VID:PID ST больше не тревожим (до переподключения его к USB или кнопки
            // «Подключить»); молчащее — ещё раз через 30 с (прибор мог загружаться), после второго раза — тоже нет.
            // Выбранный вручную порт — через 3 с.
            const bool giveUp = foreign || ++silentRejects_[name] >= 2;
            retryAt_[name] = !choice_.empty() ? now + 3000 : giveUp ? INT64_MAX : now + 30000;
            if (Same(name, lastGood_))
                lastGood_.clear();
            SetProblem(name + (foreign ? " (0483:5740) — другое устройство, не регистратор крена"
                                       : " (0483:5740) не отвечает на команды — пробую снова через 30 с"));
        }
    }

    void Reopen(std::int64_t now) override
    {
        const std::string name = PortName();
        ClosePort();
        if (!name.empty())
            retryAt_[name] = now + 1000;
        nextOpenMs_ = now + 1000;
    }

    std::string PortName() override
    {
        std::lock_guard lock(portMutex_);
        return sp_.isOpen() ? open_ : std::string{};
    }

    std::uint64_t OpenCount() override { return openCount_.load(); }

    std::vector<PortInfo> Ports() override
    {
        std::lock_guard lock(infoMutex_);
        return ports_;
    }

    // Из потока интерфейса: применяется в Tick().
    void SetPortChoice(const std::string& port) override
    {
        std::lock_guard lock(hotMutex_);
        pendingChoice_ = port;
        choiceChanged_ = true;
    }

    bool IsDemo() const override { return false; }

    std::string Problem() override
    {
        std::lock_guard lock(problemMutex_);
        return problem_;
    }

private:
    static bool Same(const std::string& a, const std::string& b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }

    void Scan()
    {
        std::vector<PortInfo> list;
        for (const auto& info : itas109::CSerialPortInfo::availablePortInfos())
        {
            PortInfo p;
            p.name = info.portName;
#ifdef _WIN32
            p.description = text::AcpToUtf8(info.description);
#else
            p.description = info.description;
#endif
            p.hwid = info.hardwareId;
            p.ours = Same(p.hwid, kHwId);
            list.push_back(std::move(p));
        }
        std::sort(list.begin(), list.end(), [](const PortInfo& a, const PortInfo& b) {
            // COM3 < COM10
            auto num = [](const std::string& s) {
                int v = 0;
                for (char c : s)
                    if (c >= '0' && c <= '9')
                        v = v * 10 + (c - '0');
                return v;
            };
            return num(a.name) < num(b.name);
        });
        std::lock_guard lock(infoMutex_);
        ports_ = std::move(list);
    }

    void SetProblem(std::string p)
    {
        std::lock_guard lock(problemMutex_);
        problem_ = std::move(p);
    }

    bool OpenPort(const std::string& name)
    {
        {
            std::lock_guard lock(readMutex_);
            splitter_.Reset();
        }
        std::lock_guard lock(portMutex_);
        if (sp_.isOpen())
            sp_.close();
        sp_.init(name.c_str(), itas109::BaudRate115200, itas109::ParityNone, itas109::DataBits8, itas109::StopOne,
                 itas109::FlowNone, 16384);
        sp_.setReadIntervalTimeout(5);
        if (!sp_.open())
            return false;
        // DTR = 1: прибор считает порт открытым (закрытие порта, DTR 1 -> 0, выключает поток stream).
        sp_.setDtr(true);
        sp_.setRts(true);
        open_ = name;
        openCount_++;
        return true;
    }

    void ClosePort()
    {
        std::lock_guard lock(portMutex_);
        sp_.close();
        open_.clear();
    }

    void onReadEvent(const char* /*portName*/, unsigned int readBufferLen) override
    {
        if (readBufferLen == 0)
            return;
        std::vector<char> data(readBufferLen);
        const int received = sp_.readData(data.data(), static_cast<int>(readBufferLen));
        if (received > 0)
        {
            std::lock_guard lock(readMutex_);
            splitter_.Feed(data.data(), static_cast<std::size_t>(received));
        }
    }

    void onHotPlugEvent(const char* portName, int isAdd) override
    {
        std::lock_guard lock(hotMutex_);
        rescan_ = true;
        if (isAdd != 1 && portName)
            removed_ = portName;
        if (isAdd == 1 && portName)
            added_.emplace_back(portName);
    }

    std::atomic<bool> enabled_{true};
    std::atomic<std::uint64_t> openCount_{0};
    std::mutex problemMutex_;
    std::string problem_;

    LineSplitter splitter_;
    std::mutex readMutex_;

    std::mutex hotMutex_;  // события hot-plug и выбор порта из интерфейса
    std::string removed_;
    std::vector<std::string> added_;
    bool rescan_ = true;
    std::string pendingChoice_;
    bool choiceChanged_ = false;

    std::mutex infoMutex_;
    std::vector<PortInfo> ports_;
    std::string choice_; // только поток связи

    std::map<std::string, std::int64_t> retryAt_;
    std::map<std::string, int> silentRejects_;
    bool resetRetries_ = false; // под hotMutex_
    std::string lastGood_;
    std::int64_t nextScanMs_ = 0, nextOpenMs_ = 0;

    std::mutex portMutex_;
    std::string open_;
    itas109::CSerialPort sp_{};
};
