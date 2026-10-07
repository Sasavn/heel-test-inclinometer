// Страница «Измерение»: сводка ГОТОВ / КАЧКА, запись, карта, АКБ, опрос; карточки датчиков Д2 и Д3 с углами и
// качкой по осям; ноль; графики углов X и Y за выбранное окно (10 с … 10 мин).
#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "../core/TextUtil.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace
{

struct SensorView
{
    bool known = false;  // есть данные (поток или status)
    bool ok = false;     // отвечает
    bool fresh = false;  // данные не старше 2 с
    char st = '?';       // O / L / A
    double x = proto::kNaN, y = proto::kNaN;
    const proto::SensorStatus* status = nullptr;
};

} // namespace

void App::PageMeasure()
{
    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = S(12);
    if (!Connected())
    {
        NotConnectedPanel("Здесь будут углы обоих датчиков, оценка качки и графики.");
        ImGui::Dummy(ImVec2(0, gap));
    }

    // Строка 1: сводка и плитки
    const float rowH = S(108);
    const float bannerW = std::max(S(330), std::floor(W * 0.40f));
    MeasureBanner(bannerW, rowH);
    ImGui::SameLine(0, gap);
    MeasureTiles(W - bannerW - gap, rowH);
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));

    // Строка 2: датчики
    const float cardW = std::floor((W - gap) * 0.5f);
    const float cardH = S(182);
    SensorCard(0, cardW, cardH);
    ImGui::SameLine(0, gap);
    SensorCard(1, W - cardW - gap, cardH);
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));

    // Строка 3: ноль и окно графика (на узком окне — без подписи и плотнее)
    const bool narrow = W < S(900);
    const bool canZero = HaveStatus();
    if (Button("Ноль", BtnKind::Normal, ImVec2(narrow ? 0.f : S(110), 0), !canZero, "Нет связи с прибором"))
        ImGui::OpenPopup("Ноль?");
    if (ImGui::IsItemHovered() && canZero)
        ImGui::SetTooltip("Принять текущие углы отвечающих датчиков за ноль (как кнопка «Ноль» на приборе).\n"
                          "Ноль не сохраняется при выключении прибора.");
    ImGui::SameLine(0, S(8));
    if (Button("Сбросить ноль", BtnKind::Normal, ImVec2(narrow ? 0.f : S(150), 0), !canZero, "Нет связи с прибором"))
        ImGui::OpenPopup("Сбросить ноль?");
    for (int z = 0; z < 2; z++)
    {
        const char* id = z == 0 ? "Ноль?" : "Сбросить ноль?";
        if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted(z == 0 ? "Принять текущие углы отвечающих датчиков за ноль?"
                                          : "Сбросить ноль у всех датчиков (углы — без вычета нуля)?");
            if (Recording())
                TextColored(pal.warn, "Идёт запись: в файлах изменятся колонки OffsetX/Y и CalcX/Y.");
            ImGui::Spacing();
            if (Button(z == 0 ? "Ноль" : "Сбросить", BtnKind::Primary, ImVec2(S(140), 0)))
            {
                set_.zeroReq = link_->Send(z == 0 ? "zero" : "zero reset");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (Button("Отмена", BtnKind::Normal, ImVec2(S(120), 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    ImGui::SameLine(0, narrow ? S(16) : S(28));
    if (!narrow)
    {
        ImGui::AlignTextToFramePadding();
        Muted("Графики:");
        ImGui::SameLine(0, S(8));
    }
    static const char* labels[] = {"10 с", "30 с", "1 мин", "2 мин", "5 мин", "10 мин"};
    static const int values[] = {10, 30, 60, 120, 300, 600};
    Segmented("win", &settings_.chartWindowS, labels, values, 6);
    ImGui::SameLine(0, S(12));
    if (Button(meas_.paused ? "▶ Продолжить" : "■ Пауза", meas_.paused ? BtnKind::Primary : BtnKind::Normal,
               ImVec2(narrow ? 0.f : S(140), 0)))
    {
        meas_.paused = !meas_.paused;
        meas_.pausedAt = static_cast<double>(link_->NowMs()) / 1000.0;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Остановить прокрутку графиков (данные продолжают приходить)");
    ImGui::Dummy(ImVec2(0, S(4)));

    const float rest = ImGui::GetContentRegionAvail().y;
    Charts(W, std::max(S(200), rest));
}

void App::MeasureBanner(float w, float h)
{
    ImVec4 fg = pal.muted, bg = pal.cardAlt;
    std::string big, sub;
    if (!Connected())
    {
        big = "НЕТ СВЯЗИ";
        sub = "Прибор не подключён к ПК.";
    }
    else if (!snap_.haveStatus)
    {
        big = "ОЖИДАНИЕ";
        sub = "Запрос состояния прибора…";
    }
    else
    {
        const auto& s = snap_.status;
        int okCount = 0;
        for (const auto& d : s.sens)
            okCount += d.Ok();
        if (okCount == 0)
        {
            big = "НЕТ ДАТЧИКОВ";
            sub = "Ни один датчик не отвечает: проверьте шину RS485 и питание датчиков.";
            fg = pal.err;
            bg = pal.errBg;
        }
        else if (s.stable)
        {
            big = "ГОТОВ";
            sub = Fmt("Покой по всем осям: наибольший размах %.2f° меньше порога %.2f° за %d с. Можно снимать отсчёт.",
                      s.span, s.rollCalm, s.rollWin);
            fg = pal.ok;
            bg = pal.okBg;
        }
        else if (s.stabFill < s.rollWin)
        {
            big = "СБОР ДАННЫХ";
            sub = Fmt("Окно качки заполнено на %d из %d с — оценка покоя появится, когда окно заполнится.", s.stabFill,
                      s.rollWin);
            fg = pal.info;
            bg = pal.infoBg;
        }
        else
        {
            big = Fmt("КАЧКА %.2f°", s.span);
            std::string where;
            if (s.stabAddr)
                where = Fmt("датчик Д%d, ось %s — ", *s.stabAddr, s.stabAxis.c_str());
            sub = "Наибольший размах: " + where + Fmt("%.2f° (порог покоя %.2f°). Дождитесь покоя.", s.span, s.rollCalm);
            fg = pal.warn;
            bg = pal.warnBg;
        }
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, Alpha(fg, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(12)));
    ImGui::BeginChild("##banner", ImVec2(w, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        dl->AddRectFilled(p, ImVec2(p.x + S(6), p.y + h), U32(fg), S(8), ImDrawFlags_RoundCornersLeft);
    }
    {
        FontScope f(fontHuge);
        TextColored(fg, "%s", big.c_str());
    }
    ImGui::PushTextWrapPos(0.f);
    ImGui::PushStyleColor(ImGuiCol_Text, pal.textDim);
    ImGui::TextUnformatted(sub.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void App::MeasureTiles(float w, float h)
{
    const float gap = S(10);
    const float tw = std::floor((w - gap) * 0.5f), th = std::floor((h - gap) * 0.5f);
    const bool have = HaveStatus();
    const auto& s = snap_.status;
    const auto* smp = snap_.haveSample && Connected() ? &snap_.sample : nullptr;
    const bool compact = tw < S(285);

    auto tile = [&](const char* id, const char* label, const std::string& value, const ImVec4& color,
                    const std::string& tip) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.card);
        ImGui::PushStyleColor(ImGuiCol_Border, pal.border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(6)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(6), S(1)));
        ImGui::BeginChild(id, ImVec2(tw, th), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
        {
            FontScope f(fontSmall);
            Muted("%s", label);
        }
        {
            FontScope f(fontBold);
            TextColored(color, "%s", value.c_str());
        }
        ImGui::EndChild();
        if (!tip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip.c_str());
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    };

    ImGui::BeginGroup();
    // Запись
    {
        std::string v = "—";
        ImVec4 c = pal.muted;
        std::string tip;
        if (have)
        {
            if (s.Recording())
            {
                v = compact ? Fmt("● M%03d · %s", s.file, text::Clock(s.recS).c_str())
                            : Fmt("● ЗАПИСЬ  M%03d · %s", s.file, text::Clock(s.recS).c_str());
                c = pal.err;
                tip = Fmt("Идёт замер M%03d: %s, строк %llu", s.file, text::Duration(s.recS).c_str(),
                          static_cast<unsigned long long>(s.rows));
                for (const auto& n : s.names)
                    tip += "\n" + n;
            }
            else if (s.sd == "ERROR")
            {
                v = Fmt("ошибка (FatFs %d)", s.sdErr);
                c = pal.err;
                tip = "Запись прервана ошибкой карты. Верните тумблер в STOP.";
            }
            else
            {
                v = s.file > 0 ? Fmt(compact ? "стоп · далее M%03d" : "остановлена · далее M%03d", s.file)
                               : std::string("остановлена");
                c = pal.text;
                tip = "Запись включается тумблером на приборе.";
            }
        }
        tile("##t_rec", "Запись замера", v, c, tip);
    }
    ImGui::SameLine(0, gap);
    // Карта
    {
        std::string v = "—";
        ImVec4 c = pal.muted;
        if (have)
        {
            if (s.sd == "NO_CARD")
            {
                v = "нет карты";
                c = pal.err;
            }
            else
            {
                v = s.sd == "ERROR" ? "ошибка" : s.Recording() ? "пишется" : "готова";
                c = s.sd == "ERROR" ? pal.err : pal.text;
                if (s.sdFreeMb)
                    v += (compact ? " · своб. " : " · свободно ") + text::MegaBytes(*s.sdFreeMb);
            }
        }
        tile("##t_sd", "Карта SD", v, c,
             have && s.sdTotalMb ? "Ёмкость " + text::MegaBytes(s.sdTotalMb) : std::string());
    }
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    // Питание
    {
        std::string v = "—";
        ImVec4 c = pal.muted;
        std::string tip;
        if (have)
        {
            if (!s.batPresent)
            {
                v = "от USB (АКБ нет)";
                c = pal.text;
            }
            else
            {
                v = Fmt(compact ? "%.2f В" : "АКБ %.2f В", s.batV);
                if (s.batPct)
                    v += Fmt(" · %d %%", *s.batPct);
                c = s.batLow ? pal.err : pal.text;
                if (s.batLow)
                    v += compact ? " · НИЗКОЕ" : " · РАЗРЯЖЕНА";
                tip = Fmt("Тревога ниже %.1f В", s.batAlarm);
            }
        }
        else if (smp && std::isfinite(smp->batV))
        {
            v = Fmt("АКБ %.2f В", smp->batV);
            c = pal.text;
        }
        tile("##t_bat", "Питание", v, c, tip);
    }
    ImGui::SameLine(0, gap);
    // Опрос
    {
        std::string v = "—";
        ImVec4 c = pal.muted;
        std::string tip;
        if (have)
        {
            v = Fmt(compact ? "%.1f/%d Гц · ЦП %d %%" : "%.1f Гц из %d · ЦП %d %%", s.rate, s.freq, s.cpu);
            c = (s.freq > 0 && s.rate < 0.9 * s.freq) ? pal.warn : pal.text;
            tip = "Фактическая частота опроса датчиков (заданная — в «Настройках»). Если шина не успевает — ниже "
                  "заданной.";
            if (s.maxRate > 0)
                tip += Fmt("\nПредел по шине: %.1f Гц", s.maxRate);
        }
        tile("##t_rate", "Опрос датчиков", v, c, tip);
    }
    ImGui::EndGroup();
}

void App::SensorCard(int idx, float w, float h)
{
    const int addr = History::kAddr[idx];
    SensorView v;
    if (Connected())
    {
        if (snap_.haveStatus)
            v.status = snap_.status.Sensor(addr);
        const proto::StreamSensor* ss = snap_.haveSample ? snap_.sample.Sensor(addr) : nullptr;
        const bool sampleFresh = snap_.haveSample && snap_.nowMs - snap_.sampleAtMs < 2000;
        if (ss && sampleFresh)
        {
            v.known = true;
            v.st = ss->st;
            v.ok = ss->st == 'O';
            v.x = ss->x;
            v.y = ss->y;
            v.fresh = true;
        }
        else if (v.status)
        {
            v.known = true;
            v.ok = v.status->Ok();
            v.st = v.ok ? 'O' : v.status->st == "LOST" ? 'L' : 'A';
            v.x = v.status->x;
            v.y = v.status->y;
            v.fresh = snap_.nowMs - snap_.statusAtMs < 3000;
        }
    }

    char id[16];
    std::snprintf(id, sizeof(id), "##sens%d", idx);
    if (BeginCard(id, ImVec2(w, h)))
    {
        // Шапка: имя, адрес, состояние
        {
            FontScope f(fontH2);
            ImGui::Text("Датчик Д%d", addr);
        }
        ImGui::SameLine(0, S(10));
        {
            FontScope f(fontSmall);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
            Muted("Modbus-адрес %d", addr);
        }
        const char* stText = "нет данных";
        ImVec4 fg = pal.muted, bg = pal.button;
        if (v.known)
        {
            if (v.st == 'O')
                stText = "в работе", fg = pal.ok, bg = pal.okBg;
            else if (v.st == 'L')
                stText = "нет связи", fg = pal.err, bg = pal.errBg;
            else
                stText = "не подключён", fg = pal.muted, bg = pal.button;
        }
        const float chipW = ImGui::CalcTextSize(stText).x + S(34);
        ImGui::SameLine(w - S(28) - chipW);
        ImGui::SetCursorPosY(S(8));
        Chip(stText, fg, bg);

        // Углы X и Y
        const float colW = (w - S(28)) * 0.5f;
        const bool compact = colW < S(250);
        const float top = ImGui::GetCursorPosY() + S(2);
        for (int axis = 0; axis < 2; axis++)
        {
            ImGui::SetCursorPos(ImVec2(S(14) + axis * colW, top));
            ImGui::BeginGroup();
            {
                FontScope f(fontSmall);
                Muted(axis == 0 ? "Угол X" : "Угол Y");
            }
            const double val = axis == 0 ? v.x : v.y;
            {
                FontScope f(fontBig);
                const ImVec4 c = !v.known ? pal.muted : (v.ok && v.fresh) ? pal.text : pal.muted;
                TextColored(c, "%s", text::Angle(val, 3).c_str());
            }
            // Качка по оси
            std::string roll = " ";
            ImVec4 rc = pal.muted;
            if (v.status && v.ok && snap_.haveStatus)
            {
                const auto& st = *v.status;
                const double r = axis == 0 ? st.rollX : st.rollY;
                const bool calm = axis == 0 ? st.calmX : st.calmY;
                if (st.fill < snap_.status.rollWin)
                {
                    roll = Fmt(compact ? "◔ сбор %d/%d с" : "◔ сбор %d из %d с", st.fill, snap_.status.rollWin);
                    rc = pal.info;
                }
                else if (calm)
                {
                    roll = Fmt(compact ? "● покой %.2f°" : "● покой · размах %.2f°", r);
                    rc = pal.ok;
                }
                else
                {
                    roll = Fmt("▲ качка %.2f°", r);
                    rc = pal.warn;
                }
            }
            else if (v.known && !v.ok)
                roll = "—";
            {
                FontScope f(fontBold);
                TextColored(rc, "%s", roll.c_str());
            }
            ImGui::EndGroup();
        }

        // Подвал: ноль, ошибки, давность
        ImGui::SetCursorPosY(h - S(11) - fontSmall->FontSize - S(2));
        FontScope f(fontSmall);
        if (v.status)
        {
            std::string foot = "ноль: X " + text::Angle(v.status->ox) + ", Y " + text::Angle(v.status->oy);
            if (v.status->err)
                foot += Fmt(" · ошибок опроса %llu", static_cast<unsigned long long>(v.status->err));
            if (!v.ok && v.status->ageMs)
            {
                foot += " · последний ответ " + text::Duration(*v.status->ageMs / 1000) + " назад";
                TextColored(pal.err, "%s", foot.c_str());
            }
            else
                Muted("%s", foot.c_str());
        }
        else
            Muted("%s", Connected() ? "ожидание данных…" : "нет связи с прибором");
    }
    EndCard();
}

void App::Charts(float w, float h)
{
    History& hist = link_->GetHistory();
    const double now = static_cast<double>(link_->NowMs()) / 1000.0;
    const double view = meas_.paused ? meas_.pausedAt : now;
    const double win = settings_.chartWindowS;

    static std::vector<double> xs, ys[2][2]; // [ось][датчик]
    {
        std::lock_guard lock(hist.mutex);
        const std::size_t i0 = hist.LowerBound(view - win - 1.0);
        const std::size_t i1 = hist.LowerBound(view + 0.001);
        const std::size_t n = i1 > i0 ? i1 - i0 : 0;
        xs.resize(n);
        for (auto& a : ys)
            for (auto& v : a)
                v.resize(n);
        double prevT = -1e300;
        std::size_t m = 0;
        for (std::size_t k = 0; k < n; k++)
        {
            const auto& s = hist.At(i0 + k);
            // Перерыв в данных больше 1,5 с — точка NaN: линия не тянется через пропуск
            if (s.t - prevT > 1.5 && m > 0)
            {
                xs.push_back(0);
                for (auto& a : ys)
                    for (auto& v : a)
                        v.push_back(0);
                xs[m] = 0.5 * (prevT + s.t) - view;
                for (int i = 0; i < 2; i++)
                    ys[0][i][m] = ys[1][i][m] = std::numeric_limits<double>::quiet_NaN();
                m++;
            }
            prevT = s.t;
            xs[m] = s.t - view;
            for (int i = 0; i < 2; i++)
            {
                ys[0][i][m] = s.x[i];
                ys[1][i][m] = s.y[i];
            }
            m++;
        }
        xs.resize(m);
        for (auto& a : ys)
            for (auto& v : a)
                v.resize(m);
    }

    // Стиль ImPlot под тему
    ImPlotStyle& ps = ImPlot::GetStyle();
    ps.Colors[ImPlotCol_PlotBg] = pal.card;
    ps.Colors[ImPlotCol_FrameBg] = pal.card;
    ps.Colors[ImPlotCol_PlotBorder] = pal.border;
    ps.Colors[ImPlotCol_LegendBg] = Alpha(pal.card, 0.92f);
    ps.Colors[ImPlotCol_LegendBorder] = pal.border;
    ps.Colors[ImPlotCol_LegendText] = pal.text;
    ps.Colors[ImPlotCol_TitleText] = pal.text;
    ps.Colors[ImPlotCol_AxisText] = pal.textDim;
    ps.Colors[ImPlotCol_AxisGrid] = Alpha(pal.border, darkTheme ? 0.45f : 0.75f);
    ps.Colors[ImPlotCol_AxisTick] = pal.border;
    ps.Colors[ImPlotCol_InlayText] = pal.textDim;
    ps.PlotPadding = ImVec2(S(10), S(10));
    ps.LabelPadding = ImVec2(S(5), S(3));
    ps.LegendPadding = ImVec2(S(8), S(6));
    ps.LineWeight = 1.6f * uiScale;

    const float gap = S(12);
    const float pw = std::floor((w - gap) * 0.5f);
    static const char* titles[] = {"Угол X, °", "Угол Y, °"};
    static const char* ids[] = {"##plotx", "##ploty"};
    for (int axis = 0; axis < 2; axis++)
    {
        if (axis)
            ImGui::SameLine(0, gap);
        char cid[16];
        std::snprintf(cid, sizeof(cid), "##chart%d", axis);
        if (BeginCard(cid, ImVec2(axis ? w - pw - gap : pw, h)))
        {
            {
                FontScope f(fontBold);
                ImGui::TextUnformatted(titles[axis]);
            }
            ImGui::SameLine();
            {
                FontScope f(fontSmall);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(2));
                if (meas_.paused)
                    Muted("  (пауза)");
                else
                    Muted("  за последние %s", (win >= 60 ? Fmt("%.0f мин", win / 60) : Fmt("%.0f с", win)).c_str());
            }
            // Пределы по Y — по видимым точкам, не уже 0,5°
            double lo = 1e9, hi = -1e9;
            for (int i = 0; i < 2; i++)
                for (std::size_t k = 0; k < xs.size(); k++)
                {
                    const double v = ys[axis][i][k];
                    if (std::isfinite(v) && xs[k] >= -win)
                    {
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                    }
                }
            if (lo > hi)
                lo = -1.0, hi = 1.0;
            const double mid = 0.5 * (lo + hi);
            const double half = std::max(0.5 * (hi - lo) * 1.15, 0.25);
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (ImPlot::BeginPlot(ids[axis], avail, ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText))
            {
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
                ImPlot::SetupAxisLimits(ImAxis_X1, -win, 0.0, ImPlotCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, mid - half, mid + half, ImPlotCond_Always);
                ImPlot::SetupAxisFormat(ImAxis_X1, win >= 120 ? "%.0f с" : "%.0f с");
                ImPlot::SetupAxisFormat(ImAxis_Y1, half < 0.6 ? "%.2f°" : "%.1f°");
                ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
                for (int i = 0; i < 2; i++)
                {
                    ImPlot::SetNextLineStyle(pal.sensor[i], 1.8f * uiScale);
                    const char* name = i == 0 ? "Д2" : "Д3";
                    ImPlot::PlotLine(name, xs.data(), ys[axis][i].data(), static_cast<int>(xs.size()));
                }
                ImPlot::EndPlot();
            }
        }
        EndCard();
    }
}

} // namespace ui
