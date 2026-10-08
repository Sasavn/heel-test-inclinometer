#pragma once
// Чтение файлов замеров (CSV с карты прибора) всех версий — формат по шапке, столбцы по именам:
//  - прошивка 1.4+: «Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms», «;», десятичная запятая, Date
//    ДД.ММ.ГГГГ, Time ЧЧ:ММ:СС, Ms — миллисекунды от начала записи (точное время отсчёта);
//  - прошивка до 1.3: «Time,RawX,RawY,OffsetX,OffsetY,CalcX,CalcY,BatV[,Ms]», «,», точка, Time «ДД.ММ.ГГ ЧЧ:ММ:СС»
//    (в самых старых файлах нет Ms);
//  - тестовые файлы старого анализатора: «Time,CalcX,CalcY,BatV», Time «ЧЧ:ММ:СС».
// Недостающие столбцы — NaN (CalcX/Y без них — Raw − Offset). Время отсчёта t (с от начала записи): по Ms, а без Ms —
// по Time (строки одной секунды — равномерно внутри неё; у Time разрешение 1 с), без Time — 10 строк в секунду.
// Строки, которые не разобрать (оборванная последняя строка при выдёргивании карты), пропускаются и считаются.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace csvlog
{

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

enum class Format
{
    Unknown,
    Fw14,     // «;» и десятичная запятая (прошивка 1.4+)
    Fw13,     // «,» и точка, Time с датой (прошивка до 1.3)
    Analyzer, // Time,CalcX,CalcY,BatV (тестовые файлы старого анализатора)
};

inline const char* FormatName(Format f)
{
    switch (f)
    {
    case Format::Fw14: return "прошивка 1.4+";
    case Format::Fw13: return "прошивка до 1.3";
    case Format::Analyzer: return "старый анализатор";
    default: return "неизвестный";
    }
}

enum class TimeSource
{
    Ms,   // столбец Ms
    Time, // столбец Time (1 с), строки секунды — равномерно
    Rows, // нет времени: 0,1 с на строку
};

struct Row
{
    double t = kNaN; // с от начала записи
    double rawX = kNaN, rawY = kNaN, offX = kNaN, offY = kNaN, calcX = kNaN, calcY = kNaN, batV = kNaN;
    std::int32_t day = -1; // дата: дней от 01.01.1970, -1 — нет
    std::int32_t sec = -1; // время суток, с; -1 — нет

    double Calc(int axis) const { return axis == 0 ? calcX : calcY; }
};

struct Log
{
    Format format = Format::Unknown;
    TimeSource time = TimeSource::Rows;
    std::vector<Row> rows;
    bool hasRaw = false, hasOffset = false, hasCalc = false, hasBat = false, hasMs = false, hasDate = false,
         hasTime = false;
    int skipped = 0;    // строк не разобрано
    std::string error;  // файл не прочитан (пусто — прочитан)

    double Duration() const { return rows.empty() ? 0.0 : rows.back().t - rows.front().t; }
};

// ---------------------------------------------------------------------------------------------------------------
// Дата и время
// ---------------------------------------------------------------------------------------------------------------

// Дней от 01.01.1970 (алгоритм days_from_civil, H. Hinnant).
inline std::int32_t DaysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

inline void CivilFromDays(std::int32_t z, int& y, int& m, int& d)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = yoe + era * 400 + (m <= 2);
}

// «ДД.ММ.ГГГГ» / «ДД.ММ.ГГ» -> дни; -1 — не дата.
inline std::int32_t ParseDate(std::string_view s)
{
    int d = 0, m = 0, y = 0, n = 0;
    char tail = 0;
    const std::string z(s);
    if (std::sscanf(z.c_str(), "%d.%d.%d%n%c", &d, &m, &y, &n, &tail) < 3 || n != static_cast<int>(z.size()))
        return -1;
    if (y < 100)
        y += 2000;
    if (d < 1 || d > 31 || m < 1 || m > 12 || y < 1970 || y > 2200)
        return -1;
    return DaysFromCivil(y, m, d);
}

// «ЧЧ:ММ:СС» -> секунды суток; -1 — не время.
inline std::int32_t ParseClock(std::string_view s)
{
    int h = 0, mi = 0, se = 0, n = 0;
    const std::string z(s);
    if (std::sscanf(z.c_str(), "%d:%d:%d%n", &h, &mi, &se, &n) != 3 || n != static_cast<int>(z.size()))
        return -1;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60)
        return -1;
    return h * 3600 + mi * 60 + se;
}

inline std::string DateText(std::int32_t day)
{
    if (day < 0)
        return {};
    int y = 0, m = 0, d = 0;
    CivilFromDays(day, y, m, d);
    char b[16];
    std::snprintf(b, sizeof(b), "%02d.%02d.%04d", d, m, y);
    return b;
}

inline std::string ClockText(std::int32_t sec)
{
    if (sec < 0)
        return {};
    char b[16];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    return b;
}

// ---------------------------------------------------------------------------------------------------------------
// Разбор
// ---------------------------------------------------------------------------------------------------------------

namespace detail
{

inline std::string_view Trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '"'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

inline bool EqualNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

// Число с точкой или запятой; «-0,000» -> +0; не число -> NaN.
inline double Number(std::string_view s)
{
    s = Trim(s);
    if (s.empty() || s.size() > 40)
        return kNaN;
    char b[48];
    std::size_t n = 0;
    for (const char c : s)
        b[n++] = c == ',' ? '.' : c;
    const char* p = b;
    if (*p == '+')
        p++;
    double v = 0.0;
    const auto r = std::from_chars(p, b + n, v);
    if (r.ec != std::errc{} || r.ptr != b + n || !std::isfinite(v))
        return kNaN;
    return v + 0.0; // -0 -> +0
}

inline void SplitInto(std::string_view s, char sep, std::vector<std::string_view>& out)
{
    out.clear();
    std::size_t a = 0;
    for (;;)
    {
        const std::size_t b = s.find(sep, a);
        if (b == std::string_view::npos)
        {
            out.push_back(Trim(s.substr(a)));
            return;
        }
        out.push_back(Trim(s.substr(a, b - a)));
        a = b + 1;
    }
}

} // namespace detail

// Текст файла -> строки. false — не файл замера (нет шапки с углами); причина в log.error.
inline bool Parse(std::string_view text, Log& log)
{
    using namespace detail;
    log = Log{};
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3); // BOM (файл сохранили из Excel)

    // Строки
    std::vector<std::string_view> lines;
    for (std::size_t a = 0; a < text.size();)
    {
        std::size_t b = text.find('\n', a);
        if (b == std::string_view::npos)
            b = text.size();
        std::string_view l = text.substr(a, b - a);
        if (!l.empty() && l.back() == '\r')
            l.remove_suffix(1);
        if (!Trim(l).empty())
            lines.push_back(l);
        a = b + 1;
    }
    if (lines.empty())
    {
        log.error = "пустой файл";
        return false;
    }

    // Шапка: разделитель и столбцы
    const std::string_view head = lines[0];
    const char sep = head.find(';') != std::string_view::npos ? ';' : ',';
    std::vector<std::string_view> cols;
    SplitInto(head, sep, cols);
    enum Col
    {
        cDate,
        cTime,
        cRawX,
        cRawY,
        cOffX,
        cOffY,
        cCalcX,
        cCalcY,
        cBat,
        cMs,
        cCount
    };
    static constexpr const char* kNames[cCount] = {"Date",    "Time",    "RawX",  "RawY", "OffsetX",
                                                   "OffsetY", "CalcX",   "CalcY", "BatV", "Ms"};
    int idx[cCount];
    for (int c = 0; c < cCount; c++)
    {
        idx[c] = -1;
        for (std::size_t k = 0; k < cols.size(); k++)
            if (EqualNoCase(cols[k], kNames[c]))
            {
                idx[c] = static_cast<int>(k);
                break;
            }
    }
    log.hasRaw = idx[cRawX] >= 0 || idx[cRawY] >= 0;
    log.hasOffset = idx[cOffX] >= 0 || idx[cOffY] >= 0;
    log.hasCalc = idx[cCalcX] >= 0 || idx[cCalcY] >= 0;
    log.hasBat = idx[cBat] >= 0;
    log.hasMs = idx[cMs] >= 0;
    log.hasDate = idx[cDate] >= 0;
    log.hasTime = idx[cTime] >= 0;
    if (!log.hasCalc && !log.hasRaw)
    {
        log.error = "нет столбцов углов (CalcX/CalcY или RawX/RawY) в первой строке";
        return false;
    }
    log.format = sep == ';' ? Format::Fw14 : log.hasRaw ? Format::Fw13 : Format::Analyzer;

    int need = 0; // столбцов в строке — не меньше
    for (const int i : idx)
        need = std::max(need, i + 1);

    // Строки данных
    std::vector<std::string_view> f;
    std::vector<double> ms;
    log.rows.reserve(lines.size() - 1);
    for (std::size_t li = 1; li < lines.size(); li++)
    {
        SplitInto(lines[li], sep, f);
        if (static_cast<int>(f.size()) < need)
        {
            log.skipped++;
            continue;
        }
        auto num = [&](int c) { return idx[c] >= 0 ? Number(f[static_cast<std::size_t>(idx[c])]) : kNaN; };
        Row r;
        r.rawX = num(cRawX);
        r.rawY = num(cRawY);
        r.offX = num(cOffX);
        r.offY = num(cOffY);
        r.calcX = num(cCalcX);
        r.calcY = num(cCalcY);
        r.batV = num(cBat);
        if (idx[cCalcX] < 0 && std::isfinite(r.rawX))
            r.calcX = r.rawX - (std::isfinite(r.offX) ? r.offX : 0.0) + 0.0;
        if (idx[cCalcY] < 0 && std::isfinite(r.rawY))
            r.calcY = r.rawY - (std::isfinite(r.offY) ? r.offY : 0.0) + 0.0;
        if (!std::isfinite(r.calcX) && !std::isfinite(r.calcY))
        {
            log.skipped++;
            continue;
        }
        // Дата и время: отдельные столбцы (1.4+) или «ДД.ММ.ГГ ЧЧ:ММ:СС» в Time (до 1.3), или только «ЧЧ:ММ:СС»
        if (idx[cDate] >= 0)
            r.day = ParseDate(f[static_cast<std::size_t>(idx[cDate])]);
        if (idx[cTime] >= 0)
        {
            const std::string_view tv = f[static_cast<std::size_t>(idx[cTime])];
            const std::size_t sp = tv.rfind(' ');
            if (sp != std::string_view::npos)
            {
                r.day = ParseDate(Trim(tv.substr(0, sp)));
                r.sec = ParseClock(Trim(tv.substr(sp + 1)));
            }
            else
                r.sec = ParseClock(tv);
        }
        const double m = num(cMs);
        if (log.hasMs && !std::isfinite(m))
        {
            log.skipped++; // строка оборвана посреди Ms
            continue;
        }
        ms.push_back(m);
        log.rows.push_back(r);
    }
    if (log.rows.empty())
    {
        log.error = "нет строк с данными";
        return false;
    }

    // Время отсчётов
    const std::size_t n = log.rows.size();
    if (log.hasMs)
    {
        log.time = TimeSource::Ms;
        for (std::size_t i = 0; i < n; i++)
            log.rows[i].t = ms[i] / 1000.0;
    }
    else
    {
        bool clock = log.hasTime;
        for (const Row& r : log.rows)
            clock = clock && r.sec >= 0;
        if (clock)
        {
            log.time = TimeSource::Time;
            // Абсолютные секунды (с датой, а без неё — через полночь вперёд), строки одной секунды — равномерно
            std::vector<double> abs(n);
            double add = 0.0;
            for (std::size_t i = 0; i < n; i++)
            {
                const Row& r = log.rows[i];
                double s = r.sec + (r.day >= 0 ? r.day * 86400.0 : 0.0);
                if (r.day < 0 && i > 0 && s + add < abs[i - 1] - 43200.0)
                    add += 86400.0;
                abs[i] = s + add;
            }
            for (std::size_t i = 0; i < n;)
            {
                std::size_t j = i;
                while (j < n && abs[j] == abs[i])
                    j++;
                for (std::size_t k = i; k < j; k++)
                    log.rows[k].t = abs[k] - abs[0] + static_cast<double>(k - i) / static_cast<double>(j - i);
                i = j;
            }
        }
        else
        {
            log.time = TimeSource::Rows;
            for (std::size_t i = 0; i < n; i++)
                log.rows[i].t = static_cast<double>(i) * 0.1;
        }
    }
    return true;
}

inline bool ReadFile(const std::filesystem::path& path, Log& log)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        log = Log{};
        log.error = "не удалось открыть файл";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Parse(text, log);
}

} // namespace csvlog
