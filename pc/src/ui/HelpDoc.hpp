#pragma once
// Разбор текста встроенной справки (HelpText.hpp) на разделы и блоки и поиск по нему. Без ImGui — проверяется в
// process_test; рисует страница «Справка» (AppHelp.cpp).
#include <string>
#include <string_view>
#include <vector>

#include "../core/HeelData.hpp" // heel::LowerUtf8

namespace help
{

enum class Kind
{
    Para,   // абзац
    Bullet, // «- » пункт
    Step,   // «1. » шаг
    Sub,    // «## » подзаголовок
    Tip,    // «> » совет
    Table   // «| a | b |» строки, первая — заголовок
};

struct Block
{
    Kind kind = Kind::Para;
    std::string text;                           // текст (у таблицы — пусто)
    int number = 0;                             // номер шага
    std::vector<std::vector<std::string>> rows; // таблица: строки ячеек
    std::string lower;                          // текст в нижнем регистре (поиск)
    std::vector<std::string> rowsLower;         // строки таблицы в нижнем регистре
};

struct Section
{
    std::string title, lower;
    std::vector<Block> blocks;
};

inline std::string Trim(std::string_view s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        b--;
    return std::string(s.substr(a, b - a));
}

// Ячейки строки таблицы «| a | b |».
inline std::vector<std::string> Cells(const std::string& line)
{
    std::vector<std::string> out;
    std::string_view s(line);
    if (!s.empty() && s.front() == '|')
        s.remove_prefix(1);
    if (!s.empty() && s.back() == '|')
        s.remove_suffix(1);
    std::size_t from = 0;
    while (from <= s.size())
    {
        const std::size_t bar = s.find('|', from);
        out.push_back(Trim(s.substr(from, bar == std::string_view::npos ? std::string_view::npos : bar - from)));
        if (bar == std::string_view::npos)
            break;
        from = bar + 1;
    }
    return out;
}

inline std::vector<Section> Parse(std::string_view text)
{
    std::vector<Section> out;
    Block* open = nullptr; // блок, к которому присоединяются строки-продолжения
    auto add = [&](Kind k, std::string t, int number = 0) -> Block* {
        if (out.empty())
            out.push_back({});
        Block b;
        b.kind = k;
        b.text = std::move(t);
        b.number = number;
        out.back().blocks.push_back(std::move(b));
        return &out.back().blocks.back();
    };
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        const std::size_t nl = text.find('\n', pos);
        const std::string line = Trim(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (line.empty())
        {
            open = nullptr;
            continue;
        }
        if (line.rfind("## ", 0) == 0)
        {
            add(Kind::Sub, line.substr(3));
            open = nullptr;
        }
        else if (line.rfind("# ", 0) == 0)
        {
            Section s;
            s.title = line.substr(2);
            out.push_back(std::move(s));
            open = nullptr;
        }
        else if (line.rfind("- ", 0) == 0)
            open = add(Kind::Bullet, line.substr(2));
        else if (line.rfind("> ", 0) == 0)
            open = add(Kind::Tip, line.substr(2));
        else if (line.front() == '|')
        {
            if (!open || open->kind != Kind::Table)
                open = add(Kind::Table, {});
            open->rows.push_back(Cells(line));
        }
        else if (line.size() > 2 && line[0] >= '1' && line[0] <= '9' &&
                 (line.find(". ") == 1 || (line.find(". ") == 2 && line[1] >= '0' && line[1] <= '9')))
        {
            const std::size_t dot = line.find(". ");
            open = add(Kind::Step, line.substr(dot + 2), std::stoi(line.substr(0, dot)));
        }
        else if (open && open->kind != Kind::Table)
            open->text += " " + line; // продолжение абзаца или пункта
        else
            open = add(Kind::Para, line);
    }
    // пустой раздел без заголовка (текст до первого «# ») не нужен
    if (!out.empty() && out.front().title.empty() && out.front().blocks.empty())
        out.erase(out.begin());
    for (auto& s : out)
    {
        s.lower = heel::LowerUtf8(s.title);
        for (auto& b : s.blocks)
        {
            b.lower = heel::LowerUtf8(b.text);
            for (const auto& r : b.rows)
            {
                std::string joined;
                for (const auto& c : r)
                    joined += c + " ";
                b.rowsLower.push_back(heel::LowerUtf8(joined));
            }
        }
    }
    return out;
}

// Запрос поиска: нижний регистр, без пробелов по краям (пусто — показывать всё).
inline std::string Query(const std::string& q)
{
    return heel::LowerUtf8(Trim(q));
}

// Совпадения: блок (у таблицы — хотя бы одна строка кроме заголовка) / строка таблицы / раздел целиком.
inline bool RowMatches(const Block& b, std::size_t row, const std::string& q)
{
    return q.empty() || (row < b.rowsLower.size() && b.rowsLower[row].find(q) != std::string::npos);
}

inline bool BlockMatches(const Block& b, const std::string& q)
{
    if (q.empty())
        return true;
    if (b.kind == Kind::Table)
    {
        for (std::size_t r = 1; r < b.rowsLower.size(); r++)
            if (RowMatches(b, r, q))
                return true;
        return false;
    }
    return b.lower.find(q) != std::string::npos;
}

inline bool TitleMatches(const Section& s, const std::string& q)
{
    return q.empty() || s.lower.find(q) != std::string::npos;
}

// Найдено: число совпавших блоков и строк таблиц (заголовок раздела совпал — весь раздел, считается как 1).
inline int Matches(const Section& s, const std::string& q)
{
    if (q.empty())
        return 1;
    int n = TitleMatches(s, q) ? 1 : 0;
    for (const auto& b : s.blocks)
    {
        if (b.kind == Kind::Table)
        {
            for (std::size_t r = 1; r < b.rows.size(); r++)
                n += RowMatches(b, r, q) ? 1 : 0;
        }
        else
            n += BlockMatches(b, q) ? 1 : 0;
    }
    return n;
}

} // namespace help
