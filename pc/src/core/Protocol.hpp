#pragma once
// Протокол командной строки прибора (USB CDC, README раздел «Командная строка по USB»):
//  - ответ на команду — строки, последняя начинается с «OK» или «ERR» (boot/reset — «DFU...»/«RESET...»);
//  - поток stream N: шапка «# S,t_ms,...» и строки «S,...»; колонки берутся по именам из шапки (новые поля прибор
//    дописывает в конец), без шапки — шапка текущей прошивки (DefaultStreamHeader);
//  - status — одна строка JSON (поля — ParseStatus и pc/README.md);
//  - ver — «BWM427 inclinometer firmware v1.3, build Oct  7 2026 20:46:12» и «HAL …, UID …»;
//  - files — строки «F,<имя>,<байт>,<ГГГГ-ММ-ДД ЧЧ:ММ>», итог «OK <число>»;
//  - get — «G,<имя>,<размер>,<смещение>», «D,<base64>», «E,<байт>,<crc32>», «OK» (Download.hpp);
//  - samples on|off (с прошивки 1.5) — «OK samples on, dropped N»; пока включено, после каждого цикла опроса строка
//    «R,<адрес>,<n>,<t_ms>,<RawX>,<RawY>,<OffsetX>,<OffsetY>,<BatV>» на каждый свежий отсчёт (в любой момент, как S).
#include <charconv>
#include <limits>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Json.hpp"

namespace proto
{

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ---------------------------------------------------------------------------------------------------------------
// Строки
// ---------------------------------------------------------------------------------------------------------------

inline bool StartsWith(std::string_view s, std::string_view p)
{
    return s.substr(0, p.size()) == p;
}

// Слово w в начале строки, за ним конец строки или пробел.
inline bool WordAt(std::string_view s, std::string_view w)
{
    return StartsWith(s, w) && (s.size() == w.size() || s[w.size()] == ' ');
}

inline bool IsOkLine(std::string_view s)
{
    return WordAt(s, "OK") || s == "DFU..." || s == "RESET...";
}

inline bool IsErrLine(std::string_view s)
{
    return WordAt(s, "ERR");
}

// Последняя строка ответа на команду.
inline bool IsTerminator(std::string_view s)
{
    return IsOkLine(s) || IsErrLine(s);
}

inline bool IsStreamLine(std::string_view s)
{
    return StartsWith(s, "S,");
}

inline bool IsStreamHeader(std::string_view s)
{
    return StartsWith(s, "# S,");
}

inline std::vector<std::string_view> Split(std::string_view s, char sep)
{
    std::vector<std::string_view> out;
    std::size_t a = 0;
    for (;;)
    {
        const std::size_t b = s.find(sep, a);
        if (b == std::string_view::npos)
        {
            out.push_back(s.substr(a));
            return out;
        }
        out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
}

// Число из текста прибора: «12.345», «-0.5», «nan», «ovf» (переполнение fx()) -> NaN.
inline double ToDouble(std::string_view s)
{
    if (s.empty() || s == "nan" || s == "ovf" || s == "-ovf" || s == "-")
        return kNaN;
    double v = 0.0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size() ? v : kNaN;
}

inline bool ToU64(std::string_view s, std::uint64_t& v)
{
    if (s.empty())
        return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

inline std::int64_t ToInt(std::string_view s, std::int64_t def = -1)
{
    std::int64_t v = 0;
    if (s.empty())
        return def;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size() ? v : def;
}

// ---------------------------------------------------------------------------------------------------------------
// Поток stream
// ---------------------------------------------------------------------------------------------------------------

struct StreamHeader
{
    std::vector<std::string> cols; // без «S» в начале: t_ms, loop_ms, s2, …
    std::vector<int> addrs;        // адреса датчиков по колонкам sN, по порядку

    int Index(std::string_view name) const
    {
        for (std::size_t i = 0; i < cols.size(); i++)
            if (cols[i] == name)
                return static_cast<int>(i);
        return -1;
    }
    bool Valid() const { return !cols.empty(); }
};

// «# S,t_ms,loop_ms,s2,x2,…» -> шапка. false — не шапка потока.
inline bool ParseStreamHeader(std::string_view line, StreamHeader& h)
{
    if (!IsStreamHeader(line))
        return false;
    StreamHeader out;
    const auto parts = Split(line.substr(4), ',');
    for (auto p : parts)
    {
        while (!p.empty() && (p.back() == ' ' || p.back() == '\r'))
            p.remove_suffix(1);
        out.cols.emplace_back(p);
        if (p.size() >= 2 && p[0] == 's' && p.find_first_not_of("0123456789", 1) == std::string_view::npos)
            out.addrs.push_back(static_cast<int>(ToInt(p.substr(1), 0)));
    }
    if (out.cols.empty())
        return false;
    h = std::move(out);
    return true;
}

// Шапка прошивки 1.3 (README): если строка «#» потерялась, а строки S уже идут.
inline const StreamHeader& DefaultStreamHeader()
{
    static const StreamHeader h = [] {
        StreamHeader x;
        ParseStreamHeader("# S,t_ms,loop_ms,s2,x2,y2,err2,s3,x3,y3,err3,sd,rate_hz,file,gap_ms,cpu,bat_v,bat_low,"
                          "ok2,to2,bad2,lat2,lmin2,lavg2,lmax2,ok3,to3,bad3,lat3,lmin3,lavg3,lmax3,cyc_us,idle_us",
                          x);
        return x;
    }();
    return h;
}

struct StreamSensor
{
    int addr = 0;
    char st = '?';            // 'O' — отвечает, 'L' — нет связи, 'A' — не отвечал с включения
    double x = kNaN, y = kNaN; // градусы (после нуля)
    std::int64_t err = -1;    // ошибок опроса всего
    // Шина (с diag reset), -1 — нет в потоке
    std::int64_t ok = -1, to = -1, bad = -1, lat = -1, lmin = -1, lavg = -1, lmax = -1;
};

struct StreamSample
{
    std::int64_t tMs = -1;
    std::int64_t loopMs = -1;
    std::vector<StreamSensor> sens;
    std::string sd;          // NOCARD / READY / REC / ERR
    double rateHz = kNaN;
    std::int64_t file = -1, gapMs = -1, cpu = -1;
    double batV = kNaN;
    std::int64_t batLow = -1;
    std::int64_t cycUs = -1, idleUs = -1;

    const StreamSensor* Sensor(int addr) const
    {
        for (const auto& s : sens)
            if (s.addr == addr)
                return &s;
        return nullptr;
    }
};

// Строка «S,…» по шапке. Лишние колонки (новая прошивка) пропускаются; недостающие — по умолчанию (-1 / NaN).
inline bool ParseStreamLine(std::string_view line, const StreamHeader& h, StreamSample& out)
{
    if (!IsStreamLine(line) || !h.Valid())
        return false;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.remove_suffix(1);
    const auto v = Split(line.substr(2), ',');
    auto col = [&](std::string_view name) -> std::string_view {
        const int i = h.Index(name);
        return i >= 0 && static_cast<std::size_t>(i) < v.size() ? v[static_cast<std::size_t>(i)] : std::string_view{};
    };
    StreamSample s;
    s.tMs = ToInt(col("t_ms"));
    if (s.tMs < 0)
        return false;
    s.loopMs = ToInt(col("loop_ms"));
    for (int a : h.addrs)
    {
        StreamSensor d;
        d.addr = a;
        const std::string n = std::to_string(a);
        const auto st = col("s" + n);
        d.st = st.empty() ? '?' : st[0];
        d.x = ToDouble(col("x" + n));
        d.y = ToDouble(col("y" + n));
        d.err = ToInt(col("err" + n));
        d.ok = ToInt(col("ok" + n));
        d.to = ToInt(col("to" + n));
        d.bad = ToInt(col("bad" + n));
        d.lat = ToInt(col("lat" + n));
        d.lmin = ToInt(col("lmin" + n));
        d.lavg = ToInt(col("lavg" + n));
        d.lmax = ToInt(col("lmax" + n));
        s.sens.push_back(d);
    }
    s.sd = std::string(col("sd"));
    s.rateHz = ToDouble(col("rate_hz"));
    s.file = ToInt(col("file"));
    s.gapMs = ToInt(col("gap_ms"));
    s.cpu = ToInt(col("cpu"));
    s.batV = ToDouble(col("bat_v"));
    s.batLow = ToInt(col("bat_low"));
    s.cycUs = ToInt(col("cyc_us"));
    s.idleUs = ToInt(col("idle_us"));
    out = std::move(s);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// status (JSON)
// ---------------------------------------------------------------------------------------------------------------

// Статистика длительностей прибора: [n, last, min, avg, max], мкс.
struct Stat5
{
    std::uint64_t n = 0, last = 0, min = 0, avg = 0, max = 0;
};

struct SensorStatus
{
    int addr = 0;
    std::string st;                      // OK / LOST / ABSENT
    double x = kNaN, y = kNaN;           // угол после нуля, °
    double ox = 0.0, oy = 0.0;           // ноль, °
    double rollX = kNaN, rollY = kNaN;   // размах качки за окно, °
    bool calmX = false, calmY = false;   // покой по оси
    int fill = 0;                        // накоплено секунд окна качки
    std::uint64_t ok = 0, err = 0, garbled = 0;
    std::string lastErr;                 // «-», TIMEOUT, CRC, BAD_FRAME, EXCEPTION, UART
    std::optional<std::uint64_t> ageMs;  // с последнего удачного ответа (нет — не отвечал)
    std::uint64_t to = 0, crc = 0, bad = 0, tmoUs = 0;
    Stat5 lat, done;                     // до первого байта ответа / до конца ответа

    bool Ok() const { return st == "OK"; }
};

struct DeviceStatus
{
    int v = 0;
    std::string fw;
    std::uint64_t upS = 0, ms = 0;
    std::string time; // «ГГГГ-ММ-ДД ЧЧ:ММ:СС» по часам прибора
    bool rtc = false;
    // Настройки
    int freq = 0, gap = 0, rollWin = 0, rollHz = 0;
    double alpha = 0.0, batAlarm = 0.0, rollCalm = 0.0, rollHyst = 0.0;
    std::string theme;
    bool savePending = false;
    // Суперцикл
    double rate = 0.0;
    int cpu = 0;
    std::uint64_t loops = 0, loopMax = 0;
    std::vector<SensorStatus> sens;
    // Качка
    bool stable = false;
    double span = 0.0;
    std::optional<int> stabAddr;
    std::string stabAxis;
    int stabFill = 0;
    // Шина
    std::uint64_t fallbacks = 0;
    Stat5 busGap, busIdle, busCyc;
    double maxRate = 0.0;
    // Карта и запись
    std::string sd; // NO_CARD / READY / RECORDING / ERROR
    int sdErr = 0, file = 0, recMask = 0;
    std::vector<std::string> names;
    std::uint64_t rows = 0, recS = 0, sdTotalMb = 0;
    std::optional<std::uint64_t> sdFreeMb;
    // Питание
    double batV = 0.0;
    bool batPresent = false, batLow = false, batSat = false;
    std::optional<int> batPct;
    // Прочее
    bool recSw = false;
    std::string svc;  // IDLE / BUSY / OK / FAIL
    std::string xfer; // idle / list / get

    const SensorStatus* Sensor(int addr) const
    {
        for (const auto& s : sens)
            if (s.addr == addr)
                return &s;
        return nullptr;
    }
    bool Recording() const { return sd == "RECORDING"; }
};

inline Stat5 ToStat5(const json::Value& a)
{
    Stat5 s;
    if (a.IsArray() && a.Size() >= 5)
    {
        s.n = static_cast<std::uint64_t>(a[0].Num());
        s.last = static_cast<std::uint64_t>(a[1].Num());
        s.min = static_cast<std::uint64_t>(a[2].Num());
        s.avg = static_cast<std::uint64_t>(a[3].Num());
        s.max = static_cast<std::uint64_t>(a[4].Num());
    }
    return s;
}

inline double NumOrNaN(const json::Value& v)
{
    return v.IsNumber() ? v.n : kNaN;
}

// Строка JSON status -> структура. false — не JSON или нет обязательных полей.
inline bool ParseStatus(std::string_view line, DeviceStatus& out, std::string* err = nullptr)
{
    json::Value j;
    if (!json::Parse(line, j, err))
        return false;
    if (!j.IsObject() || !j["v"].IsNumber() || !j["sens"].IsArray())
    {
        if (err)
            *err = "нет полей v / sens";
        return false;
    }
    DeviceStatus s;
    s.v = static_cast<int>(j["v"].Int());
    s.fw = j["fw"].Str();
    s.upS = static_cast<std::uint64_t>(j["up"].Num());
    s.ms = static_cast<std::uint64_t>(j["ms"].Num());
    s.time = j["time"].Str();
    s.rtc = j["rtc"].Bool();
    s.freq = static_cast<int>(j["freq"].Int());
    s.alpha = j["alpha"].Num();
    s.gap = static_cast<int>(j["gap"].Int());
    s.theme = j["theme"].Str();
    s.batAlarm = j["batalarm"].Num();
    s.rollWin = static_cast<int>(j["rollwin"].Int());
    s.rollHz = static_cast<int>(j["rollhz"].Int());
    s.rollCalm = j["rollcalm"].Num();
    s.rollHyst = j["rollhyst"].Num();
    s.savePending = j["save_pending"].Bool();
    s.rate = j["rate"].Num();
    s.cpu = static_cast<int>(j["cpu"].Int());
    s.loops = static_cast<std::uint64_t>(j["loops"].Num());
    s.loopMax = static_cast<std::uint64_t>(j["loop_max"].Num());
    for (const auto& e : j["sens"].arr)
    {
        SensorStatus d;
        d.addr = static_cast<int>(e["addr"].Int());
        d.st = e["st"].Str();
        d.x = NumOrNaN(e["x"]);
        d.y = NumOrNaN(e["y"]);
        d.ox = e["ox"].Num();
        d.oy = e["oy"].Num();
        d.rollX = NumOrNaN(e["roll_x"]);
        d.rollY = NumOrNaN(e["roll_y"]);
        d.calmX = e["calm_x"].Bool();
        d.calmY = e["calm_y"].Bool();
        d.fill = static_cast<int>(e["fill"].Int());
        d.ok = static_cast<std::uint64_t>(e["ok"].Num());
        d.err = static_cast<std::uint64_t>(e["err"].Num());
        d.garbled = static_cast<std::uint64_t>(e["garbled"].Num());
        d.lastErr = e["last_err"].Str("-");
        if (e["age"].IsNumber())
            d.ageMs = static_cast<std::uint64_t>(e["age"].n);
        d.to = static_cast<std::uint64_t>(e["to"].Num());
        d.crc = static_cast<std::uint64_t>(e["crc"].Num());
        d.bad = static_cast<std::uint64_t>(e["bad"].Num());
        d.tmoUs = static_cast<std::uint64_t>(e["tmo_us"].Num());
        d.lat = ToStat5(e["lat"]);
        d.done = ToStat5(e["done"]);
        s.sens.push_back(std::move(d));
    }
    s.stable = j["stable"].Bool();
    s.span = j["span"].Num();
    if (j["stab_addr"].IsNumber())
        s.stabAddr = static_cast<int>(j["stab_addr"].n);
    s.stabAxis = j["stab_axis"].Str();
    s.stabFill = static_cast<int>(j["stab_fill"].Int());
    const auto& bus = j["bus"];
    s.fallbacks = static_cast<std::uint64_t>(bus["fallbacks"].Num());
    s.busGap = ToStat5(bus["gap"]);
    s.busIdle = ToStat5(bus["idle"]);
    s.busCyc = ToStat5(bus["cyc"]);
    s.maxRate = bus["max_rate"].Num();
    s.sd = j["sd"].Str();
    s.sdErr = static_cast<int>(j["sd_err"].Int());
    s.file = static_cast<int>(j["file"].Int());
    for (const auto& n : j["names"].arr)
        s.names.push_back(n.Str());
    s.recMask = static_cast<int>(j["rec_mask"].Int());
    s.rows = static_cast<std::uint64_t>(j["rows"].Num());
    s.recS = static_cast<std::uint64_t>(j["rec_s"].Num());
    s.sdTotalMb = static_cast<std::uint64_t>(j["sd_total"].Num());
    if (j["sd_free"].IsNumber())
        s.sdFreeMb = static_cast<std::uint64_t>(j["sd_free"].n);
    s.batV = j["bat_v"].Num();
    s.batPresent = j["bat_present"].Bool();
    s.batLow = j["bat_low"].Bool();
    if (j["bat_pct"].IsNumber())
        s.batPct = static_cast<int>(j["bat_pct"].n);
    s.batSat = j["bat_sat"].Bool();
    s.recSw = j["rec_sw"].Bool();
    s.svc = j["svc"].Str();
    s.xfer = j["xfer"].Str();
    out = std::move(s);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// ver
// ---------------------------------------------------------------------------------------------------------------

struct VersionInfo
{
    bool ours = false;    // ответ прибора BWM427
    std::string version;  // «1.3» (старые прошивки без номера — "")
    std::string build;    // «Oct  7 2026 20:46:12»
    std::string details;  // вторая строка: HAL, частота, UID
};

// Строки ответа ver (без OK).
inline VersionInfo ParseVer(const std::vector<std::string>& lines)
{
    VersionInfo v;
    for (const auto& l : lines)
    {
        if (StartsWith(l, "BWM427"))
        {
            v.ours = true;
            const auto pv = l.find(" v");
            if (pv != std::string::npos)
            {
                const auto end = l.find_first_of(", ", pv + 2);
                v.version = l.substr(pv + 2, end == std::string::npos ? std::string::npos : end - pv - 2);
            }
            const auto pb = l.find("build ");
            if (pb != std::string::npos)
                v.build = l.substr(pb + 6);
        }
        else if (StartsWith(l, "HAL"))
            v.details = l;
    }
    return v;
}

// Версия прошивки не ниже major.minor: «1.5», «1.5.2», «2.0» >= 1.5; «1.4.1», «» (старые без номера) — нет.
// Номер не разобран («dev») — std::nullopt (неизвестно).
inline std::optional<bool> VersionAtLeast(std::string_view v, int major, int minor)
{
    if (v.empty())
        return false;
    const auto parts = Split(v, '.');
    const std::int64_t ma = ToInt(parts[0]);
    const std::int64_t mi = parts.size() > 1 ? ToInt(parts[1]) : 0;
    if (ma < 0 || mi < 0)
        return std::nullopt;
    return ma != major ? ma > major : mi >= minor;
}

// ---------------------------------------------------------------------------------------------------------------
// samples: отсчёты датчиков для записи на ПК (прошивка 1.5)
// ---------------------------------------------------------------------------------------------------------------

inline constexpr int kSamplesMajor = 1, kSamplesMinor = 5; // первая прошивка с командой samples

// «R,2,1234,567890,-46,-17,-458,0,118» — все числа целые, округлены прибором как в CSV на карте (sd_to_scaled).
struct RawSample
{
    int addr = 0;            // Modbus-адрес датчика
    std::uint32_t n = 0;     // номер ответа датчика (ok_count): +1 на отсчёт, по нему видны пропуски
    std::uint32_t tMs = 0;   // HAL_GetTick() ответа (переполняется через 49,7 сут)
    std::int32_t rawX = 0, rawY = 0; // ответ датчика, сотые градуса (регистр − 10000)
    std::int32_t offX = 0, offY = 0; // ноль, тысячные градуса
    std::int32_t batV = 0;           // АКБ, десятые вольта
};

inline bool IsSampleLine(std::string_view s)
{
    return StartsWith(s, "R,");
}

// Строка R -> отсчёт. Лишние поля в конце (новая прошивка) пропускаются; не число / вне диапазона — false.
inline bool ParseSampleLine(std::string_view line, RawSample& out)
{
    if (!IsSampleLine(line))
        return false;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.remove_suffix(1);
    const auto v = Split(line.substr(2), ',');
    if (v.size() < 8)
        return false;
    std::int64_t x[8];
    for (int i = 0; i < 8; i++)
    {
        const std::string_view f = v[static_cast<std::size_t>(i)];
        const auto r = std::from_chars(f.data(), f.data() + f.size(), x[i]);
        if (f.empty() || r.ec != std::errc{} || r.ptr != f.data() + f.size())
            return false;
    }
    constexpr std::int64_t kU32 = 0xFFFFFFFFLL, kI32 = 0x7FFFFFFFLL;
    if (x[0] < 1 || x[0] > 247 || x[1] < 0 || x[1] > kU32 || x[2] < 0 || x[2] > kU32)
        return false;
    for (int i = 3; i < 8; i++)
        if (x[i] < -kI32 || x[i] > kI32)
            return false;
    RawSample s;
    s.addr = static_cast<int>(x[0]);
    s.n = static_cast<std::uint32_t>(x[1]);
    s.tMs = static_cast<std::uint32_t>(x[2]);
    s.rawX = static_cast<std::int32_t>(x[3]);
    s.rawY = static_cast<std::int32_t>(x[4]);
    s.offX = static_cast<std::int32_t>(x[5]);
    s.offY = static_cast<std::int32_t>(x[6]);
    s.batV = static_cast<std::int32_t>(x[7]);
    out = s;
    return true;
}

// Ответ «OK samples on, dropped 12» / «OK samples off, dropped 0». false — не такой ответ.
inline bool ParseSamplesReply(std::string_view line, bool& on, std::uint64_t& dropped)
{
    constexpr std::string_view p = "OK samples ";
    if (!StartsWith(line, p))
        return false;
    line.remove_prefix(p.size());
    if (StartsWith(line, "on"))
        on = true, line.remove_prefix(2);
    else if (StartsWith(line, "off"))
        on = false, line.remove_prefix(3);
    else
        return false;
    dropped = 0;
    constexpr std::string_view d = ", dropped ";
    if (StartsWith(line, d))
        ToU64(line.substr(d.size()), dropped);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------------------------------------------

struct FileEntry
{
    std::string name;
    std::uint64_t size = 0;
    std::string date;     // «ГГГГ-ММ-ДД ЧЧ:ММ» (время файла на карте)
    int measurement = -1; // номер замера из имени (MNNN)
    int sensorAddr = -1;  // адрес датчика из имени (Dk)
    std::string day;      // «ГГГГ-ММ-ДД» из имени
};

inline bool AllDigits(std::string_view s)
{
    return !s.empty() && s.find_first_not_of("0123456789") == std::string_view::npos;
}

// Номер замера и датчик из имени «ГГГГ-ММ-ДД_MNNN_Dk.CSV» или старого «M_NNN_k.CSV» (как sd_parse_name).
inline void ParseMeasurementName(FileEntry& f)
{
    std::string up;
    for (char c : f.name)
        up += static_cast<char>((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
    if (up.size() < 4 || up.substr(up.size() - 4) != ".CSV")
        return;
    const std::string base = up.substr(0, up.size() - 4);
    const auto parts = Split(base, '_');
    if (parts.size() == 3 && parts[0] == "M" && parts[1].size() == 3 && AllDigits(parts[1]) && parts[2].size() == 1 &&
        AllDigits(parts[2]))
    {
        f.measurement = static_cast<int>(ToInt(parts[1]));
        f.sensorAddr = static_cast<int>(ToInt(parts[2]));
        return;
    }
    if (parts.size() == 3 && parts[0].size() == 10 && parts[0][4] == '-' && parts[0][7] == '-' && parts[1].size() >= 4 &&
        parts[1].size() <= 5 && parts[1][0] == 'M' && AllDigits(parts[1].substr(1)) && parts[2].size() >= 2 &&
        parts[2][0] == 'D' && AllDigits(parts[2].substr(1)))
    {
        f.day = std::string(parts[0]);
        f.measurement = static_cast<int>(ToInt(parts[1].substr(1)));
        f.sensorAddr = static_cast<int>(ToInt(parts[2].substr(1)));
    }
}

// «F,<имя>,<байт>,<дата время>» — разбор с конца (в имени FAT могут быть запятые).
inline bool ParseFileLine(std::string_view line, FileEntry& f)
{
    if (!StartsWith(line, "F,"))
        return false;
    line.remove_prefix(2);
    const auto c2 = line.rfind(',');
    if (c2 == std::string_view::npos || c2 == 0)
        return false;
    const auto c1 = line.rfind(',', c2 - 1);
    if (c1 == std::string_view::npos || c1 == 0)
        return false;
    FileEntry e;
    e.name = std::string(line.substr(0, c1));
    if (!ToU64(line.substr(c1 + 1, c2 - c1 - 1), e.size))
        return false;
    e.date = std::string(line.substr(c2 + 1));
    ParseMeasurementName(e);
    f = std::move(e);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Пределы настроек (app.h, bwm427.h прошивки) и команды set
// ---------------------------------------------------------------------------------------------------------------

namespace limits
{
struct IntLimit
{
    int min, max, def;
};
struct RealLimit
{
    double min, max, def;
    int decimals;
};
inline constexpr IntLimit kFreq{1, 50, 10};          // APP_FREQ_MIN_HZ..MAX_HZ, Гц
inline constexpr RealLimit kAlpha{0.01, 0.99, 0.15, 2}; // APP_ALPHA_MIN..MAX
inline constexpr IntLimit kGap{2, 100, 15};          // BWM427_GAP_MS..BWM427_GAP_MAX_MS, мс
inline constexpr RealLimit kBatAlarm{5.0, 30.0, 10.0, 1}; // APP_BAT_ALARM_MIN_V..MAX_V, В
inline constexpr IntLimit kRollWin{10, 60, 20};      // APP_ROLL_WIN_MIN_S..MAX_S, с
inline constexpr IntLimit kRollHz{2, 5, 5};          // APP_ROLL_RATE_MIN_HZ..MAX_HZ
inline constexpr RealLimit kRollCalm{0.10, 10.0, 1.5, 2}; // APP_ROLL_CALM_MIN_DEG..MAX_DEG, °
inline constexpr RealLimit kRollHyst{0.0, 2.0, 0.2, 2};   // APP_ROLL_HYST_MIN_DEG..MAX_DEG, °
inline constexpr int kAddrMin = 1, kAddrMax = 247;  // Modbus
inline constexpr int kStreamMinMs = 50, kStreamMaxMs = 60000;
} // namespace limits

inline bool InRange(int v, const limits::IntLimit& l)
{
    return v >= l.min && v <= l.max;
}

inline bool InRange(double v, const limits::RealLimit& l)
{
    const double eps = 0.5 * std::pow(10.0, -l.decimals);
    return std::isfinite(v) && v >= l.min - eps && v <= l.max + eps;
}

// Число с точкой (не запятой — независимо от языка Windows) и заданным числом знаков.
inline std::string Fixed(double v, int decimals)
{
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, decimals);
    return std::string(buf, r.ptr);
}

} // namespace proto
