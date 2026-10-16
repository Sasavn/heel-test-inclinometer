// «Обработка» в виде дашборда (с 1.6): шапка страницы (название, «Открыть папку», «Сохранить отчёт»), строка
// параметров опыта (кратко; «Параметры ▾» раскрывает полную карточку), ряд карточек-показателей (h основным
// расчётом, второй расчёт и МНК, Правила РС, вероятная ошибка / положения), ниже — карточки «Запись с
// положениями» (или «Графики крена»), «Контроль M – tg θ» и карточка с вкладками (положения / замеры, Правила РС,
// перемещения, журнал). У каждой карточки — кнопка «развернуть на всю область» (и назад).
#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include "../core/HeelReport.hpp"
#include "../core/TextUtil.hpp"
#include "Dialogs.hpp"
#include "Widgets.hpp"

namespace ui
{

void HeaderHint(float limit, const std::string& full, const std::string& brief); // AppProcess.cpp

namespace
{

// Кнопка «развернуть / свернуть» в углу карточки: рамка со скруглением и уголки (развернуть) или стрелки внутрь.
bool ExpandButton(const char* id, bool expanded)
{
    const float s = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(s, s));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), U32(hov ? pal.buttonHover : pal.button), S(6));
    dl->AddRect(p, ImVec2(p.x + s, p.y + s), U32(hov ? Mix(pal.border, pal.accent, 0.6f) : pal.border), S(6), 0, S(1));
    const ImU32 c = U32(hov ? pal.accent : pal.textDim);
    const float m = s * 0.30f, l = s * 0.16f, t = S(1.6f);
    const ImVec2 a(p.x + m, p.y + m), b(p.x + s - m, p.y + s - m);
    if (!expanded) // уголки наружу
    {
        dl->AddLine(a, ImVec2(a.x + l, a.y), c, t);
        dl->AddLine(a, ImVec2(a.x, a.y + l), c, t);
        dl->AddLine(b, ImVec2(b.x - l, b.y), c, t);
        dl->AddLine(b, ImVec2(b.x, b.y - l), c, t);
        dl->AddLine(ImVec2(a.x + S(1), a.y + S(1)), ImVec2(a.x + l * 1.6f, a.y + l * 1.6f), c, t);
        dl->AddLine(ImVec2(b.x - S(1), b.y - S(1)), ImVec2(b.x - l * 1.6f, b.y - l * 1.6f), c, t);
    }
    else // уголки внутрь
    {
        const ImVec2 ca(a.x + l, a.y + l), cb(b.x - l, b.y - l);
        dl->AddLine(ca, ImVec2(ca.x - l, ca.y), c, t);
        dl->AddLine(ca, ImVec2(ca.x, ca.y - l), c, t);
        dl->AddLine(cb, ImVec2(cb.x + l, cb.y), c, t);
        dl->AddLine(cb, ImVec2(cb.x, cb.y + l), c, t);
    }
    if (hov)
        ImGui::SetTooltip(expanded ? "Свернуть (назад к обзору)" : "Развернуть на всю область");
    return pressed;
}

// Значок показателя: квадрат со скруглением, мягкая заливка, знак.
void KpiIcon(ImVec2 p, float sz, const ImVec4& soft, const ImVec4& fg, const char* glyph)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), U32(soft), S(8));
    FontScope f(fontH2);
    const ImVec2 ts = ImGui::CalcTextSize(glyph);
    dl->AddText(ImVec2(p.x + (sz - ts.x) * 0.5f, p.y + (sz - ts.y) * 0.5f), U32(fg), glyph);
}

} // namespace

// Заголовок карточки: справа — кнопка «развернуть» (id карточки), слева — название. Возвращает правую границу,
// до которой можно ставить подсказки в строке заголовка.
float App::ProcCardTitle(const char* title, int card)
{
    if (cardFlat > 0) // внутри карточки с вкладками: название — на вкладке, «развернуть» — у карточки
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Dummy(ImVec2(0, ImGui::GetFrameHeight()));
        return ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - S(4);
    }
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float bs = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(right - bs, start.y));
    char id[24];
    std::snprintf(id, sizeof(id), "##expand%d", card);
    if (ExpandButton(id, proc_.expanded == card))
        proc_.expanded = proc_.expanded == card ? 0 : card;
    ImGui::SetCursorScreenPos(start);
    ImGui::AlignTextToFramePadding();
    {
        FontScope f(fontBold);
        ImGui::TextUnformatted(title);
    }
    return right - bs - S(12);
}

// Шапка страницы: название, источник (кратко), справа — «Открыть папку» и «Сохранить отчёт».
void App::ProcHeader(float w)
{
    const float x0 = ImGui::GetCursorScreenPos().x;
    const float saveW = std::max(S(160), ImGui::CalcTextSize("Сохранение…").x + S(28));
    const float openW = ImGui::CalcTextSize("Открыть папку").x + S(28);
    const bool narrow = w < S(960); // узко — проект одной кнопкой «Проект ▾»
    const float projW = ProcProjectButtons(0, narrow, true) + S(8);
    {
        FontScope f(fontH2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Обработка опыта");
    }
    std::string src = !proc_.files ? std::string("файлы не прочитаны")
                      : ProcCont()  ? ProcRecordText()
                                    : ProcSummaryText();
    for (char& c : src)
        if (c == '\n')
            c = ' ';
    {
        FontScope f(fontSmall);
        const float room = x0 + w - saveW - openW - projW - S(30) - (ImGui::GetItemRectMax().x + S(14));
        std::string t = src;
        while (!t.empty() && ImGui::CalcTextSize(t.c_str()).x > room)
        {
            // отрезать по символу UTF-8 и поставить «…»
            std::size_t cut = t.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80)
                cut--;
            t = t.substr(0, cut);
            if (ImGui::CalcTextSize((t + "…").c_str()).x <= room)
            {
                t += "…";
                break;
            }
        }
        if (!t.empty())
        {
            ImGui::SameLine(0, S(14));
            ImGui::AlignTextToFramePadding();
            Muted("%s", t.c_str());
            if (t != src && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", src.c_str());
        }
    }
    ImGui::SameLine(x0 + w - saveW - openW - S(8) - projW - ImGui::GetWindowPos().x);
    // Проект, «Открыть папку», «Сохранить отчёт» — в порядке «справа главное»
    ProcProjectButtons(ImGui::GetCursorPosX(), narrow, false);
    ImGui::SameLine(0, S(8));
    const bool cont = ProcCont();
    const bool saving = proc_.save.valid() || proc_.saveRequested;
    const bool canSave = proc_.files && !proc_.meas.empty() && !proc_.load.valid() && (!cont || !proc_.pos.empty());
    if (Button("Открыть папку", BtnKind::Normal, ImVec2(openW, 0), proc_.outDir.empty() && !proc_.dir[0]))
        dialogs::OpenFolder(proc_.outDir.empty() ? std::string(proc_.dir) : proc_.outDir);
    ImGui::SameLine(0, S(8));
    if (Button(saving ? "Сохранение…" : "Сохранить отчёт", BtnKind::Orange, ImVec2(saveW, 0), saving || !canSave,
               saving ? "Отчёт сохраняется" : cont ? "Нет положений" : "Нет замеров"))
        ProcessSaveReport();
    if (ImGui::IsItemHovered() && !saving && canSave)
        ImGui::SetTooltip("В папку с файлами: %s (лист «Протокол» с графиками, «Графики», таблицы, «Проверки»),\n"
                          "картинки графиков — PNG рядом",
                          heel::kReportName);
}

// Параметры опыта кратко (одной строкой) + режим + «Параметры ▾» (полная карточка — ProcParams).
void App::ProcParamsStrip(float w)
{
    if (BeginCard("##proc_strip2", ImVec2(w, 0)))
    {
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const char* tgl = proc_.paramsOpen ? "Скрыть параметры ▲" : "Параметры ▼";
        const float tw = ImGui::CalcTextSize("Скрыть параметры ▲").x + S(26);
        const bool model = settings_.heelSmall;
        std::string sens;
        for (const int k : proc_.keys)
            if (!proc_.skip.count(k))
                sens += (sens.empty() ? "" : ", ") + heel::SensorShort(k) + " " + SensorTitle(k) +
                        (proc_.invert.count(k) ? " (±)" : "");
        std::string t = Fmt("D %s %s · P %s %s · L %s %s · %s · ось %s", Fmt(NumFmt(settings_.heelD, settings_.digD).c_str(), settings_.heelD).c_str(),
                            model ? "кг" : "т", Fmt(NumFmt(settings_.heelP, settings_.digP).c_str(), settings_.heelP).c_str(),
                            model ? "кг" : "т", Fmt(NumFmt(settings_.heelL, std::max(1, settings_.digArm - 2)).c_str(), settings_.heelL).c_str(),
                            model ? "мм" : "м", model ? "модель" : "судно", settings_.heelAxis == 0 ? "X" : "Y");
        if (!sens.empty())
            t += " · " + sens;
        const std::string full = t + " · " + Shown(proc_.outDir.empty() ? std::string(proc_.dir) : proc_.outDir);
        const float room = right - ModeSwitchWidthPublic() - tw - S(36) - ImGui::GetCursorScreenPos().x;
        const std::string shown = FitText(ImGui::CalcTextSize(full.c_str()).x <= room ? full : t, room);
        ImGui::AlignTextToFramePadding();
        TextColored(pal.textDim, "%s", shown.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", full.c_str());
        ProcModeSwitch(right - tw - S(10));
        ImGui::SameLine(right - tw - ImGui::GetWindowPos().x);
        if (Button(tgl, BtnKind::Normal, ImVec2(tw, 0)))
            proc_.paramsOpen = !proc_.paramsOpen;
    }
    EndCard();
}

// Ряд карточек-показателей.
void App::ProcKpis(float w)
{
    const bool cont = ProcCont();
    const heel::Summary& s = cont ? proc_.posSum : proc_.sum;
    const heel::RsCase& rc = proc_.rs;
    const bool byTable = ProcSettings().RsMain() == 1;
    const bool dh = rc.dh != 0.0;
    const double hMain = byTable ? rc.r.hFinal : s.h - rc.dh;
    const double hOther = byTable ? s.h - rc.dh : rc.r.hFinal;
    const float gap = compactUi ? S(8) : S(12), h = compactUi ? S(46) : S(52); // значок, число и подпись — в строку
    // Узко (окно 1024–1152 точек) — 2 × 2, чтобы подписи не обрезались
    const int cols = w < S(1000) ? 2 : 4;
    const float cw = std::floor((w - (cols - 1) * gap) / cols);
    int bad = 0, known = 0;
    std::string badList;
    for (const rs::Check& c : rc.checks)
    {
        known += c.state >= 0;
        if (c.state == 0)
        {
            bad++;
            badList += (badList.empty() ? "" : ", ") + c.id;
        }
    }
    auto card = [&](int i, const char* title, const std::string& value, const ImVec4& valueColor, const std::string& sub,
                    const ImVec4& subColor, const ImVec4& soft, const ImVec4& fg, const char* glyph, const std::string& tip) {
        if (i % cols)
            ImGui::SameLine(0, gap);
        else if (i)
            ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        char id[16];
        std::snprintf(id, sizeof(id), "##kpi%d", i);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(6)));
        const bool open = BeginCard(id, ImVec2(i % cols == cols - 1 ? w - (cols - 1) * (cw + gap) : cw, h));
        ImGui::PopStyleVar();
        if (open)
        {
            // [значок] число  подпись · пояснение — одной строкой; пояснение целиком — в подсказке
            const float isz = S(28);
            const ImVec2 wp = ImGui::GetWindowPos();
            const float right = wp.x + ImGui::GetWindowContentRegionMax().x;
            const ImVec2 p0(ImGui::GetCursorScreenPos().x, wp.y + (h - isz) * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + isz, p0.y + isz), U32(soft), S(7));
            {
                FontScope f(fontBold);
                const ImVec2 ts = ImGui::CalcTextSize(glyph);
                dl->AddText(ImVec2(p0.x + (isz - ts.x) * 0.5f, p0.y + (isz - ts.y) * 0.5f), U32(fg), glyph);
            }
            float x = p0.x + isz + S(10);
            {
                FontScope f(fontH2);
                const std::string v = FitText(value, right - x);
                const float vy = wp.y + h * 0.5f - fontH2->FontSize * 0.5f - S(1);
                dl->AddText(fontH2, fontH2->FontSize, ImVec2(x, vy), U32(valueColor), v.c_str());
                x += ImGui::CalcTextSize(v.c_str()).x + S(8);
            }
            {
                FontScope f(fontSmall);
                const float lh = fontSmall->FontSize;
                const std::string t1 = FitText(title, right - x), t2 = FitText(sub, right - x);
                const float y0 = wp.y + h * 0.5f - lh - S(1);
                dl->AddText(fontSmall, lh, ImVec2(x, y0), U32(pal.muted), t1.c_str());
                dl->AddText(fontSmall, lh, ImVec2(x, y0 + lh + S(1)), U32(subColor), t2.c_str());
            }
            ImGui::Dummy(ImVec2(right - ImGui::GetCursorScreenPos().x, h - S(12)));
        }
        EndCard();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s%s\n%s", title, tip.empty() ? "" : " — ", tip.c_str(), sub.c_str());
    };
    const bool mm = settings_.heelSmall;
    auto hText = [&](double v) { return std::isfinite(v) ? Fmt("%.3f м", v) : std::string("—"); };
    auto mmText = [&](double v) { return mm && std::isfinite(v) ? Fmt("%.1f мм · ", v * 1000.0) : std::string(); };
    card(0, "h, основной расчёт", hText(hMain), pal.text,
         mmText(hMain) +
             std::string(byTable ? (rc.r.reduced ? "табл. 6.5, по 6.6.5" : "по табл. 6.5") : "от начального") +
             (dh ? " · испр. (−Δh)" : ""),
         dh ? pal.warn : pal.muted, pal.blueSoft, pal.accent, "h",
         "Основной расчёт (выбирается в «Правила РС»)");
    card(1, byTable ? "от начального" : "по табл. 6.5", hText(hOther), pal.text,
         std::isfinite(s.ls.h) ? Fmt("МНК %.3f м · R² %.4f", s.ls.h, s.ls.r2) : std::string("МНК —"), pal.muted,
         pal.greenSoft, pal.green, "∑", "Второй расчёт и наклон прямой МНК");
    card(2, "Правила РС", rc.checks.empty() ? std::string("—") : Fmt("%d из %d", known - bad, known),
         rc.checks.empty() ? pal.muted : bad ? pal.err : pal.ok,
         rc.checks.empty() ? std::string("нужно два положения и больше")
         : bad             ? "не выполнено: " + badList
                           : std::string("требования выполнены"),
         bad ? pal.err : pal.muted, bad ? pal.redSoft : pal.greenSoft, bad ? pal.err : pal.ok, bad ? "✗" : "✓",
         "Требования Правил РС, у которых есть данные (подробно — вкладка «Правила РС»)");
    if (std::isfinite(rc.r.err))
        card(3, "вероятная ошибка", Fmt("%.4f м", rc.r.err), rc.r.ok662 ? pal.text : pal.err,
             Fmt("≤ %.4f м · n = %d", rc.r.errLimit, rc.r.n), pal.muted, pal.orangeSoft, pal.orange, "±",
             "Вероятная ошибка опыта и её допуск по п. 6.6.2");
    else
    {
        const int items = cont ? static_cast<int>(proc_.pos.size()) : static_cast<int>(proc_.meas.size());
        card(3, cont ? "положений груза" : "замеров", Fmt("%d", items), pal.text,
             Fmt("датчиков %d%s", static_cast<int>(PostsInUse().size()),
                 rc.tcN ? Fmt(" · качка %.1f с", rc.tc).c_str() : ""),
             pal.muted, pal.orangeSoft, pal.orange, "θ", "");
    }
}

// Карточка «Контроль M – tg θ».
void App::ProcControlCard(float w, float h)
{
    if (BeginCard("##proc_ctrl", ImVec2(w, h)))
    {
        const float right = ProcCardTitle("Контроль M – tg θ", 2);
        HeaderHint(right, "п. 5.5 прил. 3 · красная — исключённый замер · прямая — МНК", "п. 5.5 прил. 3");
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ProcControlPlot(avail.x, avail.y, false);
    }
    EndCard();
}

// Карточка с вкладками: положения / замеры, Правила РС, перемещения, журнал.
void App::ProcTabsCard(float w, float h)
{
    if (BeginCard("##proc_tabs", ImVec2(w, h)))
    {
        const bool cont = ProcCont();
        const char* labels[] = {cont ? "Положения груза" : "Замеры", "Правила РС", "Перемещения", "Журнал"};
        static const int values[] = {0, 1, 2, 3};
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float bs = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(right - bs, start.y + S(2)));
        if (ExpandButton("##expand3", proc_.expanded == 3))
            proc_.expanded = proc_.expanded == 3 ? 0 : 3;
        ImGui::SetCursorScreenPos(start);
        Tabs("ptabs", &proc_.tab, labels, values, 4, right - start.x - bs - S(10));
        ImGui::Dummy(ImVec2(0, S(2)));
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (procNatural_)
            avail.y = 0.f; // по содержимому (карточки и таблицы — AutoResizeY)
        cardFlat++;
        switch (proc_.tab)
        {
        case 1: ProcRsView(avail.x, avail.y); break;
        case 2: ProcMovesView(avail.x, avail.y); break;
        case 3:
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.cardAlt);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(6)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(6), S(2)));
            ImGui::BeginChild("##proclog2", ImVec2(avail.x, procNatural_ ? 0.f : avail.y),
                              ImGuiChildFlags_AlwaysUseWindowPadding | NaturalChild());
            ProcLogLines();
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            break;
        }
        default:
            if (cont)
                ProcPositions(avail.x, avail.y);
            else
                ProcTable(avail.x, avail.y);
        }
        cardFlat--;
    }
    EndCard();
}

// Компактный режим: «Запись с положениями» (или «Графики крена») и «Контроль M – tg θ» — вкладками в одной карточке,
// чтобы графику доставалась вся высота.
void App::ProcGraphsCard(float w, float h)
{
    if (BeginCard("##proc_graphs", ImVec2(w, h)))
    {
        const bool cont = ProcCont();
        const char* labels[] = {cont ? "Запись с положениями" : "Графики крена", "Контроль M – tg θ"};
        static const int values[] = {0, 1};
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float bs = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(right - bs, start.y + S(2)));
        if (ExpandButton("##expandg", false))
            proc_.expanded = proc_.gtab == 0 ? 1 : 2;
        ImGui::SetCursorScreenPos(start);
        Tabs("gtabs", &proc_.gtab, labels, values, 2, right - start.x - bs - S(10));
        ImGui::Dummy(ImVec2(0, S(1)));
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        cardFlat++;
        if (proc_.gtab == 1)
            ProcControlCard(avail.x, avail.y);
        else if (cont)
            ProcSeriesCard(avail.x, avail.y);
        else
            ProcPlots(avail.x, avail.y);
        cardFlat--;
    }
    EndCard();
}

// Страница «Обработка».
void App::PageProcess()
{
    ProcMethodWindow(); // окно своих методов — поверх страницы
    if (!proc_.opened)
    {
        proc_.opened = true;
        if (!proc_.dir[0])
            std::snprintf(proc_.dir, sizeof(proc_.dir), "%s",
                          settings_.procDir.empty() ? files_.dir : settings_.procDir.c_str());
        if (!proc_.files && !proc_.load.valid())
            ProcLoad();
    }
    if (proc_.dirty && !proc_.load.valid())
        ProcRecompute();
    // Параметры раскрыты, пока файлов нет; после первого чтения — свёрнуты в строку (раскрываются кнопкой)
    if (proc_.files && !proc_.paramsAuto)
    {
        proc_.paramsAuto = true;
        proc_.paramsOpen = proc_.meas.empty();
    }

    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = compactUi ? S(8) : S(12);
    const float sp = ImGui::GetStyle().ItemSpacing.y;
    ProcHeader(W);
    ImGui::Dummy(ImVec2(0, gap - sp));
    if (proc_.projMissing)
    {
        ProcProjectMissingCard(W);
        ImGui::Dummy(ImVec2(0, gap - sp));
    }
    const bool cont = ProcCont();
    const int view = proc_.files ? ProcView() : 1;
    if ((proc_.expanded == 0 && view != 2) || !proc_.files)
    {
        if (proc_.files)
        {
            ProcParamsStrip(W);
            ImGui::Dummy(ImVec2(0, gap - sp));
        }
        if (proc_.paramsOpen || !proc_.files)
        {
            ProcParams(W);
            ImGui::Dummy(ImVec2(0, gap - sp));
        }
        ProcKpis(W);
        ImGui::Dummy(ImVec2(0, gap - sp));
    }
    // Видимая высота страницы (страница «Обработки» прокручивается целиком; карточки — своей высоты)
    const float pageH = std::max(S(300), ImGui::GetWindowHeight() - 2 * ImGui::GetStyle().WindowPadding.y);
    const float avail = std::max(S(260), ImGui::GetContentRegionAvail().y);
    if (proc_.expanded == 1)
    {
        if (cont)
            ProcSeriesCard(W, avail);
        else
            ProcPlots(W, avail);
        return;
    }
    if (proc_.expanded == 2)
    {
        ProcControlCard(W, avail);
        return;
    }
    if (proc_.expanded == 3)
    {
        ProcTabsCard(W, avail);
        return;
    }
    if (!proc_.files)
        return;
    if (view == 1)
    {
        // «Положения и расчёт»: таблица положений / Правила РС / перемещения / журнал — во всю ширину и высоту
        ProcTabsCardAuto(W);
        return;
    }
    if (view == 2)
    {
        // «Графики»: запись с положениями (и положение крупно) и контроль — друг под другом, крупно
        const float g1 = std::max(S(320), std::floor((avail - gap - sp) * 0.56f));
        const float g2 = std::max(S(300), avail - g1 - gap - sp);
        if (cont)
            ProcSeriesCard(W, g1);
        else
            ProcPlots(W, g1);
        ImGui::Dummy(ImVec2(0, gap - sp));
        ProcControlCard(W, g2);
        return;
    }
    // Обзор: сверху запись / графики и контроль (высота — делителем, доля видимой высоты страницы), снизу — вкладки
    // во всю высоту содержимого; страница прокручивается целиком
    const float minH = S(240);
    const float topH = std::clamp(std::floor(pageH * static_cast<float>(settings_.procSplit)), minH, std::max(minH, pageH - S(80)));
    const float lw = std::floor((W - gap) * 0.6f);
    if (compactUi) // HD ready: графики — вкладками в одной карточке на всю ширину
        ProcGraphsCard(W, topH);
    else
    {
        if (cont)
            ProcSeriesCard(lw, topH);
        else
            ProcPlots(lw, topH);
        ImGui::SameLine(0, gap);
        ProcControlCard(W - lw - gap, topH);
    }
    {
        // Делитель: высота графиков (доля видимой высоты страницы, запоминается)
        const ImVec2 p(ImGui::GetCursorScreenPos().x, ImGui::GetItemRectMax().y);
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##procsplit", ImVec2(W, gap));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        if (hov || act)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (act && ImGui::GetIO().MouseDelta.y != 0.f)
            settings_.procSplit = std::clamp(settings_.procSplit + ImGui::GetIO().MouseDelta.y / pageH, 0.25, 0.85);
        if (ImGui::IsItemDeactivated())
            SaveSettingsSoon();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cx = p.x + W * 0.5f, cy = p.y + gap * 0.5f;
        dl->AddRectFilled(ImVec2(cx - S(22), cy - S(2)), ImVec2(cx + S(22), cy + S(2)),
                          U32(hov || act ? pal.accent : Alpha(pal.muted, 0.45f)), S(2));
        if (hov && !act)
            ImGui::SetTooltip("Перетащите, чтобы сделать графики выше или ниже (страница прокручивается целиком)");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + gap));
    }
    ProcTabsCardAuto(W);
}

// Подпункт «Обработки»: выбранный вручную или сам — на широком и высоком окне «Обзор», иначе «Положения и расчёт».
bool App::ProcWideScreen() const
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return vp->WorkSize.x / uiScale >= 1279.5f && vp->WorkSize.y / uiScale >= 799.5f;
}

int App::ProcView() const
{
    const bool wide = ProcWideScreen();
    if (proc_.view == 1 || proc_.view == 2)
        return proc_.view;
    if (proc_.view == 0 && wide)
        return 0;
    return wide ? 0 : 1;
}

// Карточка вкладок во всю высоту содержимого: таблицы без своей прокрутки, панели и карточка — по содержимому
// (AutoResizeY); прокручивается страница целиком.
int App::NaturalChild() const
{
    return procNatural_ ? ImGuiChildFlags_AutoResizeY : ImGuiChildFlags_None;
}

void App::ProcTabsCardAuto(float w)
{
    procNatural_ = true;
    ProcTabsCard(w, 0.f);
    procNatural_ = false;
}

} // namespace ui
