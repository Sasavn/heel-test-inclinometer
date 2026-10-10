#pragma once
// Чтение файлов замеров (CSV с карты прибора) всех версий. Формат узнаётся по СОДЕРЖИМОМУ (шапке и строкам), не по
// имени файла; столбцы — по именам из шапки:
//  - прошивка 1.4+: «Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms», «;», десятичная запятая, Date
//    ДД.ММ.ГГГГ, Time ЧЧ:ММ:СС, Ms — миллисекунды от начала записи (точное время отсчёта);
//  - наша прошивка 1.0–1.3: «Time,RawX,RawY,OffsetX,OffsetY,CalcX,CalcY,BatV,Ms», «,», точка, Time «ДД.ММ.ГГ ЧЧ:ММ:СС»;
//  - исходная прошивка (Romero2207, май–июнь 2026, файлы M_NNN.CSV и M_NNN_k.CSV): то же без Ms;
//  - тестовые файлы старого анализатора (BWM427_Analyzer): «Time,CalcX,CalcY,BatV», Time «ЧЧ:ММ:СС».
// Терпимо к тому, что бывает с файлами после карты и Excel: BOM UTF-8, UTF-16 («Текст Юникод» из Excel), CRLF / LF / CR,
// пустые строки, нули в хвосте (карту выдернули), строка «sep=;», разделитель «;», «,» или табуляция, десятичная
// запятая или точка, поля в кавычках, единицы в шапке («CalcX (°)»), строки над шапкой, Time без секунд (Excel),
// дата ГГГГ-ММ-ДД. Без шапки — столбцы угадываются по первой строке (число полей и вид даты/времени).
// Недостающие столбцы — NaN (CalcX/Y без них — Raw − Offset). Время отсчёта t (с от начала записи): по Ms, а без Ms —
// по Time (строки одной секунды — равномерно внутри неё; у Time без секунд — по минуте), без Time — 10 строк в секунду.
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
    Fw14,     // столбцы Date и Time (прошивка 1.4+)
    Fw13,     // Time с датой, Raw/Offset, Ms (наша прошивка 1.0–1.3)
    FwOld,    // то же без Ms (исходная прошивка Romero2207)
    Analyzer, // Time,CalcX,CalcY,BatV (тестовые файлы старого анализатора)
};

inline const char* FormatName(Format f)
{
    switch (f)
    {
    case Format::Fw14: return "прошивка 1.4+";
    case Format::Fw13: return "прошивка 1.0–1.3";
    case Format::FwOld: return "исходная прошивка";
    case Format::Analyzer: return "старый анализатор";
    default: return "неизвестный";
    }
}

enum class TimeSource
{
    Ms,   // столбец Ms
    Time, // столбец Time (1 с или 1 мин), строки — равномерно
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
    char sep = 0;             // разделитель столбцов
    bool headerless = false;  // шапки нет — столбцы угаданы по первой строке
    bool utf16 = false;       // файл в UTF-16 (Excel «Текст Юникод»)
    bool decimalComma = false; // числа с десятичной запятой
    bool nulBytes = false;    // нули в файле (хвост кластера после выдёргивания карты)
    int skipped = 0;          // строк не разобрано
    std::string error;        // файл не прочитан (пусто — прочитан)
    std::string firstLine;    // первая непустая строка (для сообщения «не понял формат»), UTF-8, до 80 знаков

    double Duration() const { return rows.empty() ? 0.0 : rows.back().t - rows.front().t; }
};

// Особенности файла коротко: «;», запятая, без шапки, UTF-16 … (для журнала).
inline std::string Details(const Log& log)
{
    std::string s;
    auto add = [&](const char* x) { s += (s.empty() ? "" : ", ") + std::string(x); };
    if (log.sep == '\t')
        add("табуляция");
    else if (log.sep == ';')
        add("«;»");
    else if (log.sep == ',')
        add("«,»");
    if (log.decimalComma)
        add("десятичная запятая");
    if (log.headerless)
        add("без шапки — столбцы угаданы");
    if (log.utf16)
        add("UTF-16");
    if (log.nulBytes)
        add("нули в хвосте");
    return s;
}

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

// «ДД.ММ.ГГГГ» / «ДД.ММ.ГГ» / «ГГГГ-ММ-ДД» -> дни; -1 — не дата.
inline std::int32_t ParseDate(std::string_view s)
{
    int d = 0, m = 0, y = 0, n = 0;
    const std::string z(s);
    if (std::sscanf(z.c_str(), "%d.%d.%d%n", &d, &m, &y, &n) == 3 && n == static_cast<int>(z.size()))
    {
        if (y < 100)
            y += 2000;
    }
    else if (n = 0; std::sscanf(z.c_str(), "%d-%d-%d%n", &y, &m, &d, &n) == 3 && n == static_cast<int>(z.size()) &&
                    z.size() == 10)
    {
    }
    else
        return -1;
    if (d < 1 || d > 31 || m < 1 || m > 12 || y < 1970 || y > 2200)
        return -1;
    return DaysFromCivil(y, m, d);
}

// «ЧЧ:ММ:СС» (и «ЧЧ:ММ:СС.ммм», «ЧЧ:ММ» — Excel без секунд) -> секунды суток; -1 — не время.
inline std::int32_t ParseClock(std::string_view s)
{
    int h = 0, mi = 0, se = 0, n = 0;
    const std::string z(s);
    if (std::sscanf(z.c_str(), "%d:%d:%d%n", &h, &mi, &se, &n) == 3)
    {
        if (n != static_cast<int>(z.size()) && !(z[static_cast<std::size_t>(n)] == '.' || z[static_cast<std::size_t>(n)] == ','))
            return -1; // доли секунды отбрасываются
    }
    else if (n = 0, se = 0; std::sscanf(z.c_str(), "%d:%d%n", &h, &mi, &n) == 2 && n == static_cast<int>(z.size()))
    {
    }
    else
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

// Поля строки: разделитель sep, поля в кавычках ("1,25") — целиком.
inline void SplitInto(std::string_view s, char sep, std::vector<std::string_view>& out)
{
    out.clear();
    std::size_t a = 0;
    bool quoted = false;
    for (std::size_t i = 0; i <= s.size(); i++)
    {
        if (i < s.size() && s[i] == '"')
            quoted = !quoted;
        if (i == s.size() || (s[i] == sep && !quoted))
        {
            out.push_back(Trim(s.substr(a, i - a)));
            a = i + 1;
        }
    }
}

// UTF-16 (с BOM или без — по нулям в байтах) -> UTF-8. false — не UTF-16.
inline bool FromUtf16(std::string_view in, std::string& out)
{
    bool le = true, bom = false;
    if (in.size() >= 2 && static_cast<unsigned char>(in[0]) == 0xFF && static_cast<unsigned char>(in[1]) == 0xFE)
        bom = true;
    else if (in.size() >= 2 && static_cast<unsigned char>(in[0]) == 0xFE && static_cast<unsigned char>(in[1]) == 0xFF)
    {
        bom = true;
        le = false;
    }
    else
    {
        // Без BOM: в первых байтах ASCII-текста через один — нули
        const std::size_t m = std::min<std::size_t>(in.size() & ~std::size_t(1), 200);
        int zOdd = 0, zEven = 0;
        for (std::size_t i = 0; i < m; i++)
            (i % 2 ? zOdd : zEven) += in[i] == 0;
        if (m < 8 || std::max(zOdd, zEven) < static_cast<int>(m / 2) * 9 / 10 || std::min(zOdd, zEven) > 0)
            return false;
        le = zOdd > zEven;
    }
    out.clear();
    out.reserve(in.size() / 2);
    for (std::size_t i = bom ? 2 : 0; i + 1 < in.size(); i += 2)
    {
        const auto b0 = static_cast<unsigned char>(in[i]), b1 = static_cast<unsigned char>(in[i + 1]);
        std::uint32_t c = le ? (b0 | (b1 << 8)) : ((b0 << 8) | b1);
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < in.size())
        {
            const auto c0 = static_cast<unsigned char>(in[i + 2]), c1 = static_cast<unsigned char>(in[i + 3]);
            const std::uint32_t lo = le ? (c0 | (c1 << 8)) : ((c0 << 8) | c1);
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (c < 0x80)
            out += static_cast<char>(c);
        else if (c < 0x800)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return true;
}

// Строка для сообщения: управляющие знаки — пробел, не UTF-8 (cp1251 и т. п.) — «?», не длиннее 80 знаков.
inline std::string Printable(std::string_view s)
{
    std::string out;
    std::size_t chars = 0;
    for (std::size_t i = 0; i < s.size() && chars < 80;)
    {
        const auto c = static_cast<unsigned char>(s[i]);
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = len > 0 && i + static_cast<std::size_t>(len) <= s.size();
        for (int k = 1; ok && k < len; k++)
            ok = (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) == 0x80;
        if (!ok)
        {
            out += '?';
            i++;
        }
        else if (len == 1)
        {
            out += c < 0x20 || c == 0x7F ? ' ' : static_cast<char>(c);
            i++;
        }
        else
        {
            out.append(s.substr(i, static_cast<std::size_t>(len)));
            i += static_cast<std::size_t>(len);
        }
        chars++;
    }
    if (chars >= 80)
        out += "…";
    return out;
}

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

// Имя столбца шапки -> Col (-1 — чужой): без регистра, пробелов, «_», единиц в скобках.
inline int ColumnOf(std::string_view name)
{
    std::string k;
    for (const char c : name)
    {
        if (c == '(' || c == '[' || c == ',')
            break;
        if (c == ' ' || c == '_' || c == '"' || c == '\'' || c == '.')
            continue;
        k += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    struct Alias
    {
        const char* name;
        int col;
    };
    static constexpr Alias kAliases[] = {
        {"date", cDate},     {"time", cTime},      {"datetime", cTime}, {"rawx", cRawX},  {"rawy", cRawY},
        {"offsetx", cOffX},  {"offsety", cOffY},   {"offx", cOffX},     {"offy", cOffY},  {"calcx", cCalcX},
        {"calcy", cCalcY},   {"batv", cBat},       {"bat", cBat},       {"ms", cMs},      {"tms", cMs},
    };
    for (const Alias& a : kAliases)
        if (k == a.name)
            return a.col;
    return -1;
}

// Разделитель строки: табуляция, «;» или «,» — что есть (в этом порядке).
inline char GuessSep(std::string_view line)
{
    if (line.find('\t') != std::string_view::npos)
        return '\t';
    if (line.find(';') != std::string_view::npos)
        return ';';
    return ',';
}

} // namespace detail

// Текст файла -> строки. false — не файл замера (формат не понят); причина в log.error, первая строка — log.firstLine.
inline bool Parse(std::string_view input, Log& log)
{
    using namespace detail;
    log = Log{};
    std::string decoded;
    if (FromUtf16(input, decoded))
    {
        log.utf16 = true;
        input = decoded;
    }
    if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xEF && static_cast<unsigned char>(input[1]) == 0xBB &&
        static_cast<unsigned char>(input[2]) == 0xBF)
        input.remove_prefix(3); // BOM (файл сохранили из Excel)
    std::string clean;
    if (input.find('\0') != std::string_view::npos) // карту выдернули: хвост кластера — нули
    {
        log.nulBytes = true;
        clean.reserve(input.size());
        for (const char c : input)
            if (c != '\0')
                clean += c;
        input = clean;
    }

    // Строки (CRLF, LF или одиночный CR)
    std::vector<std::string_view> lines;
    for (std::size_t a = 0; a < input.size();)
    {
        std::size_t b = input.find_first_of("\r\n", a);
        if (b == std::string_view::npos)
            b = input.size();
        const std::string_view l = input.substr(a, b - a);
        if (!Trim(l).empty())
            lines.push_back(l);
        a = b + 1;
        if (b < input.size() && input[b] == '\r' && a < input.size() && input[a] == '\n')
            a++;
    }
    if (lines.empty())
    {
        log.error = "пустой файл";
        return false;
    }
    log.firstLine = Printable(Trim(lines[0]));

    // «sep=;» (Excel) — разделитель задан явно
    char sep = 0;
    std::size_t from = 0;
    if (lines[0].size() >= 5 && EqualNoCase(lines[0].substr(0, 4), "sep="))
    {
        sep = lines[0][4];
        from = 1;
    }

    // Шапка: первая из первых строк, где есть столбец угла (CalcX/CalcY или RawX/RawY)
    int idx[cCount];
    std::fill(idx, idx + cCount, -1);
    std::vector<std::string_view> f;
    std::size_t head = lines.size();
    for (std::size_t li = from; li < lines.size() && li < from + 20 && head == lines.size(); li++)
    {
        const char s = sep ? sep : GuessSep(lines[li]);
        SplitInto(lines[li], s, f);
        int found[cCount];
        std::fill(found, found + cCount, -1);
        for (std::size_t k = 0; k < f.size(); k++)
            if (const int c = ColumnOf(f[k]); c >= 0 && found[c] < 0)
                found[c] = static_cast<int>(k);
        if (found[cCalcX] >= 0 || found[cCalcY] >= 0 || found[cRawX] >= 0 || found[cRawY] >= 0)
        {
            head = li;
            sep = s;
            std::copy(found, found + cCount, idx);
        }
    }
    std::size_t data = head + 1;
    if (head == lines.size())
    {
        // Шапки нет: угадать столбцы по первой строке данных (число полей, дата/время в первых полях)
        if (from >= lines.size())
        {
            log.error = "не понял формат";
            return false;
        }
        const char s = sep ? sep : GuessSep(lines[from]);
        SplitInto(lines[from], s, f);
        const std::size_t k = f.size();
        auto dateTime = [](std::string_view v) {
            const std::size_t sp = v.rfind(' ');
            return sp != std::string_view::npos && ParseDate(Trim(v.substr(0, sp))) >= 0 && ParseClock(Trim(v.substr(sp + 1))) >= 0;
        };
        bool ok = true;
        for (std::size_t i = 1; i < k && ok; i++)
            ok = (i == 1 && k == 10) || std::isfinite(Number(f[i])) || f[i].empty();
        if (ok && k >= 8 && k <= 9 && dateTime(f[0]))
        {
            // Time,RawX,RawY,OffsetX,OffsetY,CalcX,CalcY,BatV[,Ms] (прошивки до 1.3)
            const int cols[] = {cTime, cRawX, cRawY, cOffX, cOffY, cCalcX, cCalcY, cBat, cMs};
            for (std::size_t i = 0; i < k; i++)
                idx[cols[i]] = static_cast<int>(i);
        }
        else if (ok && k == 10 && ParseDate(f[0]) >= 0 && ParseClock(f[1]) >= 0)
        {
            const int cols[] = {cDate, cTime, cRawX, cRawY, cOffX, cOffY, cCalcX, cCalcY, cBat, cMs};
            for (std::size_t i = 0; i < k; i++)
                idx[cols[i]] = static_cast<int>(i);
        }
        else if (ok && k == 4 && (ParseClock(f[0]) >= 0 || dateTime(f[0])))
        {
            idx[cTime] = 0;
            idx[cCalcX] = 1;
            idx[cCalcY] = 2;
            idx[cBat] = 3;
        }
        else
        {
            log.error = "не понял формат";
            return false;
        }
        sep = s;
        data = from;
        log.headerless = true;
    }
    log.sep = sep;
    log.hasRaw = idx[cRawX] >= 0 || idx[cRawY] >= 0;
    log.hasOffset = idx[cOffX] >= 0 || idx[cOffY] >= 0;
    log.hasCalc = idx[cCalcX] >= 0 || idx[cCalcY] >= 0;
    log.hasBat = idx[cBat] >= 0;
    log.hasMs = idx[cMs] >= 0;
    log.hasDate = idx[cDate] >= 0;
    log.hasTime = idx[cTime] >= 0;
    log.format = log.hasDate ? Format::Fw14 : !log.hasRaw ? Format::Analyzer : log.hasMs ? Format::Fw13 : Format::FwOld;

    int need = 0; // столбцов в строке — не меньше
    for (const int i : idx)
        need = std::max(need, i + 1);

    // Строки данных
    std::vector<double> ms;
    log.rows.reserve(lines.size() - std::min(lines.size(), data));
    for (std::size_t li = data; li < lines.size(); li++)
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
        if (!log.decimalComma)
            for (const int c : {cCalcX, cCalcY, cRawX, cRawY})
                if (idx[c] >= 0 && f[static_cast<std::size_t>(idx[c])].find(',') != std::string_view::npos)
                    log.decimalComma = true;
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
        bool clock = log.hasTime, days = true, minutes = true;
        for (const Row& r : log.rows)
        {
            clock = clock && r.sec >= 0;
            days = days && r.day >= 0; // дата у всех строк (у программных часов прошивки бывает «32.06.26»)
            minutes = minutes && r.sec % 60 == 0;
        }
        if (clock)
        {
            log.time = TimeSource::Time;
            // Абсолютные секунды (с датой, а без неё — через полночь вперёд)
            std::vector<double> abs(n);
            double add = 0.0;
            for (std::size_t i = 0; i < n; i++)
            {
                const Row& r = log.rows[i];
                const double s = r.sec + (days ? r.day * 86400.0 : 0.0);
                if (!days && i > 0 && s + add < abs[i - 1] - 43200.0)
                    add += 86400.0;
                abs[i] = s + add;
            }
            // Группы строк с одним и тем же Time
            std::vector<std::size_t> grp; // начала групп
            for (std::size_t i = 0; i < n; i++)
                if (i == 0 || abs[i] != abs[i - 1])
                    grp.push_back(i);
            grp.push_back(n);
            const std::size_t groups = grp.size() - 1;
            if (minutes && groups == 1 && n > 1)
            {
                // Time без секунд и всё в одной минуте: шаг не узнать — 10 строк в секунду
                log.time = TimeSource::Rows;
                for (std::size_t i = 0; i < n; i++)
                    log.rows[i].t = static_cast<double>(i) * 0.1;
            }
            else if (minutes && groups >= 2)
            {
                // Time без секунд (пересохранён в Excel): шаг — по типичному числу строк в минуте, первая (неполная)
                // минута — вплотную к следующей
                std::vector<std::size_t> sizes;
                for (std::size_t g = 1; g + 1 < groups; g++)
                    sizes.push_back(grp[g + 1] - grp[g]);
                if (sizes.empty())
                    sizes.push_back(std::max(grp[1] - grp[0], grp[2] - grp[1]));
                std::nth_element(sizes.begin(), sizes.begin() + static_cast<std::ptrdiff_t>(sizes.size() / 2), sizes.end());
                const double dt = 60.0 / static_cast<double>(std::max<std::size_t>(1, sizes[sizes.size() / 2]));
                for (std::size_t g = 0; g < groups; g++)
                    for (std::size_t k = grp[g]; k < grp[g + 1]; k++)
                        log.rows[k].t = g == 0 ? abs[grp[1]] - static_cast<double>(grp[1] - k) * dt
                                               : abs[k] + static_cast<double>(k - grp[g]) * dt;
                const double t0 = log.rows[0].t;
                for (auto& r : log.rows)
                    r.t -= t0;
            }
            else
            {
                // Строки одной секунды — равномерно внутри неё
                for (std::size_t g = 0; g < groups; g++)
                    for (std::size_t k = grp[g]; k < grp[g + 1]; k++)
                        log.rows[k].t = abs[k] - abs[0] +
                                        static_cast<double>(k - grp[g]) / static_cast<double>(grp[g + 1] - grp[g]);
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
