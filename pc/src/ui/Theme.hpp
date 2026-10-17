#pragma once
// Палитра и стиль интерфейса (с 1.6 — «дашборд»: тёмно-синяя левая панель и шапка, светлая рабочая область,
// белые карточки со скруглением и мягкой тенью): светлая (по умолчанию — лучше видна на старых мониторах и
// проекторах) и тёмная в том же духе. С 1.8.1 — цвета руководителя: тёмно-синий #103269 — основной (шапка, панель),
// оранжевый #F87128 — главное действие (тёмный текст: белый на нём — 2,8 : 1, тёмный — 5,9 : 1), лимонный #B8E039 —
// заметные кнопки («Параметры», тёмный текст 11 : 1); ок / предупреждение / ошибка / инфо — свои, не путаются с
// акцентом (красная ошибка ≠ красно-оранжевый #DF5023; рядом всегда значок ✓ ⚠ ✗ — для дальтоников).
#include <imgui.h>

namespace ui
{

// Цвет из 0xRRGGBB
constexpr ImVec4 Hex(unsigned rgb, float a = 1.f)
{
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.f, static_cast<float>((rgb >> 8) & 0xFF) / 255.f,
                  static_cast<float>(rgb & 0xFF) / 255.f, a);
}

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
    ImVec4 lemon, onAccent; // заметная кнопка (#B8E039) и текст на оранжевом / лимонном (тёмный) — вариант, в кнопках не используется
    ImVec4 action, actionHover, actionText; // главные действия: заливка, наведение, текст (≥ 7 : 1)
    ImVec4 shadow;
};

inline Palette MakeLight()
{
    Palette p;
    p.bg = Hex(0xF3F5F9);
    p.panel = Hex(0xE9EDF4);
    p.card = Hex(0xFFFFFF);
    p.cardAlt = Hex(0xF6F8FB);
    p.border = Hex(0xD5DBE6);
    p.text = Hex(0x1A1D24);      // 16,9 : 1 на карточке
    p.textDim = Hex(0x4A5468);   // 7,6 : 1
    p.muted = Hex(0x6B7489);     // 4,7 : 1 — подписи (AA)
    p.accent = Hex(0x1D4E9E);    // выбор, ссылки: 8,0 : 1 на белом, белый текст на нём — 8,0 : 1
    p.accentHover = Hex(0x2A5FB5);
    p.accentText = {1.f, 1.f, 1.f, 1.f};
    p.ok = Hex(0x1E7B3A);        // 5,3 : 1
    p.warn = Hex(0x8A5A00);      // 5,9 : 1
    p.err = Hex(0xC0262D);       // 5,9 : 1
    p.info = Hex(0x1F5FBF);      // 6,1 : 1
    p.okBg = {0.86f, 0.95f, 0.88f, 1.f};
    p.warnBg = {1.00f, 0.94f, 0.80f, 1.f};
    p.errBg = {0.99f, 0.88f, 0.88f, 1.f};
    p.infoBg = {0.88f, 0.92f, 0.99f, 1.f};
    p.button = {1.f, 1.f, 1.f, 1.f};
    p.buttonHover = {0.925f, 0.945f, 0.98f, 1.f};
    p.buttonActive = {0.85f, 0.89f, 0.96f, 1.f};
    p.buttonBorder = Hex(0x7D879C); // контур нейтральных кнопок: 3,6 : 1 к белой карточке
    p.buttonDisabled = {0.91f, 0.92f, 0.94f, 1.f};
    p.frame = {0.965f, 0.97f, 0.98f, 1.f};
    p.frameHover = {0.93f, 0.945f, 0.97f, 1.f};
    p.sensor[0] = {0.12f, 0.42f, 0.80f, 1.f};
    p.sensor[1] = {0.88f, 0.42f, 0.05f, 1.f};
    p.nav = Hex(0x103269);       // основной цвет руководителя; белый текст 12,5 : 1
    p.navText = {0.93f, 0.95f, 0.99f, 1.f};
    p.navMuted = {0.62f, 0.71f, 0.86f, 1.f};
    p.navLine = {1.f, 1.f, 1.f, 0.12f};
    p.navFrame = {1.f, 1.f, 1.f, 0.10f};
    p.navSel = Hex(0x1D4E9E);
    p.navHover = {1.f, 1.f, 1.f, 0.07f};
    p.orange = Hex(0xF87128);    // главное действие — с тёмным текстом (5,9 : 1)
    p.lemon = Hex(0xB8E039);     // заметные кнопки — с тёмным текстом (11,0 : 1)
    p.onAccent = Hex(0x1A1D24);
    p.action = Hex(0x1A4FA8);    // главные действия: белый текст 7,7 : 1, к фону карточки 7,7 : 1
    p.actionHover = Hex(0x2D63C4);
    p.actionText = Hex(0xFFFFFF);
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
    p.bg = Hex(0x121826);
    p.panel = Hex(0x161D2D);
    p.card = Hex(0x1B2233);
    p.cardAlt = Hex(0x18202F);
    p.border = Hex(0x2E3850);
    p.text = Hex(0xE8ECF4);      // 13,4 : 1 на карточке
    p.textDim = Hex(0xB3BDD1);   // 8,4 : 1
    p.muted = Hex(0x8A95AD);     // 5,3 : 1
    p.accent = Hex(0x3D7BE0);    // заливка выбора с белым текстом 4,2 : 1 (жирный / крупный)
    p.accentHover = Hex(0x4C88EA);
    p.accentText = {1.f, 1.f, 1.f, 1.f};
    p.ok = Hex(0x5FD07F);        // 8,2 : 1
    p.warn = Hex(0xF2B84B);      // 8,9 : 1
    p.err = Hex(0xFF6B6B);       // 5,7 : 1
    p.info = Hex(0x7FB0FF);      // 7,2 : 1
    p.okBg = {0.13f, 0.28f, 0.18f, 1.f};
    p.warnBg = {0.32f, 0.25f, 0.08f, 1.f};
    p.errBg = {0.35f, 0.13f, 0.13f, 1.f};
    p.infoBg = {0.13f, 0.21f, 0.36f, 1.f};
    p.button = {0.18f, 0.21f, 0.25f, 1.f};
    p.buttonHover = {0.22f, 0.27f, 0.33f, 1.f};
    p.buttonActive = {0.27f, 0.34f, 0.44f, 1.f};
    p.buttonBorder = Hex(0x77829C); // 4,1 : 1 к карточке #1B2233
    p.buttonDisabled = {0.15f, 0.17f, 0.20f, 1.f};
    p.frame = {0.17f, 0.20f, 0.24f, 1.f};
    p.frameHover = {0.21f, 0.25f, 0.30f, 1.f};
    p.sensor[0] = {0.36f, 0.62f, 1.f, 1.f};
    p.sensor[1] = {1.f, 0.62f, 0.25f, 1.f};
    p.nav = Hex(0x0B2247);       // тёмный вариант #103269
    p.navText = {0.90f, 0.93f, 0.98f, 1.f};
    p.navMuted = {0.55f, 0.63f, 0.78f, 1.f};
    p.navLine = {1.f, 1.f, 1.f, 0.10f};
    p.navFrame = {1.f, 1.f, 1.f, 0.08f};
    p.navSel = Hex(0x1D3F7A);
    p.navHover = {1.f, 1.f, 1.f, 0.06f};
    p.orange = Hex(0xF87128);
    p.lemon = Hex(0xB8E039);
    p.onAccent = Hex(0x1A1D24);
    p.action = Hex(0x7FA8F5);    // светло-синяя на тёмной карточке (6,7 : 1 к #1B2233), тёмный текст 7,5 : 1
    p.actionHover = Hex(0x9BBCF8);
    p.actionText = Hex(0x0B1733);
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
