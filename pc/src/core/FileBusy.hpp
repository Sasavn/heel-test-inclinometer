#pragma once
// Файл занят другой программой (Excel держит книгу, просмотрщик — картинку): сохранять под следующим свободным
// именем «Имя (2).xlsx», «Имя (3).xlsx», … — одним номером для всего комплекта (книга и картинки рядом).
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "TextUtil.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace filebusy
{

// «Отчет_Кренование.xlsx», 2 -> «Отчет_Кренование (2).xlsx»; n ≤ 1 — без номера.
inline std::string SuffixName(const std::string& name, int n)
{
    if (n <= 1)
        return name;
    const std::filesystem::path p = text::PathFromUtf8(name);
    return text::PathToUtf8(p.stem()) + " (" + std::to_string(n) + ")" + text::PathToUtf8(p.extension());
}

// Файл есть и открыт другой программой так, что заменить его нельзя (открыть на запись без совместного доступа не
// выходит). Нет файла — не занят.
inline bool Busy(const std::filesystem::path& file)
{
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return false;
#ifdef _WIN32
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return true;
    CloseHandle(h);
    return false;
#else
    return false;
#endif
}

// Наименьший номер комплекта (1 — без номера), при котором ни один из файлов names в dir не занят; 0 — нет такого
// до maxN.
inline int FreeSuffix(const std::filesystem::path& dir, const std::vector<std::string>& names, int maxN = 50)
{
    for (int n = 1; n <= maxN; n++)
    {
        bool ok = true;
        for (const auto& name : names)
            ok = ok && !Busy(dir / text::PathFromUtf8(SuffixName(name, n)));
        if (ok)
            return n;
    }
    return 0;
}

} // namespace filebusy
