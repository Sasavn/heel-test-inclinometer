#pragma once
// Отчёт «Обработки» — книга Excel Отчет_Кренование.xlsx (как у BWM427_Analyzer):
//  - «Результаты»: строка на замер и пост (файл, пост, замер, плечо l, средний крен, СКО, точек, h, АКБ, примечание),
//    итоговое среднее h, оценка по МНК, исходные данные (D, P, L, окно, ось, порог);
//  - «Сырые_данные»: все строки файлов замеров в расчёте + замер и пост, числа — числами.
// Непрерывная запись (BuildPositionsReport):
//  - «Положения»: строка на положение груза и пост (начало, конец, длительность, крен ± СКО, Δθ от начального, l, h,
//    примечание), итоговое среднее h, МНК, исходные данные (D, P, L, ось, порог, параметры поиска положений);
//  - «Сырые_данные»: все строки записи + номер положения, в которое попала строка.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "HeelData.hpp"
#include "HeelRs.hpp"
#include "Xlsx.hpp"

namespace heel
{

inline constexpr const char* kReportName = "Отчет_Кренование.xlsx";
inline constexpr const char* kControlPlotName = "График_контроля.png"; // кренящий момент – tg θ (Правила РС, п. 5.5)
// Имя картинки поста: «График_Нос.png», «Положения_Мидель.png» (знаки, недопустимые в имени файла, — «_»).
inline std::string PlotFileName(const char* prefix, const std::string& post)
{
    std::string s = std::string(prefix) + "_";
    for (const char c : post)
        s += (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
              static_cast<unsigned char>(c) < 0x20)
                 ? '_'
                 : c;
    return s + ".png";
}
// Строк в «Сырые_данные» не больше (у Excel предел 1 048 576; книга собирается в памяти — у 32-битной программы
// на сотни тысяч строк её может не хватить).
inline constexpr std::size_t kRawMaxRows = 300000;

struct ReportInfo
{
    std::string folder;  // папка с файлами
    std::string program; // «Регистратор крена — программа для ПК 1.1»
    std::string when;    // время обработки
};

inline const char* AxisName(int axis)
{
    return axis == 0 ? "X (CalcX)" : "Y (CalcY)";
}

namespace detail
{

inline std::size_t Utf8Len(const std::string& s)
{
    std::size_t n = 0;
    for (const char c : s)
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return n;
}

inline std::string Num(double v, int dec)
{
    if (!std::isfinite(v))
        return "—";
    char b[48];
    std::snprintf(b, sizeof(b), "%.*f", dec, v);
    return b;
}

// Ширины столбцов по содержимому (как в старой программе: длина + 2), от 6 до 60 знаков.
inline void AutoWidths(xlsx::Sheet& sh)
{
    sh.widths.clear();
    for (const auto& row : sh.rows)
        for (std::size_t c = 0; c < row.size(); c++)
        {
            const xlsx::Cell& cell = row[c];
            std::size_t len = 0;
            if (cell.kind == xlsx::Cell::Kind::Text)
                len = Utf8Len(cell.text);
            else if (cell.kind == xlsx::Cell::Kind::Number)
                len = Num(cell.num, cell.decimals < 0 ? 3 : cell.decimals).size();
            if (sh.widths.size() <= c)
                sh.widths.resize(c + 1, 6.0);
            sh.widths[c] = std::min(60.0, std::max(sh.widths[c], static_cast<double>(len) + 2.0));
        }
}

} // namespace detail

// Текст про МНК для отчёта и интерфейса.
inline std::string FitText(const Fit& f)
{
    if (f.n == 0 || !std::isfinite(f.k))
        return "нет учтённых точек";
    char b[256];
    if (f.throughOrigin)
        std::snprintf(b, sizeof(b), "tg θ = k·(P·l/D), прямая через ноль (у всех точек одно плечо): k = %.4f, точек %d",
                      f.k, f.n);
    else
        std::snprintf(b, sizeof(b), "tg θ = k·(P·l/D) + b: k = %.4f, b = %.5f (начальный крен %.3f°), R² = %.4f, точек %d",
                      f.k, f.b, std::atan(f.b) * 180.0 / kPi, f.r2, f.n);
    return b;
}

inline std::vector<xlsx::Sheet> BuildReport(const Settings& s, const std::vector<File>& files,
                                            const std::vector<Measurement>& ms, const Summary& sum,
                                            const ReportInfo& info, std::string* warning = nullptr)
{
    using xlsx::Num;
    using xlsx::Text;
    xlsx::Sheet res("Результаты");
    const std::string armU = std::string("Плечо l (") + s.units.Arm() + ")";
    res.AddRow({Text("Файл"), Text("Пост"), Text("Замер"), Text(armU), Text("Средний крен (°)"), Text("СКО (°)"),
                Text("Точек"), Text("h (м)"), Text("АКБ (В)"), Text("Примечание")});
    for (const Measurement& m : ms)
    {
        if (!m.use)
            continue;
        for (int p = 0; p < kMaxPosts; p++)
        {
            const PostData& pd = m.post[p];
            if (pd.file < 0)
                continue;
            const File& f = files[static_cast<std::size_t>(pd.file)];
            std::string note = pd.res.counted ? std::string() : pd.res.note;
            if (pd.st.whole)
                note += std::string(note.empty() ? "" : "; ") + "файл короче окна — среднее по всему файлу";
            res.AddRow({Text(f.name), Text(s.PostName(p)), m.number > kPcRunBase ? Text(m.label) : Num(m.number, 0),
                        Num(std::isfinite(m.arm) ? m.arm : s.L, s.units.model ? 1 : 3), Num(pd.st.mean, 3), Num(pd.st.sd, 3),
                        Num(pd.st.n, 0), pd.res.counted ? Num(pd.res.h, 3) : xlsx::Empty(), Num(pd.batV, 1), Text(note)});
        }
    }
    res.AddRow(std::vector<xlsx::Cell>{});
    res.AddRow({Text("ИТОГОВОЕ СРЕДНЕЕ ЗНАЧЕНИЕ:"), Text("—"), Text("—"), xlsx::Empty(), xlsx::Empty(), xlsx::Empty(),
                Num(sum.n, 0), Num(sum.h, 3), xlsx::Empty(),
                Text(sum.n ? "среднее h по " + std::to_string(sum.n) + " значениям (замер × пост)" +
                                 (std::isfinite(sum.sd) ? ", СКО " + detail::Num(sum.sd, 3) + " м" : std::string())
                           : std::string("нет учтённых значений"))});
    res.AddRow({Text("По МНК:"), Text("—"), Text("—"), xlsx::Empty(), xlsx::Empty(), xlsx::Empty(), Num(sum.ls.n, 0),
                Num(sum.ls.h, 3), xlsx::Empty(), Text(FitText(sum.ls))});
    detail::AutoWidths(res); // по таблице; подписи исходных данных ниже — в широкий столбец A
    res.AddRow(std::vector<xlsx::Cell>{});
    res.AddRow({Text("Исходные данные")});
    res.AddRow({Text("Судно"), Text(s.units.model ? "малое (модель): D и P — кг, плечи — мм" : "большое: D и P — т, плечи — м")});
    res.AddRow({Text(std::string("Водоизмещение D (") + s.units.Mass() + ")"), Num(s.D, s.units.model ? 3 : 1)});
    res.AddRow({Text(std::string("Масса кренящего груза P (") + s.units.Mass() + ")"), Num(s.P, s.units.model ? 4 : 3)});
    res.AddRow({Text(std::string("Плечо переноса L по умолчанию (") + s.units.Arm() + ")"), Num(s.L, s.units.model ? 1 : 3)});
    res.AddRow({Text("Крен — среднее за последние (с)"), Num(s.windowS, 0)});
    res.AddRow({Text("Ось угла"), Text(AxisName(s.axis))});
    res.AddRow({Text("Порог крена |θ| (°)"), Num(s.thresholdDeg, 2)});
    res.AddRow({Text("Формула"), Text("h = P·l / (D·tg|θ|); |θ| не больше порога — не учитывается")});
    if (!info.folder.empty())
        res.AddRow({Text("Папка"), Text(info.folder)});
    if (!info.when.empty())
        res.AddRow({Text("Обработано"), Text(info.when)});
    if (!info.program.empty())
        res.AddRow({Text("Программа"), Text(info.program)});
    res.widths[0] = std::max(res.widths[0], 34.0);

    xlsx::Sheet raw("Сырые_данные");
    raw.AddRow({Text("Замер"), Text("Пост"), Text("Датчик"), Text("Date"), Text("Time"), Text("t (с)"), Text("RawX"),
                Text("RawY"), Text("OffsetX"), Text("OffsetY"), Text("CalcX"), Text("CalcY"), Text("BatV")});
    raw.widths = {8, 8, 9, 12, 10, 10, 9, 9, 9, 9, 9, 9, 7};
    std::size_t skipped = 0;
    for (const Measurement& m : ms)
    {
        if (!m.use)
            continue;
        for (int p = 0; p < kMaxPosts; p++)
        {
            const PostData& pd = m.post[p];
            if (pd.file < 0)
                continue;
            const File& f = files[static_cast<std::size_t>(pd.file)];
            const std::string sensor = SensorName(f.sensor);
            for (const csvlog::Row& r : f.log.rows)
            {
                if (raw.rows.size() > kRawMaxRows)
                {
                    skipped++;
                    continue;
                }
                raw.AddRow({m.number > kPcRunBase ? Text(m.label) : Num(m.number, 0), Text(s.PostName(p)), Text(sensor),
                            r.day >= 0 ? Text(csvlog::DateText(r.day)) : xlsx::Empty(),
                            r.sec >= 0 ? Text(csvlog::ClockText(r.sec)) : xlsx::Empty(), Num(r.t, 3), Num(r.rawX, 2), Num(r.rawY, 2), Num(r.offX, 3),
                            Num(r.offY, 3), Num(r.calcX, 3), Num(r.calcY, 3), Num(r.batV, 1)});
            }
        }
    }
    if (skipped && warning)
        *warning = "в листе «Сырые_данные» не поместилось строк: " + std::to_string(skipped) + " (предел программы — " +
                   std::to_string(kRawMaxRows) + ")";
    // Посты (датчики заменяют вески) — сравнение между собой, если их больше одного
    {
        std::vector<int> used;
        for (int p = 0; p < kMaxPosts; p++)
            for (const Measurement& m : ms)
                if (m.use && m.post[p].file >= 0 && std::find(used.begin(), used.end(), p) == used.end())
                    used.push_back(p);
        std::sort(used.begin(), used.end());
        if (used.size() > 1)
        {
            xlsx::Sheet cmp("Сравнение постов");
            std::vector<xlsx::Cell> h = {Text("Замер")};
            for (const int p : used)
                h.push_back(Text("Крен " + s.PostName(p) + " (°)"));
            h.push_back(Text("Расхождение (°)"));
            cmp.AddRow(h);
            for (const Measurement& m : ms)
            {
                if (!m.use)
                    continue;
                std::vector<xlsx::Cell> r = {m.number > kPcRunBase ? Text(m.label) : Num(m.number, 0)};
                for (const int p : used)
                    r.push_back(m.post[p].file >= 0 ? Num(m.post[p].st.mean, 3) : xlsx::Empty());
                r.push_back(Num(m.Spread(), 3));
                cmp.AddRow(r);
            }
            detail::AutoWidths(cmp);
            return {std::move(res), std::move(raw), std::move(cmp)};
        }
    }
    return {std::move(res), std::move(raw)};
}

inline std::vector<xlsx::Sheet> BuildPositionsReport(const Settings& s, const std::vector<File>& files,
                                                     const Measurement& m, const std::vector<Position>& pos,
                                                     const Summary& sum, const ReportInfo& info,
                                                     std::string* warning = nullptr)
{
    using xlsx::Num;
    using xlsx::Text;
    xlsx::Sheet res("Положения");
    const bool mm = s.units.model;
    const std::size_t cols = mm ? 16 : 15, cH = 13, cNote = cols - 1;
    {
        std::vector<xlsx::Cell> h = {Text("Положение"), Text("В расчёте"), Text("Начало (с)"), Text("Конец (с)"),
                                     Text("Длительность (с)"), Text("Пост"), Text("Крен, среднее (°)"), Text("СКО (°)"),
                                     Text("Точек"), Text("Крен по методу (°)"), Text("Δθ (°)"),
                                     Text(std::string("Плечо l (") + s.units.Arm() + ")"), Text("Окно метода (с)"),
                                     Text("h (м)")};
        if (mm)
            h.push_back(Text("h (мм)"));
        h.push_back(Text("Примечание"));
        res.AddRow(h);
    }
    for (std::size_t i = 0; i < pos.size(); i++)
    {
        const Position& p = pos[i];
        for (int post = 0; post < kMaxPosts; post++)
        {
            if (m.post[post].file < 0)
                continue;
            const PointResult& r = p.res[post];
            const filt::Estimate& e = p.est[post];
            const double t0 = std::min(p.t0, p.t1), t1 = std::max(p.t0, p.t1);
            std::vector<xlsx::Cell> row = {
                Num(static_cast<double>(i + 1), 0), Text(p.use ? "да" : "нет"), Num(t0, 1), Num(t1, 1), Num(t1 - t0, 1),
                Text(s.PostName(post)), Num(p.st[post].mean, 3), Num(p.st[post].sd, 3), Num(p.st[post].n, 0),
                Num(p.Level(post), 3), Num(r.dTheta, 3),
                r.reference ? xlsx::Empty() : Num(std::isfinite(p.arm) ? p.arm : s.L, mm ? 1 : 3),
                std::isfinite(e.winT0) ? Text(detail::Num(e.winT0, 1) + "…" + detail::Num(e.winT1, 1)) : xlsx::Empty(),
                r.counted ? Num(r.h, 3) : xlsx::Empty()};
            if (mm)
                row.push_back(r.counted ? Num(r.h * 1000.0, 1) : xlsx::Empty());
            row.push_back(Text(r.counted ? std::string() : r.note));
            res.AddRow(row);
        }
    }
    res.AddRow(std::vector<xlsx::Cell>{});
    std::vector<xlsx::Cell> total(cols, xlsx::Empty());
    total[0] = Text("ИТОГОВОЕ СРЕДНЕЕ ЗНАЧЕНИЕ:");
    total[8] = Num(sum.n, 0);
    total[cH] = Num(sum.h, 3);
    if (mm)
        total[cH + 1] = Num(sum.h * 1000.0, 1);
    total[cNote] = Text(sum.n ? "среднее h по " + std::to_string(sum.n) + " значениям (положение × пост)" +
                                    (std::isfinite(sum.sd) ? ", СКО " + detail::Num(sum.sd, 3) + " м" : std::string())
                              : std::string("нет учтённых значений"));
    res.AddRow(total);
    std::vector<xlsx::Cell> ls(cols, xlsx::Empty());
    ls[0] = Text("По МНК:");
    ls[8] = Num(sum.ls.n, 0);
    ls[cH] = Num(sum.ls.h, 3);
    if (mm)
        ls[cH + 1] = Num(sum.ls.h * 1000.0, 1);
    ls[cNote] = Text(FitText(sum.ls));
    res.AddRow(ls);
    detail::AutoWidths(res);
    res.AddRow(std::vector<xlsx::Cell>{});
    res.AddRow({Text("Исходные данные")});
    std::string files1;
    for (int post = 0; post < kMaxPosts; post++)
        if (m.post[post].file >= 0)
            files1 += (files1.empty() ? "" : ", ") + files[static_cast<std::size_t>(m.post[post].file)].name + " (" +
                      s.PostName(post) + ")";
    res.AddRow({Text("Запись (весь опыт одним файлом)"), Text(files1)});
    res.AddRow({Text("Судно"), Text(s.units.model ? "малое (модель): D и P — кг, плечи — мм" : "большое: D и P — т, плечи — м")});
    res.AddRow({Text(std::string("Водоизмещение D (") + s.units.Mass() + ")"), Num(s.D, s.units.model ? 3 : 1)});
    res.AddRow({Text(std::string("Масса кренящего груза P (") + s.units.Mass() + ")"), Num(s.P, s.units.model ? 4 : 3)});
    res.AddRow({Text(std::string("Плечо переноса L по умолчанию (") + s.units.Arm() + ")"), Num(s.L, s.units.model ? 1 : 3)});
    res.AddRow({Text("Ось угла"), Text(AxisName(s.axis))});
    res.AddRow({Text("Порог |Δθ| (°)"), Num(s.thresholdDeg, 2)});
    res.AddRow({Text("Обработка сигнала положения"), Text(filt::Describe(s.filter))});
    res.AddRow({Text("Положение не короче (с)"), Num(s.minPlateauS, 0)});
    res.AddRow({Text("Перепад между положениями не меньше (°)"), Num(s.minStepDeg, 2)});
    res.AddRow({Text("Формула"), Text("Δθ = крен положения − крен начального (первого учтённого); h = P·l / (D·tg|Δθ|); "
                                      "|Δθ| не больше порога — не учитывается; МНК: tg Δθ = k·(P·l/D) + b, h = 1/k")});
    if (!info.folder.empty())
        res.AddRow({Text("Папка"), Text(info.folder)});
    if (!info.when.empty())
        res.AddRow({Text("Обработано"), Text(info.when)});
    if (!info.program.empty())
        res.AddRow({Text("Программа"), Text(info.program)});
    res.widths[0] = std::max(res.widths[0], 40.0);

    xlsx::Sheet raw("Сырые_данные");
    raw.AddRow({Text("Положение"), Text("Пост"), Text("Датчик"), Text("Date"), Text("Time"), Text("t (с)"), Text("RawX"),
                Text("RawY"), Text("OffsetX"), Text("OffsetY"), Text("CalcX"), Text("CalcY"), Text("BatV")});
    raw.widths = {10, 8, 9, 12, 10, 10, 9, 9, 9, 9, 9, 9, 7};
    std::size_t skipped = 0;
    for (int post = 0; post < kMaxPosts; post++)
    {
        if (m.post[post].file < 0)
            continue;
        const File& f = files[static_cast<std::size_t>(m.post[post].file)];
        const std::string sensor = SensorName(f.sensor);
        for (const csvlog::Row& r : f.log.rows)
        {
            if (raw.rows.size() > kRawMaxRows)
            {
                skipped++;
                continue;
            }
            int in = 0;
            for (std::size_t i = 0; i < pos.size() && !in; i++)
                if (r.t >= std::min(pos[i].t0, pos[i].t1) && r.t <= std::max(pos[i].t0, pos[i].t1))
                    in = static_cast<int>(i) + 1;
            raw.AddRow({in ? Num(in, 0) : xlsx::Empty(), Text(s.PostName(post)), Text(sensor),
                        r.day >= 0 ? Text(csvlog::DateText(r.day)) : xlsx::Empty(),
                        r.sec >= 0 ? Text(csvlog::ClockText(r.sec)) : xlsx::Empty(), Num(r.t, 3), Num(r.rawX, 2),
                        Num(r.rawY, 2), Num(r.offX, 3), Num(r.offY, 3), Num(r.calcX, 3), Num(r.calcY, 3), Num(r.batV, 1)});
        }
    }
    if (skipped && warning)
        *warning = "в листе «Сырые_данные» не поместилось строк: " + std::to_string(skipped) + " (предел программы — " +
                   std::to_string(kRawMaxRows) + ")";
    // Посты (датчики заменяют вески) — сравнение между собой, если их больше одного
    {
        std::vector<int> used;
        for (int p = 0; p < kMaxPosts; p++)
            if (m.post[p].file >= 0)
                used.push_back(p);
        if (used.size() > 1)
        {
            xlsx::Sheet cmp("Сравнение постов");
            std::vector<xlsx::Cell> h = {Text("Положение"), Text("Начало (с)"), Text("Конец (с)")};
            for (const int p : used)
                h.push_back(Text("Крен " + s.PostName(p) + " (°)"));
            h.push_back(Text("Расхождение (°)"));
            cmp.AddRow(h);
            for (std::size_t i = 0; i < pos.size(); i++)
            {
                const Position& q = pos[i];
                std::vector<xlsx::Cell> r = {Num(static_cast<double>(i + 1), 0), Num(std::min(q.t0, q.t1), 1),
                                             Num(std::max(q.t0, q.t1), 1)};
                for (const int p : used)
                    r.push_back(Num(q.Level(p), 3));
                r.push_back(Num(q.Spread(), 3));
                cmp.AddRow(r);
            }
            detail::AutoWidths(cmp);
            return {std::move(res), std::move(raw), std::move(cmp)};
        }
    }
    return {std::move(res), std::move(raw)};
}

// Лист «Проверки» (Правила РС): оба расчёта h, оценка качества 6.6, требования (✓ / ✗ / —), таблица 6.5, период качки.
inline xlsx::Sheet BuildRsSheet(const Settings& s, const RsCase& k)
{
    using xlsx::Num;
    using xlsx::Text;
    xlsx::Sheet sh("Проверки");
    const rs::Result& r = k.r;
    auto yes = [](int st) { return st > 0 ? "✓" : st == 0 ? "✗" : "—"; };
    sh.AddRow({Text("Проверка опыта кренования по Правилам РС (Ч. IV 1.5; прил. 3 к разд. 2 Руководства, табл. 6.5, п. 6.6)")});
    sh.AddRow(std::vector<xlsx::Cell>{});
    sh.AddRow({Text("Метацентрическая высота"), Text("Значение (м)"), Text("Примечание")});
    sh.AddRow({Text("От начального положения + МНК (среднее по точкам)"), Num(k.hCurrent, 3),
               Text(s.rsMain == 0 ? "основной расчёт" : "")});
    sh.AddRow({Text("По приращениям, табл. 6.5: h_k"), Num(r.hk, 3), Text(r.note)});
    sh.AddRow({Text(r.reduced ? "Итог по 6.6.5: h_k − вероятная ошибка" : "Итог по табл. 6.5 (качество 6.6 выполнено)"),
               Num(r.hFinal, 3),
               Text(std::string(s.rsMain == 1 ? "основной расчёт" : "") +
                    (r.reduced ? std::string(s.rsMain == 1 ? "; " : "") + "условия 6.6 не выполнены — h_k уменьшена" : ""))});
    sh.AddRow({Text("Замеров в h_k, n"), Num(r.n, 0)});
    sh.AddRow({Text("σ = √(Σ(h_i − h_k)²/(n − 1))"), Num(r.sigma, 4)});
    sh.AddRow({Text("t_αn (табл. 6.6.2)"), Num(r.tA, 1)});
    sh.AddRow({Text("Вероятная ошибка t_αn·√(Σ(h_i − h_k)²/(n(n − 1)))"), Num(r.err, 4)});
    sh.AddRow({Text("Допустимая ошибка 6.6.2"), Num(r.errLimit, 4), Text(r.hk > 2 ? "0,01(4 + h_k)" : "0,02(1 + h_k)")});
    sh.AddRow({Text("ε (6.6.3)"), Num(r.eps, 3),
               Text(r.ok663 < 0 ? "нужны h и l_max по Информации об остойчивости" : "min(0,05h; 0,10 l_max), не менее 0,04 м")});
    sh.AddRow({Text("Период бортовой качки Tc (оценка по записи), с"), Num(k.tc, 1),
               Text(k.tcN ? "медиана по " + std::to_string(k.tcN) + " участкам, от " + detail::Num(k.tcMin, 1) + " до " +
                                detail::Num(k.tcMax, 1) + " с"
                          : std::string("колебаний не видно"))});
    sh.AddRow({Text("Плечи"), Text(k.legacy ? "введены без знака — знак по крену относительно начального"
                                            : "со знаком: + к правому борту")});
    if (k.virtualZero)
        sh.AddRow({Text("Начальное положение"), Text("условное: крен 0 (ноль датчика), плечо 0")});
    sh.AddRow(std::vector<xlsx::Cell>{});
    sh.AddRow({Text("Требование"), Text("Пункт"), Text("Значение"), Text("Выполнено")});
    for (const rs::Check& c : k.checks)
        sh.AddRow({Text(c.req), Text(c.ref), Text(c.value), Text(yes(c.state))});
    sh.AddRow(std::vector<xlsx::Cell>{});
    sh.AddRow({Text("Таблица 6.5: замер"), Text("Положения"), Text("ΔM (т·м)"), Text("Δθ (°)"), Text("Δθ (рад)"),
               Text("h_i (м)"), Text("h_i − h_k (м)"), Text("(h_i − h_k)²"), Text("В расчёте")});
    for (std::size_t i = 0; i < r.m.size(); i++)
    {
        const rs::Meas& q = r.m[i];
        const bool ok = !q.excluded && std::isfinite(q.h);
        const std::string st = !std::isfinite(q.h) ? "нет (нет перемещения или крена)"
                               : q.autoExcluded    ? "нет — исключён по 6.6.1 (|h_i − h_k| > 2σ)"
                               : q.excluded        ? "нет — исключён вручную"
                               : q.manual == 1     ? "да (вручную)"
                                                   : "да";
        const double d = std::isfinite(q.h) && std::isfinite(r.hk) ? q.h - r.hk : kNaN;
        sh.AddRow({Num(static_cast<double>(i + 1), 0),
                   Text(k.label[static_cast<std::size_t>(q.from)] + " → " + k.label[static_cast<std::size_t>(q.to)]),
                   Num(q.dM, 4), Num(q.dTheta, 3), Num(q.dTheta * kPi / 180.0, 5), Num(q.h, 4), ok ? Num(d, 4) : xlsx::Empty(),
                   ok ? Num(d * d, 6) : xlsx::Empty(), Text(st)});
    }
    detail::AutoWidths(sh);
    sh.widths[0] = std::min(60.0, std::max(sh.widths[0], 46.0));
    return sh;
}

} // namespace heel

