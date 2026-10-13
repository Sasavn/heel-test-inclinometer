#pragma once
// Маленький безопасный интерпретатор выражений для «своих методов обработки» (UserMethod.hpp): разбор в дерево и
// вычисление над double. Никакого исполнения кода — только арифметика, сравнения, условие и заранее известные функции.
//
// Синтаксис:
//   числа 1, 0.5, 2e-3 (дробная часть — через точку); скобки; + − * / ^ (степень, справа налево); унарный минус;
//   сравнения < <= > >= == != (дают 1 или 0); && || ! (или and, or, not); условие  c ? a : b  и  if(c, a, b);
//   функции abs sqrt exp ln log10 sin cos tan atan atan2 pow min max mean floor ceil round sign clamp; константа pi;
//   x[n], x[n-k], x[k] — вход стадии (k — целое число), y[n-k] — выход стадии k отсчётов назад (k ≥ 1);
//   x без индекса = x[n]; ряды  median(x, N), sma(x, N), ema(x, α)  — по входу стадии (N и α считаются один раз);
//   в правиле среднего — avg(), avgw(T), med(), medw(T), last() по обработанному сигналу положения.
// Имена переменных задаёт окружение (Env): fs, dt, Tc, n, t, константы метода и т. д.
// Ограничения: длина текста ≤ 2000 знаков, узлов дерева ≤ 1000, вложенность ≤ 64, рядов ≤ 8, сдвиг k ≤ 100000.
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace expr
{

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr std::size_t kMaxText = 2000, kMaxNodes = 1000;
inline constexpr int kMaxDepth = 64, kMaxSeries = 8, kMaxShift = 100000;

struct Error
{
    std::size_t pos = 0; // номер знака (с 1); 0 — без позиции
    std::string msg;
    bool Ok() const { return msg.empty(); }
};

enum class Op
{
    Num, Var, Index, Series, Neg, Not, Add, Sub, Mul, Div, Pow, Lt, Le, Gt, Ge, Eq, Ne, And, Or, Cond, Call, Aggregate
};

enum class Fn
{
    Abs, Sqrt, Exp, Ln, Log10, Sin, Cos, Tan, Atan, Atan2, Pow, Min, Max, Mean, Floor, Ceil, Round, Sign, Clamp, If
};

enum class SeriesFn
{
    Median, Sma, Ema
};

enum class AggFn
{
    Avg, AvgW, Med, MedW, Last
};

struct Node
{
    Op op = Op::Num;
    double num = 0;       // Num
    int slot = -1;        // Var: номер переменной; Index: 0 — x, 1 — y; Series: номер ряда
    int shift = 0;        // Index: сдвиг k (x[n-k]) или абсолютный номер (absolute)
    bool absolute = false;
    int fn = 0;           // Call: Fn; Aggregate: AggFn
    std::vector<int> kids;
    std::size_t pos = 0;
};

struct SeriesCall
{
    SeriesFn fn = SeriesFn::Median;
    int param = -1; // узел параметра (N или α), считается один раз
    std::size_t pos = 0;
};

struct Env
{
    std::vector<std::string> vars;  // переменные по номерам
    bool allowIndex = false;        // x[…], y[…]
    bool allowY = false;            // y[n-k]
    bool allowSeries = false;       // median(x,N), sma, ema
    bool allowAggregates = false;   // avg(), avgw(T), …
};

struct Program
{
    std::vector<Node> nodes;
    std::vector<SeriesCall> series;
    int root = -1;
    bool Empty() const { return root < 0; }
};

// Значения при вычислении.
struct Runtime
{
    const double* vars = nullptr;
    long n = 0;
    std::function<double(int which, long index)> sample;            // which: 0 — x, 1 — y; index — абсолютный
    const std::vector<std::vector<double>>* series = nullptr;        // ряды (по номерам SeriesCall)
    std::function<double(AggFn fn, double arg)> aggregate;           // правило среднего
};

namespace detail
{

// Номер знака (UTF-8) по смещению в байтах — для сообщений «в позиции N».
inline std::size_t CharPos(std::string_view s, std::size_t byte)
{
    std::size_t n = 0;
    for (std::size_t i = 0; i < byte && i < s.size(); i++)
        n += (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
    return n + 1;
}

class Parser
{
public:
    Parser(std::string_view s, const Env& env, Program& p) : s_(s), env_(env), p_(p) {}

    bool Run(Error& err)
    {
        if (s_.size() > kMaxText)
            return Fail(err, 0, "формула длиннее " + std::to_string(kMaxText) + " знаков");
        Skip();
        if (pos_ >= s_.size())
            return Fail(err, pos_, "пустая формула");
        const int r = Ternary(0);
        if (!err_.msg.empty())
        {
            err = err_;
            return false;
        }
        Skip();
        if (pos_ < s_.size())
        {
            const char c = s_[pos_];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || std::string_view("_.()[],+-*/^<>=!?:&|").find(c) != std::string_view::npos))
                return Fail(err, pos_, std::string("непонятный знак «") +
                                           std::string(s_.substr(pos_, (static_cast<unsigned char>(c) & 0x80) ? 2 : 1)) + "»");
            return Fail(err, pos_, "лишнее в конце: «" + std::string(s_.substr(pos_, 12)) + "» (не хватает знака действия?)");
        }
        p_.root = r;
        return true;
    }

private:
    std::string_view s_;
    const Env& env_;
    Program& p_;
    std::size_t pos_ = 0;
    Error err_;

    bool Fail(Error& err, std::size_t at, const std::string& msg)
    {
        err.pos = CharPos(s_, at);
        err.msg = msg;
        return false;
    }
    int Bad(std::size_t at, const std::string& msg)
    {
        if (err_.msg.empty())
        {
            err_.pos = CharPos(s_, at);
            err_.msg = msg;
        }
        return -1;
    }
    bool Failed() const { return !err_.msg.empty(); }

    void Skip()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n'))
            pos_++;
    }
    bool Eat(std::string_view tok)
    {
        Skip();
        if (s_.substr(pos_, tok.size()) == tok)
        {
            pos_ += tok.size();
            return true;
        }
        return false;
    }
    bool PeekWord(std::string_view w)
    {
        Skip();
        if (s_.substr(pos_, w.size()) != w)
            return false;
        const std::size_t e = pos_ + w.size();
        return e >= s_.size() || !(std::isalnum(static_cast<unsigned char>(s_[e])) || s_[e] == '_');
    }

    int Add(Node n)
    {
        if (p_.nodes.size() >= kMaxNodes)
            return Bad(n.pos, "формула слишком сложная (больше " + std::to_string(kMaxNodes) + " элементов)");
        p_.nodes.push_back(std::move(n));
        return static_cast<int>(p_.nodes.size()) - 1;
    }
    int Make(Op op, std::size_t at, std::vector<int> kids)
    {
        for (const int k : kids)
            if (k < 0)
                return -1;
        Node n;
        n.op = op;
        n.pos = at;
        n.kids = std::move(kids);
        return Add(std::move(n));
    }

    int Ternary(int depth)
    {
        if (depth > kMaxDepth)
            return Bad(pos_, "слишком глубокая вложенность");
        const int c = Or(depth);
        if (Failed())
            return -1;
        Skip();
        const std::size_t at = pos_;
        if (Eat("?"))
        {
            const int a = Ternary(depth + 1);
            if (Failed())
                return -1;
            if (!Eat(":"))
                return Bad(pos_, "ожидалось «:» в условии  c ? a : b");
            const int b = Ternary(depth + 1);
            return Make(Op::Cond, at, {c, a, b});
        }
        return c;
    }
    int Or(int d)
    {
        int l = And(d);
        for (;;)
        {
            Skip();
            const std::size_t at = pos_;
            if (Eat("||") || (PeekWord("or") && Eat("or")))
                l = Make(Op::Or, at, {l, And(d)});
            else
                return l;
            if (Failed())
                return -1;
        }
    }
    int And(int d)
    {
        int l = Cmp(d);
        for (;;)
        {
            Skip();
            const std::size_t at = pos_;
            if (Eat("&&") || (PeekWord("and") && Eat("and")))
                l = Make(Op::And, at, {l, Cmp(d)});
            else
                return l;
            if (Failed())
                return -1;
        }
    }
    int Cmp(int d)
    {
        const int l = Sum(d);
        Skip();
        const std::size_t at = pos_;
        static const struct
        {
            const char* t;
            Op op;
        } ops[] = {{"<=", Op::Le}, {">=", Op::Ge}, {"==", Op::Eq}, {"!=", Op::Ne}, {"<", Op::Lt}, {">", Op::Gt}};
        for (const auto& o : ops)
            if (Eat(o.t))
                return Make(o.op, at, {l, Sum(d)});
        return l;
    }
    int Sum(int d)
    {
        int l = Prod(d);
        for (;;)
        {
            Skip();
            const std::size_t at = pos_;
            if (Eat("+"))
                l = Make(Op::Add, at, {l, Prod(d)});
            else if (Eat("-"))
                l = Make(Op::Sub, at, {l, Prod(d)});
            else
                return l;
            if (Failed())
                return -1;
        }
    }
    int Prod(int d)
    {
        int l = Unary(d);
        for (;;)
        {
            Skip();
            const std::size_t at = pos_;
            if (Eat("*"))
                l = Make(Op::Mul, at, {l, Unary(d)});
            else if (Eat("/"))
                l = Make(Op::Div, at, {l, Unary(d)});
            else
                return l;
            if (Failed())
                return -1;
        }
    }
    int Unary(int d)
    {
        if (d > kMaxDepth)
            return Bad(pos_, "слишком глубокая вложенность");
        Skip();
        const std::size_t at = pos_;
        if (Eat("-"))
            return Make(Op::Neg, at, {Unary(d + 1)});
        if (Eat("+"))
            return Unary(d + 1);
        if ((s_.substr(pos_, 1) == "!" && s_.substr(pos_, 2) != "!=" && Eat("!")) || (PeekWord("not") && Eat("not")))
            return Make(Op::Not, at, {Unary(d + 1)});
        return Power(d);
    }
    int Power(int d)
    {
        const int b = Primary(d);
        Skip();
        const std::size_t at = pos_;
        if (Eat("^"))
            return Make(Op::Pow, at, {b, Unary(d + 1)});
        return b;
    }

    int Primary(int d)
    {
        Skip();
        const std::size_t at = pos_;
        if (pos_ >= s_.size())
            return Bad(at, "формула оборвана: ожидалось число, имя или «(»");
        const char c = s_[pos_];
        if (c == '(')
        {
            pos_++;
            const int e = Ternary(d + 1);
            if (Failed())
                return -1;
            if (!Eat(")"))
                return Bad(pos_, "ожидалась «)»");
            return e;
        }
        if ((c >= '0' && c <= '9') || c == '.')
        {
            const std::string z(s_.substr(pos_, 40));
            char* end = nullptr;
            const double v = std::strtod(z.c_str(), &end);
            if (end == z.c_str())
                return Bad(at, "не число");
            pos_ += static_cast<std::size_t>(end - z.c_str());
            Node n;
            n.op = Op::Num;
            n.num = v;
            n.pos = at;
            return Add(n);
        }
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_'))
            return Bad(at, std::string("непонятный знак «") + std::string(s_.substr(pos_, (static_cast<unsigned char>(c) & 0x80) ? 2 : 1)) + "»");
        std::size_t e = pos_;
        while (e < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[e])) || s_[e] == '_'))
            e++;
        const std::string name(s_.substr(pos_, e - pos_));
        pos_ = e;
        Skip();
        // Индекс: x[…], y[…]
        if (pos_ < s_.size() && s_[pos_] == '[')
            return IndexRef(name, at);
        // Вызов функции
        if (pos_ < s_.size() && s_[pos_] == '(')
            return CallRef(name, at, d);
        if (name == "pi")
        {
            Node n;
            n.op = Op::Num;
            n.num = 3.14159265358979323846;
            n.pos = at;
            return Add(n);
        }
        if (name == "x" && env_.allowIndex)
        {
            Node n;
            n.op = Op::Index;
            n.slot = 0;
            n.shift = 0;
            n.pos = at;
            return Add(n);
        }
        if (name == "y" && env_.allowIndex)
            return Bad(at, "y без индекса: пишите y[n-1] (предыдущее значение выхода)");
        for (std::size_t i = 0; i < env_.vars.size(); i++)
            if (env_.vars[i] == name)
            {
                Node n;
                n.op = Op::Var;
                n.slot = static_cast<int>(i);
                n.pos = at;
                return Add(n);
            }
        return Bad(at, "неизвестное имя «" + name + "»");
    }

    int IndexRef(const std::string& name, std::size_t at)
    {
        if (!env_.allowIndex || (name != "x" && name != "y"))
            return Bad(at, "индекс [ ] можно только у x и y" + std::string(env_.allowIndex ? "" : " и только в стадиях"));
        if (name == "y" && !env_.allowY)
            return Bad(at, "y[…] здесь нельзя (только в формуле стадии)");
        pos_++; // [
        Skip();
        Node n;
        n.op = Op::Index;
        n.slot = name == "y" ? 1 : 0;
        n.pos = at;
        if (PeekWord("n"))
        {
            pos_++;
            Skip();
            if (Eat("-"))
            {
                Skip();
                std::size_t e = pos_;
                while (e < s_.size() && std::isdigit(static_cast<unsigned char>(s_[e])))
                    e++;
                if (e == pos_)
                    return Bad(pos_, "индекс: после «n-» нужно целое число, например y[n-1]");
                n.shift = std::atoi(std::string(s_.substr(pos_, e - pos_)).c_str());
                pos_ = e;
            }
            else
                n.shift = 0;
        }
        else
        {
            std::size_t e = pos_;
            while (e < s_.size() && std::isdigit(static_cast<unsigned char>(s_[e])))
                e++;
            if (e == pos_)
                return Bad(pos_, "индекс: только n, n-k или целое число (x[0])");
            n.shift = std::atoi(std::string(s_.substr(pos_, e - pos_)).c_str());
            n.absolute = true;
            pos_ = e;
        }
        if (n.shift > kMaxShift)
            return Bad(at, "сдвиг больше " + std::to_string(kMaxShift));
        if (!Eat("]"))
            return Bad(pos_, "индекс: только n, n-k или целое число (x[0]); ожидалась «]»");
        if (n.slot == 1 && (n.absolute || n.shift < 1))
            return Bad(at, "y[n] ещё не посчитано: пишите y[n-1], y[n-2], …");
        return Add(n);
    }

    int CallRef(const std::string& name, std::size_t at, int d)
    {
        pos_++; // (
        std::vector<int> args;
        Skip();
        if (!Eat(")"))
        {
            for (;;)
            {
                args.push_back(Ternary(d + 1));
                if (Failed())
                    return -1;
                if (Eat(")"))
                    break;
                if (!Eat(","))
                    return Bad(pos_, "ожидалась «,» или «)» в вызове " + name + "(…)");
            }
        }
        auto argc = [&](std::size_t lo, std::size_t hi) -> bool {
            if (args.size() < lo || args.size() > hi)
            {
                Bad(at, name + "(…): нужно " +
                            (lo == hi ? std::to_string(lo) : std::to_string(lo) + "…" + (hi > 99 ? "∞" : std::to_string(hi))) +
                            " аргумент(а), дано " + std::to_string(args.size()));
                return false;
            }
            return true;
        };
        // Ряды по входу стадии
        static const struct
        {
            const char* n;
            SeriesFn f;
        } series[] = {{"median", SeriesFn::Median}, {"sma", SeriesFn::Sma}, {"ema", SeriesFn::Ema}};
        for (const auto& sf : series)
            if (name == sf.n)
            {
                if (!env_.allowSeries)
                    return Bad(at, name + "(x, …) можно только в формуле стадии");
                if (!argc(2, 2))
                    return -1;
                const Node& a0 = p_.nodes[static_cast<std::size_t>(args[0])];
                if (!(a0.op == Op::Index && a0.slot == 0 && a0.shift == 0 && !a0.absolute))
                    return Bad(at, name + ": первый аргумент — x (вход стадии); для цепочки фильтров — несколько стадий");
                if (p_.series.size() >= static_cast<std::size_t>(kMaxSeries))
                    return Bad(at, "слишком много median/sma/ema в стадии");
                p_.series.push_back({sf.f, args[1], at});
                Node n;
                n.op = Op::Series;
                n.slot = static_cast<int>(p_.series.size()) - 1;
                n.pos = at;
                return Add(n);
            }
        // Правило среднего
        static const struct
        {
            const char* n;
            AggFn f;
            std::size_t argc;
        } aggs[] = {{"avg", AggFn::Avg, 0}, {"avgw", AggFn::AvgW, 1}, {"med", AggFn::Med, 0}, {"medw", AggFn::MedW, 1},
                    {"last", AggFn::Last, 0}};
        for (const auto& ag : aggs)
            if (name == ag.n)
            {
                if (!env_.allowAggregates)
                    return Bad(at, name + "() можно только в правиле среднего");
                if (!argc(ag.argc, ag.argc))
                    return -1;
                Node n;
                n.op = Op::Aggregate;
                n.fn = static_cast<int>(ag.f);
                n.kids = args;
                n.pos = at;
                return Add(n);
            }
        static const struct
        {
            const char* n;
            Fn f;
            std::size_t lo, hi;
        } fns[] = {{"abs", Fn::Abs, 1, 1},     {"sqrt", Fn::Sqrt, 1, 1},   {"exp", Fn::Exp, 1, 1},
                   {"ln", Fn::Ln, 1, 1},       {"log10", Fn::Log10, 1, 1}, {"sin", Fn::Sin, 1, 1},
                   {"cos", Fn::Cos, 1, 1},     {"tan", Fn::Tan, 1, 1},     {"atan", Fn::Atan, 1, 1},
                   {"atan2", Fn::Atan2, 2, 2}, {"pow", Fn::Pow, 2, 2},     {"min", Fn::Min, 1, 1000},
                   {"max", Fn::Max, 1, 1000},  {"mean", Fn::Mean, 1, 1000}, {"floor", Fn::Floor, 1, 1},
                   {"ceil", Fn::Ceil, 1, 1},   {"round", Fn::Round, 1, 1}, {"sign", Fn::Sign, 1, 1},
                   {"clamp", Fn::Clamp, 3, 3}, {"if", Fn::If, 3, 3}};
        for (const auto& f : fns)
            if (name == f.n)
            {
                if (!argc(f.lo, f.hi))
                    return -1;
                Node n;
                n.op = Op::Call;
                n.fn = static_cast<int>(f.f);
                n.kids = args;
                n.pos = at;
                return Add(n);
            }
        return Bad(at, "неизвестная функция «" + name + "»");
    }
};

inline double Bool(bool b) { return b ? 1.0 : 0.0; }

} // namespace detail

inline bool Compile(std::string_view src, const Env& env, Program& p, Error& err)
{
    p = Program{};
    err = Error{};
    detail::Parser ps(src, env, p);
    if (!ps.Run(err))
    {
        p = Program{};
        return false;
    }
    return true;
}

inline double EvalNode(const Program& p, int id, const Runtime& rt)
{
    const Node& n = p.nodes[static_cast<std::size_t>(id)];
    auto k = [&](std::size_t i) { return EvalNode(p, n.kids[i], rt); };
    switch (n.op)
    {
    case Op::Num: return n.num;
    case Op::Var: return rt.vars ? rt.vars[n.slot] : kNaN;
    case Op::Index:
        if (!rt.sample)
            return kNaN;
        return rt.sample(n.slot, n.absolute ? n.shift : rt.n - n.shift);
    case Op::Series:
    {
        if (!rt.series || n.slot >= static_cast<int>(rt.series->size()))
            return kNaN;
        const auto& s = (*rt.series)[static_cast<std::size_t>(n.slot)];
        return rt.n >= 0 && rt.n < static_cast<long>(s.size()) ? s[static_cast<std::size_t>(rt.n)] : kNaN;
    }
    case Op::Neg: return -k(0);
    case Op::Not: return detail::Bool(k(0) == 0.0);
    case Op::Add: return k(0) + k(1);
    case Op::Sub: return k(0) - k(1);
    case Op::Mul: return k(0) * k(1);
    case Op::Div: return k(0) / k(1);
    case Op::Pow: return std::pow(k(0), k(1));
    case Op::Lt: return detail::Bool(k(0) < k(1));
    case Op::Le: return detail::Bool(k(0) <= k(1));
    case Op::Gt: return detail::Bool(k(0) > k(1));
    case Op::Ge: return detail::Bool(k(0) >= k(1));
    case Op::Eq: return detail::Bool(k(0) == k(1));
    case Op::Ne: return detail::Bool(k(0) != k(1));
    case Op::And: return detail::Bool(k(0) != 0.0 && k(1) != 0.0);
    case Op::Or: return detail::Bool(k(0) != 0.0 || k(1) != 0.0);
    case Op::Cond: return k(0) != 0.0 ? k(1) : k(2);
    case Op::Aggregate:
        return rt.aggregate ? rt.aggregate(static_cast<AggFn>(n.fn), n.kids.empty() ? kNaN : k(0)) : kNaN;
    case Op::Call:
        switch (static_cast<Fn>(n.fn))
        {
        case Fn::Abs: return std::fabs(k(0));
        case Fn::Sqrt: return std::sqrt(k(0));
        case Fn::Exp: return std::exp(k(0));
        case Fn::Ln: return std::log(k(0));
        case Fn::Log10: return std::log10(k(0));
        case Fn::Sin: return std::sin(k(0));
        case Fn::Cos: return std::cos(k(0));
        case Fn::Tan: return std::tan(k(0));
        case Fn::Atan: return std::atan(k(0));
        case Fn::Atan2: return std::atan2(k(0), k(1));
        case Fn::Pow: return std::pow(k(0), k(1));
        case Fn::Floor: return std::floor(k(0));
        case Fn::Ceil: return std::ceil(k(0));
        case Fn::Round: return std::round(k(0));
        case Fn::Sign: return k(0) > 0 ? 1.0 : k(0) < 0 ? -1.0 : 0.0;
        case Fn::Clamp: return std::fmin(std::fmax(k(0), k(1)), k(2));
        case Fn::If: return k(0) != 0.0 ? k(1) : k(2);
        case Fn::Min:
        case Fn::Max:
        case Fn::Mean:
        {
            double r = k(0), s = r;
            for (std::size_t i = 1; i < n.kids.size(); i++)
            {
                const double v = k(i);
                s += v;
                r = static_cast<Fn>(n.fn) == Fn::Min ? std::fmin(r, v) : std::fmax(r, v);
            }
            return static_cast<Fn>(n.fn) == Fn::Mean ? s / static_cast<double>(n.kids.size()) : r;
        }
        }
        return kNaN;
    }
    return kNaN;
}

inline double Eval(const Program& p, const Runtime& rt)
{
    return p.Empty() ? kNaN : EvalNode(p, p.root, rt);
}

// Число из выражения без переменных (для проверки констант при разборе), NaN — не посчитать.
inline double Constant(std::string_view src, const Env& env, const std::vector<double>& vars, Error& err)
{
    Program p;
    if (!Compile(src, env, p, err))
        return kNaN;
    Runtime rt;
    rt.vars = vars.data();
    return Eval(p, rt);
}

} // namespace expr
