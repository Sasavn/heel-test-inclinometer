// «Обработка»: расписанный расчёт показателей — окна «Подробнее» по щелчку на карточках-показателях (h основной,
// второй расчёт / табл. 6.5, Правила РС, вероятная ошибка). Шаги — core/HeelSteps.hpp (те же, что в листе «Расчёт по
// шагам» полного отчёта): формула, подстановка чисел, результат, пункт Правил.
#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>

#include "../core/HeelSteps.hpp"
#include "Widgets.hpp"

namespace ui
{

steps::Steps App::ProcSteps() const
{
    if (!proc_.files)
        return {};
    const bool cont = ProcCont();
    return heel::BuildSteps(ProcSettings(), *proc_.files, proc_.meas, cont, proc_.rec, proc_.pos, cont ? proc_.posSum : proc_.sum,
                            proc_.rs);
}

// Разделы шагов: таблица значений и строки «величина — формула — подстановка — результат — пункт».
void App::ProcStepsView(const steps::Steps& st, const std::vector<std::string>& ids)
{
    int num = 0;
    for (const std::string& id : ids)
    {
        const steps::Section* sec = steps::Find(st, id);
        if (!sec)
            continue;
        if (num++)
            ImGui::Dummy(ImVec2(0, S(6)));
        {
            FontScope f(fontBold);
            TextColored(pal.accent, "%s", sec->title.c_str());
        }
        if (!sec->ref.empty())
        {
            ImGui::SameLine(0, S(8));
            FontScope f(fontSmall);
            Muted("%s", sec->ref.c_str());
        }
        const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH |
                                   ImGuiTableFlags_SizingStretchProp;
        if (!sec->table.head.empty() && !sec->table.rows.empty())
        {
            ImGui::PushID((id + "t").c_str());
            if (ImGui::BeginTable("##st", static_cast<int>(sec->table.head.size()), tf))
            {
                for (const std::string& h : sec->table.head)
                {
                    const bool wide = h.find('=') != std::string::npos;
                    const bool mid = h.find("ΔM") != std::string::npos || h.find("Участок") != std::string::npos;
                    ImGui::TableSetupColumn(h.c_str(), ImGuiTableColumnFlags_WidthStretch, wide ? 2.6f : mid ? 2.4f : 1.0f);
                }
                ImGui::TableHeadersRow();
                for (const auto& row : sec->table.rows)
                {
                    ImGui::TableNextRow();
                    for (std::size_t c = 0; c < sec->table.head.size(); c++)
                    {
                        ImGui::TableNextColumn();
                        const std::string& v = c < row.size() ? row[c] : std::string();
                        ImGui::PushTextWrapPos(0.f);
                        if (v.find("исключён") != std::string::npos || v == "нет")
                            TextColored(pal.err, "%s", v.c_str());
                        else
                            ImGui::TextUnformatted(v.c_str());
                        ImGui::PopTextWrapPos();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }
        if (!sec->rows.empty())
        {
            ImGui::PushID((id + "r").c_str());
            if (ImGui::BeginTable("##sr", 5, tf))
            {
                ImGui::TableSetupColumn("Величина", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("Формула", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Подстановка", ImGuiTableColumnFlags_WidthStretch, 2.6f);
                ImGui::TableSetupColumn("Результат", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("Пункт", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableHeadersRow();
                for (const steps::Row& r : sec->rows)
                {
                    ImGui::TableNextRow();
                    const std::string* cells[] = {&r.what, &r.formula, &r.subst, &r.result, &r.ref};
                    for (int c = 0; c < 5; c++)
                    {
                        ImGui::TableNextColumn();
                        ImGui::PushTextWrapPos(0.f);
                        if (c == 3)
                        {
                            FontScope f(fontBold);
                            const bool bad = r.result.find("не выполнено") != std::string::npos;
                            TextColored(bad ? pal.err : pal.text, "%s", cells[c]->c_str());
                        }
                        else if (c == 4 || c == 1)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, c == 4 ? pal.muted : pal.text);
                            ImGui::TextUnformatted(cells[c]->c_str());
                            ImGui::PopStyleColor();
                        }
                        else
                            ImGui::TextUnformatted(cells[c]->c_str());
                        ImGui::PopTextWrapPos();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }
        if (!sec->note.empty())
        {
            FontScope f(fontSmall);
            ImGui::PushTextWrapPos(0.f);
            Muted("%s", sec->note.c_str());
            ImGui::PopTextWrapPos();
        }
    }
}

// Окно «Подробнее» по карточке-показателю: ✕, Esc или щелчок мимо — закрыть.
void App::ProcKpiDetail()
{
    const char* id = "##kpi_detail";
    if (proc_.kpiDetail == -2) // закрыть снаружи (стенд)
    {
        if (ImGui::BeginPopupModal(id))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        proc_.kpiDetail = -1;
        return;
    }
    if (proc_.kpiDetail < 0)
        return;
    if (!ImGui::IsPopupOpen(id))
        ImGui::OpenPopup(id);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 size(std::min(vp->WorkSize.x - S(60), S(1120)), std::min(vp->WorkSize.y - S(60), S(820)));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(14)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal.card);
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoMove))
    {
        const bool cont = ProcCont();
        const bool byTable = ProcSettings().RsMain() == 1;
        const heel::RsCase& rc = proc_.rs;
        const int card = proc_.kpiDetail;
        std::string title;
        std::vector<std::string> ids;
        switch (card)
        {
        case 0:
            title = byTable ? "h, основной расчёт — по табл. 6.5" : "h, основной расчёт — от начального положения";
            ids = byTable ? std::vector<std::string>{"t65", "hk", "661", "662", "665", "dh", "result"}
                          : std::vector<std::string>{"levels", "initial", "result"};
            break;
        case 1:
            title = byTable ? "Второй расчёт — от начального положения, МНК" : "Второй расчёт — по табл. 6.5, МНК";
            ids = byTable ? std::vector<std::string>{"initial", "ls", "result"}
                          : std::vector<std::string>{"t65", "hk", "665", "dh", "ls", "result"};
            break;
        case 2:
            title = "Правила РС — требования и их проверка";
            ids = {"661", "662", "663", "664", "665"};
            break;
        default:
            title = std::isfinite(rc.r.err) ? "Вероятная ошибка (6.6.2)" : (cont ? "Положения груза" : "Замеры");
            ids = std::isfinite(rc.r.err) ? std::vector<std::string>{"t65", "hk", "661", "662", "665"}
                                          : std::vector<std::string>{"levels"};
        }
        // Заголовок и ✕
        {
            FontScope f(fontH2);
            ImGui::TextUnformatted(title.c_str());
        }
        const float bs = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bs);
        bool close = ImGui::Button("✕##kpiclose", ImVec2(bs, bs));
        {
            FontScope f(fontSmall);
            Muted("Расписанный расчёт: формула, подстановка чисел, результат и пункт Инструктивных указаний по кренованию "
                  "(прил. 3 к разд. 2 Руководства). Полностью — лист «Расчёт по шагам» полного отчёта.");
        }
        ImGui::Separator();
        ImGui::BeginChild("##kpi_body", ImVec2(0, 0), ImGuiChildFlags_None);
        if (card == 2)
        {
            // Требования: состояние, требование, опыт, пункт
            if (ImGui::BeginTable("##kpi_checks", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(24));
                ImGui::TableSetupColumn("Требование", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("Опыт", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Пункт", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableHeadersRow();
                for (const rs::Check& c : rc.checks)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    TextColored(c.state > 0 ? pal.ok : c.state == 0 ? pal.err : pal.muted, "%s", c.state > 0 ? "✓" : c.state == 0 ? "✗" : "—");
                    ImGui::TableNextColumn();
                    ImGui::PushTextWrapPos(0.f);
                    ImGui::TextUnformatted(c.req.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::TableNextColumn();
                    ImGui::PushTextWrapPos(0.f);
                    TextColored(c.state == 0 ? pal.err : pal.text, "%s", c.value.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::TableNextColumn();
                    Muted("%s", c.ref.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::Dummy(ImVec2(0, S(6)));
        }
        ProcStepsView(ProcSteps(), ids);
        ImGui::EndChild();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            close = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mp = ImGui::GetMousePos(), wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
            if (mp.x < wp.x || mp.y < wp.y || mp.x > wp.x + ws.x || mp.y > wp.y + ws.y)
                close = true; // щелчок мимо окна
        }
        if (close)
        {
            proc_.kpiDetail = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
        proc_.kpiDetail = -1;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace ui
