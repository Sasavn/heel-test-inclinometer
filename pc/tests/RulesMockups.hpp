#pragma once
// МАКЕТЫ для предложения руководителю (только стенд снимков ui_shots, в программу не входят): как могла бы выглядеть
// «Обработка» с проверкой требований Правил РС к опыту кренования (Часть IV «Остойчивость», 1.5; Руководство по
// техническому наблюдению за постройкой судов, разд. 2, приложение 3 «Инструктивные указания по кренованию судов»).
//  1. Мастер опыта, шаг «Крен-балласт и схема перемещений» — по табл. 5.4 приложения 3;
//  2. Проверка качества — график контроля «кренящий момент – tg θ» (п. 5.5) и проверки 6.6.1–6.6.4 / 3.x;
//  3. Протокол кренования по форме приложения 3-1 (раздел «Кренование»).
// Числа — настоящие расчёты по синтетической записи tests/data/continuous (плечи подобраны под h = 0,8 м, одно
// плечо намеренно с ошибкой 12 %, чтобы показать отбраковку замера по 6.6.1).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "core/HeelData.hpp"
#include "ui/Fonts.hpp"
#include "ui/Theme.hpp"
#include "ui/Widgets.hpp"

namespace mock
{

using namespace ui;

// Замер по табл. 6.5: приращение кренящего момента и угла между соседними положениями, h_i = ΔM / (Δ·tg Δθ).
struct Meas
{
    int n = 0;
    double dM = 0, dTheta = 0, h = 0; // т·м, °, м
    bool excluded = false;
};

struct Experiment
{
    double D = 1500, P = 15;       // т
    std::vector<double> theta;     // крен положений, °
    std::vector<double> arm;       // плечо от начального, м (со знаком: + ПБ)
    std::vector<Meas> m;
    double hk = 0, sigma = 0, err = 0, errLimit = 0, tan = 0;
    int nOk = 0, excluded = -1;
    double maxPB = 0, maxLB = 0, theta0 = 0, tcS = 0;
};

inline double TAlpha(int n)
{
    static const double t[] = {5.4, 5.0, 4.8, 4.6, 4.5, 4.3, 4.2, 4.1, 4.0}; // табл. 6.6.2, n = 8…16
    if (n < 8)
        return 6.0; // вне таблицы — консервативно
    return t[std::min(n, 16) - 8];
}

// Обработка по 6.5–6.6.2 (оценка качества, если график контроля не вёлся).
inline void Evaluate(Experiment& e)
{
    e.m.clear();
    for (std::size_t i = 1; i < e.theta.size(); i++)
    {
        Meas q;
        q.n = static_cast<int>(i);
        q.dM = e.P * (e.arm[i] - e.arm[i - 1]);
        q.dTheta = e.theta[i] - e.theta[i - 1];
        q.h = q.dM / (e.D * std::tan(q.dTheta * heel::kPi / 180.0));
        e.m.push_back(q);
    }
    auto stats = [&] {
        double s = 0;
        int n = 0;
        for (const auto& q : e.m)
            if (!q.excluded)
            {
                s += q.h;
                n++;
            }
        e.hk = s / n;
        double v = 0;
        for (const auto& q : e.m)
            if (!q.excluded)
                v += (q.h - e.hk) * (q.h - e.hk);
        e.nOk = n;
        e.sigma = std::sqrt(v / (n - 1));
        e.err = TAlpha(n) * std::sqrt(v / (n * (n - 1.0)));
        e.errLimit = e.hk <= 2.0 ? 0.02 * (1 + e.hk) : 0.01 * (4 + e.hk);
    };
    stats();
    // 6.6.1: |h_i − h_k| ≤ 2σ, исключается не более одного замера (худший)
    int worst = -1;
    double wd = 0;
    for (std::size_t i = 0; i < e.m.size(); i++)
        if (std::fabs(e.m[i].h - e.hk) > 2 * e.sigma && std::fabs(e.m[i].h - e.hk) > wd)
        {
            wd = std::fabs(e.m[i].h - e.hk);
            worst = static_cast<int>(i);
        }
    if (worst >= 0)
    {
        e.m[static_cast<std::size_t>(worst)].excluded = true;
        e.excluded = worst;
        stats();
    }
    e.theta0 = e.theta.empty() ? 0 : e.theta[0];
    for (const double t : e.theta)
    {
        e.maxPB = std::max(e.maxPB, t - e.theta0);
        e.maxLB = std::min(e.maxLB, t - e.theta0);
    }
}

// Опыт из синтетической записи: положения детектором, крен — по методу обработки, плечи — под h = 0,8 м.
inline Experiment FromSynthetic(const std::filesystem::path& csv)
{
    Experiment e;
    std::vector<heel::File> files{heel::LoadFile(csv)};
    auto ms = heel::Group(files, {});
    heel::Settings s;
    auto pos = heel::FindPositions(s, files, ms[0]);
    heel::ComputePositions(s, files, ms[0], pos);
    const double truth[] = {0, 3, 6, 8, 10.5, -2.5, -6, -9.5, -12, 0.3};
    for (std::size_t i = 0; i < pos.size() && i < 10; i++)
    {
        e.theta.push_back(pos[i].Level(0));
        double l = 0.8 * e.D * std::tan(truth[i] * heel::kPi / 180.0) / e.P;
        if (i == 5)
            l *= 1.12; // ошибка в плече — для показа отбраковки по 6.6.1
        e.arm.push_back(l);
        if (std::isfinite(pos[i].est[0].tcS))
            e.tcS = pos[i].est[0].tcS;
    }
    Evaluate(e);
    return e;
}

inline void Header(const char* title, const char* sub)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + S(58)), U32(pal.panel), S(8));
    ImGui::SetCursorScreenPos(ImVec2(p.x + S(14), p.y + S(8)));
    {
        FontScope f(fontH2);
        ImGui::TextUnformatted(title);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + S(14), p.y + S(34)));
    {
        FontScope f(fontSmall);
        Muted("%s", sub);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - S(260), p.y + S(16)));
    Chip("Макет — предложение, не функция", pal.warn, pal.warnBg);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + S(66)));
}

inline void Mark(bool ok, bool known = true)
{
    if (!known)
        TextColored(pal.muted, "—");
    else
        TextColored(ok ? pal.ok : pal.err, ok ? "✓" : "✗");
}

// Шаги мастера слева (номер, название, состояние).
inline void Steps(int active, float w, float h)
{
    if (BeginCard("##steps", ImVec2(w, h)))
    {
        CardTitle("Опыт кренования", "Шаги по Инструктивным указаниям (прил. 3)");
        const char* steps[] = {"Судно и условия (разд. 3, прил. 3-1)", "Крен-балласт и схема (табл. 5.4)",
                               "Приборы (разд. 3: ≥ 2 прибора)", "Осадки до опыта (п. 5.3)", "Запись перемещений",
                               "Осадки после опыта (п. 5.3)", "Проверка качества (6.6)", "Протокол (прил. 3-1)"};
        const int state[] = {2, 1, 2, 0, 0, 0, 0, 0}; // 2 — готово, 1 — текущий, 0 — впереди
        for (int i = 0; i < 8; i++)
        {
            const bool cur = i == active;
            const ImVec4 c = cur ? pal.accent : state[i] == 2 ? pal.ok : pal.muted;
            ImGui::AlignTextToFramePadding();
            TextColored(c, cur ? "▶" : state[i] == 2 ? "✓" : "○");
            ImGui::SameLine(0, S(8));
            if (cur)
            {
                FontScope f(fontBold);
                TextColored(pal.text, "%d. %s", i + 1, steps[i]);
            }
            else
                TextColored(state[i] == 2 ? pal.text : pal.textDim, "%d. %s", i + 1, steps[i]);
        }
        ImGui::Spacing();
        SmallMuted("Запись идёт непрерывно (прошивка 1.5 или запись на ПК); положения груза отмечаются кнопкой "
                   "«Груз перемещён» или находятся по записи. Протокол собирается сам.");
    }
    EndCard();
}

// Макет 1: мастер, шаг «Крен-балласт и схема перемещений» (4 группы по 2,5 т на ±5,2 м, D = 1500 т, ожидаемая h = 0,8 м).
inline void Wizard()
{
    Header("Регистратор крена — мастер опыта кренования",
           "Шаг 2 из 8: крен-балласт и порядок перемещений по табл. 5.4 Инструктивных указаний (прил. 3 к разд. 2 "
           "Руководства по техническому наблюдению за постройкой судов)");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = S(12);
    Steps(1, S(300), avail.y);
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    const float w = avail.x - S(300) - gap;
    const double D = 1500, hExp = 0.8, mGroup = 2.5, b = 5.2;
    if (BeginCard("##ballast", ImVec2(w, S(210))))
    {
        CardTitle("Крен-балласт", "Масса каждой единицы — с точностью 1 % (п. 5.6); маркировка и освидетельствование "
                                  "перед опытом (разд. 3)");
        ImGui::BeginGroup();
        if (ImGui::BeginTable("##groups", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit,
                              ImVec2(S(450), 0)))
        {
            for (const char* c : {"Группа", "Масса, т", "Y от ДП, м", "Борт в начале", "Маркировка"})
                ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            const char* side[] = {"ПБ", "ЛБ", "ПБ", "ЛБ"};
            for (int g = 0; g < 4; g++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", g + 1);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", mGroup);
                ImGui::TableNextColumn();
                ImGui::Text("±%.2f", b);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(side[g]);
                ImGui::TableNextColumn();
                Muted("КБ-%d … КБ-%d", g * 5 + 1, g * 5 + 5);
            }
            ImGui::EndTable();
        }
        ImGui::EndGroup();
        // Схема расположения (как в прил. 3-5)
        ImGui::SameLine(0, S(30));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float sw = std::min(S(360), ImGui::GetContentRegionAvail().x - S(10)), sh = S(110);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 line = U32(pal.textDim);
        dl->AddLine(ImVec2(p.x, p.y + S(10)), ImVec2(p.x + sw * 0.82f, p.y + S(10)), line, S(1.5f));
        dl->AddLine(ImVec2(p.x, p.y + sh - S(10)), ImVec2(p.x + sw * 0.82f, p.y + sh - S(10)), line, S(1.5f));
        dl->AddLine(ImVec2(p.x + sw * 0.82f, p.y + S(10)), ImVec2(p.x + sw, p.y + sh * 0.5f), line, S(1.5f));
        dl->AddLine(ImVec2(p.x + sw * 0.82f, p.y + sh - S(10)), ImVec2(p.x + sw, p.y + sh * 0.5f), line, S(1.5f));
        dl->AddLine(ImVec2(p.x, p.y + S(10)), ImVec2(p.x, p.y + sh - S(10)), line, S(1.5f));
        for (float x = p.x; x < p.x + sw; x += S(14))
            dl->AddLine(ImVec2(x, p.y + sh * 0.5f), ImVec2(x + S(7), p.y + sh * 0.5f), U32(pal.muted), S(1));
        const struct
        {
            float x, y;
            const char* n;
        } g[] = {{0.55f, 0.72f, "1"}, {0.55f, 0.28f, "2"}, {0.15f, 0.72f, "3"}, {0.15f, 0.28f, "4"}};
        for (const auto& q : g)
        {
            const ImVec2 a(p.x + sw * q.x, p.y + sh * q.y - S(8)), c(a.x + S(56), a.y + S(16));
            dl->AddRectFilled(a, c, U32(Alpha(pal.accent, 0.25f)));
            dl->AddRect(a, c, U32(pal.accent));
            dl->AddText(ImVec2(c.x + S(6), a.y), U32(pal.text), q.n);
        }
        dl->AddText(ImVec2(p.x + sw * 0.86f, p.y + sh * 0.78f), U32(pal.muted), "ПБ");
        dl->AddText(ImVec2(p.x + sw * 0.86f, p.y + sh * 0.08f), U32(pal.muted), "ЛБ");
        ImGui::Dummy(ImVec2(sw, sh));
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    if (BeginCard("##scheme", ImVec2(w, ImGui::GetContentRegionAvail().y)))
    {
        CardTitle("Порядок перемещений (табл. 5.4, 4 группы) и ожидаемый крен",
                  "Ожидаемый крен — при D = 1500 т и ожидаемой h = 0,8 м; требование: 1°–4° на каждый борт (разд. 3 прил. 3), "
                  "начальный крен ≤ 0,5° (разд. 3), h при креновании ≥ 0,20 м (1.5.8 ч. IV)");
        // Табл. 5.4, 4 группы: ЛБ / ПБ по перемещениям 0…8
        const char* lb[] = {"2, 4", "4", "—", "1", "1, 3", "1, 2, 3", "1, 2, 3, 4", "2, 3, 4", "2, 4"};
        const char* pb[] = {"1, 3", "1, 2, 3", "1, 2, 3, 4", "2, 3, 4", "2, 4", "4", "—", "1", "1, 3"};
        const int net[] = {0, 2, 4, 2, 0, -2, -4, -2, 0}; // (число групп на ПБ − на ЛБ)
        if (ImGui::BeginTable("##moves", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            for (const char* c : {"Перемещение", "ЛБ — группы", "ПБ — группы", "Кренящий момент, т·м", "Ожидаемый крен, °",
                                  "1°–4°", "Назначение"})
                ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            for (int i = 0; i < 9; i++)
            {
                const double M = net[i] * mGroup * b;
                const double th = std::atan(M / (D * hExp)) * 180.0 / heel::kPi;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(lb[i]);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pb[i]);
                ImGui::TableNextColumn();
                ImGui::Text("%+.1f", M);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text::Angle(th, 2).c_str());
                ImGui::TableNextColumn();
                if (net[i] == 0)
                    Muted("ноль");
                else
                    Mark(std::fabs(th) >= 1.0 && std::fabs(th) <= 4.0);
                ImGui::TableNextColumn();
                Muted("%s", i == 0 ? "начальное положение" : i == 4 ? "проверка нуля" : i == 8 ? "проверка нуля (можно не делать, если прямая)" : "");
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        Button("Назад", BtnKind::Normal, ImVec2(S(120), 0));
        ImGui::SameLine(0, S(8));
        Button("Схема на 6 групп…", BtnKind::Normal, ImVec2(S(190), 0));
        ImGui::SameLine(0, S(8));
        Button("Далее: приборы", BtnKind::Primary, ImVec2(S(170), 0));
    }
    EndCard();
    ImGui::EndGroup();
}

// Макет 2: проверка качества — график контроля и проверки по пунктам Правил.
inline void Checks(const Experiment& e)
{
    Header("Регистратор крена — проверка опыта по Правилам РС",
           "График контроля «кренящий момент – tg θ» (п. 5.5 прил. 3) и оценка качества кренования (6.6 прил. 3 = "
           "1.5.9 ч. IV). Синтетическая запись; у положения 6 плечо с ошибкой 12 %");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = S(12), lw = std::floor(avail.x * 0.42f);
    if (BeginCard("##graph", ImVec2(lw, avail.y)))
    {
        CardTitle("График контроля замеров", "Точки — положения груза (накопленный момент и tg крена от "
                                             "начального); прямая — МНК; красная — замер, исключённый по 6.6.1");
        std::vector<double> xs, ys;
        double cm = 0;
        xs.push_back(0);
        ys.push_back(0);
        for (std::size_t i = 1; i < e.theta.size(); i++)
        {
            cm = e.P * e.arm[i];
            xs.push_back(cm);
            ys.push_back(std::tan((e.theta[i] - e.theta0) * heel::kPi / 180.0));
        }
        if (ImPlot::BeginPlot("##ctrl", ImVec2(-1, -1), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend))
        {
            ImPlot::SetupAxes("Кренящий момент, т·м", "tg θ", 0, 0);
            ImPlot::SetupFinish();
            // Прямая tg θ = M / (D·h_k)
            const double k = 1.0 / (e.D * e.hk);
            double lo = 0, hi = 0;
            for (const double x : xs)
            {
                lo = std::min(lo, x);
                hi = std::max(hi, x);
            }
            const double lx[2] = {lo * 1.1, hi * 1.1}, ly[2] = {lx[0] * k, lx[1] * k};
            ImPlot::SetNextLineStyle(pal.accent, S(2));
            ImPlot::PlotLine("МНК", lx, ly, 2);
            for (std::size_t i = 0; i < xs.size(); i++)
            {
                const bool bad = e.excluded >= 0 && static_cast<int>(i) == e.excluded + 1;
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, S(5), bad ? pal.err : pal.text, 0);
                ImPlot::PlotScatter("##p", &xs[i], &ys[i], 1);
                ImPlot::PlotText(std::to_string(i).c_str(), xs[i], ys[i], ImVec2(S(10), -S(10)));
            }
            ImPlot::EndPlot();
        }
    }
    EndCard();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    const float rw = avail.x - lw - gap;
    if (BeginCard("##checks", ImVec2(rw, S(390))))
    {
        CardTitle("Требования Правил");
        auto row = [&](const char* req, const char* ref, const std::string& val, int st) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (st < 0)
                Mark(false, false);
            else
                Mark(st == 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(req);
            ImGui::TableNextColumn();
            Muted("%s", ref);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(val.c_str());
        };
        if (ImGui::BeginTable("##req", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("", 0, S(18));
            ImGui::TableSetupColumn("Требование", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Пункт", 0, S(96));
            ImGui::TableSetupColumn("Опыт", 0, S(170));
            ImGui::TableHeadersRow();
            const double tc = e.maxPB, tl = -e.maxLB;
            row("Удовлетворительных замеров не менее 8", "6.6.4", Fmt("%d", e.nOk), e.nOk >= 8);
            row("|h_i − h_k| ≤ 2σ, исключено не более одного", "6.6.1",
                e.excluded >= 0 ? Fmt("исключён замер %d", e.excluded + 1) : std::string("все в пределах"), 1);
            row("Вероятная ошибка ≤ 0,02(1 + h_k)", "6.6.2", Fmt("%.3f ≤ %.3f м", e.err, e.errLimit), e.err <= e.errLimit);
            row("Ошибка ≤ ε (по Информации об остойчивости)", "6.6.3", "нужны h и l_max", -1);
            row("h при креновании ≥ 0,20 м", "1.5.8 ч. IV", Fmt("%.3f м", e.hk), e.hk >= 0.20);
            row("Крен 1°–4° на каждый борт", "разд. 3 прил. 3", Fmt("ПБ %.1f°, ЛБ %.1f°", tc, tl),
                tc >= 1 && tc <= 4 && tl >= 1 && tl <= 4);
            row("Начальный крен ≤ 0,5°", "разд. 3 прил. 3", text::Angle(e.theta0, 2), std::fabs(e.theta0) <= 0.5);
            row("Не менее 2 приборов (весков / инклинометров)", "разд. 3 прил. 3", "1 датчик (Д2)", 0);
            row("Возврат в ноль (перемещения 4 / 8)", "5.5 прил. 3", "есть (положение 10)", 1);
            row("Дифферент ≤ 1 % L, осадки до/после", "разд. 3, п. 5.3", "не введены", -1);
            row("Ветер, течение, плотность воды, глубина", "разд. 3, прил. 3-1", "не введены", -1);
            ImGui::EndTable();
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    if (BeginCard("##hi", ImVec2(rw, ImGui::GetContentRegionAvail().y)))
    {
        CardTitle("Замеры по табл. 6.5");
        if (ImGui::BeginTable("##t65", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                               ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            for (const char* c : {"№", "ΔM, т·м", "Δθ, °", "h_i, м", "h_i − h_k, м"})
                ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            for (const auto& q : e.m)
            {
                ImGui::TableNextRow();
                const ImVec4 c = q.excluded ? pal.err : pal.text;
                ImGui::TableNextColumn();
                TextColored(c, "%d%s", q.n, q.excluded ? " ✗" : "");
                ImGui::TableNextColumn();
                TextColored(c, "%+.2f", q.dM);
                ImGui::TableNextColumn();
                TextColored(c, "%s", text::Angle(q.dTheta, 3).c_str());
                ImGui::TableNextColumn();
                TextColored(c, "%.3f", q.h);
                ImGui::TableNextColumn();
                TextColored(c, "%+.3f", q.h - e.hk);
            }
            ImGui::EndTable();
        }
    }
    EndCard();
    ImGui::EndGroup();
}

// Макет 3: протокол кренования по форме прил. 3-1 (раздел «Кренование» и «Выводы»), предпросмотр перед .xlsx/.pdf.
inline void Protocol(const Experiment& e)
{
    Header("Регистратор крена — протокол кренования (предпросмотр)",
           "Форма — приложение 3-1 Инструктивных указаний; поля, которых нет в записи прибора, вводятся в мастере "
           "(шаги 1, 3, 4, 6). Сохранение — Excel и PDF");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float pw = std::min(S(860), avail.x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - pw) * 0.5f);
    const ImVec4 paper = darkTheme ? pal.card : ImVec4(1, 1, 1, 1);
    if (BeginCard("##paper", ImVec2(pw, avail.y), &paper))
    {
        {
            FontScope f(fontH2);
            const char* t = "ПРОТОКОЛ КРЕНОВАНИЯ СУДНА";
            ImGui::SetCursorPosX((pw - ImGui::CalcTextSize(t).x) * 0.5f);
            ImGui::TextUnformatted(t);
        }
        {
            FontScope f(fontSmall);
            const char* t = "Причал № 3 судостроительного завода, «09» октября 2026 г. · Кренование проведено в соответствии с прил. 3 "
                            "к разд. 2 Руководства РС";
            ImGui::SetCursorPosX((pw - ImGui::CalcTextSize(t).x) * 0.5f);
            Muted("%s", t);
        }
        ImGui::Separator();
        if (ImGui::BeginTable("##head", 4, ImGuiTableFlags_SizingStretchProp))
        {
            auto kv = [](const char* k, const char* v) {
                ImGui::TableNextColumn();
                Muted("%s", k);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v);
            };
            kv("Судно", "«Пример», строительный № 001");
            kv("Цель кренования", "после постройки");
            kv("Длина L / ширина B", "62,0 / 11,2 м");
            kv("Время", "14:05 – 14:14");
            kv("Ветер / течение", "3 м/с / 0,1 м/с");
            kv("Плотность воды", "1,003 т/м³");
            kv("Начальный крен", text::Angle(e.theta0, 2).c_str());
            kv("Период качки τ", Fmt("%.1f с (по записи)", e.tcS).c_str());
            ImGui::EndTable();
        }
        ImGui::Spacing();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Кренящие моменты и углы крена (прил. 3-1, разд. «Кренование»)");
        }
        if (ImGui::BeginTable("##prot", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                              ImVec2(0, S(300))))
        {
            for (const char* c : {"№", "Масса, т", "Плечо, м", "ΔM, т·м", "Δθ Д2, рад", "Δθ Д3, рад", "Δθ весок, рад",
                                  "Δθ расч., рад"})
                ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            for (const auto& q : e.m)
            {
                ImGui::TableNextRow();
                const double r = q.dTheta * heel::kPi / 180.0;
                ImGui::TableNextColumn();
                ImGui::Text("%d", q.n);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", e.P);
                ImGui::TableNextColumn();
                ImGui::Text("%+.3f", q.dM / e.P);
                ImGui::TableNextColumn();
                ImGui::Text("%+.2f", q.dM);
                ImGui::TableNextColumn();
                ImGui::Text("%+.5f", r);
                ImGui::TableNextColumn();
                Muted("—");
                ImGui::TableNextColumn();
                Muted("—");
                ImGui::TableNextColumn();
                ImGui::Text("%+.5f", r);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Выводы");
        }
        ImGui::Text("Метацентрическая высота при креновании h_k = %.3f м; вероятная ошибка %.3f м (допуск %.3f м).", e.hk,
                    e.err, e.errLimit);
        ImGui::Text("Замеров %d, исключён %s. Кренование выполнено %s.", e.nOk,
                    e.excluded >= 0 ? Fmt("замер %d (6.6.1)", e.excluded + 1).c_str() : "нет",
                    e.err <= e.errLimit && e.nOk >= 8 ? "качественно" : "НЕ качественно (6.6.5: h_k минус вероятная ошибка)");
        SmallMuted("Водоизмещение и координаты ЦТ судна порожнем (табл. 6.7, 7) — по гидростатике и ведомостям излишних / "
                   "недостающих грузов (прил. 3-2 … 3-4), если они введены. Подписи: руководитель кренования, участники, "
                   "инспектор РС.");
    }
    EndCard();
}

} // namespace mock
