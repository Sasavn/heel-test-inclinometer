// Страница «Терминал»: любая команда прибора вручную и весь обмен (команды, ответы, события связи; поток S и опрос
// status — по флажкам).
#include "App.hpp"

#include <cstring>

#include <imgui.h>

#include "Widgets.hpp"

namespace ui
{

void App::TerminalSend(const std::string& cmd)
{
    if (cmd.empty())
        return;
    if (term_.history.empty() || term_.history.back() != cmd)
        term_.history.push_back(cmd);
    term_.histPos = -1;
    term_.scrollToEnd = true;
    // Ответ get пойдёт в журнал (строки D не показываются), boot/dfu/reset — как обычно
    link_->Send(cmd, Origin::User, cmd.rfind("get ", 0) == 0 ? 60000 : 5000);
}

void App::PageTerminal()
{
    // Новые строки журнала
    for (auto& l : link_->TermSince(term_.lastSeq))
    {
        term_.lastSeq = l.seq;
        term_.lines.push_back(std::move(l));
    }
    if (term_.lines.size() > 4000)
        term_.lines.erase(term_.lines.begin(), term_.lines.begin() + static_cast<std::ptrdiff_t>(term_.lines.size() - 4000));

    // Быстрые команды и фильтры
    static const char* quick[] = {"help", "ver", "diag", "status", "files", "stream 0", "stream 1000"};
    for (int i = 0; i < 7; i++)
    {
        if (i)
            ImGui::SameLine(0, S(6));
        if (Button(quick[i], BtnKind::Normal, ImVec2(0, 0), !Connected(), "Нет связи с прибором"))
            TerminalSend(quick[i]);
    }
    ImGui::SameLine(0, S(20));
    ImGui::Checkbox("поток S", &settings_.termShowStream);
    Hint("Строки потока stream (углы каждые 100 мс)");
    ImGui::SameLine(0, S(14));
    ImGui::Checkbox("опрос status", &settings_.termShowPolls);
    Hint("Запросы status, которые программа шлёт раз в секунду, и их ответы");
    ImGui::SameLine(0, S(14));
    if (Button("Очистить", BtnKind::Normal))
        term_.lines.clear();
    ImGui::SameLine(0, S(6));
    if (Button("Копировать", BtnKind::Normal))
    {
        std::string all;
        for (const auto& l : term_.lines)
            all += (l.kind == '>' ? "> " : l.kind == '!' ? "* " : "") + l.text + "\n";
        ImGui::SetClipboardText(all.c_str());
        Notify("Журнал скопирован", 1, 2000);
    }

    // Журнал
    const float inputH = ImGui::GetFrameHeight() + S(4) + 2 * ImGui::GetStyle().ItemSpacing.y;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.card);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8));
    ImGui::BeginChild("##termlog", ImVec2(0, ImGui::GetContentRegionAvail().y - inputH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
    {
        FontScope f(fontMono);
        std::vector<const TermLine*> vis;
        vis.reserve(term_.lines.size());
        for (const auto& l : term_.lines)
        {
            if (l.kind == 'S' && !settings_.termShowStream)
                continue;
            if (l.origin == Origin::Poll && !settings_.termShowPolls)
                continue;
            vis.push_back(&l);
        }
        const bool atEnd = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(vis.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++)
            {
                const TermLine& l = *vis[static_cast<std::size_t>(i)];
                const long long sec = l.ms / 1000;
                char ts[24];
                std::snprintf(ts, sizeof(ts), "%3lld:%02lld.%03lld ", sec / 60, sec % 60, static_cast<long long>(l.ms % 1000));
                Muted("%s", ts);
                ImGui::SameLine(0, 0);
                switch (l.kind)
                {
                case '>':
                    TextColored(l.origin == Origin::User ? pal.accent : pal.muted, "> %s", l.text.c_str());
                    break;
                case '!': TextColored(pal.warn, "* %s", l.text.c_str()); break;
                case 'S': TextColored(pal.muted, "%s", l.text.c_str()); break;
                default:
                {
                    const bool err = l.text.rfind("ERR", 0) == 0;
                    if (err)
                        TextColored(pal.err, "%s", l.text.c_str());
                    else
                        ImGui::TextUnformatted(l.text.c_str());
                }
                }
            }
        if (vis.empty())
            Muted("Команды и ответы прибора появятся здесь. Список команд — кнопка «help».");
        if (term_.scrollToEnd || atEnd)
            ImGui::SetScrollHereY(1.f);
        term_.scrollToEnd = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Ввод
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Команда:");
    ImGui::SameLine(0, S(8));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - S(140) - S(8));
    struct Cb
    {
        static int Fn(ImGuiInputTextCallbackData* d)
        {
            App* app = static_cast<App*>(d->UserData);
            auto& t = app->term_;
            if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory && !t.history.empty())
            {
                if (d->EventKey == ImGuiKey_UpArrow)
                    t.histPos = t.histPos < 0 ? static_cast<int>(t.history.size()) - 1 : std::max(0, t.histPos - 1);
                else if (d->EventKey == ImGuiKey_DownArrow && t.histPos >= 0)
                    t.histPos = t.histPos + 1 >= static_cast<int>(t.history.size()) ? -1 : t.histPos + 1;
                d->DeleteChars(0, d->BufTextLen);
                if (t.histPos >= 0)
                    d->InsertChars(0, t.history[static_cast<std::size_t>(t.histPos)].c_str());
            }
            return 0;
        }
    };
    if (term_.focusInput)
    {
        ImGui::SetKeyboardFocusHere();
        term_.focusInput = false;
    }
    bool send = ImGui::InputText("##cmd", term_.input, sizeof(term_.input),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, &Cb::Fn, this);
    ImGui::SameLine(0, S(8));
    send |= Button("Отправить", BtnKind::Primary, ImVec2(S(140), 0), !Connected(), "Нет связи с прибором");
    if (send && Connected())
    {
        TerminalSend(term_.input);
        term_.input[0] = '\0';
        term_.focusInput = true;
    }
}

} // namespace ui
