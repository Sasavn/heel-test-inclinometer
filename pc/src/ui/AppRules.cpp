// «Обработка» — проверка опыта кренования по Правилам РС (core/RsCheck.hpp, core/HeelRs.hpp): вид карточки графиков
// «Контроль M – tg θ» (график контроля замеров, п. 5.5 прил. 3: кренящий момент и tg крена от начального положения,
// исключённый замер — красным, прямая МНК) и «Правила РС» (данные для 6.6.3 и основной расчёт h, оценка качества
// 6.6, требования ✓ / ✗ / — со ссылками на пункты, табл. 6.5 с ручным «исключить / учитывать»).
#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "../core/TextUtil.hpp"
#include "Widgets.hpp"

namespace ui
{

void ProcPlotStyle(bool forExport); // AppProcess.cpp

namespace
{

void Mark(int state)
{
    if (state < 0)
        TextColored(pal.muted, "—");
    else
        TextColored(state ? pal.ok : pal.err, state ? "✓" : "✗");
}

} // namespace

// Свёрнутая карточка h над видами «Контроль» и «Правила РС»: основной h, второй расчёт, итог проверки и отчёт —
// чтобы не возвращаться на графики крена.
void App::ProcResultStrip(float w)
{
    if (BeginCard("##proc_strip", ImVec2(w, 0)))
    {
        const bool cont = ProcCont();
        const heel::Summary& s = cont ? proc_.posSum : proc_.sum;
        const heel::RsCase& rc = proc_.rs;
        const bool byTable = settings_.rsMain == 1;
        const double hMain = byTable ? rc.r.hFinal : s.h, hOther = byTable ? s.h : rc.r.hk;
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const float saveW = std::max(S(150), ImGui::CalcTextSize("Сохранение…").x + S(24));
        const float openW = ImGui::CalcTextSize("Открыть папку").x + S(24);
        const float buttonsX = right - saveW - S(8) - openW;
        ImGui::AlignTextToFramePadding();
        Muted("Метацентрическая высота h");
        ImGui::SameLine(0, S(10));
        {
            FontScope f(fontBold);
            ImGui::AlignTextToFramePadding();
            if (std::isfinite(hMain))
                ImGui::Text(settings_.heelSmall ? "%.3f м (%.1f мм)" : "%.3f м", hMain, hMain * 1000.0);
            else
                ImGui::TextUnformatted("—");
        }
        ImGui::SameLine(0, S(6));
        ImGui::AlignTextToFramePadding();
        Muted("%s", byTable ? (rc.r.reduced ? "табл. 6.5, 6.6.5" : "табл. 6.5") : "от начального");
        // Дальше — что поместится до кнопок: второй расчёт, итог проверки (полный или короткий)
        auto fits = [&](const std::string& t) {
            return ImGui::GetItemRectMax().x + S(16) + ImGui::CalcTextSize(t.c_str()).x <= buttonsX - S(12);
        };
        const std::string other = std::isfinite(hOther)
                                      ? Fmt("%s %.3f м", byTable ? "от начального" : "табл. 6.5:", hOther)
                                      : std::string(byTable ? "от начального —" : "табл. 6.5: —");
        if (fits(other))
        {
            ImGui::SameLine(0, S(16));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(other.c_str());
        }
        int st = -1;
        const std::string full = ProcRsStatus(&st);
        std::string shortSt = full;
        if (const auto dash = full.find(" — "); dash != std::string::npos)
            shortSt = full.substr(0, dash);
        for (const std::string* t : {static_cast<const std::string*>(&full), static_cast<const std::string*>(&shortSt)})
            if (fits(*t))
            {
                ImGui::SameLine(0, S(16));
                ImGui::AlignTextToFramePadding();
                TextColored(st > 0 ? pal.ok : st == 0 ? pal.err : pal.muted, "%s", t->c_str());
                if (ImGui::IsItemHovered() && t != &full)
                    ImGui::SetTooltip("%s", full.c_str());
                break;
            }
        ImGui::SameLine(buttonsX - ImGui::GetWindowPos().x);
        ProcSaveButtons(saveW, openW);
    }
    EndCard();
}

void App::ProcViewSwitch()
{
    const char* labels[] = {ProcCont() ? "Запись крена" : "Графики крена", "Контроль M – tg θ", "Правила РС"};
    static const int values[] = {0, 1, 2};
    int v = proc_.view;
    if (Segmented("view", &v, labels, values, 3))
        proc_.view = v;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Правила РС (прил. 3 к разд. 2 Руководства по техническому наблюдению за постройкой судов):\n"
                          "«Контроль» — график контроля замеров (п. 5.5), «Правила РС» — оценка качества (6.6) и\n"
                          "требования к опыту (разд. 3)");
}

std::string App::ProcRsStatus(int* state) const
{
    const heel::RsCase& k = proc_.rs;
    if (k.r.m.empty())
    {
        *state = -1;
        return "Правила РС: " + (k.r.note.empty() ? std::string("нужно два положения и больше") : k.r.note);
    }
    int bad = 0, unknown = 0;
    std::string list;
    for (const rs::Check& c : k.checks)
    {
        if (c.state == 0)
        {
            bad++;
            list += (list.empty() ? "" : ", ") + c.id;
        }
        unknown += c.state < 0;
    }
    if (!bad)
    {
        *state = 1;
        return unknown ? Fmt("✓ Правила РС: выполнено (без данных — %d)", unknown) : std::string("✓ Правила РС: выполнено");
    }
    *state = 0;
    return Fmt("✗ Правила РС: не выполнено %d из %d — %s", bad, static_cast<int>(k.checks.size()), list.c_str());
}

// График контроля замеров (п. 5.5 прил. 3): по оси X — кренящий момент M = P·l от начального положения, по оси Y —
// tg(θ − θ0). Точки — положения груза (подпись — номер), линия по порядку опыта — тонкая; красная точка — конец
// исключённого замера табл. 6.5; прямая — МНК по остальным; пунктир — прямая по h_k табл. 6.5.
void App::ProcControlPlot(float w, float h, bool forExport)
{
    const heel::RsCase& k = proc_.rs;
    ProcPlotStyle(forExport);
    const bool model = settings_.heelSmall;
    const double armToM = model ? 0.001 : 1.0;
    const double D = settings_.heelD;
    if (k.in.theta.size() < 2)
    {
        FontScope f(fontSmall);
        Muted("График контроля: нужно два положения груза и больше (учтённых, с креном по основному посту)");
        return;
    }
    ImPlotFlags flags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;
    if (forExport)
        flags |= ImPlotFlags_NoMouseText;
    // Данные сменились (плечи, единицы, другой опыт) — пределы заново по точкам
    double key = static_cast<double>(k.M.size()) + D;
    for (std::size_t i = 0; i < k.M.size(); i++)
        key += (std::isfinite(k.M[i]) ? k.M[i] * (i + 1.0) : 0.0) + (std::isfinite(k.tanT[i]) ? k.tanT[i] * 1e3 : 0.0);
    static double lastKey = 0;
    const bool refit = forExport || key != lastKey;
    if (!forExport)
        lastKey = key;
    if (!ImPlot::BeginPlot(forExport ? "##ctrl_x" : "##ctrl", ImVec2(w, h), flags))
        return;
    const std::string xl = std::string("Кренящий момент M = P·l, ") + (model ? "кг·мм" : "т·м");
    ImPlot::SetupAxes(xl.c_str(), "tg θ", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.4f");
    ImPlot::SetupLegend(ImPlotLocation_NorthWest);
    // Пределы по точкам с полями (у картинки — всегда; на экране — при смене данных)
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    for (std::size_t i = 0; i < k.M.size(); i++)
        if (std::isfinite(k.M[i]) && std::isfinite(k.tanT[i]))
        {
            x0 = std::min(x0, k.M[i]);
            x1 = std::max(x1, k.M[i]);
            y0 = std::min(y0, k.tanT[i]);
            y1 = std::max(y1, k.tanT[i]);
        }
    const double px = std::max((x1 - x0) * 0.12, 1e-9), py = std::max((y1 - y0) * 0.15, 1e-6);
    ImPlot::SetupAxisLimits(ImAxis_X1, x0 - px, x1 + px, refit ? ImPlotCond_Always : ImPlotCond_Once);
    ImPlot::SetupAxisLimits(ImAxis_Y1, y0 - py, y1 + py, refit ? ImPlotCond_Always : ImPlotCond_Once);
    ImPlot::SetupFinish();
    const double lx[2] = {x0 - px * 0.6, x1 + px * 0.6};
    // Прямая МНК по неисключённым
    if (std::isfinite(k.fit.k) && k.fit.k != 0)
    {
        const double hFit = armToM / (k.fit.k * D);
        const double ly[2] = {k.fit.k * lx[0] + k.fit.b, k.fit.k * lx[1] + k.fit.b};
        const std::string lab = Fmt("МНК: h = %.3f м", hFit);
        ImPlot::SetNextLineStyle(pal.accent, S(2.f));
        ImPlot::PlotLine(lab.c_str(), lx, ly, 2);
    }
    // Прямая по h_k табл. 6.5 (через начальное положение)
    if (std::isfinite(k.r.hk) && k.r.hk != 0)
    {
        const double kk = armToM / (D * k.r.hk);
        const std::string lab = Fmt("По табл. 6.5: h_k = %.3f м", k.r.hk);
        std::vector<double> dx, dy;
        const int seg = 40;
        for (int i = 0; i < seg; i++)
        {
            const double a = lx[0] + (lx[1] - lx[0]) * i / seg, b = lx[0] + (lx[1] - lx[0]) * (i + 0.5) / seg;
            dx.insert(dx.end(), {a, b, heel::kNaN});
            dy.insert(dy.end(), {kk * a, kk * b, heel::kNaN});
        }
        ImPlot::SetNextLineStyle(Alpha(pal.textDim, 0.8f), S(1.3f));
        ImPlot::PlotLine(lab.c_str(), dx.data(), dy.data(), static_cast<int>(dx.size()));
    }
    // Порядок опыта — тонкой линией
    ImPlot::SetNextLineStyle(Alpha(pal.muted, 0.45f), S(1.f));
    ImPlot::PlotLine("##order", k.M.data(), k.tanT.data(), static_cast<int>(std::min(k.M.size(), k.tanT.size())));
    std::vector<double> gx, gy, bx, by;
    for (std::size_t i = 0; i < k.M.size(); i++)
        (k.excluded[i] ? bx : gx).push_back(k.M[i]), (k.excluded[i] ? by : gy).push_back(k.tanT[i]);
    ImPlot::SetNextLineStyle(pal.text);
    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, S(4.5f), pal.text, S(1.f), pal.text);
    ImPlot::PlotScatter("Положения груза", gx.data(), gy.data(), static_cast<int>(gx.size()));
    if (!bx.empty())
    {
        ImPlot::SetNextLineStyle(pal.err);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, S(6.f), pal.err, S(1.f), pal.err);
        ImPlot::PlotScatter("Исключённый замер", bx.data(), by.data(), static_cast<int>(bx.size()));
    }
    ImPlot::PushStyleColor(ImPlotCol_InlayText, pal.textDim);
    for (std::size_t i = 0; i < k.M.size() && i < k.label.size(); i++)
        if (std::isfinite(k.M[i]) && std::isfinite(k.tanT[i]))
        {
            if (k.excluded[i])
                ImPlot::PushStyleColor(ImPlotCol_InlayText, pal.err);
            ImPlot::PlotText(k.label[i].c_str(), k.M[i], k.tanT[i], ImVec2(S(9), -S(9)));
            if (k.excluded[i])
                ImPlot::PopStyleColor();
        }
    ImPlot::PopStyleColor();
    ImPlot::EndPlot();
}

// Картинка «График_контроля.png» для отчёта: заголовок, исходные данные, график.
void App::ProcExportControl(float w, float h)
{
    const heel::RsCase& k = proc_.rs;
    {
        FontScope f(fontH2);
        ImGui::TextUnformatted("График контроля замеров: кренящий момент – tg θ (Правила РС, п. 5.5 прил. 3)");
    }
    {
        FontScope f(fontSmall);
        const bool model = settings_.heelSmall;
        Muted("D = %.*f %s, P = %.*f %s · пост «%s» · h_k по табл. 6.5 = %s · МНК от начального = %s · %s",
              model ? 3 : 1, settings_.heelD, model ? "кг" : "т", model ? 4 : 2, settings_.heelP, model ? "кг" : "т",
              k.mainPost >= 0 ? PostTitle(k.mainPost).c_str() : "—",
              std::isfinite(k.r.hk) ? Fmt("%.3f м", k.r.hk).c_str() : "—",
              std::isfinite(k.hCurrent) ? Fmt("%.3f м", k.hCurrent).c_str() : "—", Shown(proc_.outDir).c_str());
        Muted("Точки — положения груза (номер), красная — конец замера, исключённого по 6.6.1 или вручную; плечи %s",
              k.legacy ? "без знака — знак по крену" : "со знаком (+ к правому борту)");
    }
    ImGui::Spacing();
    (void) h;
    ProcControlPlot(w, ImGui::GetContentRegionAvail().y, true);
}

// Вид «Правила РС»: слева — данные для 6.6.3, основной расчёт и сводка 6.6; посередине — требования; справа —
// табл. 6.5 с ручным «исключить / учитывать».
void App::ProcRsView(float w, float h)
{
    const heel::RsCase& k = proc_.rs;
    const rs::Result& r = k.r;
    const bool model = settings_.heelSmall;
    const float gap = S(12);
    // Широко — три столбца; узко — слева данные и сводка, справа требования над табл. 6.5
    const bool narrow = w < S(1000);
    const float aw = std::clamp(std::floor(w * 0.26f), S(230), S(340));
    const float cw = narrow ? w - aw - gap : std::max(S(220), std::floor(w * 0.38f));
    const float bw = narrow ? cw : std::max(S(160), w - aw - cw - 2 * gap);
    const float bh = narrow ? std::floor((h - gap) * 0.5f) : h, ch = narrow ? h - bh - gap : h;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.cardAlt);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(6)));

    // --- Данные и сводка
    ImGui::BeginChild("##rs_a", ImVec2(aw, h), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Основной расчёт h");
        }
        ImGui::SameLine();
        Hint("Руководитель: показываются оба расчёта. «От начального + МНК» — h по каждому положению от начального "
             "(как в старой программе), «По табл. 6.5» — h_i по приращениям момента и крена между соседними "
             "положениями, h_k — среднее, при невыполнении 6.6 — h_k минус вероятная ошибка (6.6.5). Основной — "
             "крупно в карточке h и в проверке h ≥ 0,20 м.");
        int main = settings_.rsMain;
        static const char* mains[] = {"От начального", "Табл. 6.5"};
        static const int mainValues[] = {0, 1};
        Segmented("rs_main", &main, mains, mainValues, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("«От начального» — h по каждому положению от начального + МНК (как раньше);\n"
                              "«Табл. 6.5» — h_k по приращениям, итог по 6.6.5");
        if (main != settings_.rsMain)
        {
            settings_.rsMain = main;
            proc_.dirty = true;
            SaveSettingsSoon();
        }
        ImGui::Spacing();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Для 6.6.3 и сверки постов");
        }
        ImGui::SameLine();
        Hint("Из Информации об остойчивости (необязательно): h и l_max по наихудшему варианту загрузки, Δ0 — "
             "водоизмещение порожнем (нет — берётся D). 0 — не задано: 6.6.3 — «нужны данные».\n\n"
             "Сверка постов: наибольшее расхождение крена между датчиками на одном положении. По умолчанию 0,05° — "
             "пять шагов отображения датчика (0,01°) и около 2,5 % крена 2° (середина требуемых 1°–4°), т. е. "
             "заметно больше шума усреднённого крена (сотые доли градуса) и заметно меньше ошибки, которая "
             "испортила бы h.");
        // Подписи (с единицами) — столбцом, поля — за самой длинной до края; 0 — «не задано» серым в поле
        const std::string lh = std::string("h по Информации, ") + (model ? "мм" : "м");
        const std::string ll = std::string("l_max, ") + (model ? "мм" : "м");
        const std::string ld = std::string("Δ0 порожнем, ") + (model ? "кг" : "т");
        const char* sp = "Сверка постов, °";
        float labW = 0;
        for (const char* l : {lh.c_str(), ll.c_str(), ld.c_str(), sp})
            labW = std::max(labW, ImGui::CalcTextSize(l).x);
        const float fieldX = labW + S(10);
        const float fw = std::max(S(60), ImGui::GetContentRegionAvail().x - fieldX);
        auto field = [&](const char* label, const char* id, double* v, const char* fmt, double lo, double hi,
                         bool optional, const char* tip) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tip);
            ImGui::SameLine(fieldX + ImGui::GetStyle().WindowPadding.x);
            ImGui::SetNextItemWidth(fw);
            const bool blank = optional && !(*v > 0);
            const ImGuiID iid = ImGui::GetID(id);
            const bool editing = ImGui::GetActiveID() == iid;
            if (blank && !editing)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            double x = *v;
            if (ImGui::InputDouble(id, &x, 0.0, 0.0, fmt))
            {
                *v = std::clamp(x, lo, hi);
                proc_.dirty = true;
                SaveSettingsSoon();
            }
            if (blank && !editing)
            {
                ImGui::PopStyleColor();
                const ImVec2 r0 = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(r0.x + ImGui::GetStyle().FramePadding.x, r0.y + ImGui::GetStyle().FramePadding.y),
                    U32(pal.muted), "не задано");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tip);
        };
        field(lh.c_str(), "##rs_h", &settings_.rsH, model ? "%.1f" : "%.3f", 0.0, 1e6, true,
              "Метацентрическая высота по наихудшему варианту загрузки (Информация об остойчивости) — для ε в 6.6.3.\n"
              "0 — не задано.");
        field(ll.c_str(), "##rs_l", &settings_.rsLmax, model ? "%.1f" : "%.3f", 0.0, 1e7, true,
              "Наибольшее плечо диаграммы статической остойчивости по наихудшему варианту загрузки — для ε в 6.6.3.\n"
              "0 — не задано.");
        field(ld.c_str(), "##rs_d0", &settings_.rsD0, model ? "%.3f" : "%.1f", 0.0, 1e9, true,
              "Водоизмещение судна порожнем Δ0 — множитель Δ0/Δ в 6.6.3. 0 — не задано (берётся D, множитель 1).");
        field(sp, "##rs_sp", &settings_.rsSpread, "%.3f", 0.001, 10.0, false,
              "Наибольшее допустимое расхождение крена между датчиками (постами) на одном положении, °.\n"
              "По умолчанию 0,05°: пять шагов датчика (0,01°) и ~2,5 % крена 2°.");
        ImGui::Spacing();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Оценка качества (6.6)");
        }
        FontScope f(fontSmall);
        if (BeginKV("##rs_kv", S(128)))
        {
            auto num = [](double v, const char* fmt) { return std::isfinite(v) ? Fmt(fmt, v) : std::string("—"); };
            KV("h_k (табл. 6.5)", num(r.hk, "%.3f м"));
            KV("Замеров n", r.m.empty() ? std::string("—") : Fmt("%d из %d", r.n, static_cast<int>(r.m.size())));
            KV("σ", num(r.sigma, "%.4f м"));
            KV("t_αn (табл. 6.6.2)", std::isfinite(r.tA) ? Fmt("%.1f", r.tA) : std::string("n вне 8…16"));
            KV("Вероятная ошибка", num(r.err, "%.4f м"));
            KV("Допустимая (6.6.2)", num(r.errLimit, "%.4f м"));
            KV("ε (6.6.3)", r.ok663 < 0 ? std::string("нужны h и l_max") : num(r.eps, "%.3f м"));
            const ImVec4 fc = r.reduced ? pal.warn : pal.text;
            KV(r.reduced ? "Итог (6.6.5)" : "Итог", r.reduced ? num(r.hFinal, "%.3f м = h_k − ошибка") : num(r.hFinal, "%.3f м"),
               &fc);
            KV("От начального", num(k.hCurrent, "%.3f м"));
            KV("Период качки Tc",
               k.tcN ? Fmt("%.1f с (%.1f…%.1f, по %d)", k.tc, k.tcMin, k.tcMax, k.tcN) : std::string("не видно"));
            EndKV();
        }
        if (!r.note.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.warn);
            ImGui::TextWrapped("%s", r.note.c_str());
            ImGui::PopStyleColor();
        }
        if (k.legacy && r.m.size() >= 2)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
            ImGui::TextWrapped("Плечи без знака — знак по крену относительно начального (как раньше).");
            ImGui::PopStyleColor();
        }
        if (k.virtualZero)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
            ImGui::TextWrapped("Начальное положение — условное (крен 0, плечо 0): у первого замера плечо не 0.");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    // --- Требования
    ImGui::SameLine(0, gap);
    if (narrow)
        ImGui::BeginGroup();
    ImGui::BeginChild("##rs_b", ImVec2(bw, bh), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Проверка по Правилам РС");
        }
        ImGui::SameLine();
        Hint("Ч. IV «Остойчивость», 1.5; Инструктивные указания по кренованию (прил. 3 к разд. 2 Руководства по "
             "техническому наблюдению за постройкой судов): разд. 3 — условия опыта, п. 5.5 — график контроля и возврат "
             "в ноль, табл. 6.5 и п. 6.6 — обработка и оценка качества.\n✓ — выполнено, ✗ — нет, — — нет данных.");
        if (ImGui::BeginTable("##rs_req", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(16));
            ImGui::TableSetupColumn("Требование (пункт)", ImGuiTableColumnFlags_WidthStretch, 1.7f);
            ImGui::TableSetupColumn("Опыт", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (const rs::Check& c : k.checks)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Mark(c.state);
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", c.req.c_str());
                {
                    FontScope f(fontSmall);
                    ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
                    ImGui::TextWrapped("%s", c.ref.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, c.state == 0 ? pal.err : pal.text);
                ImGui::TextWrapped("%s", c.value.c_str());
                ImGui::PopStyleColor();
            }
            if (k.checks.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                Muted("Нет данных: нужно два положения груза и больше");
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    // --- Табл. 6.5
    if (narrow)
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + gap));
    else
        ImGui::SameLine(0, gap);
    ImGui::BeginChild("##rs_c", ImVec2(cw, ch), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Замеры по табл. 6.5");
        }
        ImGui::SameLine();
        Hint("Замер — переход между соседними положениями (по порядку опыта): ΔM = P·(l_i − l_{i−1}), Δθ = θ_i − "
             "θ_{i−1}, h_i = ΔM / (D·Δθ, рад).\n\n6.6.1: замер с |h_i − h_k| > 2σ исключается автоматически — только "
             "один, худший, и только если вручную ничего не исключено; после этого h_k и σ считаются заново. Если и "
             "тогда остаются замеры вне 2σ (или исключено больше одного) — 6.6.1 не выполнен.\n\n«Учёт»: авто — по 6.6.1; "
             "исключить — замер не в расчёте (автоисключение тогда не делается); учитывать — замер в расчёте, даже "
             "если он вне 2σ.");
        const bool showDiff = ImGui::GetContentRegionAvail().x >= S(440); // узко — h_i − h_k в подсказке строки
        if (ImGui::BeginTable("##rs_t65", showDiff ? 6 : 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit,
                              ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed, S(40));
            ImGui::TableSetupColumn(model ? "ΔM, кг·мм" : "ΔM, т·м", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Δθ, °", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("h_i, м", ImGuiTableColumnFlags_WidthStretch);
            if (showDiff)
                ImGui::TableSetupColumn("h_i − h_k", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Учёт", ImGuiTableColumnFlags_WidthFixed, S(86));
            ImGui::TableHeadersRow();
            const double toUser = model ? 1e6 : 1.0; // т·м -> кг·мм
            for (std::size_t i = 0; i < r.m.size(); i++)
            {
                const rs::Meas& q = r.m[i];
                const bool none = !std::isfinite(q.h);
                const ImVec4 c = none ? pal.muted : q.excluded ? pal.err : pal.text;
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                TextColored(c, "%s→%s", k.label[static_cast<std::size_t>(q.from)].c_str(),
                            k.label[static_cast<std::size_t>(q.to)].c_str());
                if (ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex())
                    ImGui::SetTooltip("Замер %d: из положения %s в %s%s%s", static_cast<int>(i) + 1,
                                      k.label[static_cast<std::size_t>(q.from)].c_str(),
                                      k.label[static_cast<std::size_t>(q.to)].c_str(),
                                      none ? " — груз не перемещался или крен не изменился: не замер"
                                      : q.autoExcluded ? " — исключён по 6.6.1 (|h_i − h_k| > 2σ)"
                                      : q.excluded     ? " — исключён вручную"
                                                       : "",
                                      none || !std::isfinite(r.hk)
                                          ? ""
                                          : Fmt("\nh_i = %.4f м, h_i − h_k = %+.4f м, 2σ = %.4f м", q.h, q.h - r.hk,
                                                2 * r.sigma).c_str());
                ImGui::TableNextColumn();
                TextColored(c, "%+.*f", model ? 0 : 1, q.dM * toUser);
                ImGui::TableNextColumn();
                TextColored(c, "%s", text::Angle(q.dTheta, 2).c_str());
                ImGui::TableNextColumn();
                if (none)
                    TextColored(c, "—");
                else
                    TextColored(c, "%.3f", q.h);
                if (showDiff)
                {
                    ImGui::TableNextColumn();
                    if (none || !std::isfinite(r.hk))
                        TextColored(c, "—");
                    else
                        TextColored(c, "%+.3f", q.h - r.hk);
                }
                ImGui::TableNextColumn();
                if (!none)
                {
                    const int number = k.in.number[static_cast<std::size_t>(q.to)];
                    const char* items[] = {"авто", "исключить", "учитывать"};
                    int cur = q.manual + 1;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const std::string preview =
                        q.manual < 0 ? (q.autoExcluded ? "авто: ✗" : "авто") : std::string(items[cur]);
                    if (ImGui::BeginCombo("##man", preview.c_str()))
                    {
                        for (int j = 0; j < 3; j++)
                            if (ImGui::Selectable(items[j], j == cur))
                                cur = j;
                        ImGui::EndCombo();
                    }
                    if (cur != q.manual + 1)
                        ProcessSetRsManual(number, cur - 1);
                }
                ImGui::PopID();
            }
            if (r.m.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                Muted("нет замеров");
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    if (narrow)
        ImGui::EndGroup();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace ui
