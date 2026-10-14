#pragma once
// Обработка и проверка опыта кренования по Правилам РС: Часть IV «Остойчивость», 1.5 (1.5.8, 1.5.9), и
// Инструктивные указания по кренованию судов (приложение 3 к разд. 2 Руководства по техническому наблюдению за
// постройкой судов): табл. 6.5 (h_i по приращениям), 6.6 (h_k и оценка качества), разд. 3 (условия), п. 5.5 (график
// контроля). Чистые функции без интерфейса; расчёт h «от начального положения + МНК» — в Heel.hpp, этот — рядом.
//
// Замер i (табл. 6.5) — переход между соседними учтёнными положениями: приращение кренящего момента
// ΔM_i = P·(l_i − l_{i−1}), т·м, приращение угла Δθ_i = θ_i − θ_{i−1}, рад; h_i = ΔM_i / (Δ·Δθ_i) (столбец IV).
// h_k = Σh_i/n (6.6). Качество (6.6):
//   6.6.1 |h_i − h_k| ≤ 2·√(Σ(h_i − h_k)²/(n − 1)); не отвечающий — исключается (не более одного: худший), h_k заново;
//   6.6.2 вероятная ошибка t_αn·√(Σ(h_i − h_k)²/(n(n − 1))) ≤ 0,02(1 + h_k) при h_k ≤ 2 м, ≤ 0,01(4 + h_k) при h_k > 2 м;
//         t_αn — табл. 6.6.2 (n = 8…16);
//   6.6.3 вероятная ошибка · Δ0/Δ ≤ ε, ε = min(0,05·h; 0,10·l_max) по наихудшему варианту загрузки, не менее 4 см;
//   6.6.4 удовлетворительных замеров не менее 8;
//   6.6.5 если не выполнено — в расчёт h_k минус вероятная ошибка.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace rs
{

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr double kPi = 3.14159265358979323846;

// t_αn — табл. 6.6.2 Инструктивных указаний (= табл. 1.5.9.2 Части IV): n = 8 … 16; вне таблицы — NaN.
inline double TAlpha(int n)
{
    static const double t[] = {5.4, 5.0, 4.8, 4.6, 4.5, 4.3, 4.2, 4.1, 4.0};
    return n >= 8 && n <= 16 ? t[n - 8] : kNaN;
}

struct Input
{
    double D = kNaN, P = kNaN;      // т (водоизмещение при креновании, масса крен-балласта)
    std::vector<int> number;        // номера положений / замеров (для подписей)
    std::vector<double> theta;      // крен положения, °
    std::vector<double> arm;        // плечо от начального положения, м (со знаком: + к правому борту)
    std::vector<int> manual;        // по замерам (переход в положение i, i ≥ 1): -1 — авто, 0 — исключить, 1 — учитывать
    double hInfo = kNaN, lMax = kNaN, D0 = kNaN; // 6.6.3: h и l_max по Информации об остойчивости, Δ0 — порожнем, т
    // Несколько приборов (постов): крен положения по каждому, °, [прибор][положение]. Если задано — Δθ замера берётся
    // отсюда: среднее приращений приборов (instrument = -1) или приращение прибора instrument; theta — только для
    // подписей и знака старого ввода плеч.
    std::vector<std::vector<double>> post;
    int instrument = -1;
    // Приращения кренящего момента по замерам, т·м (группы крен-балласта, табл. перемещений); пусто — P·Δl по arm.
    std::vector<double> dM;
    double Dworst = kNaN;           // 6.6.3: Δ наихудшего варианта загрузки, т (NaN — D)
    double dh = 0.0;                // поправка на свободные поверхности, м: h исправленная = h − dh
};

struct Meas
{
    int from = 0, to = 0;           // индексы положений
    double dM = kNaN, dTheta = kNaN, h = kNaN; // т·м, ° (среднее приборов или выбранный), м
    std::vector<double> dThetaPost, hPost;     // по приборам (Input::post): Δθ, ° и h_i, м
    double spread = kNaN;                      // расхождение Δθ приборов (наибольшее − наименьшее), °
    bool excluded = false;          // не в расчёте h_k
    bool autoExcluded = false;      // исключён по 6.6.1 автоматически
    int manual = -1;
};

struct Result
{
    std::vector<Meas> m;
    int n = 0;                      // замеров в h_k (удовлетворительных)
    double hk = kNaN, sigma = kNaN; // м
    double sumSq = kNaN;            // Σ(h_i − h_k)²
    double tA = kNaN;               // t_αn
    double err = kNaN, errLimit = kNaN; // вероятная ошибка и предел 6.6.2, м
    double eps = kNaN, err663 = kNaN;   // 6.6.3: ε и ошибка·Δ0/Δ, м
    bool ok661 = false, ok662 = false, ok664 = false;
    int ok663 = -1;                 // 1 — да, 0 — нет, -1 — нет данных
    int excludedCount = 0;
    double hFinal = kNaN;           // итог: h_k, а при невыполнении 6.6.1–6.6.4 — h_k минус вероятная ошибка (6.6.5);
                                    // с поправкой на свободные поверхности: минус dh
    double hFinalRaw = kNaN;        // тот же итог без поправки на свободные поверхности
    double dh = 0.0;                // поправка на свободные поверхности, м
    bool reduced = false;           // 6.6.5 применён
    double maxSpread = kNaN;        // наибольшее расхождение Δθ приборов по замерам, °
    int maxSpreadAt = -1;           // замер (индекс) с наибольшим расхождением
    double Dratio = kNaN;           // 6.6.3: Δ0/Δ (Δ — наихудшего варианта или D)
    std::string note;               // почему не посчитано
};

inline void Stats(Result& r)
{
    double s = 0;
    int n = 0;
    for (const Meas& q : r.m)
        if (!q.excluded && std::isfinite(q.h))
        {
            s += q.h;
            n++;
        }
    r.n = n;
    if (n == 0)
    {
        r.hk = r.sigma = r.sumSq = r.err = kNaN;
        return;
    }
    r.hk = s / n;
    double v = 0;
    for (const Meas& q : r.m)
        if (!q.excluded && std::isfinite(q.h))
            v += (q.h - r.hk) * (q.h - r.hk);
    r.sumSq = v;
    r.sigma = n >= 2 ? std::sqrt(v / (n - 1)) : kNaN;
    r.tA = TAlpha(n);
    r.err = n >= 2 && std::isfinite(r.tA) ? r.tA * std::sqrt(v / (n * (n - 1.0))) : kNaN;
}

inline Result Evaluate(const Input& in)
{
    Result r;
    std::size_t k = in.theta.size();
    if (in.dM.empty())
        k = std::min(k, in.arm.size());
    else
        k = std::min(k, in.dM.size() + 1);
    auto height = [&](double dM, double dThetaDeg) {
        const double rad = dThetaDeg * kPi / 180.0;
        // табл. 6.5: IV = II / (III·Δ); без перемещения груза (ΔM = 0) или без изменения крена — не замер
        return std::fabs(rad) > 1e-9 && std::fabs(dM) > 1e-12 ? dM / (in.D * rad) : kNaN;
    };
    for (std::size_t i = 1; i < k; i++)
    {
        Meas q;
        q.from = static_cast<int>(i) - 1;
        q.to = static_cast<int>(i);
        q.dM = in.dM.empty() ? in.P * (in.arm[i] - in.arm[i - 1]) : in.dM[i - 1];
        if (in.post.empty())
            q.dTheta = in.theta[i] - in.theta[i - 1];
        else
        {
            // по приборам: приращение каждого, среднее (или выбранный), расхождение
            double sum = 0, lo = 1e300, hi = -1e300;
            int c = 0;
            q.dThetaPost.assign(in.post.size(), kNaN);
            q.hPost.assign(in.post.size(), kNaN);
            for (std::size_t p = 0; p < in.post.size(); p++)
            {
                if (i >= in.post[p].size())
                    continue;
                const double d = in.post[p][i] - in.post[p][i - 1];
                if (!std::isfinite(d))
                    continue;
                q.dThetaPost[p] = d;
                q.hPost[p] = height(q.dM, d);
                sum += d;
                lo = std::min(lo, d);
                hi = std::max(hi, d);
                c++;
            }
            q.spread = c >= 2 ? hi - lo : kNaN;
            const bool one = in.instrument >= 0 && static_cast<std::size_t>(in.instrument) < in.post.size();
            q.dTheta = one ? q.dThetaPost[static_cast<std::size_t>(in.instrument)] : c ? sum / c : kNaN;
            if (std::isfinite(q.spread) && !(q.spread <= r.maxSpread))
            {
                r.maxSpread = q.spread;
                r.maxSpreadAt = static_cast<int>(i) - 1;
            }
        }
        q.h = height(q.dM, q.dTheta);
        q.manual = i - 1 < in.manual.size() ? in.manual[i - 1] : -1;
        q.excluded = q.manual == 0 || !std::isfinite(q.h);
        r.m.push_back(q);
    }
    if (r.m.empty() || !(in.D > 0))
    {
        r.note = r.m.empty() ? "нужно два положения и больше" : "не задано водоизмещение";
        return r;
    }
    Stats(r);
    // 6.6.1: худший из не отвечающих условию — исключается (если вручную не помечен «учитывать»); не более одного
    auto worst = [&] {
        int w = -1;
        double wd = 0;
        for (std::size_t i = 0; i < r.m.size(); i++)
        {
            const Meas& q = r.m[i];
            if (q.excluded || !std::isfinite(q.h) || !std::isfinite(r.sigma))
                continue;
            // допуск на округление: при одинаковых h_i σ = 0 и разница — порядка 1e-16
            const double d = std::fabs(q.h - r.hk);
            if (d > 2 * r.sigma + 1e-9 * (1.0 + std::fabs(r.hk)) && d > wd)
            {
                wd = d;
                w = static_cast<int>(i);
            }
        }
        return w;
    };
    int manualOut = 0;
    for (const Meas& q : r.m)
        manualOut += q.manual == 0;
    const int w = worst();
    if (w >= 0 && manualOut == 0 && r.m[static_cast<std::size_t>(w)].manual != 1)
    {
        r.m[static_cast<std::size_t>(w)].excluded = true;
        r.m[static_cast<std::size_t>(w)].autoExcluded = true;
        Stats(r);
    }
    r.excludedCount = 0;
    for (const Meas& q : r.m)
        r.excludedCount += q.excluded && std::isfinite(q.h);
    r.ok661 = worst() < 0 && r.excludedCount <= 1;
    // 6.6.2
    r.errLimit = std::isfinite(r.hk) ? (r.hk <= 2.0 ? 0.02 * (1 + r.hk) : 0.01 * (4 + r.hk)) : kNaN;
    r.ok662 = std::isfinite(r.err) && r.err <= r.errLimit;
    // 6.6.3
    if (std::isfinite(in.hInfo) || std::isfinite(in.lMax))
    {
        double e = std::numeric_limits<double>::infinity();
        if (std::isfinite(in.hInfo))
            e = std::min(e, 0.05 * in.hInfo);
        if (std::isfinite(in.lMax))
            e = std::min(e, 0.10 * in.lMax);
        r.eps = std::max(e, 0.04);
        const double ratio = in.D0 > 0 ? in.D0 / (in.Dworst > 0 ? in.Dworst : in.D) : 1.0;
        r.Dratio = ratio;
        r.err663 = r.err * ratio;
        r.ok663 = std::isfinite(r.err663) ? (r.err663 <= r.eps ? 1 : 0) : 0;
    }
    // 6.6.4
    r.ok664 = r.n >= 8;
    // 6.6.5
    const bool ok = r.ok661 && r.ok662 && r.ok664 && r.ok663 != 0;
    r.reduced = !ok && std::isfinite(r.err);
    r.hFinalRaw = r.reduced ? r.hk - r.err : r.hk;
    r.dh = std::isfinite(in.dh) ? in.dh : 0.0;
    r.hFinal = r.hFinalRaw - r.dh;
    if (r.n < 8)
        r.note = "замеров " + std::to_string(r.n) + " — меньше 8: t_αn по табл. 6.6.2 нет, вероятная ошибка не считается";
    else if (r.n > 16)
        r.note = "замеров " + std::to_string(r.n) + " — больше 16: t_αn по табл. 6.6.2 нет";
    return r;
}

// Строка таблицы требований.
struct Check
{
    std::string req, ref, value;
    std::string id; // коротко — для строки итога («6.6.4», «крен 1°–4°»)
    int state = -1; // 1 — выполнено, 0 — нарушено, -1 — нет данных / не проверялось
};

struct Conditions
{
    double theta0 = kNaN;          // начальный крен (абсолютный), °
    std::vector<double> dTheta;    // крен положений относительно начального, °
    int instruments = 0;           // постов (датчиков) с данными
    double spread = kNaN, spreadLimit = 0.1;  // наибольшее расхождение приращений Δθ приборов на замере, °
    double h = kNaN;               // h для проверки ≥ 0,20 м (основной расчёт)
    bool signedArms = true;        // плечи введены со знаком
    std::vector<std::string> labels; // подписи положений (номера); пусто — 1, 2, …
};

inline std::string F(const char* fmt, double v)
{
    char b[64];
    std::snprintf(b, sizeof(b), fmt, v);
    return b;
}

inline std::vector<Check> Checks(const Result& r, const Conditions& c)
{
    std::vector<Check> out;
    static const char* ids[] = {"6.6.4", "6.6.1", "6.6.2", "6.6.3", "h ≥ 0,20 м", "крен 1°–4°", "нач. крен",
                                "возврат в ноль", "2 прибора", "сверка постов", "знак плеч"};
    auto add = [&](const std::string& req, const std::string& ref, const std::string& val, int st) {
        Check c{req, ref, val, ""};
        c.state = st;
        c.id = out.size() < std::size(ids) ? ids[out.size()] : ref;
        out.push_back(c);
    };
    add("Удовлетворительных замеров не менее 8", "6.6.4 прил. 3", std::to_string(r.n), r.ok664 ? 1 : 0);
    add("|h_i − h_k| ≤ 2σ, исключено не более одного замера", "6.6.1 прил. 3",
        r.excludedCount == 0 ? std::string("все в пределах") : "исключено " + std::to_string(r.excludedCount),
        std::isfinite(r.hk) ? (r.ok661 ? 1 : 0) : -1);
    add("Вероятная ошибка ≤ " + std::string(r.hk > 2 ? "0,01(4 + h_k)" : "0,02(1 + h_k)"), "6.6.2 прил. 3",
        std::isfinite(r.err) ? F("%.4f", r.err) + " ≤ " + F("%.4f м", r.errLimit) : std::string("n вне 8…16"),
        std::isfinite(r.err) ? (r.ok662 ? 1 : 0) : 0);
    add("Ошибка · Δ0/Δ ≤ ε = min(0,05h; 0,10 l_max), ≥ 4 см", "6.6.3 прил. 3",
        r.ok663 < 0 ? std::string("нужны h и l_max") : F("%.4f", r.err663) + " ≤ " + F("%.3f м", r.eps), r.ok663);
    add("h при креновании ≥ 0,20 м", "1.5 ч. IV, разд. 3", F("%.3f м", c.h), std::isfinite(c.h) ? (c.h >= 0.20 ? 1 : 0) : -1);
    double pb = 0, lb = 0;
    for (const double d : c.dTheta)
    {
        pb = std::max(pb, d);
        lb = std::min(lb, d);
    }
    add("Крен 1°–4° на каждый борт", "разд. 3 прил. 3", F("ПБ %.2f°", pb) + F(", ЛБ %.2f°", -lb),
        c.dTheta.empty() ? -1 : (pb >= 1 && pb <= 4 && -lb >= 1 && -lb <= 4 ? 1 : 0));
    add("Начальный крен ≤ 0,5°", "разд. 3 прил. 3", F("%+.2f°", c.theta0),
        std::isfinite(c.theta0) ? (std::fabs(c.theta0) <= 0.5 ? 1 : 0) : -1);
    // Возврат в ноль: положение (не первое), крен которого — у начального (меньше 10 % наибольшего, не меньше 0,2°)
    double big = std::max(pb, -lb);
    int zero = -1;
    for (std::size_t i = 1; i < c.dTheta.size() && zero < 0; i++) // первый возврат
        if (std::fabs(c.dTheta[i]) <= std::max(0.2, 0.1 * big))
            zero = static_cast<int>(i);
    add("Возврат в ноль (перемещения 4 / 8)", "п. 5.5 прил. 3",
        zero >= 0 ? "положение " + (static_cast<std::size_t>(zero) < c.labels.size() ? c.labels[static_cast<std::size_t>(zero)]
                                                                               : std::to_string(zero + 1)) : std::string(r.ok661 ? "нет (прямая — можно не делать)" : "нет"),
        zero >= 0 || r.ok661 ? 1 : 0);
    add("Не менее 2 приборов (весков / датчиков)", "разд. 3 прил. 3", std::to_string(c.instruments),
        c.instruments >= 2 ? 1 : 0);
    add("Сверка приборов: расхождение приращений Δθ на замере ≤ " + F("%.2f°", c.spreadLimit), "разд. 3 прил. 3",
        std::isfinite(c.spread) ? F("%.3f°", c.spread) + (r.maxSpreadAt >= 0 ? ", замер " + std::to_string(r.maxSpreadAt + 1) : std::string())
                                : std::string("один прибор"),
        std::isfinite(c.spread) ? (c.spread <= c.spreadLimit ? 1 : 0) : -1);
    add("Плечи со знаком (+ к правому борту)", "табл. 6.5", c.signedArms ? "да" : "без знака — по знаку крена",
        c.signedArms ? 1 : -1);
    return out;
}

} // namespace rs
