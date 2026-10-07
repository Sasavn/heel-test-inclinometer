#pragma once
// Шрифты интерфейса. Основной — системный Segoe UI (обычный, Semibold для подписей и заголовков, Bold для крупных
// цифр) и Consolas для терминала из %WINDIR%\Fonts: они есть в Windows 7 и новее, с кириллицей, и выглядят как
// остальные программы Windows. Растеризация — FreeType с лёгким хинтингом: чётко, без «мыла».
// Встроенные в .exe ресурсами RCDATA (fonts.rc) DejaVu Sans / Sans Bold / Sans Mono 2.35 (лицензия —
// assets/fonts/LICENSE_DEJAVU.txt) — запасной вариант, если системного шрифта нет, и источник знаков, которых нет в
// Segoe UI (подмешиваются в тот же шрифт: MergeMode берёт только недостающие).
//
// Атлас: основной 16 px, мелкий, полужирный, заголовки, крупные цифры углов, моноширинный для терминала. Знаки
// (● ▲ ✓ ⚙ …) — только нужные (GlyphRangesBuilder), чтобы атлас не рос: старые встроенные видеокарты не берут
// текстуры больше 2048 px.
#include <imgui.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Theme.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(IMGUI_ENABLE_FREETYPE) && __has_include(<imgui_freetype.h>)
#include <imgui_freetype.h>
#define KRENOMER_FT_FLAGS ImGuiFreeTypeBuilderFlags_LightHinting
#else
#define KRENOMER_FT_FLAGS 0u
#endif

// Номера ресурсов (fonts.rc.in)
#define KRENOMER_FONT_REGULAR 101
#define KRENOMER_FONT_BOLD 102
#define KRENOMER_FONT_MONO 103

namespace ui
{

inline ImFont* fontBody = nullptr;
inline ImFont* fontSmall = nullptr;
inline ImFont* fontBold = nullptr;
inline ImFont* fontH2 = nullptr;    // заголовки карточек
inline ImFont* fontBig = nullptr;   // углы на главном экране
inline ImFont* fontHuge = nullptr;  // ГОТОВ / КАЧКА
inline ImFont* fontMono = nullptr;  // терминал, diag

// Все знаки интерфейса вне латиницы и кириллицы.
inline constexpr const char* kSymbols =
    "°±×÷·—–‒«»„“”‘’…•‰−≈≠≤≥→←↑↓↔↻⇪⇩⇧◉●○◔◕◐◑▲▼△▽▶◀■□▪▫▤▥⚙⚠⚡✓✔✗✕✖⌨☰★☆⏏№µΔα";

inline bool FontResource(int id, void** data, int* size)
{
#ifdef _WIN32
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!r)
        return false;
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g)
        return false;
    *data = LockResource(g);
    *size = static_cast<int>(SizeofResource(nullptr, r));
    return *data && *size > 0;
#else
    (void) id;
    (void) data;
    (void) size;
    return false;
#endif
}

inline const ImWchar* TextRanges()
{
    static ImVector<ImWchar> ranges;
    if (ranges.empty())
    {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesCyrillic()); // латиница, Latin-1, кириллица
        b.AddText(kSymbols);
        b.BuildRanges(&ranges);
    }
    return ranges.Data;
}

// Крупные шрифты: цифры, знаки, буквы для «ГОТОВ / КАЧКА / СБОР ДАННЫХ», °.
inline const ImWchar* BigRanges()
{
    static ImVector<ImWchar> ranges;
    if (ranges.empty())
    {
        ImFontGlyphRangesBuilder b;
        static const ImWchar base[] = {0x0020, 0x007E, 0x00B0, 0x00B0, 0x0410, 0x044F, 0x0401, 0x0401, 0x0451, 0x0451, 0};
        b.AddRanges(base);
        b.AddText("−…—●▲");
        b.BuildRanges(&ranges);
    }
    return ranges.Data;
}

inline ImFont* AddFont(ImGuiIO& io, int id, float px, const ImWchar* ranges, bool merge = false,
                       float glyphOffsetY = 0.f)
{
    void* data = nullptr;
    int size = 0;
    if (!FontResource(id, &data, &size))
        return nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; // данные — в ресурсах .exe
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.MergeMode = merge;
    cfg.GlyphOffset.y = glyphOffsetY;
    cfg.FontBuilderFlags = KRENOMER_FT_FLAGS;
    return io.Fonts->AddFontFromMemoryTTF(data, size, px, &cfg, ranges);
}

// Путь к системному шрифту Windows (пусто, если файла нет).
inline std::string SystemFontPath(const char* file)
{
#ifdef _WIN32
    char dir[MAX_PATH] = {};
    const UINT n = GetWindowsDirectoryA(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    std::string path = std::string(dir) + "\\Fonts\\" + file;
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return {};
    return path;
#else
    (void) file;
    return {};
#endif
}

// Шрифт: системный file (если есть) + подмешанный встроенный fallbackId для недостающих знаков; без системного —
// только встроенный.
inline ImFont* AddUiFont(ImGuiIO& io, const char* file, int fallbackId, float px, const ImWchar* ranges)
{
    const std::string path = file ? SystemFontPath(file) : std::string();
    if (path.empty())
        return AddFont(io, fallbackId, px, ranges);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.FontBuilderFlags = KRENOMER_FT_FLAGS;
    // У Segoe UI большой верхний запас в метриках (место под диакритику): по
    // ним ImGui ставит строку, и строчные буквы и цифры оказываются ниже середины
    // кнопок и полей на ~6 % кегля. Поднять глифы на столько же.
    cfg.GlyphOffset.y = -std::round(px * 0.06f);
    ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), px, &cfg, ranges);
    if (!f)
        return AddFont(io, fallbackId, px, ranges);
    AddFont(io, fallbackId, px, ranges, true, cfg.GlyphOffset.y); // только знаки, которых нет в системном
    return f;
}

// Загрузить шрифты в атлас (до первого кадра). scale — масштаб экрана (96 dpi = 1). Segoe UI при том же кегле
// визуально мельче DejaVu (меньше высота строчных) — ему размеры на 1–2 px больше.
// KRENOMER_EMBEDDED_FONTS=1 в окружении — только встроенные DejaVu (для сравнения и на случай проблем).
inline void SetupFonts(ImGuiIO& io, float scale)
{
    io.Fonts->Clear();
    io.Fonts->TexDesiredWidth = 2048;
    const char* env = std::getenv("KRENOMER_EMBEDDED_FONTS");
    const bool sys = !(env && env[0] == '1');
    const char* regular = sys ? "segoeui.ttf" : nullptr;
    const char* semibold = sys ? "seguisb.ttf" : nullptr;
    const char* bold = sys ? "segoeuib.ttf" : nullptr;
    const char* mono = sys ? "consola.ttf" : nullptr;
    fontBody = AddUiFont(io, regular, KRENOMER_FONT_REGULAR, (sys ? 17.f : 16.f) * scale, TextRanges());
    if (!fontBody)
    {
        // Нет ни системного шрифта, ни ресурсов (не должно быть) — встроенный шрифт ImGui, без кириллицы
        fontBody = io.Fonts->AddFontDefault();
        fontSmall = fontBold = fontH2 = fontBig = fontHuge = fontMono = fontBody;
        io.FontDefault = fontBody;
        return;
    }
    fontSmall = AddUiFont(io, regular, KRENOMER_FONT_REGULAR, (sys ? 15.f : 13.5f) * scale, TextRanges());
    fontBold = AddUiFont(io, semibold, KRENOMER_FONT_BOLD, (sys ? 17.f : 16.f) * scale, TextRanges());
    fontH2 = AddUiFont(io, semibold, KRENOMER_FONT_BOLD, (sys ? 20.5f : 19.f) * scale, TextRanges());
    fontBig = AddUiFont(io, bold, KRENOMER_FONT_BOLD, (sys ? 40.f : 38.f) * scale, BigRanges());
    fontHuge = AddUiFont(io, bold, KRENOMER_FONT_BOLD, (sys ? 31.f : 30.f) * scale, BigRanges());
    fontMono = AddUiFont(io, mono, KRENOMER_FONT_MONO, (sys ? 15.f : 14.f) * scale, TextRanges());
    io.FontDefault = fontBody;
}

} // namespace ui
