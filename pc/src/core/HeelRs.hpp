#pragma once
// Проверка опыта кренования по Правилам РС на данных «Обработки» (RsCheck.hpp — формулы и требования):
//  - вход табл. 6.5 — положения груза в порядке опыта и их крен по основному посту (первый пост с файлом): у записи
//    «весь опыт одним файлом» — учтённые положения (первое — начальное, плечо 0), у «файл = замер» — учтённые замеры
//    по номеру (если у первого плечо не 0 — впереди условное начальное положение: крен 0, плечо 0);
//  - плечи со знаком (+ к правому борту); если ни одного отрицательного (старый ввод модулем) — знак по крену
//    относительно начального (|Δθ| не больше порога — груз на месте, плечо 0), в журнале — пометка;
//  - график контроля (п. 5.5): кренящий момент M = P·l и tg(θ − θ0) по положениям, МНК по неисключённым;
//  - условия разд. 3: начальный крен, крен на борта, число приборов (постов), сверка постов; период качки Tc — по
//    переходам через медиану (Filters.hpp, DominantOmega) на каждом положении, медиана.
#include <algorithm>
#include <cmath>
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
    std::vector<double> M, tanT;     // график контроля: момент P·l (единицы массы × плеча) и tg(θ − θ0)
    std::vector<bool> excluded;      // положение — конец исключённого замера (на графике — красным)
    Fit fit;                         // МНК tg(θ − θ0) = k·M + b по неисключённым
    bool legacy = false;             // плечи без знака — знак по крену
    bool virtualZero = false;        // «файл = замер»: условное начальное положение
    int mainPost = -1;
    double tc = kNaN, tcMin = kNaN, tcMax = kNaN; // период качки, с: медиана и разброс по положениям
    int tcN = 0;
    double hMain = kNaN;             // h основного расчёта (Settings::rsMain)
    double hCurrent = kNaN;          // h «от начального + МНК» (среднее по точкам)
};

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

// Общая часть: плечи (знак), Evaluate, график контроля, условия, требования.
inline void RsFinish(const Settings& s, RsCase& k, const std::vector<double>& rawArm, double hCurrent,
                     std::vector<double> periods)
{
    rs::Input& in = k.in;
    const std::size_t n = in.theta.size();
    const double th0 = n ? in.theta[0] : kNaN;
    k.legacy = true;
    for (std::size_t i = 1; i < n; i++)
        k.legacy &= !(rawArm[i] < 0);
    in.arm.assign(n, 0.0);
    for (std::size_t i = 1; i < n; i++)
    {
        const double d = in.theta[i] - th0;
        if (k.legacy)
            in.arm[i] = std::fabs(d) <= s.thresholdDeg ? 0.0 : (d < 0 ? -1.0 : 1.0) * std::fabs(rawArm[i]);
        else
            in.arm[i] = rawArm[i];
    }
    in.D = s.units.ToTonnes(s.D);
    in.P = s.units.ToTonnes(s.P);
    in.hInfo = std::isfinite(s.rsH) && s.rsH > 0 ? s.units.ToMetres(s.rsH) : kNaN;
    in.lMax = std::isfinite(s.rsLmax) && s.rsLmax > 0 ? s.units.ToMetres(s.rsLmax) : kNaN;
    in.D0 = std::isfinite(s.rsD0) && s.rsD0 > 0 ? s.units.ToTonnes(s.rsD0) : kNaN;
    k.r = rs::Evaluate(in);
    // График контроля: M = P·l в единицах ввода (т·м или кг·мм)
    k.M.assign(n, kNaN);
    k.tanT.assign(n, kNaN);
    k.excluded.assign(n, false);
    for (const rs::Meas& q : k.r.m)
        if (q.excluded && std::isfinite(q.h))
            k.excluded[static_cast<std::size_t>(q.to)] = true;
    std::vector<double> xs, ys;
    for (std::size_t i = 0; i < n; i++)
    {
        k.M[i] = s.P * in.arm[i] / s.units.ArmToM();
        k.tanT[i] = std::tan((in.theta[i] - th0) * kPi / 180.0);
        if (!k.excluded[i] && std::isfinite(k.tanT[i]))
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
    k.c.spreadLimit = s.rsSpread > 0 ? s.rsSpread : 0.05;
    k.c.signedArms = !k.legacy;
    k.c.labels = k.label;
    k.hCurrent = hCurrent;
    k.hMain = s.rsMain == 1 ? k.r.hFinal : hCurrent;
    k.c.h = k.hMain;
    k.checks = rs::Checks(k.r, k.c);
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
    std::vector<double> rawArm, periods;
    std::vector<double> t, v;
    Series(files[static_cast<std::size_t>(m.post[mp].file)].log, s.axis, t, v);
    for (std::size_t i = 0; i < pos.size(); i++)
    {
        const Position& ps = pos[i];
        const double th = ps.Level(mp);
        if (!ps.use || !std::isfinite(th))
            continue;
        const int number = static_cast<int>(i) + 1;
        k.in.number.push_back(number);
        k.label.push_back(std::to_string(number));
        k.in.theta.push_back(th);
        rawArm.push_back(rawArm.empty() ? 0.0 : s.units.ToMetres(std::isfinite(ps.arm) ? ps.arm : s.L));
        if (!k.in.theta.empty() && k.in.theta.size() > 1)
        {
            const auto it = manual.find(number);
            k.in.manual.push_back(it != manual.end() ? it->second : -1);
        }
        if (const double T = detail::PeriodOn(t, v, std::min(ps.t0, ps.t1), std::max(ps.t0, ps.t1)); std::isfinite(T))
            periods.push_back(std::clamp(T, 1.0, 60.0));
        // Сверка постов — по учтённым положениям
        if (const double sp = ps.Spread(); std::isfinite(sp))
            k.c.spread = std::isfinite(k.c.spread) ? std::max(k.c.spread, sp) : sp;
    }
    for (int p = 0; p < kMaxPosts; p++)
        k.c.instruments += m.post[p].file >= 0;
    detail::RsFinish(s, k, rawArm, hCurrent, periods);
    return k;
}

// Файл = замер: замеры ms (после Compute), учтённые — по номеру; manual — по номеру замера.
inline RsCase RsFromMeasurements(const Settings& s, const std::vector<File>& files, const std::vector<Measurement>& ms,
                                 const std::map<int, int>& manual, double hCurrent)
{
    RsCase k;
    int mp = -1;
    for (const Measurement& m : ms)
        for (int p = 0; p < kMaxPosts && mp < 0; p++)
            if (m.use && m.post[p].file >= 0)
                mp = p;
    for (const Measurement& m : ms)
        for (int p = 0; p < kMaxPosts; p++)
            if (m.use && m.post[p].file >= 0 && p < mp)
                mp = p;
    k.mainPost = mp;
    if (mp < 0)
        return k;
    std::vector<double> rawArm, periods;
    bool post[kMaxPosts] = {};
    for (const Measurement& m : ms)
    {
        if (!m.use)
            continue;
        for (int p = 0; p < kMaxPosts; p++)
            post[p] |= m.post[p].file >= 0;
        const PostData& pd = m.post[mp];
        if (pd.file < 0 || !std::isfinite(pd.st.mean))
            continue;
        const double a = s.units.ToMetres(std::isfinite(m.arm) ? m.arm : s.L);
        if (k.in.theta.empty() && a != 0.0)
        {
            // Условное начальное положение: груз на месте, крен — ноль датчика
            k.virtualZero = true;
            k.in.number.push_back(0);
            k.label.push_back("0");
            k.in.theta.push_back(0.0);
            rawArm.push_back(0.0);
        }
        k.in.number.push_back(m.number);
        k.label.push_back(m.label.empty() ? std::to_string(m.number) : m.label);
        k.in.theta.push_back(pd.st.mean);
        rawArm.push_back(rawArm.empty() ? 0.0 : a);
        if (k.in.theta.size() > 1)
        {
            const auto it = manual.find(m.number);
            k.in.manual.push_back(it != manual.end() ? it->second : -1);
        }
        const csvlog::Log& log = files[static_cast<std::size_t>(pd.file)].log;
        if (!log.rows.empty())
        {
            std::vector<double> t, v;
            Series(log, s.axis, t, v);
            if (const double T = detail::PeriodOn(t, v, t.back() - s.windowS, t.back()); std::isfinite(T))
                periods.push_back(std::clamp(T, 1.0, 60.0));
        }
        if (const double sp = m.Spread(); std::isfinite(sp))
            k.c.spread = std::isfinite(k.c.spread) ? std::max(k.c.spread, sp) : sp;
    }
    for (int p = 0; p < kMaxPosts; p++)
        k.c.instruments += post[p];
    detail::RsFinish(s, k, rawArm, hCurrent, periods);
    return k;
}

} // namespace heel
