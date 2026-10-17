#pragma once
// Свой метод обработки сигнала положения — из формул (интерпретатор Expr.hpp), для апробации методов без сборки
// программы. Метод: имя, константы, стадии (выход одной — вход следующей) и правило среднего.
//
// Текст метода (файл .krenomer-method, им же методы хранятся в настройках):
//   krenomer-method 1
//   name = Медиана + EMA (статья)
//   const fc = 0.5                         ← константы по порядку; можно fs, dt, Tc, pi и прежние константы
//   const a = 2*pi*fc/(2*pi*fc + fs)
//   stage = median(x, 3)                   ← стадия: y[n] = формула; x — вход стадии (1-й — исходный крен)
//   stage = a*x + (1 - a)*y[n-1]
//   init = x[0]                            ← начальное значение y[n-k] при n-k < 0 для последней стадии (по умолчанию x[0])
//   mean = window max(3*Tc, (T1 - T0)/2)        ← среднее: window <секунд> в конце положения | all | expr <формула>
// В стадии: x, x[n-k], x[k], y[n-k] (k ≥ 1), n, t (с), fs (Гц), dt (с), Tc (с), константы, median/sma/ema(x, …).
// В правиле среднего: avg(), avgw(T), med(), medw(T), last(), T0, T1 (начало и конец установившегося крена положения —
// без переходных начала и конца участка, с), fs, dt, Tc, константы.
// Нечисловой результат стадии (деление на 0, корень из отрицательного) заменяется предыдущим значением и считается.
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "Expr.hpp"
#include "Filters.hpp"

namespace um
{

inline constexpr int kMaxStages = 8, kMaxConsts = 16;

struct Stage
{
    std::string formula;
    std::string init = "x[0]";
};

enum class MeanKind
{
    Window, // среднее по последним arg секундам положения
    All,    // по всему положению
    Expr,   // своя формула (avgw, med, …)
};

struct Method
{
    std::string name = "Свой метод";
    std::vector<std::pair<std::string, std::string>> consts; // имя, формула
    std::vector<Stage> stages;
    MeanKind mean = MeanKind::Window;
    std::string meanArg = "3*Tc";
};

// Ошибка с местом: «Стадия 2, знак 14: неизвестное имя «z»».
struct Problem
{
    std::string where;
    expr::Error e;
    std::string Text() const
    {
        if (e.Ok())
            return {};
        return where + (e.pos ? ", знак " + std::to_string(e.pos) : std::string()) + ": " + e.msg;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Текст метода
// ---------------------------------------------------------------------------------------------------------------

inline std::string ToText(const Method& m)
{
    std::string s = "krenomer-method 1\nname = " + m.name + "\n";
    for (const auto& [k, v] : m.consts)
        s += "const " + k + " = " + v + "\n";
    for (const Stage& st : m.stages)
    {
        s += "stage = " + st.formula + "\n";
        if (st.init != "x[0]")
            s += "init = " + st.init + "\n";
    }
    s += std::string("mean = ") + (m.mean == MeanKind::Window ? "window " + m.meanArg
                                   : m.mean == MeanKind::All  ? std::string("all")
                                                              : "expr " + m.meanArg) +
         "\n";
    return s;
}

inline std::string Trim(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t'))
        s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t'))
        a++;
    return s.substr(a);
}

// Разбор текста метода. false — не тот формат (err — что не так и в какой строке).
inline bool FromText(const std::string& text, Method& out, std::string* err = nullptr)
{
    Method m;
    m.consts.clear();
    m.stages.clear();
    bool head = false, meanSet = false;
    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        if (err)
            *err = "строка " + std::to_string(lineNo) + ": " + why;
        return false;
    };
    std::size_t a = 0;
    if (text.size() > 64 * 1024)
        return fail("файл слишком большой");
    while (a <= text.size())
    {
        std::size_t b = text.find('\n', a);
        if (b == std::string::npos)
            b = text.size();
        std::string line = Trim(text.substr(a, b - a));
        a = b + 1;
        lineNo++;
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF)
            line = line.substr(3); // BOM
        if (line.empty() || line[0] == '#')
            continue;
        if (!head)
        {
            if (line.rfind("krenomer-method", 0) != 0)
                return fail("это не файл метода (нет строки «krenomer-method 1»)");
            head = true;
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            return fail("нет «=»");
        const std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
        if (key == "name")
            m.name = val.substr(0, 80);
        else if (key.rfind("const ", 0) == 0)
        {
            if (static_cast<int>(m.consts.size()) >= kMaxConsts)
                return fail("констант больше " + std::to_string(kMaxConsts));
            m.consts.push_back({Trim(key.substr(6)), val});
        }
        else if (key == "stage")
        {
            if (static_cast<int>(m.stages.size()) >= kMaxStages)
                return fail("стадий больше " + std::to_string(kMaxStages));
            m.stages.push_back({val, "x[0]"});
        }
        else if (key == "init")
        {
            if (m.stages.empty())
                return fail("init — до первой стадии");
            m.stages.back().init = val;
        }
        else if (key == "mean")
        {
            meanSet = true;
            if (val == "all")
                m.mean = MeanKind::All;
            else if (val.rfind("window", 0) == 0)
            {
                m.mean = MeanKind::Window;
                m.meanArg = Trim(val.substr(6));
            }
            else if (val.rfind("expr", 0) == 0)
            {
                m.mean = MeanKind::Expr;
                m.meanArg = Trim(val.substr(4));
            }
            else
                return fail("mean: window <секунд> | all | expr <формула>");
        }
        else
            return fail("непонятный ключ «" + key + "»");
    }
    if (!head)
        return fail("пустой файл");
    if (!meanSet)
        m.mean = MeanKind::All;
    out = m;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Готовые методы
// ---------------------------------------------------------------------------------------------------------------

// Метод статьи формулами: медиана по 3 последним отсчётам (10), EMA (11) с α по (12), среднее по окну Ω в конце
// установившегося крена — вторая его половина, не короче 3·Tc (13) (как встроенный метод).
inline Method PresetArticle()
{
    Method m;
    m.name = "Медиана + EMA (статья, формулами)";
    m.consts = {{"fc", "0.5"}, {"a", "2*pi*fc/(2*pi*fc + fs)"}};
    m.stages = {{"median(x, 3)", "x[0]"}, {"a*x + (1 - a)*y[n-1]", "x[0]"}};
    m.mean = MeanKind::All; // по всему диапазону положения (T0…T1 — без переходных краёв), как встроенный
    m.meanArg.clear();
    return m;
}

inline Method PresetMean()
{
    Method m;
    m.name = "Простое среднее (без отбраковки)";
    m.stages = {{"x", "x[0]"}};
    m.mean = MeanKind::All;
    m.meanArg.clear();
    return m;
}

// Пример цепочки: медиана 5 → EMA дважды (свои α), среднее — медиана по последним 20 с.
inline Method PresetExample()
{
    Method m;
    m.name = "Пример: медиана 5 и EMA дважды";
    m.consts = {{"a", "0.15"}};
    m.stages = {{"median(x, 5)", "x[0]"}, {"ema(x, a)", "x[0]"}, {"a*x + (1 - a)*y[n-1]", "x[0]"}};
    m.mean = MeanKind::Expr;
    m.meanArg = "medw(20)";
    return m;
}

// ---------------------------------------------------------------------------------------------------------------
// Разбор и выполнение
// ---------------------------------------------------------------------------------------------------------------

class Compiled : public filt::Custom
{
public:
    // false — ошибка (problem — где и что).
    bool Build(const Method& m, Problem& problem)
    {
        method_ = m;
        consts_.clear();
        stages_.clear();
        inits_.clear();
        static const char* reserved[] = {"x", "y", "n", "t", "fs", "dt", "Tc", "pi", "T0", "T1", "if", "and", "or", "not"};
        std::vector<std::string> base = {"fs", "dt", "Tc"};
        for (std::size_t i = 0; i < m.consts.size(); i++)
        {
            const std::string& name = m.consts[i].first;
            problem.where = "Константа «" + name + "»";
            bool okName = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
            for (const char c : name)
                okName = okName && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
            for (const char* r : reserved)
                okName = okName && name != r;
            if (!okName)
            {
                problem.e.msg = "имя — латинские буквы, цифры и «_», не x, y, n, t, fs, dt, Tc, pi, T0, T1";
                return false;
            }
            expr::Env env;
            env.vars = base;
            expr::Program p;
            if (!expr::Compile(m.consts[i].second, env, p, problem.e))
                return false;
            consts_.push_back(std::move(p));
            base.push_back(name);
        }
        if (m.stages.empty())
        {
            problem.where = "Метод";
            problem.e.msg = "нет ни одной стадии (самая простая: x)";
            return false;
        }
        std::vector<std::string> stageVars = base;
        stageVars.push_back("n");
        stageVars.push_back("t");
        for (std::size_t i = 0; i < m.stages.size(); i++)
        {
            expr::Env env;
            env.vars = stageVars;
            env.allowIndex = env.allowY = env.allowSeries = true;
            problem.where = "Стадия " + std::to_string(i + 1);
            expr::Program p;
            if (!expr::Compile(m.stages[i].formula, env, p, problem.e))
                return false;
            stages_.push_back(std::move(p));
            env.allowY = env.allowSeries = false;
            problem.where = "Начальное значение стадии " + std::to_string(i + 1);
            expr::Program q;
            if (!expr::Compile(m.stages[i].init.empty() ? "x[0]" : m.stages[i].init, env, q, problem.e))
                return false;
            inits_.push_back(std::move(q));
        }
        std::vector<std::string> meanVars = base;
        meanVars.push_back("T0");
        meanVars.push_back("T1");
        problem.where = "Правило среднего";
        mean_ = expr::Program{};
        if (m.mean != MeanKind::All)
        {
            expr::Env env;
            env.vars = meanVars;
            env.allowAggregates = m.mean == MeanKind::Expr;
            if (!expr::Compile(m.meanArg, env, mean_, problem.e))
                return false;
        }
        problem = Problem{};
        return true;
    }

    std::string Name() const override { return method_.name; }
    const Method& Source() const { return method_; }
    mutable long nonFinite = 0; // стадии: нечисловых результатов (заменены предыдущим) — для сообщения

    // Значения переменных: fs, dt, Tc, константы.
    std::vector<double> Vars(double fs, double tc) const
    {
        std::vector<double> v = {fs, 1.0 / fs, tc};
        for (const auto& p : consts_)
        {
            expr::Runtime rt;
            rt.vars = v.data();
            v.push_back(expr::Eval(p, rt));
        }
        return v;
    }

    std::vector<double> Process(const std::vector<double>& t, const std::vector<double>& x, double tcS) const override
    {
        const std::size_t n = std::min(t.size(), x.size());
        if (n == 0)
            return {};
        const double fs = filt::SampleRate(t);
        double tc = tcS;
        if (!(tc > 0))
        {
            const double w = filt::DominantOmega(t, x);
            tc = std::isfinite(w) ? std::clamp(2 * filt::kPi / w, 2.0, 40.0) : 10.0;
        }
        std::vector<double> vars = Vars(fs, tc);
        const std::size_t nBase = vars.size();
        vars.push_back(0); // n
        vars.push_back(0); // t
        std::vector<double> in(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(n)), out(n);
        nonFinite = 0;
        for (std::size_t s = 0; s < stages_.size(); s++)
        {
            const expr::Program& p = stages_[s];
            // Ряды median/sma/ema по входу стадии; параметры — один раз
            std::vector<std::vector<double>> series;
            for (const auto& sc : p.series)
            {
                expr::Runtime rt0;
                rt0.vars = vars.data();
                const double par = expr::EvalNode(p, sc.param, rt0);
                if (sc.fn == expr::SeriesFn::Ema)
                    series.push_back(filt::Ema(in, std::isfinite(par) ? std::clamp(par, 0.0, 1.0) : 1.0));
                else
                {
                    const int w = std::isfinite(par) ? std::clamp(static_cast<int>(std::lround(par)), 1, 100000) : 1;
                    if (sc.fn == expr::SeriesFn::Median)
                        series.push_back(filt::MedianCausal(in, w));
                    else
                    {
                        std::vector<double> sm(n);
                        double acc = 0;
                        for (std::size_t i = 0; i < n; i++)
                        {
                            acc += in[i];
                            if (i >= static_cast<std::size_t>(w))
                                acc -= in[i - static_cast<std::size_t>(w)];
                            sm[i] = acc / static_cast<double>(std::min<std::size_t>(i + 1, static_cast<std::size_t>(w)));
                        }
                        series.push_back(std::move(sm));
                    }
                }
            }
            // Начальное значение
            expr::Runtime rt;
            rt.vars = vars.data();
            rt.series = &series;
            vars[nBase] = 0;
            vars[nBase + 1] = t[0];
            rt.n = 0;
            rt.sample = [&](int which, long idx) {
                (void) which;
                return in[static_cast<std::size_t>(std::clamp<long>(idx, 0, static_cast<long>(n) - 1))];
            };
            double init = expr::Eval(inits_[s], rt);
            if (!std::isfinite(init))
                init = in[0];
            rt.sample = [&](int which, long idx) {
                if (which == 0)
                    return in[static_cast<std::size_t>(std::clamp<long>(idx, 0, static_cast<long>(n) - 1))];
                return idx < 0 ? init : out[static_cast<std::size_t>(idx)];
            };
            for (std::size_t i = 0; i < n; i++)
            {
                vars[nBase] = static_cast<double>(i);
                vars[nBase + 1] = t[i];
                rt.n = static_cast<long>(i);
                double v = expr::Eval(p, rt);
                if (!std::isfinite(v))
                {
                    nonFinite++;
                    v = i ? out[i - 1] : init;
                }
                out[i] = v;
            }
            in = out;
        }
        return in;
    }

    double Level(const std::vector<double>& st, const std::vector<double>& sp, const std::vector<double>& sx, double tcS,
                 double& winT0, double& winT1, double& tcUsed) const override
    {
        if (st.size() < 3 || sp.size() < st.size())
            return filt::kNaN;
        const double fop = filt::SampleRate(st);
        const double w = filt::DominantOmega(st, sx);
        tcUsed = tcS > 0 ? tcS : std::isfinite(w) ? std::clamp(2 * filt::kPi / w, 2.0, 40.0) : 10.0;
        std::vector<double> vars = Vars(fop, tcUsed);
        vars.push_back(st.front()); // T0
        vars.push_back(st.back());  // T1
        winT1 = st.back();
        winT0 = st.front();
        auto range = [&](double T, bool median) {
            const double a = std::isfinite(T) ? std::max(st.front(), winT1 - T + 0.5 / fop) : st.front();
            std::vector<double> v;
            for (std::size_t i = 0; i < st.size(); i++)
                if (st[i] >= a && std::isfinite(sp[i]))
                    v.push_back(sp[i]);
            if (v.empty())
                return filt::kNaN;
            if (median)
                return filt::MedianOf(v);
            double s = 0;
            for (const double q : v)
                s += q;
            return s / static_cast<double>(v.size());
        };
        expr::Runtime rt;
        rt.vars = vars.data();
        switch (method_.mean)
        {
        case MeanKind::All: return range(filt::kNaN, false);
        case MeanKind::Window:
        {
            const double T = expr::Eval(mean_, rt);
            winT0 = std::isfinite(T) ? std::max(st.front(), winT1 - T + 0.5 / fop) : st.front();
            return range(T, false);
        }
        case MeanKind::Expr:
            rt.aggregate = [&](expr::AggFn fn, double arg) {
                switch (fn)
                {
                case expr::AggFn::Avg: return range(filt::kNaN, false);
                case expr::AggFn::AvgW: winT0 = std::max(st.front(), winT1 - arg + 0.5 / fop); return range(arg, false);
                case expr::AggFn::Med: return range(filt::kNaN, true);
                case expr::AggFn::MedW: winT0 = std::max(st.front(), winT1 - arg + 0.5 / fop); return range(arg, true);
                case expr::AggFn::Last:
                    for (std::size_t i = st.size(); i-- > 0;)
                        if (std::isfinite(sp[i]))
                            return sp[i];
                    return filt::kNaN;
                }
                return filt::kNaN;
            };
            return expr::Eval(mean_, rt);
        }
        return filt::kNaN;
    }

private:
    Method method_;
    std::vector<expr::Program> consts_, stages_, inits_;
    expr::Program mean_;
};

// Разобрать метод; nullptr — ошибка (в problem).
inline std::shared_ptr<const Compiled> Build(const Method& m, Problem& problem)
{
    auto c = std::make_shared<Compiled>();
    if (!c->Build(m, problem))
        return nullptr;
    return c;
}

} // namespace um
