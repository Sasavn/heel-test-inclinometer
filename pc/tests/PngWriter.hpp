#pragma once
// Запись снимков стенда в PNG — общий с программой писатель src/core/Png.hpp (им же «Обработка» сохраняет графики;
// раньше здесь была его копия из ShagomerPCModule/tests/ui_smoke/PngWriter.hpp).
#include <string>
#include <vector>

#include "core/Png.hpp"

namespace uismoke::png
{

// rgb — w·h·3 байт построчно сверху вниз. false — файл не записан.
inline bool WriteRgb(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb)
{
    return ::png::WriteRgb(std::filesystem::path(path), w, h, rgb);
}

} // namespace uismoke::png
