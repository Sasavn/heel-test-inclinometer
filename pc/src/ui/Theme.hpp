#pragma once
// Палитра и стиль интерфейса: светлая (по умолчанию — лучше видна на старых мониторах и проекторах) и тёмная.
#include <imgui.h>

namespace ui
{

struct Palette
{
    ImVec4 bg, panel, card, cardAlt, border, text, textDim, muted;
    ImVec4 accent, accentHover, accentText;
    ImVec4 ok, warn, err, info;
    ImVec4 okBg, warnBg, errBg, infoBg;
    ImVec4 button, buttonHover, buttonActive, frame, frameHover;
    ImVec4 sensor[2]; // цвета Д2 и Д3 на графиках
    ImVec4 navSel, navHover;
};

inline Palette MakeLight()
{
    Palette p;
    p.bg = {0.945f, 0.953f, 0.965f, 1.f};
    p.panel = {0.894f, 0.910f, 0.933f, 1.f};
    p.card = {1.f, 1.f, 1.f, 1.f};
    p.cardAlt = {0.973f, 0.976f, 0.984f, 1.f};
    p.border = {0.80f, 0.82f, 0.86f, 1.f};
    p.text = {0.11f, 0.13f, 0.17f, 1.f};
    p.textDim = {0.30f, 0.33f, 0.39f, 1.f};
    p.muted = {0.43f, 0.47f, 0.53f, 1.f};
    p.accent = {0.13f, 0.40f, 0.80f, 1.f};
    p.accentHover = {0.20f, 0.48f, 0.88f, 1.f};
    p.accentText = {1.f, 1.f, 1.f, 1.f};
    p.ok = {0.09f, 0.55f, 0.24f, 1.f};
    p.warn = {0.78f, 0.47f, 0.02f, 1.f};
    p.err = {0.80f, 0.16f, 0.16f, 1.f};
    p.info = {0.13f, 0.40f, 0.80f, 1.f};
    p.okBg = {0.86f, 0.95f, 0.88f, 1.f};
    p.warnBg = {1.00f, 0.94f, 0.80f, 1.f};
    p.errBg = {0.99f, 0.88f, 0.88f, 1.f};
    p.infoBg = {0.88f, 0.92f, 0.99f, 1.f};
    p.button = {0.89f, 0.91f, 0.94f, 1.f};
    p.buttonHover = {0.82f, 0.86f, 0.92f, 1.f};
    p.buttonActive = {0.74f, 0.80f, 0.90f, 1.f};
    p.frame = {0.965f, 0.97f, 0.98f, 1.f};
    p.frameHover = {0.93f, 0.945f, 0.97f, 1.f};
    p.sensor[0] = {0.12f, 0.42f, 0.80f, 1.f};
    p.sensor[1] = {0.88f, 0.42f, 0.05f, 1.f};
    p.navSel = {0.13f, 0.40f, 0.80f, 1.f};
    p.navHover = {0.82f, 0.86f, 0.92f, 1.f};
    return p;
}

inline Palette MakeDark()
{
    Palette p;
    p.bg = {0.075f, 0.085f, 0.10f, 1.f};
    p.panel = {0.095f, 0.11f, 0.13f, 1.f};
    p.card = {0.13f, 0.15f, 0.18f, 1.f};
    p.cardAlt = {0.115f, 0.13f, 0.16f, 1.f};
    p.border = {0.22f, 0.25f, 0.30f, 1.f};
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
    p.button = {0.19f, 0.23f, 0.28f, 1.f};
    p.buttonHover = {0.24f, 0.30f, 0.37f, 1.f};
    p.buttonActive = {0.28f, 0.36f, 0.46f, 1.f};
    p.frame = {0.17f, 0.20f, 0.24f, 1.f};
    p.frameHover = {0.21f, 0.25f, 0.30f, 1.f};
    p.sensor[0] = {0.36f, 0.62f, 1.f, 1.f};
    p.sensor[1] = {1.f, 0.62f, 0.25f, 1.f};
    p.navSel = {0.25f, 0.55f, 0.98f, 1.f};
    p.navHover = {0.19f, 0.23f, 0.28f, 1.f};
    return p;
}

inline Palette pal = MakeLight();
inline bool darkTheme = false;
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
    style.ChildRounding = 8.f;
    style.FrameRounding = 5.f;
    style.PopupRounding = 6.f;
    style.GrabRounding = 5.f;
    style.TabRounding = 5.f;
    style.ScrollbarRounding = 8.f;
    style.WindowPadding = {14, 12};
    style.FramePadding = {9, 6};
    style.ItemSpacing = {10, 8};
    style.ItemInnerSpacing = {7, 6};
    style.CellPadding = {8, 5};
    style.ChildBorderSize = 1.f;
    style.WindowBorderSize = 0.f;
    style.FrameBorderSize = dark ? 0.f : 1.f;
    style.PopupBorderSize = 1.f;
    style.ScrollbarSize = 16.f;
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
