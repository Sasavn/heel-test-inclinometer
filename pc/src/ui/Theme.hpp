#pragma once
// Палитра и стиль интерфейса (с 1.6 — «дашборд»: тёмно-синяя левая панель и шапка, светлая рабочая область,
// белые карточки со скруглением и мягкой тенью, акценты синий / зелёный / оранжевый): светлая (по умолчанию — лучше
// видна на старых мониторах и проекторах) и тёмная в том же духе.
#include <imgui.h>

namespace ui
{

struct Palette
{
    ImVec4 bg, panel, card, cardAlt, border, text, textDim, muted;
    ImVec4 accent, accentHover, accentText;
    ImVec4 ok, warn, err, info;
    ImVec4 okBg, warnBg, errBg, infoBg;
    ImVec4 button, buttonHover, buttonActive, buttonBorder, buttonDisabled, frame, frameHover;
    ImVec4 sensor[2]; // цвета Д2 и Д3 на графиках
    ImVec4 navSel, navHover;
    // Левая панель и шапка (тёмно-синие в обеих темах), акценты, тень карточек
    ImVec4 nav, navText, navMuted, navLine, navFrame;
    ImVec4 orange, green, blueSoft, greenSoft, orangeSoft, redSoft;
    ImVec4 shadow;
};

inline Palette MakeLight()
{
    Palette p;
    p.bg = {0.949f, 0.957f, 0.973f, 1.f};
    p.panel = {0.925f, 0.937f, 0.957f, 1.f};
    p.card = {1.f, 1.f, 1.f, 1.f};
    p.cardAlt = {0.965f, 0.973f, 0.984f, 1.f};
    p.border = {0.86f, 0.88f, 0.915f, 1.f};
    p.text = {0.11f, 0.13f, 0.17f, 1.f};
    p.textDim = {0.30f, 0.33f, 0.39f, 1.f};
    p.muted = {0.43f, 0.47f, 0.53f, 1.f};
    p.accent = {0.122f, 0.384f, 0.776f, 1.f};
    p.accentHover = {0.18f, 0.45f, 0.85f, 1.f};
    p.accentText = {1.f, 1.f, 1.f, 1.f};
    p.ok = {0.09f, 0.55f, 0.24f, 1.f};
    p.warn = {0.78f, 0.47f, 0.02f, 1.f};
    p.err = {0.80f, 0.16f, 0.16f, 1.f};
    p.info = {0.13f, 0.40f, 0.80f, 1.f};
    p.okBg = {0.86f, 0.95f, 0.88f, 1.f};
    p.warnBg = {1.00f, 0.94f, 0.80f, 1.f};
    p.errBg = {0.99f, 0.88f, 0.88f, 1.f};
    p.infoBg = {0.88f, 0.92f, 0.99f, 1.f};
    p.button = {1.f, 1.f, 1.f, 1.f};
    p.buttonHover = {0.925f, 0.945f, 0.98f, 1.f};
    p.buttonActive = {0.85f, 0.89f, 0.96f, 1.f};
    p.buttonBorder = {0.76f, 0.79f, 0.84f, 1.f};
    p.buttonDisabled = {0.91f, 0.92f, 0.94f, 1.f};
    p.frame = {0.965f, 0.97f, 0.98f, 1.f};
    p.frameHover = {0.93f, 0.945f, 0.97f, 1.f};
    p.sensor[0] = {0.12f, 0.42f, 0.80f, 1.f};
    p.sensor[1] = {0.88f, 0.42f, 0.05f, 1.f};
    p.nav = {0.043f, 0.200f, 0.467f, 1.f};      // #0B3377
    p.navText = {0.93f, 0.95f, 0.99f, 1.f};
    p.navMuted = {0.62f, 0.71f, 0.86f, 1.f};
    p.navLine = {1.f, 1.f, 1.f, 0.12f};
    p.navFrame = {1.f, 1.f, 1.f, 0.10f};
    p.navSel = {0.11f, 0.29f, 0.58f, 1.f};
    p.navHover = {1.f, 1.f, 1.f, 0.07f};
    p.orange = {0.96f, 0.45f, 0.13f, 1.f};
    p.green = {0.33f, 0.66f, 0.18f, 1.f};
    p.blueSoft = {0.92f, 0.95f, 1.f, 1.f};
    p.greenSoft = {0.91f, 0.97f, 0.89f, 1.f};
    p.orangeSoft = {1.f, 0.95f, 0.90f, 1.f};
    p.redSoft = {1.f, 0.92f, 0.92f, 1.f};
    p.shadow = {0.10f, 0.16f, 0.30f, 1.f};
    return p;
}

inline Palette MakeDark()
{
    Palette p;
    p.bg = {0.067f, 0.082f, 0.110f, 1.f};
    p.panel = {0.085f, 0.105f, 0.14f, 1.f};
    p.card = {0.115f, 0.140f, 0.185f, 1.f};
    p.cardAlt = {0.100f, 0.122f, 0.162f, 1.f};
    p.border = {0.19f, 0.23f, 0.30f, 1.f};
    p.text = {0.92f, 0.94f, 0.96f, 1.f};
    p.textDim = {0.75f, 0.78f, 0.83f, 1.f};
    p.muted = {0.55f, 0.60f, 0.67f, 1.f};
    p.accent = {0.25f, 0.55f, 0.98f, 1.f};
    p.accentHover = {0.35f, 0.62f, 1.f, 1.f};
    p.accentText = {1.f, 1.f, 1.f, 1.f};
    p.ok = {0.32f, 0.84f, 0.47f, 1.f};
    p.warn = {0.98f, 0.76f, 0.22f, 1.f};
    p.err = {0.97f, 0.40f, 0.38f, 1.f};
    p.info = {0.45f, 0.66f, 1.f, 1.f};
    p.okBg = {0.13f, 0.28f, 0.18f, 1.f};
    p.warnBg = {0.32f, 0.25f, 0.08f, 1.f};
    p.errBg = {0.35f, 0.13f, 0.13f, 1.f};
    p.infoBg = {0.13f, 0.21f, 0.36f, 1.f};
    p.button = {0.18f, 0.21f, 0.25f, 1.f};
    p.buttonHover = {0.22f, 0.27f, 0.33f, 1.f};
    p.buttonActive = {0.27f, 0.34f, 0.44f, 1.f};
    p.buttonBorder = {0.29f, 0.33f, 0.39f, 1.f};
    p.buttonDisabled = {0.15f, 0.17f, 0.20f, 1.f};
    p.frame = {0.17f, 0.20f, 0.24f, 1.f};
    p.frameHover = {0.21f, 0.25f, 0.30f, 1.f};
    p.sensor[0] = {0.36f, 0.62f, 1.f, 1.f};
    p.sensor[1] = {1.f, 0.62f, 0.25f, 1.f};
    p.nav = {0.035f, 0.075f, 0.165f, 1.f};
    p.navText = {0.90f, 0.93f, 0.98f, 1.f};
    p.navMuted = {0.55f, 0.63f, 0.78f, 1.f};
    p.navLine = {1.f, 1.f, 1.f, 0.10f};
    p.navFrame = {1.f, 1.f, 1.f, 0.08f};
    p.navSel = {0.13f, 0.27f, 0.52f, 1.f};
    p.navHover = {1.f, 1.f, 1.f, 0.06f};
    p.orange = {0.98f, 0.55f, 0.22f, 1.f};
    p.green = {0.45f, 0.80f, 0.32f, 1.f};
    p.blueSoft = {0.14f, 0.20f, 0.32f, 1.f};
    p.greenSoft = {0.14f, 0.25f, 0.17f, 1.f};
    p.orangeSoft = {0.30f, 0.20f, 0.11f, 1.f};
    p.redSoft = {0.32f, 0.14f, 0.15f, 1.f};
    p.shadow = {0.f, 0.f, 0.f, 1.f};
    return p;
}

inline Palette pal = MakeLight();
inline bool darkTheme = false;
// Компактный режим (экраны HD ready, окно ниже ~800 точек): меньше отступы, левая панель — значками, на «Обработке»
// графики — вкладками в одной карточке (App::Render решает по настройке «Интерфейс» и высоте окна).
inline bool compactUi = false;
inline float uiScale = 1.f;

inline float S(float px)
{
    return px * uiScale;
}

inline void ApplyTheme(bool dark, float scale)
{
    darkTheme = dark;
    pal = dark ? MakeDark() : MakeLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    if (dark)
        ImGui::StyleColorsDark(&style);
    else
        ImGui::StyleColorsLight(&style);

    style.WindowRounding = 0.f;
    style.ChildRounding = 10.f;
    style.FrameRounding = 6.f;
    style.PopupRounding = 6.f;
    style.GrabRounding = 5.f;
    style.TabRounding = 5.f;
    style.ScrollbarRounding = 8.f;
    style.WindowPadding = compactUi ? ImVec2(10, 8) : ImVec2(14, 12);
    style.FramePadding = compactUi ? ImVec2(9, 4) : ImVec2(12, 6);
    style.ItemSpacing = compactUi ? ImVec2(8, 5) : ImVec2(10, 8);
    style.ItemInnerSpacing = compactUi ? ImVec2(6, 4) : ImVec2(7, 6);
    style.CellPadding = compactUi ? ImVec2(6, 2) : ImVec2(8, 4);
    style.ChildBorderSize = 1.f;
    style.WindowBorderSize = 0.f;
    style.FrameBorderSize = 1.f;
    style.PopupBorderSize = 1.f;
    style.ScrollbarSize = compactUi ? 13.f : 16.f;
    style.GrabMinSize = 12.f;
    style.ScaleAllSizes(scale);
    uiScale = scale;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = pal.bg;
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = pal.card;
    c[ImGuiCol_Text] = pal.text;
    c[ImGuiCol_TextDisabled] = pal.muted;
    c[ImGuiCol_Border] = pal.border;
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = pal.frame;
    c[ImGuiCol_FrameBgHovered] = pal.frameHover;
    c[ImGuiCol_FrameBgActive] = pal.frameHover;
    c[ImGuiCol_Button] = pal.button;
    c[ImGuiCol_ButtonHovered] = pal.buttonHover;
    c[ImGuiCol_ButtonActive] = pal.buttonActive;
    c[ImGuiCol_Header] = ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.18f);
    c[ImGuiCol_HeaderHovered] = ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.28f);
    c[ImGuiCol_HeaderActive] = ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.40f);
    c[ImGuiCol_CheckMark] = pal.accent;
    c[ImGuiCol_SliderGrab] = pal.accent;
    c[ImGuiCol_SliderGrabActive] = pal.accentHover;
    c[ImGuiCol_Separator] = pal.border;
    c[ImGuiCol_TableHeaderBg] = pal.cardAlt;
    c[ImGuiCol_TableBorderStrong] = pal.border;
    c[ImGuiCol_TableBorderLight] = ImVec4(pal.border.x, pal.border.y, pal.border.z, 0.6f);
    c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt] = dark ? ImVec4(1, 1, 1, 0.025f) : ImVec4(0, 0, 0, 0.022f);
    c[ImGuiCol_PlotHistogram] = pal.accent;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ModalWindowDimBg] = {0.f, 0.f, 0.f, 0.45f};
    c[ImGuiCol_NavCursor] = pal.accent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.30f);
    c[ImGuiCol_TitleBg] = pal.panel;
    c[ImGuiCol_TitleBgActive] = pal.panel;
}

} // namespace ui
