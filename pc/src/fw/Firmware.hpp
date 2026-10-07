#pragma once
// Обновление прошивки по USB без кнопок (как tools/flash/usb_flash.ps1 + dfu_flash.cmd):
//   1. проверка образа .bin (размер до 384 КБ, вектор сброса во Flash, указатель стека в RAM, строка версии);
//   2. команда boot (во время записи — только boot force по согласию) -> прибор уходит в системный загрузчик
//      STM32, на USB появляется DFU 0483:DF11 (ждём до 15 с);
//   3. dfu-util -d 0483:df11 -a 0 -s 0x08000000 -D образ — запись (прогресс — из вывода dfu-util);
//   4. dfu-util … -s 0x08000000:<размер> -U файл — чтение обратно и побайтное сравнение с образом;
//   5. dfu-util … -s 0x08000000:4:leave -U … — выход из загрузчика, запуск новой прошивки;
//   6. ждём прибор на COM-порту (до 40 с) и показываем ver.
// Проверка не прошла — выход из загрузчика не делается: прибор остаётся в DFU, запись можно повторить.
// Сектор 7 (настройки) dfu-util не трогает — настройки переживают перепрошивку.
//
// dfu-util ищется: рядом с программой (dfu-util.exe или dfu-util\dfu-util.exe), переменная DFU_UTIL, PATH, копия
// Arduino IDE в %LOCALAPPDATA%\Arduino15 (сначала у текущего пользователя, затем у любого). Состояние
// обновления двигает Tick() из потока интерфейса; dfu-util работает отдельным процессом (Process.hpp).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Process.hpp"
#include "../comm/DeviceLink.hpp"
#include "../comm/SimDevice.hpp"
#include "../core/TextUtil.hpp"

#ifdef _WIN32
#include <setupapi.h>
#endif

namespace fw
{

inline constexpr std::uint32_t kFlashBase = 0x08000000u;
inline constexpr std::uint32_t kImageMax = 384u * 1024u; // сектора 0–6; сектор 7 — настройки
inline constexpr const char* kDfuId = "0483:df11";

// ---------------------------------------------------------------------------------------------------------------
// Образ
// ---------------------------------------------------------------------------------------------------------------

struct ImageInfo
{
    bool ok = false;
    std::string error;
    std::uint64_t size = 0;
    std::uint32_t sp = 0, reset = 0;
    std::string version; // из строки «BWM427 inclinometer firmware v…» в образе
    std::string build;
};

inline bool ReadFile(const std::filesystem::path& p, std::vector<std::uint8_t>& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

inline ImageInfo CheckImage(const std::vector<std::uint8_t>& d)
{
    ImageInfo info;
    info.size = d.size();
    if (d.size() < 1024)
    {
        info.error = "файл слишком мал для прошивки (" + std::to_string(d.size()) + " байт)";
        return info;
    }
    if (d.size() > kImageMax)
    {
        info.error = "образ больше 384 КБ — затёр бы сектор настроек (сектор 7)";
        return info;
    }
    auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1]) << 8) |
               (static_cast<std::uint32_t>(d[o + 2]) << 16) | (static_cast<std::uint32_t>(d[o + 3]) << 24);
    };
    info.sp = u32(0);
    info.reset = u32(4);
    // STM32F411: RAM 0x20000000..0x2001FFFF (стек — вершина, может быть ровно 0x20020000), Flash 0x08000000..
    if (info.sp < 0x20000000u || info.sp > 0x20020000u)
    {
        info.error = "не похоже на прошивку STM32F411: начальный указатель стека вне RAM";
        return info;
    }
    if ((info.reset & 1u) == 0 || info.reset < kFlashBase || info.reset >= kFlashBase + kImageMax)
    {
        info.error = "не похоже на прошивку STM32F411: вектор сброса вне Flash (это .bin для 0x08000000?)";
        return info;
    }
    const std::string_view all(reinterpret_cast<const char*>(d.data()), d.size());
    const std::string_view tag = "BWM427 inclinometer firmware";
    const auto p = all.find(tag);
    if (p != std::string_view::npos)
    {
        std::string_view rest = all.substr(p, 120);
        rest = rest.substr(0, rest.find('\0'));
        const auto v = rest.find(" v");
        if (v != std::string_view::npos)
        {
            const auto e = rest.find_first_of(", ", v + 2);
            info.version = std::string(rest.substr(v + 2, e == std::string_view::npos ? std::string_view::npos : e - v - 2));
        }
        const auto b = rest.find("build ");
        if (b != std::string_view::npos)
            info.build = std::string(rest.substr(b + 6));
    }
    else
    {
        info.error = "в файле нет строки «BWM427 inclinometer firmware» — это не прошивка регистратора крена";
        return info;
    }
    info.ok = true;
    return info;
}

// Процент из строки прогресса dfu-util: «Download	[=====     ]  36%        28672 bytes». -1 — не прогресс.
inline int ParseDfuProgress(std::string_view line)
{
    const auto pct = line.find('%');
    const auto br = line.find(']');
    if (pct == std::string_view::npos || br == std::string_view::npos || pct < br)
        return -1;
    std::size_t a = pct;
    while (a > 0 && line[a - 1] >= '0' && line[a - 1] <= '9')
        a--;
    if (a == pct)
        return -1;
    return std::clamp(static_cast<int>(proto::ToInt(line.substr(a, pct - a), -1)), -1, 100);
}

// ---------------------------------------------------------------------------------------------------------------
// Окружение: dfu-util, устройство DFU, процессы (настоящее и демо)
// ---------------------------------------------------------------------------------------------------------------

class IDfuBackend
{
public:
    virtual ~IDfuBackend() = default;
    // Путь к dfu-util.exe ("" — не найден) и где искали.
    virtual std::string FindDfuUtil(std::vector<std::string>* searched = nullptr) = 0;
    // Устройство USB DFU 0483:DF11 есть на шине; driver — у него стоит драйвер (WinUSB / libusb).
    virtual bool DfuDevicePresent(bool* driver = nullptr) = 0;
    virtual std::unique_ptr<IProcess> Start(const std::string& exe, const std::vector<std::string>& args, std::string* err) = 0;
    virtual bool IsDemo() const { return false; }
};

#ifdef _WIN32

class RealDfuBackend final : public IDfuBackend
{
public:
    std::string FindDfuUtil(std::vector<std::string>* searched) override
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> cands;
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path dir = fs::path(exe).parent_path();
        cands.push_back(dir / L"dfu-util.exe");
        cands.push_back(dir / L"dfu-util" / L"dfu-util.exe");
        wchar_t env[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"DFU_UTIL", env, MAX_PATH) > 0)
            cands.push_back(fs::path(env));
        wchar_t found[MAX_PATH] = {};
        if (SearchPathW(nullptr, L"dfu-util.exe", nullptr, MAX_PATH, found, nullptr) > 0)
            cands.push_back(fs::path(found));
        auto arduino = [&](const fs::path& local) {
            std::error_code ec;
            const fs::path base = local / L"Arduino15" / L"packages" / L"arduino" / L"tools" / L"dfu-util";
            std::vector<fs::path> vers;
            for (const auto& e : fs::directory_iterator(base, ec))
                if (e.is_directory(ec))
                    vers.push_back(e.path());
            std::sort(vers.rbegin(), vers.rend()); // новее — первым
            for (const auto& v : vers)
                cands.push_back(v / L"dfu-util.exe");
        };
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", env, MAX_PATH) > 0)
            arduino(fs::path(env));
        {
            std::error_code ec;
            wchar_t sysdrive[16] = L"C:";
            GetEnvironmentVariableW(L"SystemDrive", sysdrive, 16);
            for (const auto& u : fs::directory_iterator(fs::path(std::wstring(sysdrive) + L"\\Users"), ec))
                if (u.is_directory(ec))
                    arduino(u.path() / L"AppData" / L"Local");
        }
        for (const auto& c : cands)
        {
            if (searched)
                searched->push_back(text::PathToUtf8(c));
            std::error_code ec;
            if (fs::is_regular_file(c, ec))
                return text::PathToUtf8(c);
        }
        return {};
    }

    bool DfuDevicePresent(bool* driver) override
    {
        bool present = false, hasDriver = false;
        HDEVINFO h = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        SP_DEVINFO_DATA d{};
        d.cbSize = sizeof(d);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d); i++)
        {
            wchar_t ids[1024] = {};
            if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids),
                                                   sizeof(ids) - sizeof(wchar_t), nullptr))
                continue;
            std::wstring s(ids);
            for (auto& c : s)
                c = static_cast<wchar_t>(towupper(c));
            if (s.find(L"VID_0483&PID_DF11") == std::wstring::npos)
                continue;
            present = true;
            wchar_t svc[128] = {};
            if (SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_SERVICE, nullptr, reinterpret_cast<PBYTE>(svc),
                                                  sizeof(svc) - sizeof(wchar_t), nullptr) &&
                svc[0])
                hasDriver = true;
        }
        SetupDiDestroyDeviceInfoList(h);
        if (driver)
            *driver = hasDriver;
        return present;
    }

    std::unique_ptr<IProcess> Start(const std::string& exe, const std::vector<std::string>& args, std::string* err) override
    {
        return WinProcess::Start(exe, args, err);
    }
};

#endif

// Демо: «dfu-util» без процесса — печатает то же, что настоящий, и двигает прибор-имитатор.
class DemoDfuBackend final : public IDfuBackend
{
public:
    DemoDfuBackend(std::shared_ptr<SimDevice> dev, std::function<std::int64_t()> clock)
        : dev_(std::move(dev)), clock_(std::move(clock))
    {
    }

    std::string FindDfuUtil(std::vector<std::string>* searched) override
    {
        if (searched)
            searched->push_back("(демо-режим: dfu-util не нужен)");
        return "dfu-util (демо)";
    }

    bool DfuDevicePresent(bool* driver) override
    {
        if (driver)
            *driver = true;
        return dev_->InDfu();
    }

    std::unique_ptr<IProcess> Start(const std::string&, const std::vector<std::string>& args, std::string*) override
    {
        auto p = std::make_unique<Proc>();
        p->clock = clock_;
        p->start = clock_();
        p->dev = dev_;
        std::string s;
        for (const auto& a : args)
            s += a + " ";
        p->leave = s.find(":leave") != std::string::npos;
        p->upload = std::find(args.begin(), args.end(), "-U") != args.end();
        for (std::size_t i = 0; i + 1 < args.size(); i++)
        {
            if (args[i] == "-D")
                lastImage_ = args[i + 1];
            if (args[i] == "-U")
                p->out = args[i + 1];
        }
        p->image = lastImage_;
        return p;
    }

    bool IsDemo() const override { return true; }

private:
    struct Proc final : IProcess
    {
        std::function<std::int64_t()> clock;
        std::int64_t start = 0;
        std::shared_ptr<SimDevice> dev;
        bool leave = false, upload = false, finished = false;
        std::string out, image;
        int shown = -1;
        std::vector<std::string> pending;

        std::int64_t Duration() const { return leave ? 600 : upload ? 1500 : 3000; }

        bool Running() override
        {
            Advance();
            return !finished;
        }
        int ExitCode() override { return 0; }
        void Kill() override { finished = true; }

        void Advance()
        {
            if (finished)
                return;
            const std::int64_t t = clock() - start;
            if (shown < 0)
            {
                pending.push_back("dfu-util 0.11 (демо-режим)");
                pending.push_back("Opening DFU capable USB device...");
                pending.push_back("Device ID 0483:df11");
                pending.push_back("Downloading element to address = 0x08000000");
                shown = 0;
            }
            const int pct = static_cast<int>(std::min<std::int64_t>(100, t * 100 / Duration()));
            if (!leave)
                while (shown < pct)
                {
                    shown = std::min(100, shown + 10);
                    char b[96];
                    std::snprintf(b, sizeof(b), "%s\t[%-25.*s] %3d%%", upload ? "Upload" : "Download", shown / 4,
                                  "=========================", shown);
                    pending.push_back(b);
                }
            if (t >= Duration())
            {
                if (leave)
                {
                    pending.push_back("Submitting leave request...");
                    dev->LeaveDfu(clock(), "");
                }
                else if (upload)
                {
                    std::error_code ec;
                    std::filesystem::copy_file(text::PathFromUtf8(image), text::PathFromUtf8(out),
                                               std::filesystem::copy_options::overwrite_existing, ec);
                    pending.push_back("Upload done.");
                }
                else
                    pending.push_back("File downloaded successfully");
                finished = true;
            }
        }

        std::vector<std::string> TakeLines() override
        {
            Advance();
            std::vector<std::string> o;
            o.swap(pending);
            return o;
        }
    };

    std::shared_ptr<SimDevice> dev_;
    std::function<std::int64_t()> clock_;
    std::string lastImage_;
};

// ---------------------------------------------------------------------------------------------------------------
// Обновление
// ---------------------------------------------------------------------------------------------------------------

class Updater
{
public:
    enum class Step
    {
        Idle,
        Boot,       // ждём ответ на boot
        WaitDfu,    // ждём DFU 0483:DF11
        Download,   // dfu-util -D
        Readback,   // dfu-util -U + сравнение
        Leave,      // dfu-util …:leave
        WaitDevice, // ждём прибор на COM
        Done,
        Failed,
    };

    struct LogLine
    {
        char kind; // 'i' — шаг, 'o' — вывод dfu-util, 'e' — ошибка, 'k' — успех
        std::string text;
    };

    Updater(DeviceLink* link, IDfuBackend* backend) : link_(link), backend_(backend) {}

    void SetBackend(DeviceLink* link, IDfuBackend* backend)
    {
        if (Busy())
            return;
        link_ = link;
        backend_ = backend;
    }

    bool Busy() const { return step_ != Step::Idle && step_ != Step::Done && step_ != Step::Failed; }
    Step GetStep() const { return step_; }
    double Progress() const { return progress_; }
    const std::vector<LogLine>& Log() const { return log_; }
    const std::string& Error() const { return error_; }
    const std::string& NewVersion() const { return newVersion_; }
    bool NeedsForce() const { return needForce_; }
    const ImageInfo& Image() const { return image_; }

    // Начать: образ, сразу в загрузчик (force — прервать запись). false — не начато (ошибка в Error()).
    bool Start(const std::string& imagePath, bool force, std::int64_t now)
    {
        if (Busy())
            return false;
        log_.clear();
        error_.clear();
        newVersion_.clear();
        needForce_ = false;
        progress_ = 0.0;
        std::vector<std::uint8_t> data;
        if (!ReadFile(text::PathFromUtf8(imagePath), data))
            return Fail("не удалось прочитать файл " + imagePath);
        image_ = CheckImage(data);
        if (!image_.ok)
            return Fail("образ не годится: " + image_.error);
        data_ = std::move(data);
        dfu_ = backend_->FindDfuUtil();
        if (dfu_.empty())
            return Fail("не найден dfu-util.exe — положите его рядом с программой (см. README) или установите Arduino IDE");
        // Копия образа во временную папку с коротким путём: dfu-util (ANSI argv) не прочтёт кириллицу вне кодовой
        // страницы.
        std::error_code ec;
        const auto tmp = std::filesystem::temp_directory_path(ec) / L"krenomer_fw";
        std::filesystem::create_directories(tmp, ec);
        imageCopy_ = tmp / L"image.bin";
        readback_ = tmp / L"readback.bin";
        leaveFile_ = tmp / L"leave.bin";
        std::filesystem::copy_file(text::PathFromUtf8(imagePath), imageCopy_, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec)
            return Fail("не удалось скопировать образ во временную папку: " + ec.message());
        Info("образ: " + std::to_string(image_.size) + " байт, версия " + (image_.version.empty() ? "?" : image_.version) +
             (image_.build.empty() ? "" : " (сборка " + image_.build + ")"));
        Info("dfu-util: " + dfu_);
        startConnectSeq_ = link_->Snapshot().connectSeq;
        if (backend_->DfuDevicePresent())
        {
            Info("прибор уже в загрузчике (DFU 0483:DF11)");
            Go(Step::WaitDfu, now, 15000);
            return true;
        }
        if (!link_->IsConnected())
            return Fail("прибор не подключён: подключите его по USB (или переведите в загрузчик кнопками BOOT0 + NRST)");
        Info(std::string("перевод прибора в загрузчик: ") + (force ? "boot force" : "boot"));
        bootReq_ = link_->Send(force ? "boot force" : "boot", Origin::App, 4000);
        Go(Step::Boot, now, 6000);
        return true;
    }

    void Cancel()
    {
        if (proc_)
            proc_->Kill();
        proc_.reset();
        if (Busy())
            Fail("отменено пользователем");
    }

    void Tick(std::int64_t now)
    {
        if (proc_)
            for (auto& l : proc_->TakeLines())
            {
                const int p = ParseDfuProgress(l);
                if (p >= 0)
                {
                    progress_ = p / 100.0;
                    // Прогресс не дублировать в журнале: последняя строка прогресса заменяется
                    if (!log_.empty() && log_.back().kind == 'o' && ParseDfuProgress(log_.back().text) >= 0)
                    {
                        log_.back().text = l;
                        continue;
                    }
                }
                log_.push_back({'o', l});
                if (log_.size() > 400)
                    log_.erase(log_.begin());
            }
        switch (step_)
        {
        case Step::Boot:
            if (bootReq_ && bootReq_->done.load())
            {
                if (bootReq_->ok)
                {
                    Info("прибор ответил «" + bootReq_->final + "» — ждём USB DFU 0483:DF11");
                    Go(Step::WaitDfu, now, 15000);
                }
                else if (proto::StartsWith(bootReq_->final, "ERR recording"))
                {
                    needForce_ = true;
                    Fail("идёт запись замера на карту: остановите её тумблером на приборе или нажмите «Прервать запись "
                         "и обновить»");
                }
                else
                    Fail("прибор не перешёл в загрузчик: " + bootReq_->final);
            }
            else if (now > deadline_)
                Fail("нет ответа на boot");
            break;
        case Step::WaitDfu:
        {
            bool driver = false;
            if (backend_->DfuDevicePresent(&driver))
            {
                if (!driver && !backend_->IsDemo())
                {
                    if (!warnedDriver_)
                        Error("у устройства DFU нет драйвера: установите WinUSB утилитой Zadig (см. README) — пробую "
                              "всё равно");
                    warnedDriver_ = true;
                }
                if (dfuSeenAt_ == 0)
                    dfuSeenAt_ = now;
                if (now - dfuSeenAt_ >= 700) // загрузчику — время на перечисление
                {
                    Info("загрузчик на USB — запись образа");
                    StartDfu({"-d", kDfuId, "-a", "0", "-s", "0x08000000", "-D", ShortPath(imageCopy_)});
                    Go(Step::Download, now, 120000);
                }
            }
            else if (now > deadline_)
                Fail("устройство USB DFU 0483:DF11 не появилось за 15 с (драйвер DFU, кабель?). Прибор можно "
                     "перевести в загрузчик вручную: держать BOOT0, нажать и отпустить NRST");
            break;
        }
        case Step::Download:
            if (ProcFinished())
            {
                if (exitCode_ != 0)
                {
                    Fail("dfu-util: запись не удалась (код " + std::to_string(exitCode_) + ")" + DriverHint());
                    break;
                }
                Info("записано — чтение обратно для проверки");
                std::error_code ec;
                std::filesystem::remove(readback_, ec); // dfu-util -U не перезаписывает файл
                StartDfu({"-d", kDfuId, "-a", "0", "-s", "0x08000000:" + std::to_string(image_.size), "-U",
                          ShortPath(readback_)});
                Go(Step::Readback, now, 120000);
            }
            else if (now > deadline_)
                Fail("dfu-util: запись идёт дольше 2 мин — прервано");
            break;
        case Step::Readback:
            if (ProcFinished())
            {
                std::vector<std::uint8_t> rb;
                if (exitCode_ != 0 || !ReadFile(readback_, rb))
                {
                    Fail("dfu-util: чтение обратно не удалось (код " + std::to_string(exitCode_) +
                         "). Прибор остаётся в загрузчике — повторите запись");
                    break;
                }
                if (rb != data_)
                {
                    Fail("проверка НЕ прошла: прочитанное не совпало с образом. Прибор остаётся в загрузчике — "
                         "повторите запись");
                    break;
                }
                Ok("проверка прошла: " + std::to_string(rb.size()) + " байт совпали — запуск новой прошивки");
                std::error_code ec;
                std::filesystem::remove(leaveFile_, ec);
                StartDfu({"-d", kDfuId, "-a", "0", "-s", "0x08000000:4:leave", "-U", ShortPath(leaveFile_)});
                Go(Step::Leave, now, 20000);
            }
            else if (now > deadline_)
                Fail("dfu-util: чтение обратно идёт дольше 2 мин — прервано");
            break;
        case Step::Leave:
            if (ProcFinished() || now > deadline_)
            {
                proc_.reset();
                Info("ждём прибор на COM-порту");
                Go(Step::WaitDevice, now, 40000);
            }
            break;
        case Step::WaitDevice:
        {
            const auto s = link_->Snapshot();
            if (s.state == LinkState::Connected && s.connectSeq != startConnectSeq_ && s.haveVer)
            {
                newVersion_ = s.ver.version;
                progress_ = 1.0;
                Ok("готово: прибор на связи, прошивка " + (newVersion_.empty() ? "?" : "v" + newVersion_) +
                   (s.ver.build.empty() ? "" : ", сборка " + s.ver.build));
                step_ = Step::Done;
            }
            else if (now > deadline_)
                Fail("прошивка записана и проверена, но прибор не появился на COM-порту за 40 с: переподключите USB");
            break;
        }
        default:
            break;
        }
    }

private:
    bool Fail(const std::string& e)
    {
        if (proc_)
            proc_->Kill();
        proc_.reset();
        error_ = e;
        log_.push_back({'e', e});
        step_ = Step::Failed;
        return false;
    }
    void Info(const std::string& s) { log_.push_back({'i', s}); }
    void Ok(const std::string& s) { log_.push_back({'k', s}); }
    void Error(const std::string& s) { log_.push_back({'e', s}); }

    void Go(Step s, std::int64_t now, std::int64_t timeoutMs)
    {
        step_ = s;
        deadline_ = now + timeoutMs;
        if (s == Step::Download || s == Step::Readback)
            progress_ = 0.0;
        if (s == Step::WaitDfu)
            dfuSeenAt_ = 0;
    }

    void StartDfu(const std::vector<std::string>& args)
    {
        std::string cmd = "dfu-util";
        for (const auto& a : args)
            cmd += " " + a;
        log_.push_back({'o', "> " + cmd});
        std::string err;
        proc_ = backend_->Start(dfu_, args, &err);
        if (!proc_)
            Fail(err.empty() ? "не удалось запустить dfu-util" : err);
    }

    bool ProcFinished()
    {
        if (!proc_)
            return true;
        if (proc_->Running())
            return false;
        for (auto& l : proc_->TakeLines())
            log_.push_back({'o', l});
        exitCode_ = proc_->ExitCode();
        proc_.reset();
        return true;
    }

    std::string DriverHint() const
    {
        for (auto it = log_.rbegin(); it != log_.rend() && it - log_.rbegin() < 30; ++it)
            if (it->text.find("Cannot open DFU device") != std::string::npos ||
                it->text.find("No DFU capable USB device") != std::string::npos ||
                it->text.find("LIBUSB_ERROR") != std::string::npos)
                return ". Похоже, у устройства DFU нет драйвера WinUSB: установите его утилитой Zadig (README)";
        return "";
    }

    static std::string ShortPath(const std::filesystem::path& p)
    {
#ifdef _WIN32
        wchar_t buf[MAX_PATH] = {};
        // Файла ещё может не быть (readback): короткий путь — у папки
        const auto parent = p.parent_path();
        if (GetShortPathNameW(parent.c_str(), buf, MAX_PATH) > 0)
            return text::PathToUtf8(std::filesystem::path(buf) / p.filename());
#endif
        return text::PathToUtf8(p);
    }

    DeviceLink* link_;
    IDfuBackend* backend_;
    Step step_ = Step::Idle;
    double progress_ = 0.0;
    std::vector<LogLine> log_;
    std::string error_, newVersion_, dfu_;
    ImageInfo image_;
    std::vector<std::uint8_t> data_;
    std::filesystem::path imageCopy_, readback_, leaveFile_;
    RequestPtr bootReq_;
    std::unique_ptr<IProcess> proc_;
    int exitCode_ = 0;
    std::int64_t deadline_ = 0, dfuSeenAt_ = 0;
    std::uint64_t startConnectSeq_ = 0;
    bool needForce_ = false, warnedDriver_ = false;
};

} // namespace fw
