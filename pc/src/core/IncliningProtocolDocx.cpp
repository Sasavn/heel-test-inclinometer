#include "IncliningProtocolDocx.hpp"
#include "HeelProtocol.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace heel::formal
{
namespace
{
using Row = std::vector<std::string>;
using Rows = std::vector<Row>;
const std::string missing = "нет данных / не заполнено";
const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
const std::string relBase = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
std::string X(const std::string &s)
{
    return xlsx::detail::Escaped(s);
}
std::string DisplayDecimals(std::string text)
{
    auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    for (const std::string token : {"nan", "inf", "NaN", "Inf"})
    {
        std::size_t at = 0;
        while ((at = text.find(token, at)) != std::string::npos)
        {
            const auto end = at + token.size();
            if ((at && alpha(text[at - 1])) || (end < text.size() && alpha(text[end])))
            {
                at = end;
                continue;
            }
            std::size_t start = at;
            if (start && (text[start - 1] == '+' || text[start - 1] == '-'))
                --start;
            text.replace(start, end - start, missing);
            at = start + missing.size();
        }
    }
    // Computed descriptions from the existing core use a decimal point. Preserve
    // multi-part rule numbers, dates, filenames and all persisted input strings.
    for (std::size_t at = 0; at < text.size();)
    {
        if (text[at] < '0' || text[at] > '9')
        {
            ++at;
            continue;
        }
        auto end = at;
        std::size_t dots = 0, point = at;
        while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || text[end] == '.'))
        {
            if (text[end] == '.')
            {
                ++dots;
                point = end;
            }
            ++end;
        }
        if (dots == 1 && point > at && point + 1 < end)
            text[point] = ',';
        at = end;
    }
    return text;
}
std::string F(double x)
{
    if (!std::isfinite(x))
        return missing;
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::fixed << std::setprecision(6) << x;
    std::string display = o.str();
    std::replace(display.begin(), display.end(), '.', ',');
    return display;
}
double N(std::string s)
{
    std::replace(s.begin(), s.end(), ',', '.');
    std::istringstream i(s);
    i.imbue(std::locale::classic());
    double v;
    if (!(i >> v) || !std::isfinite(v))
        return kNaN;
    i >> std::ws;
    return i.eof() ? v : kNaN;
}
std::string Cell(const Row &r, std::size_t i)
{
    return i < r.size() ? r[i] : "";
}
std::string Field(const Data &d, const std::string &id)
{
    auto i = d.fields.find(id);
    return i == d.fields.end() ? "" : i->second;
}
double Num(const Data &d, const std::string &id)
{
    return N(Field(d, id));
}
Rows Table(const Data &d, const std::string &id)
{
    auto i = d.tables.find(id);
    if (i != d.tables.end() && !i->second.empty())
        return i->second;
    for (const auto &p : std::vector<std::pair<std::string, std::string>>{{"ballast", "act_ballast"},
                                                                          {"moves", "act_moves"},
                                                                          {"readings_1", "act_readings_1"},
                                                                          {"readings_2", "act_readings_2"}})
    {
        const auto other = id == p.first ? p.second : id == p.second ? p.first : "";
        auto a = d.tables.find(other);
        if (a != d.tables.end() && !a->second.empty())
            return a->second;
    }
    if (id == "freeboard")
    {
        const auto a = d.tables.find("act_freeboard");
        Rows rows;
        if (a != d.tables.end())
            for (const auto &r : a->second)
                rows.push_back({std::to_string(rows.size() + 1), Cell(r, 0), Cell(r, 1), Cell(r, 2), Cell(r, 4),
                                Cell(r, 5), F((N(Cell(r, 1)) + N(Cell(r, 2)) + N(Cell(r, 4)) + N(Cell(r, 5))) / 4),
                                ""});
        return rows;
    }
    if (id == "draft_marks")
    {
        const auto a = d.tables.find("act_drafts");
        Rows rows;
        if (a != d.tables.end())
            for (auto r : a->second)
            {
                r.resize(std::max<std::size_t>(8, r.size()));
                r[7] = F((N(Cell(r, 3)) + N(Cell(r, 6))) / 2);
                rows.push_back(r);
            }
        return rows;
    }
    return {};
}
bool Any(const Row &r)
{
    for (const auto &c : r)
        if (!c.empty())
            return true;
    return false;
}
std::string Show(const std::string &s)
{
    return s.empty() ? missing : s;
}
std::string DisplayField(const Data &d, const std::string &id)
{
    const auto value = Field(d, id);
    if (id == "center_mode")
    {
        if (value == "manual")
            return "Ручной ввод";
        if (value == "hydro")
            return "По заданной гидростатике";
        return value.empty() ? missing : "Способ не определён / требуется выбор";
    }
    if (id == "excess_inventory_complete" || id == "missing_inventory_complete")
    {
        if (value == "yes")
            return "Опись проверена";
        return value.empty() ? missing : "Статус не определён / требуется проверка описи";
    }
    return Show(value);
}

std::string Run(const std::string &t, bool bold = false, int size = 22)
{
    // Separate paragraphs at newlines are handled by P; tabs and line breaks
    // inside cells are explicit XML.
    std::string r = "<w:r><w:rPr><w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\" "
                    "w:cs=\"Arial\"/>";
    if (bold)
        r += "<w:b/>";
    r += "<w:sz w:val=\"" + std::to_string(size) + "\"/><w:color w:val=\"000000\"/></w:rPr>";
    std::size_t at = 0;
    for (std::size_t i = 0; i <= t.size(); ++i)
        if (i == t.size() || t[i] == '\n' || t[i] == '\t')
        {
            r += "<w:t xml:space=\"preserve\">" + X(t.substr(at, i - at)) + "</w:t>";
            if (i < t.size())
                r += t[i] == '\n' ? "<w:br/>" : "<w:tab/>";
            at = i + 1;
        }
    return r + "</w:r>";
}
std::string P(const std::string &t, bool bold = false, int size = 22, const std::string &props = {})
{
    const std::string spacing = props.find("<w:spacing") == std::string::npos
                                    ? "<w:spacing w:after=\"100\" w:line=\"260\" w:lineRule=\"auto\"/>"
                                    : "";
    const std::string properties = props.find("<w:keepNext") != std::string::npos ? props + spacing : spacing + props;
    return "<w:p><w:pPr>" + properties + "</w:pPr>" + Run(t, bold, size) + "</w:p>";
}
std::string Math(const std::string &formula)
{
    return "<w:p><m:oMathPara><m:oMath><m:r><m:rPr><m:sty "
           "m:val=\"p\"/></m:rPr><m:t>" +
           X(formula) + "</m:t></m:r></m:oMath></m:oMathPara></w:p>";
}
std::string Tc(const std::string &s, int width, bool head = false, int span = 1)
{
    std::string c = "<w:tc><w:tcPr><w:tcW w:w=\"" + std::to_string(width) + "\" w:type=\"dxa\"/>";
    if (span > 1)
        c += "<w:gridSpan w:val=\"" + std::to_string(span) + "\"/>";
    c += "<w:vAlign w:val=\"center\"/></w:tcPr>";
    return c + P(Show(s), head, 20, "<w:spacing w:after=\"40\" w:line=\"220\" w:lineRule=\"auto\"/>") + "</w:tc>";
}
struct Builder
{
    std::string body, relationships;
    std::vector<std::pair<std::string, std::string>> media;
    int imageId = 0;
    void Para(const std::string &s)
    {
        body += P(s);
    }
    void Heading(const std::string &s, int id, bool page = true)
    {
        body += "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/><w:keepNext/>";
        if (page)
            body += "<w:pageBreakBefore/>";
        body += "</w:pPr><w:bookmarkStart w:id=\"" + std::to_string(id) + "\" w:name=\"section" + std::to_string(id) +
                "\"/>" + Run(s, true, 26) + "<w:bookmarkEnd w:id=\"" + std::to_string(id) + "\"/></w:p>";
    }
    void Caption(const std::string &t)
    {
        body += P(t, true, 22, "<w:keepNext/>");
    }
    void Grid(const Row &headers, const Rows &rows, bool grouped = false)
    {
        const int width = 9300, cw = width / static_cast<int>(headers.size());
        body += "<w:tbl><w:tblPr><w:tblW w:w=\"9300\" w:type=\"dxa\"/><w:tblBorders>";
        for (const char *side : {"top", "left", "bottom", "right", "insideH", "insideV"})
            body += "<w:" + std::string(side) + " w:val=\"single\" w:sz=\"4\" w:color=\"000000\"/>";
        body += "</w:tblBorders><w:tblLayout w:type=\"fixed\"/><w:tblCellMar><w:top w:w=\"60\" "
                "w:type=\"dxa\"/><w:left w:w=\"80\" w:type=\"dxa\"/>"
                "<w:bottom w:w=\"60\" w:type=\"dxa\"/><w:right w:w=\"80\" "
                "w:type=\"dxa\"/></w:tblCellMar></w:tblPr><w:tblGrid>";
        for (std::size_t i = 0; i < headers.size(); ++i)
            body += "<w:gridCol w:w=\"" + std::to_string(cw) + "\"/>";
        body += "</w:tblGrid>";
        if (grouped)
        {
            body += "<w:tr><w:trPr><w:tblHeader/></w:trPr>";
            for (std::size_t i = 0; i < headers.size();)
            {
                const bool port = headers[i].find("ЛБ") != std::string::npos;
                const bool star = headers[i].find("ПрБ") != std::string::npos;
                if (port || star)
                {
                    std::size_t j = i + 1;
                    while (j < headers.size() && headers[j].find(port ? "ЛБ" : "ПрБ") != std::string::npos)
                        ++j;
                    body += Tc(port ? "Левый борт (ЛБ)" : "Правый борт (ПрБ)", cw * static_cast<int>(j - i), true,
                               static_cast<int>(j - i));
                    i = j;
                }
                else
                {
                    body += Tc(headers[i], cw, true);
                    ++i;
                }
            }
            body += "</w:tr>";
        }
        body += "<w:tr><w:trPr><w:tblHeader/></w:trPr>";
        for (const auto &c : headers)
            body += Tc(c, cw, true);
        body += "</w:tr>";
        auto row = [&](const Row &r) {
            body += "<w:tr>"; // No fixed height / cantSplit: long text must flow
                              // across pages.
            for (std::size_t c = 0; c < headers.size(); ++c)
                body += Tc(Cell(r, c), cw);
            body += "</w:tr>";
        };
        if (rows.empty())
            row(Row(headers.size()));
        else
            for (const auto &r : rows)
                row(r);
        body += "</w:tbl>" + P("");
    }
    void Tab(const std::string &title, Row heads, const Rows &rows, bool group = false)
    {
        std::size_t n = heads.size();
        for (const auto &r : rows)
            n = std::max(n, r.size());
        while (heads.size() < n)
            heads.push_back("Дополнительная графа " + std::to_string(heads.size() + 1));
        // Repeat the row identifier in every horizontal continuation. Never drop
        // rows or columns.
        const std::size_t block = 4;
        for (std::size_t start = 1; start < n; start += block)
        {
            const auto end = std::min(n, start + block);
            Row h{heads[0]};
            Rows rr;
            h.insert(h.end(), heads.begin() + start, heads.begin() + end);
            for (const auto &r : rows)
            {
                Row v{Cell(r, 0)};
                for (auto c = start; c < end; ++c)
                    v.push_back(Cell(r, c));
                rr.push_back(std::move(v));
            }
            Caption(title + (start == 1
                                 ? ""
                                 : " — продолжение, графы " + std::to_string(start + 1) + "–" + std::to_string(end)));
            Grid(h, rr, group);
        }
        if (n <= 1)
        {
            Caption(title);
            Grid(heads, rows);
        }
    }
    bool Image(const std::vector<unsigned char> &png, const std::string &caption, double maxWidth = 5905500.0,
               double maxHeight = 7500000.0)
    {
        // Use the PNG's own IHDR dimensions, not caller supplied estimates.
        static const unsigned char sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (png.size() < 33 || !std::equal(std::begin(sig), std::end(sig), png.begin()) ||
            std::string(png.begin() + 12, png.begin() + 16) != "IHDR")
            return false;
        auto be = [&](int i) {
            return (std::uint32_t(png[i]) << 24) | (std::uint32_t(png[i + 1]) << 16) |
                   (std::uint32_t(png[i + 2]) << 8) | png[i + 3];
        };
        const double w = be(16), h = be(20);
        if (w <= 0 || h <= 0)
            return false;
        const double scale = std::min(maxWidth / w, maxHeight / h);
        const auto cx = std::to_string(static_cast<long long>(w * scale)),
                   cy = std::to_string(static_cast<long long>(h * scale));
        const std::string id = std::to_string(++imageId), name = "image" + id + ".png";
        relationships +=
            "<Relationship Id=\"im" + id + "\" Type=\"" + relBase + "image\" Target=\"media/" + name + "\"/>";
        media.emplace_back("word/media/" + name, std::string(png.begin(), png.end()));
        body += "<w:p><w:pPr><w:jc "
                "w:val=\"center\"/></w:pPr><w:r><w:drawing><wp:inline distT=\"0\" "
                "distB=\"0\" distL=\"0\" distR=\"0\">"
                "<wp:extent cx=\"" +
                cx + "\" cy=\"" + cy + "\"/><wp:docPr id=\"" + id + "\" name=\"" + X(caption) + "\" descr=\"" +
                X(caption) +
                "\"/>"
                "<wp:cNvGraphicFramePr/><a:graphic><a:graphicData "
                "uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"0\" name=\"" +
                name +
                "\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill>"
                "<a:blip r:embed=\"im" +
                id +
                "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr>"
                "<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" +
                cx + "\" cy=\"" + cy +
                "\"/></a:xfrm>"
                "<a:prstGeom "
                "prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></"
                "a:graphicData></a:graphic>"
                "</wp:inline></w:drawing></w:r></w:p>" +
                P(caption, false, 22, "<w:jc w:val=\"center\"/>");
        return true;
    }
};

struct Mass
{
    double mass = 0;
    std::array<double, 3> moment{0, 0, 0};
    void Add(double m, const std::array<double, 3> &c)
    {
        mass += m;
        for (int i = 0; i < 3; ++i)
            moment[i] += m * c[i];
    }
    Row AsRow(const std::string &name) const
    {
        Row r{name, F(mass)};
        for (double x : moment)
            r.push_back(F(mass > 0 ? x / mass : kNaN));
        for (double x : moment)
            r.push_back(F(x));
        return r;
    }
};
struct Calculations
{
    double D = kNaN;
    std::array<double, 3> center{kNaN, kNaN, kNaN};
    Mass light;
    Rows balance, tanks, excess, missingLoads, comparison;
    std::string centerNote;
};
Calculations Calculate(const Data &d, const Settings &s, const RsCase &k)
{
    Calculations c;
    const bool hasExperience = !k.in.theta.empty() || !k.in.post.empty() || !k.r.m.empty();
    // Settings defaults alone are not evidence of an experiment.
    c.D = hasExperience ? s.units.ToTonnes(s.D) : kNaN;
    const auto mode = Field(d, "center_mode");
    if (!Field(d, "center_source").empty() && mode == "manual")
    {
        c.center = {Num(d, "center_x_m"), Num(d, "center_y_m"), Num(d, "center_z_m")};
        c.centerNote = "Источник ЦТ: ручной ввод; " + Field(d, "center_source");
    }
    else if (!Field(d, "center_source").empty() && mode == "hydro" && !Field(d, "hydro_source").empty() &&
             !Field(d, "coordinate_system").empty())
    {
        double xc = Num(d, "hydro_xc_m"), zc = Num(d, "hydro_zc_m"), r = Num(d, "hydro_r_m"),
               psi = Num(d, "trim_deg") * kPi / 180, theta = Num(d, "initial_heel_deg") * kPi / 180;
        const auto sign = Field(d, "hydro_x_sign");
        const double sx = sign == "+" ? 1 : (sign == "-" || sign == "−") ? -1 : kNaN;
        if (std::isfinite(sx) && std::abs(psi) < kPi / 2 && std::abs(theta) < kPi / 2)
            c.center = {xc + sx * (r - k.hMain) * std::sin(psi), k.hMain * std::tan(theta),
                        zc + (r - k.hMain) * std::cos(psi)};
        c.centerNote = "Источник ЦТ: гидростатика при наклонении; знак X введён явно: " + Show(sign) + "; " +
                       Field(d, "center_source");
    }
    else
        c.centerNote = "ЦТ не определён: выберите способ определения ЦТ и укажите документированный "
                       "источник; гидростатические знаки автоматически не выбираются.";
    Mass exp;
    exp.Add(c.D, c.center);
    c.balance.push_back(exp.AsRow("Судно в условиях опыта"));
    Mass extra, shortage;
    auto loads = [&](const std::string &id, Rows &dest, Mass &sum, int mi, int ci) {
        dest = Table(d, id);
        for (auto &row : dest)
        {
            if (!Any(row))
                continue;
            const double m = N(Cell(row, mi));
            std::array<double, 3> xyz{N(Cell(row, ci)), N(Cell(row, ci + 1)), N(Cell(row, ci + 2))};
            if (m < 0)
            {
                sum.Add(kNaN, xyz);
            }
            else
                sum.Add(m, xyz);
            Row br{Cell(row, 0), F(m)};
            for (double v : xyz)
                br.push_back(F(v));
            for (double v : xyz)
                br.push_back(F(m * v));
            c.balance.push_back(br);
            if (id == "excess_loads")
            {
                row.resize(std::max<std::size_t>(row.size(), 9));
                for (int i = 0; i < 3; ++i)
                    row[6 + i] = F(m * xyz[i]);
            }
        }
    };
    c.balance.push_back({"Излишние грузы (вычитаются)"});
    loads("excess_loads", c.excess, extra, 2, 3);
    c.tanks = Table(d, "tanks");
    for (auto &row : c.tanks)
    {
        if (!Any(row))
            continue;
        double m = N(Cell(row, 1));
        std::array<double, 3> xyz{N(Cell(row, 2)), N(Cell(row, 3)), N(Cell(row, 4))};
        const std::string use = Cell(row, 8);
        row.resize(std::max<std::size_t>(row.size(), 9));
        for (int i = 0; i < 3; ++i)
            row[5 + i] = F(m * xyz[i]);
        if (use == "excess" || use == "излишние")
        {
            bool duplicate = false;
            for (const auto &e : c.excess)
                if (Cell(e, 0) == Cell(row, 0))
                {
                    duplicate = true;
                    if (N(Cell(e, 2)) != m || N(Cell(e, 3)) != xyz[0] || N(Cell(e, 4)) != xyz[1] ||
                        N(Cell(e, 5)) != xyz[2])
                    {
                        extra.mass = kNaN;
                        extra.moment.fill(kNaN);
                        c.centerNote += " Противоречивое дублирование цистерны в излишних грузах: " + Cell(row, 0) +
                                        ". Итог порожнем не определён.";
                    }
                    else
                        c.centerNote +=
                            " Цистерна " + Cell(row, 0) +
                            " учтена один раз через акт излишних грузов; повторная строка цистерн не вычитается.";
                }
            if (duplicate)
                continue;
            extra.Add(m >= 0 ? m : kNaN, xyz);
            Row br{Cell(row, 0), F(m)};
            for (double v : xyz)
                br.push_back(F(v));
            for (double v : xyz)
                br.push_back(F(m * v));
            c.balance.push_back(br);
        }
        else if (use != "retained" && use != "в порожнем")
            extra.mass = kNaN;
    }
    if (Field(d, "excess_inventory_complete") != "yes")
    {
        extra.mass = kNaN;
        extra.moment.fill(kNaN);
    }
    c.balance.push_back(extra.AsRow("Всего излишних грузов"));
    c.balance.push_back({"Недостающие грузы (добавляются)"});
    loads("missing_loads", c.missingLoads, shortage, 2, 3);
    if (Field(d, "missing_inventory_complete") != "yes")
    {
        shortage.mass = kNaN;
        shortage.moment.fill(kNaN);
    }
    c.balance.push_back(shortage.AsRow("Всего недостающих грузов"));
    c.light.mass = c.D - extra.mass + shortage.mass;
    for (int i = 0; i < 3; ++i)
        c.light.moment[i] = exp.moment[i] - extra.moment[i] + shortage.moment[i];
    if (!(c.light.mass > 0))
    {
        c.light.mass = kNaN;
        c.light.moment.fill(kNaN);
    }
    c.balance.push_back(c.light.AsRow("Судно порожнем"));
    const std::array<double, 4> got{c.light.mass, c.light.moment[0] / c.light.mass, c.light.moment[2] / c.light.mass,
                                    k.hMain};
    const std::array<const char *, 4> refs{"reference_mass_t", "reference_x_m", "reference_z_m", "reference_h_m"};
    const std::array<const char *, 4> labels{"Водоизмещение порожнем, т", "Абсцисса центра тяжести, м",
                                             "Аппликата центра тяжести, м", "Метацентрическая высота, м"};
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double ref = Num(d, refs[i]), diff = got[i] - ref;
        const double denom = i == 1 ? Num(d, "length_reference_m") : std::abs(ref);
        c.comparison.push_back({labels[i], F(got[i]), F(ref), F(diff),
                                F(denom > 0 ? 100 * (i == 1 ? std::abs(diff) : diff) / denom : kNaN),
                                i == 1 ? "Процент от расчётной длины L. " + Show(Field(d, "reference_document"))
                                       : Show(Field(d, "reference_document"))});
    }
    return c;
}

std::string Label(const RsCase &k, std::size_t i)
{
    return i < k.label.size()       ? k.label[i]
           : i < k.in.number.size() ? std::to_string(k.in.number[i])
                                    : std::to_string(i);
}
bool SelectedInstrument(const RsCase &k)
{
    return k.in.instrument >= 0 && static_cast<std::size_t>(k.in.instrument) < k.in.post.size();
}
std::string ActiveAngleSource(const Settings &s, const RsCase &k)
{
    if (k.in.post.empty())
        return k.in.theta.empty() ? missing : "Приращения единого расчётного ряда";
    if (!SelectedInstrument(k))
        return "Среднее пригодных приращений постов";
    const auto index = static_cast<std::size_t>(k.in.instrument);
    const int post = index < k.postIds.size() ? k.postIds[index] : -1;
    return post >= 0 && post < kMaxPosts ? "Пост «" + s.PostName(post) + "»"
                                         : "Пост входного ряда №" + std::to_string(index + 1) + " (название не задано)";
}
Rows Electronic(const Settings &s, const RsCase &k, bool manual)
{
    Rows rows;
    for (std::size_t p = 0; p < k.in.post.size(); ++p)
    {
        const auto &post = k.in.post[p];
        int pid = p < k.postIds.size() ? k.postIds[p] : static_cast<int>(p);
        const std::string name = pid >= 0 && pid < kMaxPosts ? s.PostName(pid) : "Пост " + std::to_string(pid + 1);
        for (std::size_t i = 0; i < post.size(); ++i)
            rows.push_back({name, Label(k, i), "", manual ? "угол из ручных отсчётов" : "электронный уровень положения",
                            "", "", "", F(i ? (post[i] - post[i - 1]) * kPi / 180 : kNaN), F(post[i])});
    }
    if (k.in.post.empty())
        for (std::size_t i = 0; i < k.in.theta.size(); ++i)
            rows.push_back({"Расчётный угол (пост не указан)", Label(k, i), "",
                            manual ? "угол из ручных отсчётов" : "электронный уровень положения", "", "", "",
                            F(i ? (k.in.theta[i] - k.in.theta[i - 1]) * kPi / 180 : kNaN), F(k.in.theta[i])});
    return rows;
}

void ReadingMatrix(Builder &b, const std::string &title, const Rows &rows)
{
    b.Para("Ручные вески: отсчёты и средние отклонения в мм; электронные посты: "
           "углы в градусах и приращения в радианах. Пустые ручные графы "
           "сохранены.");
    std::vector<std::string> positions;
    std::map<std::string, std::map<std::string, std::string>> matrix;
    std::vector<std::string> order;
    std::vector<std::string> posts;
    std::map<std::string, std::set<int>> suppliedNumbers;
    auto set = [&](const std::string &key, const std::string &pos, const std::string &value) {
        if (!matrix.count(key))
            order.push_back(key);
        auto &v = matrix[key][pos];
        if (!v.empty())
            v += "; ";
        v += Show(value);
    };
    for (const auto &row : rows)
    {
        if (!Any(row))
            continue;
        std::string pos = Show(Cell(row, 1));
        if (std::find(positions.begin(), positions.end(), pos) == positions.end())
            positions.push_back(pos);
        const std::string prefix = Show(Cell(row, 0)) + " — ";
        if (std::find(posts.begin(), posts.end(), prefix) == posts.end())
            posts.push_back(prefix);
        if (!Cell(row, 8).empty())
            set(prefix + (Cell(row, 3) == "угол из ручных отсчётов" ? "Угол из ручных отсчётов, °"
                                                                    : "Электронный уровень положения, °"),
                pos, Cell(row, 8));
        if (!Cell(row, 4).empty() || !Cell(row, 2).empty())
        {
            set(prefix + "Замер " + Show(Cell(row, 2)) + " " + Cell(row, 3) + ", мм", pos, Cell(row, 4));
            const double number = N(Cell(row, 2));
            if (std::isfinite(number) && number >= 1 && number <= 10 && std::floor(number) == number)
                suppliedNumbers[prefix].insert(static_cast<int>(number));
        }
        for (const auto &pair : std::vector<std::pair<int, std::string>>{
                 {5, "Среднее отклонение, мм"}, {6, "Разность отклонений, мм"}, {7, "Приращение угла, рад"}})
            if (!Cell(row, pair.first).empty())
                set(prefix + pair.second, pos, Cell(row, pair.first));
        for (std::size_t c = 9; c < row.size(); ++c)
            set(prefix + "Дополнительная графа " + std::to_string(c + 1), pos, row[c]);
    }
    if (positions.empty())
        positions = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
    // Every post retains the complete minimum manual form, even when only a
    // mean or a subset of readings was supplied. Add empty cells, never values.
    if (posts.empty())
        posts.push_back("");
    auto ensure = [&](const std::string &key) {
        if (matrix.emplace(key, std::map<std::string, std::string>{}).second)
            order.push_back(key);
    };
    for (const auto &prefix : posts)
    {
        for (int n = 1; n <= 10; ++n)
            if (!suppliedNumbers[prefix].count(n))
                ensure(prefix + "Замер " + std::to_string(n) + (n % 2 ? " вправо, мм" : " влево, мм"));
        for (const char *s : {"Среднее отклонение, мм", "Разность отклонений, мм", "Приращение угла, рад"})
            ensure(prefix + s);
    }
    for (std::size_t start = 0; start < positions.size(); start += 4)
    {
        auto end = std::min(positions.size(), start + 4);
        Row headers{"Показания / номер наклонения"};
        headers.insert(headers.end(), positions.begin() + start, positions.begin() + end);
        Rows rr;
        for (const auto &key : order)
        {
            Row row{key};
            for (auto p = start; p < end; ++p)
                row.push_back(matrix[key][positions[p]]);
            rr.push_back(row);
        }
        b.Caption(title + (start ? " — продолжение" : ""));
        b.Grid(headers, rr);
    }
}

std::string ReadFile(const std::filesystem::path &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Не удалось прочитать вложение");
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    if (f.bad())
        throw std::runtime_error("Ошибка чтения вложения");
    return bytes;
}
void WriteFile(const std::filesystem::path &p, const std::string &bytes)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !f.flush())
        throw std::runtime_error("Не удалось записать файл отчёта");
}
struct Attached
{
    std::string slot, caption, status, name, bytes;
    bool png = false;
};

bool CoversSlot(const std::string &composite, const std::string &wanted)
{
    std::size_t start = 0;
    while (start <= composite.size())
    {
        const auto end = composite.find('+', start);
        if (composite.substr(start, end == std::string::npos ? end : end - start) == wanted)
            return true;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return false;
}

std::vector<Attached> Gather(const Data &d, const std::filesystem::path &target)
{
    auto list = d.attachments;
    for (const auto &a : DefaultData().attachments)
    {
        const bool supplied = std::any_of(list.begin(), list.end(), [&](const Attachment &x) {
            return !x.path.empty() && CoversSlot(x.slot, a.slot);
        });
        if (supplied)
        {
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const Attachment &x) { return x.path.empty() && x.slot == a.slot; }),
                       list.end());
            continue;
        }
        bool found = false;
        for (const auto &x : list)
            found |= x.slot == a.slot;
        if (!found)
            list.push_back(a);
    }
    // Preserve the already exported legacy workbook and all sibling plots as
    // supplements.
    std::set<std::filesystem::path> paths;
    for (const auto &a : list)
        if (!a.path.empty())
            paths.insert(std::filesystem::absolute(text::PathFromUtf8(a.path)).lexically_normal());
    bool legacy = false;
    // Only the current export directory is an implicit source. info.folder is the
    // measurement input directory and may contain stale or unrelated legacy outputs.
    for (const auto &dir : {std::filesystem::absolute(target).parent_path()})
    {
        if (dir.empty() || !std::filesystem::is_directory(dir))
            continue;
        for (const auto &e : std::filesystem::directory_iterator(dir))
        {
            const auto name = text::PathToUtf8(e.path().filename());
            if (!e.is_regular_file())
                continue;
            const bool workbook = name == kReportName;
            const bool plot =
                e.path().extension() == ".png" &&
                (name.find("График_") == 0 || name.find("Положения_") == 0 || name.find("Сравнение_") == 0);
            if (!workbook && !plot)
                continue;
            legacy |= workbook;
            const auto abs = std::filesystem::absolute(e.path()).lexically_normal();
            if (paths.insert(abs).second)
                list.push_back({"legacy", text::PathToUtf8(e.path()), name});
        }
    }
    if (!legacy)
        list.push_back({"legacy", "", "Дополнение – исходный отчёт XLSX"});
    std::vector<Attached> out;
    std::size_t index = 0;
    for (const auto &a : list)
    {
        Attached r;
        r.slot = a.slot;
        r.caption = a.caption;
        if (a.path.empty())
            r.status = "ОТСУТСТВУЕТ: требуется предоставить файл";
        else
        {
            auto path = text::PathFromUtf8(a.path);
            if (!std::filesystem::is_regular_file(path))
                r.status = "ОТСУТСТВУЕТ: файл недоступен: " + a.path;
            else
            {
                r.bytes = ReadFile(path);
                if (r.bytes.empty())
                    throw std::runtime_error("Пустой файл вложения: " + a.caption);
                std::string ext = text::PathToUtf8(path.extension());
                // Generated ASCII filenames avoid unsafe relative paths and unsupported
                // ZIP filename encodings.
                if (ext.size() > 12 || ext.find_first_not_of(".abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ"
                                                             "RSTUVWXYZ0123456789") != std::string::npos)
                    ext = ".bin";
                r.name = "attachment_" + std::to_string(++index) + ext;
                r.png = r.bytes.size() > 8 && r.bytes.compare(1, 3, "PNG") == 0;
                r.status = r.png ? "Файл скопирован в комплект; рисунок встроен при "
                                   "корректном PNG"
                                 : "Отдельный файл в комплекте; содержимое не "
                                   "отображается в DOCX";
            }
        }
        out.push_back(std::move(r));
    }
    return out;
}
} // namespace

namespace
{
std::string Mr(const std::string &s)
{
    return "<m:r><m:t>" + X(s) + "</m:t></m:r>";
}
std::string Sub(const std::string &s, const std::string &sub)
{
    return "<m:sSub><m:e>" + Mr(s) + "</m:e><m:sub>" + Mr(sub) + "</m:sub></m:sSub>";
}
std::string Sup(const std::string &s, const std::string &power)
{
    return "<m:sSup><m:e>" + Mr(s) + "</m:e><m:sup>" + Mr(power) + "</m:sup></m:sSup>";
}
std::string Frac(const std::string &a, const std::string &b)
{
    return "<m:f><m:num>" + a + "</m:num><m:den>" + b + "</m:den></m:f>";
}
std::string Root(const std::string &a)
{
    return "<m:rad><m:radPr><m:degHide m:val=\"1\"/></m:radPr><m:deg/><m:e>" + a + "</m:e></m:rad>";
}
std::string Om(const std::string &a)
{
    return "<w:p><m:oMathPara><m:oMath>" + a + "</m:oMath></m:oMathPara></w:p>";
}
std::string SecProps()
{
    std::string s = "<w:sectPr><w:footerReference w:type=\"default\" r:id=\"footer\"/>"
                    "<w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"850\" "
                    "w:right=\"1134\" w:bottom=\"1150\" "
                    "w:left=\"1472\" w:header=\"360\" w:footer=\"430\" w:gutter=\"0\"/>"
                    "<w:pgBorders w:offsetFrom=\"page\">";
    for (const char *side : {"top", "left", "bottom", "right"})
        s += "<w:" + std::string(side) + " w:val=\"single\" w:sz=\"6\" w:space=\"20\" w:color=\"000000\"/>";
    return s + "</w:pgBorders></w:sectPr>";
}
Rows DefaultDocs(const Data &d)
{
    auto r = Table(d, "documents");
    if (!r.empty())
        return r;
    for (const char *s :
         {"Правила классификации и постройки морских судов, часть IV "
          "«Остойчивость»",
          "Руководство по техническому наблюдению за постройкой судов", "Инструкция по проведению опыта кренования",
          "Общее расположение", "Теоретический чертёж", "Информация об остойчивости и непотопляемости"})
        r.push_back({std::to_string(r.size() + 1), s, "", "", "нет данных / требуется документ и редакция"});
    return r;
}
void Signatures(Builder &b, const std::string &names, bool observers = false)
{
    b.Para("Акт не подписан. Места подписания сохранены.");
    Rows rows;
    std::istringstream input(names);
    std::string name;
    while (std::getline(input, name))
        if (!name.empty())
            rows.push_back({observers ? "Наблюдатель" : "Член комиссии", "________________", name, "________________"});
    while (rows.size() < (observers ? 1u : 2u))
        rows.push_back(
            {observers ? "Наблюдатель" : "Член комиссии", "________________", "________________", "________________"});
    b.Tab("Места подписания", {"Должность", "Подпись", "Расшифровка", "Дата"}, rows);
}
} // namespace

bool WriteDocx(const std::filesystem::path &file, const Data &d, const Settings &s, const RsCase &k,
               const ReportInfo &info, const std::vector<ReportImage> &images, std::string *err)
{
    std::filesystem::path bundle, temporary;
    std::vector<std::filesystem::path> created;
    try
    {
        if (file.empty())
            throw std::runtime_error("Не задан путь DOCX");
        if (std::filesystem::exists(file) && !std::filesystem::is_regular_file(file))
            throw std::runtime_error("Путь DOCX указывает не на обычный файл");
        if (!file.parent_path().empty())
            std::filesystem::create_directories(file.parent_path());
        auto attachments = Gather(d, file);
        const auto base = file.parent_path() / (file.stem().native() + std::filesystem::path("_files").native());
        bundle = base;
        for (int n = 1; std::filesystem::exists(bundle); ++n)
            bundle = base.native() + std::filesystem::path("_" + std::to_string(n)).native();
        if (!std::filesystem::create_directory(bundle))
            throw std::runtime_error("Не удалось создать комплект вложений");
        const std::string folder = text::PathToUtf8(bundle.filename());
        std::string manifest = "Комплект приложений к протоколу кренования\nСтатус: ЧЕРНОВИК, подписи "
                               "и одобрение не подтверждены\n";
        for (const auto &a : attachments)
        {
            manifest += "\n" + a.slot + " | " + a.caption + " | " + a.status;
            if (!a.name.empty())
            {
                const auto dest = bundle / a.name;
                created.push_back(dest);
                WriteFile(dest, a.bytes);
                if (ReadFile(dest) != a.bytes)
                    throw std::runtime_error("Не прошла проверка копии вложения");
                manifest += " | " + a.name + " | bytes=" + std::to_string(a.bytes.size()) + " | crc32=" +
                            std::to_string(codec::Crc32(0, reinterpret_cast<const std::uint8_t *>(a.bytes.data()),
                                                        a.bytes.size()));
            }
        }
        const auto cal = Calculate(d, s, k);
        const bool d0Mismatch = std::isfinite(cal.light.mass) && cal.light.mass > 0 && std::isfinite(k.in.D0) &&
                                k.in.D0 > 0 && std::abs(cal.light.mass - k.in.D0) > 1e-6;
        Builder b;
        b.Heading("ПРОТОКОЛ КРЕНОВАНИЯ", 0, false);
        b.Para("Судно: " + Show(Field(d, "vessel_name")) + ". Проект: " + Show(Field(d, "project_number")));
        b.Para("ЧЕРНОВИК. Неподписанный документ. Соответствие нормам и одобрение "
               "не подтверждены.");
        b.Para("Назначение: определение водоизмещения порожнем и координат центра "
               "тяжести по данным опыта и судовой документации.");
        const std::set<std::string> titleRoleFields = {
            "developer",      "checker",      "bureau_head",      "norm_controller",      "approver",
            "developer_date", "checker_date", "bureau_head_date", "norm_controller_date", "approver_date"};
        for (const auto &f : FieldSpecs())
            if (f.section == 0 && !titleRoleFields.count(f.id))
                b.Para(f.label + ": " + DisplayField(d, f.id));
        Rows titleRoles;
        for (const auto &p : std::vector<std::pair<std::string, std::string>>{{"Разработал", "developer"},
                                                                              {"Проверил", "checker"},
                                                                              {"Нач. бюро", "bureau_head"},
                                                                              {"Н. контр.", "norm_controller"},
                                                                              {"Утвердил", "approver"}})
            titleRoles.push_back({p.first, Field(d, p.second), "________________", Show(Field(d, p.second + "_date"))});
        b.Tab("Основная надпись титульного листа", {"Должность", "Фамилия", "Подпись", "Дата"}, titleRoles);
        b.Tab("Реквизиты основного штампа", {"Обозначение", "Литера", "Лист", "Листов"},
              {{Field(d, "document_number"), Field(d, "document_letter"), "1", "См. нумерацию листов"}});
        b.Para("Регистрационные отметки: ____________________. Подпись: "
               "____________________. Дата: ____________________.");
        for (const auto &a : attachments)
            if (a.slot == "organization_logo")
            {
                b.Para(a.caption + ": " + a.status);
                if (a.png)
                    b.Image(std::vector<unsigned char>(a.bytes.begin(), a.bytes.end()), a.caption, 1500000, 600000);
                if (!a.name.empty())
                    b.Para("Файл логотипа: " + folder + "/" + a.name);
            }
        b.Heading("СОДЕРЖАНИЕ", 90);
        for (std::size_t sec = 1; sec < SectionNames().size(); ++sec)
        {
            b.body += "<w:p><w:pPr><w:tabs><w:tab w:val=\"right\" w:leader=\"dot\" "
                      "w:pos=\"9000\"/></w:tabs></w:pPr>" +
                      Run(SectionNames()[sec]) + "<w:r><w:tab/></w:r><w:fldSimple w:instr=\" PAGEREF section" +
                      std::to_string(sec) + " \\h \" w:dirty=\"true\">" + Run("обновить поле") + "</w:fldSimple></w:p>";
        }
        b.Para("Лист регистрации изменений и дополнения расположены после приложения "
               "Ж. Номера страниц определяются при обновлении полей документа.");

        std::map<std::string, Rows> automatic;
        automatic["documents"] = DefaultDocs(d);
        automatic["lightship"] = cal.balance;
        automatic["comparison"] = cal.comparison;
        automatic["tanks"] = cal.tanks;
        automatic["excess_loads"] = cal.excess;
        automatic["missing_loads"] = cal.missingLoads;
        auto ballast = Table(d, "ballast");
        // Coordinates are manual, masses and group identifiers come from the active
        // Settings.
        if (!s.rsGroups.empty())
        {
            Rows groups;
            for (std::size_t g = 0; g < s.rsGroups.size(); ++g)
            {
                Row r{s.rsGroups[g].name, F(s.units.ToTonnes(s.rsGroups[g].mass)), "", "", ""};
                for (const auto &manual : ballast)
                    if (Cell(manual, 0) == r[0])
                    {
                        for (std::size_t c = 2; c < manual.size(); ++c)
                        {
                            if (r.size() <= c)
                                r.resize(c + 1);
                            r[c] = manual[c];
                        }
                    }
                groups.push_back(r);
            }
            for (const auto &r : ballast)
            {
                bool match = false;
                for (const auto &g : groups)
                    match |= Cell(r, 0) == Cell(g, 0);
                if (!match)
                    groups.push_back(r);
            }
            ballast = groups;
        }
        automatic["ballast"] = ballast;
        automatic["act_ballast"] = ballast;
        auto rawMoveTable = [&](const std::string &id) {
            const auto table = d.tables.find(id);
            return table == d.tables.end() ? Rows{} : table->second;
        };
        const Rows manualMoves = rawMoveTable("moves"), manualActMoves = rawMoveTable("act_moves");
        Rows moves;
        if (!s.rsMoves.empty())
        {
            moves.clear();
            for (std::size_t i = 0; i < s.rsMoves.size(); ++i)
                for (std::size_t g = 0; g < std::max(s.rsGroups.size(), s.rsMoves[i].size()); ++g)
                {
                    const double arm = g < s.rsMoves[i].size() ? s.units.ToMetres(s.rsMoves[i][g]) : kNaN;
                    const double mass = g < s.rsGroups.size() ? s.units.ToTonnes(s.rsGroups[g].mass) : kNaN;
                    moves.push_back({"Перемещение " + std::to_string(i + 1),
                                     g < s.rsGroups.size() ? s.rsGroups[g].name : "Группа " + std::to_string(g + 1), "",
                                     "", "", F(arm), F(mass), F(mass * arm)});
                }
        }
        automatic["moves"] = moves;
        automatic["act_moves"] = moves;
        auto readings1 = Table(d, "readings_1"), readings2 = Table(d, "readings_2");
        const auto electronic = Electronic(s, k, Field(d, "measurement_mode") == "manual");
        const std::size_t firstCount = k.in.post.empty() ? electronic.size() : k.in.post[0].size();
        const std::size_t secondCount = k.in.post.size() > 1 ? k.in.post[1].size() : 0;
        for (std::size_t i = 0; i < electronic.size(); ++i)
        {
            auto &rows = i >= firstCount && i < firstCount + secondCount ? readings2 : readings1;
            rows.push_back(electronic[i]);
        }
        automatic["readings_1"] = readings1;
        automatic["act_readings_1"] = readings1;
        automatic["readings_2"] = readings2;
        automatic["act_readings_2"] = readings2;
        // Appendices reuse the canonical primary tables. Legacy independent rows
        // remain visible below.
        auto freeboard = Table(d, "freeboard");
        Rows actFreeboard;
        for (const auto &r : freeboard)
            actFreeboard.push_back({Cell(r, 1), Cell(r, 2), Cell(r, 3), F((N(Cell(r, 2)) + N(Cell(r, 3))) / 2),
                                    Cell(r, 4), Cell(r, 5), F((N(Cell(r, 4)) + N(Cell(r, 5))) / 2)});
        automatic["act_freeboard"] = actFreeboard;
        auto draft = Table(d, "draft_marks");
        for (auto &r : draft)
            if (r.size() > 7)
                r.erase(r.begin() + 7);
        automatic["act_drafts"] = draft;
        Rows rolling = Table(d, "rolling");
        double periods = 0;
        int count = 0;
        for (auto &r : rolling)
        {
            if (!Any(r))
                continue;
            r.resize(std::max<std::size_t>(r.size(), 6));
            const double n = N(r[2]), t = N(r[3]), period = n > 0 && t >= 0 ? t / n : kNaN;
            r[4] = F(period);
            if (std::isfinite(period))
            {
                periods += period;
                ++count;
            }
        }
        for (auto &r : rolling)
            if (Any(r))
                r[5] = F(count ? periods / count : kNaN);
        automatic["rolling"] = rolling;
        for (std::size_t i = 0; i < k.r.m.size(); ++i)
        {
            const auto &q = k.r.m[i];
            const auto index = q.to >= 0 ? static_cast<std::size_t>(q.to) : i + 1;
            const std::string note =
                q.excluded ? (q.autoExcluded ? "исключён автоматически" : "исключён (ручное решение / нет данных)")
                           : (q.manual == 1 ? "учтён вручную" : "учтён");
            automatic["metacentric"].push_back({Label(k, index), "См. отдельные графы постов ниже",
                                                F(q.dTheta * kPi / 180), F(q.dM), F(q.h), F(q.h - k.r.hk),
                                                F((q.h - k.r.hk) * (q.h - k.r.hk)), note});
            automatic["quality"].push_back(
                {Label(k, index), F(q.h), F(std::abs(q.h - k.r.hk)), F(2 * k.r.sigma),
                 note + "; " +
                     (!std::isfinite(q.h) || !std::isfinite(k.r.sigma) || !std::isfinite(k.r.hk) ? missing
                      : std::abs(q.h - k.r.hk) <= 2 * k.r.sigma + 1e-9 * (1.0 + std::fabs(k.r.hk))
                          ? "в пределах 2σ"
                          : "за пределом 2σ")});
        }
        automatic["metrology"] = Table(d, "metrology");
        if (automatic["metrology"].empty())
            for (const char *name : {"Рулетка", "Крановые весы", "Ареометр", "Электронный инклинометр"})
                automatic["metrology"].push_back(
                    {name, "", "", "", "", "", "", "не заполнено / пригодность не установлена"});
        automatic["metrology_details"] = Table(d, "metrology_details");
        if (automatic["metrology_details"].empty())
        {
            const std::vector<std::pair<std::string, Row>> required = {
                {"Рулетка / лицевая часть",
                 {"Изготовитель и тип СИ", "Номер сертификата", "Заводской номер", "Владелец", "Методика и эталоны",
                  "Условия калибровки", "Дата калибровки",
                  "Клеймо, подписи и расшифровки (только подлинное вложение)"}},
                {"Рулетка / оборот",
                 {"Диапазон измерения", "Погрешность измерения",
                  "Результаты измерений и метрологические характеристики",
                  "Подпись инженера по метрологии (вложение)"}},
                {"Крановые весы / результат",
                 {"Реестр утверждённого типа СИ", "Тип, наименование и модификация", "Заводской номер",
                  "Организация поверителя", "Шифр знака", "Владелец", "Тип поверки", "Дата поверки и срок действия",
                  "Методика поверки", "Пригодность", "Номер свидетельства", "Знаки в паспорте и на СИ"}},
                {"Крановые весы / продолжение",
                 {"Средства поверки: эталон 1", "Средства поверки: эталон 2", "Средства поверки: эталон 3",
                  "Средства поверки: эталон 4", "Средства поверки: эталон 5", "Вспомогательные средства измерения",
                  "Сокращённый объём поверки / дополнительные сведения"}},
                {"Ареометр / паспорт",
                 {"Изготовитель", "Тип и заводской номер", "Обозначение паспорта", "Назначение",
                  "Диапазон, цена деления и погрешность", "Метод отсчёта и температура градуировки",
                  "Комплект поставки"}},
                {"Ареометр / оборот",
                 {"Гарантийные обязательства", "Приёмка и методика поверки", "Межповерочный интервал",
                  "Дата изготовления и первичной поверки", "ОТК, клеймо и подпись (только подлинное вложение)"}}};
            for (const auto &p : required)
                for (const auto &label : p.second)
                    automatic["metrology_details"].push_back({p.first, label, ""});
        }

        std::set<std::string> shownAttachments;
        auto attachAt = [&](const std::string &slot) {
            for (const auto &a : attachments)
                if (CoversSlot(a.slot, slot))
                {
                    b.Caption(a.caption);
                    b.Para(a.status);
                    if (!a.name.empty())
                    {
                        b.Para("Файл комплекта: " + folder + "/" + a.name +
                               ". Размер: " + std::to_string(a.bytes.size()) + " байт.");
                        if (!shownAttachments.insert(a.name).second)
                        {
                            b.Para("Составное вложение: эта логическая часть представлена тем же файлом; состав задан "
                                   "оператором: " +
                                   a.slot);
                            continue;
                        }
                        if (a.png && !b.Image(std::vector<unsigned char>(a.bytes.begin(), a.bytes.end()), a.caption))
                            b.Para("Встроенное изображение недоступно: некорректный PNG; "
                                   "оригинал сохранён отдельно.");
                    }
                }
        };
        for (int section = 1; section <= 16; ++section)
        {
            b.Heading(SectionNames()[section], section);
            if (section >= 10 && section <= 15)
            {
                static const char *acts[] = {
                    "Акт №1 – Измерение высоты надводного борта и осадок", "Акт №2 – Недостающие грузы",
                    "Акт №3 – Состояние цистерн и жидких грузов судна",    "Акт №4 – Излишние грузы",
                    "Акт №5 – Акт готовности и перемещения крен-балласта", "Акт №6 – Запись показания весков"};
                b.Caption(acts[section - 10]);
                b.Para("Судно: " + Show(Field(d, "vessel_name")) +
                       ". Дата опыта: " + Show(Field(d, "experiment_date")));
            }
            for (const auto &f : FieldSpecs())
                if (f.section == section)
                    b.Para(f.label + ": " + DisplayField(d, f.id));
            if (section == 1)
                b.Para("Применимость и редакция правил, инструкция и факт её одобрения "
                       "требуют подтверждающих документов. Автоматическая обработка не "
                       "устанавливает факт одобрения.");
            if (section == 4)
            {
                attachAt("mooring_plan");
                b.body += Math("ΔT = ± погрешность измерения осадок (м); значение: " + Show(Field(d, "draft_error_m")));
                b.body += Om(Mr("Θ ≈ ") + Frac(Mr("TПрБ − TЛБ"), Mr("B")) + Mr(" · 180/π"));
                b.body += Om(Mr("ψ ≈ ") + Frac(Mr("Tн − Tк"), Mr("L")) + Mr(" · 180/π"));
                const double span = Num(d, "draft_transverse_span_m"), len = Num(d, "draft_longitudinal_span_m");
                const double th =
                    span > 0 ? (Num(d, "draft_starboard_m") - Num(d, "draft_port_m")) / span * 180 / kPi : kNaN;
                const double psi =
                    len > 0 ? (Num(d, "draft_forward_m") - Num(d, "draft_aft_m")) / len * 180 / kPi : kNaN;
                b.Para("Приближение малых углов по осадкам: Θ = " + F(th) + " °; ψ = " + F(psi) +
                       " °. Углы гидростатики вводятся отдельно с согласованными "
                       "знаками.");
                b.Para("Дифферент на длине КВЛ = (Tн−Tк)·LКВЛ/L = " +
                       F(len > 0
                             ? (Num(d, "draft_forward_m") - Num(d, "draft_aft_m")) * Num(d, "length_waterline_m") / len
                             : kNaN) +
                       " м; для сравнения использовать модуль. Осадки от надводного "
                       "борта требуют геометрии и поправок по теоретическому чертежу.");
            }
            if (section == 5)
            {
                b.body += Om(Sub("ΔM", "i") + Mr(" = Σ ") + Sub("m", "g") + Mr(" · ") + Sub("l", "ig"));
                b.body += Om(Mr("ā = ") + Frac(Mr("Σ a_j"), Mr("n")) + Mr("; Δa_i = ā_(i−1) − ā_i; Δθ_i ≈ ") +
                             Frac(Mr("Δa_i"), Mr("λ")));
                if (SelectedInstrument(k))
                    b.body += Om(Mr("Δθ расчёта = Δθ выбранного поста; T = ") + Frac(Mr("Σt"), Mr("N")));
                else if (!k.in.post.empty())
                    b.body += Om(Mr("Δθср = ") + Frac(Mr("Σ Δθ постов"), Mr("число пригодных постов")) + Mr("; T = ") +
                                 Frac(Mr("Σt"), Mr("N")));
                else
                    b.body += Om(Mr("Δθ расчёта = θ_i − θ_(i−1); T = ") + Frac(Mr("Σt"), Mr("N")));
                b.Para("Отсчёты ручных весков и электронных постов различаются по "
                       "происхождению. Электронные углы взяты из RsCase; ручные "
                       "миллиметры не синтезируются.");
                b.Para("Период по электронному сигналу (алгоритм текущей обработки): " + F(k.tc) + " с; диапазон " +
                       F(k.tcMin) + "…" + F(k.tcMax) + " с; участков " + std::to_string(k.tcN) +
                       ". Это отдельная оценка от ручного T=t/N.");
            }
            if (section == 6)
            {
                b.Para(cal.centerNote);
                b.Para("Источник приращения угла таблицы 9: " + ActiveAngleSource(s, k));
                b.body += Om(Mr("D = ρ · V"));
                b.Para("Водоизмещение из Settings = " + F(cal.D) + " т; ρ·V по ручной гидростатике = " +
                       F(Num(d, "water_density_tm3") * Num(d, "hydro_volume_m3")) +
                       " т. Совпадение проверяет ответственный исполнитель.");
                b.Para("Источники водоизмещения порожнем: D₀ баланса грузов = " + F(cal.light.mass) +
                       " т; D₀ в ранее рассчитанной проверке качества = " + F(k.in.D0) + " т.");
                const bool haveBalance = std::isfinite(cal.light.mass) && cal.light.mass > 0;
                const bool haveQualityD0 = std::isfinite(k.in.D0) && k.in.D0 > 0;
                if (haveBalance && haveQualityD0)
                {
                    if (d0Mismatch)
                        b.Para("БЛОКИРУЮЩЕЕ ПРЕДУПРЕЖДЕНИЕ: D₀ баланса отличается от D₀ проверки качества. "
                               "Оценка качества текущего баланса заблокирована для итогового заключения. "
                               "Требуется сверить исходные данные и повторить обработку. "
                               "Проверка качества не пересчитывалась; D₀ баланса в неё не подставлялось.");
                    else
                        b.Para("D₀ баланса и проверки качества согласованы в пределах точности вывода 0,000001 т. "
                               "Сохранены ранее рассчитанные входы и результаты проверки качества.");
                }
                else if (!haveQualityD0)
                    b.Para("ПРЕДУПРЕЖДЕНИЕ: D₀ проверки качества не задано. "
                           "Ранее рассчитанные коэффициент приведения, ошибка и статус сохранены; "
                           "D₀ баланса вместо отсутствующего входа не подставлялось.");
                else
                    b.Para("ПРЕДУПРЕЖДЕНИЕ: D₀ баланса не определено; согласованность с D₀ проверки качества "
                           "не подтверждена. Ранее рассчитанная проверка качества не пересчитывалась.");
                b.body += Om(Sub("z", "gψ") + Mr(" = ") + Sub("z", "cψ") + Mr(" + (rψ − h) · cos ψ"));
                b.body += Om(Sub("x", "gψ") + Mr(" = ") + Sub("x", "cψ") +
                             Mr(" " + Show(Field(d, "hydro_x_sign")) + " (rψ − h) · sin ψ"));
                b.body += Om(Sub("y", "g") + Mr(" = h · tan Θ₀"));
                b.Para("ЦТ при опыте: Xg=" + F(cal.center[0]) + " м; Yg=" + F(cal.center[1]) +
                       " м; Zg=" + F(cal.center[2]) + " м. h основного расчёта=" + F(k.hMain) + " м.");
                b.body += Om(Sub("h", "i") + Mr(" = ") + Frac(Sub("ΔM", "i"), Mr("D · ") + Sub("Δθ", "i")) + Mr("; ") +
                             Sub("h", "k") + Mr(" = ") + Frac(Mr("Σ h_i"), Mr("n")));
                b.body +=
                    Om(Sub("h", "испр") + Mr(" = ") + Sub("h", "без Δh") + Mr(" − Δh; Mx = m·X; My = m·Y; Mz = m·Z"));
                b.body += Om(Sub("h", "без Δh") + Mr(" = ") + Sub("h", "k") + Mr(" − e (по 6.6.5); иначе ") +
                             Sub("h", "без Δh") + Mr(" = ") + Sub("h", "k"));
                b.Para(k.r.reduced
                           ? "Уменьшение по 6.6.5 применено. Итог без поправки на свободные поверхности уже уменьшен "
                             "на вероятную ошибку."
                           : "Уменьшение по 6.6.5 не применено. Итог без поправки на свободные поверхности равен h_k.");
                b.body +=
                    Om(Sub("D", "0") + Mr(" = D − Σmизл + Σmнед; ") + Sub("M", "0") + Mr(" = Mопыт − ΣMизл + ΣMнед"));
                b.body += Om(Mr("(X₀, Y₀, Z₀) = ") + Frac(Sub("M", "0"), Sub("D", "0")));
                b.Para("Все массовые моменты пересчитаны из массы и координат, без "
                       "переноса готовых моментов. Жидкости вычитаются один раз: "
                       "строки цистерн со статусом excess / излишние. Для сохранённых "
                       "в порожнем: retained / в порожнем. Не дублировать их в акте "
                       "излишних грузов.");
                double sum = 0;
                int n = 0;
                for (const auto &q : k.r.m)
                    if (std::isfinite(q.h))
                    {
                        sum += q.h;
                        ++n;
                    }
                b.Para("До отбраковки: n=" + std::to_string(n) + ", среднее h=" + F(n ? sum / n : kNaN) +
                       " м. После отбраковки: n=" + std::to_string(k.r.n) + ", h_k=" + F(k.r.hk) +
                       " м; Σ(h_i−h_k)²=" + F(k.r.sumSq) + " м². Поправка свободных поверхностей Δh=" + F(k.dh) +
                       " м (вход текущего расчёта).");
                b.Para("Итог по приращениям: h без поправки=" + F(k.r.hFinalRaw) + " м, h исправленная=" +
                       F(k.r.hFinal) + " м. От начального положения, без поправки: " + F(k.hCurrent) +
                       " м. От начального положения, с поправкой Δh: " + F(k.hCurrent - k.dh) + " м. Основной расчёт=" +
                       std::string(s.RsMain() == 1 ? "по приращениям" : "от начального положения") +
                       "; итог=" + F(k.hMain) + " м.");
                // Same conversion as AppRules.cpp::ProcControlPlot (also used by
                // ProcExportControl): M uses input mass/arm units, not P*l/D.
                const double fitDenominator = k.fit.k * s.D;
                const double hControlFit = std::isfinite(s.D) && s.D > 0 && std::isfinite(k.fit.k) &&
                                                   std::isfinite(fitDenominator) && fitDenominator != 0
                                               ? s.units.ArmToM() / fitDenominator
                                               : kNaN;
                b.Para("МНК контрольного графика: " + F(hControlFit) +
                       " м (без поправки Δh). "
                       "Высота по наклону прямой tg(θ−θ₀) от кренящего момента M.");
            }
            if (section == 7)
            {
                if (d0Mismatch)
                    b.Para("БЛОКИРУЮЩЕЕ ПРЕДУПРЕЖДЕНИЕ: D₀ проверки качества расходится с балансом грузов. "
                           "Приведённые ниже статусы относятся к ранее рассчитанным исходным данным. "
                           "До согласования D₀ и повторной обработки качество текущего баланса для итогового "
                           "заключения не подтверждено.");
                const std::string ss = Mr("Σ ") + Sup("(h_i − h_k)", "2");
                b.body += Om(Mr("|h_i − h_k| ≤ 2 · ") + Root(Frac(ss, Mr("n − 1"))));
                b.body += Om(Mr("σ = ") + Root(Frac(ss, Mr("n − 1"))));
                b.body += Om(Mr("e = ") + Sub("t", "αn") + Mr(" · ") + Root(Frac(ss, Mr("n · (n − 1)"))));
                b.body += Om(Mr("ε = max(min(0,05 · h; 0,10 · ") + Sub("l", "max") + Mr("); 0,04 м)"));
                b.body += Om(Mr("e · ") + Frac(Sub("Δ", "0"), Sub("Δ", "1")) + Mr(" ≤ ε; nудовл ≥ 8"));
                b.Para("Предел ошибки при h_k ≤ 2 м: 0,02·(1+h_k); при h_k > 2 м: "
                       "0,01·(4+h_k). Допускается не более одного исключения по "
                       "выбранной методике; номера и решения сохранены в таблицах.");
                b.Para("При сравнении с 2σ применяется допуск округления 10⁻⁹·(1+|h_k|), как в существующем расчёте.");
                b.Para("t_αn=" + F(k.r.tA) + "; σ=" + F(k.r.sigma) + " м; вероятная ошибка e=" + F(k.r.err) +
                       " м; предел=" + F(k.r.errLimit) + " м; ε=" + F(k.r.eps) + " м; e·Δ₀/Δ₁=" + F(k.r.err663) +
                       " м.");
                b.Para("Входы проверки худшего случая: h=" + F(k.in.hInfo) + " м, l_max=" + F(k.in.lMax) +
                       " м, Δ₀=" + F(k.in.D0) + " т, Δ₁=" + F(k.in.Dworst) + " т. Отношение Δ₀/Δ₁=" + F(k.r.Dratio) +
                       ". Фактические статусы взяты из RsCase.");
                Rows checks;
                for (const auto &q : k.checks)
                    checks.push_back({q.id, q.req, q.ref, DisplayDecimals(q.value),
                                      q.state < 0 ? missing
                                      : q.state   ? "выполнено по расчёту"
                                                  : "не выполнено по расчёту"});
                b.Tab("Проверки текущего расчёта", {"Пункт", "Условие", "Ссылка", "Значение", "Статус"}, checks);
                b.Para("Оценка алгоритма не удостоверяет реальный опыт. Перенос "
                       "заявлений образца о соответствии и одобрении не выполняется.");
            }
            if (section == 8)
            {
                b.body += Om(Mr("δD = Dнов − Dстар; δX = Xнов − Xстар; δZ = Zнов − Zстар"));
                b.body += Om(Mr("%D = 100 · ") + Frac(Mr("δD"), Mr("Dстар")) + Mr("; %L = 100 · ") +
                             Frac(Mr("|δX|"), Mr("L")) + Mr("; %Z = 100 · ") + Frac(Mr("δZ"), Mr("Zстар")));
                b.Para("Статус заключения: ЧЕРНОВИК. Вывод о качестве опыта, "
                       "допустимости отклонений и необходимости изменений подтверждает "
                       "ответственное лицо по применяемым документам.");
            }
            if (section == 16)
            {
                // Three original sheets, six parts. Both parts must be supplied or
                // explicitly declared missing.
                for (const auto &p : std::vector<std::pair<std::string, std::string>>{
                         {"tape_calibration", "Рулетка: сертификат и метрологические "
                                              "характеристики (две стороны)"},
                         {"scales_verification", "Крановые весы: результат поверки и "
                                                 "средства поверки (две части)"},
                         {"hydrometer_passport", "Ареометр: паспорт и приёмка/поверка/гарантия (две "
                                                 "стороны)"}})
                {
                    b.Caption(p.second);
                    attachAt(p.first);
                    attachAt(p.first + "_back");
                    b.Para("Лицевая часть и продолжение/оборот должны присутствовать в "
                           "комплекте. Действительность и пригодность на дату опыта "
                           "автоматически не подтверждаются.");
                }
            }
            for (const auto &spec : TableSpecs())
                if (spec.section == section && spec.id != "changes")
                {
                    Rows rows = automatic.count(spec.id) ? automatic[spec.id] : Table(d, spec.id);
                    if (spec.id.find("readings") != std::string::npos)
                        ReadingMatrix(b, spec.title, rows);
                    else
                    {
                        auto columns = spec.columns;
                        if (spec.id == "metacentric")
                            columns[2] += "\n" + ActiveAngleSource(s, k);
                        const bool moveForm = spec.id == "moves" || spec.id == "act_moves";
                        const std::string title = moveForm ? spec.title + " — Расчётный блок из настроек обработки: " +
                                                                 std::to_string(rows.size()) + " строк"
                                                           : spec.title;
                        b.Tab(title, columns, rows,
                              spec.id == "freeboard" || spec.id == "draft_marks" || spec.id == "act_freeboard" ||
                                  spec.id == "act_drafts");
                        if (moveForm)
                        {
                            auto reference = [&](const Rows &supplied, const std::string &source) {
                                if (supplied.empty())
                                    return;
                                b.Caption("Справочные ручные сведения — " + source);
                                b.Para("Эти сведения не являются дополнительными расчётными перемещениями и не входят "
                                       "в сумму ΔM. "
                                       "Ручные строки сохранены в исходном составе; использованные при расчёте ΔM "
                                       "приведены в таблице 9.");
                                b.Tab("Ручная ведомость (справочно)", spec.columns, supplied);
                            };
                            if (spec.id == "moves")
                            {
                                if (!manualMoves.empty())
                                    reference(manualMoves, manualMoves == manualActMoves
                                                               ? "общий ввод таблицы 4 и акта №5"
                                                               : "ввод таблицы 4");
                                else
                                    reference(manualActMoves, "ввод акта №5");
                            }
                            else if (!manualMoves.empty() && manualMoves == manualActMoves)
                                reference(manualMoves, "общий ввод таблицы 4 и акта №5");
                            else
                            {
                                reference(manualActMoves, "ввод акта №5");
                                reference(manualMoves, "ввод таблицы 4");
                            }
                        }
                    }
                    if (spec.id == "metacentric")
                    {
                        Row head{"Переход"};
                        std::size_t posts = k.in.post.size();
                        for (const auto &q : k.r.m)
                            posts = std::max(posts, q.dThetaPost.size());
                        for (std::size_t p = 0; p < posts; ++p)
                            head.push_back(
                                "Δθ " +
                                (p < k.postIds.size() ? s.PostName(k.postIds[p]) : "пост " + std::to_string(p + 1)) +
                                ", рад");
                        Rows rr;
                        for (const auto &q : k.r.m)
                        {
                            Row r{q.to >= 0 ? Label(k, q.to) : "нет номера"};
                            for (std::size_t p = 0; p < posts; ++p)
                                r.push_back(F(p < q.dThetaPost.size() ? q.dThetaPost[p] * kPi / 180 : kNaN));
                            rr.push_back(r);
                        }
                        if (posts)
                            b.Tab("Таблица 9 – Раздельные приращения угла каждого поста", head, rr);
                    }
                    if (spec.id == "act_readings_1" || spec.id == "act_readings_2")
                    {
                        const bool first = spec.id == "act_readings_1";
                        b.Para(std::string(first ? "Первый весок / пост" : "Второй весок / пост") +
                               ": длина и расположение приведены в следующей форме.");
                        for (const auto &t : TableSpecs())
                            if (t.id == "instruments")
                                b.Tab(t.title + " — к акту №6", t.columns, Table(d, "instruments"));
                        Signatures(b, Field(d, first ? "act_f_observer_1" : "act_f_observer_2"), true);
                    }
                    const auto manual = Table(d, spec.id);
                    if (automatic.count(spec.id) && !manual.empty() && manual != rows && spec.id != "moves" &&
                        spec.id != "act_moves" && spec.id != "ballast" && spec.id != "tanks" &&
                        spec.id != "excess_loads" && spec.id != "rolling" && spec.id != "comparison")
                        b.Tab(spec.title + " — дополнительные ручные сведения (не заменяют расчёт)", spec.columns,
                              manual);
                }
            if (section >= 10 && section <= 14)
            {
                const char *ids[] = {"act_a_members", "act_b_members", "act_c_members", "act_d_members",
                                     "act_e_members"};
                Signatures(b, Field(d, ids[section - 10]));
            }
            if (section == 10)
                b.Para("Погрешность измерения осадок ΔT = ±" + Show(Field(d, "draft_error_m")) +
                       " м. Сведения о средстве и методике — приложение Ж.");
        }
        b.Heading("ЛИСТ РЕГИСТРАЦИИ ИЗМЕНЕНИЙ", 30);
        for (const auto &spec : TableSpecs())
            if (spec.id == "changes")
                b.Tab(spec.title, spec.columns, Table(d, spec.id));
        b.Heading("ДОПОЛНЕНИЯ К ПРОТОКОЛУ", 31);
        b.Para("Исходный отчёт обработки XLSX и все графики сохраняются "
               "дополнением. Реестр комплекта: " +
               folder + "/manifest.txt.");
        b.Para("Источник обработки: " + Show(info.folder) + "; программа: " + Show(info.program) +
               "; время обработки: " + Show(info.when));
        b.Para("Единицы Settings: " + std::string(s.units.Mass()) + ", " + s.units.Arm() +
               "; нормализовано в т и м. Ось " + AxisName(s.axis) + "; окно усреднения " + F(s.windowS) +
               " с; порог угла " + F(s.thresholdDeg) + " °; параметры выделения положений: " + F(s.minPlateauS) +
               " с, " + F(s.minStepDeg) + " °.");
        b.Para("Обработка сигнала: " + (s.filter.method == filt::Method::User
                                            ? filt::Describe(s.filter)
                                            : DisplayDecimals(filt::Describe(s.filter))));
        b.Para("Электронные таблицы содержат уровни положений из RsCase, а не "
               "исходные колебательные отсчёты. Исходные отсчёты, временные окна "
               "каждого положения, файлы, инверсия датчиков и СКО сохраняются в "
               "исходном дополнении XLSX; если книга отсутствует, эти сведения не "
               "предоставлены.");
        Rows registry;
        for (const auto &a : attachments)
            registry.push_back({a.slot, a.caption, a.status, a.name.empty() ? missing : folder + "/" + a.name});
        b.Tab("Реестр вложений и отсутствующих документов", {"Место", "Наименование", "Статус", "Файл комплекта"},
              registry);
        for (const auto &a : attachments)
            if (a.slot != "mooring_plan" && a.slot.find("tape_calibration") != 0 &&
                a.slot.find("scales_verification") != 0 && a.slot.find("hydrometer_passport") != 0)
                attachAt(a.slot);
        for (const auto &im : images)
        {
            if (!b.Image(im.png, im.caption))
            {
                b.Para("Рисунок не встроен: " + im.caption + ". Некорректный или отсутствующий PNG.");
                if (!im.png.empty())
                {
                    const auto name = "report_image_" + std::to_string(created.size()) + ".bin";
                    created.push_back(bundle / name);
                    WriteFile(bundle / name, std::string(im.png.begin(), im.png.end()));
                    manifest += "\n" + name + " | Некорректный PNG сохранён без потери байтов";
                }
            }
        }
        // Future/unknown manual fields and tables round-trip and remain visible
        // instead of disappearing.
        for (const auto &p : d.fields)
        {
            bool known = false;
            for (const auto &f : FieldSpecs())
                known |= p.first == f.id;
            if (!known)
                b.Para(p.first + ": " + Show(p.second));
        }
        for (const auto &p : d.tables)
        {
            bool known = false;
            for (const auto &t : TableSpecs())
                known |= p.first == t.id;
            if (!known)
            {
                std::size_t cols = 1;
                for (const auto &r : p.second)
                    cols = std::max(cols, r.size());
                Row h;
                for (std::size_t i = 0; i < cols; ++i)
                    h.push_back("Графа " + std::to_string(i + 1));
                b.Tab("Дополнительная таблица " + p.first, h, p.second);
            }
        }
        manifest += "\nВсе файлы скопированы и побайтово проверены. Отсутствующие "
                    "документы перечислены явно.\n";
        created.push_back(bundle / "manifest.txt");
        WriteFile(bundle / "manifest.txt", manifest);
        created.push_back(bundle / "formal-data.json");
        WriteFile(bundle / "formal-data.json", ToJson(d));
        const std::string ns = " xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/"
                               "main\""
                               " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                               "relationships\""
                               " xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                               "math\""
                               " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/"
                               "wordprocessingDrawing\""
                               " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
                               " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/"
                               "picture\"";
        xlsx::detail::ZipWriter zip;
        zip.Add("[Content_Types].xml", xml + "<Types "
                                             "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
                                             "content-types\">"
                                             "<Default Extension=\"rels\" "
                                             "ContentType=\"application/"
                                             "vnd.openxmlformats-package.relationships+xml\"/>"
                                             "<Default Extension=\"xml\" "
                                             "ContentType=\"application/xml\"/><Default Extension=\"png\" "
                                             "ContentType=\"image/png\"/>"
                                             "<Override PartName=\"/word/document.xml\" "
                                             "ContentType=\"application/"
                                             "vnd.openxmlformats-officedocument.wordprocessingml.document.main+"
                                             "xml\"/>"
                                             "<Override PartName=\"/word/styles.xml\" "
                                             "ContentType=\"application/"
                                             "vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
                                             "<Override PartName=\"/word/settings.xml\" "
                                             "ContentType=\"application/"
                                             "vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/"
                                             ">"
                                             "<Override PartName=\"/word/footer1.xml\" "
                                             "ContentType=\"application/"
                                             "vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/"
                                             "></Types>");
        zip.Add("_rels/.rels", xml +
                                   "<Relationships "
                                   "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
                                   "relationships\">"
                                   "<Relationship Id=\"doc\" Type=\"" +
                                   relBase + "officeDocument\" Target=\"word/document.xml\"/></Relationships>");
        zip.Add("word/document.xml",
                xml + "<w:document" + ns + "><w:body>" + b.body + SecProps() + "</w:body></w:document>");
        zip.Add("word/styles.xml", xml + "<w:styles" + ns +
                                       "><w:docDefaults><w:rPrDefault><w:rPr>"
                                       "<w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\"/><w:sz "
                                       "w:val=\"22\"/><w:color w:val=\"000000\"/>"
                                       "<w:lang w:val=\"ru-RU\"/></w:rPr></w:rPrDefault></w:docDefaults>"
                                       "<w:style w:type=\"paragraph\" w:default=\"1\" "
                                       "w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
                                       "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name "
                                       "w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/>"
                                       "<w:pPr><w:keepNext/><w:outlineLvl w:val=\"0\"/><w:spacing "
                                       "w:before=\"180\" w:after=\"180\"/></w:pPr>"
                                       "<w:rPr><w:b/><w:color "
                                       "w:val=\"000000\"/></w:rPr></w:style></w:styles>");
        zip.Add("word/settings.xml", xml + "<w:settings" + ns + "><w:updateFields w:val=\"true\"/></w:settings>");
        // Compact two-row engineering stamp, inside the reserved bottom margin.
        const std::array<int, 7> widths{400, 500, 1000, 750, 650, 5000, 1000};
        std::string footer = "<w:tbl><w:tblPr><w:tblW w:w=\"9300\" w:type=\"dxa\"/><w:tblBorders>";
        for (const char *side : {"top", "left", "bottom", "right", "insideH", "insideV"})
            footer += "<w:" + std::string(side) + " w:val=\"single\" w:sz=\"6\" w:color=\"000000\"/>";
        footer += "</w:tblBorders><w:tblLayout w:type=\"fixed\"/><w:tblCellMar>"
                  "<w:top w:w=\"0\" w:type=\"dxa\"/><w:left w:w=\"10\" w:type=\"dxa\"/>"
                  "<w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"10\" w:type=\"dxa\"/>"
                  "</w:tblCellMar></w:tblPr><w:tblGrid>";
        for (int width : widths)
            footer += "<w:gridCol w:w=\"" + std::to_string(width) + "\"/>";
        footer += "</w:tblGrid>";
        const Row stamp{"Изм",          "Лист", "№ докум.", "Подпись", "Дата", Show(Field(d, "document_number")),
                        "Лист / Листов"};
        for (int row = 0; row < 2; ++row)
        {
            footer += "<w:tr>";
            for (std::size_t col = 0; col < widths.size(); ++col)
            {
                footer += "<w:tc><w:tcPr><w:tcW w:w=\"" + std::to_string(widths[col]) +
                          "\" w:type=\"dxa\"/></w:tcPr><w:p><w:pPr><w:jc w:val=\"center\"/><w:spacing w:after=\"0\" "
                          "w:before=\"0\" w:line=\"180\" w:lineRule=\"auto\"/></w:pPr>";
                if (row == 1 && col == 6)
                    footer += "<w:fldSimple w:instr=\" PAGE \">" + Run("1", false, 16) + "</w:fldSimple>" +
                              Run(" / ", false, 16) + "<w:fldSimple w:instr=\" NUMPAGES \">" + Run("1", false, 16) +
                              "</w:fldSimple>";
                else
                    footer += Run(row == 0 ? stamp[col] : "", false, col == 5 ? 18 : 16);
                footer += "</w:p></w:tc>";
            }
            footer += "</w:tr>";
        }
        footer += "</w:tbl>" + P("", false, 2, "<w:spacing w:after=\"0\" w:line=\"20\" w:lineRule=\"exact\"/>");
        zip.Add("word/footer1.xml", xml + "<w:ftr" + ns + ">" + footer + "</w:ftr>");
        zip.Add("word/_rels/document.xml.rels",
                xml +
                    "<Relationships "
                    "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
                    "relationships\">" +
                    "<Relationship Id=\"styles\" Type=\"" + relBase + "styles\" Target=\"styles.xml\"/>" +
                    "<Relationship Id=\"settings\" Type=\"" + relBase + "settings\" Target=\"settings.xml\"/>" +
                    "<Relationship Id=\"footer\" Type=\"" + relBase + "footer\" Target=\"footer1.xml\"/>" +
                    b.relationships + "</Relationships>");
        for (const auto &m : b.media)
            zip.Add(m.first, m.second);
        temporary = file.native() + std::filesystem::path(".partial").native();
        for (int i = 1; std::filesystem::exists(temporary); ++i)
            temporary = file.native() + std::filesystem::path(".partial" + std::to_string(i)).native();
        WriteFile(temporary, zip.Finish());
        if (std::filesystem::exists(file))
        {
            auto backup = temporary.native() + std::filesystem::path(".backup").native();
            std::filesystem::rename(file, backup);
            try
            {
                std::filesystem::rename(temporary, file);
            }
            catch (...)
            {
                std::filesystem::rename(backup, file);
                throw;
            }
            std::filesystem::remove(backup);
        }
        else
            std::filesystem::rename(temporary, file);
        if (err)
            err->clear();
        return true;
    }
    catch (const std::exception &e)
    {
        std::error_code ignored;
        if (!temporary.empty())
            std::filesystem::remove(temporary, ignored);
        for (const auto &p : created)
            std::filesystem::remove(p, ignored);
        // Remove only the empty directory owned by this call; never recursively
        // touch an existing bundle.
        if (!bundle.empty())
            std::filesystem::remove(bundle, ignored);
        if (err)
            *err = e.what();
        return false;
    }
}
} // namespace heel::formal
