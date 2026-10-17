// Страница «Справка» (F1, «? Справка» внизу левой панели): оглавление слева, поиск по тексту, разделы справа.
// Текст — HelpText.hpp (встроен в программу), разбор и поиск — HelpDoc.hpp.
#include "App.hpp"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include <imgui.h>

#include "HelpDoc.hpp"
#include "HelpText.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace
{

const std::vector<help::Section>& Doc()
{
    static const std::vector<help::Section> doc = help::Parse(kHelpText);
    return doc;
}

// Совет: светло-синяя плашка с полосой слева.
void TipBox(const std::string& text, float width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float pad = S(10);
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str(), nullptr, false, width - 2 * pad - S(4));
    const float h = ts.y + 2 * S(7);
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), U32(pal.infoBg), S(6));
    dl->AddRectFilled(p, ImVec2(p.x + S(3), p.y + h), U32(pal.info), S(1.5f));
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad + S(4), p.y + S(7)));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2 * pad - S(4));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(width, 0));
}

// Пункт списка или шаг: метка слева (• или номер в кружке), текст с отступом.
void ItemText(const std::string& text, int number, float width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float indent = number > 0 ? S(32) : S(20);
    const float lh = ImGui::GetTextLineHeight();
    if (number > 0)
    {
        const ImVec2 c(p.x + S(11), p.y + lh * 0.5f);
        dl->AddCircleFilled(c, S(10), U32(pal.action));
        char n[8];
        std::snprintf(n, sizeof(n), "%d", number);
        const ImVec2 ns = fontBold->CalcTextSizeA(fontSmall->FontSize, FLT_MAX, 0.f, n);
        dl->AddText(fontBold, fontSmall->FontSize, ImVec2(c.x - ns.x * 0.5f, c.y - ns.y * 0.5f), U32(pal.actionText), n);
    }
    else
        dl->AddCircleFilled(ImVec2(p.x + S(8), p.y + lh * 0.5f + S(1)), S(2.6f), U32(pal.textDim));
    ImGui::SetCursorScreenPos(ImVec2(p.x + indent, p.y));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - indent);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

// Таблица: первая строка — заголовок; первый столбец — полужирный (клавиши, пункты Правил). Поиск — только
// совпавшие строки.
void HelpTable(const help::Block& b, const std::string& q, float width, int id)
{
    if (b.rows.empty())
        return;
    float col0 = 0.f;
    for (const auto& r : b.rows)
        if (!r.empty())
        {
            FontScope f(fontBold);
            col0 = std::max(col0, ImGui::CalcTextSize(r[0].c_str()).x);
        }
    col0 = std::min(col0 + S(16), width * 0.42f);
    const int cols = static_cast<int>(b.rows.front().size());
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(10), S(5)));
    if (ImGui::BeginTable("##ht", std::max(1, cols),
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp,
                          ImVec2(width, 0)))
    {
        for (int c = 0; c < cols; c++)
        {
            if (c == 0)
                ImGui::TableSetupColumn(b.rows.front()[0].c_str(), ImGuiTableColumnFlags_WidthFixed, col0);
            else
                ImGui::TableSetupColumn(b.rows.front()[static_cast<std::size_t>(c)].c_str(), ImGuiTableColumnFlags_WidthStretch);
        }
        {
            FontScope f(fontBold);
            ImGui::TableHeadersRow();
        }
        for (std::size_t r = 1; r < b.rows.size(); r++)
        {
            if (!help::RowMatches(b, r, q))
                continue;
            ImGui::TableNextRow();
            for (int c = 0; c < cols; c++)
            {
                ImGui::TableNextColumn();
                const std::string cell = static_cast<std::size_t>(c) < b.rows[r].size() ? b.rows[r][static_cast<std::size_t>(c)] : "";
                ImGui::PushTextWrapPos(0.f);
                if (c == 0)
                {
                    FontScope f(fontBold);
                    TextColored(pal.accent, "%s", cell.c_str());
                }
                else
                    ImGui::TextUnformatted(cell.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
}

} // namespace

void App::HelpSetSearch(const std::string& q)
{
    std::snprintf(help_.search, sizeof(help_.search), "%s", q.c_str());
}

void App::PageHelp()
{
    const auto& doc = Doc();
    const std::string q = help::Query(help_.search);
    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = compactUi ? S(8) : S(12);

    // Шапка: название, строка поиска, «О программе»
    {
        FontScope f(fontH2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Справка");
    }
    ImGui::SameLine(0, S(12));
    {
        FontScope f(fontSmall);
        ImGui::AlignTextToFramePadding();
        if (W > S(900))
            Muted("Регистратор крена %s — кратко о программе и опыте кренования", kAppVersion);
    }
    const float aboutW = ImGui::CalcTextSize("О программе").x + S(28);
    const float searchW = std::min(S(340), W * 0.36f);
    ImGui::SameLine(W - aboutW - searchW - S(8) + ImGui::GetStyle().WindowPadding.x);
    ImGui::SetNextItemWidth(searchW);
    if (help_.focusSearch)
    {
        ImGui::SetKeyboardFocusHere();
        help_.focusSearch = false;
    }
    if (ImGui::InputTextWithHint("##helpsearch", "Поиск по справке (Ctrl+F)", help_.search, sizeof(help_.search)))
        help_.goTo = -1;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Слово или часть слова: показываются только подходящие абзацы и строки таблиц");
    ImGui::SameLine(0, S(8));
    if (Button("О программе", BtnKind::Normal, ImVec2(aboutW, 0)))
        aboutOpen_ = true;
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));

    // Сколько найдено
    std::vector<int> hits(doc.size(), 1);
    int total = 0;
    for (std::size_t i = 0; i < doc.size(); i++)
    {
        hits[i] = help::Matches(doc[i], q);
        total += hits[i];
    }
    help_.found = q.empty() ? -1 : total;

    const float H = ImGui::GetContentRegionAvail().y;
    const float tocW = compactUi ? S(210) : S(250);

    // Оглавление
    if (BeginCard("##help_toc", ImVec2(tocW, H)))
    {
        {
            FontScope f(fontSmall);
            Muted("%s", q.empty() ? "СОДЕРЖАНИЕ" : total ? Fmt("НАЙДЕНО: %d", total).c_str() : "НИЧЕГО НЕ НАЙДЕНО");
        }
        ImGui::Dummy(ImVec2(0, S(2)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float iw = ImGui::GetContentRegionAvail().x;
        for (std::size_t i = 0; i < doc.size(); i++)
        {
            const bool any = hits[i] > 0;
            const bool sel = help_.current == static_cast<int>(i) && any;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float ih = compactUi ? S(28) : S(32);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::InvisibleButton("##toc", ImVec2(iw, ih)) && any)
                help_.goTo = static_cast<int>(i);
            const bool hov = ImGui::IsItemHovered() && any;
            ImGui::PopID();
            if (sel || hov)
                dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + ih), U32(sel ? pal.blueSoft : pal.buttonHover), S(6));
            if (sel)
                dl->AddRectFilled(ImVec2(p.x, p.y + S(6)), ImVec2(p.x + S(3), p.y + ih - S(6)), U32(pal.accent), S(1.5f));
            ImFont* font = sel ? fontBold : fontBody;
            const ImVec4 col = !any ? Alpha(pal.muted, 0.6f) : sel ? pal.accent : pal.text;
            const std::string t = FitText(doc[i].title, iw - S(44));
            dl->AddText(font, font->FontSize, ImVec2(p.x + S(12), p.y + (ih - font->FontSize) * 0.5f), U32(col), t.c_str());
            if (!q.empty() && any)
            {
                const std::string n = std::to_string(hits[i]);
                const ImVec2 ns = fontSmall->CalcTextSizeA(fontSmall->FontSize, FLT_MAX, 0.f, n.c_str());
                dl->AddText(fontSmall, fontSmall->FontSize, ImVec2(p.x + iw - ns.x - S(8), p.y + (ih - fontSmall->FontSize) * 0.5f),
                            U32(pal.muted), n.c_str());
            }
        }
        ImGui::Dummy(ImVec2(0, S(8)));
        FontScope f(fontSmall);
        MutedWrapped("F1 — справка и обратно, Esc — назад. Все клавиши — раздел «Горячие клавиши».");
    }
    EndCard();
    ImGui::SameLine(0, gap);

    // Текст
    const float cw = W - tocW - gap;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.card);
    ImGui::PushStyleColor(ImGuiCol_Border, darkTheme ? pal.border : Mix(pal.border, pal.card, 0.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, compactUi ? ImVec2(S(16), S(10)) : ImVec2(S(24), S(16)));
    ImGui::BeginChild("##help_body", ImVec2(cw, H), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (q != help_.lastQuery)
    {
        help_.lastQuery = q;
        ImGui::SetScrollY(0.f);
    }
    {
        const float tw = std::min(ImGui::GetContentRegionAvail().x - S(4), S(940)); // строки не шире удобного для чтения
        const float scrollY = ImGui::GetScrollY();
        int current = -1;
        bool first = true;
        int shown = 0;
        for (std::size_t i = 0; i < doc.size(); i++)
        {
            const help::Section& s = doc[i];
            if (hits[i] == 0)
                continue;
            shown++;
            if (!first)
                ImGui::Dummy(ImVec2(0, S(14)));
            first = false;
            // раздел вверху видимой части — подсвечен в оглавлении
            if (ImGui::GetCursorPosY() <= scrollY + S(60) || current < 0)
                current = static_cast<int>(i);
            if (help_.goTo == static_cast<int>(i))
            {
                ImGui::SetScrollHereY(0.0f);
                help_.goTo = -1;
                current = static_cast<int>(i);
                help_.current = current;
            }
            {
                FontScope f(fontH2);
                ImGui::TextUnformatted(s.title.c_str());
            }
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + S(2)), ImVec2(p.x + tw, p.y + S(2)), U32(pal.border), S(1));
                ImGui::Dummy(ImVec2(0, S(8)));
            }
            const bool all = q.empty() || help::TitleMatches(s, q);
            int bi = 0;
            for (const help::Block& b : s.blocks)
            {
                bi++;
                if (!all && !help::BlockMatches(b, q))
                    continue;
                switch (b.kind)
                {
                case help::Kind::Sub:
                {
                    ImGui::Dummy(ImVec2(0, S(4)));
                    FontScope f(fontBold);
                    TextColored(pal.accent, "%s", b.text.c_str());
                    ImGui::Dummy(ImVec2(0, S(2)));
                    break;
                }
                case help::Kind::Para:
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tw);
                    ImGui::TextUnformatted(b.text.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Dummy(ImVec2(0, S(4)));
                    break;
                case help::Kind::Bullet: ItemText(b.text, 0, tw); break;
                case help::Kind::Step:
                    ItemText(b.text, b.number, tw);
                    ImGui::Dummy(ImVec2(0, S(2)));
                    break;
                case help::Kind::Tip:
                    ImGui::Dummy(ImVec2(0, S(4)));
                    TipBox(b.text, tw);
                    ImGui::Dummy(ImVec2(0, S(4)));
                    break;
                case help::Kind::Table:
                    ImGui::Dummy(ImVec2(0, S(2)));
                    HelpTable(b, all ? std::string() : q, tw, static_cast<int>(i) * 100 + bi);
                    ImGui::Dummy(ImVec2(0, S(4)));
                    break;
                }
            }
        }
        if (shown == 0)
        {
            ImGui::Dummy(ImVec2(0, S(20)));
            Muted("По запросу «%s» ничего не найдено. Попробуйте часть слова: «плеч», «ноль», «отчёт».", help_.search);
        }
        // в конце — место, чтобы последний раздел можно было прокрутить к верху
        ImGui::Dummy(ImVec2(0, std::max(0.f, H - S(220))));
        if (current >= 0)
            help_.current = current;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace ui
