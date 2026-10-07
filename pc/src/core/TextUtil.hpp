#pragma once
// Мелочи для текста интерфейса: UTF-8 <-> UTF-16 (Windows), размеры файлов, длительности, склонения.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace text
{

#ifdef _WIN32
inline std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
}

inline std::string Narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// Строка в кодировке Windows (ANSI) или уже UTF-8 -> UTF-8 (описания портов от драйверов).
inline std::string AcpToUtf8(const std::string& s)
{
    if (s.empty())
        return s;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) > 0)
        return s; // уже правильный UTF-8
    const int n = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return Narrow(w);
}
#endif

inline std::filesystem::path PathFromUtf8(const std::string& s)
{
#ifdef _WIN32
    return std::filesystem::path(Widen(s));
#else
    return std::filesystem::path(s);
#endif
}

inline std::string PathToUtf8(const std::filesystem::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// 1 файл, 2 файла, 5 файлов
inline const char* Plural(std::uint64_t n, const char* one, const char* few, const char* many)
{
    const std::uint64_t m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11)
        return one;
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14))
        return few;
    return many;
}

// 950 Б, 12,3 КБ, 4,56 МБ, 7,4 ГБ
inline std::string Bytes(std::uint64_t n)
{
    char buf[48];
    if (n < 1024)
        std::snprintf(buf, sizeof(buf), "%llu Б", static_cast<unsigned long long>(n));
    else if (n < 1024ull * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f КБ", n / 1024.0);
    else if (n < 1024ull * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.2f МБ", n / (1024.0 * 1024.0));
    else
        std::snprintf(buf, sizeof(buf), "%.1f ГБ", n / (1024.0 * 1024.0 * 1024.0));
    std::string s = buf;
    for (auto& c : s)
        if (c == '.')
            c = ',';
    return s;
}

// Мегабайты карты -> «7,4 ГБ» / «950 МБ»
inline std::string MegaBytes(std::uint64_t mb)
{
    char buf[48];
    if (mb >= 1024)
        std::snprintf(buf, sizeof(buf), "%.1f ГБ", mb / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%llu МБ", static_cast<unsigned long long>(mb));
    std::string s = buf;
    for (auto& c : s)
        if (c == '.')
            c = ',';
    return s;
}

// 45 с, 12 мин 03 с, 2 ч 05 мин
inline std::string Duration(std::uint64_t s)
{
    char buf[48];
    if (s < 60)
        std::snprintf(buf, sizeof(buf), "%llu с", static_cast<unsigned long long>(s));
    else if (s < 3600)
        std::snprintf(buf, sizeof(buf), "%llu мин %02llu с", static_cast<unsigned long long>(s / 60),
                      static_cast<unsigned long long>(s % 60));
    else
        std::snprintf(buf, sizeof(buf), "%llu ч %02llu мин", static_cast<unsigned long long>(s / 3600),
                      static_cast<unsigned long long>((s / 60) % 60));
    return buf;
}

// 00:01:05
inline std::string Clock(std::uint64_t s)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu", static_cast<unsigned long long>(s / 3600),
                  static_cast<unsigned long long>((s / 60) % 60), static_cast<unsigned long long>(s % 60));
    return buf;
}

// Угол со знаком: «+1.234°», «−0.567°» (минус — U+2212, ровно по ширине с плюсом), NaN — «—».
inline std::string Angle(double v, int decimals = 3, bool degree = true)
{
    if (!std::isfinite(v))
        return "—";
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, std::fabs(v));
    std::string s = (v < 0 && std::fabs(v) >= 0.5 * std::pow(10.0, -decimals)) ? "\xE2\x88\x92" : "+";
    s += buf;
    if (degree)
        s += "°";
    return s;
}

// Местное время ПК «ГГГГ-ММ-ДД ЧЧ:ММ:СС»
inline std::string LocalTimeString(std::time_t t)
{
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// «ГГГГ-ММ-ДД ЧЧ:ММ:СС» (местное время) -> time_t; -1 — не разобрано.
inline std::time_t ParseLocalTime(const std::string& s)
{
    std::tm tm{};
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
                    &tm.tm_sec) != 6)
        return -1;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

} // namespace text
