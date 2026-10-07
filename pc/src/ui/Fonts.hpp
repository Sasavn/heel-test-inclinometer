#pragma once
// Шрифты интерфейса — DejaVu Sans / Sans Bold / Sans Mono 2.35 (полные, с кириллицей; лицензия —
// assets/fonts/LICENSE_DEJAVU.txt). Встроены в .exe ресурсами RCDATA (fonts.rc): программа не зависит от шрифтов
// Windows и выглядит одинаково на Windows 7 и 10.
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

inline ImFont* AddFont(ImGuiIO& io, int id, float px, const ImWchar* ranges)
{
    void* data = nullptr;
    int size = 0;
    if (!FontResource(id, &data, &size))
        return nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; // данные — в ресурсах .exe
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    return io.Fonts->AddFontFromMemoryTTF(data, size, px, &cfg, ranges);
}

// Загрузить шрифты в атлас (до первого кадра). scale — масштаб экрана (96 dpi = 1).
inline void SetupFonts(ImGuiIO& io, float scale)
{
    io.Fonts->Clear();
    io.Fonts->TexDesiredWidth = 2048;
    fontBody = AddFont(io, KRENOMER_FONT_REGULAR, 16.f * scale, TextRanges());
    if (!fontBody)
    {
        // Нет ресурсов (не должно быть) — встроенный шрифт ImGui, без кириллицы
        fontBody = io.Fonts->AddFontDefault();
        fontSmall = fontBold = fontH2 = fontBig = fontHuge = fontMono = fontBody;
        io.FontDefault = fontBody;
        return;
    }
    fontSmall = AddFont(io, KRENOMER_FONT_REGULAR, 13.5f * scale, TextRanges());
    fontBold = AddFont(io, KRENOMER_FONT_BOLD, 16.f * scale, TextRanges());
    fontH2 = AddFont(io, KRENOMER_FONT_BOLD, 19.f * scale, TextRanges());
    fontBig = AddFont(io, KRENOMER_FONT_BOLD, 38.f * scale, BigRanges());
    fontHuge = AddFont(io, KRENOMER_FONT_BOLD, 30.f * scale, BigRanges());
    fontMono = AddFont(io, KRENOMER_FONT_MONO, 14.f * scale, TextRanges());
    io.FontDefault = fontBody;
}

} // namespace ui
