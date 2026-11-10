#pragma once
#include "EspOta.hpp"
#include "EspRom.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
namespace fw
{
struct FactoryPlan
{
    std::vector<esprom::Part> parts;
    std::string version, error;
    bool ok = false;
};
inline bool ReadBounded(const std::filesystem::path &path, std::vector<unsigned char> &out, std::size_t maximum)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return false;
    auto n = f.tellg();
    if (n < 0 || static_cast<std::uint64_t>(n) > maximum)
        return false;
    out.resize(static_cast<std::size_t>(n));
    f.seekg(0);
    return bool(f.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size())));
}
inline FactoryPlan LoadFactory(const std::string &filename)
{
    FactoryPlan p;
    auto fail = [&](const char *s) {
        p.error = s;
        return p;
    };
    auto manifest = text::PathFromUtf8(filename);
    std::vector<unsigned char> raw;
    if (!ReadBounded(manifest, raw, 65536))
        return fail("Cannot read factory manifest (64 KiB maximum)");
    json::Value j;
    std::string error;
    if (!json::Parse(std::string(raw.begin(), raw.end()), j, &error) || j["format"].Int() != 1 ||
        j["board"].Str() != "tab5" || j["chip"].Str() != "esp32p4" || j["protocol"].Int() != 2 ||
        !j["factory_parts"].IsObject())
        return fail("Expected Tab5 ESP32-P4 proto2 factory_parts manifest");
    if (j["factory_parts"].obj.size() != 4)
        return fail("Factory requires bootloader, partitions, initialized OTA "
                    "metadata, application");
    bool boot = false, table = false, ota = false, app = false;
    for (const auto &entry : j["factory_parts"].obj)
    {
        esprom::Part part;
        auto addr = entry.first;
        auto name = entry.second.Str();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos)
            return fail("Factory paths must be local basenames");
        if (addr == "0x2000")
        {
            if (boot)
                return fail("Duplicate bootloader");
            boot = true;
            part.kind = esprom::PartKind::Bootloader;
            part.offset = 0x2000;
        }
        else if (addr == "0x8000")
        {
            if (table)
                return fail("Duplicate partitions");
            table = true;
            part.kind = esprom::PartKind::Partitions;
            part.offset = 0x8000;
        }
        else if (addr == "0xf000")
        {
            if (ota)
                return fail("Duplicate OTA metadata");
            ota = true;
            part.kind = esprom::PartKind::OtaData;
            part.offset = 0xf000;
        }
        else if (addr == "0x20000")
        {
            if (app)
                return fail("Duplicate application");
            app = true;
            part.kind = esprom::PartKind::Application;
            part.offset = 0x20000;
        }
        else
            return fail("Unexpected factory address (NVS/full flash writes forbidden)");
        const auto path = manifest.parent_path() / text::PathFromUtf8(name);
        std::error_code ec;
        if (std::filesystem::is_symlink(path, ec))
            return fail("Factory part symlinks are forbidden");
        if (!ReadBounded(path, part.data, 6291456))
            return fail("Cannot read bounded factory part");
        part.name = name;
        if (part.kind == esprom::PartKind::Application)
        {
            auto image = CheckEspImage(part.data);
            if (!image.ok)
            {
                p.error = image.error;
                return p;
            }
            if (image.version != j["version"].Str() || image.sha != j["sha256"].Str() ||
                static_cast<std::int64_t>(image.size) != j["size"].Int())
                return fail("Manifest application version/size/SHA256 mismatch");
            p.version = image.version;
        }
        if (part.offset == 0xf000 &&
            (part.data.size() != 8192 ||
             !std::all_of(part.data.begin(), part.data.end(), [](unsigned char c) { return c == 0xff; })))
            return fail("OTA metadata must be 8192 bytes FF (select ota_0, preserve NVS)");
        p.parts.push_back(std::move(part));
    }
    p.ok = boot && table && ota && app;
    if (p.ok)
    {
        auto checked = esprom::ValidatePlan(p.parts);
        if (!checked.ok())
        {
            p.ok = false;
            p.error = checked.message;
        }
    }
    return p;
}
#ifdef _WIN32
// Lazy opens only an explicit COM name, only after ROM engine validates the
// plan.
class WinRomTransport final : public esprom::ITransport
{
  public:
    WinRomTransport(std::string port, bool manual, std::atomic<bool> &stop)
        : port_(std::move(port)), manual_(manual), stop_(stop)
    {
    }
    ~WinRomTransport() override
    {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    std::size_t write(const std::uint8_t *p, std::size_t n, unsigned ms) override
    {
        return Transfer(const_cast<std::uint8_t *>(p), n, ms, true);
    }
    std::size_t read(std::uint8_t *p, std::size_t n, unsigned ms) override
    {
        return Transfer(p, n, ms, false);
    }
    bool reset(esprom::ResetMode mode, unsigned) override
    {
        Open();
        if (stop_)
            return false;
        auto dtr = [&](bool b) {
            if (!EscapeCommFunction(handle_, b ? SETDTR : CLRDTR))
                throw std::runtime_error("DTR failed");
        };
        auto rts = [&](bool b) {
            if (!EscapeCommFunction(handle_, b ? SETRTS : CLRRTS))
                throw std::runtime_error("RTS failed");
            dtr(dtrState_);
        };
        auto d = [&](bool b) {
            dtrState_ = b;
            dtr(b);
        };
        if (mode == esprom::ResetMode::EnterBootloader)
        {
            if (!manual_)
            {
                rts(false);
                d(false);
                Sleep(100);
                d(true);
                rts(false);
                Sleep(100);
                rts(true);
                d(false);
                rts(true);
                Sleep(100);
                d(false);
                rts(false);
                Sleep(100);
            }
            if (!PurgeComm(handle_, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR))
                throw std::runtime_error("Purge failed");
        }
        else
        {
            rts(true);
            Sleep(200);
            rts(false);
            Sleep(200);
        }
        return !stop_;
    }
    static bool ValidPort(const std::string &p)
    {
        return p.size() > 3 && p.size() < 16 && p.rfind("COM", 0) == 0 && p[3] != '0' &&
               std::all_of(p.begin() + 3, p.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

  private:
    void Open()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
            return;
        if (!ValidPort(port_))
            throw std::runtime_error("Explicit COM<number> required");
        handle_ = CreateFileA(("\\\\.\\" + port_).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot open explicitly selected ROM port");
        DCB d{};
        d.DCBlength = sizeof(d);
        d.BaudRate = 115200;
        d.ByteSize = 8;
        d.Parity = NOPARITY;
        d.StopBits = ONESTOPBIT;
        d.fBinary = TRUE;
        d.fDtrControl = DTR_CONTROL_DISABLE;
        d.fRtsControl = RTS_CONTROL_DISABLE;
        if (!SetCommState(handle_, &d))
            throw std::runtime_error("Cannot configure ROM port115200 8N1");
        COMMTIMEOUTS t{};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = 50;
        t.WriteTotalTimeoutConstant = 3000;
        if (!SetCommTimeouts(handle_, &t))
            throw std::runtime_error("Cannot set ROM timeouts");
    }
    std::size_t Transfer(std::uint8_t *p, std::size_t n, unsigned ms, bool output)
    {
        Open();
        if (stop_)
            return 0;
        OVERLAPPED o{};
        o.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!o.hEvent)
            throw std::runtime_error("ROM event failed");
        DWORD got = 0;
        BOOL ok = output ? WriteFile(handle_, p, static_cast<DWORD>(n), &got, &o)
                         : ReadFile(handle_, p, static_cast<DWORD>(n), &got, &o);
        if (!ok && GetLastError() != ERROR_IO_PENDING)
        {
            CloseHandle(o.hEvent);
            throw std::runtime_error("ROM device disconnected");
        }
        if (!ok)
        {
            auto until = GetTickCount64() + ms;
            DWORD rc = WAIT_TIMEOUT;
            while (!stop_ && GetTickCount64() < until)
            {
                rc = WaitForSingleObject(o.hEvent,
                                         static_cast<DWORD>(std::min<ULONGLONG>(50, until - GetTickCount64())));
                if (rc != WAIT_TIMEOUT)
                    break;
            }
            if (rc != WAIT_OBJECT_0)
            {
                CancelIoEx(handle_, &o);
                GetOverlappedResult(handle_, &o, &got, TRUE);
                CloseHandle(o.hEvent);
                return 0;
            }
            ok = GetOverlappedResult(handle_, &o, &got, FALSE);
        }
        CloseHandle(o.hEvent);
        if (!ok)
            throw std::runtime_error("ROM I/O failed");
        return got;
    }
    std::string port_;
    bool manual_, dtrState_ = false;
    std::atomic<bool> &stop_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#endif
class RomRecovery
{
  public:
    ~RomRecovery()
    {
        Cancel();
        if (future_.valid())
            future_.wait();
    }
    bool Busy() const
    {
        return running_;
    }
    const std::string &Message() const
    {
        return message_;
    }
    const esprom::Result &Result() const
    {
        return result_;
    }
    float Progress() const
    {
        return progress_.load();
    }
    bool Start(DeviceLink *link, const std::string &manifest, const std::string &port, bool manual)
    {
        if (running_)
            return false;
        auto plan = LoadFactory(manifest);
        if (!plan.ok)
        {
            message_ = plan.error;
            return false;
        }
#ifdef _WIN32
        if (!WinRomTransport::ValidPort(port))
        {
            message_ = "Enter an explicit COM port; no automatic discovery";
            return false;
        }
        link_ = link;
        wasEnabled_ = link && link->Connection()->Enabled();
        if (link)
            link->SetEnabled(false);
        stop_ = false;
        progress_ = 0;
        running_ = true;
        message_ = "Validating factory plan; native ROM recovery";
        future_ = std::async(std::launch::async, [this, parts = std::move(plan.parts), port, manual] {
            esprom::Result result;
            try
            {
                for (int i = 0; link_ && link_->Connection()->IsOpen() && i < 300 && !stop_; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (link_ && link_->Connection()->IsOpen())
                    throw std::runtime_error("Runtime transport did not close; recovery not started");
                WinRomTransport transport(port, manual, stop_);
                result = esprom::Run(
                    transport, parts, esprom::Options{},
                    [this](const esprom::Progress &p) {
                        progress_ = p.totalBytes ? float(p.acknowledgedBytes) / p.totalBytes : 0;
                    },
                    [this] { return stop_.load(); });
            }
            catch (const std::exception &e)
            {
                result.error = esprom::Error::Transport;
                result.message = e.what();
            }
            return result;
        });
        return true;
#else
        message_ = "Native ROM recovery requires Windows";
        return false;
#endif
    }
    void Tick()
    {
        if (running_ && future_.valid() && future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            result_ = future_.get();
            running_ = false;
            message_ = result_.ok() ? "All factory parts verified by ROM; reboot requested. "
                                      "Reconnect to check boot confirmation."
                                    : result_.message;
            if (link_)
                link_->SetEnabled(wasEnabled_);
            link_ = nullptr;
        }
    }
    void Cancel()
    {
        stop_ = true;
    }
    void Shutdown()
    {
        Cancel();
        if (future_.valid())
            future_.wait();
        Tick();
    }

  private:
    DeviceLink *link_ = nullptr;
    bool wasEnabled_ = false, running_ = false;
    std::atomic<bool> stop_{false};
    std::atomic<float> progress_{0};
    std::string message_;
    esprom::Result result_;
    std::future<esprom::Result> future_;
};
} // namespace fw
