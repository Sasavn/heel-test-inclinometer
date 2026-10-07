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

// Кнопка; disabled — серая, не нажимается (подсказка — почему).
inline bool Button(const char* label, BtnKind kind = BtnKind::Normal, ImVec2 size = {0, 0}, bool disabled = false,
                   const char* whyDisabled = nullptr)
{
    int colors = 0;
    if (kind == BtnKind::Primary)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, pal.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.accentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal.accent);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.accentText);
        colors = 4;
    }
    else if (kind == BtnKind::Danger || kind == BtnKind::Warn)
    {
        const ImVec4 c = kind == BtnKind::Danger ? pal.err : pal.warn;
        ImGui::PushStyleColor(ImGuiCol_Button, c);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Alpha(c, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        colors = 4;
    }
    if (disabled)
        ImGui::BeginDisabled();
    const bool pressed = ImGui::Button(label, size);
    if (disabled)
    {
        ImGui::EndDisabled();
        if (whyDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", whyDisabled);
    }
    ImGui::PopStyleColor(colors);
    return pressed && !disabled;
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
