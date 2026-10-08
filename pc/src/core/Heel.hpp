#pragma once
// Расчёт опыта кренования — отдельный модуль без файлов и интерфейса, чтобы формулы можно было уточнить или
// заменить, не трогая остальное. Вход — точки «замер × пост» (установившийся крен θ и плечо переноса груза l) и
// водоизмещение D, масса груза P; выход — таблица h по точкам, итог и оценка по МНК.
//
// Формулы — как в исходной программе обработки (BWM427_Analyzer, Romero2207; будут уточняться):
//   h = P·l / (D·tg|θ|)                         — метацентрическая высота по одному замеру и посту;
//   |θ| ≤ порога (0,1°) — точка не учитывается (h не считается);
//   итог — среднее h по всем учтённым точкам (оба поста).
// Дополнительно — МНК: прямая tg θ = k·x + b по учтённым точкам, x = ±P·l/D (знак переноса = знак крена), h = 1/k.
// Свободный член b поглощает начальный крен (неточный ноль датчика), который сдвигает «поточечные» h. Если у всех
// точек одно и то же x, прямая проводится через начало координат (b = 0).
// Углы — в градусах, l — в метрах, D и P — в тоннах (единицы D и P сокращаются).
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace heel
{

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr double kPi = 3.14159265358979323846;

// Вход: одна точка — замер на одном посту.
struct Point
{
    int number = 0;          // номер замера
    int post = 0;            // 0 — нос, 1 — корма
    double thetaDeg = kNaN;  // установившийся крен, ° (знак: + правый борт, − левый)
    double arm = kNaN;       // |l| — плечо переноса груза, м
};

// Выход по точке.
struct PointResult
{
    double h = kNaN;       // м; NaN — не посчитана
    bool counted = false;  // вошла в среднее и в МНК
    std::string note;      // почему не учтена
    double x = kNaN;       // ±P·l/D, м (абсцисса МНК)
    double tanTheta = kNaN;
};

// Прямая y = k·x + b по МНК.
struct Fit
{
    double k = kNaN, b = kNaN;
    double r2 = kNaN;         // коэффициент детерминации (NaN — прямая через ноль)
    double h = kNaN;          // 1/k, м
    int n = 0;                // точек
    bool throughOrigin = false;
};

struct Result
{
    std::vector<PointResult> points; // в порядке входа
    double h = kNaN;                 // среднее h по учтённым точкам, м
    double sd = kNaN;                // СКО h (выборочное), м
    int n = 0;                       // учтённых точек
    Fit ls;                          // МНК
};

// h = P·l / (D·tg|θ|); NaN — θ = 0 или нет данных.
inline double Height(double P, double arm, double D, double thetaDeg)
{
    const double t = std::tan(std::fabs(thetaDeg) * kPi / 180.0);
    if (!(t > 0.0) || !(D > 0.0) || !std::isfinite(P) || !std::isfinite(arm))
        return kNaN;
    return P * arm / (D * t);
}

inline Fit FitLine(const std::vector<double>& x, const std::vector<double>& y)
{
    Fit f;
    const std::size_t n = std::min(x.size(), y.size());
    f.n = static_cast<int>(n);
    if (n == 0)
        return f;
    double mx = 0, my = 0, sxx0 = 0, sxy0 = 0;
    for (std::size_t i = 0; i < n; i++)
    {
        mx += x[i];
        my += y[i];
        sxx0 += x[i] * x[i];
        sxy0 += x[i] * y[i];
    }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);
    double sxx = 0, sxy = 0, syy = 0;
    for (std::size_t i = 0; i < n; i++)
    {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
        syy += (y[i] - my) * (y[i] - my);
    }
    if (n >= 2 && sxx > 1e-12 * sxx0)
    {
        f.k = sxy / sxx;
        f.b = my - f.k * mx;
        double res = 0;
        for (std::size_t i = 0; i < n; i++)
        {
            const double e = y[i] - (f.k * x[i] + f.b);
            res += e * e;
        }
        f.r2 = syy > 0 ? 1.0 - res / syy : 1.0;
    }
    else if (sxx0 > 0)
    {
        f.k = sxy0 / sxx0;
        f.b = 0.0;
        f.throughOrigin = true;
    }
    if (f.k > 0)
        f.h = 1.0 / f.k;
    return f;
}

// Расчёт по точкам. D, P — т; thresholdDeg — порог |θ|, °.
inline Result Solve(const std::vector<Point>& in, double D, double P, double thresholdDeg)
{
    Result r;
    r.points.resize(in.size());
    std::vector<double> xs, ys, hs;
    for (std::size_t i = 0; i < in.size(); i++)
    {
        const Point& p = in[i];
        PointResult& o = r.points[i];
        if (!std::isfinite(p.thetaDeg))
        {
            o.note = "нет данных";
            continue;
        }
        if (std::fabs(p.thetaDeg) <= thresholdDeg)
        {
            o.note = "крен не больше порога";
            continue;
        }
        if (!(p.arm > 0.0))
        {
            o.note = "плечо l не задано";
            continue;
        }
        o.h = Height(P, p.arm, D, p.thetaDeg);
        if (!std::isfinite(o.h))
        {
            o.note = "D не задано";
            continue;
        }
        o.counted = true;
        o.x = (p.thetaDeg < 0 ? -1.0 : 1.0) * P * p.arm / D;
        o.tanTheta = std::tan(p.thetaDeg * kPi / 180.0);
        xs.push_back(o.x);
        ys.push_back(o.tanTheta);
        hs.push_back(o.h);
    }
    r.n = static_cast<int>(hs.size());
    if (!hs.empty())
    {
        double s = 0;
        for (const double h : hs)
            s += h;
        r.h = s / static_cast<double>(hs.size());
        if (hs.size() >= 2)
        {
            double v = 0;
            for (const double h : hs)
                v += (h - r.h) * (h - r.h);
            r.sd = std::sqrt(v / static_cast<double>(hs.size() - 1));
        }
    }
    r.ls = FitLine(xs, ys);
    return r;
}

} // namespace heel
