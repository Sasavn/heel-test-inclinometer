#pragma once
// Запись книги Excel (.xlsx) без библиотек: SpreadsheetML в ZIP (deflate — Deflate.hpp). Открывается в Excel 2007+
// и LibreOffice. Ячейки — текст или число с заданным числом знаков после запятой (формат ячейки "0.000": Excel
// показывает разделитель по настройкам Windows — в русской запятую) или своим форматом (Style::fmt). Строки — inline
// (без общей таблицы строк), первая строка листа может быть жирной шапкой с закреплением.
//
// Оформление (для печатного отчёта): у ячейки — Style (жирный, курсив, размер и цвет шрифта, заливка, рамка,
// перенос по словам, выравнивание, формат числа); у листа — объединённые ячейки, высоты строк, закрепление строк,
// сетка вкл/выкл, печать (A4, книжная/альбомная, «по ширине страницы», поля, повтор строк шапки — definedName
// _xlnm.Print_Titles) и рисунки PNG (xl/drawings, xl/media, связи; привязка oneCellAnchor к ячейке + размер).
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
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Codec.hpp"
#include "Deflate.hpp"

namespace xlsx
{

// Оформление ячейки. По умолчанию — как раньше (без рамок и заливки, шрифт 11).
struct Style
{
    bool bold = false, italic = false;
    int size = 0;          // кегль, пт; 0 — 11
    int color = 0;         // цвет шрифта: 0 — авто, 1 — зелёный, 2 — красный, 3 — серый, 4 — синий
    int fill = 0;          // заливка: 0 — нет, 1 — шапка (серо-голубая), 2 — выделение (жёлтая), 3 — светло-серая
    bool border = false;   // тонкая рамка со всех сторон
    bool wrap = false;     // перенос по словам
    int align = 0;         // по горизонтали: 0 — общее, 1 — влево, 2 — по центру, 3 — вправо
    int valign = 0;        // по вертикали: 0 — снизу (как в Excel), 1 — по центру, 2 — сверху
    std::string fmt;       // формат числа (код Excel, например 0.000"°"); пусто — по Cell::decimals
};

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
    Style st;
    bool styled = false; // задан st (пустая ячейка со стилем — тоже пишется: рамки таблиц, объединения)
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

// Та же ячейка с оформлением.
inline Cell With(Cell c, const Style& s)
{
    c.st = s;
    c.styled = true;
    return c;
}

// Рисунок PNG на листе: левый верхний угол — в ячейке (col, row, от 0) со сдвигом (dx, dy) в пикселях; размер на
// листе — width × height пикселей (96 dpi).
struct Image
{
    std::vector<unsigned char> png;
    int col = 0, row = 0;
    int dx = 0, dy = 0;
    int width = 0, height = 0;
    std::string name; // подпись (описание рисунка)
};

struct Merge
{
    int r0 = 0, c0 = 0, r1 = 0, c1 = 0; // включительно, от 0
};

struct Sheet
{
    explicit Sheet(std::string n = "Лист1") : name(std::move(n)) {}
    std::string name;                     // до 31 знака, без []:*?/\ (лишнее заменяется)
    std::vector<std::vector<Cell>> rows;  // строки сверху вниз
    std::vector<double> widths;           // ширины столбцов в знаках (0 — по умолчанию)
    bool header = true;                   // первая строка — жирная шапка, закреплена при прокрутке
    // Оформление и печать
    std::vector<Merge> merges;
    std::vector<double> heights;          // высоты строк, пт (0 — авто)
    int freezeRows = -1;                  // закрепить строк сверху; -1 — по header (1 или 0)
    bool gridLines = true;                // сетка на экране
    bool print = false;                   // параметры печати заданы (ниже)
    bool landscape = false;
    bool fitWidth = true;                 // по ширине страницы (высота — сколько получится)
    int printTitleRows = 0;               // повторять на каждой странице строки 1…N
    std::string printArea;                // область печати («A1:J1»; пусто — весь лист) — _xlnm.Print_Area
    bool footer = true;                   // колонтитул: имя листа и номер страницы
    double marginLR = 0.5, marginTB = 0.6; // поля, дюймы
    std::vector<Image> images;

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

inline std::string Escaped(std::string_view s)
{
    std::string o;
    AppendEscaped(o, s);
    return o;
}

// Число без локали (точка) и с 15 значащими цифрами — как хранит Excel
inline void AppendNumber(std::string& out, double v)
{
    char b[40];
    std::snprintf(b, sizeof(b), "%.15g", v);
    out += b;
}

inline std::string NumberText(double v)
{
    std::string s;
    AppendNumber(s, v);
    return s;
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

// Таблица стилей книги: шрифты, заливки, рамки, форматы чисел и их сочетания (cellXfs). Первые шесть сочетаний —
// как в прежних версиях: 0 — обычный, 1 — жирный, 2..5 — число с 0..3 знаками после запятой.
class Styles
{
public:
    Styles()
    {
        Font(false, false, 0, 0);
        Font(true, false, 0, 0);
        xf_.push_back({0, 0, 0, 0, false, 0, 0});
        xf_.push_back({0, 1, 0, 0, false, 0, 0});
        xf_.push_back({1, 0, 0, 0, false, 0, 0});
        xf_.push_back({NumFmt("0.0"), 0, 0, 0, false, 0, 0});
        xf_.push_back({2, 0, 0, 0, false, 0, 0});
        xf_.push_back({NumFmt("0.000"), 0, 0, 0, false, 0, 0});
    }

    // Номер стиля ячейки (bold — жирная шапка листа).
    int Of(const Cell& c, bool bold)
    {
        if (!c.styled)
        {
            if (bold)
                return 1;
            if (c.kind == Cell::Kind::Number && c.decimals >= 0)
                return 2 + c.decimals;
            return 0;
        }
        const Style& s = c.st;
        Xf x;
        x.font = Font(s.bold || bold, s.italic, s.size, s.color);
        x.fill = s.fill > 0 && s.fill <= 3 ? s.fill + 1 : 0; // 0 — нет, 1 — gray125 (обязательная), 2.. — свои
        x.border = s.border ? 1 : 0;
        x.wrap = s.wrap;
        x.align = s.align;
        x.valign = s.valign;
        if (!s.fmt.empty())
            x.numFmt = NumFmt(s.fmt);
        else if (c.kind == Cell::Kind::Number && c.decimals >= 0)
            x.numFmt = c.decimals == 0 ? 1 : c.decimals == 2 ? 2 : NumFmt(c.decimals == 1 ? "0.0" : "0.000");
        for (std::size_t i = 0; i < xf_.size(); i++)
            if (xf_[i] == x)
                return static_cast<int>(i);
        xf_.push_back(x);
        return static_cast<int>(xf_.size() - 1);
    }

    std::string Xml() const
    {
        std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
        x += "<numFmts count=\"" + std::to_string(fmts_.size()) + "\">";
        for (std::size_t i = 0; i < fmts_.size(); i++)
            x += "<numFmt numFmtId=\"" + std::to_string(164 + i) + "\" formatCode=\"" + Escaped(fmts_[i]) + "\"/>";
        x += "</numFmts>";
        static const char* colors[] = {nullptr, "FF1E7B34", "FFC0392B", "FF707070", "FF1F4E9A"};
        x += "<fonts count=\"" + std::to_string(fonts_.size()) + "\">";
        for (const FontKey& f : fonts_)
        {
            x += "<font>";
            if (f.bold)
                x += "<b/>";
            if (f.italic)
                x += "<i/>";
            x += "<sz val=\"" + std::to_string(f.size > 0 ? f.size : 11) + "\"/>";
            if (f.color > 0 && f.color <= 4)
                x += std::string("<color rgb=\"") + colors[f.color] + "\"/>";
            x += "<name val=\"Calibri\"/><family val=\"2\"/></font>";
        }
        x += "</fonts>";
        x += "<fills count=\"5\"><fill><patternFill patternType=\"none\"/></fill>"
             "<fill><patternFill patternType=\"gray125\"/></fill>"
             "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFDCE6F1\"/><bgColor indexed=\"64\"/></patternFill></fill>"
             "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFFFF2CC\"/><bgColor indexed=\"64\"/></patternFill></fill>"
             "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF2F2F2\"/><bgColor indexed=\"64\"/></patternFill></fill>"
             "</fills>";
        x += "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
             "<border><left style=\"thin\"><color rgb=\"FF8EA9C1\"/></left><right style=\"thin\"><color rgb=\"FF8EA9C1\"/></right>"
             "<top style=\"thin\"><color rgb=\"FF8EA9C1\"/></top><bottom style=\"thin\"><color rgb=\"FF8EA9C1\"/></bottom>"
             "<diagonal/></border></borders>";
        x += "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>";
        x += "<cellXfs count=\"" + std::to_string(xf_.size()) + "\">";
        static const char* h[] = {nullptr, "left", "center", "right"};
        static const char* v[] = {nullptr, "center", "top"};
        for (const Xf& f : xf_)
        {
            x += "<xf numFmtId=\"" + std::to_string(f.numFmt) + "\" fontId=\"" + std::to_string(f.font) + "\" fillId=\"" +
                 std::to_string(f.fill) + "\" borderId=\"" + std::to_string(f.border) + "\" xfId=\"0\"";
            if (f.numFmt)
                x += " applyNumberFormat=\"1\"";
            if (f.font)
                x += " applyFont=\"1\"";
            if (f.fill)
                x += " applyFill=\"1\"";
            if (f.border)
                x += " applyBorder=\"1\"";
            const bool al = f.wrap || f.align || f.valign;
            if (al)
            {
                x += " applyAlignment=\"1\"><alignment";
                if (f.align > 0 && f.align <= 3)
                    x += std::string(" horizontal=\"") + h[f.align] + "\"";
                if (f.valign > 0 && f.valign <= 2)
                    x += std::string(" vertical=\"") + v[f.valign] + "\"";
                if (f.wrap)
                    x += " wrapText=\"1\"";
                x += "/></xf>";
            }
            else
                x += "/>";
        }
        x += "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
             "</styleSheet>";
        return x;
    }

private:
    struct FontKey
    {
        bool bold, italic;
        int size, color;
        bool operator==(const FontKey&) const = default;
    };
    struct Xf
    {
        int numFmt = 0, font = 0, fill = 0, border = 0;
        bool wrap = false;
        int align = 0, valign = 0;
        bool operator==(const Xf&) const = default;
    };
    int Font(bool b, bool i, int size, int color)
    {
        const FontKey k{b, i, size == 11 ? 0 : size, color};
        for (std::size_t n = 0; n < fonts_.size(); n++)
            if (fonts_[n] == k)
                return static_cast<int>(n);
        fonts_.push_back(k);
        return static_cast<int>(fonts_.size() - 1);
    }
    int NumFmt(const std::string& code)
    {
        if (code == "0")
            return 1;
        if (code == "0.00")
            return 2;
        for (std::size_t i = 0; i < fmts_.size(); i++)
            if (fmts_[i] == code)
                return static_cast<int>(164 + i);
        fmts_.push_back(code);
        return static_cast<int>(164 + fmts_.size() - 1);
    }
    std::vector<FontKey> fonts_;
    std::vector<std::string> fmts_;
    std::vector<Xf> xf_;
};

// Лист. drawingRel — номер связи рисунков листа (0 — рисунков нет).
inline std::string SheetXml(const Sheet& sh, Styles& styles, bool drawing)
{
    std::string x;
    x.reserve(64 + sh.rows.size() * 96);
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
         "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
    if (sh.print && sh.fitWidth)
        x += "<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>";
    const int freeze = sh.freezeRows >= 0 ? sh.freezeRows : (sh.header && sh.rows.size() > 1 ? 1 : 0);
    x += "<sheetViews><sheetView workbookViewId=\"0\"";
    if (!sh.gridLines)
        x += " showGridLines=\"0\"";
    if (freeze > 0)
    {
        const std::string tl = "A" + std::to_string(freeze + 1);
        x += "><pane ySplit=\"" + std::to_string(freeze) + "\" topLeftCell=\"" + tl +
             "\" activePane=\"bottomLeft\" state=\"frozen\"/><selection pane=\"bottomLeft\" activeCell=\"" + tl +
             "\" sqref=\"" + tl + "\"/></sheetView></sheetViews>";
    }
    else
        x += "/></sheetViews>";
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
        x += "<row r=\"" + rn + "\"";
        if (r < sh.heights.size() && sh.heights[r] > 0)
        {
            x += " ht=\"";
            AppendNumber(x, sh.heights[r]);
            x += "\" customHeight=\"1\"";
        }
        x += ">";
        const auto& row = sh.rows[r];
        for (std::size_t c = 0; c < row.size(); c++)
        {
            const Cell& cell = row[c];
            if (cell.kind == Cell::Kind::Empty && !cell.styled)
                continue;
            const int st = styles.Of(cell, sh.header && r == 0);
            x += "<c r=\"" + ColName(c) + rn + "\"";
            if (st)
                x += " s=\"" + std::to_string(st) + "\"";
            if (cell.kind == Cell::Kind::Number)
            {
                x += "><v>";
                AppendNumber(x, cell.num);
                x += "</v></c>";
            }
            else if (cell.kind == Cell::Kind::Text)
            {
                x += " t=\"inlineStr\"><is><t xml:space=\"preserve\">";
                AppendEscaped(x, cell.text);
                x += "</t></is></c>";
            }
            else
                x += "/>";
        }
        x += "</row>";
    }
    x += "</sheetData>";
    if (!sh.merges.empty())
    {
        x += "<mergeCells count=\"" + std::to_string(sh.merges.size()) + "\">";
        for (const Merge& m : sh.merges)
            x += "<mergeCell ref=\"" + ColName(static_cast<std::size_t>(m.c0)) + std::to_string(m.r0 + 1) + ":" +
                 ColName(static_cast<std::size_t>(m.c1)) + std::to_string(m.r1 + 1) + "\"/>";
        x += "</mergeCells>";
    }
    if (sh.print)
    {
        x += "<printOptions horizontalCentered=\"1\"/>";
        x += "<pageMargins left=\"" + NumberText(sh.marginLR) + "\" right=\"" + NumberText(sh.marginLR) + "\" top=\"" +
             NumberText(sh.marginTB) + "\" bottom=\"" + NumberText(sh.marginTB) + "\" header=\"0.3\" footer=\"0.3\"/>";
        x += "<pageSetup paperSize=\"9\" orientation=\"";
        x += sh.landscape ? "landscape" : "portrait";
        x += "\"";
        if (sh.fitWidth)
            x += " fitToWidth=\"1\" fitToHeight=\"0\"";
        x += "/>";
        if (sh.footer)
            x += "<headerFooter><oddFooter>&amp;L&amp;8&amp;A&amp;R&amp;8&amp;P / &amp;N</oddFooter></headerFooter>";
    }
    if (drawing)
        x += "<drawing r:id=\"rId1\"/>";
    x += "</worksheet>";
    return x;
}

// Рисунки листа: oneCellAnchor (угол — в ячейке, размер — в EMU: 9525 на пиксель при 96 dpi).
inline std::string DrawingXml(const Sheet& sh)
{
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                    "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
                    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
    for (std::size_t i = 0; i < sh.images.size(); i++)
    {
        const Image& im = sh.images[i];
        const std::string id = std::to_string(i + 1);
        auto emu = [](long long px) { return std::to_string(px * 9525LL); };
        x += "<xdr:oneCellAnchor><xdr:from><xdr:col>" + std::to_string(im.col) + "</xdr:col><xdr:colOff>" + emu(im.dx) +
             "</xdr:colOff><xdr:row>" + std::to_string(im.row) + "</xdr:row><xdr:rowOff>" + emu(im.dy) +
             "</xdr:rowOff></xdr:from><xdr:ext cx=\"" + emu(im.width) + "\" cy=\"" + emu(im.height) + "\"/>";
        x += "<xdr:pic><xdr:nvPicPr><xdr:cNvPr id=\"" + std::to_string(i + 2) + "\" name=\"Рисунок " + id + "\" descr=\"" +
             Escaped(im.name) + "\"/><xdr:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></xdr:cNvPicPr></xdr:nvPicPr>";
        x += "<xdr:blipFill><a:blip r:embed=\"rId" + id + "\"/><a:stretch><a:fillRect/></a:stretch></xdr:blipFill>";
        x += "<xdr:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + emu(im.width) + "\" cy=\"" + emu(im.height) +
             "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></xdr:spPr></xdr:pic><xdr:clientData/>"
             "</xdr:oneCellAnchor>";
    }
    x += "</xdr:wsDr>";
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
inline std::string Build(const std::vector<Sheet>& sheetsIn, std::time_t when = std::time(nullptr), bool compress = true)
{
    detail::ZipWriter z;
    z.compress = compress;
    z.SetTime(when);
    const std::vector<Sheet> empty(1);
    const std::vector<Sheet>& sheets = sheetsIn.empty() ? empty : sheetsIn;
    const std::size_t n = sheets.size();

    bool anyImage = false;
    for (const Sheet& s : sheets)
        anyImage |= !s.images.empty();
    std::string ct = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                     "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                     "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                     "<Default Extension=\"xml\" ContentType=\"application/xml\"/>";
    if (anyImage)
        ct += "<Default Extension=\"png\" ContentType=\"image/png\"/>";
    ct += "<Override PartName=\"/xl/workbook.xml\" "
          "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
          "<Override PartName=\"/xl/styles.xml\" "
          "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    for (std::size_t i = 0; i < n; i++)
    {
        ct += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1) +
              ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
        if (!sheets[i].images.empty())
            ct += "<Override PartName=\"/xl/drawings/drawing" + std::to_string(i + 1) +
                  ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>";
    }
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
    std::string names; // definedNames: повтор строк шапки при печати
    std::vector<std::string> used;
    for (std::size_t i = 0; i < n; i++)
    {
        std::string name = detail::SafeSheetName(sheets[i].name, i);
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
        std::string esc; // имя листа в ссылке: в апострофах, апостроф — удвоенный
        for (const char c : name)
            esc += c == '\'' ? std::string("''") : std::string(1, c);
        if (sheets[i].print && !sheets[i].printArea.empty())
        {
            // «A1:J1» -> «$A$1:$J$1»
            std::string ref;
            for (std::size_t k = 0; k < sheets[i].printArea.size(); k++)
            {
                const char c = sheets[i].printArea[k];
                const bool letter = c >= 'A' && c <= 'Z';
                const bool prevLetter = k > 0 && sheets[i].printArea[k - 1] >= 'A' && sheets[i].printArea[k - 1] <= 'Z';
                if ((letter && !prevLetter) || (c >= '0' && c <= '9' && prevLetter))
                    ref += '$';
                ref += c;
            }
            names += "<definedName name=\"_xlnm.Print_Area\" localSheetId=\"" + std::to_string(i) + "\">";
            detail::AppendEscaped(names, "'" + esc + "'!" + ref);
            names += "</definedName>";
        }
        if (sheets[i].print && sheets[i].printTitleRows > 0)
        {
            names += "<definedName name=\"_xlnm.Print_Titles\" localSheetId=\"" + std::to_string(i) + "\">";
            detail::AppendEscaped(names, "'" + esc + "'!$1:$" + std::to_string(sheets[i].printTitleRows));
            names += "</definedName>";
        }
    }
    wb += "</sheets>";
    if (!names.empty())
        wb += "<definedNames>" + names + "</definedNames>";
    wb += "</workbook>";
    rels += "<Relationship Id=\"rId" + std::to_string(n + 1) +
            "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
            "Target=\"styles.xml\"/></Relationships>";
    z.Add("xl/workbook.xml", wb);
    z.Add("xl/_rels/workbook.xml.rels", rels);

    // Листы (стили собираются по ходу), рисунки
    detail::Styles styles;
    std::vector<std::string> sheetXml(n);
    for (std::size_t i = 0; i < n; i++)
        sheetXml[i] = detail::SheetXml(sheets[i], styles, !sheets[i].images.empty());
    z.Add("xl/styles.xml", styles.Xml());
    int media = 0;
    for (std::size_t i = 0; i < n; i++)
    {
        const std::string id = std::to_string(i + 1);
        z.Add("xl/worksheets/sheet" + id + ".xml", sheetXml[i]);
        const Sheet& sh = sheets[i];
        if (sh.images.empty())
            continue;
        z.Add("xl/worksheets/_rels/sheet" + id + ".xml.rels",
              "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
              "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" "
              "Target=\"../drawings/drawing" + id + ".xml\"/></Relationships>");
        z.Add("xl/drawings/drawing" + id + ".xml", detail::DrawingXml(sh));
        std::string dr = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                         "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
        for (std::size_t k = 0; k < sh.images.size(); k++)
        {
            const std::string m = "image" + std::to_string(++media) + ".png";
            dr += "<Relationship Id=\"rId" + std::to_string(k + 1) +
                  "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                  "Target=\"../media/" + m + "\"/>";
            z.Add("xl/media/" + m, std::string(sh.images[k].png.begin(), sh.images[k].png.end()));
        }
        dr += "</Relationships>";
        z.Add("xl/drawings/_rels/drawing" + id + ".xml.rels", dr);
    }
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
