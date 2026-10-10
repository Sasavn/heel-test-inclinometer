#pragma once
// Чтение книги Excel (.xlsx) без библиотек — для «Обработки»: руководитель открывает CSV прибора в Excel и
// пересохраняет книгой. Книга -> текст CSV («;», числа с точкой) по каждому листу, дальше — обычный разбор CsvLog.hpp.
//  - ZIP: центральный каталог, части без сжатия и deflate (Deflate.hpp, Inflate); ZIP64 и шифрование — нет;
//  - листы — в порядке книги (xl/workbook.xml + xl/_rels/workbook.xml.rels);
//  - ячейки: числа, общие строки (xl/sharedStrings.xml, в том числе из нескольких кусков <r>), inlineStr, str, b;
//  - Excel превращает Date и Time в числа-даты: в столбцах с шапкой Date / Time серийные дни (1900, 25569 =
//    01.01.1970) и доли суток переводятся обратно в «ДД.ММ.ГГГГ» и «ЧЧ:ММ:СС»; строки остаются строками.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Deflate.hpp"

namespace xlsxread
{

inline bool IsZip(std::string_view d)
{
    return d.size() >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 3 && d[3] == 4;
}

namespace detail
{

inline std::uint32_t U16(std::string_view d, std::size_t p)
{
    return static_cast<unsigned char>(d[p]) | (static_cast<unsigned char>(d[p + 1]) << 8);
}

inline std::uint32_t U32(std::string_view d, std::size_t p)
{
    return U16(d, p) | (U16(d, p + 2) << 16);
}

// Часть ZIP по имени (без учёта регистра и ведущего «/»). false — нет или не распаковать.
inline bool Extract(std::string_view zip, std::string name, std::string& out)
{
    auto lower = [](std::string s) {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        while (!s.empty() && s.front() == '/')
            s.erase(s.begin());
        return s;
    };
    name = lower(name);
    // Конец центрального каталога — в последних 64 КБ + 22 байта
    if (zip.size() < 22)
        return false;
    std::size_t eocd = std::string_view::npos;
    const std::size_t stop = zip.size() > 65557 ? zip.size() - 65557 : 0;
    for (std::size_t p = zip.size() - 22 + 1; p-- > stop;)
        if (U32(zip, p) == 0x06054b50u)
        {
            eocd = p;
            break;
        }
    if (eocd == std::string_view::npos)
        return false;
    const std::uint32_t count = U16(zip, eocd + 10);
    std::size_t p = U32(zip, eocd + 16);
    for (std::uint32_t i = 0; i < count; i++)
    {
        if (p + 46 > zip.size() || U32(zip, p) != 0x02014b50u)
            return false;
        const std::uint32_t method = U16(zip, p + 10), csize = U32(zip, p + 20), usize = U32(zip, p + 24);
        const std::uint32_t nlen = U16(zip, p + 28), elen = U16(zip, p + 30), clen = U16(zip, p + 32);
        const std::uint32_t local = U32(zip, p + 42);
        if (p + 46 + nlen > zip.size())
            return false;
        const std::string entry = lower(std::string(zip.substr(p + 46, nlen)));
        p += 46 + nlen + elen + clen;
        if (entry != name)
            continue;
        if (local + 30 > zip.size() || U32(zip, local) != 0x04034b50u)
            return false;
        const std::size_t data = local + 30 + U16(zip, local + 26) + U16(zip, local + 28);
        if (data + csize > zip.size())
            return false;
        const auto* src = reinterpret_cast<const unsigned char*>(zip.data() + data);
        if (method == 0)
        {
            out.assign(zip.data() + data, csize);
            return true;
        }
        if (method == 8)
        {
            std::vector<unsigned char> buf;
            buf.reserve(usize);
            if (!deflate::Inflate(src, csize, buf, std::size_t(usize) + 1024))
                return false;
            out.assign(buf.begin(), buf.end());
            return true;
        }
        return false; // другие методы сжатия Excel не пишет
    }
    return false;
}

// &amp; &lt; &gt; &quot; &apos; &#N; &#xN; -> UTF-8
inline std::string Unescape(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); i++)
    {
        if (s[i] != '&')
        {
            out += s[i];
            continue;
        }
        const std::size_t e = s.find(';', i);
        if (e == std::string_view::npos || e - i > 10)
        {
            out += s[i];
            continue;
        }
        const std::string_view ent = s.substr(i + 1, e - i - 1);
        std::uint32_t c = 0;
        if (ent == "amp")
            c = '&';
        else if (ent == "lt")
            c = '<';
        else if (ent == "gt")
            c = '>';
        else if (ent == "quot")
            c = '"';
        else if (ent == "apos")
            c = '\'';
        else if (!ent.empty() && ent[0] == '#')
            c = static_cast<std::uint32_t>(ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                                               ? std::strtoul(std::string(ent.substr(2)).c_str(), nullptr, 16)
                                               : std::strtoul(std::string(ent.substr(1)).c_str(), nullptr, 10));
        if (!c)
        {
            out += s[i];
            continue;
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
        i = e;
    }
    return out;
}

// Значение атрибута name="…" в открывающем теге tag (без учёта префикса пространства имён у name: r:id).
inline std::string Attr(std::string_view tag, std::string_view name)
{
    for (std::size_t p = tag.find(name); p != std::string_view::npos; p = tag.find(name, p + 1))
    {
        const char before = p ? tag[p - 1] : ' ';
        if (before != ' ' && before != '\t' && before != '\n' && before != '\r' && before != ':')
            continue;
        std::size_t q = p + name.size();
        while (q < tag.size() && tag[q] == ' ')
            q++;
        if (q >= tag.size() || tag[q] != '=')
            continue;
        q++;
        while (q < tag.size() && tag[q] == ' ')
            q++;
        if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\''))
            continue;
        const char quote = tag[q];
        const std::size_t e = tag.find(quote, q + 1);
        if (e == std::string_view::npos)
            return {};
        return Unescape(tag.substr(q + 1, e - q - 1));
    }
    return {};
}

// Следующий элемент <name …>…</name> или <name …/> с позиции pos: open — открывающий тег, body — содержимое.
// Имя сравнивается с учётом возможного префикса (x:row).
inline bool Next(std::string_view xml, std::string_view name, std::size_t& pos, std::string_view& open,
                 std::string_view& body)
{
    for (;;)
    {
        const std::size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos)
            return false;
        std::size_t q = lt + 1;
        std::size_t nameEnd = q;
        while (nameEnd < xml.size() && xml[nameEnd] != ' ' && xml[nameEnd] != '>' && xml[nameEnd] != '/' &&
               xml[nameEnd] != '\t' && xml[nameEnd] != '\r' && xml[nameEnd] != '\n')
            nameEnd++;
        std::string_view tag = xml.substr(q, nameEnd - q);
        const std::size_t colon = tag.find(':');
        const std::string_view local = colon == std::string_view::npos ? tag : tag.substr(colon + 1);
        const std::size_t gt = xml.find('>', nameEnd);
        if (gt == std::string_view::npos)
            return false;
        if (local != name)
        {
            pos = gt + 1;
            continue;
        }
        open = xml.substr(lt, gt - lt + 1);
        if (xml[gt - 1] == '/')
        {
            body = {};
            pos = gt + 1;
            return true;
        }
        const std::string close = "</" + std::string(tag) + ">";
        const std::size_t ce = xml.find(close, gt + 1);
        if (ce == std::string_view::npos)
            return false;
        body = xml.substr(gt + 1, ce - gt - 1);
        pos = ce + close.size();
        return true;
    }
}

// Весь текст <t>…</t> внутри (общая строка из кусков <r><t>…</t></r>, inlineStr).
inline std::string AllText(std::string_view body)
{
    std::string s;
    std::size_t p = 0;
    std::string_view open, t;
    while (Next(body, "t", p, open, t))
        s += Unescape(t);
    return s;
}

inline void CivilFromDays(std::int64_t z, int& y, int& m, int& d)
{
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2));
}

// Серийная дата Excel (система 1900) -> «ДД.ММ.ГГГГ»; доля суток -> «ЧЧ:ММ:СС».
inline std::string SerialDate(double v)
{
    int y = 0, m = 0, d = 0;
    CivilFromDays(static_cast<std::int64_t>(std::floor(v)) - 25569, y, m, d);
    char b[24];
    std::snprintf(b, sizeof(b), "%02d.%02d.%04d", d, m, y);
    return b;
}

inline std::string SerialClock(double v)
{
    long s = std::lround((v - std::floor(v)) * 86400.0);
    if (s >= 86400)
        s = 86399;
    char b[16];
    std::snprintf(b, sizeof(b), "%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return b;
}

inline bool SameNoCase(std::string_view a, std::string_view b)
{
    while (!a.empty() && (a.front() == ' ' || a.front() == '"'))
        a.remove_prefix(1);
    while (!a.empty() && (a.back() == ' ' || a.back() == '"'))
        a.remove_suffix(1);
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); i++)
    {
        char x = a[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (x != b[i])
            return false;
    }
    return true;
}

} // namespace detail

struct Sheet
{
    std::string name;
    std::string csv; // строки «;», числа с точкой
};

// Книга -> листы в порядке книги. false — не ZIP / не книга Excel (причина в err).
inline bool ToCsv(std::string_view zip, std::vector<Sheet>& out, std::string* err = nullptr)
{
    using namespace detail;
    out.clear();
    auto fail = [&](const char* why) {
        if (err)
            *err = why;
        return false;
    };
    std::string wb, rels, sst;
    if (!IsZip(zip) || !Extract(zip, "xl/workbook.xml", wb))
        return fail("не книга Excel (.xlsx) или книга повреждена");
    Extract(zip, "xl/_rels/workbook.xml.rels", rels);
    // Общие строки
    std::vector<std::string> shared;
    if (Extract(zip, "xl/sharedStrings.xml", sst))
    {
        std::size_t p = 0;
        std::string_view open, body;
        while (Next(sst, "si", p, open, body))
            shared.push_back(AllText(body));
    }
    // Листы: имя и путь части
    std::vector<std::pair<std::string, std::string>> sheets;
    {
        std::size_t p = 0;
        std::string_view open, body;
        while (Next(wb, "sheet", p, open, body))
        {
            const std::string name = Attr(open, "name"), rid = Attr(open, "id");
            std::string target;
            std::size_t q = 0;
            std::string_view ro, rb;
            while (Next(rels, "Relationship", q, ro, rb))
                if (Attr(ro, "Id") == rid)
                    target = Attr(ro, "Target");
            if (target.empty())
                target = "worksheets/sheet" + std::to_string(sheets.size() + 1) + ".xml";
            if (!target.empty() && target[0] == '/')
                target = target.substr(1);
            else
                target = "xl/" + target;
            sheets.push_back({name, target});
        }
    }
    for (const auto& [name, part] : sheets)
    {
        std::string xml;
        if (!Extract(zip, part, xml))
            continue;
        Sheet sh;
        sh.name = name;
        int dateCol = -1, timeCol = -1;
        bool headerSeen = false;
        std::size_t p = 0;
        std::string_view rowOpen, rowBody;
        std::vector<std::string> cells;
        std::vector<bool> numeric;
        while (Next(xml, "row", p, rowOpen, rowBody))
        {
            cells.clear();
            numeric.clear();
            std::size_t q = 0;
            std::string_view co, cb;
            int col = 0;
            while (Next(rowBody, "c", q, co, cb))
            {
                const std::string ref = Attr(co, "r");
                if (!ref.empty())
                {
                    int c = 0;
                    for (const char ch : ref)
                        if (ch >= 'A' && ch <= 'Z')
                            c = c * 26 + (ch - 'A' + 1);
                        else
                            break;
                    if (c > 0)
                        col = c - 1;
                }
                if (col > 1000)
                    break;
                const std::string type = Attr(co, "t");
                std::string v;
                bool num = false;
                std::size_t r = 0;
                std::string_view vo, vb;
                if (type == "inlineStr")
                    v = AllText(cb);
                else if (Next(cb, "v", r, vo, vb))
                {
                    v = Unescape(vb);
                    if (type == "s")
                    {
                        const auto i = static_cast<std::size_t>(std::strtoul(v.c_str(), nullptr, 10));
                        v = i < shared.size() ? shared[i] : std::string();
                    }
                    else if (type.empty() || type == "n")
                        num = true;
                }
                if (static_cast<int>(cells.size()) <= col)
                {
                    cells.resize(static_cast<std::size_t>(col) + 1);
                    numeric.resize(static_cast<std::size_t>(col) + 1, false);
                }
                cells[static_cast<std::size_t>(col)] = v;
                numeric[static_cast<std::size_t>(col)] = num;
                col++;
            }
            // Шапка: столбцы Date и Time (их числа — даты Excel)
            if (!headerSeen)
                for (std::size_t i = 0; i < cells.size(); i++)
                {
                    if (SameNoCase(cells[i], "calcx") || SameNoCase(cells[i], "calcy") || SameNoCase(cells[i], "rawx") ||
                        SameNoCase(cells[i], "rawy"))
                        headerSeen = true;
                    if (SameNoCase(cells[i], "date"))
                        dateCol = static_cast<int>(i);
                    if (SameNoCase(cells[i], "time"))
                        timeCol = static_cast<int>(i);
                }
            std::string line;
            for (std::size_t i = 0; i < cells.size(); i++)
            {
                std::string v = cells[i];
                for (auto& ch : v)
                    if (ch == ';' || ch == '\n' || ch == '\r')
                        ch = ' ';
                if (numeric[i] && !v.empty() && headerSeen)
                {
                    const double x = std::strtod(v.c_str(), nullptr);
                    if (static_cast<int>(i) == dateCol)
                        v = SerialDate(x);
                    else if (static_cast<int>(i) == timeCol)
                        v = x >= 1.0 ? SerialDate(x) + " " + SerialClock(x) : SerialClock(x);
                }
                line += (i ? ";" : "") + v;
            }
            sh.csv += line + "\n";
        }
        out.push_back(std::move(sh));
    }
    if (out.empty())
        return fail("в книге нет листов");
    return true;
}

} // namespace xlsxread
