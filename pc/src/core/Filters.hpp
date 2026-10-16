#pragma once
// Обработка сигнала крена на участке одного положения груза и оценка установившегося угла. Чистые функции без
// интерфейса и файлов (тесты — process_test). Методы:
//
//  Kalman (по умолчанию с программы 1.4) — см. ниже; лучший в сравнении на синтетике и записи M020 (README).
//  MedianEma — по статье «Математический способ обработки углов крена при проведении опыта
//    кренования» (Русановский С.А. и др., МИТ, 2026): каскад «медианный фильтр по трём последним отсчётам (10) →
//    EMA y[n] = α·yмед[n] + (1 − α)·y[n−1] (11)», α = 2π·fс / (2π·fс + fоп) (12), fс = 0,5 Гц, fоп — частота
//    опроса (по данным). Начальное значение EMA в статье не задано — берётся первый отсчёт после медианы.
//    Среднее — по обработанному сигналу на окне Ω длиной N = round(3·Tc·fоп) отсчётов (13) в конце участка
//    положения (там же статья считает размах (14) и дисперсию (15) для критерия установившегося угла); Tc — период
//    собственных колебаний: задаётся или (0) оценивается по переходам через уровень на участке.
//  Mean — среднее исходных отсчётов участка без выбросов (как было до этого).
//  SavGol — Савицкий–Голай: локальный полином 2-й степени по окну sgWindowS с (по времени), среднее — на Ω.
//  Kalman — фильтр Калмана «уровень + колебание»: состояние [θ0, y, ẏ], θ0 — постоянный уровень (медленное
//    блуждание), y — затухающая качка (осциллятор с ω по данным и ζ = 0,05), измерение z = θ0 + y. Оценка —
//    θ0 в конце участка; кривая — θ0(t) (как сходится).
//  DampedSine — аппроксимация θ(t) = θ∞ + e^{−λt}(a·cos ωt + b·sin ωt) методом наименьших квадратов (линейно по θ∞,
//    a, b; λ и ω — перебором и уточнением), выбросы — вторым проходом. Оценка — равновесный угол θ∞.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace filt
{

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr double kPi = 3.14159265358979323846;

enum class Method
{
    MedianEma = 0,
    Mean = 1,
    SavGol = 2,
    Kalman = 3,
    DampedSine = 4,
    User = 5, // свой метод из формул (UserMethod.hpp)
    Count
};

inline constexpr int kBuiltinCount = 5; // встроенные методы — 0…4

inline const char* MethodName(Method m)
{
    switch (m)
    {
    case Method::MedianEma: return "Медиана + EMA (статья)";
    case Method::Mean: return "Простое среднее";
    case Method::SavGol: return "Савицкий–Голай";
    case Method::Kalman: return "Калман: уровень + качка";
    case Method::DampedSine: return "Затухающая синусоида";
    case Method::User: return "Свой метод (формулы)";
    default: return "?";
    }
}

// Свой метод (формулы, UserMethod.hpp) — через этот интерфейс, чтобы Filters.hpp не зависел от интерпретатора.
struct Custom
{
    virtual ~Custom() = default;
    virtual std::string Name() const = 0;
    // Обработанный сигнал всей записи (t, x); tcS — Tc из настроек (0 — по данным).
    virtual std::vector<double> Process(const std::vector<double>& t, const std::vector<double>& x, double tcS) const = 0;
    // Крен положения: st, sp — время и обработанный сигнал участка, sx — исходный; окно и Tc — в winT0/winT1/tcUsed.
    virtual double Level(const std::vector<double>& st, const std::vector<double>& sp, const std::vector<double>& sx,
                         double tcS, double& winT0, double& winT1, double& tcUsed) const = 0;
};

struct Params
{
    Method method = Method::Kalman;
    int medianN = 3;        // окно медианы, отсчётов (статья: 3 последних)
    double fcHz = 0.5;      // частота среза EMA, Гц (статья: 0,5)
    double alpha = 0.0;     // 0 — по формуле (12) из fc и частоты опроса; иначе — заданное α
    double tcS = 0.0;       // период собственных колебаний Tc, с; 0 — по данным
    double sgWindowS = 4.0; // Савицкий–Голай: окно, с
    bool wholeSpan = false; // среднее (MedianEma, SavGol) — по всему участку, а не по окну Ω = 3·Tc статьи
    std::shared_ptr<const Custom> custom; // Method::User
    int sgOrder = 2;        // Савицкий–Голай: степень
};

// Метод и его параметры одной строкой (отчёт, подсказки).
inline std::string Describe(const Params& p)
{
    char b[640];
    const std::string tc = p.tcS > 0 ? "Tc = " + std::to_string(static_cast<int>(std::lround(p.tcS))) + " с"
                                     : std::string("Tc — по данным");
    const std::string win = p.wholeSpan ? "среднее — по установившемуся крену (без переходных начала и конца участка)"
                                        : "среднее — по окну Ω в конце установившегося крена: вторая его половина, не "
                                          "короче 3·Tc, " + tc;
    switch (p.method)
    {
    case Method::MedianEma:
        if (p.alpha > 0)
            std::snprintf(b, sizeof(b), "%s: медиана по %d последним отсчётам, EMA α = %.3f; %s", MethodName(p.method),
                          p.medianN, p.alpha, win.c_str());
        else
            std::snprintf(b, sizeof(b), "%s: медиана по %d последним отсчётам, EMA α = 2π·fс/(2π·fс + fоп), fс = %.2f Гц; %s",
                          MethodName(p.method), p.medianN, p.fcHz, win.c_str());
        return b;
    case Method::SavGol:
        std::snprintf(b, sizeof(b), "%s: полином %d-й степени, окно %.1f с; %s", MethodName(p.method), p.sgOrder,
                      p.sgWindowS, win.c_str());
        return b;
    case Method::Mean: return std::string(MethodName(p.method)) + ": по всему участку, без выбросов";
    case Method::Kalman: return std::string(MethodName(p.method)) + ": уровень по окну Ω в конце установившегося крена";
    case Method::DampedSine: return std::string(MethodName(p.method)) + ": равновесный угол θ∞ по всему участку";
    case Method::User: return p.custom ? "Свой метод «" + p.custom->Name() + "»" : std::string("Свой метод — не выбран");
    default: return MethodName(p.method);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Фильтры (на всём ряду; t — по возрастанию)
// ---------------------------------------------------------------------------------------------------------------

inline double MedianOf(std::vector<double> x)
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

// (10): yмед[n] = Me(x[n−N+1], …, x[n]) — по N последним отсчётам (в начале ряда — сколько есть).
inline std::vector<double> MedianCausal(const std::vector<double>& x, int n)
{
    std::vector<double> y(x.size());
    const std::size_t w = static_cast<std::size_t>(std::max(1, n));
    std::vector<double> buf;
    for (std::size_t i = 0; i < x.size(); i++)
    {
        const std::size_t a = i + 1 >= w ? i + 1 - w : 0;
        buf.assign(x.begin() + static_cast<std::ptrdiff_t>(a), x.begin() + static_cast<std::ptrdiff_t>(i) + 1);
        y[i] = MedianOf(buf);
    }
    return y;
}

// (11): y[n] = α·x[n] + (1 − α)·y[n−1], y[0] = x[0].
inline std::vector<double> Ema(const std::vector<double>& x, double alpha)
{
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); i++)
        y[i] = i == 0 ? x[0] : alpha * x[i] + (1.0 - alpha) * y[i - 1];
    return y;
}

// (12): α = 2π·fс / (2π·fс + fоп).
inline double AlphaFor(double fcHz, double fopHz)
{
    const double w = 2.0 * kPi * fcHz;
    return w / (w + fopHz);
}

// Частота опроса по ряду: 1 / медиана шага времени.
inline double SampleRate(const std::vector<double>& t)
{
    std::vector<double> d;
    for (std::size_t i = 1; i < t.size(); i++)
        if (t[i] > t[i - 1])
            d.push_back(t[i] - t[i - 1]);
    const double m = MedianOf(d);
    return m > 0 ? 1.0 / m : 10.0;
}

// Решение 3×3 (и меньше) методом Гаусса; false — вырождена.
inline bool Solve(int n, double a[4][4], double b[4], double x[4])
{
    for (int c = 0; c < n; c++)
    {
        int p = c;
        for (int r = c + 1; r < n; r++)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c]))
                p = r;
        if (std::fabs(a[p][c]) < 1e-300)
            return false;
        if (p != c)
        {
            for (int k = 0; k < n; k++)
                std::swap(a[p][k], a[c][k]);
            std::swap(b[p], b[c]);
        }
        for (int r = c + 1; r < n; r++)
        {
            const double f = a[r][c] / a[c][c];
            for (int k = c; k < n; k++)
                a[r][k] -= f * a[c][k];
            b[r] -= f * b[c];
        }
    }
    for (int r = n - 1; r >= 0; r--)
    {
        double s = b[r];
        for (int k = r + 1; k < n; k++)
            s -= a[r][k] * x[k];
        x[r] = s / a[r][r];
    }
    return true;
}

// Савицкий–Голай: в каждой точке — полином степени order по отсчётам окна ±windowS/2 (по времени), значение в точке.
inline std::vector<double> SavGol(const std::vector<double>& t, const std::vector<double>& x, double windowS, int order)
{
    const std::size_t n = std::min(t.size(), x.size());
    const int p = std::clamp(order, 0, 3) + 1;
    std::vector<double> y(n, kNaN);
    std::size_t a = 0, b = 0;
    for (std::size_t i = 0; i < n; i++)
    {
        while (a < i && t[a] < t[i] - 0.5 * windowS)
            a++;
        b = std::max(b, i + 1);
        while (b < n && t[b] <= t[i] + 0.5 * windowS)
            b++;
        double m[4][4] = {}, r[4] = {}, c[4] = {};
        for (std::size_t k = a; k < b; k++)
        {
            const double d = t[k] - t[i];
            double pw[7] = {1, d, d * d, d * d * d, d * d * d * d, 0, 0};
            pw[5] = pw[4] * d;
            pw[6] = pw[5] * d;
            for (int u = 0; u < p; u++)
            {
                r[u] += pw[u] * x[k];
                for (int v = 0; v < p; v++)
                    m[u][v] += pw[u + v];
            }
        }
        const int use = std::min<int>(p, static_cast<int>(b - a));
        y[i] = Solve(use, m, r, c) ? c[0] : x[i];
    }
    return y;
}

// Угловая частота качки по переходам сглаженного ряда через его медиану; NaN — переходов меньше двух.
inline double DominantOmega(const std::vector<double>& t, const std::vector<double>& x)
{
    if (x.size() < 10)
        return kNaN;
    const std::vector<double> s = Ema(MedianCausal(x, 3), 0.3);
    const double med = MedianOf(s);
    int cross = 0;
    double first = kNaN, last = kNaN;
    for (std::size_t i = 1; i < s.size(); i++)
        if ((s[i - 1] - med) * (s[i] - med) < 0)
        {
            cross++;
            if (!std::isfinite(first))
                first = t[i];
            last = t[i];
        }
    if (cross < 3 || !(last > first))
        return kNaN;
    return kPi * (cross - 1) / (last - first); // полупериод — между соседними переходами
}

// ---------------------------------------------------------------------------------------------------------------
// Калман «уровень + качка» и затухающая синусоида
// ---------------------------------------------------------------------------------------------------------------

// Уровень θ0(t) фильтра Калмана по участку (t, x); omega — угловая частота качки, рад/с.
// level0 — начальный уровень (NaN — медиана первых 20 отсчётов): по окну Ω фильтр стартует с медианы окна, без
// переходного процесса в начале окна.
inline std::vector<double> KalmanLevel(const std::vector<double>& t, const std::vector<double>& x, double omega,
                                       double zeta = 0.05, double level0 = kNaN)
{
    const std::size_t n = std::min(t.size(), x.size());
    std::vector<double> lev(n, kNaN);
    if (n == 0)
        return lev;
    // Шум измерения — по разностям соседних отсчётов (качка между ними почти не меняется)
    std::vector<double> d;
    for (std::size_t i = 1; i < n; i++)
        d.push_back(std::fabs(x[i] - x[i - 1]));
    const double sn = std::max(0.01, 1.4826 * MedianOf(d) / std::sqrt(2.0));
    const double R = sn * sn;
    double s[3] = {MedianOf(std::vector<double>(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(n, 20)))), 0, 0};
    if (std::isfinite(level0))
        s[0] = level0;
    s[1] = x[0] - s[0];
    double P[3][3] = {{std::isfinite(level0) ? 0.01 : 4.0, 0, 0}, {0, 4, 0}, {0, 0, 4}};
    const double w2 = omega * omega, c = 2 * zeta * omega;
    for (std::size_t i = 0; i < n; i++)
    {
        if (i > 0)
        {
            const double dt = std::clamp(t[i] - t[i - 1], 1e-3, 1.0);
            // exp(A·dt) осциллятора [[0,1],[−ω²,−2ζω]] — ряд Тейлора до 4-й степени (ω·dt ≪ 1)
            double A[2][2] = {{0, 1}, {-w2, -c}}, F[2][2] = {{1, 0}, {0, 1}}, term[2][2] = {{1, 0}, {0, 1}};
            for (int k = 1; k <= 4; k++)
            {
                double nt[2][2] = {};
                for (int a = 0; a < 2; a++)
                    for (int b = 0; b < 2; b++)
                        nt[a][b] = (term[a][0] * A[0][b] + term[a][1] * A[1][b]) * dt / k;
                for (int a = 0; a < 2; a++)
                    for (int b = 0; b < 2; b++)
                    {
                        term[a][b] = nt[a][b];
                        F[a][b] += nt[a][b];
                    }
            }
            const double y = F[0][0] * s[1] + F[0][1] * s[2], v = F[1][0] * s[1] + F[1][1] * s[2];
            s[1] = y;
            s[2] = v;
            // P = Φ P Φᵀ + Q, Φ = diag(1, F)
            double Phi[3][3] = {{1, 0, 0}, {0, F[0][0], F[0][1]}, {0, F[1][0], F[1][1]}}, T[3][3] = {}, Np[3][3] = {};
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                    for (int k = 0; k < 3; k++)
                        T[a][b] += Phi[a][k] * P[k][b];
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                    for (int k = 0; k < 3; k++)
                        Np[a][b] += T[a][k] * Phi[b][k];
            Np[0][0] += 1e-6 * dt;           // уровень почти постоянен
            Np[2][2] += 0.02 * w2 * dt;      // качку раскачивает
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                    P[a][b] = Np[a][b];
        }
        // Измерение z = θ0 + y; выброс (дальше 5σ инновации) — пропускается
        const double inn = x[i] - (s[0] + s[1]);
        const double S = P[0][0] + 2 * P[0][1] + P[1][1] + R;
        if (inn * inn <= 25.0 * S || i < 5)
        {
            double K[3];
            for (int a = 0; a < 3; a++)
                K[a] = (P[a][0] + P[a][1]) / S;
            for (int a = 0; a < 3; a++)
                s[a] += K[a] * inn;
            double Np[3][3];
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                    Np[a][b] = P[a][b] - K[a] * (P[0][b] + P[1][b]);
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                    P[a][b] = Np[a][b];
        }
        lev[i] = s[0];
    }
    return lev;
}

struct SineFit
{
    double level = kNaN;               // равновесный угол θ∞
    double a = 0, b = 0, lambda = 0, omega = kNaN;
    double rms = kNaN;
    bool ok = false;

    double At(double dt) const { return level + std::exp(-lambda * dt) * (a * std::cos(omega * dt) + b * std::sin(omega * dt)); }
};

namespace detail
{

// При заданных λ и ω — θ∞, a, b по МНК; возвращает сумму квадратов (∞ — не решить). mask — учитываемые отсчёты.
inline double SineLinear(const std::vector<double>& dt, const std::vector<double>& x, const std::vector<char>& mask,
                         double lambda, double omega, SineFit& f)
{
    double m[4][4] = {}, r[4] = {}, c[4] = {};
    for (std::size_t i = 0; i < x.size(); i++)
    {
        if (!mask[i])
            continue;
        const double e = std::exp(-lambda * dt[i]);
        const double phi[3] = {1.0, e * std::cos(omega * dt[i]), e * std::sin(omega * dt[i])};
        for (int u = 0; u < 3; u++)
        {
            r[u] += phi[u] * x[i];
            for (int v = 0; v < 3; v++)
                m[u][v] += phi[u] * phi[v];
        }
    }
    if (!Solve(3, m, r, c))
        return std::numeric_limits<double>::infinity();
    f.level = c[0];
    f.a = c[1];
    f.b = c[2];
    f.lambda = lambda;
    f.omega = omega;
    double q = 0;
    int k = 0;
    for (std::size_t i = 0; i < x.size(); i++)
        if (mask[i])
        {
            const double e = x[i] - f.At(dt[i]);
            q += e * e;
            k++;
        }
    f.rms = k ? std::sqrt(q / k) : kNaN;
    return q;
}

inline SineFit SineSearch(const std::vector<double>& dt, const std::vector<double>& x, const std::vector<char>& mask,
                          double w0)
{
    SineFit best;
    double bestQ = std::numeric_limits<double>::infinity();
    for (int iw = 0; iw <= 24; iw++)
        for (int il = 0; il <= 12; il++)
        {
            const double w = w0 * (0.6 + 0.8 * iw / 24.0);
            const double l = il == 0 ? 0.0 : 0.004 * std::pow(1.6, il - 1);
            SineFit f;
            const double q = SineLinear(dt, x, mask, l, w, f);
            if (q < bestQ)
            {
                bestQ = q;
                best = f;
            }
        }
    // Уточнение: по очереди ω и λ, шаг — вдвое меньше на каждом круге
    double sw = 0.8 * w0 / 24.0, sl = std::max(0.002, best.lambda * 0.3);
    for (int it = 0; it < 30; it++)
    {
        bool moved = false;
        for (int dir : {-1, 1})
        {
            SineFit f;
            double q = SineLinear(dt, x, mask, best.lambda, best.omega + dir * sw, f);
            if (q < bestQ)
            {
                bestQ = q;
                best = f;
                moved = true;
            }
            const double l = std::max(0.0, best.lambda + dir * sl);
            q = SineLinear(dt, x, mask, l, best.omega, f);
            if (q < bestQ)
            {
                bestQ = q;
                best = f;
                moved = true;
            }
        }
        if (!moved)
        {
            sw *= 0.5;
            sl *= 0.5;
        }
    }
    best.ok = std::isfinite(best.level);
    return best;
}

} // namespace detail

// θ(t) = θ∞ + e^{−λ(t−t0)}(a·cos ω(t−t0) + b·sin ω(t−t0)) по отсчётам участка; выбросы — вторым проходом.
inline SineFit FitDampedSine(const std::vector<double>& t, const std::vector<double>& x)
{
    SineFit f;
    const std::size_t n = std::min(t.size(), x.size());
    if (n < 20)
        return f;
    std::vector<double> dt(n);
    for (std::size_t i = 0; i < n; i++)
        dt[i] = t[i] - t[0];
    double w0 = DominantOmega(t, x);
    if (!std::isfinite(w0))
        w0 = 2 * kPi / 8.0;
    std::vector<char> mask(n, 1);
    f = detail::SineSearch(dt, x, mask, w0);
    if (!f.ok)
        return f;
    // Выбросы: остаток дальше max(5·1,4826·MAD, 1°) — второй проход без них
    std::vector<double> r(n);
    for (std::size_t i = 0; i < n; i++)
        r[i] = std::fabs(x[i] - f.At(dt[i]));
    const double thr = std::max(5.0 * 1.4826 * MedianOf(r), 1.0);
    int out = 0;
    for (std::size_t i = 0; i < n; i++)
        if (r[i] > thr)
        {
            mask[i] = 0;
            out++;
        }
    if (out)
        f = detail::SineSearch(dt, x, mask, f.omega);
    return f;
}

// ---------------------------------------------------------------------------------------------------------------
// Оценка уровня на участке
// ---------------------------------------------------------------------------------------------------------------

// Установившийся крен на участке [s0, s1]: без «головы», где крен ещё не успокоился после переноса груза, и без
// «хвоста», где он уже уходит (груз начали переносить, по палубе пошли). Скользящее среднее исходных за период качки
// Tc сравнивается с уровнем второй половины участка (медиана этих средних); с каждого края отбрасывается всё, пока не
// наберётся период подряд в пределах max(0,05°; 3·σ) (σ — по MAD средних второй половины). С каждого края — не
// больше половины участка; участок короче 4·Tc — целиком.
struct Settled
{
    double t0 = kNaN, t1 = kNaN;
};

inline Settled SettledSpan(const std::vector<double>& st, const std::vector<double>& sx, double tcS, double fop)
{
    Settled r;
    const std::size_t n = std::min(st.size(), sx.size());
    if (!n)
        return r;
    r.t0 = st.front();
    r.t1 = st[n - 1];
    if (n < 10 || !(tcS > 0) || !(fop > 0) || st[n - 1] - st.front() < 4.0 * tcS)
        return r;
    const std::size_t k = std::max<std::size_t>(3, static_cast<std::size_t>(std::lround(tcS * fop)));
    if (n < 3 * k)
        return r;
    std::vector<double> pre(n + 1, 0.0);
    for (std::size_t i = 0; i < n; i++)
        pre[i + 1] = pre[i] + sx[i];
    // m[j] — среднее отсчётов j…j+k−1
    const std::size_t nm = n - k + 1;
    std::vector<double> m(nm);
    for (std::size_t j = 0; j < nm; j++)
        m[j] = (pre[j + k] - pre[j]) / static_cast<double>(k);
    const double mid = 0.5 * (st.front() + st[n - 1]);
    std::vector<double> late;
    for (std::size_t j = 0; j < nm; j++)
        if (st[j + k / 2] >= mid)
            late.push_back(m[j]);
    if (late.size() < k)
        return r;
    const double ref = MedianOf(late);
    std::vector<double> dev;
    for (const double v : late)
        dev.push_back(std::fabs(v - ref));
    const double thr = std::max(0.05, 3.0 * 1.4826 * MedianOf(dev));
    // Хвост: с конца — до периода подряд в пределах
    std::size_t run = 0;
    for (std::size_t j = nm; j-- > 0;)
    {
        if (st[j + k - 1] < mid)
            break;
        if (std::fabs(m[j] - ref) <= thr)
        {
            if (++run >= k)
            {
                r.t1 = st[std::min(n - 1, j + 2 * k - 2)]; // конец последнего окна, ещё в пределах
                break;
            }
        }
        else
            run = 0;
    }
    // Голова: с начала — до периода подряд в пределах
    run = 0;
    for (std::size_t j = 0; j < nm; j++)
    {
        if (st[j] > mid)
            break;
        if (std::fabs(m[j] - ref) <= thr)
        {
            if (++run >= k)
            {
                r.t0 = st[j + 1 - k]; // начало первого окна в пределах
                break;
            }
        }
        else
            run = 0;
    }
    if (r.t1 - r.t0 < 3.0 * tcS) // на всякий случай — не меньше трёх периодов
    {
        r.t0 = st.front();
        r.t1 = st[n - 1];
    }
    return r;
}

struct Estimate
{
    double level = kNaN;            // установившийся крен по методу, °
    double winT0 = kNaN, winT1 = kNaN; // окно среднего (Ω), с
    double settledT0 = kNaN, settledT1 = kNaN; // установившийся крен (без переходных головы и хвоста), с
    double tcS = kNaN;              // период качки Tc, с (заданный или по данным)
    double alpha = kNaN;            // α EMA (MedianEma)
    double range = kNaN, sd = kNaN; // размах (14) и СКО (√(15)) обработанного сигнала на Ω
    std::vector<double> t, y;       // обработанный сигнал (или кривая метода) на участке — для графика
};

// Обработанный сигнал по всему ряду (для MedianEma и SavGol; у остальных — пусто).
inline std::vector<double> ProcessSeries(const std::vector<double>& t, const std::vector<double>& x, const Params& p,
                                         double* alphaOut = nullptr)
{
    if (p.method == Method::MedianEma)
    {
        const double a = p.alpha > 0 ? p.alpha : AlphaFor(p.fcHz, SampleRate(t));
        if (alphaOut)
            *alphaOut = a;
        return Ema(MedianCausal(x, p.medianN), a);
    }
    if (p.method == Method::SavGol)
        return SavGol(t, x, p.sgWindowS, p.sgOrder);
    if (p.method == Method::User && p.custom)
        return p.custom->Process(t, x, p.tcS);
    return {};
}

// Крен на участке [t0, t1] записи (t, x) по методу p. processed — ProcessSeries того же ряда (или пусто).
// Выбросы для Mean — как в Plateaus.hpp (здесь — 5 «сигм» MAD и не меньше 1°).
inline Estimate EstimateSpan(const std::vector<double>& t, const std::vector<double>& x,
                             const std::vector<double>& processed, double t0, double t1, const Params& p)
{
    Estimate e;
    std::vector<double> st, sx, sp;
    for (std::size_t i = 0; i < t.size() && i < x.size(); i++)
        if (t[i] >= t0 && t[i] <= t1 && std::isfinite(x[i]))
        {
            st.push_back(t[i]);
            sx.push_back(x[i]);
            sp.push_back(i < processed.size() ? processed[i] : kNaN);
        }
    if (st.size() < 3)
        return e;
    const double fop = SampleRate(st);
    // Tc и окно Ω = 3·Tc в конце участка (13)
    double w = DominantOmega(st, sx);
    e.tcS = p.tcS > 0 ? p.tcS : std::isfinite(w) ? std::clamp(2 * kPi / w, 2.0, 40.0) : 10.0;
    // Окно Ω — в конце установившегося крена (без переходных головы и хвоста), длина — вторая половина
    // установившегося крена, но не меньше 3·Tc статьи. Одни 3·Tc (у модели Tc ≈ 2 с — 6 с) не усредняют медленное
    // «гуляние» крена (биения, дрейф с периодом 10–20 с), и среднее на графике уходит от сигнала на 0,1–0,2°.
    const Settled set = SettledSpan(st, sx, e.tcS, fop);
    e.settledT0 = set.t0;
    e.settledT1 = set.t1;
    e.winT1 = set.t1;
    e.winT0 = p.wholeSpan ? set.t0
                          : std::max(set.t0, e.winT1 - std::max(3.0 * e.tcS, 0.5 * (set.t1 - set.t0)) + 0.5 / fop);
    std::size_t nSettled = st.size(); // отсчётов до конца установившегося крена
    while (nSettled > 1 && st[nSettled - 1] > e.winT1)
        nSettled--;
    std::size_t nWin0 = 0;            // первый отсчёт окна
    while (nWin0 + 1 < nSettled && st[nWin0] < e.winT0)
        nWin0++;
    std::size_t nHead = 0;            // первый отсчёт установившегося крена
    while (nHead + 1 < nSettled && st[nHead] < e.settledT0)
        nHead++;
    auto windowStats = [&](const std::vector<double>& y) {
        double s = 0, lo = 1e300, hi = -1e300;
        int k = 0;
        for (std::size_t i = 0; i < st.size(); i++)
            if (st[i] >= e.winT0 && st[i] <= e.winT1 && std::isfinite(y[i]))
            {
                s += y[i];
                lo = std::min(lo, y[i]);
                hi = std::max(hi, y[i]);
                k++;
            }
        if (!k)
            return kNaN;
        const double m = s / k;
        double q = 0;
        for (std::size_t i = 0; i < st.size(); i++)
            if (st[i] >= e.winT0 && st[i] <= e.winT1 && std::isfinite(y[i]))
                q += (y[i] - m) * (y[i] - m);
        e.range = hi - lo;
        e.sd = std::sqrt(q / k);
        return m;
    };
    switch (p.method)
    {
    case Method::MedianEma:
    case Method::SavGol:
    {
        if (!std::isfinite(sp.front()))
            sp = ProcessSeries(st, sx, p, &e.alpha); // ряд не дан — только по участку
        else if (p.method == Method::MedianEma)
            e.alpha = p.alpha > 0 ? p.alpha : AlphaFor(p.fcHz, SampleRate(t));
        e.level = windowStats(sp);
        e.t = st;
        e.y = sp;
        break;
    }
    case Method::Mean:
    {
        const double med = MedianOf(sx);
        std::vector<double> dev(sx.size());
        for (std::size_t i = 0; i < sx.size(); i++)
            dev[i] = std::fabs(sx[i] - med);
        const double thr = sx.size() >= 5 ? std::max(5.0 * 1.4826 * MedianOf(dev), 1.0) : 1e300;
        double s = 0;
        int k = 0;
        for (const double v : sx)
            if (std::fabs(v - med) <= thr)
            {
                s += v;
                k++;
            }
        e.level = s / k;
        e.winT0 = st.front();
        e.winT1 = st.back();
        e.t = {st.front(), st.back()};
        e.y = {e.level, e.level};
        break;
    }
    case Method::Kalman:
    {
        // Фильтр — по окну Ω (как у остальных методов): медленный уход начала участка уровень не тянет
        const std::vector<double> wt(st.begin() + static_cast<std::ptrdiff_t>(nWin0), st.begin() + static_cast<std::ptrdiff_t>(nSettled));
        const std::vector<double> wx(sx.begin() + static_cast<std::ptrdiff_t>(nWin0), sx.begin() + static_cast<std::ptrdiff_t>(nSettled));
        const std::vector<double> lev = KalmanLevel(wt, wx, std::isfinite(w) ? w : 2 * kPi / e.tcS, 0.05, MedianOf(wx));
        e.level = lev.back(); // уровень в конце установившегося крена
        e.t = wt;
        e.y = lev;
        break;
    }
    case Method::User:
    {
        if (!p.custom)
            break;
        if (!std::isfinite(sp.front()))
            sp = p.custom->Process(st, sx, p.tcS);
        // Уровень — по установившемуся крену (T0, T1 правила среднего — его начало и конец), Tc — тот же
        const auto a = static_cast<std::ptrdiff_t>(nHead), b = static_cast<std::ptrdiff_t>(nSettled);
        e.level = p.custom->Level(std::vector<double>(st.begin() + a, st.begin() + b), std::vector<double>(sp.begin() + a, sp.begin() + b),
                                  std::vector<double>(sx.begin() + a, sx.begin() + b), e.tcS, e.winT0, e.winT1, e.tcS);
        e.t = st;
        e.y = sp;
        break;
    }
    case Method::DampedSine:
    {
        const SineFit f = FitDampedSine(st, sx);
        e.level = f.ok ? f.level : kNaN;
        e.winT0 = st.front();
        e.winT1 = st.back();
        e.t = st;
        e.y.resize(st.size());
        for (std::size_t i = 0; i < st.size(); i++)
            e.y[i] = f.ok ? f.At(st[i] - st.front()) : kNaN;
        break;
    }
    default: break;
    }
    return e;
}

// ---------------------------------------------------------------------------------------------------------------
// Сравнение методов
// ---------------------------------------------------------------------------------------------------------------

struct Quality
{
    double level = kNaN;     // оценка по всему участку
    double spread = kNaN;    // СКО оценок по соседним окнам windowS с (устойчивость)
    double settleS = kNaN;   // сколько секунд от начала участка нужно, чтобы оценка дальше не уходила от итога > tol
};

inline Quality Assess(const std::vector<double>& t, const std::vector<double>& x, double t0, double t1, const Params& p,
                      double windowS = 15.0, double tol = 0.02)
{
    Quality q;
    const std::vector<double> proc = ProcessSeries(t, x, p);
    q.level = EstimateSpan(t, x, proc, t0, t1, p).level;
    // Соседние окна
    std::vector<double> lv;
    for (double a = t0; a + windowS <= t1 + 1e-9; a += windowS)
    {
        const double v = EstimateSpan(t, x, proc, a, a + windowS, p).level;
        if (std::isfinite(v))
            lv.push_back(v);
    }
    if (lv.size() >= 2)
    {
        double m = 0;
        for (const double v : lv)
            m += v;
        m /= static_cast<double>(lv.size());
        double s = 0;
        for (const double v : lv)
            s += (v - m) * (v - m);
        q.spread = std::sqrt(s / static_cast<double>(lv.size() - 1));
    }
    // Сходимость: оценка по [t0, t0 + T] для T = 5, 6, … с
    q.settleS = t1 - t0;
    for (double T = t1 - t0; T >= 5.0; T -= 1.0)
    {
        const double v = EstimateSpan(t, x, proc, t0, t0 + T, p).level;
        if (!(std::fabs(v - q.level) <= tol))
            break;
        q.settleS = T;
    }
    return q;
}

} // namespace filt
