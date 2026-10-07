#pragma once
// (Взят из ShagomerPCModule/tests/ui_smoke/PngWriter.hpp без изменений.)
// Запись снимков стенда в PNG без сторонних библиотек (RGB 8 бит): фильтр строк (None / Sub / Up — меньшая сумма),
// deflate — LZ77 (хэш-цепочки, окно 32 КБ) с фиксированными кодами Хаффмана, zlib (Adler-32), CRC-32 блоков. Снимки
// интерфейса (большие ровные поля) сжимаются в 10–30 раз — не нужно ни PIL, ни stb_image_write.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace uismoke::png
{

namespace detail
{

inline std::uint32_t Crc32(const unsigned char* data, std::size_t n, std::uint32_t crc = 0)
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
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

class BitWriter
{
public:
    std::vector<unsigned char> out;

    void Bits(std::uint32_t value, int count) // младшими битами вперёд (порядок deflate)
    {
        acc_ |= static_cast<std::uint64_t>(value) << n_;
        n_ += count;
        while (n_ >= 8)
        {
            out.push_back(static_cast<unsigned char>(acc_ & 0xFFu));
            acc_ >>= 8;
            n_ -= 8;
        }
    }

    void Code(std::uint32_t code, int len) // код Хаффмана: старшим битом вперёд
    {
        std::uint32_t r = 0;
        for (int i = 0; i < len; i++)
            r |= ((code >> i) & 1u) << (len - 1 - i);
        Bits(r, len);
    }

    void Flush()
    {
        if (n_ > 0)
            out.push_back(static_cast<unsigned char>(acc_ & 0xFFu));
        acc_ = 0;
        n_ = 0;
    }

private:
    std::uint64_t acc_ = 0;
    int n_ = 0;
};

inline void LitLen(BitWriter& w, int sym)
{
    if (sym <= 143)
        w.Code(0x30u + static_cast<std::uint32_t>(sym), 8);
    else if (sym <= 255)
        w.Code(0x190u + static_cast<std::uint32_t>(sym - 144), 9);
    else if (sym <= 279)
        w.Code(static_cast<std::uint32_t>(sym - 256), 7);
    else
        w.Code(0xC0u + static_cast<std::uint32_t>(sym - 280), 8);
}

inline void Match(BitWriter& w, int len, int dist)
{
    static constexpr int kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static constexpr int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static constexpr int kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                          193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static constexpr int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int li = 28;
    while (li > 0 && kLenBase[li] > len)
        li--;
    LitLen(w, 257 + li);
    if (kLenExtra[li])
        w.Bits(static_cast<std::uint32_t>(len - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (di > 0 && kDistBase[di] > dist)
        di--;
    w.Code(static_cast<std::uint32_t>(di), 5);
    if (kDistExtra[di])
        w.Bits(static_cast<std::uint32_t>(dist - kDistBase[di]), kDistExtra[di]);
}

// Один блок deflate с фиксированными кодами.
inline std::vector<unsigned char> Deflate(const std::vector<unsigned char>& in)
{
    constexpr int kWindow = 32768, kMinMatch = 3, kMaxMatch = 258, kHashBits = 15, kMaxChain = 48;
    BitWriter w;
    w.Bits(1, 1); // BFINAL
    w.Bits(1, 2); // BTYPE = 01 (фиксированные коды)
    const int n = static_cast<int>(in.size());
    std::vector<int> head(1 << kHashBits, -1), prev(static_cast<std::size_t>(std::max(n, 1)), -1);
    auto hash = [&](int i) {
        const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
        return static_cast<int>((v * 2654435761u) >> (32 - kHashBits));
    };
    auto insert = [&](int i) {
        if (i + kMinMatch > n)
            return;
        const int h = hash(i);
        prev[static_cast<std::size_t>(i)] = head[static_cast<std::size_t>(h)];
        head[static_cast<std::size_t>(h)] = i;
    };
    int i = 0;
    while (i < n)
    {
        int bestLen = 0, bestDist = 0;
        if (i + kMinMatch <= n)
        {
            const int maxLen = std::min(kMaxMatch, n - i);
            int cand = head[static_cast<std::size_t>(hash(i))];
            for (int chain = 0; cand >= 0 && chain < kMaxChain && i - cand <= kWindow; chain++)
            {
                if (in[static_cast<std::size_t>(cand + bestLen)] == in[static_cast<std::size_t>(i + bestLen)])
                {
                    int l = 0;
                    while (l < maxLen && in[static_cast<std::size_t>(cand + l)] == in[static_cast<std::size_t>(i + l)])
                        l++;
                    if (l > bestLen)
                    {
                        bestLen = l;
                        bestDist = i - cand;
                        if (l == maxLen)
                            break;
                    }
                }
                cand = prev[static_cast<std::size_t>(cand)];
            }
        }
        if (bestLen >= kMinMatch)
        {
            Match(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; k++)
                insert(i + k);
            i += bestLen;
        }
        else
        {
            LitLen(w, in[static_cast<std::size_t>(i)]);
            insert(i);
            i++;
        }
    }
    LitLen(w, 256); // конец блока
    w.Flush();
    return std::move(w.out);
}

inline void Be32(std::vector<unsigned char>& v, std::uint32_t x)
{
    v.push_back(static_cast<unsigned char>(x >> 24));
    v.push_back(static_cast<unsigned char>(x >> 16));
    v.push_back(static_cast<unsigned char>(x >> 8));
    v.push_back(static_cast<unsigned char>(x));
}

inline void Chunk(std::vector<unsigned char>& file, const char* type, const std::vector<unsigned char>& data)
{
    Be32(file, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = file.size();
    file.insert(file.end(), type, type + 4);
    file.insert(file.end(), data.begin(), data.end());
    Be32(file, Crc32(file.data() + start, file.size() - start));
}

} // namespace detail

// rgb — w·h·3 байт построчно сверху вниз. false — файл не записан.
inline bool WriteRgb(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb)
{
    using namespace detail;
    if (w <= 0 || h <= 0 || rgb.size() < static_cast<std::size_t>(w) * h * 3)
        return false;
    const std::size_t stride = static_cast<std::size_t>(w) * 3;
    std::vector<unsigned char> raw;
    raw.reserve((stride + 1) * static_cast<std::size_t>(h));
    std::vector<unsigned char> cand[3];
    for (int y = 0; y < h; y++)
    {
        const unsigned char* row = rgb.data() + stride * static_cast<std::size_t>(y);
        const unsigned char* up = y > 0 ? row - stride : nullptr;
        long best = -1;
        int bestType = 0;
        for (int t = 0; t < 3; t++)
        {
            cand[t].resize(stride);
            long sum = 0;
            for (std::size_t x = 0; x < stride; x++)
            {
                const int a = x >= 3 ? row[x - 3] : 0, b = up ? up[x] : 0;
                const int pred = t == 0 ? 0 : t == 1 ? a : b;
                const unsigned char v = static_cast<unsigned char>(row[x] - pred);
                cand[t][x] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (best < 0 || sum < best)
            {
                best = sum;
                bestType = t;
            }
        }
        raw.push_back(static_cast<unsigned char>(bestType));
        raw.insert(raw.end(), cand[bestType].begin(), cand[bestType].end());
    }
    std::vector<unsigned char> z{0x78, 0x01};
    const std::vector<unsigned char> deflated = Deflate(raw);
    z.insert(z.end(), deflated.begin(), deflated.end());
    std::uint32_t s1 = 1, s2 = 0;
    for (unsigned char c : raw)
    {
        s1 = (s1 + c) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    Be32(z, (s2 << 16) | s1);

    std::vector<unsigned char> file{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<unsigned char> ihdr;
    Be32(ihdr, static_cast<std::uint32_t>(w));
    Be32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8 бит, RGB, deflate, фильтры, без чересстрочности
    Chunk(file, "IHDR", ihdr);
    Chunk(file, "IDAT", z);
    Chunk(file, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace uismoke::png
