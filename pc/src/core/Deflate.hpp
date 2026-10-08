#pragma once
// Сжатие deflate (RFC 1951) без сторонних библиотек: LZ77 (хэш-цепочки, окно 32 КБ) и один блок с фиксированными
// кодами Хаффмана. Нужно PNG (Png.hpp — графики «Обработки» и стенд снимков) и ZIP книги Excel (Xlsx.hpp). Сжимает
// хуже zlib (нет своих кодов Хаффмана), но снимки интерфейса и XML листов — в 5–30 раз.
// (Из ShagomerPCModule/tests/ui_smoke/PngWriter.hpp; цепочки — кольцом на 32 КБ, память не растёт с размером данных.)
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace deflate
{

namespace detail
{

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

} // namespace detail

// Поток deflate (без заголовка zlib): один блок с фиксированными кодами.
inline std::vector<unsigned char> Compress(const unsigned char* in, std::size_t size)
{
    using namespace detail;
    constexpr int kWindow = 32768, kMinMatch = 3, kMaxMatch = 258, kHashBits = 15, kMaxChain = 48;
    constexpr std::size_t kMask = kWindow - 1;
    BitWriter w;
    w.out.reserve(size / 4 + 64);
    w.Bits(1, 1); // BFINAL
    w.Bits(1, 2); // BTYPE = 01 (фиксированные коды)
    const std::int64_t n = static_cast<std::int64_t>(size);
    // head — последняя позиция с этим хэшем, prev — предыдущая с тем же хэшем (кольцо: позиции старше окна не нужны)
    std::vector<std::int64_t> head(std::size_t{1} << kHashBits, -1), prev(kWindow, -1);
    auto hash = [&](std::int64_t i) {
        const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
        return static_cast<std::size_t>((v * 2654435761u) >> (32 - kHashBits));
    };
    auto insert = [&](std::int64_t i) {
        if (i + kMinMatch > n)
            return;
        const std::size_t h = hash(i);
        prev[static_cast<std::size_t>(i) & kMask] = head[h];
        head[h] = i;
    };
    std::int64_t i = 0;
    while (i < n)
    {
        int bestLen = 0;
        std::int64_t bestDist = 0;
        if (i + kMinMatch <= n)
        {
            const int maxLen = static_cast<int>(std::min<std::int64_t>(kMaxMatch, n - i));
            std::int64_t cand = head[hash(i)];
            for (int chain = 0; cand >= 0 && chain < kMaxChain && i - cand <= kWindow; chain++)
            {
                if (in[cand + bestLen] == in[i + bestLen])
                {
                    int l = 0;
                    while (l < maxLen && in[cand + l] == in[i + l])
                        l++;
                    if (l > bestLen)
                    {
                        bestLen = l;
                        bestDist = i - cand;
                        if (l == maxLen)
                            break;
                    }
                }
                const std::int64_t next = prev[static_cast<std::size_t>(cand) & kMask];
                if (next >= cand)
                    break; // ячейка кольца уже перезаписана более новой позицией
                cand = next;
            }
        }
        if (bestLen >= kMinMatch)
        {
            Match(w, bestLen, static_cast<int>(bestDist));
            for (int k = 0; k < bestLen; k++)
                insert(i + k);
            i += bestLen;
        }
        else
        {
            LitLen(w, in[i]);
            insert(i);
            i++;
        }
    }
    LitLen(w, 256); // конец блока
    w.Flush();
    return std::move(w.out);
}

inline std::vector<unsigned char> Compress(std::string_view s)
{
    return Compress(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

// Adler-32 (хвост потока zlib).
inline std::uint32_t Adler32(const unsigned char* data, std::size_t n)
{
    std::uint32_t s1 = 1, s2 = 0;
    while (n > 0)
    {
        const std::size_t k = std::min<std::size_t>(n, 5552); // без переполнения до взятия остатка
        for (std::size_t j = 0; j < k; j++)
        {
            s1 += data[j];
            s2 += s1;
        }
        s1 %= 65521u;
        s2 %= 65521u;
        data += k;
        n -= k;
    }
    return (s2 << 16) | s1;
}

} // namespace deflate
