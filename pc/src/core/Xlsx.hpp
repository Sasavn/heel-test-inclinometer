#pragma once
// Запись книги Excel (.xlsx) без библиотек: SpreadsheetML в ZIP (deflate — Deflate.hpp). Открывается в Excel 2007+
// и LibreOffice. Ячейки — текст или число с заданным числом знаков после запятой (формат ячейки "0.000": Excel
// показывает разделитель по настройкам Windows — в русской запятую). Строки — inline (без общей таблицы строк),
// первая строка листа может быть жирной шапкой с закреплением.
//
//   xlsx::Sheet s("Результаты");
//   s.widths = {28, 12, 14};
//   s.AddRow({xlsx::Text("Файл"), xlsx::Text("Крен, °"), xlsx::Text("h, м")});
//   s.AddRow({xlsx::Text("M001_D2.CSV"), xlsx::Num(2.451, 3), xlsx::Num(0.734, 3)});
//   std::string err;
//   xlsx::Write(L"Отчет.xlsx", {s}, &err);
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "Codec.hpp"
#include "Deflate.hpp"

namespace xlsx
{

struct Cell
{
    enum class Kind
    {
        Empty,
        Number,
        Text
    };
    Kind kind = Kind::Empty;
    double num = 0.0;
    int decimals = -1; // число: 0..3 знака после запятой; -1 — общий формат
    std::string text;  // UTF-8
};

inline Cell Text(std::string s)
{
    Cell c;
    c.kind = Cell::Kind::Text;
    c.text = std::move(s);
    return c;
}

inline Cell Num(double v, int decimals = -1)
{
    Cell c;
    if (!std::isfinite(v))
        return c; // NaN/inf — пустая ячейка
    c.kind = Cell::Kind::Number;
    c.num = v;
    c.decimals = decimals < -1 ? -1 : decimals > 3 ? 3 : decimals;
    return c;
}

inline Cell Empty() { return Cell{}; }

struct Sheet
{
    explicit Sheet(std::string n = "Лист1") : name(std::move(n)) {}
    std::string name;                     // до 31 знака, без []:*?/\ (лишнее заменяется)
    std::vector<std::vector<Cell>> rows;  // строки сверху вниз
    std::vector<double> widths;           // ширины столбцов в знаках (0 — по умолчанию)
    bool header = true;                   // первая строка — жирная шапка, закреплена при прокрутке

    void AddRow(std::initializer_list<Cell> cells) { rows.emplace_back(cells); }
    void AddRow(std::vector<Cell> cells) { rows.push_back(std::move(cells)); }
};

namespace detail
{

// «A», «B», … «Z», «AA», …
inline std::string ColName(std::size_t col)
{
    std::string s;
    col++;
    while (col)
    {
        col--;
        s.insert(s.begin(), static_cast<char>('A' + col % 26));
        col /= 26;
    }
    return s;
}

inline void AppendEscaped(std::string& out, std::string_view s)
{
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char>(ch);
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default:
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
                out += ' '; // управляющие символы XML 1.0 не допускает
            else
                out += ch;
        }
    }
}

// Число без локали (точка) и с 15 значащими цифрами — как хранит Excel
inline void AppendNumber(std::string& out, double v)
{
    char b[40];
    std::snprintf(b, sizeof(b), "%.15g", v);
    out += b;
}

inline std::string SafeSheetName(const std::string& name, std::size_t index)
{
    std::string s;
    for (const char ch : name)
        s += (ch == '[' || ch == ']' || ch == ':' || ch == '*' || ch == '?' || ch == '/' || ch == '\\') ? '_' : ch;
    // 31 знак Excel считает в UTF-16; кириллица — 2 байта UTF-8 на знак: режем по границе символа
    std::size_t chars = 0, cut = s.size();
    for (std::size_t i = 0; i < s.size(); i++)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && ++chars > 31)
        {
            cut = i;
            break;
        }
    s.resize(cut);
    if (s.empty())
        s = "Лист" + std::to_string(index + 1);
    return s;
}

// Стили: 0 — обычный, 1 — жирный, 2..5 — число с 0..3 знаками после запятой
inline int StyleOf(const Cell& c, bool bold)
{
    if (bold)
        return 1;
    if (c.kind == Cell::Kind::Number && c.decimals >= 0)
        return 2 + c.decimals;
    return 0;
}

inline std::string SheetXml(const Sheet& sh)
{
    std::string x;
    x.reserve(64 + sh.rows.size() * 96);
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
         "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
    if (sh.header && sh.rows.size() > 1)
        x += "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\""
             " state=\"frozen\"/></sheetView></sheetViews>";
    bool anyWidth = false;
    for (const double w : sh.widths)
        anyWidth |= w > 0;
    if (anyWidth)
    {
        x += "<cols>";
        for (std::size_t i = 0; i < sh.widths.size(); i++)
        {
            if (sh.widths[i] <= 0)
                continue;
            const std::string n = std::to_string(i + 1);
            x += "<col min=\"" + n + "\" max=\"" + n + "\" width=\"";
            AppendNumber(x, sh.widths[i]);
            x += "\" customWidth=\"1\"/>";
        }
        x += "</cols>";
    }
    x += "<sheetData>";
    for (std::size_t r = 0; r < sh.rows.size(); r++)
    {
        const std::string rn = std::to_string(r + 1);
        x += "<row r=\"" + rn + "\">";
        const auto& row = sh.rows[r];
        for (std::size_t c = 0; c < row.size(); c++)
        {
            const Cell& cell = row[c];
            if (cell.kind == Cell::Kind::Empty)
                continue;
            const int st = StyleOf(cell, sh.header && r == 0);
            x += "<c r=\"" + ColName(c) + rn + "\"";
            if (st)
                x += " s=\"" + std::to_string(st) + "\"";
            if (cell.kind == Cell::Kind::Number)
            {
                x += "><v>";
                AppendNumber(x, cell.num);
                x += "</v></c>";
            }
            else
            {
                x += " t=\"inlineStr\"><is><t xml:space=\"preserve\">";
                AppendEscaped(x, cell.text);
                x += "</t></is></c>";
            }
        }
        x += "</row>";
    }
    x += "</sheetData></worksheet>";
    return x;
}

// ZIP: локальные заголовки + центральный каталог (имена ASCII, файлы < 4 ГБ). Части больше 1 КБ сжимаются deflate
// (Deflate.hpp; лист сырых данных — в 5–8 раз меньше), остальные — без сжатия («stored»).
class ZipWriter
{
public:
    bool compress = true;

    void Add(const std::string& name, const std::string& data)
    {
        Entry e;
        e.name = name;
        e.offset = static_cast<std::uint32_t>(out_.size());
        e.size = static_cast<std::uint32_t>(data.size());
        e.crc = codec::Crc32(0, reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        std::vector<unsigned char> packed;
        if (compress && data.size() > 1024)
            packed = deflate::Compress(data);
        e.method = !packed.empty() && packed.size() < data.size() ? 8 : 0;
        e.csize = e.method ? static_cast<std::uint32_t>(packed.size()) : e.size;
        U32(0x04034b50);
        U16(20); // версия для распаковки 2.0
        U16(0);  // флаги
        U16(e.method); // 0 — stored, 8 — deflate
        U16(time_);
        U16(date_);
        U32(e.crc);
        U32(e.csize);
        U32(e.size);
        U16(static_cast<std::uint16_t>(name.size()));
        U16(0);
        out_ += name;
        if (e.method)
            out_.append(reinterpret_cast<const char*>(packed.data()), packed.size());
        else
            out_ += data;
        entries_.push_back(std::move(e));
    }

    std::string Finish()
    {
        const auto cdStart = static_cast<std::uint32_t>(out_.size());
        for (const Entry& e : entries_)
        {
            U32(0x02014b50);
            U16(20);
            U16(20);
            U16(0);
            U16(e.method);
            U16(time_);
            U16(date_);
            U32(e.crc);
            U32(e.csize);
            U32(e.size);
            U16(static_cast<std::uint16_t>(e.name.size()));
            U16(0);
            U16(0);
            U16(0);
            U16(0);
            U32(0);
            U32(e.offset);
            out_ += e.name;
        }
        const auto cdSize = static_cast<std::uint32_t>(out_.size()) - cdStart;
        U32(0x06054b50);
        U16(0);
        U16(0);
        U16(static_cast<std::uint16_t>(entries_.size()));
        U16(static_cast<std::uint16_t>(entries_.size()));
        U32(cdSize);
        U32(cdStart);
        U16(0);
        return std::move(out_);
    }

    void SetTime(std::time_t t)
    {
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        const int year = tm.tm_year + 1900 < 1980 ? 1980 : tm.tm_year + 1900;
        date_ = static_cast<std::uint16_t>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
        time_ = static_cast<std::uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    }

private:
    struct Entry
    {
        std::string name;
        std::uint32_t offset = 0, size = 0, csize = 0, crc = 0;
        std::uint16_t method = 0;
    };
    void U16(std::uint16_t v)
    {
        out_ += static_cast<char>(v & 0xFF);
        out_ += static_cast<char>(v >> 8);
    }
    void U32(std::uint32_t v)
    {
        U16(static_cast<std::uint16_t>(v & 0xFFFF));
        U16(static_cast<std::uint16_t>(v >> 16));
    }
    std::string out_;
    std::vector<Entry> entries_;
    std::uint16_t date_ = (46 << 9) | (1 << 5) | 1, time_ = 0; // 01.01.2026
};

} // namespace detail

// Книга целиком в памяти (байты файла .xlsx). when — время изменения файлов внутри ZIP; compress = false — части
// без сжатия (тесты читают XML прямо из байтов).
inline std::string Build(const std::vector<Sheet>& sheets, std::time_t when = std::time(nullptr), bool compress = true)
{
    detail::ZipWriter z;
    z.compress = compress;
    z.SetTime(when);
    const std::size_t n = sheets.empty() ? 1 : sheets.size();

    std::string ct = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                     "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                     "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                     "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                     "<Override PartName=\"/xl/workbook.xml\" "
                     "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                     "<Override PartName=\"/xl/styles.xml\" "
                     "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    for (std::size_t i = 0; i < n; i++)
        ct += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1) +
              ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    ct += "</Types>";
    z.Add("[Content_Types].xml", ct);

    z.Add("_rels/.rels",
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
          "<Relationship Id=\"rId1\" "
          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
          "Target=\"xl/workbook.xml\"/></Relationships>");

    std::string wb = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                     "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                     "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
    std::string rels = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    std::vector<std::string> used;
    for (std::size_t i = 0; i < n; i++)
    {
        std::string name = detail::SafeSheetName(sheets.empty() ? std::string() : sheets[i].name, i);
        for (int k = 2; std::find(used.begin(), used.end(), name) != used.end(); k++)
            name = detail::SafeSheetName(name.substr(0, name.size() > 26 ? 26 : name.size()) + " (" +
                                             std::to_string(k) + ")",
                                         i);
        used.push_back(name);
        const std::string id = std::to_string(i + 1);
        wb += "<sheet name=\"";
        detail::AppendEscaped(wb, name);
        wb += "\" sheetId=\"" + id + "\" r:id=\"rId" + id + "\"/>";
        rels += "<Relationship Id=\"rId" + id +
                "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                "Target=\"worksheets/sheet" +
                id + ".xml\"/>";
    }
    wb += "</sheets></workbook>";
    rels += "<Relationship Id=\"rId" + std::to_string(n + 1) +
            "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
            "Target=\"styles.xml\"/></Relationships>";
    z.Add("xl/workbook.xml", wb);
    z.Add("xl/_rels/workbook.xml.rels", rels);

    // 0 — обычный; 1 — жирный; 2..5 — 0, 0.0, 0.00, 0.000 (1 и 2 — встроенные форматы Excel)
    z.Add("xl/styles.xml",
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
          "<numFmts count=\"2\"><numFmt numFmtId=\"164\" formatCode=\"0.0\"/>"
          "<numFmt numFmtId=\"165\" formatCode=\"0.000\"/></numFmts>"
          "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
          "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
          "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
          "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
          "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
          "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
          "<cellXfs count=\"6\">"
          "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
          "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
          "<xf numFmtId=\"1\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
          "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
          "<xf numFmtId=\"2\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
          "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
          "</cellXfs>"
          "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
          "</styleSheet>");

    for (std::size_t i = 0; i < n; i++)
        z.Add("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml",
              sheets.empty() ? detail::SheetXml(Sheet()) : detail::SheetXml(sheets[i]));
    return z.Finish();
}

// Записать книгу в файл: через временный рядом и переименование (не оставляет половинку файла). false — ошибка
// (текст в err).
inline bool Write(const std::filesystem::path& file, const std::vector<Sheet>& sheets, std::string* err = nullptr)
{
    const std::string data = Build(sheets);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            if (err)
                *err = "не удалось создать файл";
            return false;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f)
        {
            if (err)
                *err = "ошибка записи (нет места?)";
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        if (err)
            *err = "файл занят (открыт в Excel?)";
        return false;
    }
    return true;
}

} // namespace xlsx
