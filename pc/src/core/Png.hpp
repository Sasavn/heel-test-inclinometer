#pragma once
// Запись PNG (RGB 8 бит) без сторонних библиотек: фильтр строк (None / Sub / Up — меньшая сумма), deflate
// (Deflate.hpp), zlib (Adler-32), CRC-32 блоков. Графики «Обработки» и снимки стенда ui_shots.
// (Из ShagomerPCModule/tests/ui_smoke/PngWriter.hpp; путь — std::filesystem::path: кириллица в имени папки.)
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Codec.hpp"
#include "Deflate.hpp"

namespace png
{

namespace detail
{

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
    Be32(file, codec::Crc32(0, file.data() + start, file.size() - start));
}

} // namespace detail

// Байты файла PNG; rgb — w·h·3 байт построчно сверху вниз. Пусто — неверный размер.
inline std::vector<unsigned char> EncodeRgb(int w, int h, const std::vector<unsigned char>& rgb)
{
    using namespace detail;
    if (w <= 0 || h <= 0 || rgb.size() < static_cast<std::size_t>(w) * h * 3)
        return {};
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
    const std::vector<unsigned char> deflated = deflate::Compress(raw.data(), raw.size());
    z.insert(z.end(), deflated.begin(), deflated.end());
    Be32(z, deflate::Adler32(raw.data(), raw.size()));

    std::vector<unsigned char> file{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<unsigned char> ihdr;
    Be32(ihdr, static_cast<std::uint32_t>(w));
    Be32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8 бит, RGB, deflate, фильтры, без чересстрочности
    Chunk(file, "IHDR", ihdr);
    Chunk(file, "IDAT", z);
    Chunk(file, "IEND", {});
    return file;
}

// Записать PNG через временный файл рядом. false — файл не записан.
inline bool WriteRgb(const std::filesystem::path& path, int w, int h, const std::vector<unsigned char>& rgb)
{
    const std::vector<unsigned char> file = EncodeRgb(w, h, rgb);
    if (file.empty())
        return false;
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        if (!f)
        {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
        std::filesystem::remove(tmp, ec);
    return !ec;
}

} // namespace png
