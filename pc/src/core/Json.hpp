#pragma once
// Мини-разборщик JSON: ответ прибора на команду status — одна строка JSON (README, «Командная строка по USB»).
// Без исключений: Parse() возвращает false и текст ошибки. Числа — double (все поля прибора в него помещаются точно:
// целые до 2^53). Ключи объекта — в порядке появления, поиск линейный (полей ~50).
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json
{

class Value
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    bool IsNull() const { return type == Type::Null; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }
    bool IsBool() const { return type == Type::Bool; }

    // Поле объекта; нет поля или не объект — общий пустой Value (null).
    const Value& operator[](std::string_view key) const
    {
        if (type == Type::Object)
            for (const auto& [k, v] : obj)
                if (k == key)
                    return v;
        return Empty();
    }

    const Value& operator[](std::size_t i) const
    {
        if (type == Type::Array && i < arr.size())
            return arr[i];
        return Empty();
    }

    bool Has(std::string_view key) const
    {
        if (type == Type::Object)
            for (const auto& kv : obj)
                if (kv.first == key)
                    return true;
        return false;
    }

    std::size_t Size() const { return type == Type::Array ? arr.size() : type == Type::Object ? obj.size() : 0; }

    double Num(double def = 0.0) const { return type == Type::Number ? n : type == Type::Bool ? (b ? 1.0 : 0.0) : def; }
    std::int64_t Int(std::int64_t def = 0) const
    {
        return type == Type::Number ? static_cast<std::int64_t>(n) : type == Type::Bool ? (b ? 1 : 0) : def;
    }
    bool Bool(bool def = false) const { return type == Type::Bool ? b : type == Type::Number ? n != 0.0 : def; }
    std::string Str(const std::string& def = {}) const { return type == Type::String ? s : def; }

    static const Value& Empty()
    {
        static const Value v;
        return v;
    }
};

namespace detail
{

class Parser
{
public:
    explicit Parser(std::string_view text) : t_(text) {}

    bool Run(Value& out, std::string* err)
    {
        SkipWs();
        if (!ParseValue(out, 0))
        {
            if (err)
                *err = error_ + " (позиция " + std::to_string(pos_) + ")";
            return false;
        }
        SkipWs();
        if (pos_ != t_.size())
        {
            if (err)
                *err = "лишние символы после JSON (позиция " + std::to_string(pos_) + ")";
            return false;
        }
        return true;
    }

private:
    std::string_view t_;
    std::size_t pos_ = 0;
    std::string error_;

    bool Fail(const char* what)
    {
        error_ = what;
        return false;
    }

    void SkipWs()
    {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\r' || t_[pos_] == '\n'))
            pos_++;
    }

    bool Literal(std::string_view lit)
    {
        if (t_.substr(pos_, lit.size()) != lit)
            return Fail("неизвестное слово");
        pos_ += lit.size();
        return true;
    }

    static void AppendUtf8(std::string& s, unsigned cp)
    {
        if (cp < 0x80)
            s += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseString(std::string& out)
    {
        if (pos_ >= t_.size() || t_[pos_] != '"')
            return Fail("ожидалась строка");
        pos_++;
        out.clear();
        while (pos_ < t_.size())
        {
            const char c = t_[pos_++];
            if (c == '"')
                return true;
            if (c == '\\')
            {
                if (pos_ >= t_.size())
                    break;
                const char e = t_[pos_++];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    if (pos_ + 4 > t_.size())
                        return Fail("обрыв \\u");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++)
                    {
                        const char h = t_[pos_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')
                            cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            cp |= static_cast<unsigned>(h - 'A' + 10);
                        else
                            return Fail("плохой \\u");
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return Fail("плохая escape-последовательность");
                }
            }
            else if (static_cast<unsigned char>(c) < 0x20)
                return Fail("управляющий символ в строке");
            else
                out += c;
        }
        return Fail("строка не закрыта");
    }

    bool ParseNumber(Value& out)
    {
        const std::size_t start = pos_;
        if (pos_ < t_.size() && t_[pos_] == '-')
            pos_++;
        bool digits = false;
        while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9')
        {
            pos_++;
            digits = true;
        }
        if (pos_ < t_.size() && t_[pos_] == '.')
        {
            pos_++;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9')
            {
                pos_++;
                digits = true;
            }
        }
        if (pos_ < t_.size() && (t_[pos_] == 'e' || t_[pos_] == 'E'))
        {
            pos_++;
            if (pos_ < t_.size() && (t_[pos_] == '+' || t_[pos_] == '-'))
                pos_++;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9')
                pos_++;
        }
        if (!digits)
            return Fail("плохое число");
        const std::string num(t_.substr(start, pos_ - start));
        out.type = Value::Type::Number;
        out.n = std::strtod(num.c_str(), nullptr);
        return true;
    }

    bool ParseValue(Value& out, int depth)
    {
        if (depth > 32)
            return Fail("слишком глубокая вложенность");
        SkipWs();
        if (pos_ >= t_.size())
            return Fail("конец строки вместо значения");
        const char c = t_[pos_];
        if (c == '{')
        {
            pos_++;
            out.type = Value::Type::Object;
            SkipWs();
            if (pos_ < t_.size() && t_[pos_] == '}')
            {
                pos_++;
                return true;
            }
            for (;;)
            {
                SkipWs();
                std::string key;
                if (!ParseString(key))
                    return false;
                SkipWs();
                if (pos_ >= t_.size() || t_[pos_] != ':')
                    return Fail("ожидалось ':'");
                pos_++;
                Value v;
                if (!ParseValue(v, depth + 1))
                    return false;
                out.obj.emplace_back(std::move(key), std::move(v));
                SkipWs();
                if (pos_ < t_.size() && t_[pos_] == ',')
                {
                    pos_++;
                    continue;
                }
                if (pos_ < t_.size() && t_[pos_] == '}')
                {
                    pos_++;
                    return true;
                }
                return Fail("ожидалось ',' или '}'");
            }
        }
        if (c == '[')
        {
            pos_++;
            out.type = Value::Type::Array;
            SkipWs();
            if (pos_ < t_.size() && t_[pos_] == ']')
            {
                pos_++;
                return true;
            }
            for (;;)
            {
                Value v;
                if (!ParseValue(v, depth + 1))
                    return false;
                out.arr.push_back(std::move(v));
                SkipWs();
                if (pos_ < t_.size() && t_[pos_] == ',')
                {
                    pos_++;
                    continue;
                }
                if (pos_ < t_.size() && t_[pos_] == ']')
                {
                    pos_++;
                    return true;
                }
                return Fail("ожидалось ',' или ']'");
            }
        }
        if (c == '"')
        {
            out.type = Value::Type::String;
            return ParseString(out.s);
        }
        if (c == 't')
        {
            out.type = Value::Type::Bool;
            out.b = true;
            return Literal("true");
        }
        if (c == 'f')
        {
            out.type = Value::Type::Bool;
            out.b = false;
            return Literal("false");
        }
        if (c == 'n')
        {
            out.type = Value::Type::Null;
            return Literal("null");
        }
        return ParseNumber(out);
    }
};

} // namespace detail

inline bool Parse(std::string_view text, Value& out, std::string* err = nullptr)
{
    out = Value{};
    return detail::Parser(text).Run(out, err);
}

} // namespace json
