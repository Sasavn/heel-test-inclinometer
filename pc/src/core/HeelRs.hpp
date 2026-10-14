#pragma once
// Проверка опыта кренования по Правилам РС на данных «Обработки» (RsCheck.hpp — формулы и требования):
//  - вход табл. 6.5 — положения груза в порядке опыта: у записи «весь опыт одним файлом» — учтённые положения (первое
//    — начальное), у «файл = замер» — учтённые замеры по номеру (если у первого плечо не 0 — впереди условное
//    начальное положение: крен 0, плечо 0; в режиме групп его нет — первый замер и есть начальное положение);
//  - крен — по каждому прибору (посту с файлом); Δθ замера — среднее приращений приборов или приращение выбранного
//    (Settings::rsInstrument); сверка приборов — по расхождению их приращений на каждом замере;
//  - момент: одна масса P и плечо от начального (плечи со знаком, + к правому борту; если ни одного отрицательного —
//    старый ввод модулем: знак по крену относительно начального) или группы крен-балласта и перемещения
//    (Settings::rsGroupMode): ΔM_i = Σ m_g·l_ig;
//  - поправка на свободные поверхности Δh (h исправленная = h − Δh), Δ наихудшего варианта для 6.6.3;
//  - график контроля (п. 5.5): кренящий момент и tg(θ − θ0) по положениям, МНК по неисключённым;
//  - условия разд. 3: начальный крен, крен на борта, число приборов, сверка приборов; период качки Tc — по переходам
//    через медиану (Filters.hpp, DominantOmega) на каждом положении, медиана.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "HeelData.hpp"
#include "RsCheck.hpp"

namespace heel
{

struct RsCase
{
    rs::Input in;
    rs::Result r;
    rs::Conditions c;
    std::vector<rs::Check> checks;
    std::vector<std::string> label;  // по положениям входа: «1», «2», … («0» — условное начальное)
    std::vector<int> postIds;        // приборы входа (Input::post) — номера постов
    std::vector<double> M, tanT;     // график контроля: момент (единицы массы × плеча) и tg(θ − θ0)
    std::vector<bool> excluded;      // положение — конец исключённого замера (на графике — красным)
    Fit fit;                         // МНК tg(θ − θ0) = k·M + b по неисключённым
    bool legacy = false;             // плечи без знака — знак по крену
    bool virtualZero = false;        // «файл = замер»: условное начальное положение
    bool groupMode = false;          // момент — по группам и перемещениям
    int mainPost = -1;               // первый прибор (для подписей); расчёт — по Settings::rsInstrument
    double tc = kNaN, tcMin = kNaN, tcMax = kNaN; // период качки, с: медиана и разброс по положениям
    int tcN = 0;
    double hMain = kNaN;             // h основного расчёта (Settings::RsMain), с поправкой Δh
    double hCurrent = kNaN;          // h «от начального + МНК» (среднее по точкам), без поправки
    double dh = 0.0;                 // поправка на свободные поверхности, м
};

// Группы строкой для настроек: «название=масса;…» (знаки «=» и «;» в названии заменяются запятой).
inline std::string GroupsText(const std::vector<RsGroup>& g)
{
    std::string out;
    for (const RsGroup& x : g)
    {
        std::string n = x.name;
        for (char& c : n)
            if (c == '=' || c == ';')
                c = ',';
        char b[48];
        std::snprintf(b, sizeof(b), "%.12g", x.mass);
        out += (out.empty() ? "" : ";") + n + "=" + b;
    }
    return out;
}

inline std::vector<RsGroup> ParseGroups(const std::string& text)
{
    std::vector<RsGroup> out;
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find(';', at);
        if (end == std::string::npos)
            end = text.size();
        const std::string item = text.substr(at, end - at);
        const auto eq = item.rfind('=');
        if (eq != std::string::npos)
        {
            const double m = std::strtod(item.c_str() + eq + 1, nullptr);
            if (std::isfinite(m) && m >= 0)
                out.push_back({item.substr(0, eq), m});
        }
        at = end + 1;
    }
    return out;
}

// Приращения кренящего момента по шагам (группы и перемещения), т·м: steps шагов, нет строки — 0.
inline std::vector<double> MovesMoment(const Settings& s, std::size_t steps)
{
    std::vector<double> dM(steps, 0.0);
    for (std::size_t i = 0; i < steps && i < s.rsMoves.size(); i++)
        for (std::size_t g = 0; g < s.rsGroups.size() && g < s.rsMoves[i].size(); g++)
            if (std::isfinite(s.rsMoves[i][g]))
                dM[i] += s.units.ToTonnes(s.rsGroups[g].mass) * s.units.ToMetres(s.rsMoves[i][g]);
    return dM;
}

// Эквивалентные плечи положений (единицы плеча) для расчёта «от начального» в режиме групп: накопленный момент / P.
// n — положений в порядке опыта (первое — начальное, 0).
inline std::vector<double> GroupArms(const Settings& s, std::size_t n)
{
    std::vector<double> arm(n, 0.0);
    const double P = s.units.ToTonnes(s.P);
    if (n == 0 || !(P > 0))
        return arm;
    const std::vector<double> dM = MovesMoment(s, n - 1);
    double m = 0;
    for (std::size_t i = 1; i < n; i++)
    {
        m += dM[i - 1];
        arm[i] = m / P / s.units.ArmToM();
    }
    return arm;
}

namespace detail
{

inline double Median(std::vector<double> v)
{
    if (v.empty())
        return kNaN;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Период качки на участке [t0, t1] ряда; NaN — не видно.
inline double PeriodOn(const std::vector<double>& t, const std::vector<double>& v, double t0, double t1)
{
    std::vector<double> st, sv;
    for (std::size_t i = 0; i < t.size() && i < v.size(); i++)
        if (t[i] >= t0 && t[i] <= t1 && std::isfinite(v[i]))
        {
            st.push_back(t[i]);
            sv.push_back(v[i]);
        }
    const double w = filt::DominantOmega(st, sv);
    return std::isfinite(w) && w > 0 ? 2 * kPi / w : kNaN;
}

// Крен положений одним рядом (для подписей, знака старых плеч, графика контроля): выбранный прибор или начальное —
// среднее приборов, дальше — накопленные средние приращения (как Δθ табл. 6.5).
inline std::vector<double> CombinedTheta(const std::vector<std::vector<double>>& post, int instrument, std::size_t n)
{
    std::vector<double> th(n, kNaN);
    if (instrument >= 0 && static_cast<std::size_t>(instrument) < post.size())
    {
        for (std::size_t i = 0; i < n && i < post[static_cast<std::size_t>(instrument)].size(); i++)
            th[i] = post[static_cast<std::size_t>(instrument)][i];
        return th;
    }
    for (std::size_t i = 0; i < n; i++)
    {
        double s = 0;
        int c = 0;
        for (const auto& p : post)
            if (i == 0 ? std::isfinite(p[0]) : std::isfinite(p[i]) && std::isfinite(p[i - 1]))
            {
                s += i == 0 ? p[0] : p[i] - p[i - 1];
                c++;
            }
        th[i] = i == 0 ? (c ? s / c : kNaN) : th[i - 1] + (c ? s / c : kNaN);
    }
    return th;
}

// Общая часть: крен одним рядом, плечи (знак) или моменты групп, Evaluate, график контроля, условия, требования.
inline void RsFinish(const Settings& s, RsCase& k, const std::vector<double>& rawArm, double hCurrent,
                     std::vector<double> periods)
{
    rs::Input& in = k.in;
    const std::size_t n = in.number.size();
    in.instrument = -1;
    for (std::size_t i = 0; i < k.postIds.size(); i++)
        if (k.postIds[i] == s.rsInstrument)
            in.instrument = static_cast<int>(i);
    in.theta = CombinedTheta(in.post, in.instrument, n);
    const double th0 = n ? in.theta[0] : kNaN;
    in.D = s.units.ToTonnes(s.D);
    in.P = s.units.ToTonnes(s.P);
    k.groupMode = s.rsGroupMode;
    in.arm.assign(n, 0.0);
    if (k.groupMode)
    {
        in.dM = MovesMoment(s, n ? n - 1 : 0);
        double m = 0;
        for (std::size_t i = 1; i < n; i++)
        {
            m += in.dM[i - 1];
            in.arm[i] = in.P > 0 ? m / in.P : 0.0;
        }
    }
    else
    {
        k.legacy = true;
        for (std::size_t i = 1; i < n; i++)
            k.legacy &= !(rawArm[i] < 0);
        for (std::size_t i = 1; i < n; i++)
        {
            const double d = in.theta[i] - th0;
            if (k.legacy)
                in.arm[i] = std::fabs(d) <= s.thresholdDeg ? 0.0 : (d < 0 ? -1.0 : 1.0) * std::fabs(rawArm[i]);
            else
                in.arm[i] = rawArm[i];
        }
    }
    in.hInfo = std::isfinite(s.rsH) && s.rsH > 0 ? s.units.ToMetres(s.rsH) : kNaN;
    in.lMax = std::isfinite(s.rsLmax) && s.rsLmax > 0 ? s.units.ToMetres(s.rsLmax) : kNaN;
    in.D0 = std::isfinite(s.rsD0) && s.rsD0 > 0 ? s.units.ToTonnes(s.rsD0) : kNaN;
    in.Dworst = std::isfinite(s.rsDworst) && s.rsDworst > 0 ? s.units.ToTonnes(s.rsDworst) : kNaN;
    in.dh = std::isfinite(s.rsDh) ? s.units.ToMetres(s.rsDh) : 0.0;
    k.dh = in.dh;
    k.r = rs::Evaluate(in);
    // График контроля: M = P·l (= накопленный момент групп) в единицах ввода (т·м или кг·мм)
    k.M.assign(n, kNaN);
    k.tanT.assign(n, kNaN);
    k.excluded.assign(n, false);
    for (const rs::Meas& q : k.r.m)
        if (q.excluded && std::isfinite(q.h))
            k.excluded[static_cast<std::size_t>(q.to)] = true;
    std::vector<double> xs, ys;
    for (std::size_t i = 0; i < n; i++)
    {
        k.M[i] = in.P * in.arm[i] / s.units.MassToT() / s.units.ArmToM();
        k.tanT[i] = std::tan((in.theta[i] - th0) * kPi / 180.0);
        if (!k.excluded[i] && std::isfinite(k.tanT[i]) && std::isfinite(k.M[i]))
        {
            xs.push_back(k.M[i]);
            ys.push_back(k.tanT[i]);
        }
    }
    k.fit = FitLine(xs, ys);
    // Условия
    k.c.theta0 = k.virtualZero ? kNaN : th0;
    k.c.dTheta.clear();
    for (std::size_t i = 0; i < n; i++)
        k.c.dTheta.push_back(in.theta[i] - th0);
    k.c.instruments = static_cast<int>(k.postIds.size());
    k.c.spread = k.r.maxSpread;
    k.c.spreadLimit = s.rsSpread > 0 ? s.rsSpread : 0.1;
    k.c.signedArms = k.groupMode || !k.legacy;
    k.c.labels = k.label;
    k.hCurrent = hCurrent;
    k.hMain = s.RsMain() == 1 ? k.r.hFinal : hCurrent - k.dh;
    k.c.h = k.hMain;
    k.checks = rs::Checks(k.r, k.c);
    if (k.groupMode && k.checks.size() > 10)
    {
        k.checks[10].req = "Моменты — по группам крен-балласта и перемещениям";
        k.checks[10].value = std::to_string(s.rsGroups.size()) + " гр.";
    }
    if (s.units.model && k.checks.size() > 4) // h ≥ 0,20 м — требование к судну, у модели не проверяется
    {
        k.checks[4].state = -1;
        k.checks[4].value += " (модель)";
    }
    k.tcN = static_cast<int>(periods.size());
    if (!periods.empty())
    {
        k.tc = Median(periods);
        k.tcMin = *std::min_element(periods.begin(), periods.end());
        k.tcMax = *std::max_element(periods.begin(), periods.end());
    }
}

} // namespace detail

// Весь опыт одной записью: положения pos (после ComputePositions); manual — по номеру положения (конец замера):
// -1 / нет — авто, 0 — исключить, 1 — учитывать. hCurrent — h «от начального + МНК» (Summary::h).
inline RsCase RsFromPositions(const Settings& s, const std::vector<File>& files, const Measurement& m,
                              const std::vector<Position>& pos, const std::map<int, int>& manual, double hCurrent)
{
    RsCase k;
    const int mp = MainPost(m);
    k.mainPost = mp;
    if (mp < 0)
        return k;
    for (int p = 0; p < kMaxPosts; p++)
        if (m.post[p].file >= 0)
            k.postIds.push_back(p);
    k.in.post.assign(k.postIds.size(), {});
    std::vector<double> rawArm, periods;
    std::vector<double> t, v;
    Series(files[static_cast<std::size_t>(m.post[mp].file)].log, s.axis, t, v);
    for (std::size_t i = 0; i < pos.size(); i++)
    {
        const Position& ps = pos[i];
        bool any = false;
        for (const int p : k.postIds)
            any |= std::isfinite(ps.Level(p));
        if (!ps.use || !any)
            continue;
        const int number = static_cast<int>(i) + 1;
        k.in.number.push_back(number);
        k.label.push_back(std::to_string(number));
        for (std::size_t j = 0; j < k.postIds.size(); j++)
            k.in.post[j].push_back(ps.Level(k.postIds[j]));
        rawArm.push_back(rawArm.empty() ? 0.0 : s.units.ToMetres(std::isfinite(ps.arm) ? ps.arm : s.L));
        if (k.in.number.size() > 1)
        {
            const auto it = manual.find(number);
            k.in.manual.push_back(it != manual.end() ? it->second : -1);
        }
        if (const double T = detail::PeriodOn(t, v, std::min(ps.t0, ps.t1), std::max(ps.t0, ps.t1)); std::isfinite(T))
            periods.push_back(std::clamp(T, 1.0, 60.0));
    }
    detail::RsFinish(s, k, rawArm, hCurrent, periods);
    return k;
}

// Файл = замер: замеры ms (после Compute), учтённые — по номеру; manual — по номеру замера.
inline RsCase RsFromMeasurements(const Settings& s, const std::vector<File>& files, const std::vector<Measurement>& ms,
                                 const std::map<int, int>& manual, double hCurrent)
{
    RsCase k;
    for (int p = 0; p < kMaxPosts; p++)
        for (const Measurement& m : ms)
            if (m.use && m.post[p].file >= 0)
            {
                k.postIds.push_back(p);
                break;
            }
    if (k.postIds.empty())
        return k;
    k.mainPost = k.postIds.front();
    k.in.post.assign(k.postIds.size(), {});
    std::vector<double> rawArm, periods;
    for (const Measurement& m : ms)
    {
        if (!m.use)
            continue;
        bool any = false;
        for (const int p : k.postIds)
            any |= m.post[p].file >= 0 && std::isfinite(m.post[p].st.mean);
        if (!any)
            continue;
        const double a = s.units.ToMetres(std::isfinite(m.arm) ? m.arm : s.L);
        if (k.in.number.empty() && a != 0.0 && !s.rsGroupMode)
        {
            // Условное начальное положение: груз на месте, крен — ноль датчиков
            k.virtualZero = true;
            k.in.number.push_back(0);
            k.label.push_back("0");
            for (auto& p : k.in.post)
                p.push_back(0.0);
            rawArm.push_back(0.0);
        }
        k.in.number.push_back(m.number);
        k.label.push_back(m.label.empty() ? std::to_string(m.number) : m.label);
        for (std::size_t j = 0; j < k.postIds.size(); j++)
        {
            const PostData& pd = m.post[k.postIds[j]];
            k.in.post[j].push_back(pd.file >= 0 ? pd.st.mean : kNaN);
        }
        rawArm.push_back(rawArm.empty() ? 0.0 : a);
        if (k.in.number.size() > 1)
        {
            const auto it = manual.find(m.number);
            k.in.manual.push_back(it != manual.end() ? it->second : -1);
        }
        const PostData& pd = m.post[k.mainPost];
        if (pd.file >= 0)
        {
            const csvlog::Log& log = files[static_cast<std::size_t>(pd.file)].log;
            if (!log.rows.empty())
            {
                std::vector<double> t, v;
                Series(log, s.axis, t, v);
                if (const double T = detail::PeriodOn(t, v, t.back() - s.windowS, t.back()); std::isfinite(T))
                    periods.push_back(std::clamp(T, 1.0, 60.0));
            }
        }
    }
    detail::RsFinish(s, k, rawArm, hCurrent, periods);
    return k;
}

} // namespace heel
