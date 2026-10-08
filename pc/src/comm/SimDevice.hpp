#pragma once
// Прибор-имитатор для демо-режима и тестов: говорит тем же протоколом, что прошивка (usb_cli.c + usb_cli_ext.c):
// ver, help, diag, diag reset, stream N (шапка и строки S), set …, set time, status (тот же JSON), addr, zero,
// files, get (G / D / E с base64 и CRC-32), get abort, boot / dfu, reset. Форматы ответов — как у прошивки, чтобы
// демо проверяло тот же разбор, что и настоящий прибор.
//
// Модель: опыт кренования — каждые 40 с груз переносят, крен меняется ступенькой (0 → +1 → +2 → +1 → 0 → −1 → −2 → −1)
// и затухающими колебаниями; поверх — качка (слабая / сильная) и шум датчика. Фильтр EMA, ноль, окно качки с
// порогом и гистерезисом, сводка ГОТОВ/КАЧКА, запись на карту по тумблеру, АКБ, статистика шины — как в app.c.
//
// Все открытые методы потокобезопасны (один мьютекс). Вывод — через emitter (строки без \r\n), вызывается под
// мьютексом имитатора.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "../core/Codec.hpp"
#include "../core/Protocol.hpp"

class SimDevice
{
public:
    static constexpr int kSensors = 2;
    static constexpr int kAddr[kSensors] = {2, 3};

    // Пульт демо-режима (интерфейс).
    struct Controls
    {
        bool recSwitch = false;               // тумблер записи
        bool sensorOn[kSensors] = {true, true}; // датчик на шине
        bool card = true;                     // карта вставлена
        int motion = 1;                       // качка: 0 — штиль, 1 — слабая, 2 — сильная
        bool batteryLow = false;              // АКБ разряжена
    };

    explicit SimDevice(std::uint32_t seed = 12345, std::time_t startTime = 0) : rng_(seed)
    {
        // Часы прибора отстают от ПК на 37 с (видно на «Синхронизировать время»).
        const std::time_t now = startTime ? startTime : std::time(nullptr);
        clockBase_ = now - 37;
        MakeInitialFiles(now);
        ResetState(0);
    }

    void SetEmitter(std::function<void(const std::string&)> emit)
    {
        std::lock_guard lock(m_);
        emit_ = std::move(emit);
    }

    Controls GetControls() const
    {
        std::lock_guard lock(m_);
        return ctl_;
    }

    void SetControls(const Controls& c)
    {
        std::lock_guard lock(m_);
        ctl_ = c;
    }

    // USB виден (иначе — перезагрузка / загрузчик: порт пропал).
    bool UsbPresent(std::int64_t now) const
    {
        std::lock_guard lock(m_);
        return !inDfu_ && now >= usbBackAt_;
    }

    // В системном загрузчике (USB DFU 0483:DF11).
    bool InDfu() const
    {
        std::lock_guard lock(m_);
        return inDfu_;
    }

    // dfu-util … :leave — запуск (новой) прошивки через 1,5 с.
    void LeaveDfu(std::int64_t now, const std::string& version)
    {
        std::lock_guard lock(m_);
        if (!inDfu_)
            return;
        inDfu_ = false;
        if (!version.empty())
            fwVersion_ = version;
        usbBackAt_ = now + 1500;
        ResetState(now + 1500);
    }

    std::string FwVersion() const
    {
        std::lock_guard lock(m_);
        return fwVersion_;
    }

    // Порт открыли (DTR 0 -> 1): прибор выбрасывает неотправленное; закрыли — выключает поток.
    void PortOpened()
    {
        std::lock_guard lock(m_);
        streamMs_ = 0;
        xfer_ = {};
    }

    // Строка команды (без \r\n).
    void Command(const std::string& raw, std::int64_t now)
    {
        std::lock_guard lock(m_);
        Advance(now);
        std::string line;
        for (char c : raw)
        {
            if (c == '\t')
                c = ' ';
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E)
                continue;
            if (c == ' ' && line.empty())
                continue;
            line += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
        }
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        if (line.empty())
            return;
        if (line.size() > 64)
        {
            Out("ERR line too long (max 64)");
            return;
        }
        std::string cmd = line, arg;
        const auto sp = line.find(' ');
        if (sp != std::string::npos)
        {
            cmd = line.substr(0, sp);
            arg = line.substr(line.find_first_not_of(' ', sp));
        }
        Exec(cmd, arg, now);
    }

    // Время идёт: опрос датчиков, окно качки, запись, поток, передача файла.
    void Tick(std::int64_t now)
    {
        std::lock_guard lock(m_);
        Advance(now);
        if (inDfu_ || now < usbBackAt_)
            return;
        if (streamMs_ > 0 && now - streamLast_ >= streamMs_)
        {
            streamLast_ = now;
            Out(StreamLine(now));
        }
        XferStep(now);
    }

    // Содержимое файла на «карте» (тесты сверяют скачанное).
    std::string FileContent(const std::string& name) const
    {
        std::lock_guard lock(m_);
        for (const auto& f : files_)
            if (EqualNoCase(f.name, name))
                return f.data;
        return {};
    }

    // Для тестов: сколько строк D уходит за Tick (скорость «USB»).
    void SetXferBytesPerMs(double v)
    {
        std::lock_guard lock(m_);
        xferBytesPerMs_ = v;
    }

private:
    struct SimFile
    {
        std::string name;
        std::string data;
        std::time_t mtime = 0;
    };

    struct Stat
    {
        std::uint64_t n = 0, last = 0, min = 0, max = 0, sum = 0;
        void Add(std::uint64_t us)
        {
            last = us;
            if (n == 0 || us < min)
                min = us;
            if (us > max)
                max = us;
            sum += us;
            n++;
        }
        std::uint64_t Avg() const { return n ? sum / n : 0; }
    };

    struct Sensor
    {
        bool ok = false;      // status OK
        bool everOk = false;  // иначе ABSENT
        double rawX = 0, rawY = 0; // ответ датчика, шаг 0,01°
        double filtX = 0, filtY = 0, offX = 0, offY = 0;
        bool filtInit = false;
        std::uint64_t okCount = 0, errCount = 0, garbled = 0;
        std::int64_t lastOkMs = 0;
        int lastErr = 0; // bwm427_result_t
        std::uint64_t to = 0, crc = 0, bad = 0;
        Stat lat, done;
        // окно качки
        std::deque<std::pair<double, double>> win; // (x, y) с частотой rollhz
        double rollX = 0, rollY = 0;
        bool calmX = false, calmY = false;
        int fill = 0;
        double fillS = 0;
    };

    struct Xfer
    {
        int mode = 0; // 0 — нет, 1 — список, 2 — передача
        std::size_t file = 0;
        std::uint64_t pos = 0, start = 0;
        std::uint32_t crc = 0;
        double budget = 0;
        std::int64_t last = 0;
    };

    // ---------------------------------------------------------------------------------------------------------------
    // Модель
    // ---------------------------------------------------------------------------------------------------------------

    void ResetState(std::int64_t now)
    {
        bootMs_ = now;
        simMs_ = now;
        nextPoll_ = now;
        nextRoll_ = now;
        for (auto& s : sens_)
            s = Sensor{};
        streamMs_ = 0;
        xfer_ = {};
        sdState_ = ctl_.card ? "READY" : "NO_CARD";
        recRows_ = 0;
        recMask_ = 0;
        svc_ = "IDLE";
        stable_ = false;
        busGap_ = busIdle_ = busCyc_ = Stat{};
    }

    static double Heel(double t)
    {
        // Ступени крена (груз переносят каждые 40 с), переход с постоянной времени 4 с
        static const double steps[] = {0.0, 1.0, 2.0, 1.0, 0.0, -1.0, -2.0, -1.0};
        const double period = 40.0;
        const int k = static_cast<int>(std::floor(t / period));
        const double dt = t - k * period;
        const double cur = steps[((k % 8) + 8) % 8];
        const double prev = steps[(((k - 1) % 8) + 8) % 8];
        double h = cur + (prev - cur) * std::exp(-dt / 4.0);
        // Колебания после переноса груза (затухают за ~20 с)
        h += 0.9 * (cur - prev) * std::exp(-dt / 7.0) * std::sin(2.0 * 3.14159265 * dt / 6.5);
        return h;
    }

    // Истинные углы датчика i в момент t (с)
    void TrueAngles(int i, double t, double& x, double& y)
    {
        const double amp = ctl_.motion == 0 ? 0.03 : ctl_.motion == 1 ? 0.22 : 1.3;
        const double roll = amp * std::sin(2.0 * 3.14159265 * t / 6.2 + i * 0.4) +
                            0.35 * amp * std::sin(2.0 * 3.14159265 * t / 2.9 + 1.0);
        const double heel = Heel(t);
        std::normal_distribution<double> noise(0.0, 0.008);
        if (i == 0)
        {
            x = 0.12 + heel + roll + noise(rng_);
            y = -0.35 + 0.08 * heel + 0.15 * roll + noise(rng_);
        }
        else
        {
            // Д3 стоит на другом борту и чуть «качается» сильнее
            x = -0.05 + heel + 1.6 * roll + noise(rng_);
            y = 0.21 + 0.06 * heel + 0.3 * roll + noise(rng_);
        }
    }

    void Advance(std::int64_t now)
    {
        if (now <= simMs_)
            return;
        // Шаг — цикл опроса; если отстали надолго (тест), не больше 5 мин модели.
        if (now - simMs_ > 300000)
            simMs_ = now - 300000, nextPoll_ = std::max(nextPoll_, simMs_), nextRoll_ = std::max(nextRoll_, simMs_);
        const int pollMs = std::max(1, 1000 / std::max(1, freq_));
        const int rollMs = 1000 / std::max(1, rollHz_);
        while (nextPoll_ <= now || nextRoll_ <= now)
        {
            if (nextPoll_ <= nextRoll_)
            {
                PollCycle(nextPoll_);
                nextPoll_ += pollMs;
            }
            else
            {
                RollSample(nextRoll_);
                nextRoll_ += rollMs;
            }
        }
        // Часы, батарея, карта
        if (!ctl_.card && sdState_ != "NO_CARD")
        {
            if (sdState_ == "RECORDING")
                StopRecording(now);
            sdState_ = "NO_CARD";
            xfer_ = {};
        }
        else if (ctl_.card && sdState_ == "NO_CARD")
            sdState_ = "READY";
        if (sdState_ == "READY" && ctl_.recSwitch)
            StartRecording(now);
        else if (sdState_ == "RECORDING" && !ctl_.recSwitch)
            StopRecording(now);
        simMs_ = now;
        const double target = ctl_.batteryLow ? 9.6 : 12.1 - (now - bootMs_) / 1000.0 * 0.00002;
        batV_ += (target - batV_) * 0.2;
        batLow_ = batV_ < batAlarm_ || (batLow_ && batV_ < batAlarm_ + 0.3);
        if (svc_ == "BUSY" && now >= svcDoneAt_)
            FinishSvc(now);
    }

    void PollCycle(std::int64_t t)
    {
        const double ts = (t - bootMs_) / 1000.0 + 12.0; // модель с 12-й секунды: уже ступенька
        std::uniform_int_distribution<int> jitter(0, 400);
        std::uint64_t cyc = 0;
        bool allOk = true;
        for (int i = 0; i < kSensors; i++)
        {
            Sensor& s = sens_[i];
            if (i > 0)
            {
                const std::uint64_t gap = static_cast<std::uint64_t>(gap_) * 1000 + static_cast<std::uint64_t>(jitter(rng_) / 8);
                busGap_.Add(gap);
                cyc += gap;
            }
            if (!ctl_.sensorOn[i])
            {
                if (s.ok)
                {
                    s.errCount++;
                    s.to++;
                    s.lastErr = 1; // TIMEOUT
                    if (s.errCount % 3 == 0)
                        s.ok = false;
                }
                allOk = false;
                cyc += 30000;
                continue;
            }
            double x, y;
            TrueAngles(i, ts, x, y);
            x = std::round(x * 100.0) / 100.0; // датчик отдаёт сотые градуса
            y = std::round(y * 100.0) / 100.0;
            s.rawX = x;
            s.rawY = y;
            if (!s.filtInit)
            {
                s.filtX = x;
                s.filtY = y;
                s.filtInit = true;
            }
            s.filtX += alpha_ * (x - s.filtX);
            s.filtY += alpha_ * (y - s.filtY);
            s.ok = true;
            s.everOk = true;
            s.okCount++;
            s.lastOkMs = t;
            const std::uint64_t lat = (i == 0 ? 3000 : 7000) + static_cast<std::uint64_t>(jitter(rng_));
            s.lat.Add(lat);
            s.done.Add(lat + 781);
            cyc += 694 + lat + 781;
        }
        if (allOk)
            busCyc_.Add(cyc);
        busIdle_.Add(1000 + static_cast<std::uint64_t>(jitter(rng_)) * 10);
        if (sdState_ == "RECORDING")
            for (int i = 0; i < kSensors; i++)
                if (sens_[i].ok)
                {
                    recRows_++;
                    recMask_ |= 1 << i;
                    AppendRow(i, t);
                }
    }

    void RollSample(std::int64_t t)
    {
        const std::size_t cap = static_cast<std::size_t>(rollWin_ * rollHz_);
        bool all = true, any = false;
        double span = 0.0;
        int spanSensor = -1, spanAxis = 0;
        int minFill = rollWin_;
        for (int i = 0; i < kSensors; i++)
        {
            Sensor& s = sens_[i];
            if (!s.ok)
            {
                s.win.clear();
                s.fill = 0;
                s.calmX = s.calmY = false;
                continue;
            }
            s.win.emplace_back(s.filtX, s.filtY);
            while (s.win.size() > cap)
                s.win.pop_front();
            s.fill = static_cast<int>(s.win.size() / std::max(1, rollHz_));
            double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
            for (const auto& [x, y] : s.win)
            {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
            s.rollX = maxX - minX;
            s.rollY = maxY - minY;
            const bool full = s.win.size() >= cap;
            auto calm = [&](bool was, double r) { return full && (was ? r <= rollCalm_ + rollHyst_ : r < rollCalm_); };
            s.calmX = calm(s.calmX, s.rollX);
            s.calmY = calm(s.calmY, s.rollY);
            any = true;
            all = all && s.calmX && s.calmY;
            minFill = std::min(minFill, s.fill);
            if (s.rollX > span)
                span = s.rollX, spanSensor = i, spanAxis = 0;
            if (s.rollY > span)
                span = s.rollY, spanSensor = i, spanAxis = 1;
        }
        stable_ = any && all;
        stabSpan_ = span;
        stabSensor_ = spanSensor;
        stabAxis_ = spanAxis;
        stabFill_ = any ? minFill : 0;
        (void) t;
    }

    void ResetRollWindows()
    {
        for (auto& s : sens_)
        {
            s.win.clear();
            s.fill = 0;
            s.calmX = s.calmY = false;
        }
        stable_ = false;
        stabFill_ = 0;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Карта
    // ---------------------------------------------------------------------------------------------------------------

    static std::string Two(int v)
    {
        char b[8];
        std::snprintf(b, sizeof(b), "%02d", v);
        return b;
    }

    static std::tm LocalTm(std::time_t t)
    {
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        return tm;
    }

    static std::string FileName(std::time_t day, int num, int addr)
    {
        const std::tm tm = LocalTm(day);
        char b[64];
        std::snprintf(b, sizeof(b), "%04d-%02d-%02d_M%03d_D%d.CSV", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, num, addr);
        return b;
    }

    // Целое v / 10^dec с десятичной запятой, как sd_logger.c: (-46, 2) -> "-0,46"
    static std::string Scaled(long long v, int dec)
    {
        long long scale = 1;
        for (int k = 0; k < dec; k++)
            scale *= 10;
        const long long a = v < 0 ? -v : v;
        char b[32];
        if (dec)
            std::snprintf(b, sizeof(b), "%s%lld,%0*lld", v < 0 ? "-" : "", a / scale, dec, a % scale);
        else
            std::snprintf(b, sizeof(b), "%s%lld", v < 0 ? "-" : "", a);
        return b;
    }

    // Строка как у прибора 1.4: сырой угол (сотые), ноль и Raw − Offset (тысячные), «;», запятая
    static std::string Row(std::time_t t, double rx, double ry, double ox, double oy, double bat, std::int64_t ms)
    {
        const std::tm tm = LocalTm(t);
        const long long rcx = std::llround(rx * 100.0), rcy = std::llround(ry * 100.0);
        const long long omx = std::llround(ox * 1000.0), omy = std::llround(oy * 1000.0);
        char d[40];
        std::snprintf(d, sizeof(d), "%02d.%02d.%04d;%02d:%02d:%02d;", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
                      tm.tm_hour, tm.tm_min, tm.tm_sec);
        return d + Scaled(rcx, 2) + ";" + Scaled(rcy, 2) + ";" + Scaled(omx, 3) + ";" + Scaled(omy, 3) + ";" +
               Scaled(rcx * 10 - omx, 3) + ";" + Scaled(rcy * 10 - omy, 3) + ";" + Scaled(std::llround(bat * 10.0), 1) +
               ";" + std::to_string(ms) + "\n";
    }

    static constexpr const char* kHeader = "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";

    void MakeInitialFiles(std::time_t now)
    {
        // Шесть прошлых замеров (по файлу на датчик), 5–8 мин при 10 Гц
        std::mt19937 r(7);
        for (int m = 1; m <= 6; m++)
        {
            const std::time_t start = now - (7 - m) * 86400 / 2 - 3600 * (m % 3);
            const int rows = 3000 + m * 600;
            for (int i = 0; i < kSensors; i++)
            {
                SimFile f;
                f.name = FileName(start, m, kAddr[i]);
                f.data = kHeader;
                const double base = (m - 3) * 0.7 + (i ? -0.17 : 0.0);
                for (int k = 0; k < rows; k++)
                {
                    const double tt = k / 10.0;
                    const double x = base + 0.15 * std::sin(tt * 1.01) + (r() % 100) / 10000.0;
                    const double y = -0.3 + 0.05 * std::sin(tt * 0.7) + (r() % 100) / 10000.0;
                    f.data += Row(start + k / 10, x, y, 0.0, 0.0, 12.4 - k * 0.00001, k * 100 + (i ? 40 : 0));
                }
                f.mtime = start + rows / 10;
                files_.push_back(std::move(f));
            }
        }
        nextNum_ = 7;
    }

    void StartRecording(std::int64_t now)
    {
        sdState_ = "RECORDING";
        recStartMs_ = now;
        recStartWall_ = WallTime(now);
        recRows_ = 0;
        recMask_ = 0;
        recData_[0] = recData_[1] = kHeader;
    }

    void StopRecording(std::int64_t now)
    {
        for (int i = 0; i < kSensors; i++)
            if (recMask_ & (1 << i))
            {
                SimFile f;
                f.name = FileName(recStartWall_, nextNum_, kAddr[i]);
                f.data = recData_[i];
                f.mtime = WallTime(now);
                files_.push_back(std::move(f));
            }
        if (recMask_)
            nextNum_++;
        recMask_ = 0;
        sdState_ = ctl_.card ? "READY" : "NO_CARD";
    }

    void AppendRow(int i, std::int64_t t)
    {
        const Sensor& s = sens_[i];
        recData_[i] += Row(WallTime(t), s.rawX, s.rawY, s.offX, s.offY, batV_, t - recStartMs_);
    }

    std::time_t WallTime(std::int64_t now) const { return clockBase_ + static_cast<std::time_t>(now / 1000); }

    std::string ClockString(std::int64_t now) const
    {
        const std::tm tm = LocalTm(WallTime(now));
        char b[32];
        std::snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                      tm.tm_min, tm.tm_sec);
        return b;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Вывод
    // ---------------------------------------------------------------------------------------------------------------

    void Out(const std::string& s)
    {
        if (emit_)
            emit_(s);
    }

    // Как fx() прошивки: фиксированная точка, «nan»
    static std::string Fx(double v, int dec)
    {
        if (!std::isfinite(v))
            return "nan";
        char b[48];
        std::snprintf(b, sizeof(b), "%.*f", dec, v);
        if (b[0] == '-' && std::atof(b) == 0.0)
            return std::string(b + 1); // как fx(): «-0.000» -> «0.000»
        return b;
    }

    std::string StreamHeaderLine() const
    {
        std::string h = "# S,t_ms,loop_ms";
        for (int a : kAddr)
            h += ",s" + std::to_string(a) + ",x" + std::to_string(a) + ",y" + std::to_string(a) + ",err" + std::to_string(a);
        h += ",sd,rate_hz,file,gap_ms,cpu,bat_v,bat_low";
        for (int a : kAddr)
        {
            const std::string n = std::to_string(a);
            h += ",ok" + n + ",to" + n + ",bad" + n + ",lat" + n + ",lmin" + n + ",lavg" + n + ",lmax" + n;
        }
        h += ",cyc_us,idle_us";
        return h;
    }

    std::string SdShort() const
    {
        if (sdState_ == "NO_CARD")
            return "NOCARD";
        if (sdState_ == "RECORDING")
            return "REC";
        if (sdState_ == "ERROR")
            return "ERR";
        return "READY";
    }

    char SensorChar(const Sensor& s) const { return s.ok ? 'O' : s.everOk ? 'L' : 'A'; }

    double Rate() const
    {
        bool any = false;
        for (const auto& s : sens_)
            any = any || s.ok;
        return any ? freq_ * 0.996 : 0.0;
    }

    std::string StreamLine(std::int64_t now)
    {
        std::string l = "S," + std::to_string(now) + ",3";
        for (int i = 0; i < kSensors; i++)
        {
            const Sensor& s = sens_[i];
            l += ",";
            l += SensorChar(s);
            l += "," + Fx(s.filtX - s.offX, 3) + "," + Fx(s.filtY - s.offY, 3) + "," + std::to_string(s.errCount);
        }
        l += "," + SdShort() + "," + Fx(Rate(), 1) + "," + std::to_string(sdState_ == "NO_CARD" ? 0 : nextNum_) + "," +
             std::to_string(gap_) + "," + std::to_string(Cpu()) + "," + Fx(batV_, 2) + "," + (batLow_ ? "1" : "0");
        for (const auto& s : sens_)
            l += "," + std::to_string(s.lat.n) + "," + std::to_string(s.to) + "," + std::to_string(s.crc + s.bad) + "," +
                 std::to_string(s.lat.last) + "," + std::to_string(s.lat.min) + "," + std::to_string(s.lat.Avg()) + "," +
                 std::to_string(s.lat.max);
        l += "," + std::to_string(busCyc_.last) + "," + std::to_string(busIdle_.last);
        return l;
    }

    int Cpu() const { return 11 + static_cast<int>((simMs_ / 1000) % 3) + (xfer_.mode ? 6 : 0); }

    std::string StatusJson(std::int64_t now) const
    {
        std::string j = "{\"v\":1,\"fw\":\"" + fwVersion_ + "\"";
        auto u = [&](const char* k, std::uint64_t v) { j += ",\"" + std::string(k) + "\":" + std::to_string(v); };
        auto f = [&](const char* k, double v, int dec) {
            j += ",\"" + std::string(k) + "\":" + (std::isfinite(v) ? Fx(v, dec) : std::string("null"));
        };
        auto b = [&](const char* k, bool v) { j += ",\"" + std::string(k) + "\":" + (v ? "true" : "false"); };
        auto st = [&](const char* k, const Stat& s) {
            j += ",\"" + std::string(k) + "\":[" + std::to_string(s.n) + "," + std::to_string(s.last) + "," +
                 std::to_string(s.min) + "," + std::to_string(s.Avg()) + "," + std::to_string(s.max) + "]";
        };
        u("up", static_cast<std::uint64_t>((now - bootMs_) / 1000));
        u("ms", static_cast<std::uint64_t>(now));
        j += ",\"time\":\"" + ClockString(now) + "\"";
        b("rtc", true);
        u("freq", static_cast<std::uint64_t>(freq_));
        f("alpha", alpha_, 2);
        u("gap", static_cast<std::uint64_t>(gap_));
        j += std::string(",\"theme\":\"") + (themeLight_ ? "light" : "dark") + "\"";
        f("batalarm", batAlarm_, 2);
        u("rollwin", static_cast<std::uint64_t>(rollWin_));
        u("rollhz", static_cast<std::uint64_t>(rollHz_));
        f("rollcalm", rollCalm_, 2);
        f("rollhyst", rollHyst_, 2);
        b("save_pending", now < savePendingUntil_);
        f("rate", Rate(), 1);
        u("cpu", static_cast<std::uint64_t>(Cpu()));
        u("loops", 2400);
        u("loop_max", 5);
        j += ",\"sens\":[";
        for (int i = 0; i < kSensors; i++)
        {
            const Sensor& s = sens_[i];
            if (i)
                j += ",";
            j += "{\"addr\":" + std::to_string(kAddr[i]) + ",\"st\":\"" + (s.ok ? "OK" : s.everOk ? "LOST" : "ABSENT") + "\"";
            f("x", s.filtX - s.offX, 3);
            f("y", s.filtY - s.offY, 3);
            f("ox", s.offX, 3);
            f("oy", s.offY, 3);
            f("roll_x", s.rollX, 3);
            f("roll_y", s.rollY, 3);
            b("calm_x", s.calmX);
            b("calm_y", s.calmY);
            u("fill", static_cast<std::uint64_t>(s.fill));
            u("ok", s.okCount);
            u("err", s.errCount);
            u("garbled", s.garbled);
            static const char* errs[] = {"-", "TIMEOUT", "CRC", "BAD_FRAME", "EXCEPTION", "UART", "PENDING"};
            j += std::string(",\"last_err\":\"") + errs[std::clamp(s.lastErr, 0, 6)] + "\"";
            if (s.okCount == 0)
                j += ",\"age\":null";
            else
                u("age", static_cast<std::uint64_t>(now - s.lastOkMs));
            u("to", s.to);
            u("crc", s.crc);
            u("bad", s.bad);
            u("tmo_us", s.ok ? (i == 0 ? 16781 : 20000) : 30000);
            st("lat", s.lat);
            st("done", s.done);
            j += "}";
        }
        j += "]";
        b("stable", stable_);
        f("span", stabSpan_, 3);
        if (stabSensor_ >= 0)
            j += ",\"stab_addr\":" + std::to_string(kAddr[stabSensor_]) + ",\"stab_axis\":\"" + (stabAxis_ ? "Y" : "X") + "\"";
        else
            j += ",\"stab_addr\":null,\"stab_axis\":null";
        u("stab_fill", static_cast<std::uint64_t>(stabFill_));
        j += ",\"bus\":{\"fallbacks\":0";
        st("gap", busGap_);
        st("idle", busIdle_);
        st("cyc", busCyc_);
        f("max_rate", busCyc_.Avg() ? 1e6 / static_cast<double>(busCyc_.Avg()) : 0.0, 2);
        j += "}";
        j += ",\"sd\":\"" + sdState_ + "\"";
        u("sd_err", 0);
        const bool card = sdState_ != "NO_CARD";
        u("file", card ? static_cast<std::uint64_t>(nextNum_) : 0);
        j += ",\"names\":[";
        if (card)
        {
            const std::time_t day = sdState_ == "RECORDING" ? recStartWall_ : WallTime(now);
            for (int i = 0; i < kSensors; i++)
                j += std::string(i ? "," : "") + "\"" + FileName(day, nextNum_, kAddr[i]) + "\"";
        }
        j += "]";
        u("rec_mask", static_cast<std::uint64_t>(recMask_));
        u("rows", recRows_);
        u("rec_s", sdState_ == "RECORDING" ? static_cast<std::uint64_t>((now - recStartMs_) / 1000) : 0);
        std::uint64_t used = 0;
        for (const auto& fl : files_)
            used += fl.data.size();
        u("sd_total", card ? 7580 : 0);
        if (card)
            u("sd_free", 7580 - 3 - used / (1024 * 1024));
        else
            j += ",\"sd_free\":null";
        f("bat_v", batV_, 2);
        b("bat_present", true);
        b("bat_low", batLow_);
        const double cell = batV_ / 3.0;
        const int pct = static_cast<int>(std::clamp((cell - 3.3) / (4.2 - 3.3) * 100.0, 0.0, 100.0) + 0.5);
        u("bat_pct", static_cast<std::uint64_t>(pct));
        b("bat_sat", false);
        b("rec_sw", ctl_.recSwitch);
        j += ",\"svc\":\"" + svc_ + "\"";
        j += std::string(",\"xfer\":\"") + (xfer_.mode == 2 ? "get" : xfer_.mode == 1 ? "list" : "idle") + "\"";
        j += "}";
        return j;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Команды
    // ---------------------------------------------------------------------------------------------------------------

    static bool EqualNoCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); i++)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    }

    static bool ParseUInt(const std::string& s, long long& v)
    {
        if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos)
            return false;
        v = std::atoll(s.c_str());
        return true;
    }

    static bool ParseDec(const std::string& s, double& v)
    {
        if (s.empty() || s.find_first_not_of("0123456789.") != std::string::npos ||
            std::count(s.begin(), s.end(), '.') > 1 || s == ".")
            return false;
        v = std::atof(s.c_str());
        return true;
    }

    std::string SaveNote(std::int64_t now)
    {
        savePendingUntil_ = now + 3000;
        return sdState_ == "RECORDING" ? "flash save after recording stops" : "flash save pending";
    }

    void Exec(const std::string& cmd, const std::string& arg, std::int64_t now)
    {
        if (cmd == "status")
        {
            if (!arg.empty())
                return Out("ERR usage: status");
            Out(StatusJson(now));
            return Out("OK");
        }
        if (cmd == "help" || cmd == "?")
        {
            for (const char* l : {"BWM427 USB CLI, commands (case-insensitive, end with CR/LF):",
                                  "  ver            firmware version, build date/time, HAL, chip UID",
                                  "  diag           one-shot status dump",
                                  "  diag reset     zero bus statistics (reply latency, timeouts, CRC, cycle)",
                                  "  stream N       status line 'S,...' every N ms (0 = off, 50..60000)",
                                  "  set freq N     poll/log rate, 1..50 Hz", "  set alpha X    EMA filter alpha, 0.01..0.99",
                                  "  set gap N      RS485 bus silence before each request, 2..100 ms",
                                  "  set theme dark|light   UI theme",
                                  "  set batalarm X battery low alarm below X V, 5..30 (default 10.0)",
                                  "  set rollwin N  roll window, 10..60 s (default 20)",
                                  "  set rollhz N   roll samples per second, 2..5 (default 5)",
                                  "  set rollcalm X calm threshold, deg (default 1.50)",
                                  "  set rollhyst X calm hysteresis, deg (default 0.20)",
                                  "  boot | dfu     reboot into STM32 system bootloader (USB DFU 0483:DF11)",
                                  "  boot force     ... even while recording to SD", "  reset [force]  reboot the firmware",
                                  "  status         one-line JSON snapshot (for the PC program)",
                                  "  set time YYYY-MM-DD HH:MM:SS   set the clock (and DS3231)",
                                  "  addr OLD NEW   change sensor Modbus address (only it on the bus!)",
                                  "  zero | zero reset   zero angles of answering sensors / clear zero",
                                  "  files          list *.CSV on the card: F,name,bytes,date time",
                                  "  get NAME [OFS] send a file: G,name,size,ofs  D,base64...  E,bytes,crc32",
                                  "  get abort      stop the transfer", "  help", "OK"})
                Out(l);
            return;
        }
        if (cmd == "ver")
        {
            Out("BWM427 inclinometer firmware v" + fwVersion_ + ", build Oct  7 2026 21:05:00");
            Out("HAL 1.8.1, SYSCLK 96 MHz, UID 0034003A3133510B37363834");
            return Out("OK");
        }
        if (cmd == "diag")
            return Diag(arg, now);
        if (cmd == "stream")
        {
            if (arg.empty())
                return Out(streamMs_ ? "OK stream every " + std::to_string(streamMs_) + " ms" : "OK stream off");
            long long ms;
            if (!ParseUInt(arg, ms))
                return Out("ERR usage: stream N (ms, 0 = off, 50..60000)");
            if (ms != 0 && ms < 50)
                ms = 50;
            if (ms > 60000)
                ms = 60000;
            streamMs_ = static_cast<int>(ms);
            if (ms == 0)
                return Out("OK stream off");
            Out(StreamHeaderLine());
            Out("OK stream every " + std::to_string(ms) + " ms");
            streamLast_ = now - ms;
            return;
        }
        if (cmd == "set")
            return Set(arg, now);
        if (cmd == "addr")
            return Addr(arg, now);
        if (cmd == "zero")
        {
            if (arg == "reset")
            {
                for (auto& s : sens_)
                    s.offX = s.offY = 0.0;
                return Out("OK zero reset");
            }
            if (!arg.empty())
                return Out("ERR usage: zero | zero reset");
            std::string list;
            for (int i = 0; i < kSensors; i++)
                if (sens_[i].ok)
                {
                    sens_[i].offX = sens_[i].filtX;
                    sens_[i].offY = sens_[i].filtY;
                    list += " D" + std::to_string(kAddr[i]);
                }
            return Out(list.empty() ? "ERR no answering sensors" : "OK zero set:" + list);
        }
        if (cmd == "files")
        {
            if (!arg.empty())
                return Out("ERR usage: files");
            xfer_ = {};
            if (!CardReady())
                return;
            xfer_.mode = 1;
            xfer_.file = 0;
            xfer_.last = now;
            return;
        }
        if (cmd == "get")
            return Get(arg, now);
        if (cmd == "boot" || cmd == "dfu" || cmd == "reset")
        {
            const bool force = arg == "force";
            if (!arg.empty() && !force)
                return Out("ERR usage: " + cmd + " [force]");
            if (sdState_ == "RECORDING" && !force)
                return Out("ERR recording to SD in progress: stop it or use '" + cmd + " force'");
            if (sdState_ == "RECORDING")
                StopRecording(now);
            const bool dfu = cmd != "reset";
            Out(dfu ? "DFU..." : "RESET...");
            if (dfu)
                inDfu_ = true;
            usbBackAt_ = now + (dfu ? 1000000000LL : 2500);
            ResetState(now + 2500);
            return;
        }
        Out("ERR unknown command '" + cmd + "' (try: help)");
    }

    bool CardReady()
    {
        if (sdState_ == "RECORDING")
        {
            Out("ERR busy recording");
            return false;
        }
        if (sdState_ == "NO_CARD")
        {
            Out("ERR no card");
            return false;
        }
        if (sdState_ == "ERROR")
        {
            Out("ERR card error (FatFs 1): set the REC switch to STOP");
            return false;
        }
        return true;
    }

    void Get(const std::string& arg, std::int64_t now)
    {
        if (arg == "abort")
        {
            const bool active = xfer_.mode != 0;
            xfer_ = {};
            return Out(active ? "OK aborted" : "OK nothing to abort");
        }
        xfer_ = {};
        std::string name = arg;
        long long offset = 0;
        const auto sp = arg.rfind(' ');
        if (sp != std::string::npos && ParseUInt(arg.substr(sp + 1), offset))
        {
            name = arg.substr(0, sp);
            while (!name.empty() && name.back() == ' ')
                name.pop_back();
        }
        else
            offset = 0;
        if (name.empty())
            return Out("ERR usage: get <name> [offset] | get abort");
        if (name.find_first_of("/\\:*?\"<>|") != std::string::npos)
            return Out("ERR only files in the card root");
        if (!CardReady())
            return;
        std::size_t idx = files_.size();
        for (std::size_t i = 0; i < files_.size(); i++)
            if (EqualNoCase(files_[i].name, name))
                idx = i;
        if (idx == files_.size())
            return Out("ERR no such file");
        const std::uint64_t size = files_[idx].data.size();
        if (static_cast<std::uint64_t>(offset) > size)
            return Out("ERR offset " + std::to_string(offset) + " > size " + std::to_string(size));
        xfer_.mode = 2;
        xfer_.file = idx;
        xfer_.pos = xfer_.start = static_cast<std::uint64_t>(offset);
        xfer_.crc = 0;
        xfer_.last = now;
        Out("G," + name + "," + std::to_string(size) + "," + std::to_string(offset));
        XferStep(now);
    }

    void XferStep(std::int64_t now)
    {
        if (xfer_.mode == 0)
            return;
        if (sdState_ != "READY")
        {
            const bool rec = sdState_ == "RECORDING";
            xfer_ = {};
            return Out(rec ? "ERR aborted: recording started" : "ERR aborted: card removed");
        }
        if (xfer_.mode == 1)
        {
            for (; xfer_.file < files_.size(); xfer_.file++)
            {
                const SimFile& f = files_[xfer_.file];
                const std::tm tm = LocalTm(f.mtime);
                char b[200];
                std::snprintf(b, sizeof(b), "F,%s,%llu,%04d-%02d-%02d %02d:%02d", f.name.c_str(),
                              static_cast<unsigned long long>(f.data.size()), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                              tm.tm_hour, tm.tm_min);
                Out(b);
            }
            Out("OK " + std::to_string(files_.size()));
            xfer_ = {};
            return;
        }
        // Передача: «скорость USB» xferBytesPerMs_
        xfer_.budget += static_cast<double>(now - xfer_.last) * xferBytesPerMs_;
        xfer_.last = now;
        const std::string& data = files_[xfer_.file].data;
        while (xfer_.pos < data.size() && xfer_.budget >= 1.0)
        {
            const std::size_t n = std::min<std::size_t>(48, data.size() - static_cast<std::size_t>(xfer_.pos));
            const auto* p = reinterpret_cast<const std::uint8_t*>(data.data() + xfer_.pos);
            xfer_.crc = codec::Crc32(xfer_.crc, p, n);
            Out("D," + codec::Base64Encode(p, n));
            xfer_.pos += n;
            xfer_.budget -= static_cast<double>(n);
        }
        if (xfer_.pos >= data.size())
        {
            char b[64];
            std::snprintf(b, sizeof(b), "E,%llu,%08x", static_cast<unsigned long long>(data.size() - xfer_.start), xfer_.crc);
            Out(b);
            Out("OK");
            xfer_ = {};
        }
    }

    void Set(const std::string& arg, std::int64_t now)
    {
        const auto sp = arg.find(' ');
        if (sp == std::string::npos)
            return SetUsage();
        const std::string name = arg.substr(0, sp);
        std::string val = arg.substr(sp + 1);
        while (!val.empty() && val.front() == ' ')
            val.erase(val.begin());
        long long n;
        double d;
        if (name == "time")
        {
            std::tm tm{};
            int Y, M, D, h, mi, s;
            char sep;
            if (val.size() != 19 || std::sscanf(val.c_str(), "%4d-%2d-%2d%c%2d:%2d:%2d", &Y, &M, &D, &sep, &h, &mi, &s) != 7 ||
                (sep != ' ' && sep != 't') || Y < 2000 || Y > 2099 || M < 1 || M > 12 || D < 1 || D > 31 || h > 23 ||
                mi > 59 || s > 59)
                return Out("ERR usage: set time YYYY-MM-DD HH:MM:SS (2000..2099, valid date)");
            tm.tm_year = Y - 1900;
            tm.tm_mon = M - 1;
            tm.tm_mday = D;
            tm.tm_hour = h;
            tm.tm_min = mi;
            tm.tm_sec = s;
            tm.tm_isdst = -1;
            const std::time_t t = std::mktime(&tm);
            clockBase_ = t - static_cast<std::time_t>(now / 1000);
            return Out("OK time=" + val.substr(0, 10) + " " + val.substr(11) + " (rtc yes)");
        }
        if (name == "freq")
        {
            if (!ParseUInt(val, n))
                return SetUsage();
            freq_ = static_cast<int>(std::clamp<long long>(n, 1, 50));
            return Out("OK freq=" + std::to_string(freq_) + " Hz (" + SaveNote(now) + ")");
        }
        if (name == "alpha")
        {
            if (!ParseDec(val, d))
                return SetUsage();
            alpha_ = std::clamp(d, 0.01, 0.99);
            return Out("OK alpha=" + Fx(alpha_, 2) + " (" + SaveNote(now) + ")");
        }
        if (name == "gap")
        {
            if (!ParseUInt(val, n))
                return SetUsage();
            gap_ = static_cast<int>(std::clamp<long long>(n, 2, 100));
            return Out("OK gap=" + std::to_string(gap_) + " ms (" + SaveNote(now) + ")");
        }
        if (name == "theme")
        {
            if (val != "dark" && val != "light")
                return SetUsage();
            themeLight_ = val == "light";
            return Out("OK theme=" + val + " (" + SaveNote(now) + ")");
        }
        if (name == "batalarm")
        {
            if (!ParseDec(val, d))
                return SetUsage();
            batAlarm_ = std::clamp(d, 5.0, 30.0);
            return Out("OK batalarm=" + Fx(batAlarm_, 2) + " V (" + SaveNote(now) + ")");
        }
        if (name == "rollwin")
        {
            if (!ParseUInt(val, n))
                return SetUsage();
            const int v = static_cast<int>(std::clamp<long long>(n, 10, 60));
            if (v != rollWin_)
                ResetRollWindows();
            rollWin_ = v;
            return Out("OK rollwin=" + std::to_string(rollWin_) + " s (" + SaveNote(now) + ")");
        }
        if (name == "rollhz")
        {
            if (!ParseUInt(val, n))
                return SetUsage();
            const int v = static_cast<int>(std::clamp<long long>(n, 2, 5));
            if (v != rollHz_)
                ResetRollWindows();
            rollHz_ = v;
            return Out("OK rollhz=" + std::to_string(rollHz_) + " Hz (" + SaveNote(now) + ")");
        }
        if (name == "rollcalm")
        {
            if (!ParseDec(val, d))
                return SetUsage();
            rollCalm_ = std::clamp(d, 0.10, 10.0);
            return Out("OK rollcalm=" + Fx(rollCalm_, 2) + " deg (" + SaveNote(now) + ")");
        }
        if (name == "rollhyst")
        {
            if (!ParseDec(val, d))
                return SetUsage();
            rollHyst_ = std::clamp(d, 0.0, 2.0);
            return Out("OK rollhyst=" + Fx(rollHyst_, 2) + " deg (" + SaveNote(now) + ")");
        }
        SetUsage();
    }

    void SetUsage()
    {
        Out("ERR usage: set freq N | set alpha X | set gap N | set theme dark|light | set batalarm X | set rollwin N | "
            "set rollhz N | set rollcalm X | set rollhyst X");
    }

    void Addr(const std::string& arg, std::int64_t now)
    {
        long long from = 0, to = 0;
        const auto sp = arg.find(' ');
        if (sp == std::string::npos || !ParseUInt(arg.substr(0, sp), from) ||
            !ParseUInt(arg.substr(arg.find_first_not_of(' ', sp)), to))
            return Out("ERR usage: addr OLD NEW (Modbus 1..247)");
        if (from < 1 || from > 247 || to < 1 || to > 247 || from == to)
            return Out("ERR addresses must be 1..247 and differ");
        if (sdState_ == "RECORDING")
            return Out("ERR busy recording");
        if (svc_ == "BUSY")
            return Out("ERR busy: address change in progress");
        for (int i = 0; i < kSensors; i++)
            if (kAddr[i] == to && sens_[i].ok)
            {
                svc_ = "FAIL";
                return Out("ERR addr " + std::to_string(from) + "->" + std::to_string(to) + " rejected: address " +
                           std::to_string(to) + " answers on the bus");
            }
        svc_ = "BUSY";
        svcFrom_ = static_cast<int>(from);
        svcTo_ = static_cast<int>(to);
        svcDoneAt_ = now + 1500;
        Out("OK addr " + std::to_string(from) + "->" + std::to_string(to) + " started (result: status \"svc\")");
    }

    void FinishSvc(std::int64_t now)
    {
        (void) now;
        // Датчик со старым адресом есть, если это 1 (новый с завода) или отвечающий Д2/Д3
        bool present = svcFrom_ == 1;
        for (int i = 0; i < kSensors; i++)
            if (kAddr[i] == svcFrom_ && sens_[i].ok)
            {
                present = true;
                ctl_.sensorOn[i] = false;
            }
        svc_ = present ? "OK" : "FAIL";
        if (present)
            for (int i = 0; i < kSensors; i++)
                if (kAddr[i] == svcTo_)
                    ctl_.sensorOn[i] = true;
    }

    void Diag(const std::string& arg, std::int64_t now)
    {
        if (arg == "reset")
        {
            for (auto& s : sens_)
            {
                s.to = s.crc = s.bad = 0;
                s.lat = s.done = Stat{};
            }
            busGap_ = busIdle_ = busCyc_ = Stat{};
            return Out("OK bus stats reset");
        }
        if (!arg.empty())
            return Out("ERR usage: diag [reset]");
        char b[256];
        Out("BWM427 diag");
        std::snprintf(b, sizeof(b), "uptime %lld s, tick %lld ms", static_cast<long long>((now - bootMs_) / 1000),
                      static_cast<long long>(now));
        Out(b);
        Out("loop 2400/s, max 5 ms (app 2 ms, ui 4 ms)");
        std::snprintf(b, sizeof(b), "cpu load=%d%% idle_pass=41.27 us min_pass=40.95 us (load = 1 - passes * idle_pass / 1 s)",
                      Cpu());
        Out(b);
        for (int i = 0; i < kSensors; i++)
        {
            const Sensor& s = sens_[i];
            std::snprintf(b, sizeof(b), "sensor D%d addr=%d %s x=%s y=%s ok=%llu err=%llu garbled=%llu last_err=%s since_ok=%s",
                          kAddr[i], kAddr[i], s.ok ? "OK" : s.everOk ? "LOST" : "ABSENT", Fx(s.filtX - s.offX, 3).c_str(),
                          Fx(s.filtY - s.offY, 3).c_str(), static_cast<unsigned long long>(s.okCount),
                          static_cast<unsigned long long>(s.errCount), static_cast<unsigned long long>(s.garbled),
                          s.lastErr ? "TIMEOUT" : "-",
                          s.okCount ? (std::to_string(now - s.lastOkMs) + " ms").c_str() : "never");
            Out(b);
            std::snprintf(b, sizeof(b), "  roll x=%s y=%s deg calm=%d,%d fill=%d/%d s", Fx(s.rollX, 3).c_str(),
                          Fx(s.rollY, 3).c_str(), s.calmX ? 1 : 0, s.calmY ? 1 : 0, s.fill, rollWin_);
            Out(b);
        }
        std::snprintf(b, sizeof(b), "rate actual=%s Hz, log_freq=%d Hz, ema_alpha=%s", Fx(Rate(), 1).c_str(), freq_,
                      Fx(alpha_, 2).c_str());
        Out(b);
        std::snprintf(b, sizeof(b), "sd %s err=0 file=%d rec_mask=0x%02X rows=%llu rec_time=%lld s", sdState_.c_str(),
                      nextNum_, recMask_, static_cast<unsigned long long>(recRows_),
                      static_cast<long long>(sdState_ == "RECORDING" ? (now - recStartMs_) / 1000 : 0));
        Out(b);
        std::snprintf(b, sizeof(b), "battery %s V, present=1 low=%d pct=%d%% alarm_below=%s V (hyst 0.30 V)",
                      Fx(batV_, 2).c_str(), batLow_ ? 1 : 0, 85, Fx(batAlarm_, 2).c_str());
        Out(b);
        Out("time " + ClockString(now) + ", rtc yes");
        Out(std::string("rec_switch ") + (ctl_.recSwitch ? "ON" : "OFF"));
        std::snprintf(b, sizeof(b), "bus gap %d ms", gap_);
        Out(b);
        for (int i = 0; i < kSensors; i++)
        {
            const Sensor& s = sens_[i];
            std::snprintf(b, sizeof(b), "reply D%d n_ok=%llu timeout=%llu crc=%llu bad_frame=%llu timeout_us=%d", kAddr[i],
                          static_cast<unsigned long long>(s.lat.n), static_cast<unsigned long long>(s.to),
                          static_cast<unsigned long long>(s.crc), static_cast<unsigned long long>(s.bad),
                          i == 0 ? 16781 : 20000);
            Out(b);
        }
        std::snprintf(b, sizeof(b), "bus cycle_us n=%llu last=%llu avg=%llu min=%llu max=%llu",
                      static_cast<unsigned long long>(busCyc_.n), static_cast<unsigned long long>(busCyc_.last),
                      static_cast<unsigned long long>(busCyc_.Avg()), static_cast<unsigned long long>(busCyc_.min),
                      static_cast<unsigned long long>(busCyc_.max));
        Out(b);
        Out("bus timer_fallbacks=0");
        Out(std::string("theme ") + (themeLight_ ? "light" : "dark"));
        Out("svc " + svc_);
        Out("OK");
    }

    // ---------------------------------------------------------------------------------------------------------------

    mutable std::mutex m_;
    std::function<void(const std::string&)> emit_;
    std::mt19937 rng_;
    Controls ctl_;

    std::string fwVersion_ = "1.4";
    std::int64_t bootMs_ = 0, simMs_ = 0, nextPoll_ = 0, nextRoll_ = 0;
    std::int64_t usbBackAt_ = 0;
    bool inDfu_ = false;
    std::time_t clockBase_ = 0;

    // Настройки
    int freq_ = 10, gap_ = 15, rollWin_ = 20, rollHz_ = 5;
    double alpha_ = 0.15, batAlarm_ = 10.0, rollCalm_ = 1.5, rollHyst_ = 0.2;
    bool themeLight_ = false;
    std::int64_t savePendingUntil_ = 0;

    Sensor sens_[kSensors];
    bool stable_ = false;
    double stabSpan_ = 0;
    int stabSensor_ = -1, stabAxis_ = 0, stabFill_ = 0;
    Stat busGap_, busIdle_, busCyc_;

    double batV_ = 12.1;
    bool batLow_ = false;

    std::string sdState_ = "READY";
    std::vector<SimFile> files_;
    int nextNum_ = 7;
    std::int64_t recStartMs_ = 0;
    std::time_t recStartWall_ = 0;
    std::uint64_t recRows_ = 0;
    int recMask_ = 0;
    std::string recData_[kSensors];

    std::string svc_ = "IDLE";
    int svcFrom_ = 0, svcTo_ = 0;
    std::int64_t svcDoneAt_ = 0;

    int streamMs_ = 0;
    std::int64_t streamLast_ = 0;
    Xfer xfer_;
    double xferBytesPerMs_ = 120.0; // ~120 КБ/с данных файла
};
