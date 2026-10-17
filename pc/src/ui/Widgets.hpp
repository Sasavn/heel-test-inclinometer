#pragma once
// Общие элементы интерфейса: карточки, плашки, кнопки, строки «название — значение», подсказки.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "Fonts.hpp"
#include "Theme.hpp"

namespace ui
{

inline ImU32 U32(const ImVec4& c)
{
    return ImGui::ColorConvertFloat4ToU32(c);
}

inline ImVec4 Alpha(const ImVec4& c, float a)
{
    return ImVec4(c.x, c.y, c.z, a);
}

// Смесь цветов: t = 0 — a, t = 1 — b.
inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// Чекбокс: скруглённый квадрат с тонкой рамкой; отмеченный — заливка акцентом и белая галка. Наведение — рамка
// акцентного оттенка. Высота строки — как у полей ввода (выравнивается с ними в одной строке).
inline bool Checkbox(const char* label, bool* v)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& st = g.Style;
    const ImGuiID id = w->GetID(label);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end, false);
    const float frameH = ImGui::GetFrameHeight();
    const float box = IM_ROUND(g.FontSize * 1.05f);
    const ImVec2 pos = w->DC.CursorPos;
    const float labelW = ts.x > 0.f ? st.ItemInnerSpacing.x + S(2) + ts.x : 0.f;
    const ImRect bb(pos, ImVec2(pos.x + box + labelW, pos.y + frameH));
    ImGui::ItemSize(bb, st.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
    {
        *v = !*v;
        ImGui::MarkItemEdited(id);
    }
    ImDrawList* dl = w->DrawList;
    const ImVec2 b0(pos.x, IM_ROUND(pos.y + (frameH - box) * 0.5f));
    const ImVec2 b1(b0.x + box, b0.y + box);
    const float r = S(4.f);
    if (*v)
    {
        const ImVec4 fill = held ? Mix(pal.accent, ImVec4(0, 0, 0, 1), 0.15f) : hovered ? pal.accentHover : pal.accent;
        dl->AddRectFilled(b0, b1, ImGui::GetColorU32(fill), r);
        const float t = std::max(1.6f, box * 0.12f);
        dl->PathLineTo(ImVec2(b0.x + box * 0.25f, b0.y + box * 0.53f));
        dl->PathLineTo(ImVec2(b0.x + box * 0.43f, b0.y + box * 0.70f));
        dl->PathLineTo(ImVec2(b0.x + box * 0.76f, b0.y + box * 0.33f));
        dl->PathStroke(ImGui::GetColorU32(pal.accentText), 0, t);
    }
    else
    {
        dl->AddRectFilled(b0, b1, ImGui::GetColorU32(hovered ? pal.frameHover : pal.frame), r);
        const ImVec4 bc = (hovered || held) ? Mix(pal.border, pal.accent, 0.7f) : Mix(pal.border, pal.muted, 0.35f);
        dl->AddRect(b0, b1, ImGui::GetColorU32(bc), r, 0, S(1.2f));
    }
    if (ts.x > 0.f)
        ImGui::RenderText(ImVec2(b1.x + st.ItemInnerSpacing.x + S(2), pos.y + (frameH - ts.y) * 0.5f), label, end);
    return pressed;
}

struct FontScope
{
    explicit FontScope(ImFont* f) : on(f != nullptr)
    {
        if (on)
            ImGui::PushFont(f);
    }
    ~FontScope()
    {
        if (on)
            ImGui::PopFont();
    }
    bool on;
};

// Внутри карточки-контейнера (вкладки, развёрнутая карточка) вложенные карточки рисуются «плоско»: без рамки,
// тени, своей заливки и полей — содержимое встаёт прямо в контейнер.
inline int cardFlat = 0;

// Мягкая тень под прямоугольником (несколько полупрозрачных слоёв со сдвигом вниз) — в список рисования окна
// под ним: дочерние окна рисуются поверх родителя, так что тень остаётся под карточкой.
inline void CardShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding)
{
    // Только кольца снаружи карточки (внутрь не заходят: порядок рисования дочерних окон относительно родителя
    // не всегда «родитель — снизу», заливка затемняла бы саму карточку)
    const float k = darkTheme ? 0.30f : 0.075f;
    for (int i = 1; i <= 4; i++)
    {
        const float g = S(1.0f) * static_cast<float>(i);
        const float dy = S(1.5f);
        dl->AddRect(ImVec2(a.x - g + S(0.5f), a.y - g + dy), ImVec2(b.x + g - S(0.5f), b.y + g + dy),
                    ImGui::ColorConvertFloat4ToU32(Alpha(pal.shadow, k * (1.f - 0.22f * static_cast<float>(i)))),
                    rounding + g, 0, S(1.2f));
    }
}

// Карточка: белый (тёмный) прямоугольник со скруглением, тонкой рамкой, мягкой тенью и полями. height = 0 — по
// содержимому.
inline bool BeginCard(const char* id, ImVec2 size = {0, 0}, const ImVec4* bg = nullptr)
{
    const bool flat = cardFlat > 0;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, flat ? ImVec4(0, 0, 0, 0) : bg ? *bg : pal.card);
    ImGui::PushStyleColor(ImGuiCol_Border, darkTheme ? pal.border : Mix(pal.border, pal.card, 0.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, flat ? ImVec2(0, 0) : compactUi ? ImVec2(S(10), S(8)) : ImVec2(S(14), S(12)));
    ImGuiChildFlags flags = (flat ? 0 : ImGuiChildFlags_Borders) | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (size.y == 0)
        flags |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

inline void EndCard()
{
    ImGui::EndChild();
    if (cardFlat == 0)
        CardShadow(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), S(10.f));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// Текст, обрезанный по ширине (в пикселях) с «…» на конце (по границе символа UTF-8).
inline std::string FitText(const std::string& s, float width)
{
    if (ImGui::CalcTextSize(s.c_str()).x <= width)
        return s;
    std::string t = s;
    while (!t.empty())
    {
        std::size_t cut = t.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80)
            cut--;
        t.resize(cut);
        if (ImGui::CalcTextSize((t + "…").c_str()).x <= width)
            return t + "…";
    }
    return t;
}

// Вкладки с подчёркиванием: подписи в ряд, выбранная — акцентом и полосой снизу; под рядом — тонкая линия на всю
// ширину (lineW = 0 — до края окна). Возвращает true, если выбор изменился.
inline bool Tabs(const char* id, int* value, const char* const* labels, const int* values, int n, float lineW = 0.f)
{
    bool changed = false;
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight() + S(4);
    const float padX = S(14);
    float x = start.x;
    for (int i = 0; i < n; i++)
    {
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        const float w = ts.x + 2 * padX;
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##tab", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool sel = *value == values[i];
        if (pressed && !sel)
        {
            *value = values[i];
            changed = true;
        }
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(sel ? pal.accent : hov ? pal.text : pal.textDim);
        if (sel)
        {
            FontScope f(fontBold);
            dl->AddText(ImVec2(x + padX, start.y + (h - ts.y) * 0.5f - S(1)), col, labels[i]);
        }
        else
            dl->AddText(ImVec2(x + padX, start.y + (h - ts.y) * 0.5f - S(1)), col, labels[i]);
        if (sel)
            dl->AddRectFilled(ImVec2(x + S(4), start.y + h - S(3)), ImVec2(x + w - S(4), start.y + h), col, S(1.5f));
        x += w;
    }
    const float right = lineW > 0 ? start.x + lineW : ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    dl->AddLine(ImVec2(start.x, start.y + h - S(0.5f)), ImVec2(std::max(x, right), start.y + h - S(0.5f)),
                ImGui::ColorConvertFloat4ToU32(pal.border), S(1));
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y));
    ImGui::Dummy(ImVec2(x - start.x, h));
    ImGui::PopID();
    return changed;
}

// Ширина ряда вкладок Tabs.
inline float TabsWidth(const char* const* labels, int n)
{
    float w = 0;
    for (int i = 0; i < n; i++)
        w += ImGui::CalcTextSize(labels[i]).x + 2 * S(14);
    return w;
}

// Маленькая квадратная кнопка-значок в углу карточки (развернуть ⤢ / свернуть). true — нажата.
inline bool IconButton(const char* id, const char* glyph, const char* tip)
{
    const float s = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(s, s));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), ImGui::ColorConvertFloat4ToU32(hov ? pal.buttonHover : pal.button), S(6));
    dl->AddRect(p, ImVec2(p.x + s, p.y + s), ImGui::ColorConvertFloat4ToU32(hov ? Mix(pal.border, pal.accent, 0.6f) : pal.border),
                S(6), 0, S(1));
    const ImVec2 ts = ImGui::CalcTextSize(glyph);
    dl->AddText(ImVec2(p.x + (s - ts.x) * 0.5f, p.y + (s - ts.y) * 0.5f), ImGui::ColorConvertFloat4ToU32(pal.textDim), glyph);
    if (hov && tip)
        ImGui::SetTooltip("%s", tip);
    return pressed;
}

inline void CardTitle(const char* text, const char* sub = nullptr)
{
    {
        FontScope f(fontH2);
        ImGui::TextUnformatted(text);
    }
    if (sub)
    {
        FontScope f(fontSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
        ImGui::TextWrapped("%s", sub);
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
}

inline void TextColored(const ImVec4& c, const char* fmt, ...) IM_FMTARGS(2);
inline void TextColored(const ImVec4& c, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

inline void Muted(const char* fmt, ...) IM_FMTARGS(1);
inline void Muted(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
    ImGui::TextV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

inline void MutedWrapped(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

inline void SmallMuted(const char* text)
{
    FontScope f(fontSmall);
    MutedWrapped(text);
}

// Плашка-«пилюля» с цветной точкой: ● Подключено
inline void Chip(const char* text, const ImVec4& fg, const ImVec4& bg, bool dot = true)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    const float padX = S(10), dotW = dot ? S(14) : 0.f;
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 size(ts.x + 2 * padX + dotW, h);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), U32(bg), h * 0.5f);
    if (dot)
        dl->AddCircleFilled(ImVec2(pos.x + padX + S(4), pos.y + h * 0.5f), S(4.5f), U32(fg));
    dl->AddText(ImVec2(pos.x + padX + dotW, pos.y + (h - ts.y) * 0.5f), U32(fg), text);
    ImGui::Dummy(size);
}

// Значок программы (SVG руководителя «инстан.svg», 56,28 × 56,28): два кольца с разрывами и круг — векторно, теми же
// пропорциями, что и src/app.ico (scratchpad: icons.py, resvg). Кольцо 1 — r 21,96…28,14, дуги −5,6…197,1° и
// −119,3…−36,9°; кольцо 2 — r 14,25…20,40, дуга −292,4…24,1° (углы — от оси x против часовой); круг r 12,42.
inline void DrawAppIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 col)
{
    const float k = size / 56.28f;
    constexpr float kDeg = 3.14159265f / 180.f;
    auto ring = [&](float ri, float ro, float a0, float a1) {
        const float r = 0.5f * (ri + ro) * k, w = (ro - ri) * k;
        // экран: y вниз — угол со знаком минус; дуга от −a1 до −a0
        const int seg = std::max(12, static_cast<int>((a1 - a0) / 6.f));
        dl->PathArcTo(center, r, -a1 * kDeg, -a0 * kDeg, seg);
        dl->PathStroke(col, ImDrawFlags_None, w);
    };
    ring(21.96f, 28.14f, -5.6f, 197.1f);
    ring(21.96f, 28.14f, -119.3f, -36.9f);
    ring(14.25f, 20.40f, -292.4f, 24.1f);
    dl->AddCircleFilled(center, 12.42f * k, col, 32);
}

enum class BtnKind
{
    Normal,
    Primary,
    Danger,
    Warn,
    Orange, // с 1.8.1 (вариант «главные действия синие»): как Primary — синяя заливка #1F5FBF, белый текст 6,1 : 1
    Lemon,  // с 1.8.1: нейтральная (лимонный — вариант палитры, в кнопках не используется)
    Secondary, // вторичная: синий контур и синий текст на светлом
    Nav,    // на тёмно-синей шапке: полупрозрачная, светлый текст, тонкий контур
};

// Кнопка. Обычная — светлая заливка с рамкой, при наведении рамка и фон с акцентным оттенком, при нажатии
// темнее; основная (Primary) — акцентная заливка; Danger / Warn — красная / оранжевая. disabled — серый фон и
// серый текст без рамки-акцента, не нажимается (подсказка — почему).
inline bool Button(const char* label, BtnKind kind = BtnKind::Normal, ImVec2 size = {0, 0}, bool disabled = false,
                   const char* whyDisabled = nullptr)
{
    const ImVec4 black(0, 0, 0, 1);
    ImVec4 bg, bgHover, bgActive, fg, border;
    switch (kind)
    {
    case BtnKind::Primary:
    case BtnKind::Orange:
        // Главное действие — одна система: синяя заливка, белый текст
        bg = pal.action;
        bgHover = pal.actionHover;
        bgActive = Mix(pal.action, black, 0.22f);
        fg = pal.actionText;
        border = darkTheme ? pal.action : Mix(pal.action, black, 0.25f);
        break;
    case BtnKind::Secondary:
        bg = pal.button;
        bgHover = Mix(pal.button, pal.action, 0.08f);
        bgActive = Mix(pal.button, pal.action, 0.16f);
        fg = pal.action;
        border = pal.action;
        break;
    case BtnKind::Nav:
        bg = pal.navFrame;
        bgHover = Alpha(pal.navText, 0.16f);
        bgActive = Alpha(pal.navText, 0.24f);
        fg = pal.navText;
        border = pal.navLine;
        break;
    case BtnKind::Danger:
    case BtnKind::Warn:
    case BtnKind::Lemon:
        // Нейтральные контурные (цвет состояния — только у текста опасного действия): янтарный / лимонный / красный
        // заливкой в кнопках не используются
        bg = pal.button;
        bgHover = pal.buttonHover;
        bgActive = pal.buttonActive;
        fg = kind == BtnKind::Danger ? pal.err : pal.text;
        border = kind == BtnKind::Danger ? Mix(pal.buttonBorder, pal.err, 0.5f) : pal.buttonBorder;
        break;
    default:
        bg = pal.button;
        bgHover = pal.buttonHover;
        bgActive = pal.buttonActive;
        fg = pal.text;
        border = pal.buttonBorder;
        break;
    }
    if (disabled && kind == BtnKind::Nav)
    {
        bg = bgHover = bgActive = pal.navFrame;
        fg = pal.navMuted;
        border = pal.navLine;
    }
    else if (disabled)
    {
        bg = bgHover = bgActive = pal.buttonDisabled;
        fg = pal.muted;
        border = pal.buttonDisabled;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgActive);
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f); // серость — цветами выше, без полупрозрачности
    // Главное действие — заметнее: полужирный шрифт, на 5 точек выше, лёгкая тень под кнопкой
    const bool main = kind == BtnKind::Primary || kind == BtnKind::Orange; // и недоступная — той же высоты
    ImDrawList* mdl = ImGui::GetWindowDrawList();
    if (main)
    {
        ImGui::PushFont(fontBold);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y + S(2.5f)));
        mdl->ChannelsSplit(2);
        mdl->ChannelsSetCurrent(1);
    }
    if (disabled)
        ImGui::BeginDisabled();
    const bool pressed = ImGui::Button(label, size);
    if (disabled)
        ImGui::EndDisabled();
    if (main)
    {
        mdl->ChannelsSetCurrent(0);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (!disabled)
        {
        const float r = ImGui::GetStyle().FrameRounding;
        const ImVec4 sh = darkTheme ? ImVec4(0, 0, 0, 0.45f) : ImVec4(0.06f, 0.12f, 0.28f, 0.22f);
        mdl->AddRectFilled(ImVec2(a.x + S(1), a.y + S(3)), ImVec2(b.x + S(1), b.y + S(3)), U32(sh), r);
        mdl->AddRectFilled(ImVec2(a.x, a.y + S(1.5f)), ImVec2(b.x, b.y + S(1.5f)), U32(Alpha(sh, sh.w * 0.6f)), r);
        }
        mdl->ChannelsMerge();
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    // Наведение на обычную кнопку — рамка акцентного оттенка
    if (!disabled && kind == BtnKind::Normal && ImGui::IsItemHovered())
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            U32(Mix(pal.buttonBorder, pal.accent, 0.65f)),
                                            ImGui::GetStyle().FrameRounding, 0, S(1.f));
    if (disabled && whyDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", whyDisabled);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return pressed && !disabled;
}

// Знак «−» / «+» на только что нарисованной кнопке: прямоугольниками по
// пиксельной сетке, строго симметрично относительно рамки кнопки. Глифы шрифта
// для этого не годятся: ImGui центрирует по ширине символа вместе с боковыми
// отступами (знак уезжает вбок), а «−» и «+» стоят на математической оси шрифта.
inline void StepSign(bool plus)
{
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const float x0 = IM_ROUND(a.x), y0 = IM_ROUND(a.y);
    const float w = IM_ROUND(b.x) - x0, h = IM_ROUND(b.y) - y0;
    // Толщина и длина — той же чётности, что ширина/высота кнопки: тогда поля
    // слева и справа (сверху и снизу) равны целому числу пикселей
    float t = std::max(2.f, IM_ROUND(S(2.f)));
    float len = std::max(6.f, IM_ROUND(w * 0.4f));
    if (std::fmod(h - t, 2.f) != 0.f)
        t += 1.f;
    if (std::fmod(w - len, 2.f) != 0.f)
        len += 1.f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Горизонтальная черта
    const float hx = x0 + (w - len) / 2.f, hy = y0 + (h - t) / 2.f;
    dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + len, hy + t), col);
    if (plus)
    {
        // Вертикальная: та же длина по высоте, толщина — по чётности ширины
        float tv = t, lv = len;
        if (std::fmod(w - tv, 2.f) != 0.f)
            tv += 1.f;
        if (std::fmod(h - lv, 2.f) != 0.f)
            lv += 1.f;
        const float vx = x0 + (w - tv) / 2.f, vy = y0 + (h - lv) / 2.f;
        dl->AddRectFilled(ImVec2(vx, vy), ImVec2(vx + tv, vy + lv), col);
    }
}

// Поле числа с кнопками «−» / «+» справа (вместо встроенных ImGui: там дефис,
// он уже и тоньше плюса). Ширина — как у обычного поля (SetNextItemWidth /
// PushItemWidth). Ctrl + кнопка — крупный шаг; удержание кнопки повторяет шаг.
template <typename T, typename Input>
inline bool StepField(const char* id, T* v, T step, T stepFast, Input input)
{
    ImGuiStyle& st = ImGui::GetStyle();
    const float bw = ImGui::GetFrameHeight();
    const float w = ImGui::CalcItemWidth();
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(std::max(S(30), w - 2.f * (bw + st.ItemInnerSpacing.x)));
    bool changed = input(v);
    const T d = ImGui::GetIO().KeyCtrl ? stepFast : step;
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    if (Button("##dec", BtnKind::Normal, ImVec2(bw, bw)))
    {
        *v -= d;
        changed = true;
    }
    StepSign(false);
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    if (Button("##inc", BtnKind::Normal, ImVec2(bw, bw)))
    {
        *v += d;
        changed = true;
    }
    StepSign(true);
    ImGui::PopItemFlag();
    ImGui::PopID();
    return changed;
}

inline bool InputIntStep(const char* id, int* v, int step, int stepFast)
{
    return StepField(id, v, step, stepFast, [](int* p) { return ImGui::InputInt("##v", p, 0, 0); });
}

inline bool InputDoubleStep(const char* id, double* v, double step, double stepFast, const char* fmt)
{
    return StepField(id, v, step, stepFast,
                     [fmt](double* p) { return ImGui::InputDouble("##v", p, 0.0, 0.0, fmt); });
}

// Подсказка «?» рядом с подписью.
inline void Hint(const char* text)
{
    ImGui::SameLine(0, S(4));
    ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
    ImGui::TextUnformatted("(?)");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(420));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Цветная полоса-сообщение (предупреждение, ошибка, успех): текст переносится.
inline void Banner(const ImVec4& fg, const ImVec4& bg, const char* icon, const char* text)
{
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float pad = S(10);
    const float iconW = icon ? ImGui::CalcTextSize(icon).x + S(8) : 0.f;
    const float wrap = w - 2 * pad - iconW;
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const float h = ts.y + 2 * S(7);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), U32(bg), S(6));
    dl->AddRectFilled(p, ImVec2(p.x + S(4), p.y + h), U32(fg), S(6), ImDrawFlags_RoundCornersLeft);
    if (icon)
        dl->AddText(ImVec2(p.x + pad, p.y + S(7)), U32(fg), icon);
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad + iconW, p.y + S(7)));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::PushStyleColor(ImGuiCol_Text, darkTheme ? pal.text : ImVec4(fg.x * 0.75f, fg.y * 0.75f, fg.z * 0.75f, 1.f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, S(2)));
}

// Таблица «название — значение» (две колонки). Begin/End вокруг вызовов KV.
inline bool BeginKV(const char* id, float labelW)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
        return false;
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, labelW);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

inline void KV(const char* label, const std::string& value, const ImVec4* color = nullptr, const char* hint = nullptr)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, pal.textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (hint)
        Hint(hint);
    ImGui::TableNextColumn();
    if (color)
        ImGui::PushStyleColor(ImGuiCol_Text, *color);
    ImGui::TextWrapped("%s", value.c_str());
    if (color)
        ImGui::PopStyleColor();
}

inline void EndKV()
{
    ImGui::EndTable();
}

// Переключатель на несколько положений (сегменты). Возвращает true, если выбор изменился.
inline bool Segmented(const char* id, int* value, const char* const* labels, const int* values, int n)
{
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(2), 0));
    for (int i = 0; i < n; i++)
    {
        if (i)
            ImGui::SameLine();
        const bool sel = *value == values[i];
        // выбранный — светлая синяя подложка, синий текст и тонкая синяя рамка (не яркая заливка)
        const ImVec4 selText = darkTheme ? Mix(pal.action, ImVec4(1, 1, 1, 1), 0.55f) : pal.action;
        if (sel)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Mix(pal.card, pal.action, darkTheme ? 0.30f : 0.10f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Mix(pal.card, pal.action, darkTheme ? 0.36f : 0.14f));
            ImGui::PushStyleColor(ImGuiCol_Text, selText);
        }
        ImGui::PushID(i);
        if (ImGui::Button(labels[i]) && !sel)
        {
            *value = values[i];
            changed = true;
        }
        if (sel)
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                U32(darkTheme ? Mix(pal.action, ImVec4(1, 1, 1, 1), 0.3f) : pal.action),
                                                ImGui::GetStyle().FrameRounding, 0, S(1.2f));
        ImGui::PopID();
        if (sel)
            ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// Полоса прогресса с подписью внутри.
inline void Progress(float frac, const char* overlay, float height = 0.f)
{
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, pal.accent);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, darkTheme ? pal.frame : ImVec4(0.88f, 0.90f, 0.93f, 1.f));
    ImGui::ProgressBar(std::clamp(frac, 0.f, 1.f), ImVec2(-1, height > 0 ? height : ImGui::GetFrameHeight()), overlay);
    ImGui::PopStyleColor(2);
}

// Текст по центру прямоугольника (рисование без раскладки).
inline void CenterText(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, ImU32 col, const char* text)
{
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
    dl->AddText(font, size, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), col, text);
}

inline std::string Fmt(const char* fmt, ...) IM_FMTARGS(1);
inline std::string Fmt(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

} // namespace ui
