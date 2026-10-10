#pragma once
// Сжатие deflate (RFC 1951) без сторонних библиотек: LZ77 (хэш-цепочки, окно 32 КБ) и один блок с фиксированными
// кодами Хаффмана. Нужно PNG (Png.hpp — графики «Обработки» и стенд снимков) и ZIP книги Excel (Xlsx.hpp). Сжимает
// хуже zlib (нет своих кодов Хаффмана), но снимки интерфейса и XML листов — в 5–30 раз. Распаковка (Inflate) — все
// виды блоков: книги .xlsx, пересохранённые Excel, читает «Обработка» (XlsxRead.hpp).
// (Из ShagomerPCModule/tests/ui_smoke/PngWriter.hpp; цепочки — кольцом на 32 КБ, память не растёт с размером данных.)
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
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

// ---------------------------------------------------------------------------------------------------------------
// Распаковка deflate (RFC 1951) — все виды блоков: без сжатия, фиксированные и свои коды Хаффмана (книги Excel,
// пересохранённые самим Excel). false — поток испорчен или больше maxOut байт.
// ---------------------------------------------------------------------------------------------------------------

namespace detail
{

class BitReader
{
public:
    BitReader(const unsigned char* p, std::size_t n) : p_(p), n_(n) {}
    bool Bits(int count, std::uint32_t& v)
    {
        v = 0;
        for (int i = 0; i < count; i++)
        {
            if (pos_ >= n_)
                return false;
            v |= static_cast<std::uint32_t>((p_[pos_] >> bit_) & 1u) << i;
            if (++bit_ == 8)
            {
                bit_ = 0;
                pos_++;
            }
        }
        return true;
    }
    void AlignByte()
    {
        if (bit_)
        {
            bit_ = 0;
            pos_++;
        }
    }
    bool Bytes(std::size_t count, std::vector<unsigned char>& out)
    {
        if (pos_ + count > n_)
            return false;
        out.insert(out.end(), p_ + pos_, p_ + pos_ + count);
        pos_ += count;
        return true;
    }

private:
    const unsigned char* p_;
    std::size_t n_, pos_ = 0;
    int bit_ = 0;
};

// Канонический код Хаффмана: число кодов каждой длины и символы по порядку (как в zlib puff.c).
struct Huffman
{
    std::uint16_t count[16] = {};
    std::vector<std::uint16_t> symbol;

    bool Build(const std::uint8_t* lengths, int n)
    {
        std::fill(std::begin(count), std::end(count), std::uint16_t(0));
        for (int i = 0; i < n; i++)
            count[lengths[i]]++;
        if (count[0] == n)
        {
            symbol.clear();
            return true; // пустой код (бывает у расстояний)
        }
        int left = 1;
        for (int len = 1; len < 16; len++)
        {
            left <<= 1;
            left -= count[len];
            if (left < 0)
                return false; // перебор кодов
        }
        std::uint16_t offs[16] = {};
        for (int len = 1; len < 15; len++)
            offs[len + 1] = static_cast<std::uint16_t>(offs[len] + count[len]);
        symbol.assign(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; i++)
            if (lengths[i])
                symbol[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
        return true;
    }

    int Decode(BitReader& br) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++)
        {
            std::uint32_t b = 0;
            if (!br.Bits(1, b))
                return -1;
            code |= static_cast<int>(b);
            const int c = count[len];
            if (code - c < first)
                return symbol[static_cast<std::size_t>(index + (code - first))];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

inline bool InflateCodes(BitReader& br, const Huffman& lit, const Huffman& dist, std::vector<unsigned char>& out,
                         std::size_t maxOut)
{
    static const std::uint16_t lBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const std::uint8_t lExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                            2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const std::uint16_t dBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const std::uint8_t dExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;)
    {
        const int sym = lit.Decode(br);
        if (sym < 0)
            return false;
        if (sym < 256)
        {
            if (out.size() >= maxOut)
                return false;
            out.push_back(static_cast<unsigned char>(sym));
            continue;
        }
        if (sym == 256)
            return true;
        const int li = sym - 257;
        if (li >= 29)
            return false;
        std::uint32_t e = 0;
        if (!br.Bits(lExtra[li], e))
            return false;
        const std::size_t len = lBase[li] + e;
        const int di = dist.Decode(br);
        if (di < 0 || di >= 30)
            return false;
        if (!br.Bits(dExtra[di], e))
            return false;
        const std::size_t d = dBase[di] + e;
        if (d > out.size() || out.size() + len > maxOut)
            return false;
        const std::size_t from = out.size() - d;
        for (std::size_t k = 0; k < len; k++)
            out.push_back(out[from + k]);
    }
}

} // namespace detail

inline bool Inflate(const unsigned char* in, std::size_t size, std::vector<unsigned char>& out,
                    std::size_t maxOut = std::size_t(1) << 31)
{
    using namespace detail;
    BitReader br(in, size);
    out.clear();
    for (;;)
    {
        std::uint32_t last = 0, type = 0;
        if (!br.Bits(1, last) || !br.Bits(2, type))
            return false;
        if (type == 0)
        {
            br.AlignByte();
            std::vector<unsigned char> hdr;
            if (!br.Bytes(4, hdr))
                return false;
            const std::size_t len = hdr[0] | (hdr[1] << 8), nlen = hdr[2] | (hdr[3] << 8);
            if ((len ^ 0xFFFFu) != nlen || out.size() + len > maxOut || !br.Bytes(len, out))
                return false;
        }
        else if (type == 1)
        {
            Huffman lit, dist; // фиксированные коды (построить — микросекунды)
            std::uint8_t l[288];
            for (int i = 0; i < 288; i++)
                l[i] = static_cast<std::uint8_t>(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
            std::uint8_t d[30];
            std::fill(std::begin(d), std::end(d), std::uint8_t(5));
            if (!lit.Build(l, 288) || !dist.Build(d, 30) || !InflateCodes(br, lit, dist, out, maxOut))
                return false;
        }
        else if (type == 2)
        {
            std::uint32_t hlit = 0, hdist = 0, hclen = 0;
            if (!br.Bits(5, hlit) || !br.Bits(5, hdist) || !br.Bits(4, hclen))
                return false;
            hlit += 257;
            hdist += 1;
            hclen += 4;
            if (hlit > 286 || hdist > 30)
                return false;
            static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::uint8_t cl[19] = {};
            for (std::uint32_t i = 0; i < hclen; i++)
            {
                std::uint32_t v = 0;
                if (!br.Bits(3, v))
                    return false;
                cl[order[i]] = static_cast<std::uint8_t>(v);
            }
            Huffman clh;
            if (!clh.Build(cl, 19))
                return false;
            std::uint8_t lens[286 + 30] = {};
            std::uint32_t idx = 0;
            while (idx < hlit + hdist)
            {
                const int sym = clh.Decode(br);
                if (sym < 0)
                    return false;
                if (sym < 16)
                {
                    lens[idx++] = static_cast<std::uint8_t>(sym);
                    continue;
                }
                std::uint8_t val = 0;
                std::uint32_t rep = 0;
                if (sym == 16)
                {
                    if (idx == 0 || !br.Bits(2, rep))
                        return false;
                    val = lens[idx - 1];
                    rep += 3;
                }
                else if (sym == 17)
                {
                    if (!br.Bits(3, rep))
                        return false;
                    rep += 3;
                }
                else
                {
                    if (!br.Bits(7, rep))
                        return false;
                    rep += 11;
                }
                if (idx + rep > hlit + hdist)
                    return false;
                while (rep--)
                    lens[idx++] = val;
            }
            if (lens[256] == 0)
                return false; // нет кода конца блока
            Huffman lit, dist;
            if (!lit.Build(lens, static_cast<int>(hlit)) || !dist.Build(lens + hlit, static_cast<int>(hdist)) ||
                !InflateCodes(br, lit, dist, out, maxOut))
                return false;
        }
        else
            return false;
        if (last)
            return true;
    }
}

} // namespace deflate
