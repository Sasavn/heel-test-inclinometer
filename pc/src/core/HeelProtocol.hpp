#pragma once
// Отчёт «Обработки» в печатной форме (Отчет_Кренование.xlsx, с 1.5.2):
//  - «Протокол» — первым: исходные данные (судно, дата опыта, D, P, единицы, датчики с названиями и знаком, метод
//    обработки, папка и файлы), положения (замеры) с креном по датчикам и средним ± СКО, табл. 6.5 (ΔM, Δθ по
//    приборам и среднее, h_i, учёт), проверки по Правилам РС ✓ / ✗ / — со ссылками на пункты, итог h (оба расчёта,
//    основной выделен, поправка Δh) и графики (контроль и запись с положениями) — картинками на листе;
//  - «Графики» — все картинки обработки;
//  - «Положения» / «Результаты», «Проверки», «Перемещения», «Сравнение постов», «Сырые_данные» — в той же сетке.
// Единая сетка: все столбцы всех листов одной ширины (kColWidth знаков), длинный текст — в объединённых ячейках с
// переносом; числа — с фиксированными знаками (углы 0.000°, h 0.000 м, моменты 0.000); рамки таблиц, заливка шапок;
// закреплённые строки; печать A4 по ширине страницы, повтор строк шапки.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "HeelReport.hpp"
#include "HeelRs.hpp"
#include "Xlsx.hpp"

namespace heel
{

inline constexpr double kColWidth = 10.5;  // ширина столбца, знаков (все листы)
inline constexpr int kColPx = 78;          // она же в пикселях (Calibri 11, 96 dpi) — для размеров картинок
inline constexpr int kGridCols = 10;       // столбцов протокола (A4 книжная)

// Картинка отчёта: подпись и PNG (размер — в пикселях файла).
struct ReportImage
{
    std::string caption;
    std::vector<unsigned char> png;
    int w = 0, h = 0;
    bool control = false; // график контроля (на «Протокол»)
    bool series = false;  // запись / замеры основного поста (на «Протокол»)
};

namespace proto
{

using xlsx::Cell;
using xlsx::Style;

inline Style S(bool bold = false, int fill = 0, bool border = false, bool wrap = false, int align = 0, std::string fmt = {})
{
    Style s;
    s.bold = bold;
    s.fill = fill;
    s.border = border;
    s.wrap = wrap;
    s.align = align;
    s.valign = 1;
    s.fmt = std::move(fmt);
    return s;
}

inline const char* kFmtAngle = "+0.000\"°\";-0.000\"°\";0.000\"°\"";
inline const char* kFmt3 = "0.000";
inline const char* kFmt4 = "0.0000";
inline const char* kFmt1 = "0.0";

inline int TextLines(const std::string& t, int span, bool bold);

// Сетка листа: строки по kGridCols (или больше) столбцов; ячейки и объединения с оформлением.
class Grid
{
public:
    Grid(xlsx::Sheet& sh, int cols) : sh_(sh), cols_(cols)
    {
        sh_.header = false;
        sh_.widths.assign(static_cast<std::size_t>(cols), kColWidth);
        sh_.gridLines = false;
    }
    int Cols() const { return cols_; }
    int NewRow(double height = 0)
    {
        sh_.rows.emplace_back();
        if (height > 0)
        {
            sh_.heights.resize(sh_.rows.size(), 0.0);
            sh_.heights.back() = height;
        }
        return static_cast<int>(sh_.rows.size()) - 1;
    }
    void Height(int r, double pt)
    {
        if (sh_.heights.size() <= static_cast<std::size_t>(r))
            sh_.heights.resize(static_cast<std::size_t>(r) + 1, 0.0);
        sh_.heights[static_cast<std::size_t>(r)] = pt;
    }
    // Ячейка (c0…c1 — объединение) со стилем; у объединённых — стиль во всех (рамка по всему прямоугольнику).
    void Put(int r, int c0, int c1, Cell v, const Style& st)
    {
        auto& row = sh_.rows[static_cast<std::size_t>(r)];
        if (static_cast<int>(row.size()) <= c1)
            row.resize(static_cast<std::size_t>(c1) + 1);
        // Excel не подбирает высоту строки по объединённым ячейкам: строк текста — оценкой по длине (кириллица и
        // жирный шрифт шире цифры «0», которой меряется ширина столбца)
        if (v.kind == Cell::Kind::Text && st.wrap)
        {
            const int lines = TextLines(v.text, c1 - c0 + 1, st.bold);
            if (lines > 1)
            {
                const double need = lines * 15.0 + 3.0;
                const double cur = static_cast<std::size_t>(r) < sh_.heights.size() ? sh_.heights[static_cast<std::size_t>(r)] : 0.0;
                if (need > cur)
                    Height(r, need);
            }
        }
        row[static_cast<std::size_t>(c0)] = xlsx::With(std::move(v), st);
        for (int c = c0 + 1; c <= c1; c++)
            row[static_cast<std::size_t>(c)] = xlsx::With(xlsx::Empty(), st);
        if (c1 > c0)
            sh_.merges.push_back({r, c0, r, c1});
    }
    void Title(const std::string& t, int size = 14)
    {
        const int r = NewRow(size > 12 ? 24 : 20);
        Style st = S(true);
        st.size = size;
        st.color = 4;
        Put(r, 0, cols_ - 1, xlsx::Text(t), st);
    }
    void Note(const std::string& t, double height = 0)
    {
        const int r = NewRow(height);
        Style st = S(false, 0, false, true);
        st.color = 3;
        st.italic = true;
        st.size = 9;
        Put(r, 0, cols_ - 1, xlsx::Text(t), st);
    }
    void Gap() { NewRow(8); }
    // «Подпись — значение»: подпись — столбцы 0…lw−1 (заливка), значение — до конца строки
    void KV(const std::string& label, Cell value, const Style& valueStyle = S(false, 0, true, true, 1), int lw = 4,
            double height = 0)
    {
        const int r = NewRow(height);
        Put(r, 0, lw - 1, xlsx::Text(label), S(false, 3, true, true, 1));
        Put(r, lw, cols_ - 1, std::move(value), valueStyle);
    }
    // Строка таблицы: ячейки с ширинами (в столбцах сетки)
    struct Col
    {
        Cell v;
        int span = 1;
        Style st;
    };
    int Row(const std::vector<Col>& cells, double height = 0)
    {
        const int r = NewRow(height);
        int c = 0;
        for (const Col& x : cells)
        {
            const int c1 = std::min(cols_ - 1, c + x.span - 1);
            Put(r, c, c1, x.v, x.st);
            c = c1 + 1;
        }
        return r;
    }
    // Картинка на всю ширину сетки (с подписью над ней); строки под неё пропускаются
    void Picture(const std::string& caption, const std::vector<unsigned char>& png, int w, int h)
    {
        if (png.empty() || w <= 0 || h <= 0)
            return;
        const int r0 = NewRow(18);
        Put(r0, 0, cols_ - 1, xlsx::Text(caption), S(true));
        const int width = cols_ * kColPx - 8;
        const int height = static_cast<int>(std::lround(static_cast<double>(h) * width / w));
        xlsx::Image im;
        im.png = png;
        im.col = 0;
        im.row = static_cast<int>(sh_.rows.size());
        im.dx = 4;
        im.dy = 2;
        im.width = width;
        im.height = height;
        im.name = caption;
        sh_.images.push_back(std::move(im));
        const int rows = height / 20 + 2; // строка по умолчанию — 15 пт = 20 пикселей
        for (int i = 0; i < rows; i++)
            NewRow();
    }

private:
    xlsx::Sheet& sh_;
    int cols_;
};

inline Style Th()
{
    Style st = S(true, 1, true, true, 2);
    st.size = 10;
    return st;
}

// Строк текста в ячейке шириной span столбцов (оценка: кириллица и жирный шрифт шире цифры «0»).
inline int TextLines(const std::string& t, int span, bool bold)
{
    const double perLine = span * kColWidth / (bold ? 1.25 : 1.12) - 1.0;
    int lines = 0;
    std::size_t at = 0;
    while (at <= t.size())
    {
        std::size_t end = t.find('\n', at);
        if (end == std::string::npos)
            end = t.size();
        const double len = static_cast<double>(detail::Utf8Len(t.substr(at, end - at)));
        lines += std::max(1, static_cast<int>(std::ceil(len / std::max(1.0, perLine))));
        at = end + 1;
    }
    return lines;
}
inline Style TdText(int align = 1) { return S(false, 0, true, true, align); }
inline Style TdNum(const char* fmt) { return S(false, 0, true, false, 3, fmt); }

inline Grid::Col C(Cell v, int span, const Style& st) { return {std::move(v), span, st}; }
inline Grid::Col H(const std::string& t, int span = 1) { return {xlsx::Text(t), span, Th()}; }
inline Grid::Col T(const std::string& t, int span = 1, int align = 1) { return {xlsx::Text(t), span, TdText(align)}; }
inline Grid::Col N(double v, const char* fmt, int span = 1) { return {xlsx::Num(v), span, TdNum(fmt)}; }
inline Grid::Col N(double v, const char* fmt, int span, const Style& base)
{
    Style st = base;
    st.fmt = fmt;
    st.border = true;
    st.align = 3;
    return {xlsx::Num(v), span, st};
}

inline std::string Mark(int st) { return st > 0 ? "✓" : st == 0 ? "✗" : "—"; }

// Таблица 6.5 и проверки по Правилам РС в сетку (для «Протокола» — кратко, для «Проверок» — с h_i по приборам).
inline void RsSections(Grid& g, const Settings& s, const RsCase& k, bool full)
{
    const rs::Result& r = k.r;
    const int G = g.Cols();
    const int np = static_cast<int>(k.postIds.size());
    const bool many = np > 1;
    // Табл. 6.5
    g.Title("Таблица 6.5 — метацентрическая высота по замерам (приращения)", 12);
    {
        std::vector<Grid::Col> h = {H("№"), H("Переход"),
                                    H(std::string("ΔM, ") + s.units.Mass() + "·" + s.units.Arm())};
        if (many)
            for (const int p : k.postIds)
                h.push_back(H("Δθ " + s.PostName(p)));
        h.push_back(H(many ? (k.in.instrument >= 0 ? "Δθ расч." : "Δθ ср.") : "Δθ"));
        h.push_back(H("Δθ, рад"));
        if (full && many)
            for (const int p : k.postIds)
                h.push_back(H("h_i " + s.PostName(p) + ", м"));
        h.push_back(H("h_i, м"));
        int used = 0;
        for (const auto& x : h)
            used += x.span;
        h.push_back(H("Учёт", std::max(1, G - used)));
        g.Row(h, 20);
    }
    const double toUser = 1.0 / (s.units.MassToT() * s.units.ArmToM()); // т·м -> единицы ввода (кг·мм у модели)
    for (std::size_t i = 0; i < r.m.size(); i++)
    {
        const rs::Meas& q = r.m[i];
        Style base = TdText(2);
        if (q.excluded && std::isfinite(q.h))
            base.color = 2;
        std::vector<Grid::Col> row = {C(xlsx::Num(static_cast<double>(i + 1), 0), 1, base),
                                      C(xlsx::Text(k.label[static_cast<std::size_t>(q.from)] + " → " +
                                                   k.label[static_cast<std::size_t>(q.to)]),
                                        1, base),
                                      N(q.dM * toUser, s.units.model ? kFmt1 : kFmt3, 1, base)};
        if (many)
            for (std::size_t j = 0; j < k.postIds.size(); j++)
                row.push_back(N(j < q.dThetaPost.size() ? q.dThetaPost[j] : kNaN, kFmtAngle, 1, base));
        row.push_back(N(q.dTheta, kFmtAngle, 1, base));
        row.push_back(N(q.dTheta * kPi / 180.0, "0.00000", 1, base));
        if (full && many)
            for (std::size_t j = 0; j < k.postIds.size(); j++)
                row.push_back(N(j < q.hPost.size() ? q.hPost[j] : kNaN, kFmt3, 1, base));
        row.push_back(N(q.h, kFmt3, 1, base));
        const std::string st = !std::isfinite(q.h) ? "нет (не перемещали)"
                               : q.autoExcluded    ? "исключён по 6.6.1"
                               : q.excluded        ? "исключён вручную"
                               : q.manual == 1     ? "учтён (вручную)"
                                                   : "учтён";
        const bool bad = std::isfinite(q.spread) && q.spread > k.c.spreadLimit;
        int used = 0;
        for (const auto& x : row)
            used += x.span;
        row.push_back(C(xlsx::Text(st + (bad ? "; приборы расходятся" : "")), std::max(1, G - used), base));
        g.Row(row);
    }
    // Проверки
    g.Gap();
    g.Title("Проверка по Правилам РС (Ч. IV 1.5; прил. 3 к разд. 2 Руководства)", 12);
    const int wReq = G >= 10 ? 5 : 4, wRef = 2;
    g.Row({H(""), H("Требование", wReq), H("Пункт", wRef), H("Опыт", G - 1 - wReq - wRef)}, 20);
    for (const rs::Check& c : k.checks)
    {
        Style ms = S(true, 0, true, false, 2);
        ms.color = c.state > 0 ? 1 : c.state == 0 ? 2 : 3;
        Style vs = TdText(1);
        if (c.state == 0)
            vs.color = 2;
        g.Row({C(xlsx::Text(Mark(c.state)), 1, ms), T(c.req, wReq), T(c.ref, wRef), C(xlsx::Text(c.value), G - 1 - wReq - wRef, vs)});
    }
}

// Итог h (оба расчёта, основной выделен) — KV-строки.
inline void SummaryRows(Grid& g, const Settings& s, const RsCase& k, const heel::Summary& sum, bool cont)
{
    const rs::Result& r = k.r;
    const bool byTable = s.RsMain() == 1;
    const bool dh = k.dh != 0.0;
    g.Title("Итог: метацентрическая высота", 12);
    auto val = [&](double v, bool main) {
        Style st = S(main, main ? 2 : 0, true, false, 1, "0.000\" м\"");
        return std::make_pair(xlsx::Num(v), st);
    };
    {
        auto [v, st] = val(dh ? sum.h - k.dh : sum.h, !byTable);
        g.KV(std::string(byTable ? "" : "ОСНОВНОЙ: ") + "от начального положения (среднее по " + std::to_string(sum.n) +
                 " точкам" + (cont ? ", положения" : ", замеры") + ")" + (dh ? ", исправленная (−Δh)" : ""),
             v, st, 6, 30);
    }
    {
        Style st = S(false, 0, true, false, 1, "0.000\" м\"");
        g.KV("по МНК (наклон прямой tg θ — P·l/D)" + std::string(dh ? ", без поправки" : ""), xlsx::Num(sum.ls.h), st, 6);
    }
    {
        Style st = S(false, 0, true, false, 1, "0.000\" м\"");
        g.KV("табл. 6.5: h_k = Σh_i / n, n = " + std::to_string(r.n), xlsx::Num(r.hk), st, 6);
    }
    {
        Style st = S(false, 0, true, true, 1);
        const std::string e = std::isfinite(r.err)
                                  ? detail::Num(r.err, 4) + " м ≤ " + detail::Num(r.errLimit, 4) + " м — " +
                                        (r.ok662 ? "выполнено" : "НЕ выполнено") + " (t_αn = " + detail::Num(r.tA, 1) + ")"
                                  : std::string("не считается: n вне 8…16");
        g.KV("вероятная ошибка (6.6.2)", xlsx::Text(e), st, 6);
    }
    {
        auto [v, st] = val(r.hFinal, byTable);
        std::string lab = std::string(byTable ? "ОСНОВНОЙ: " : "") +
                          (r.reduced ? "табл. 6.5, по 6.6.5: h_k − вероятная ошибка" : "табл. 6.5, итог");
        if (dh)
            lab += ", исправленная (−Δh)";
        g.KV(lab, v, st, 6, 30);
    }
    if (dh)
    {
        Style st = S(false, 0, true, false, 1, "0.000\" м\"");
        g.KV("поправка на свободные поверхности Δh", xlsx::Num(k.dh), st, 6);
    }
    if (k.tcN)
    {
        Style st = S(false, 0, true, true, 1);
        g.KV("период качки Tc (по записи)",
             xlsx::Text(detail::Num(k.tc, 1) + " с (" + detail::Num(k.tcMin, 1) + "…" + detail::Num(k.tcMax, 1) + ")"), st, 6);
    }
}

// Строки сетки: длинные таблицы прежних листов («Положения», «Сырые_данные», …) — та же ширина столбцов, шапка
// с заливкой и переносом, рамки, итоговые строки жирным, блок «подпись — значение» (после строки «Исходные данные»)
// — подпись в объединённых A:C, значение — дальше (текст — до конца строки с переносом).
inline void Restyle(xlsx::Sheet& sh, bool print = true, bool landscape = false)
{
    std::size_t cols = 0;
    for (const auto& r : sh.rows)
        cols = std::max(cols, r.size());
    cols = std::max<std::size_t>(cols, kGridCols);
    sh.widths.assign(cols, kColWidth);
    sh.print = print;
    sh.landscape = landscape;
    sh.printTitleRows = sh.header ? 1 : 0;
    sh.freezeRows = sh.header ? 1 : 0;
    // Таблица — до первой пустой строки; итоги — до следующей; дальше — «подпись — значение»
    std::size_t tableEnd = sh.rows.size(), totalEnd = sh.rows.size();
    for (std::size_t i = 1; i < sh.rows.size(); i++)
        if (sh.rows[i].empty())
        {
            tableEnd = i;
            break;
        }
    for (std::size_t i = tableEnd + 1; i < sh.rows.size(); i++)
        if (sh.rows[i].empty())
        {
            totalEnd = i;
            break;
        }
    auto styleRow = [&](std::vector<Cell>& row, const Style& text, const Style& num) {
        const std::size_t width = std::max(row.size(), std::size_t{0});
        for (std::size_t c = 0; c < width; c++)
        {
            Cell& cell = row[c];
            Style st = cell.kind == Cell::Kind::Number ? num : text;
            if (cell.kind == Cell::Kind::Number && cell.decimals >= 0)
                st.fmt = cell.decimals == 0 ? "0" : cell.decimals == 1 ? "0.0" : cell.decimals == 2 ? "0.00" : "0.000";
            cell = xlsx::With(cell, st);
        }
    };
    if (sh.header && !sh.rows.empty())
    {
        auto& h = sh.rows[0];
        h.resize(std::max(h.size(), std::size_t{1}));
        styleRow(h, Th(), Th());
        sh.heights.resize(1, 0.0);
        sh.heights[0] = 32;
    }
    const std::size_t hdrCols = sh.rows.empty() ? 0 : sh.rows[0].size();
    for (std::size_t i = sh.header ? 1 : 0; i < tableEnd; i++)
    {
        sh.rows[i].resize(std::max(sh.rows[i].size(), hdrCols));
        styleRow(sh.rows[i], S(false, 0, true, true, 1), S(false, 0, true, false, 3));
    }
    for (std::size_t i = tableEnd + 1; i < totalEnd && i < sh.rows.size(); i++)
        styleRow(sh.rows[i], S(true, 2, true, false, 1), S(true, 2, true, false, 3));
    for (std::size_t i = totalEnd + 1; i < sh.rows.size(); i++)
    {
        auto& row = sh.rows[i];
        if (row.empty())
            continue;
        if (row.size() == 1) // заголовок блока
        {
            Style st = S(true);
            st.color = 4;
            row[0] = xlsx::With(row[0], st);
            continue;
        }
        std::vector<Cell> nr(3);
        nr[0] = xlsx::With(row[0], S(false, 3, true, true, 1));
        nr[1] = xlsx::With(xlsx::Empty(), S(false, 3, true, true, 1));
        nr[2] = xlsx::With(xlsx::Empty(), S(false, 3, true, true, 1));
        const int r = static_cast<int>(i);
        sh.merges.push_back({r, 0, r, 2});
        const bool oneText = row.size() == 2 && row[1].kind == Cell::Kind::Text;
        for (std::size_t c = 1; c < row.size(); c++)
        {
            Style st = row[c].kind == Cell::Kind::Number ? S(false, 0, true, false, 1) : S(false, 0, true, true, 1);
            if (row[c].kind == Cell::Kind::Number && row[c].decimals >= 0)
                st.fmt = row[c].decimals == 0 ? "0" : row[c].decimals == 1 ? "0.0" : row[c].decimals == 2 ? "0.00" : "0.000";
            nr.push_back(xlsx::With(row[c], st));
        }
        if (oneText)
        {
            for (std::size_t c = nr.size(); c < cols; c++)
                nr.push_back(xlsx::With(xlsx::Empty(), S(false, 0, true, true, 1)));
            sh.merges.push_back({r, 3, r, static_cast<int>(cols) - 1});
        }
        // высота: подпись в трёх столбцах, текст значения — до конца строки (объединения Excel по высоте не подбирает)
        int lines = row[0].kind == Cell::Kind::Text ? TextLines(row[0].text, 3, false) : 1;
        if (oneText)
            lines = std::max(lines, TextLines(row[1].text, static_cast<int>(cols) - 3, false));
        if (lines > 1)
        {
            if (sh.heights.size() <= i)
                sh.heights.resize(i + 1, 0.0);
            sh.heights[i] = lines * 15.0 + 3.0;
        }
        row = std::move(nr);
    }
}

// Лист «Перемещения» (режим групп) в сетке: группы и плечи перемещений по шагам, ΔM.
inline xlsx::Sheet MovesSheet(const Settings& s, const RsCase& k, int G)
{
    xlsx::Sheet sh("Перемещения");
    Grid g(sh, std::max(G, 5 + static_cast<int>(s.rsGroups.size())));
    sh.freezeRows = 1;
    sh.print = true;
    sh.printTitleRows = 1;
    g.Title("Крен-балласт: группы и перемещения");
    g.Gap();
    g.Title("Группы", 12);
    g.Row({H("Группа"), H("Название", 3), H(std::string("Масса, ") + s.units.Mass(), 2)}, 20);
    for (std::size_t i = 0; i < s.rsGroups.size(); i++)
        g.Row({T(std::to_string(i + 1), 1, 2), T(s.rsGroups[i].name, 3), N(s.rsGroups[i].mass, kFmt3, 2)});
    g.Gap();
    g.Title(std::string("Перемещения: плечо со знаком (+ к правому борту), ") + s.units.Arm() + "; пусто — не двигали", 12);
    {
        std::vector<Grid::Col> h = {H("Шаг"), H("Положения")};
        for (const RsGroup& x : s.rsGroups)
            h.push_back(H(x.name.empty() ? std::string("группа") : x.name));
        h.push_back(H("ΔM, т·м"));
        h.push_back(H("Накопленный, т·м"));
        g.Row(h, 32);
    }
    double cum = 0;
    const bool mm = s.units.model;
    for (std::size_t i = 0; i < k.r.m.size(); i++)
    {
        const rs::Meas& q = k.r.m[i];
        std::vector<Grid::Col> row = {T(std::to_string(i + 1), 1, 2),
                                      T(k.label[static_cast<std::size_t>(q.from)] + " → " + k.label[static_cast<std::size_t>(q.to)], 1, 2)};
        for (std::size_t gi = 0; gi < s.rsGroups.size(); gi++)
        {
            const double l = i < s.rsMoves.size() && gi < s.rsMoves[i].size() ? s.rsMoves[i][gi] : 0.0;
            row.push_back(l != 0.0 && std::isfinite(l) ? N(l, mm ? kFmt1 : kFmt3) : T(""));
        }
        cum += std::isfinite(q.dM) ? q.dM : 0.0;
        row.push_back(N(q.dM, kFmt4));
        row.push_back(N(cum, kFmt4));
        g.Row(row);
    }
    return sh;
}

} // namespace proto

// Крен положения / замера по постам и сводные величины (для таблицы «Протокола»).
struct ProtoItem
{
    std::string label, place; // номер; участок «12.3–45.6» или файл / метка
    double arm = kNaN;        // единицы ввода
    double level[kMaxPosts] = {kNaN, kNaN, kNaN, kNaN};
    double sd = kNaN, dTheta = kNaN, h = kNaN;
    bool use = true, reference = false;
};

// Книга отчёта целиком. cont — «весь опыт одной записью» (rec — запись в ms, pos — положения), иначе «файл = замер».
inline std::vector<xlsx::Sheet> BuildBook(const Settings& s, const std::vector<File>& files, const std::vector<Measurement>& ms,
                                          bool cont, int rec, const std::vector<Position>& pos, const Summary& sum,
                                          const RsCase& k, const ReportInfo& info, const std::vector<ReportImage>& images,
                                          std::string* warning = nullptr)
{
    using namespace proto;
    std::vector<xlsx::Sheet> book;
    // Посты с файлами
    std::vector<int> posts;
    for (int p = 0; p < kMaxPosts; p++)
    {
        bool any = false;
        if (cont && rec >= 0 && rec < static_cast<int>(ms.size()))
            any = ms[static_cast<std::size_t>(rec)].post[p].file >= 0;
        else
            for (const Measurement& m : ms)
                any |= m.use && m.post[p].file >= 0;
        if (any)
            posts.push_back(p);
    }
    const int np = static_cast<int>(posts.size());
    const bool mm = s.units.model;
    const char* armFmt = mm ? kFmt1 : kFmt3;

    // ---------------- Протокол
    xlsx::Sheet pr("Протокол");
    const int G = std::max(kGridCols, 8 + np);
    {
        Grid g(pr, G);
        g.Title("Протокол обработки опыта кренования");
        g.Note(info.program + (info.when.empty() ? "" : " · обработано " + info.when));
        pr.freezeRows = 1;
        pr.print = true;
        pr.printTitleRows = 1;
        g.Gap();
        g.Title("Исходные данные", 12);
        // Дата и время опыта — по первой и последней строке файлов
        std::int32_t d0 = -1, s0 = -1, s1 = -1;
        std::string fileList;
        auto addFile = [&](int idx, int post) {
            if (idx < 0)
                return;
            const File& f = files[static_cast<std::size_t>(idx)];
            fileList += (fileList.empty() ? "" : ", ") + f.name + " (" + s.PostName(post) + ")";
            if (!f.log.rows.empty())
            {
                if (d0 < 0 && f.log.rows.front().day >= 0)
                    d0 = f.log.rows.front().day;
                if (f.log.rows.front().sec >= 0 && (s0 < 0 || f.log.rows.front().sec < s0))
                    s0 = f.log.rows.front().sec;
                if (f.log.rows.back().sec > s1)
                    s1 = f.log.rows.back().sec;
            }
        };
        if (cont && rec >= 0 && rec < static_cast<int>(ms.size()))
            for (const int p : posts)
                addFile(ms[static_cast<std::size_t>(rec)].post[p].file, p);
        else
            for (const Measurement& m : ms)
                if (m.use)
                    for (const int p : posts)
                        addFile(m.post[p].file, p);
        std::string when = d0 >= 0 ? csvlog::DateText(d0) : std::string("нет в файлах");
        if (s0 >= 0)
            when += ", " + csvlog::ClockText(s0) + (s1 > s0 ? " – " + csvlog::ClockText(s1) : std::string());
        g.KV("Судно", xlsx::Text(mm ? "малое (модель): D и P — кг, плечи — мм" : "большое: D и P — т, плечи — м"));
        g.KV("Дата и время опыта", xlsx::Text(when));
        g.KV(std::string("Водоизмещение D, ") + s.units.Mass(), xlsx::Num(s.D), S(false, 0, true, false, 1, mm ? "0.000" : "0.00"));
        if (s.rsGroupMode)
        {
            std::string gr;
            for (const RsGroup& x : s.rsGroups)
                gr += (gr.empty() ? "" : "; ") + x.name + " — " + detail::Num(x.mass, 3) + " " + s.units.Mass();
            g.KV("Крен-балласт (группы)", xlsx::Text(gr.empty() ? std::string("—") : gr));
        }
        else
            g.KV(std::string("Масса кренящего груза P, ") + s.units.Mass(), xlsx::Num(s.P),
                 S(false, 0, true, false, 1, mm ? "0.000" : "0.000"));
        std::string sensors;
        for (const int p : posts)
        {
            int key = -1;
            bool inv = false;
            auto look = [&](int idx) {
                if (idx >= 0 && key < 0)
                {
                    key = files[static_cast<std::size_t>(idx)].sensor;
                    inv = files[static_cast<std::size_t>(idx)].inverted;
                }
            };
            if (cont && rec >= 0 && rec < static_cast<int>(ms.size()))
                look(ms[static_cast<std::size_t>(rec)].post[p].file);
            else
                for (const Measurement& m : ms)
                    look(m.post[p].file);
            sensors += (sensors.empty() ? "" : "; ") + SensorShort(key) + " — «" + s.PostName(p) + "», знак " +
                       (inv ? "обратный (ось +X — к ЛБ)" : "прямой");
        }
        g.KV("Датчики (посты)", xlsx::Text(sensors), S(false, 0, true, true, 1), 4, np > 2 ? 30 : 0);
        g.KV("Крен", xlsx::Text(std::string("ось ") + (s.axis == 0 ? "X (CalcX)" : "Y (CalcY)") +
                               "; «+» — на правый борт; плечи со знаком: «+» — к правому борту"));
        if (s.armFrom != 0 && s.B > 0 && !s.rsGroupMode)
            g.KV("Плечи", xlsx::Text(std::string("вводились от ") + ArmFromName(s.armFrom) +
                                    ": x — расстояние от борта до центра груза; ширина B = " +
                                    detail::Num(s.B, mm ? 1 : 3) + " " + s.units.Arm() + "; плечо от ДП l = " +
                                    (s.armFrom == 2 ? "B/2 − x" : "x − B/2")),
                 S(false, 0, true, true, 1), 4, 30);
        g.KV("Режим", xlsx::Text(cont ? "весь опыт одной записью: положения груза — участки установившегося крена (не короче " +
                                           detail::Num(s.minPlateauS, 0) + " с, перепад от " + detail::Num(s.minStepDeg, 2) + "°)"
                                     : "файл = замер: крен — среднее за последние " + detail::Num(s.windowS, 0) + " с файла"),
             S(false, 0, true, true, 1), 4, 30);
        g.KV("Обработка сигнала", xlsx::Text(filt::Describe(s.filter)), S(false, 0, true, true, 1), 4, 30);
        if (!info.folder.empty())
            g.KV("Папка", xlsx::Text(info.folder), S(false, 0, true, true, 1));
        g.KV("Файлы", xlsx::Text(fileList), S(false, 0, true, true, 1), 4, fileList.size() > 90 ? 30 : 0);

        // Положения / замеры
        std::vector<ProtoItem> items;
        if (cont)
            for (std::size_t i = 0; i < pos.size(); i++)
            {
                const Position& p = pos[i];
                ProtoItem it;
                it.label = std::to_string(i + 1);
                it.place = detail::Num(std::min(p.t0, p.t1), 1) + "–" + detail::Num(std::max(p.t0, p.t1), 1);
                it.arm = p.arm;
                it.use = p.use;
                double sdS = 0, dS = 0, hS = 0;
                int sdN = 0, dN = 0, hN = 0;
                for (const int q : posts)
                {
                    it.level[q] = p.Level(q);
                    if (std::isfinite(p.st[q].sd))
                        sdS += p.st[q].sd, sdN++;
                    if (std::isfinite(p.res[q].dTheta))
                        dS += p.res[q].dTheta, dN++;
                    if (p.res[q].counted)
                        hS += p.res[q].h, hN++;
                    it.reference |= p.res[q].reference;
                }
                it.sd = sdN ? sdS / sdN : kNaN;
                it.dTheta = dN ? dS / dN : kNaN;
                it.h = hN ? hS / hN : kNaN;
                items.push_back(it);
            }
        else
            for (const Measurement& m : ms)
            {
                ProtoItem it;
                it.label = m.label.empty() ? std::to_string(m.number) : m.label;
                it.arm = std::isfinite(m.arm) ? m.arm : s.L;
                it.use = m.use;
                double sdS = 0, hS = 0;
                int sdN = 0, hN = 0;
                for (const int q : posts)
                    if (m.post[q].file >= 0)
                    {
                        it.level[q] = m.post[q].st.mean;
                        if (std::isfinite(m.post[q].st.sd))
                            sdS += m.post[q].st.sd, sdN++;
                        if (m.post[q].res.counted)
                            hS += m.post[q].res.h, hN++;
                        if (it.place.empty())
                            it.place = files[static_cast<std::size_t>(m.post[q].file)].name;
                    }
                it.sd = sdN ? sdS / sdN : kNaN;
                it.h = hN ? hS / hN : kNaN;
                items.push_back(it);
            }
        g.Gap();
        g.Title(cont ? "Положения груза" : "Замеры", 12);
        const bool side = s.armFrom != 0 && s.B > 0 && !s.rsGroupMode; // плечи вводились от борта — оба столбца
        {
            std::vector<Grid::Col> h = {H("№"), H(cont ? "Участок, с" : "Файл", 2)};
            if (side)
                h.push_back(H(std::string("От ") + ArmFromName(s.armFrom) + " x, " + s.units.Arm()));
            h.push_back(H(std::string(side ? "Плечо l от ДП, " : "Плечо l, ") + s.units.Arm()));
            for (const int p : posts)
                h.push_back(H("Крен " + s.PostName(p)));
            h.push_back(H(np > 1 ? "Крен, среднее" : "Крен"));
            h.push_back(H("± СКО (качка)"));
            if (cont)
                h.push_back(H("Δθ от нач."));
            int used = 0;
            for (const auto& x : h)
                used += x.span;
            h.push_back(H("h, м", std::max(1, G - used)));
            g.Row(h, 20);
        }
        for (const ProtoItem& it : items)
        {
            Style base = TdText(2);
            if (!it.use)
                base.color = 3;
            std::vector<Grid::Col> row = {C(xlsx::Text(it.label), 1, base), C(xlsx::Text(it.place), 2, base)};
            if (side)
                row.push_back(std::isfinite(it.arm) ? N(SideFromArm(it.arm, s.armFrom, s.B), armFmt, 1, base)
                                                    : C(xlsx::Text("нач."), 1, base));
            if (it.reference && !std::isfinite(it.arm))
                row.push_back(C(xlsx::Text("нач."), 1, base));
            else
                row.push_back(N(it.arm, armFmt, 1, base));
            double sum2 = 0;
            int n2 = 0;
            for (const int p : posts)
            {
                row.push_back(N(it.level[p], kFmtAngle, 1, base));
                if (std::isfinite(it.level[p]))
                    sum2 += it.level[p], n2++;
            }
            row.push_back(N(n2 ? sum2 / n2 : kNaN, kFmtAngle, 1, base));
            row.push_back(N(it.sd, "0.000\"°\"", 1, base));
            if (cont)
                row.push_back(it.reference ? C(xlsx::Text("0 (нач.)"), 1, base) : N(it.dTheta, kFmtAngle, 1, base));
            int used = 0;
            for (const auto& x : row)
                used += x.span;
            row.push_back(it.use ? N(it.h, kFmt3, std::max(1, G - used), base)
                                 : C(xlsx::Text("не в расчёте"), std::max(1, G - used), base));
            g.Row(row);
        }
        g.Note("Крен — по методу обработки (уровень положения), СКО — исходных отсчётов на участке (качка); h — среднее по "
               "датчикам, h = P·l / (D·tg|Δθ|).",
               24);
        g.Gap();
        if (!k.r.m.empty())
        {
            RsSections(g, s, k, false);
            g.Gap();
        }
        SummaryRows(g, s, k, sum, cont);
        // Графики — картинками
        for (const ReportImage& im : images)
            if (im.control)
            {
                g.Gap();
                g.Picture(im.caption, im.png, im.w, im.h);
            }
        for (const ReportImage& im : images)
            if (im.series)
            {
                g.Gap();
                g.Picture(im.caption, im.png, im.w, im.h);
                break;
            }
    }
    book.push_back(std::move(pr));

    // ---------------- Графики
    if (!images.empty())
    {
        xlsx::Sheet gr("Графики");
        Grid g(gr, G);
        g.Title("Графики обработки");
        gr.freezeRows = 1;
        gr.print = true;
        for (const ReportImage& im : images)
        {
            g.Gap();
            g.Picture(im.caption, im.png, im.w, im.h);
        }
        book.push_back(std::move(gr));
    }

    // ---------------- Таблицы (прежние листы) в той же сетке
    std::vector<xlsx::Sheet> tables =
        cont && rec >= 0 && rec < static_cast<int>(ms.size())
            ? BuildPositionsReport(s, files, ms[static_cast<std::size_t>(rec)], pos, sum, info, warning)
            : BuildReport(s, files, ms, sum, info, warning);
    // Проверки — в сетке протокола, полностью (h_i по приборам)
    if (!k.r.m.empty())
    {
        xlsx::Sheet ch("Проверки");
        Grid g(ch, std::max(G, 9 + 2 * np));
        g.Title("Проверка опыта кренования по Правилам РС");
        ch.freezeRows = 1;
        ch.print = true;
        ch.landscape = np > 2;
        ch.printTitleRows = 1;
        g.Gap();
        SummaryRows(g, s, k, sum, cont);
        g.KV("Δθ табл. 6.5", xlsx::Text(k.in.instrument >= 0 ? "по одному прибору" : "среднее приращений приборов"), S(false, 0, true, true, 1), 6);
        g.KV("Сверка приборов: наибольшее расхождение приращений",
             std::isfinite(k.r.maxSpread) ? xlsx::Text(detail::Num(k.r.maxSpread, 3) + "° (замер " +
                                                       std::to_string(k.r.maxSpreadAt + 1) + "), порог " +
                                                       detail::Num(k.c.spreadLimit, 2) + "°")
                                          : xlsx::Text("один прибор"),
             S(false, 0, true, true, 1), 6);
        g.Gap();
        RsSections(g, s, k, true);
        tables.insert(tables.begin() + 1, std::move(ch));
        if (k.groupMode)
            tables.insert(tables.begin() + 2, MovesSheet(s, k, G));
    }
    for (xlsx::Sheet& t : tables)
        if (t.name != "Проверки" && t.name != "Перемещения")
            Restyle(t, true, t.rows.empty() ? false : t.rows.front().size() > 12);
    // Сырые данные в печать не включаются: сверху — строка-пояснение, область печати — только она (в Excel нет
    // флага «не печатать лист»: при «Напечатать всю книгу» лист займёт одну страницу с пояснением)
    for (xlsx::Sheet& t : tables)
        if (t.name == "Сырые_данные")
        {
            const std::size_t cols = std::max<std::size_t>(t.widths.size(), 1);
            std::vector<Cell> note(cols);
            Style st = S(false, 3, false, true, 1);
            st.italic = true;
            st.color = 3;
            for (std::size_t c = 0; c < cols; c++)
                note[c] = xlsx::With(c ? xlsx::Empty()
                                       : xlsx::Text("Сырые данные — все строки файлов в расчёте; в печать не включаются "
                                                    "(область печати — эта строка)."),
                                     st);
            t.rows.insert(t.rows.begin(), note);
            for (auto& m : t.merges)
            {
                m.r0++;
                m.r1++;
            }
            t.merges.push_back({0, 0, 0, static_cast<int>(cols) - 1});
            t.heights.insert(t.heights.begin(), 20.0);
            // шапка таблицы — теперь вторая строка (её стиль уже задан Restyle)
            t.header = false;
            t.freezeRows = 2;
            t.printTitleRows = 0;
            t.footer = false;
            t.landscape = false;
            t.printArea = "A1:" + xlsx::detail::ColName(cols - 1) + "1";
        }
    for (xlsx::Sheet& t : tables)
        book.push_back(std::move(t));
    return book;
}

} // namespace heel
