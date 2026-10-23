#pragma once
// Reserve a fresh directory for one complete report set. Never reuse or remove
// an existing path. On a later export failure leave the partial set in this
// newly owned directory for diagnosis; never recursively clean a chosen parent.
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <cstdint>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace exporttarget
{
struct Result
{
    std::filesystem::path dir;
    std::string error;
    explicit operator bool() const { return error.empty() && !dir.empty(); }
};

// Strict UTF-8, including rejection of surrogate encodings and overlong forms.
inline bool ValidUtf8(std::string_view s)
{
    for (std::size_t i = 0; i < s.size();)
    {
        const auto c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        unsigned n; std::uint32_t cp;
        if (c >= 0xc2 && c <= 0xdf) { n = 1; cp = c & 31; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; cp = c & 15; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp = c & 7; }
        else return false;
        if (s.size() - i < n) return false;
        for (unsigned j = 0; j < n; ++j)
        {
            const auto d = static_cast<unsigned char>(s[i++]);
            if ((d & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (d & 63);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) ||
            (n == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}

inline std::string SafeBasename(std::string_view source)
{
    std::string name;
    // Bound the component, leaving room for suffixes even with multi-byte text.
    for (std::size_t i = 0; i < source.size();)
    {
        const auto c = static_cast<unsigned char>(source[i]);
        const std::size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (name.size() + n > 120) break;
        if (c < 32 || c == 127 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos)
            name += '_';
        else name.append(source.substr(i, n));
        i += n;
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) name = "Report";
    auto stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (auto& c : stem) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const bool numbered = stem.size() == 4 &&
        (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
        ((stem[3] >= '1' && stem[3] <= '9'));
    // Windows also recognizes superscript 1/2/3 as DOS device digits.
    const bool superNumbered = stem.size() == 5 &&
        (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
        static_cast<unsigned char>(stem[3]) == 0xc2 &&
        (static_cast<unsigned char>(stem[4]) == 0xb9 || static_cast<unsigned char>(stem[4]) == 0xb2 || static_cast<unsigned char>(stem[4]) == 0xb3);
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        stem == "CONIN$" || stem == "CONOUT$" || numbered || superNumbered) name.insert(0, "_");
    return name;
}

// Windows ordinal case comparison follows the filesystem's case-insensitive
// naming rules (including Cyrillic), without depending on the process locale.
inline bool SameFileName(const std::string& a, const std::string& b)
{
#ifdef _WIN32
    const auto wa = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(a.data()), a.size())).native();
    const auto wb = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(b.data()), b.size())).native();
    return CompareStringOrdinal(wa.data(), static_cast<int>(wa.size()), wb.data(), static_cast<int>(wb.size()), TRUE) == CSTR_EQUAL;
#else
    // Portable test fallback: ASCII and Cyrillic. Production Windows uses the
    // OS Unicode table above, not this deliberately limited fold.
    auto fold = [](const std::string& value) {
        std::vector<std::uint32_t> result;
        for (std::size_t i = 0; i < value.size();)
        {
            auto c = static_cast<unsigned char>(value[i++]);
            std::uint32_t cp = c;
            unsigned n = 0;
            if (c >= 0xf0) { cp = c & 7; n = 3; }
            else if (c >= 0xe0) { cp = c & 15; n = 2; }
            else if (c >= 0xc0) { cp = c & 31; n = 1; }
            while (n--) cp = (cp << 6) | (static_cast<unsigned char>(value[i++]) & 63);
            if (cp >= 'A' && cp <= 'Z') cp += 32;
            else if (cp >= 0x410 && cp <= 0x42f) cp += 32;
            else if (cp >= 0x400 && cp <= 0x40f) cp += 80;
            result.push_back(cp);
        }
        return result;
    };
    return fold(a) == fold(b);
#endif
}

// Call once for the WHOLE set before writing, within a freshly reserved empty
// directory. Returns leaf names only, preserving order. Malformed UTF-8 gets
// a safe fallback rather than being passed to platform path conversion.
inline std::vector<std::string> UniqueFileNames(const std::vector<std::string>& names)
{
    std::vector<std::string> result;
    result.reserve(names.size());
    for (const auto& input : names)
    {
        const auto safe = SafeBasename(ValidUtf8(input) ? std::string_view(input) : std::string_view("Report"));
        const auto dot = safe.find_last_of('.');
        const bool hasExtension = dot != std::string::npos && dot != 0;
        const auto stem = hasExtension ? safe.substr(0, dot) : safe;
        const auto ext = hasExtension ? safe.substr(dot) : std::string{};
        auto candidate = safe;
        for (std::size_t n = 2;; ++n)
        {
            bool collision = false;
            for (const auto& previous : result)
                if (SameFileName(candidate, previous)) { collision = true; break; }
            if (!collision) break;
            candidate = stem + " (" + std::to_string(n) + ")" + ext;
        }
        result.push_back(std::move(candidate));
    }
    return result;
}

inline Result Reserve(const std::filesystem::path& parent, std::string_view basenameUtf8)
{
    if (!ValidUtf8(basenameUtf8)) return {{}, "Название отчёта содержит некорректный UTF-8"};
    if (parent.empty()) return {{}, "Не выбрана папка для сохранения отчёта"};
    std::error_code ec;
    const auto root = std::filesystem::absolute(parent, ec);
    if (ec) return {{}, "Не удалось определить путь к папке отчёта: " + ec.message()};
    if (!std::filesystem::is_directory(root, ec))
        return {{}, "Папка для сохранения отчёта недоступна или не является папкой" + (ec ? ": " + ec.message() : "")};
    const auto base = SafeBasename(basenameUtf8);
    for (unsigned n = 1; n <= 10000; ++n)
    {
        const auto name = base + (n == 1 ? std::string{} : " (" + std::to_string(n) + ")");
        const auto candidate = root / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size()));
        // This operation, not an exists-then-create check, owns the reservation.
        if (std::filesystem::create_directory(candidate, ec)) return {candidate, {}};
        if (ec && ec != std::errc::file_exists)
            return {{}, "Не удалось создать папку отчёта: " + ec.message()};
        ec.clear();
    }
    return {{}, "Не удалось подобрать свободное имя папки отчёта за 10000 попыток"};
}
} // namespace exporttarget
