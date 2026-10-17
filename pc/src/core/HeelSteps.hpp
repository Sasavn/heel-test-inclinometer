#pragma once
// Расчёт по шагам: исходные данные → положения и уровни → h от начального (+ МНК) → табл. 6.5 (ΔM, Δθ по приборам,
// h_i) → h_k → 6.6.1 … 6.6.5 → поправка Δh → итог. Каждый шаг — формула, подстановка чисел, результат и пункт
// Инструктивных указаний по кренованию (прил. 3 к разд. 2 Руководства) / Части IV Правил. Числа берутся из уже
// посчитанного (Summary, RsCase) — шаги только расписывают их; окна «Подробнее» на карточках-показателях «Обработки»
// и лист «Расчёт по шагам» полного отчёта строятся отсюда одинаково.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Filters.hpp"
#include "HeelData.hpp"
#include "HeelRs.hpp"

namespace steps
{

struct Row
{
    std::string what;    // величина
    std::string formula; // формула
    std::string subst;   // подстановка чисел
    std::string result;  // результат
    std::string ref;     // пункт Правил
};

struct Table
{
    std::vector<std::string> head;
    std::vector<std::vector<std::string>> rows;
};

struct Section
{
    std::string id;    // input, levels, initial, ls, t65, hk, 661, 662, 663, 664, 665, dh, result
    std::string title;
    std::string ref;   // пункт Правил раздела
    std::vector<Row> rows;
    Table table;       // построчные значения (положения, точки, замеры) — до строк rows
    std::string note;
};

using Steps = std::vector<Section>;

inline std::string F(double v, int digits)
{
    if (!std::isfinite(v))
        return "—";
    char b[64];
    std::snprintf(b, sizeof(b), "%.*f", digits, v);
    return b;
}

inline std::string Fs(double v, int digits) // со знаком
{
    if (!std::isfinite(v))
        return "—";
    char b[64];
    std::snprintf(b, sizeof(b), "%+.*f", digits, v);
    return b;
}

// Число в подстановке: отрицательное — в скобках («0,0535·(−0,23296)»)
inline std::string Pa(double v, int digits)
{
    const std::string t = F(v, digits);
    return std::isfinite(v) && v < 0 ? "(" + t + ")" : t;
}

inline const Section* Find(const Steps& st, const std::string& id)
{
    for (const Section& s : st)
        if (s.id == id)
            return &s;
    return nullptr;
}

} // namespace steps

namespace heel
{

// Шаги расчёта. cont — «весь опыт одной записью» (pos, rec), иначе — «файл = замер» (meas). sum — h от начального и
// МНК (у записи — по положениям), k — табл. 6.5 и проверки (HeelRs.hpp).
inline steps::Steps BuildSteps(const Settings& s, const std::vector<File>& files, const std::vector<Measurement>& meas,
                               bool cont, int rec, const std::vector<Position>& pos, const Summary& sum, const RsCase& k)
{
    using namespace steps;
    Steps out;
    const bool mm = s.units.model;
    const double D = s.units.ToTonnes(s.D), P = s.units.ToTonnes(s.P);
    const std::string mu = s.units.Mass(), au = s.units.Arm();
    const int armDig = mm ? 1 : 3;
    std::vector<int> posts;
    if (cont && rec >= 0 && rec < static_cast<int>(meas.size()))
    {
        for (int p = 0; p < kMaxPosts; p++)
            if (meas[static_cast<std::size_t>(rec)].post[p].file >= 0)
                posts.push_back(p);
    }
    else
        for (int p = 0; p < kMaxPosts; p++)
        {
            bool any = false;
            for (const Measurement& m : meas)
                any |= m.post[p].file >= 0;
            if (any)
                posts.push_back(p);
        }

    // 1. Исходные данные
    {
        Section sec{"input", "Исходные данные", "", {}, {}, ""};
        sec.rows.push_back({"Водоизмещение при креновании Δ", "", F(s.D, mm ? 3 : 1) + " " + mu, F(D, 4) + " т", "разд. 6 прил. 3"});
        if (s.rsGroupMode)
        {
            std::string g;
            for (const RsGroup& x : s.rsGroups)
                g += (g.empty() ? "" : "; ") + x.name + " — " + F(x.mass, 3) + " " + mu;
            sec.rows.push_back({"Крен-балласт (группы)", "ΔM_i = ∑ m_g·l_ig", g.empty() ? "—" : g, "", "табл. 6.5, п. 5.4"});
        }
        else
            sec.rows.push_back({"Масса крен-балласта P", "", F(s.P, mm ? 4 : 3) + " " + mu, F(P, 6) + " т", "табл. 6.5"});
        std::string arms = "плечо от начального положения со знаком («+» — к правому борту)";
        if (s.armFrom != 0 && s.B > 0 && !s.rsGroupMode)
            arms = std::string("вводились от ") + ArmFromName(s.armFrom) + " (x — до центра груза), B = " + F(s.B, armDig) +
                   " " + au + ": l = " + (s.armFrom == 2 ? "B/2 − x" : "x − B/2");
        sec.rows.push_back({"Плечи", "", arms, "", ""});
        sec.rows.push_back({"Крен", "ось " + std::string(s.axis == 0 ? "X (CalcX)" : "Y (CalcY)"), "«+» — на правый борт", "", ""});
        std::string ps;
        for (const int p : posts)
            ps += (ps.empty() ? "" : ", ") + s.PostName(p);
        sec.rows.push_back({"Приборы (посты)", "", ps, std::to_string(posts.size()), "разд. 3 прил. 3 (не менее 2)"});
        sec.rows.push_back({"Уровень положения", filt::MethodName(s.filter.method), filt::Describe(s.filter), "",
                            "прил. 3-10, 3-13: ось колебаний по записи перебежки"});
        sec.rows.push_back({"Окно среднего", "", filt::WindowName(s.filter.window), "", ""});
        out.push_back(sec);
    }

    // 2. Положения (замеры) и уровни
    {
        Section sec{"levels", cont ? "Положения груза и уровни крена" : "Замеры и уровни крена", "", {}, {}, ""};
        sec.table.head = {"№", cont ? "Участок, с" : "Файл / замер", cont ? "Окно среднего, с" : "Окно, с",
                          std::string("Плечо, ") + au};
        for (const int p : posts)
            sec.table.head.push_back("θ " + s.PostName(p) + ", °");
        sec.table.head.push_back("В расчёте");
        if (cont)
            for (std::size_t i = 0; i < pos.size(); i++)
            {
                const Position& q = pos[i];
                std::string win = "—";
                for (const int p : posts)
                    if (std::isfinite(q.est[p].winT0))
                    {
                        win = F(q.est[p].winT0, 1) + "…" + F(q.est[p].winT1, 1);
                        break;
                    }
                std::vector<std::string> row = {std::to_string(i + 1), F(std::min(q.t0, q.t1), 1) + "…" + F(std::max(q.t0, q.t1), 1),
                                                win, std::isfinite(q.arm) ? Fs(q.arm, armDig) : "L = " + F(s.L, armDig)};
                for (const int p : posts)
                    row.push_back(Fs(q.Level(p), 3));
                row.push_back(q.use ? "да" : "нет");
                sec.table.rows.push_back(row);
            }
        else
            for (const Measurement& m : meas)
            {
                std::vector<std::string> row = {m.label.empty() ? std::to_string(m.number) : m.label, "", "посл. " + F(s.windowS, 0) + " с",
                                                std::isfinite(m.arm) ? Fs(m.arm, armDig) : "L = " + F(s.L, armDig)};
                for (const int p : posts)
                {
                    row.push_back(Fs(m.post[p].st.mean, 3));
                    if (row[1].empty() && m.post[p].file >= 0)
                        row[1] = files[static_cast<std::size_t>(m.post[p].file)].name;
                }
                row.push_back(m.use ? "да" : "нет");
                sec.table.rows.push_back(row);
            }
        sec.note = cont ? "θ — уровень крена положения по методу обработки на окне среднего (прибор — свой ноль)."
                        : "θ — среднее крена за последние " + F(s.windowS, 0) + " с файла замера.";
        out.push_back(sec);
    }

    // 3. h от начального положения и МНК
    {
        Section sec{"initial", "h от начального положения", "", {}, {}, ""};
        sec.table.head = {"Положение", "Прибор", "Δθ, °", "l, м", "h = P·l / (Δ·tg|Δθ|)", "h, м", "Примечание"};
        double refArm = 0.0;
        if (cont)
            for (const Position& q : pos)
                if (q.use)
                {
                    refArm = std::isfinite(q.arm) ? q.arm : 0.0;
                    break;
                }
        auto point = [&](const std::string& label, int p, const PointResult& r, double thetaDeg, double armM) {
            std::vector<std::string> row = {label, s.PostName(p), Fs(thetaDeg, 3), Fs(armM, 4)};
            if (r.counted)
            {
                row.push_back(F(P, 6) + "·" + F(std::fabs(armM), 4) + " / (" + F(D, 4) + "·tg " + F(std::fabs(thetaDeg), 3) + "°)");
                row.push_back(F(r.h, 4));
                row.push_back("");
            }
            else
            {
                row.push_back("");
                row.push_back("—");
                row.push_back(r.note);
            }
            sec.table.rows.push_back(row);
        };
        if (cont)
        {
            for (std::size_t i = 0; i < pos.size(); i++)
                for (const int p : posts)
                {
                    const Position& q = pos[i];
                    if (q.res[p].reference)
                        continue;
                    const double armM = s.units.ToMetres((std::isfinite(q.arm) ? q.arm : s.L) - refArm);
                    point(std::to_string(i + 1), p, q.res[p], q.res[p].dTheta, armM);
                }
        }
        else
            for (const Measurement& m : meas)
                for (const int p : posts)
                    if (m.post[p].file >= 0)
                        point(m.label.empty() ? std::to_string(m.number) : m.label, p, m.post[p].res, m.post[p].st.mean,
                              s.units.ToMetres(std::isfinite(m.arm) ? m.arm : s.L));
        double sumH = 0;
        int n = 0;
        auto addH = [&](const PointResult& r) {
            if (r.counted && std::isfinite(r.h))
            {
                sumH += r.h;
                n++;
            }
        };
        if (cont)
        {
            for (const Position& q : pos)
                for (const int p : posts)
                    addH(q.res[p]);
        }
        else
            for (const Measurement& m : meas)
                for (const int p : posts)
                    addH(m.post[p].res);
        sec.rows.push_back({"Среднее h", "h = ∑h / n", F(sumH, 4) + " / " + std::to_string(n), F(sum.h, 4) + " м", "1.5 ч. IV"});
        sec.rows.push_back({"СКО h", "√(∑(h − h_ср)² / (n − 1))", "n = " + std::to_string(sum.n), F(sum.sd, 4) + " м", ""});
        out.push_back(sec);
        Section ls{"ls", "График контроля и МНК", "", {}, {}, ""};
        ls.rows.push_back({"Прямая МНК", "tg Δθ = k·(P·l/Δ) + b", "точек " + std::to_string(sum.ls.n),
                           "k = " + F(sum.ls.k, 4) + ", b = " + F(sum.ls.b, 5), "п. 5.5 прил. 3"});
        ls.rows.push_back({"h по МНК", "h = 1 / k", "1 / " + F(sum.ls.k, 4), F(sum.ls.h, 4) + " м", ""});
        ls.rows.push_back({"R²", "", "", F(sum.ls.r2, 5), "п. 5.5: точки — на прямой"});
        out.push_back(ls);
    }

    // 4. Табл. 6.5: ΔM, Δθ по приборам, h_i
    const rs::Result& r = k.r;
    {
        Section sec{"t65", "Табл. 6.5: замеры по приращениям", "табл. 6.5 прил. 3", {}, {}, ""};
        sec.table.head = {"Замер i", "Положения", "ΔM_i, т·м"};
        for (std::size_t j = 0; j < k.postIds.size(); j++)
            sec.table.head.push_back("Δθ " + s.PostName(k.postIds[j]) + ", °");
        sec.table.head.push_back("Δθ_i, °");
        sec.table.head.push_back("h_i = ΔM_i / (Δ·Δθ_i)");
        sec.table.head.push_back("h_i, м");
        sec.table.head.push_back("Учёт");
        for (std::size_t i = 0; i < r.m.size(); i++)
        {
            const rs::Meas& q = r.m[i];
            const std::string from = q.from < static_cast<int>(k.label.size()) ? k.label[static_cast<std::size_t>(q.from)] : std::to_string(q.from);
            const std::string to = q.to < static_cast<int>(k.label.size()) ? k.label[static_cast<std::size_t>(q.to)] : std::to_string(q.to);
            std::vector<std::string> row = {std::to_string(i + 1), from + " → " + to};
            if (!k.groupMode && q.to < static_cast<int>(k.in.arm.size()))
                row.push_back(F(P, 6) + "·(" + F(k.in.arm[static_cast<std::size_t>(q.to)], 4) + " − " +
                              Pa(k.in.arm[static_cast<std::size_t>(q.from)], 4) + ") = " + F(q.dM, 6));
            else
                row.push_back(Fs(q.dM, 6));
            for (std::size_t j = 0; j < k.postIds.size(); j++)
                row.push_back(j < q.dThetaPost.size() ? Fs(q.dThetaPost[j], 3) : "—");
            row.push_back(Fs(q.dTheta, 3));
            row.push_back(std::isfinite(q.h) ? F(q.dM, 6) + " / (" + F(D, 4) + "·" + Pa(q.dTheta * rs::kPi / 180.0, 5) + ")" : "");
            row.push_back(F(q.h, 4));
            row.push_back(q.autoExcluded ? "исключён (6.6.1)" : q.excluded ? (q.manual == 0 ? "исключён вручную" : "—") : "да");
            sec.table.rows.push_back(row);
        }
        sec.note = std::string("Δθ_i — ") +
                   (k.in.instrument >= 0 ? "по прибору «" + s.PostName(k.postIds[static_cast<std::size_t>(k.in.instrument)]) + "»"
                                         : std::string("среднее приращений приборов")) +
                   "; Δθ в радианах = Δθ° · π / 180.";
        out.push_back(sec);
    }
    // 5. h_k
    {
        double sh = 0;
        std::string list;
        for (const rs::Meas& q : r.m)
            if (!q.excluded && std::isfinite(q.h))
            {
                sh += q.h;
                list += (list.empty() ? "" : " + ") + F(q.h, 4);
            }
        Section sec{"hk", "Метацентрическая высота по табл. 6.5", "6.6 прил. 3", {}, {}, ""};
        sec.rows.push_back({"h_k", "h_k = ∑h_i / n", "(" + list + ") / " + std::to_string(r.n) + " = " + F(sh, 4) + " / " + std::to_string(r.n),
                            F(r.hk, 4) + " м", "6.6 прил. 3"});
        out.push_back(sec);
    }
    // 6. 6.6.1
    {
        Section sec{"661", "6.6.1: отклонения замеров", "6.6.1 прил. 3", {}, {}, ""};
        sec.rows.push_back({"∑(h_i − h_k)²", "", "по " + std::to_string(r.n) + " замерам", F(r.sumSq, 7) + " м²", ""});
        sec.rows.push_back({"σ", "σ = √(∑(h_i − h_k)² / (n − 1))", "√(" + F(r.sumSq, 7) + " / " + std::to_string(r.n - 1) + ")",
                            F(r.sigma, 4) + " м", "6.6.1"});
        sec.rows.push_back({"2σ", "", "2·" + F(r.sigma, 4), F(2 * r.sigma, 4) + " м", ""});
        sec.table.head = {"Замер i", "h_i, м", "|h_i − h_k|, м", "≤ 2σ"};
        for (std::size_t i = 0; i < r.m.size(); i++)
        {
            const rs::Meas& q = r.m[i];
            if (!std::isfinite(q.h))
                continue;
            const double d = std::fabs(q.h - r.hk);
            sec.table.rows.push_back({std::to_string(i + 1), F(q.h, 4), F(d, 4),
                                      q.autoExcluded ? "нет — исключён" : q.excluded ? "исключён вручную" : d <= 2 * r.sigma + 1e-12 ? "да" : "нет"});
        }
        sec.rows.push_back({"Итог 6.6.1", "|h_i − h_k| ≤ 2σ; исключается не более одного замера", "исключено " + std::to_string(r.excludedCount),
                            r.ok661 ? "выполнено" : "не выполнено", "6.6.1"});
        out.push_back(sec);
    }
    // 7. 6.6.2
    {
        Section sec{"662", "6.6.2: вероятная ошибка", "6.6.2 прил. 3", {}, {}, ""};
        sec.rows.push_back({"t_αn", "табл. 6.6.2 (n = 8…16)", "n = " + std::to_string(r.n), F(r.tA, 1), "табл. 6.6.2"});
        sec.rows.push_back({"Вероятная ошибка", "t_αn·√(∑(h_i − h_k)² / (n(n − 1)))",
                            F(r.tA, 1) + "·√(" + F(r.sumSq, 7) + " / (" + std::to_string(r.n) + "·" + std::to_string(r.n - 1) + "))",
                            F(r.err, 4) + " м", "6.6.2"});
        const bool big = r.hk > 2.0;
        sec.rows.push_back({"Допустимая", big ? "0,01·(4 + h_k)" : "0,02·(1 + h_k)",
                            big ? "0,01·(4 + " + F(r.hk, 4) + ")" : "0,02·(1 + " + F(r.hk, 4) + ")", F(r.errLimit, 4) + " м", "6.6.2"});
        sec.rows.push_back({"Итог 6.6.2", "ошибка ≤ допустимой", F(r.err, 4) + (r.ok662 ? " ≤ " : " > ") + F(r.errLimit, 4),
                            std::isfinite(r.err) ? (r.ok662 ? "выполнено" : "не выполнено") : "не считается (n вне 8…16)", "6.6.2"});
        out.push_back(sec);
    }
    // 8. 6.6.3
    {
        Section sec{"663", "6.6.3: ошибка для порожнего судна", "6.6.3 прил. 3", {}, {}, ""};
        if (r.ok663 < 0)
            sec.rows.push_back({"ε", "min(0,05·h; 0,10·l_max), не менее 0,04 м", "нужны h и l_max по Информации об остойчивости", "нет данных", "6.6.3"});
        else
        {
            sec.rows.push_back({"ε", "min(0,05·h; 0,10·l_max), не менее 0,04 м",
                                "h = " + F(k.in.hInfo, 3) + " м, l_max = " + F(k.in.lMax, 3) + " м", F(r.eps, 4) + " м", "6.6.3"});
            sec.rows.push_back({"Ошибка · Δ0/Δ", "", F(r.err, 4) + "·" + F(r.Dratio, 4), F(r.err663, 4) + " м", "6.6.3"});
            sec.rows.push_back({"Итог 6.6.3", "ошибка · Δ0/Δ ≤ ε", "", r.ok663 ? "выполнено" : "не выполнено", "6.6.3"});
        }
        out.push_back(sec);
    }
    // 9. 6.6.4
    {
        Section sec{"664", "6.6.4: число замеров", "6.6.4 прил. 3", {}, {}, ""};
        sec.rows.push_back({"Удовлетворительных замеров", "n ≥ 8", "n = " + std::to_string(r.n), r.ok664 ? "выполнено" : "не выполнено", "6.6.4"});
        out.push_back(sec);
    }
    // 10. 6.6.5
    {
        Section sec{"665", "6.6.5: если требования не выполнены", "6.6.5 прил. 3", {}, {}, ""};
        if (r.reduced)
            sec.rows.push_back({"h в расчёт", "h_k − вероятная ошибка", F(r.hk, 4) + " − " + F(r.err, 4), F(r.hFinalRaw, 4) + " м", "6.6.5"});
        else
            sec.rows.push_back({"h в расчёт", "h_k (6.6.1–6.6.4 выполнены)", "", F(r.hFinalRaw, 4) + " м", "6.6.5"});
        out.push_back(sec);
    }
    // 11. Поправка на свободные поверхности
    {
        Section sec{"dh", "Поправка на свободные поверхности", "", {}, {}, ""};
        sec.rows.push_back({"h исправленная", "h − Δh", F(r.hFinalRaw, 4) + " − " + F(r.dh, 4), F(r.hFinal, 4) + " м", "1.5 ч. IV"});
        out.push_back(sec);
    }
    // 12. Итог
    {
        Section sec{"result", "Итог", "", {}, {}, ""};
        const bool byTable = s.RsMain() == 1;
        sec.rows.push_back({"Основной расчёт", byTable ? "по табл. 6.5 (h_k с 6.6.5 и Δh)" : "от начального положения − Δh",
                            byTable ? F(r.hFinal, 4) : F(k.hCurrent, 4) + " − " + F(k.dh, 4), F(k.hMain, 4) + " м", ""});
        sec.rows.push_back({"Второй расчёт", byTable ? "от начального положения − Δh" : "по табл. 6.5 (h_k с 6.6.5 и Δh)", "",
                            F(byTable ? k.hCurrent - k.dh : r.hFinal, 4) + " м", ""});
        out.push_back(sec);
    }
    return out;
}

} // namespace heel
