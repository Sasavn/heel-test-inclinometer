#pragma once
// Общие элементы интерфейса: карточки, плашки, кнопки, строки «название — значение», подсказки.
#include <algorithm>
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

// Карточка: белый (тёмный) прямоугольник с рамкой и полями. height = 0 — по содержимому.
inline bool BeginCard(const char* id, ImVec2 size = {0, 0}, const ImVec4* bg = nullptr)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg ? *bg : pal.card);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(11)));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (size.y == 0)
        flags |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

inline void EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
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

enum class BtnKind
{
    Normal,
    Primary,
    Danger,
    Warn,
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
        bg = pal.accent;
        bgHover = pal.accentHover;
        bgActive = Mix(pal.accent, black, 0.18f);
        fg = pal.accentText;
        border = pal.accent;
        break;
    case BtnKind::Danger:
    case BtnKind::Warn:
    {
        const ImVec4 c = kind == BtnKind::Danger ? pal.err : pal.warn;
        bg = c;
        bgHover = Mix(c, ImVec4(1, 1, 1, 1), 0.12f);
        bgActive = Mix(c, black, 0.15f);
        fg = ImVec4(1, 1, 1, 1);
        border = c;
        break;
    }
    default:
        bg = pal.button;
        bgHover = pal.buttonHover;
        bgActive = pal.buttonActive;
        fg = pal.text;
        border = pal.buttonBorder;
        break;
    }
    if (disabled)
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
    if (disabled)
        ImGui::BeginDisabled();
    const bool pressed = ImGui::Button(label, size);
    if (disabled)
        ImGui::EndDisabled();
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
    if (Button("−##dec", BtnKind::Normal, ImVec2(bw, bw)))
    {
        *v -= d;
        changed = true;
    }
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    if (Button("+##inc", BtnKind::Normal, ImVec2(bw, bw)))
    {
        *v += d;
        changed = true;
    }
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
        if (sel)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, pal.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.accentHover);
            ImGui::PushStyleColor(ImGuiCol_Text, pal.accentText);
        }
        ImGui::PushID(i);
        if (ImGui::Button(labels[i]) && !sel)
        {
            *value = values[i];
            changed = true;
        }
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
