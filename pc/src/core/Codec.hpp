#pragma once
// Base64 и CRC-32 — для передачи файлов с карты (команда get: строки D,<base64>, итог E,<байт>,<crc32>).
// CRC-32 — как zlib crc32() и прошивка (usb_cli_ext_crc32): IEEE 802.3, отражённый, начальное и итоговое ~0.
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace codec
{

// Base64 (RFC 4648, с '=') -> байты в конец out. false — недопустимый знак или длина.
inline bool Base64Decode(std::string_view s, std::vector<std::uint8_t>& out)
{
    static const std::array<std::int8_t, 256> table = [] {
        std::array<std::int8_t, 256> t{};
        t.fill(-1);
        const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++)
            t[static_cast<unsigned char>(abc[i])] = static_cast<std::int8_t>(i);
        return t;
    }();
    if (s.size() % 4 != 0)
        return false;
    for (std::size_t i = 0; i < s.size(); i += 4)
    {
        const bool last = i + 4 == s.size();
        const int a = table[static_cast<unsigned char>(s[i])];
        const int b = table[static_cast<unsigned char>(s[i + 1])];
        const bool pad2 = s[i + 2] == '=';
        const bool pad3 = s[i + 3] == '=';
        if ((pad2 || pad3) && !last)
            return false;
        if (pad2 && !pad3)
            return false;
        const int c = pad2 ? 0 : table[static_cast<unsigned char>(s[i + 2])];
        const int d = pad3 ? 0 : table[static_cast<unsigned char>(s[i + 3])];
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return false;
        const std::uint32_t v = (static_cast<std::uint32_t>(a) << 18) | (static_cast<std::uint32_t>(b) << 12) |
                                (static_cast<std::uint32_t>(c) << 6) | static_cast<std::uint32_t>(d);
        out.push_back(static_cast<std::uint8_t>(v >> 16));
        if (!pad2)
            out.push_back(static_cast<std::uint8_t>(v >> 8));
        if (!pad3)
            out.push_back(static_cast<std::uint8_t>(v));
    }
    return true;
}

inline std::string Base64Encode(const std::uint8_t* p, std::size_t n)
{
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= n; i += 3)
    {
        const std::uint32_t v = (static_cast<std::uint32_t>(p[i]) << 16) | (static_cast<std::uint32_t>(p[i + 1]) << 8) | p[i + 2];
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += abc[(v >> 6) & 63];
        out += abc[v & 63];
    }
    if (i < n)
    {
        std::uint32_t v = static_cast<std::uint32_t>(p[i]) << 16;
        if (i + 1 < n)
            v |= static_cast<std::uint32_t>(p[i + 1]) << 8;
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += (i + 1 < n) ? abc[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// crc = Crc32(crc, p, n), начало — 0 (как zlib).
inline std::uint32_t Crc32(std::uint32_t crc, const std::uint8_t* p, std::size_t n)
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; i++)
        {
            std::uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

} // namespace codec
