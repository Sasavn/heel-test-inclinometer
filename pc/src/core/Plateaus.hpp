#pragma once
// Поиск положений груза в непрерывной записи опыта кренования: один файл — весь опыт, груз переносят несколько раз,
// крен стоит «ступеньками» (на каждой — качка с периодом 5–15 с, после переноса — всплеск и затухающие колебания).
// Чистый модуль без файлов и интерфейса: вход — время t (с) и угол v (°), выход — участки установившегося крена
// (начало, конец, среднее, СКО).
//
// Алгоритм:
//  1. Скользящая медиана за smoothS с (по времени, окно по центру): убирает всплески короче половины окна и большую
//     часть качки, а ступеньки сохраняет (медиана не размывает перепад, как среднее).
//  2. Бинарная сегментация сглаженного ряда: на каждом шаге — разрез с наибольшим уменьшением суммы квадратов
//     отклонений от средних частей; разрез допустим, если обе части не короче minLenS с и средние частей отличаются
//     не меньше чем на minStepDeg. Повторяется, пока есть допустимые разрезы.
//  3. В каждом куске — уровень (медиана сглаженного ряда) и «устоявшиеся» точки: сглаженный ряд ближе tol к уровню
//     (tol = max(tolDeg, 0,15 × меньший перепад к соседям)). Край куска до первой и после последней такой точки —
//     переход (груз едет, всплеск), отбрасывается; ещё edgeS с с каждого края — запас на хвост всплеска.
//  4. Выбросы (всплески — люди ходят, стук по датчику): отсчёт дальше от медианы участка, чем
//     max(outlierK × 1,4826 × MAD, outlierMinDeg). Выброс в первых / последних (smoothS/2 + edgeS) с участка —
//     начало перехода: край участка переносится за него (ещё 1 с запаса).
//  5. Участки короче minLenS отбрасываются; соседние участки с уровнями ближе minStepDeg сливаются.
//  6. Среднее и СКО участка — по исходным (не сглаженным) отсчётам без выбросов: качка входит в СКО (Measure).
//
// Проверено на синтетике (tests/data/continuous) и на настоящей записи руководителя (tests/data/real, прошивка 1.5,
// 5400 строк, 10 Гц): без п. 4 всплески внутри положений сдвигали уровень (0,25° вместо 0,14°) и растягивали СКО.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace plateau
{

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Params
{
    double minLenS = 10.0;      // участок (положение) не короче, с
    double minStepDeg = 0.5;    // перепад между соседними положениями не меньше, °
    double smoothS = 12.0;      // окно скользящей медианы, с (порядка периода качки)
    double tolDeg = 0.25;       // допуск «устоявшегося» крена от уровня, ° (не меньше)
    double edgeS = 2.0;         // запас на краях участка, с
};

inline constexpr double kOutlierK = 5.0;      // выброс: дальше медианы на 5 «сигм» по MAD…
inline constexpr double kOutlierMinDeg = 1.0; // …и не меньше чем на 1°

// Участок записи и крен на нём.
struct Span
{
    double t0 = kNaN, t1 = kNaN;   // с от начала записи
    double mean = kNaN, sd = kNaN; // ° (без выбросов)
    int n = 0;                     // отсчётов в расчёте
    int rejected = 0;              // выбросов отброшено

    double Duration() const { return t1 - t0; }
};

namespace detail
{

inline double Median(std::vector<double> x)
{
    if (x.empty())
        return kNaN;
    const std::size_t m = x.size() / 2;
    std::nth_element(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(m), x.end());
    double med = x[m];
    if (x.size() % 2 == 0)
        med = 0.5 * (med + *std::max_element(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(m)));
    return med;
}

// Медиана и порог выброса по отсчётам; порог — бесконечность, если отсчётов меньше 5.
inline void OutlierBand(const std::vector<double>& x, double& med, double& thr)
{
    med = Median(x);
    thr = std::numeric_limits<double>::infinity();
    if (x.size() < 5)
        return;
    std::vector<double> dev(x.size());
    for (std::size_t i = 0; i < x.size(); i++)
        dev[i] = std::fabs(x[i] - med);
    thr = std::max(kOutlierK * 1.4826 * Median(std::move(dev)), kOutlierMinDeg);
}

} // namespace detail

// Среднее, СКО (выборочное) и число отсчётов на [t0, t1] без выбросов (см. п. 4); NaN в v пропускаются.
inline Span Measure(const std::vector<double>& t, const std::vector<double>& v, double t0, double t1)
{
    Span s;
    s.t0 = t0;
    s.t1 = t1;
    const std::size_t n = std::min(t.size(), v.size());
    std::vector<double> x;
    for (std::size_t i = 0; i < n; i++)
        if (t[i] >= t0 && t[i] <= t1 && std::isfinite(v[i]))
            x.push_back(v[i]);
    if (x.empty())
        return s;
    double med = 0, thr = 0;
    detail::OutlierBand(x, med, thr);
    double sum = 0;
    for (const double y : x)
        if (std::fabs(y - med) <= thr)
        {
            sum += y;
            s.n++;
        }
        else
            s.rejected++;
    s.mean = sum / s.n;
    if (s.n >= 2)
    {
        double q = 0;
        for (const double y : x)
            if (std::fabs(y - med) <= thr)
                q += (y - s.mean) * (y - s.mean);
        s.sd = std::sqrt(q / (s.n - 1));
    }
    return s;
}

// Края участка [t0, t1] — за выбросы в первых / последних win с (начало и конец перехода).
inline void TrimOutlierEdges(const std::vector<double>& t, const std::vector<double>& v, double& t0, double& t1,
                             double win)
{
    const std::size_t n = std::min(t.size(), v.size());
    std::vector<double> x;
    for (std::size_t i = 0; i < n; i++)
        if (t[i] >= t0 && t[i] <= t1 && std::isfinite(v[i]))
            x.push_back(v[i]);
    double med = 0, thr = 0;
    detail::OutlierBand(x, med, thr);
    if (!std::isfinite(thr))
        return;
    double headLast = kNaN, tailFirst = kNaN;
    for (std::size_t i = 0; i < n; i++)
        if (t[i] >= t0 && t[i] <= t1 && std::isfinite(v[i]) && std::fabs(v[i] - med) > thr)
        {
            if (t[i] <= t0 + win)
                headLast = t[i];
            if (t[i] >= t1 - win && !std::isfinite(tailFirst))
                tailFirst = t[i];
        }
    if (std::isfinite(headLast))
        t0 = headLast + 1.0;
    if (std::isfinite(tailFirst))
        t1 = tailFirst - 1.0;
}

// Скользящая медиана за windowS с (окно по центру, по времени). t — по возрастанию.
inline std::vector<double> MedianFilter(const std::vector<double>& t, const std::vector<double>& v, double windowS)
{
    const std::size_t n = std::min(t.size(), v.size());
    std::vector<double> out(n, kNaN), buf;
    const double h = 0.5 * windowS;
    std::size_t a = 0, b = 0; // окно [a, b)
    for (std::size_t i = 0; i < n; i++)
    {
        while (a < i && t[a] < t[i] - h)
            a++;
        if (b < i + 1)
            b = i + 1;
        while (b < n && t[b] <= t[i] + h)
            b++;
        buf.assign(v.begin() + static_cast<std::ptrdiff_t>(a), v.begin() + static_cast<std::ptrdiff_t>(b));
        const std::size_t m = buf.size() / 2;
        std::nth_element(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m), buf.end());
        double med = buf[m];
        if (buf.size() % 2 == 0)
        {
            const double lo = *std::max_element(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(m));
            med = 0.5 * (med + lo);
        }
        out[i] = med;
    }
    return out;
}

// Положения (участки установившегося крена) по порядку времени.
inline std::vector<Span> Detect(const std::vector<double>& tIn, const std::vector<double>& vIn, const Params& p)
{
    // Только конечные отсчёты, по возрастанию времени
    std::vector<double> t, v;
    const std::size_t nIn = std::min(tIn.size(), vIn.size());
    t.reserve(nIn);
    v.reserve(nIn);
    for (std::size_t i = 0; i < nIn; i++)
        if (std::isfinite(tIn[i]) && std::isfinite(vIn[i]) && (t.empty() || tIn[i] >= t.back()))
        {
            t.push_back(tIn[i]);
            v.push_back(vIn[i]);
        }
    std::vector<Span> out;
    const std::size_t n = t.size();
    if (n < 3 || !(t.back() - t.front() >= p.minLenS))
        return out;
    const std::vector<double> f = MedianFilter(t, v, std::max(p.smoothS, 0.0));

    // 2. Бинарная сегментация: префиксные суммы сглаженного ряда
    std::vector<double> s1(n + 1, 0.0), s2(n + 1, 0.0);
    for (std::size_t i = 0; i < n; i++)
    {
        s1[i + 1] = s1[i] + f[i];
        s2[i + 1] = s2[i] + f[i] * f[i];
    }
    auto mean = [&](std::size_t a, std::size_t b) { return (s1[b] - s1[a]) / static_cast<double>(b - a); };
    auto sse = [&](std::size_t a, std::size_t b) {
        const double s = s1[b] - s1[a];
        return s2[b] - s2[a] - s * s / static_cast<double>(b - a);
    };
    struct Seg
    {
        std::size_t a, b; // [a, b)
        bool done;
    };
    std::vector<Seg> segs{{0, n, false}};
    for (int iter = 0; iter < 400; iter++)
    {
        double bestGain = -1;
        std::size_t bestSeg = 0, bestK = 0;
        for (std::size_t si = 0; si < segs.size(); si++)
        {
            Seg& sg = segs[si];
            if (sg.done)
                continue;
            const double base = sse(sg.a, sg.b);
            bool any = false;
            for (std::size_t k = sg.a + 1; k < sg.b; k++)
            {
                if (t[k] - t[sg.a] < p.minLenS)
                    continue;
                if (t[sg.b - 1] - t[k] < p.minLenS)
                    break;
                if (std::fabs(mean(sg.a, k) - mean(k, sg.b)) < p.minStepDeg)
                    continue;
                const double g = base - sse(sg.a, k) - sse(k, sg.b);
                any = true;
                if (g > bestGain)
                {
                    bestGain = g;
                    bestSeg = si;
                    bestK = k;
                }
            }
            if (!any)
                sg.done = true; // этот кусок больше не делится
        }
        if (bestGain < 0)
            break;
        const Seg old = segs[bestSeg];
        segs[bestSeg] = {old.a, bestK, false};
        segs.insert(segs.begin() + static_cast<std::ptrdiff_t>(bestSeg) + 1, Seg{bestK, old.b, false});
    }

    // 3. Уровни кусков, устоявшаяся часть
    std::vector<double> level(segs.size());
    for (std::size_t si = 0; si < segs.size(); si++)
        level[si] = detail::Median(std::vector<double>(f.begin() + static_cast<std::ptrdiff_t>(segs[si].a),
                                                       f.begin() + static_cast<std::ptrdiff_t>(segs[si].b)));
    std::vector<Span> raw;
    for (std::size_t si = 0; si < segs.size(); si++)
    {
        double step = std::numeric_limits<double>::infinity();
        if (si > 0)
            step = std::min(step, std::fabs(level[si] - level[si - 1]));
        if (si + 1 < segs.size())
            step = std::min(step, std::fabs(level[si] - level[si + 1]));
        const double tol = std::max(p.tolDeg, std::isfinite(step) ? 0.15 * step : 0.0);
        std::size_t first = segs[si].b, last = segs[si].a;
        for (std::size_t i = segs[si].a; i < segs[si].b; i++)
            if (std::fabs(f[i] - level[si]) <= tol)
            {
                first = std::min(first, i);
                last = i;
            }
        if (first > last)
            continue;
        double t0 = t[first] + p.edgeS, t1 = t[last] - p.edgeS;
        if (t1 > t0)
            TrimOutlierEdges(t, v, t0, t1, 0.5 * p.smoothS + p.edgeS);
        if (t1 - t0 < p.minLenS)
            continue;
        raw.push_back(Measure(t, v, t0, t1));
    }

    // 4. Слить соседние участки с почти равным уровнем
    for (const Span& s : raw)
    {
        if (!out.empty() && std::fabs(out.back().mean - s.mean) < p.minStepDeg)
            out.back() = Measure(t, v, out.back().t0, s.t1);
        else
            out.push_back(s);
    }
    return out;
}

} // namespace plateau
