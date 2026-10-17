// Страница «Обработка» — результаты опыта кренования по файлам замеров (по мотивам BWM427_Analyzer, Romero2207):
// папка (по умолчанию — папка «Файлов на карте») или выбранные файлы; исходные данные D, P, L, ось, окно, порог; таблица
// замеров (в расчёте ли, плечо l, установившийся крен и h по постам «нос» / «корма»); итог — среднее h и h по МНК;
// графики крена каждого поста — «Правый борт (+)» и «Левый борт (−)», пунктир — среднее; журнал; отчёт
// Отчет_Кренование.xlsx и графики График_Нос.png / График_Корма.png. Прибор не нужен.
//
// Режим «одна запись — весь опыт» (выбирается сам, если в файле есть три и больше положений груза, и переключается
// вручную): вся запись на графике, положения груза — участки установившегося крена (core/Plateaus.hpp) с заливкой и
// номерами; таблица положений (в расчёте ли, начало и конец — правятся, плечо l, крен ± СКО, Δθ от начального, h),
// добавить / разделить / слить / удалить положение, границы выбранного — перетаскиваются на графике; отчёт — лист
// «Положения» и картинка всей записи.
//
// Файлы читаются и отчёт пишется в фоне (std::async) — окно не замирает; картинки графиков рисуются своим контекстом
// ImGui без окна (Offscreen.hpp) в Tick, между кадрами. Расчёт — core/Heel.hpp (формулы) и core/HeelData.hpp.
#include "App.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <system_error>

#include <imgui.h>
#include <implot.h>

#include "../core/FileBusy.hpp"
#include "../core/HeelReport.hpp"
#include "../core/HeelProtocol.hpp"
#include "../core/Png.hpp"
#include "../core/TextUtil.hpp"
#include "Dialogs.hpp"
#include "Offscreen.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace fs = std::filesystem;

namespace
{

// Цвета замеров — как у matplotlib (tab10) в BWM427_Analyzer: тот же замер — тот же цвет на всех графиках.
const ImVec4 kMeasColors[10] = {
    {0.122f, 0.467f, 0.706f, 1.f}, {1.000f, 0.498f, 0.055f, 1.f}, {0.173f, 0.627f, 0.173f, 1.f},
    {0.839f, 0.153f, 0.157f, 1.f}, {0.580f, 0.404f, 0.741f, 1.f}, {0.549f, 0.337f, 0.294f, 1.f},
    {0.890f, 0.467f, 0.761f, 1.f}, {0.498f, 0.498f, 0.498f, 1.f}, {0.737f, 0.741f, 0.133f, 1.f},
    {0.090f, 0.745f, 0.812f, 1.f},
};

constexpr int kLogMax = 200;
constexpr int kExportW = 1000, kExportH = 820; // картинка графиков, логических точек
constexpr float kExportScale = 2.f;            // -> 2000 × 1640 пикселей

const char* kPostPresets[] = {"Нос", "Корма", "Мидель", "Нос, ЛБ", "Нос, ПБ", "Корма, ЛБ", "Корма, ПБ", "Мидель, ЛБ",
                              "Мидель, ПБ"};

std::string Deg(double v)
{
    return text::Angle(v, 3);
}

// Ширина переключателя режима (App::ProcModeSwitch): «Режим» и две кнопки.
float ModeSwitchWidth()
{
    return ImGui::CalcTextSize("Режим").x + S(8) + ImGui::CalcTextSize("Файл = замер").x +
           ImGui::CalcTextSize("Весь опыт одной записью").x + 4 * ImGui::GetStyle().FramePadding.x + S(2);
}

} // namespace

// Подсказка в строке заголовка карточки (после последнего элемента), если помещается до x = limit; иначе — короткая,
// иначе — никакой. (Общая для карточек «Обработки», в том числе AppProcDash.cpp.)
void HeaderHint(float limit, const std::string& full, const std::string& brief)
{
    FontScope f(fontSmall);
    for (const std::string* t : {&full, &brief})
    {
        const float w = ImGui::CalcTextSize(t->c_str()).x;
        if (ImGui::GetItemRectMax().x + S(14) + w <= limit)
        {
            ImGui::SameLine(0, S(14));
            ImGui::AlignTextToFramePadding();
            Muted("%s", t->c_str());
            return;
        }
    }
}

namespace
{

} // namespace

// Цвета и размеры графиков ImPlot под тему (forExport — свой контекст картинки: размеры по её масштабу). Общая для
// графиков «Обработки» (и AppRules.cpp).
void ProcPlotStyle(bool forExport)
{
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
    ps.Colors[ImPlotCol_InlayText] = pal.muted;
    ps.PlotPadding = ImVec2(S(10), S(10));
    ps.LabelPadding = ImVec2(S(5), S(3));
    ps.LegendPadding = ImVec2(S(8), S(6));
    ps.LineWeight = 1.6f * uiScale;
    if (forExport) // свой контекст ImPlot: размеры по масштабу картинки
    {
        ps.MajorTickLen = ImVec2(S(10), S(10));
        ps.MinorTickLen = ImVec2(S(5), S(5));
        ps.MajorTickSize = ps.MinorTickSize = ImVec2(S(1), S(1));
        ps.MajorGridSize = ps.MinorGridSize = ImVec2(S(1), S(1));
        ps.PlotBorderSize = S(1);
        ps.LegendInnerPadding = ImVec2(S(5), S(5));
        ps.LegendSpacing = ImVec2(S(5), S(2));
    }
}

namespace
{

const char* kArmTip =
    "Плечо переноса груза от начального положения — со знаком: «+» — к правому борту, «−» — к левому\n"
    "(табл. 6.5 Правил РС: h_i по приращениям момента и крена между соседними положениями).\n"
    "Если все плечи положительные (ввод модулем, как раньше) — борт берётся по знаку крена, в журнале — пометка.";

// Строка заголовков таблицы, как TableHeadersRow, но у столбца hintCol — подсказка при наведении.
void HeadersRowHint(int hintCol, const char* tip)
{
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    const int n = ImGui::TableGetColumnCount();
    for (int c = 0; c < n; c++)
    {
        if (!ImGui::TableSetColumnIndex(c))
            continue;
        ImGui::PushID(c);
        ImGui::TableHeader(ImGui::TableGetColumnName(c));
        ImGui::PopID();
        if (c == hintCol && ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tip);
            ImGui::EndTooltip();
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Данные: чтение, группировка, расчёт
// ---------------------------------------------------------------------------------------------------------------

heel::Settings App::ProcSettings() const
{
    heel::Settings s;
    s.D = settings_.heelD;
    s.P = settings_.heelP;
    s.L = settings_.heelL;
    s.B = settings_.heelB;
    s.armFrom = settings_.heelB > 0 ? settings_.armFrom : 0;
    s.windowS = settings_.heelWinS;
    s.thresholdDeg = settings_.heelThr;
    s.axis = settings_.heelAxis;
    s.minPlateauS = settings_.heelPlateauS;
    s.minStepDeg = settings_.heelStep;
    s.units.model = settings_.heelSmall;
    for (const auto& [key, post] : proc_.postOf)
        if (post >= 0 && post < heel::kMaxPosts)
            s.postNames[static_cast<std::size_t>(post)] = SensorTitle(key);
    s.filter.method = static_cast<filt::Method>(std::clamp(settings_.fltMethod, 0, static_cast<int>(filt::Method::Count) - 1));
    if (s.filter.method == filt::Method::User)
    {
        // Свой метод: по имени среди сохранённых; не найден или с ошибкой — Калман
        const_cast<App*>(this)->ProcLoadMethods();
        for (const auto& m : proc_.methods)
            if (m.name == settings_.fltUser)
                s.filter.custom = ProcUserCompiled(m, nullptr);
        if (!s.filter.custom)
            s.filter.method = filt::Method::Kalman;
    }
    s.filter.medianN = settings_.fltMedianN;
    s.filter.fcHz = settings_.fltFc;
    s.filter.tcS = settings_.fltTc;
    s.filter.sgWindowS = settings_.fltSgWin;
    s.filter.window = static_cast<filt::Window>(std::clamp(settings_.fltWindow, 0, 3));
    s.rsH = settings_.rsH > 0 ? settings_.rsH : heel::kNaN;
    s.rsLmax = settings_.rsLmax > 0 ? settings_.rsLmax : heel::kNaN;
    s.rsD0 = settings_.rsD0 > 0 ? settings_.rsD0 : heel::kNaN;
    s.rsSpread = settings_.rsSpread;
    s.rsMain = settings_.rsMain;
    s.rsMainGroups = settings_.rsMainGroups;
    s.rsInstrument = settings_.rsInstrument;
    s.rsDh = settings_.rsDh;
    s.rsDworst = settings_.rsDworst > 0 ? settings_.rsDworst : heel::kNaN;
    s.rsGroupMode = settings_.rsGroupMode;
    s.rsGroups = ProcGroups();
    s.rsMoves = proc_.rsMoves;
    return s;
}

// Плечи «от борта» (переключатель над таблицей и ширина B задана)
bool App::ProcArmSide() const
{
    return settings_.armFrom != 0 && settings_.heelB > 0;
}

std::string App::ProcArmHeader() const
{
    const char* u = settings_.heelSmall ? "мм" : "м";
    if (ProcArmSide())
        return Fmt("От %s x, %s → l от ДП (?)", heel::ArmFromName(settings_.armFrom), u);
    return Fmt("Плечо l, %s (?)", u);
}

// Поле плеча в таблице. arm — плечо от ДП (или от начального, как раньше); при «от борта» в поле — расстояние от
// борта до центра груза, справа серым — плечо от ДП. hideValue — текст поля не показывать («нач.»). true — изменено.
bool App::ProcArmField(const char* id, double& arm, const ImVec4& color, bool hideValue)
{
    const int dig = std::max(3, settings_.digArm);
    if (!ProcArmSide())
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool ch = ImGui::InputDouble(id, &arm, 0.0, 0.0, NumFmt(arm, dig).c_str());
        ImGui::PopStyleColor();
        return ch;
    }
    const int from = settings_.armFrom;
    const double B = settings_.heelB;
    double x = heel::SideFromArm(arm, from, B);
    const int digX = settings_.heelSmall ? 1 : 3;
    const float cw = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::floor(cw * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool ch = ImGui::InputDouble(id, &x, 0.0, 0.0, NumFmt(x, digX).c_str());
    ImGui::PopStyleColor();
    if (ch)
        arm = heel::ArmFromSide(x, from, B);
    if (!hideValue)
    {
        // плечо от ДП — серым справа от поля (рисунком: последним элементом остаётся поле — его подсказки)
        const ImVec2 r1 = ImGui::GetItemRectMax(), r0 = ImGui::GetItemRectMin();
        const std::string t = Fmt("%+.*f", settings_.heelSmall ? 1 : 3, arm);
        ImGui::GetWindowDrawList()->AddText(ImVec2(r1.x + S(6), r0.y + ImGui::GetStyle().FramePadding.y), U32(pal.muted),
                                            t.c_str());
    }
    return ch;
}

// «Плечи от: ДП / ЛБ / ПрБ» и ширина B — над таблицей положений / замеров
void App::ProcArmFromSwitch()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Плечи от");
    Hint("Как вводить плечи в таблице. «ДП» — плечо груза от диаметральной плоскости (+ к правому борту). «ЛБ» / "
         "«ПрБ» — расстояние от выбранного борта до центра груза, как его мерят; плечо от ДП программа считает по "
         "ширине B: от ПрБ l = B/2 − x, от ЛБ l = x − B/2 (серым — рядом). Переключение только меняет вид ввода — "
         "введённые плечи не теряются. В режиме «Группы» плечи перемещений — разности, способ на них не влияет.");
    ImGui::SameLine(0, S(8));
    static const char* labels[] = {"ДП", "ЛБ", "ПрБ"};
    static const int values[] = {0, 1, 2};
    if (Segmented("armfrom", &settings_.armFrom, labels, values, 3))
        SaveSettingsSoon();
    if (settings_.armFrom != 0)
    {
        ImGui::SameLine(0, S(14));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(settings_.heelSmall ? "B, мм" : "B, м");
        ImGui::SameLine(0, S(6));
        ImGui::SetNextItemWidth(S(110));
        if (ImGui::InputDouble("##armB", &settings_.heelB, 0.0, 0.0, NumFmt(settings_.heelB, settings_.heelSmall ? 1 : 3).c_str()))
        {
            settings_.heelB = std::clamp(settings_.heelB, 0.0, 1e7);
            proc_.dirty = true;
            SaveSettingsSoon();
        }
        if (!(settings_.heelB > 0))
        {
            ImGui::SameLine(0, S(8));
            ImGui::AlignTextToFramePadding();
            TextColored(pal.warn, "задайте ширину B — пока плечи вводятся от ДП");
        }
    }
}

// Формат поля: digits знаков, но ненулевое число не показывается нулём (знаков — сколько нужно, до 6).
std::string App::NumFmt(double v, int digits) const
{
    int d = std::clamp(digits, 0, 6);
    const double a = std::fabs(v);
    if (a > 0 && a < 1.0)
        d = std::max(d, std::min(6, static_cast<int>(std::ceil(-std::log10(a))) + 1));
    return Fmt("%%.%df", d);
}

// Большое / малое судно: D и P — т или кг, плечи — м или мм. Введённые числа переводятся в новые единицы; значения по
// умолчанию одного типа судна заменяются значениями по умолчанию другого (1500 т не превращаются в 1 500 000 кг).
void App::ProcSetSmallShip(bool model)
{
    if (model == settings_.heelSmall)
        return;
    const double k = model ? 1000.0 : 0.001; // т -> кг и м -> мм (или обратно)
    const bool bigDefaults = settings_.heelD == 1500.0 && settings_.heelP == 15.0 && settings_.heelL == 4.5;
    const bool smallDefaults = settings_.heelD == 50.0 && settings_.heelP == 1.0 && settings_.heelL == 200.0;
    if (model && bigDefaults)
    {
        settings_.heelD = 50.0;
        settings_.heelP = 1.0;
        settings_.heelL = 200.0;
    }
    else if (!model && smallDefaults)
    {
        settings_.heelD = 1500.0;
        settings_.heelP = 15.0;
        settings_.heelL = 4.5;
    }
    else
    {
        settings_.heelD *= k;
        settings_.heelP *= k;
        settings_.heelL *= k;
    }
    settings_.heelB *= k;
    for (auto& [n, a] : proc_.arm)
        a *= k;
    for (auto& p : proc_.pos)
        if (std::isfinite(p.arm))
            p.arm *= k;
    settings_.heelSmall = model;
    ProcLog(model ? "Малое судно (модель): D и P — в кг, плечи — в мм" : "Большое судно: D и P — в т, плечи — в м");
    proc_.dirty = true;
    SaveSettingsSoon();
}

void App::ProcessSetMode(int mode)
{
    if ((mode == 1 ? 1 : 0) != proc_.mode)
        proc_.rsManual.clear(); // номера замеров табл. 6.5 у режимов разные
    proc_.mode = mode == 1 ? 1 : 0;
    proc_.modeUser = true;
    ProcChooseMode(false);
}

void App::ProcessSetPositionArm(int index, double arm)
{
    if (index >= 0 && index < static_cast<int>(proc_.pos.size()))
    {
        proc_.pos[static_cast<std::size_t>(index)].arm = arm;
        proc_.dirty = true;
    }
}

// Режим и запись. fromLoad — после чтения файлов: без ручного выбора режим — по содержимому (есть запись с тремя и
// больше положениями груза — «одна запись — весь опыт»).
void App::ProcChooseMode(bool fromLoad)
{
    if (!proc_.files)
        return;
    const int suggested = heel::SuggestRecord(*proc_.files, proc_.meas);
    if (fromLoad && !proc_.modeUser)
        proc_.mode = suggested >= 0 ? 1 : 0;
    if (proc_.mode == 1)
    {
        const bool keep = !fromLoad && proc_.rec >= 0 && proc_.rec < static_cast<int>(proc_.meas.size());
        if (!keep)
            proc_.rec = suggested >= 0 ? suggested : heel::LongestRecord(*proc_.files, proc_.meas);
        if (!keep || proc_.pos.empty())
            ProcFindPositions();
    }
    proc_.dirty = true;
}

std::filesystem::path App::EditsDir() const
{
    if (!opt_.editsDir.empty())
        return text::PathFromUtf8(opt_.editsDir);
    if (opt_.useSettingsFile)
        return PcSettings::Dir() / L"edits";
    return {};
}

std::string App::EditsKeyNow() const
{
    if (!ProcCont() || !proc_.files)
        return {};
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    const int mp = heel::MainPost(m);
    return mp < 0 ? std::string() : heel::EditsKey((*proc_.files)[static_cast<std::size_t>(m.post[mp].file)], settings_.heelAxis);
}

void App::ProcFindPositions(bool restore)
{
    proc_.pos.clear();
    proc_.sel = -1;
    proc_.posEdited = false;
    proc_.undoValid = false;
    proc_.rsManual.clear();
    proc_.editsSaveAt = 0;
    if (!ProcCont() || !proc_.files)
        return;
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    const heel::Settings s = ProcSettings();
    proc_.pos = heel::FindPositions(s, *proc_.files, m);
    const int mp = heel::MainPost(m);
    if (mp >= 0)
    {
        const auto& f = (*proc_.files)[static_cast<std::size_t>(m.post[mp].file)];
        (void) f;
        ProcLog(ProcRecordText());
        // Ручные правки этой записи (границы, флажки, плечи) — из прошлой обработки
        std::vector<heel::Position> edited;
        const auto dir = EditsDir();
        if (restore && !dir.empty() && heel::LoadPositionEdits(dir, EditsKeyNow(), edited, s.units))
        {
            proc_.pos = std::move(edited);
            proc_.posEdited = true;
            ProcLog(Fmt("Восстановлены ручные правки положений (%d) — «Сбросить правки» вернёт найденные",
                        static_cast<int>(proc_.pos.size())));
        }
    }
    proc_.dirty = true;
}

void App::ProcSortPositions()
{
    for (auto& p : proc_.pos)
        if (p.t0 > p.t1)
            std::swap(p.t0, p.t1);
    std::stable_sort(proc_.pos.begin(), proc_.pos.end(),
                     [](const heel::Position& a, const heel::Position& b) { return a.t0 < b.t0; });
}

int App::ProcShownPost() const
{
    if (!ProcCont())
        return proc_.show;
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    return m.post[proc_.show].file >= 0 ? proc_.show : std::max(0, heel::MainPost(m));
}

void App::ProcLog(const std::string& s, const std::string& tip)
{
    proc_.log.push_back({s, tip});
    if (proc_.log.size() > static_cast<std::size_t>(kLogMax))
        proc_.log.erase(proc_.log.begin());
    proc_.logScroll = true;
}

void App::ProcessOpen(const std::string& dir)
{
    std::snprintf(proc_.dir, sizeof(proc_.dir), "%s", dir.c_str());
    proc_.picked.clear();
    proc_.opened = true;
    ProcLoad();
}

bool App::ProcessBusy() const
{
    return proc_.load.valid() || proc_.save.valid() || proc_.saveRequested;
}

void App::ProcessSetArm(int number, double arm)
{
    proc_.arm[number] = arm;
    proc_.dirty = true;
}

void App::ProcessSaveReport()
{
    if (!ProcessBusy() && proc_.files)
        proc_.saveRequested = true;
}

void App::ProcLoad()
{
    if (proc_.load.valid())
        return;
    std::vector<fs::path> paths;
    std::string folder;
    if (!proc_.picked.empty())
    {
        for (const auto& p : proc_.picked)
            paths.push_back(text::PathFromUtf8(p));
        folder = text::PathToUtf8(paths.front().parent_path());
        ProcLog(Fmt("Выбрано файлов: %d (папка %s)", static_cast<int>(paths.size()), Shown(folder).c_str()));
    }
    else
    {
        folder = proc_.dir;
        if (!proc_.projPending && !proc_.askSkip && folder != proc_.outDir && ProcAskProject(folder))
            return; // в папке есть проект: сначала вопрос — открыть его или начать новый
        proc_.askSkip = false;
        settings_.procDir = folder == files_.dir ? "" : folder; // запомнить, если не папка «Файлов на карте»
        std::string err;
        bool sub = false;
        paths = heel::ListCsv(text::PathFromUtf8(folder), &err, &sub);
        if (!err.empty())
        {
            ProcLog("Папка «" + Shown(folder) + "»: " + err);
            paths.clear();
        }
        else if (paths.empty())
            ProcLog("В папке «" + Shown(folder) + "» и её подпапках нет файлов *.CSV и *.TXT");
        else if (sub)
            ProcLog(Fmt("В самой папке файлов нет — взяты из подпапок: %d", static_cast<int>(paths.size())));
    }
    proc_.loadDone = std::make_shared<std::atomic<int>>(0);
    proc_.loadTotal = static_cast<int>(paths.size());
    auto done = proc_.loadDone;
    const heel::Settings s = ProcSettings();
    proc_.load = std::async(std::launch::async,
                            [paths, folder, done, s] { return heel::LoadAll(paths, folder, done.get(), &s); });
}

void App::ProcRegroup(bool quiet)
{
    proc_.meas.clear();
    if (!proc_.files)
        return;
    std::set<int> keys;
    for (const auto& f : *proc_.files)
        if (f.Measurement() && f.sensor >= 0)
            keys.insert(f.sensor);
    proc_.keys.assign(keys.begin(), keys.end());
    ProcAssignPosts();
    std::vector<std::string> notes;
    proc_.meas = heel::Group(*proc_.files, proc_.postOf, &notes);
    if (!quiet)
    {
        // Непонятые файлы (чужие книги и CSV) — одной строкой, список — в подсказке
        std::string foreign;
        int nForeign = 0;
        for (const auto& n : notes)
            if (n.rfind("не понял формат: ", 0) == 0)
            {
                foreign += (foreign.empty() ? "" : "\n") + n.substr(std::string("не понял формат: ").size());
                nForeign++;
            }
            else
                ProcLog(n);
        if (nForeign)
            ProcLog(Fmt("Пропущено %d %s не того формата (нет столбцов углов) — наведите, чтобы увидеть список", nForeign,
                        text::Plural(static_cast<std::uint64_t>(nForeign), "файл", "файла", "файлов")),
                    foreign);
    }
    proc_.dirty = true;
}

void App::ProcRecompute()
{
    proc_.dirty = false;
    proc_.projCheck = true;
    if (!proc_.files)
    {
        proc_.sum = heel::Summary{};
        proc_.rs = heel::RsCase{};
        return;
    }
    for (auto& m : proc_.meas)
    {
        const auto off = proc_.off.find(m.number);
        m.use = off == proc_.off.end() || !off->second;
        const auto a = proc_.arm.find(m.number);
        m.arm = a != proc_.arm.end() ? a->second : settings_.heelL;
    }
    proc_.groupArm.clear();
    if (settings_.rsGroupMode)
    {
        // Группы: плечо замера — эквивалентное (накопленный момент / P), первый учтённый — начальное положение
        std::size_t n = 0;
        for (const auto& m : proc_.meas)
            n += m.use;
        const auto arms = heel::GroupArms(ProcSettings(), n);
        std::size_t k = 0;
        for (auto& m : proc_.meas)
            if (m.use)
                m.arm = arms[k++];
    }
    proc_.sum = heel::Compute(ProcSettings(), *proc_.files, proc_.meas);
    for (int p = 0; p < heel::kMaxPosts; p++)
    {
        proc_.procT[p].clear();
        proc_.procY[p].clear();
    }
    if (ProcCont())
    {
        const heel::Settings s = ProcSettings();
        const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
        if (settings_.rsGroupMode)
        {
            // Группы: расчёт «от начального» — с эквивалентными плечами (накопленный момент / P), свои плечи не трогаются
            auto calc = proc_.pos;
            std::size_t n = 0;
            for (const auto& q : calc)
                n += q.use;
            const auto arms = heel::GroupArms(s, n);
            proc_.groupArm.assign(calc.size(), heel::kNaN);
            for (std::size_t i = 0, k = 0; i < calc.size(); i++)
                if (calc[i].use)
                {
                    calc[i].arm = arms[k++];
                    proc_.groupArm[i] = calc[i].arm;
                }
            proc_.posSum = heel::ComputePositions(s, *proc_.files, m, calc);
            for (std::size_t i = 0; i < calc.size(); i++)
            {
                const double own = proc_.pos[i].arm;
                proc_.pos[i] = calc[i];
                proc_.pos[i].arm = own;
            }
        }
        else
            proc_.posSum = heel::ComputePositions(s, *proc_.files, m, proc_.pos);
        // Обработанный сигнал всей записи — для графика (Медиана + EMA и Савицкий–Голай; у остальных — по положениям)
        for (int p = 0; p < heel::kMaxPosts; p++)
            if (m.post[p].file >= 0)
            {
                std::vector<double> v;
                heel::Series((*proc_.files)[static_cast<std::size_t>(m.post[p].file)].log, s.axis, proc_.procT[p], v);
                proc_.procY[p] = filt::ProcessSeries(proc_.procT[p], v, s.filter);
                if (proc_.procY[p].empty())
                    proc_.procT[p].clear();
            }
        if (proc_.posEdited)
            proc_.editsSaveAt = link_->NowMs() + 1000;
        proc_.rs = heel::RsFromPositions(s, *proc_.files, m, proc_.pos, proc_.rsManual, proc_.posSum.h);
    }
    else
    {
        proc_.posSum = heel::Summary{};
        proc_.rs = heel::RsFromMeasurements(ProcSettings(), *proc_.files, proc_.meas, proc_.rsManual, proc_.sum.h);
    }
    // Плечи без знака (старый ввод модулем): знак переноса — по крену; сказать один раз
    if (proc_.rs.legacy && proc_.rs.r.m.size() >= 2 && !proc_.rsLegacyLogged)
    {
        proc_.rsLegacyLogged = true;
        ProcLog("Плечи введены без знака — знак переноса взят по крену (как раньше). Для Правил РС плечо вводится со "
                "знаком: + к правому борту, − к левому",
                "Табл. 6.5 считает h_i по приращениям момента и крена между соседними положениями, поэтому ей нужен "
                "знак плеча. Пока все плечи положительные, программа ставит знак по крену положения относительно "
                "начального; |Δθ| не больше порога — груз считается на месте (плечо 0).");
    }
}

void App::ProcessSetRsManual(int number, int state)
{
    if (state < 0)
        proc_.rsManual.erase(number);
    else
        proc_.rsManual[number] = state ? 1 : 0;
    proc_.dirty = true;
}

void App::TickProcess()
{
    using namespace std::chrono_literals;
    // Чтение файлов закончено
    if (proc_.load.valid() && proc_.load.wait_for(0s) == std::future_status::ready)
    {
        heel::Loaded r = proc_.load.get();
        const std::shared_ptr<proj::Project> pend = std::move(proc_.projPending);
        proc_.projPending.reset();
        if (r.folder != proc_.outDir || pend || proc_.freshStart) // другой опыт: плечи и снятые замеры — заново (или из проекта)
        {
            proc_.arm.clear();
            proc_.off.clear();
            proc_.rsManual.clear();
            proc_.rsLegacyLogged = false;
            proc_.rsMoves.clear();
            proc_.projSaveAt = 0;
            proc_.projAutoFailed = false;
            if (!pend)
                proc_.projectPath.clear(); // новый опыт без проекта
        }
        if (pend)
        {
            proc_.arm = pend->arms;
            proc_.off = pend->excluded;
            proc_.rsMoves = pend->rsMoves;
            proc_.mode = pend->mode;
            proc_.modeUser = pend->modeManual;
        }
        proc_.outDir = r.folder;
        proc_.filesRaw = std::make_shared<const std::vector<heel::File>>(std::move(r.files));
        ProcApplyInvert();
        ProcRegroup();
        ProcRecompute();
        // Итог чтения: файлы, замеры, форматы, посты
        int used = 0;
        std::set<std::string> formats;
        bool byTime = false;
        for (const auto& m : proc_.meas)
            for (const auto& pd : m.post)
                if (pd.file >= 0)
                {
                    used++;
                    const auto& f = (*proc_.files)[static_cast<std::size_t>(pd.file)];
                    formats.insert(csvlog::FormatName(f.log.format));
                    byTime |= f.log.time != csvlog::TimeSource::Ms;
                    if (f.log.skipped)
                        ProcLog(Fmt("%s: пропущено строк %d (оборваны?)", f.name.c_str(), f.log.skipped));
                    // Необычный вид файла (без шапки, после Excel, UTF-16, нули в хвосте) — сказать, как понят
                    if (f.log.headerless || f.log.utf16 || f.log.nulBytes || f.log.fromXlsx || f.log.format == csvlog::Format::FwOld ||
                        f.log.format == csvlog::Format::Analyzer || f.log.format == csvlog::Format::Angles || (f.log.sep != ';' && f.log.decimalComma) ||
                        (f.log.sep == ';' && f.log.format != csvlog::Format::Fw14))
                        ProcLog(Fmt("%s: %s (%s), %d строк, %.0f с", f.name.c_str(), csvlog::FormatName(f.log.format),
                                    csvlog::Details(f.log).c_str(), static_cast<int>(f.log.rows.size()), f.log.Duration()));
                }
        if (!proc_.meas.empty())
            ProcLog(ProcSummaryText());
        else if (!proc_.files->empty())
            ProcLog("Ни один файл не понят как файл замера (что не так — выше)");
        ProcChooseMode(true); // режим по содержимому: одна запись — весь опыт или файл = замер
        if (proc_.freshStart)
        {
            // «Создать новый проект кренования»: чистые плечи, без прежних правок положений этой записи
            proc_.freshStart = false;
            proc_.projectPath.clear();
            if (ProcCont())
                ProcFindPositions(false);
        }
        if (pend)
            ProcApplyProjectExperiment(*pend);
        ProcRecompute();
        proc_.projSnap = ProcProjectSnapshot(); // открыли — не изменение: автосохранение только после правок
        proc_.projCheck = false;
    }

    // Картинки графиков — здесь, между кадрами (свой контекст ImGui), книга и запись файлов — в фоне
    if (proc_.saveRequested && !proc_.load.valid() && proc_.files)
    {
        proc_.saveRequested = false;
        if (proc_.dirty)
            ProcRecompute();
        std::shared_ptr<offscreen::Frame> frames[heel::kMaxPosts], posFrames[heel::kMaxPosts];
        std::string plotName[heel::kMaxPosts], posName[heel::kMaxPosts];
        const bool cont = ProcCont();
        for (int post = 0; post < heel::kMaxPosts; post++)
        {
            plotName[post] = heel::PlotFileName("График", ProcSettings().PostName(post));
            posName[post] = heel::PlotFileName("Положения", ProcSettings().PostName(post));
            bool any = false;
            if (cont)
                any = proc_.meas[static_cast<std::size_t>(proc_.rec)].post[post].file >= 0;
            else
                for (const auto& m : proc_.meas)
                    any |= m.use && m.post[post].file >= 0;
            if (any)
                frames[post] = offscreen::Capture(kExportW, kExportH, kExportScale, [this, post, cont](float w, float h) {
                    if (cont)
                        ProcExportSeries(post, w, h);
                    else
                        ProcExportPage(post, w, h);
                });
            // Положения крупно — сеткой по два в ряд
            if (any && cont && !proc_.pos.empty())
            {
                const int rowsN = (static_cast<int>(proc_.pos.size()) + 1) / 2;
                posFrames[post] = offscreen::Capture(kExportW, 170 + rowsN * 300, kExportScale,
                                                     [this, post](float w, float h) { ProcExportPositions(post, w, h); });
            }
        }
        // График контроля (Правила РС, п. 5.5) — по основному посту
        std::shared_ptr<offscreen::Frame> ctrlFrame;
        if (!proc_.rs.r.m.empty())
            ctrlFrame = offscreen::Capture(kExportW, 720, kExportScale,
                                           [this](float w, float h) { ProcExportControl(w, h); });
        const heel::RsCase rsCase = proc_.rs;
        heel::ReportInfo info;
        info.folder = Shown(proc_.outDir);
        info.program = WindowTitle();
        info.when = text::LocalTimeString(std::time(nullptr));
        const auto files = proc_.files;
        const auto meas = proc_.meas;
        const auto sum = cont ? proc_.posSum : proc_.sum;
        const auto pos = proc_.pos;
        const int rec = proc_.rec;
        const auto s = ProcSettings();
        const fs::path dir = text::PathFromUtf8(proc_.outDir);
        // Полный отчёт: лист «Расчёт по шагам» — из тех же чисел, что окна «Подробнее»
        const bool full = settings_.reportFull;
        const steps::Steps stepsAll = full ? ProcSteps() : steps::Steps{};
        proc_.save = std::async(std::launch::async, [=]() {
            std::vector<std::string> out;
            std::error_code ec;
            fs::create_directories(dir, ec);
            std::string warn, err;
            // Книга или картинка открыта в другой программе (Excel держит книгу) — весь комплект под следующим
            // свободным номером: «Отчет_Кренование (2).xlsx», «График_Нос (2).png», …
            std::vector<std::string> names = {heel::kReportName};
            if (ctrlFrame)
                names.push_back(heel::kControlPlotName);
            for (int post = 0; post < heel::kMaxPosts; post++)
            {
                if (frames[post])
                    names.push_back(plotName[post]);
                if (posFrames[post])
                    names.push_back(posName[post]);
            }
            const int n = std::max(1, filebusy::FreeSuffix(dir, names));
            const std::string reportName = filebusy::SuffixName(heel::kReportName, n);
            if (n > 1)
            {
                std::string busy;
                for (const auto& nm : names)
                    if (filebusy::Busy(dir / text::PathFromUtf8(nm)))
                        busy += (busy.empty() ? "" : ", ") + nm;
                out.push_back("⚠ Отчёт открыт в другой программе (" + busy + ") — сохранено как " + reportName +
                              (names.size() > 1 ? " и картинки с тем же номером (" + std::to_string(n) + ")" : std::string()));
            }
            // Картинки: PNG-файлами рядом и в книгу (лист «Протокол» — контроль и запись основного поста, «Графики» — все)
            std::vector<heel::ReportImage> images;
            auto picture = [&](const std::shared_ptr<offscreen::Frame>& fr, const std::string& file, const std::string& caption,
                               bool control, bool series, const char* what) {
                if (!fr)
                    return;
                const auto rgb = offscreen::Rasterize(*fr);
                heel::ReportImage im;
                im.caption = caption;
                im.png = png::EncodeRgb(fr->w, fr->h, rgb);
                im.w = fr->w;
                im.h = fr->h;
                im.control = control;
                im.series = series;
                bool ok = !im.png.empty();
                if (ok)
                {
                    std::ofstream f(dir / text::PathFromUtf8(file), std::ios::binary | std::ios::trunc);
                    f.write(reinterpret_cast<const char*>(im.png.data()), static_cast<std::streamsize>(im.png.size()));
                    ok = static_cast<bool>(f);
                }
                if (ok)
                    out.push_back(Fmt("✓ %s сохранён: %s (%d × %d)", what, file.c_str(), fr->w, fr->h));
                else
                    out.push_back("✗ " + file + " не сохранён");
                if (!im.png.empty())
                    images.push_back(std::move(im));
            };
            picture(ctrlFrame, filebusy::SuffixName(heel::kControlPlotName, n),
                    "График контроля замеров: кренящий момент – tg θ (п. 5.5 прил. 3)", true, false, "График контроля");
            for (int post = 0; post < heel::kMaxPosts; post++)
            {
                const std::string pn = s.PostName(post);
                picture(frames[post], filebusy::SuffixName(plotName[post], n),
                        (cont ? "Запись крена с положениями груза — пост «" : "Крен по замерам — пост «") + pn + "»", false,
                        true, "График");
                picture(posFrames[post], filebusy::SuffixName(posName[post], n), "Положения крупно — пост «" + pn + "»",
                        false, false, "Положения");
            }
            const auto sheets = heel::BuildBook(s, *files, meas, cont, rec, pos, sum, rsCase, info, images, &warn,
                                                full ? &stepsAll : nullptr);
            const std::size_t at = n > 1 ? 1 : 0; // после строки «⚠ … сохранено как …»
            if (xlsx::Write(dir / text::PathFromUtf8(reportName), sheets, &err))
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), "✓ Отчёт сохранён: " + reportName);
            else
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), "✗ " + reportName + " не сохранён: " + err);
            if (!warn.empty())
                out.push_back(warn);
            return out;
        });
    }
    if (proc_.save.valid() && proc_.save.wait_for(0s) == std::future_status::ready)
    {
        const auto lines = proc_.save.get();
        bool ok = true;
        std::string failed;
        for (const auto& l : lines)
        {
            ProcLog(l);
            if (l.rfind("✗", 0) == 0)
            {
                ok = false;
                failed += (failed.empty() ? "" : "\n") + l;
            }
        }
        // Не молчать: ошибка и «сохранено под другим именем» — заметным сообщением с «Открыть папку» (журнал — на
        // вкладке, его не видно)
        if (!ok)
            Notify("Отчёт сохранён не полностью:\n" + failed, 2, 12000, proc_.outDir);
        else if (!lines.empty() && lines.front().rfind("⚠", 0) == 0)
        {
            const std::string& l = lines.front();
            const std::size_t k = l.find("сохранено как ");
            Notify("Отчёт открыт в другой программе — " + (k == std::string::npos ? l : l.substr(k)), 0, 12000, proc_.outDir);
        }
        else
            Notify("Отчёт и графики сохранены в папку " + Shown(proc_.outDir), 1, 6000, proc_.outDir);
    }
    if (proc_.dirty && !proc_.load.valid())
        ProcRecompute();
    TickProject();
    // Ручные правки положений — в файл
    if (proc_.editsSaveAt && link_->NowMs() >= proc_.editsSaveAt)
    {
        proc_.editsSaveAt = 0;
        const auto dir = EditsDir();
        const std::string key = EditsKeyNow();
        if (!dir.empty() && !key.empty() && proc_.posEdited)
            heel::SavePositionEdits(dir, key, proc_.pos, ProcSettings().units);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Страница
// ---------------------------------------------------------------------------------------------------------------


void App::ProcParams(float w)
{
    if (BeginCard("##proc_in", ImVec2(w, 0)))
    {
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const bool loading = proc_.load.valid();

        // Строка 1: откуда файлы
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Папка");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Читаются все *.CSV и *.TXT папки: формат узнаётся по содержимому (все прошивки, файлы\n"
                              "после Excel). Имена «ГГГГ-ММ-ДД_MNNN_Dk.CSV», «M_NNN_k.CSV», «M_NNN.CSV» — номер замера\n"
                              "и датчик; записи на ПК «…_PC_Dk.CSV» и файлы с другими именами — каждый свой замер.\n"
                              "Сюда же сохраняется отчёт.");
        ImGui::SameLine(0, S(12));
        const float btnW = S(130) + S(8); // «Записи ▼»
        ImGui::SetNextItemWidth(std::max(S(120), right - ImGui::GetCursorScreenPos().x - btnW));
        if (PathInput("##pdir", proc_.dir, sizeof(proc_.dir), ImGuiInputTextFlags_EnterReturnsTrue) && !loading)
        {
            proc_.picked.clear();
            ProcLoad();
        }
        if (ImGui::IsItemHovered() && !proc_.picked.empty())
            ImGui::SetTooltip("Выбраны отдельные файлы (%d) — «Перечитать» читает их; Enter в поле — вся папка",
                              static_cast<int>(proc_.picked.size()));
        ImGui::SameLine(0, S(8));
        // «Записи ▼»: выбрать папку, выбрать файлы, перечитать — одной нейтральной кнопкой
        if (Button(loading ? "Чтение…" : "Записи ▼", BtnKind::Normal, ImVec2(S(130), 0), loading, "Идёт чтение файлов") ||
            proc_.recMenu > 0)
        {
            ImGui::OpenPopup("##recmenu");
            proc_.recMenu = 0;
        }
        if (ImGui::IsItemHovered() && !loading)
            ImGui::SetTooltip("Откуда записи опыта: папка, отдельные файлы, перечитать");
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + S(4)), ImGuiCond_Appearing,
                                ImVec2(1, 0));
        if (ImGui::BeginPopup("##recmenu"))
        {
            if (ImGui::MenuItem("Выбрать папку…"))
            {
                std::string d = proc_.dir;
                if (dialogs::PickFolder(d))
                {
                    std::snprintf(proc_.dir, sizeof(proc_.dir), "%s", d.c_str());
                    proc_.picked.clear();
                    ProcLoad();
                }
            }
            if (ImGui::MenuItem("Выбрать файлы…"))
            {
                std::vector<std::string> picked;
                if (dialogs::PickFiles(picked, proc_.dir, L"Файлы замеров (*.CSV, *.TXT)\0*.CSV;*.TXT\0Все файлы\0*.*\0",
                                       L"Файлы замеров для обработки"))
                {
                    proc_.picked = picked;
                    const std::string d = text::PathToUtf8(text::PathFromUtf8(picked.front()).parent_path());
                    std::snprintf(proc_.dir, sizeof(proc_.dir), "%s", d.c_str());
                    ProcLoad();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Отдельные файлы (Ctrl / Shift — несколько)");
            ImGui::Separator();
            if (ImGui::MenuItem("Перечитать", "F5"))
                ProcLoad();
            if (proc_.recMenu < 0)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (proc_.recMenu < 0)
            proc_.recMenu = 0;

        // Строки 2–3: исходные данные (переносятся по ширине окна)
        bool first = true;
        auto flow = [&](float itemW) {
            if (!first && ImGui::GetItemRectMax().x + S(20) + itemW <= right)
                ImGui::SameLine(0, S(20));
            first = false;
        };
        auto field = [&](const char* label, const char* id, double* v, double step, double fast, const char* fmt,
                         double lo, double hi, float fw, const char* tip) {
            flow(ImGui::CalcTextSize(label).x + S(8) + fw);
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (tip && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tip);
            ImGui::SameLine(0, S(8));
            ImGui::SetNextItemWidth(fw);
            const bool changed = InputDoubleStep(id, v, step, fast, fmt);
            if (changed)
            {
                *v = std::clamp(*v, lo, hi);
                proc_.dirty = true;
                SaveSettingsSoon();
            }
            ImGui::EndGroup();
            return changed;
        };
        // Запись (если файлов несколько)
        if (ProcCont() && proc_.meas.size() > 1 && proc_.files)
        {
            const float cw = S(220);
            flow(ImGui::CalcTextSize("Запись").x + S(8) + cw);
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Запись");
            ImGui::SameLine(0, S(8));
            ImGui::SetNextItemWidth(cw);
            auto recName = [&](int i) {
                const auto& m = proc_.meas[static_cast<std::size_t>(i)];
                const int mp = heel::MainPost(m);
                const double dur = mp >= 0 ? (*proc_.files)[static_cast<std::size_t>(m.post[mp].file)].log.Duration() : 0.0;
                return Fmt("%s%s · %s", m.number < heel::kPcRunBase ? "Замер " : "", m.label.c_str(),
                           text::Duration(static_cast<std::uint64_t>(dur)).c_str());
            };
            if (ImGui::BeginCombo("##rec", recName(proc_.rec).c_str()))
            {
                for (int i = 0; i < static_cast<int>(proc_.meas.size()); i++)
                    if (ImGui::Selectable(recName(i).c_str(), i == proc_.rec) && i != proc_.rec)
                    {
                        proc_.rec = i;
                        ProcFindPositions();
                    }
                ImGui::EndCombo();
            }
            ImGui::EndGroup();
        }
        const bool model = settings_.heelSmall;
        const std::string labD = std::string("Водоизмещение D, ") + (model ? "кг" : "т");
        const std::string labP = std::string("Масса груза P, ") + (model ? "кг" : "т");
        const std::string labL = std::string("Плечо L, ") + (model ? "мм" : "м");
        const std::string fmtD = NumFmt(settings_.heelD, settings_.digD), fmtP = NumFmt(settings_.heelP, settings_.digP);
        const std::string fmtL = NumFmt(settings_.heelL, std::max(3, settings_.digArm));
        field(labD.c_str(), "##hd", &settings_.heelD, model ? 1 : 10, model ? 10 : 100, fmtD.c_str(), 0.0001, 1e9, S(150),
              "Водоизмещение судна в момент опыта");
        field(labP.c_str(), "##hp", &settings_.heelP, model ? 0.1 : 0.5, model ? 1 : 5, fmtP.c_str(), 0.0, 1e9, S(136),
              "Масса кренящего груза, который переносят с борта на борт");
        {
            const char* label = labL.c_str();
            flow(ImGui::CalcTextSize(label).x + S(8) + S(136) + S(8) + S(72));
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Плечо переноса груза по умолчанию (как «макс. плечо переноса» старой программы).\n"
                                  "У каждого замера можно задать своё — в таблице, столбец «Плечо l».");
            ImGui::SameLine(0, S(8));
            ImGui::SetNextItemWidth(S(136));
            if (InputDoubleStep("##hl", &settings_.heelL, model ? 10.0 : 0.1, model ? 100.0 : 1.0, fmtL.c_str()))
            {
                settings_.heelL = std::clamp(settings_.heelL, 0.0, 1e7);
                proc_.dirty = true;
                SaveSettingsSoon();
            }
            ImGui::SameLine(0, S(8));
            const bool cont = ProcCont();
            bool anyOwn = !proc_.arm.empty();
            if (cont)
            {
                anyOwn = false;
                for (const auto& p : proc_.pos)
                    anyOwn |= std::isfinite(p.arm);
            }
            if (Button("Всем", BtnKind::Normal, ImVec2(S(72), 0), !anyOwn,
                       cont ? "Плечи всех положений уже равны L" : "Плечи всех замеров уже равны L"))
            {
                if (cont)
                    for (auto& p : proc_.pos)
                        p.arm = heel::kNaN;
                else
                    proc_.arm.clear();
                proc_.dirty = true;
            }
            if (ImGui::IsItemHovered() && anyOwn)
                ImGui::SetTooltip(cont ? "Всем положениям — плечо L (сбросить плечи, заданные в таблице)"
                                       : "Всем замерам — плечо L (сбросить плечи, заданные в таблице)");
            ImGui::EndGroup();
        }
        {
            const std::string labB = std::string("Ширина B, ") + (model ? "мм" : "м");
            const std::string fmtB = NumFmt(settings_.heelB, model ? 1 : 3);
            field(labB.c_str(), "##hb", &settings_.heelB, model ? 10 : 0.1, model ? 100 : 1, fmtB.c_str(), 0.0, 1e7, S(120),
                  "Ширина судна (модели) — для плеч «от борта»: в таблице вводится расстояние от выбранного борта до\n"
                  "центра груза, плечо от ДП программа считает сама (от ПрБ l = B/2 − x, от ЛБ l = x − B/2)");
        }
        {
            flow(S(40) + S(8) + S(84));
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Ось");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Угол из столбца CalcX или CalcY файла. X — датчики стоят поперёк судна (по умолчанию);\n"
                                  "Y — как в старой программе обработки. Выбор запоминается.");
            ImGui::SameLine(0, S(8));
            static const char* axes[] = {"X", "Y"};
            static const int axisValues[] = {0, 1};
            if (Segmented("axis", &settings_.heelAxis, axes, axisValues, 2))
            {
                proc_.dirty = true;
                SaveSettingsSoon(); // выбор оси — сразу в файл настроек
                if (ProcCont())
                    ProcFindPositions(); // ступеньки — по другому столбцу
            }
            ImGui::EndGroup();
        }
        if (ProcCont())
        {
            const bool edited = proc_.posEdited;
            if (field("Положение ≥, с", "##hps", &settings_.heelPlateauS, 1, 5, "%.0f", 2.0, 600.0, S(112),
                      "Положение груза — участок установившегося крена не короче N секунд (переходы при переносе\n"
                      "груза и всплески отбрасываются). Изменение — поиск положений заново."))
                ProcFindPositions(false);
            const std::string fmtS = NumFmt(settings_.heelStep, settings_.digStep);
            if (field("Шаг ≥, °", "##hst", &settings_.heelStep, 0.1, 0.5, fmtS.c_str(), 0.05, 20.0, S(120),
                      "Соседние положения отличаются по крену не меньше чем на столько градусов; ближе — одно\n"
                      "положение (качка не делит ступеньку). Изменение — поиск положений заново."))
                ProcFindPositions(false);
            if (edited && !proc_.posEdited)
                ProcLog("Положения найдены заново — ручные правки сброшены");
        }
        else
            field("Окно, с", "##hw", &settings_.heelWinS, 5, 30, "%.0f", 1.0, 3600.0, S(120),
                  "Установившийся крен — среднее за последние N секунд каждого файла (в старой программе — 30 с)");
        const std::string fmtT = NumFmt(settings_.heelThr, settings_.digThr);
        field("Порог, °", "##ht", &settings_.heelThr, 0.05, 0.5, fmtT.c_str(), 0.0, 10.0, S(120),
              ProcCont() ? "Положение с |Δθ| (от начального) не больше порога не учитывается: h не считается"
                         : "Замер на посту с |крен| не больше порога не учитывается: h не считается (в старой программе — 0,1°)");
        // Датчики: название поста каждого (нос, корма, мидель, …) и учитывать ли
        ProcSensorNames(right, flow);
        // Судно: большое (т, м) или малое — модель (кг, мм); знаки после запятой в полях
        {
            static const char* ships[] = {"Большое судно", "Малое судно (модель)"};
            static const int shipValues[] = {0, 1};
            const float sw = ImGui::CalcTextSize(ships[0]).x + ImGui::CalcTextSize(ships[1]).x +
                             4 * ImGui::GetStyle().FramePadding.x + S(2) + S(8) + S(84);
            flow(sw);
            ImGui::BeginGroup();
            int ship = settings_.heelSmall ? 1 : 0;
            if (Segmented("ship", &ship, ships, shipValues, 2))
                ProcSetSmallShip(ship == 1);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Большое судно: водоизмещение D и масса груза P — в тоннах, плечи — в метрах.\n"
                                  "Малое судно (модель, длина примерно до 5 м): D и P — в килограммах, плечи — в миллиметрах.\n"
                                  "При переключении введённые числа переводятся в новые единицы. h — в метрах (и в мм).");
            ImGui::SameLine(0, S(8));
            if (Button("Знаки…", BtnKind::Normal, ImVec2(S(84), 0)))
                ImGui::OpenPopup("##digits");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Сколько знаков после запятой показывать в полях. Число хранится как введено;\n"
                                  "маленькое ненулевое число нулём не показывается.");
            if (ImGui::BeginPopup("##digits"))
            {
                struct Dig
                {
                    const char* label;
                    int* v;
                    int lo;
                };
                const Dig digs[] = {{"Водоизмещение D", &settings_.digD, 0}, {"Масса груза P", &settings_.digP, 0},
                                    {"Плечи L и l (не меньше 3)", &settings_.digArm, 3}, {"Порог, °", &settings_.digThr, 0},
                                    {"Шаг ≥, °", &settings_.digStep, 0}};
                for (const Dig& d : digs)
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(d.label);
                    ImGui::SameLine(S(230));
                    ImGui::SetNextItemWidth(S(110));
                    if (InputIntStep(d.label, d.v, 1, 1))
                    {
                        *d.v = std::clamp(*d.v, d.lo, 6);
                        SaveSettingsSoon();
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::EndGroup();
        }
    }
    EndCard();
}

void App::ProcTable(float w, float h)
{
    if (BeginCard("##proc_tab", ImVec2(w, h)))
    {
        const auto* files = proc_.files.get();
        if (proc_.load.valid())
        {
            Muted("Чтение файлов… %d из %d", proc_.loadDone ? proc_.loadDone->load() : 0, proc_.loadTotal);
            Progress(proc_.loadTotal ? static_cast<float>(proc_.loadDone->load()) / proc_.loadTotal : 0.f, "");
        }
        else if (proc_.meas.empty())
        {
            CardTitle("Нет замеров");
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(files && !files->empty()
                                       ? "Ни один файл папки не понят как файл замера — что в них не так, написано в "
                                         "журнале справа («не понял формат: …, первая строка: …»)."
                                       : "В папке (и её подпапках) нет файлов *.CSV и *.TXT.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            SmallMuted("Скачайте замеры на странице «Файлы на карте» (они ложатся в её папку) или выберите папку / "
                       "файлы кнопками выше. Прибор для обработки не нужен.");
        }
        else
        {
            // Заголовки постов — с датчиками: «Крен Нос (Д2)»
            const std::vector<int> usedPosts = PostsInUse();
            std::vector<std::string> hdr, hdrH;
            for (const int p : usedPosts)
            {
                hdr.push_back("Крен " + PostTitle(p));
                hdrH.push_back("h " + SensorTitleOfPost(p) + ", м");
            }
            if (!settings_.rsGroupMode)
                ProcArmFromSwitch();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(2)));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(8), S(3)));
            if (ImGui::BeginTable("##proc_t", 3 + 2 * static_cast<int>(usedPosts.size()),
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | (procNatural_ ? 0 : ImGuiTableFlags_ScrollY) |
                                      ImGuiTableFlags_SizingFixedFit,
                                  ImVec2(0, procNatural_ ? 0.f : ImGui::GetContentRegionAvail().y)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(24));
                float numW = S(64); // у записей на ПК вместо номера — время и метка
                for (const auto& m : proc_.meas)
                    numW = std::max(numW, ImGui::CalcTextSize(m.label.c_str()).x + S(20));
                ImGui::TableSetupColumn("Замер", ImGuiTableColumnFlags_WidthFixed, std::min(numW, S(170)));
                const std::string armHdr = ProcArmHeader();
                ImGui::TableSetupColumn(armHdr.c_str(), ImGuiTableColumnFlags_WidthFixed,
                                        std::max(ProcArmSide() ? S(150) : S(76), ImGui::CalcTextSize(armHdr.c_str()).x + S(6)));
                for (std::size_t k = 0; k < usedPosts.size(); k++)
                {
                    ImGui::TableSetupColumn(hdr[k].c_str(), ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn(hdrH[k].c_str(), ImGuiTableColumnFlags_WidthFixed,
                                            std::max(S(66), ImGui::CalcTextSize(hdrH[k].c_str()).x + S(4)));
                }
                HeadersRowHint(2, kArmTip);
                for (std::size_t mi = 0; mi < proc_.meas.size(); mi++)
                {
                    heel::Measurement& m = proc_.meas[mi];
                    ImGui::TableNextRow();
                    ImGui::PushID(m.number);
                    // В расчёте
                    ImGui::TableNextColumn();
                    bool use = m.use;
                    if (ui::Checkbox("##use", &use))
                    {
                        proc_.off[m.number] = !use;
                        proc_.dirty = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Учитывать замер в расчёте, отчёте и на графиках");
                    const ImVec4 tc = m.use ? pal.text : pal.muted;
                    // Номер (цвет — как линия на графиках)
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    TextColored(m.use ? kMeasColors[mi % 10] : pal.muted, "●");
                    ImGui::SameLine(0, S(5));
                    TextColored(tc, "%s", m.label.c_str());
                    if (ImGui::IsItemHovered() && files)
                    {
                        std::string tip = "Замер " + m.label;
                        for (const int p : usedPosts)
                        {
                            tip += "\n" + PostTitle(p) + ": ";
                            if (m.post[p].file < 0)
                            {
                                tip += "нет файла";
                                continue;
                            }
                            const auto& f = (*files)[static_cast<std::size_t>(m.post[p].file)];
                            tip += f.name + Fmt(" — %s, %d строк, %.1f с", csvlog::FormatName(f.log.format),
                                                static_cast<int>(f.log.rows.size()), f.log.Duration());
                        }
                        ImGui::SetTooltip("%s", tip.c_str());
                    }
                    // Плечо
                    ImGui::TableNextColumn();
                    if (settings_.rsGroupMode)
                    {
                        ImGui::AlignTextToFramePadding();
                        if (m.use && std::isfinite(m.arm))
                            Muted(NumFmt(m.arm, std::max(3, settings_.digArm)).c_str(), m.arm);
                        else
                            Muted("—");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Крен-балласт группами: плечо замера — эквивалентное (накопленный момент / P),\n"
                                              "моменты — в виде «Перемещения»; первый учтённый замер — начальное положение");
                    }
                    else
                    {
                        const bool own = proc_.arm.count(m.number) > 0;
                        double arm = std::isfinite(m.arm) ? m.arm : settings_.heelL;
                        if (ProcArmField("##arm", arm, own ? pal.accent : tc))
                        {
                            proc_.arm[m.number] = std::clamp(arm, -1e7, 1e7);
                            proc_.dirty = true;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(own ? "Своё плечо переноса этого замера, м (L = %.2f м — кнопка «Всем»)"
                                                  : "Плечо переноса груза, м: пока = L (%.2f м); можно задать своё.\n"
                                                    "Со знаком: + к правому борту, − к левому (все положительные —\n"
                                                    "борт по знаку крена, как раньше).",
                                              settings_.heelL);
                    }
                    // Посты
                    for (const int p : usedPosts)
                    {
                        const heel::PostData& pd = m.post[p];
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        if (pd.file < 0)
                            Muted("нет файла");
                        else if (!std::isfinite(pd.st.mean))
                            TextColored(pal.err, "нет данных");
                        else
                        {
                            TextColored(tc, "%s", Deg(pd.st.mean).c_str());
                            if (std::isfinite(pd.st.sd))
                            {
                                ImGui::SameLine(0, S(6));
                                FontScope f(fontSmall);
                                Muted("±%.3f", pd.st.sd);
                            }
                        }
                        if (pd.file >= 0 && files && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped))
                        {
                            const auto& f = (*files)[static_cast<std::size_t>(pd.file)];
                            ImGui::SetTooltip("%s\nСреднее за %s %.1f…%.1f с: %s, СКО %.3f°, точек %d%s\nАКБ %.1f В",
                                              f.name.c_str(), pd.st.whole ? "весь файл," : "окно", pd.st.from, pd.st.to,
                                              Deg(pd.st.mean).c_str(), pd.st.sd, pd.st.n,
                                              pd.st.whole ? " (файл короче окна)" : "", pd.batV);
                        }
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        if (pd.res.counted)
                            TextColored(tc, "%.3f", pd.res.h);
                        else if (pd.file >= 0 && m.use)
                        {
                            Muted("—");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Не учитывается: %s", pd.res.note.c_str());
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar(2);
        }
    }
    EndCard();
}

// Строки журнала (внутри дочернего окна).
void App::ProcLogLines()
{
    {
        FontScope f(fontSmall);
        if (proc_.log.empty())
            Muted("Журнал: здесь — что прочитано, что пропущено и куда сохранён отчёт.");
        for (const auto& [l, tip] : proc_.log)
        {
            const bool bad = l.rfind("✗", 0) == 0, good = l.rfind("✓", 0) == 0;
            ImGui::PushStyleColor(ImGuiCol_Text, bad ? pal.err : good ? pal.ok : pal.textDim);
            ImGui::TextWrapped("%s", l.c_str());
            ImGui::PopStyleColor();
            if (!tip.empty() && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(S(640));
                ImGui::TextUnformatted(tip.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        if (proc_.logScroll)
        {
            ImGui::SetScrollHereY(1.f);
            proc_.logScroll = false;
        }
    }
}

void App::ProcPlots(float w, float h)
{
    if (BeginCard("##proc_plots", ImVec2(w, h)))
    {
        const float right = ProcCardTitle("Графики крена", 1);
        const std::vector<int> used = PostsInUse();
        if (!used.empty())
        {
            ImGui::SameLine(0, S(14));
            std::vector<std::string> labels;
            std::vector<const char*> lp;
            for (const int p : used)
                labels.push_back(PostTitle(p));
            for (const auto& l : labels)
                lp.push_back(l.c_str());
            if (std::find(used.begin(), used.end(), proc_.show) == used.end())
                proc_.show = used.front();
            Segmented("post", &proc_.show, lp.data(), used.data(), static_cast<int>(used.size()));
        }
        HeaderHint(right, Fmt("ось %s · пунктир — среднее за последние %.0f с · легенда — средний крен замера",
                              settings_.heelAxis == 0 ? "X" : "Y", settings_.heelWinS),
                   Fmt("ось %s · пунктир — среднее за %.0f с", settings_.heelAxis == 0 ? "X" : "Y", settings_.heelWinS));
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ProcPanes(proc_.show, avail.x, avail.y, false, false);
    }
    EndCard();
}

// Переключатель режима справа в строке заголовка карточки графиков: right — правый край (ширина — ModeSwitchWidth).
float App::ModeSwitchWidthPublic() const
{
    return ModeSwitchWidth();
}

void App::ProcModeSwitch(float right)
{
    static const char* modes[] = {"Файл = замер", "Весь опыт одной записью"};
    static const int modeValues[] = {0, 1};
    ImGui::SameLine(right - ModeSwitchWidth() - ImGui::GetWindowPos().x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Режим");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("«Файл = замер» — каждый файл (пара файлов постов) — один замер, крен — среднее за\n"
                          "последние N секунд файла.\n"
                          "«Весь опыт одной записью» — груз переносили во время одной записи: положения груза\n"
                          "ищутся по ступенькам крена.\n"
                          "Выбирается сам при чтении: если в записи три положения и больше — второй режим.");
    ImGui::SameLine(0, S(8));
    int mode = proc_.mode;
    if (Segmented("mode", &mode, modes, modeValues, 2))
        ProcessSetMode(mode);
}

// Картинка графиков одного поста для отчёта: заголовок, две панели друг под другом (как у старой программы).
void App::ProcExportPage(int post, float w, float h)
{
    {
        FontScope f(fontH2);
        ImGui::Text("Пост «%s» — крен по оси %s", PostTitle(post).c_str(), settings_.heelAxis == 0 ? "X" : "Y");
    }
    {
        FontScope f(fontSmall);
        Muted("Опыт кренования: D = %.1f т, P = %.2f т · пунктир — среднее за последние %.0f с · %s",
              settings_.heelD, settings_.heelP, settings_.heelWinS, Shown(proc_.outDir).c_str());
    }
    ImGui::Spacing();
    (void) h;
    ProcPanes(post, w, ImGui::GetContentRegionAvail().y, true, true);
}

// Панели «Правый борт (+)» и «Левый борт (−)» поста: замер — линия крена от времени, пунктир того же цвета — его
// среднее за окно, в легенде — «Замер N (+2.451°)». Борт — по знаку среднего (как у старой программы).
void App::ProcPanes(int post, float w, float h, bool stacked, bool forExport)
{
    const auto* files = proc_.files.get();
    const int axis = settings_.heelAxis;
    ProcPlotStyle(forExport);
    const float lineW = S(forExport ? 1.5f : 1.6f);
    const float gap = S(12);
    const float pw = stacked ? w : std::floor((w - gap) * 0.5f);
    const float ph = stacked ? std::floor((h - gap) * 0.5f) : h;
    std::vector<double> dx, dy;
    for (int side = 0; side < 2; side++)
    {
        if (side)
        {
            if (stacked)
                ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
            else
                ImGui::SameLine(0, gap);
        }
        // Замеры этого борта и пределы осей
        struct Series
        {
            std::size_t mi;
            const heel::File* f;
            double mean;
        };
        std::vector<Series> ser;
        double tMax = 0, lo = std::numeric_limits<double>::infinity(), hi = -lo;
        if (files)
            for (std::size_t mi = 0; mi < proc_.meas.size(); mi++)
            {
                const auto& m = proc_.meas[mi];
                const auto& pd = m.post[post];
                if (!m.use || pd.file < 0 || !std::isfinite(pd.st.mean) || (pd.st.mean > 0) != (side == 0))
                    continue;
                const heel::File& f = (*files)[static_cast<std::size_t>(pd.file)];
                ser.push_back({mi, &f, pd.st.mean});
                for (const auto& r : f.log.rows)
                {
                    const double v = r.Calc(axis);
                    if (std::isfinite(v))
                    {
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                    }
                }
                tMax = std::max(tMax, f.log.rows.back().t);
            }
        if (ser.empty() || !(hi >= lo))
        {
            lo = side == 0 ? 0.0 : -1.0;
            hi = side == 0 ? 1.0 : 0.0;
            tMax = 60.0;
        }
        const double pad = std::max((hi - lo) * 0.08, 0.005);
        const char* title = side == 0 ? "Правый борт (+)" : "Левый борт (−)";
        char id[64];
        std::snprintf(id, sizeof(id), "%s##proc%d%d%d", title, post, side, forExport ? 1 : 0);
        if (ImPlot::BeginPlot(id, ImVec2(pw, ph), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText))
        {
            ImPlot::SetupAxes("Время, с", "Угол крена, °", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, std::max(1.0, tMax * 1.02), ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, lo - pad, hi + pad, ImPlotCond_Always);
            ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3f");
            ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);
            ImPlot::SetupFinish();
            const ImPlotRect lim = ImPlot::GetPlotLimits();
            const double perPx = (lim.X.Max - lim.X.Min) / std::max(1.f, ImPlot::GetPlotSize().x);
            for (const Series& s : ser)
            {
                const ImVec4 col = kMeasColors[s.mi % 10];
                const std::string label = Fmt("Замер %s (%s)", proc_.meas[s.mi].label.c_str(), Deg(s.mean).c_str());
                const auto& rows = s.f->log.rows;
                ImPlot::SetNextLineStyle(col, lineW);
                ImPlot::PlotLine(label.c_str(), &rows[0].t, axis == 0 ? &rows[0].calcX : &rows[0].calcY,
                                 static_cast<int>(rows.size()), 0, 0, static_cast<int>(sizeof(csvlog::Row)));
                // Пунктир во всю ширину (как axhline): штрих 9 точек, пробел 5; та же метка — та же запись легенды
                dx.clear();
                dy.clear();
                const double dash = S(9) * perPx, space = S(5) * perPx;
                for (double x = lim.X.Min; x < lim.X.Max; x += dash + space)
                {
                    dx.insert(dx.end(), {x, std::min(x + dash, lim.X.Max), std::numeric_limits<double>::quiet_NaN()});
                    dy.insert(dy.end(), {s.mean, s.mean, std::numeric_limits<double>::quiet_NaN()});
                }
                ImPlot::SetNextLineStyle(Alpha(col, 0.85f), lineW);
                ImPlot::PlotLine(label.c_str(), dx.data(), dy.data(), static_cast<int>(dx.size()));
            }
            if (ser.empty())
                ImPlot::PlotText(side == 0 ? "нет замеров с креном на правый борт" : "нет замеров с креном на левый борт",
                                 0.5 * (lim.X.Min + lim.X.Max), 0.5 * (lim.Y.Min + lim.Y.Max));
            ImPlot::EndPlot();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Одна запись — весь опыт: положения груза
// ---------------------------------------------------------------------------------------------------------------

// Самый длинный свободный участок записи (между положениями, с краями записи); true — не короче 4 с.
bool App::ProcFreeGap(double& gapA, double& gapB) const
{
    gapA = gapB = 0;
    if (!ProcCont() || !proc_.files)
        return false;
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    const auto* files = proc_.files.get();
    const int mp = heel::MainPost(m);
    const heel::File* mainFile = mp >= 0 ? &(*files)[static_cast<std::size_t>(m.post[mp].file)] : nullptr;
    if (!mainFile || mainFile->log.rows.empty())
        return false;
    const double tBeg = mainFile->log.rows.front().t, tEnd = mainFile->log.rows.back().t;
    std::vector<std::pair<double, double>> sp;
    for (const auto& p : proc_.pos)
        sp.push_back({std::min(p.t0, p.t1), std::max(p.t0, p.t1)});
    std::sort(sp.begin(), sp.end());
    double prev = tBeg;
    sp.push_back({tEnd, tEnd});
    for (const auto& [a, b] : sp)
    {
        if (a - prev > gapB - gapA)
        {
            gapA = prev;
            gapB = a;
        }
        prev = std::max(prev, b);
    }
    return gapB - gapA >= 4.0;
}

bool App::ProcessAddPosition()
{
    double gapA = 0, gapB = 0;
    if (!ProcFreeGap(gapA, gapB))
        return false;
    heel::Position p;
    p.t0 = gapA + std::min(1.0, 0.25 * (gapB - gapA));
    p.t1 = gapB - std::min(1.0, 0.25 * (gapB - gapA));
    proc_.pos.push_back(p);
    ProcSortPositions();
    for (std::size_t i = 0; i < proc_.pos.size(); i++)
        if (proc_.pos[i].t0 == p.t0)
            proc_.sel = static_cast<int>(i);
    proc_.posEdited = true;
    proc_.dirty = true;
    return true;
}

void App::ProcDeletePosition(int index)
{
    if (index < 0 || index >= static_cast<int>(proc_.pos.size()))
        return;
    proc_.undoValid = true;
    proc_.undoIndex = index;
    proc_.undoPos = proc_.pos[static_cast<std::size_t>(index)];
    proc_.pos.erase(proc_.pos.begin() + index);
    ProcSortPositions();
    proc_.sel = -1;
    proc_.posEdited = true;
    proc_.dirty = true;
}

bool App::ProcessDeleteSelected()
{
    if (!ProcCont() || proc_.sel < 0 || proc_.sel >= static_cast<int>(proc_.pos.size()))
        return false;
    ProcDeletePosition(proc_.sel);
    ProcLog("Положение удалено (Del) — вернуть: Ctrl+Z");
    return true;
}

bool App::ProcessUndoDelete()
{
    if (!proc_.undoValid || !ProcCont())
        return false;
    proc_.undoValid = false;
    const heel::Position p = proc_.undoPos;
    proc_.pos.push_back(p);
    ProcSortPositions();
    for (std::size_t i = 0; i < proc_.pos.size(); i++)
        if (proc_.pos[i].t0 == p.t0 && proc_.pos[i].t1 == p.t1)
            proc_.sel = static_cast<int>(i);
    proc_.posEdited = true;
    proc_.dirty = true;
    ProcLog("Удалённое положение возвращено (Ctrl+Z)");
    return true;
}

bool App::ProcessSelectNext(int step)
{
    const int n = ProcCont() ? static_cast<int>(proc_.pos.size()) : 0;
    if (n == 0)
        return false;
    if (proc_.sel < 0 || proc_.sel >= n)
        proc_.sel = step > 0 ? 0 : n - 1;
    else
        proc_.sel = std::clamp(proc_.sel + step, 0, n - 1);
    return true;
}

void App::ProcPositions(float w, float h)
{
    if (BeginCard("##proc_pos", ImVec2(w, h)))
    {
        const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
        const auto* files = proc_.files.get();
        const int mp = heel::MainPost(m);
        const heel::File* mainFile = mp >= 0 && files ? &(*files)[static_cast<std::size_t>(m.post[mp].file)] : nullptr;
        const double tBeg = mainFile && !mainFile->log.rows.empty() ? mainFile->log.rows.front().t : 0.0;
        const double tEnd = mainFile && !mainFile->log.rows.empty() ? mainFile->log.rows.back().t : 0.0;
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

        // Свободный участок записи для «+ Положение»: самый длинный промежуток между положениями
        double gapA = 0, gapB = 0;
        const bool canAdd = ProcFreeGap(gapA, gapB);

        // Заголовок: число положений, запись; справа — «+ Положение», «Найти заново»
        ImGui::AlignTextToFramePadding();
        {
            FontScope f(fontBold);
            ImGui::Text("Положения груза: %d", static_cast<int>(proc_.pos.size()));
        }
        const float bw1 = S(126), bw2 = S(132);
        const float btnX = right - bw1 - bw2 - S(8);
        if (mainFile)
        {
            const std::string info = Fmt("%s · %s · по %s%s", mainFile->name.c_str(),
                                         text::Duration(static_cast<std::uint64_t>(mainFile->log.Duration())).c_str(),
                                         settings_.heelAxis == 0 ? "CalcX" : "CalcY",
                                         proc_.posEdited ? " · поправлены вручную" : "");
            FontScope f(fontSmall);
            ImGui::SameLine(0, S(10));
            const float tw = ImGui::CalcTextSize(info.c_str()).x;
            if (ImGui::GetCursorScreenPos().x + tw < btnX - S(8))
            {
                ImGui::AlignTextToFramePadding();
                Muted("%s", info.c_str());
            }
            else
                ImGui::Dummy(ImVec2(0, 0));
        }
        ImGui::SameLine(btnX - ImGui::GetWindowPos().x);
        if (Button("+ Положение", BtnKind::Normal, ImVec2(bw1, 0), !canAdd, "Нет свободного участка записи"))
            ProcessAddPosition();
        if (ImGui::IsItemHovered() && canAdd)
            ImGui::SetTooltip("+ Положение (Ins): новое положение на самом длинном свободном участке записи (%.1f…%.1f с);\n"
                              "границы — в таблице или перетаскиванием на графике.",
                              gapA, gapB);
        ImGui::SameLine(0, S(8));
        if (Button(proc_.posEdited ? "Сбросить правки" : "Найти заново", BtnKind::Normal, ImVec2(bw2, 0)))
        {
            if (const auto dir = EditsDir(); !dir.empty())
                heel::ForgetPositionEdits(dir, EditsKeyNow());
            ProcFindPositions(false);
            ProcRecompute();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(proc_.posEdited ? "Забыть ручные правки положений этой записи (границы, флажки, плечи,\n"
                                                "добавленные и удалённые) и найти положения заново"
                                              : "Найти положения груза по записи заново");

        if (proc_.pos.empty())
        {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted("Положений груза не найдено: нет участков установившегося крена нужной длины. "
                                   "Уменьшите «Положение ≥, с» или «Шаг ≥, °», проверьте ось — или добавьте положения "
                                   "вручную («+ Положение»).");
            ImGui::PopTextWrapPos();
        }
        else
        {
            int posts[heel::kMaxPosts], np = 0;
            for (int p = 0; p < heel::kMaxPosts; p++)
                if (m.post[p].file >= 0)
                    posts[np++] = p;
            const bool showDelta = np == 1;
            if (!settings_.rsGroupMode)
                ProcArmFromSwitch();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(2)));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(6), S(3)));
            const int cols = 6 + np * (showDelta ? 3 : 2);
            if (ImGui::BeginTable("##pos_t", cols,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | (procNatural_ ? 0 : ImGuiTableFlags_ScrollY) |
                                      ImGuiTableFlags_SizingFixedFit,
                                  ImVec2(0, procNatural_ ? 0.f : ImGui::GetContentRegionAvail().y)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(22));
                ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed, S(34));
                ImGui::TableSetupColumn("Начало, с", ImGuiTableColumnFlags_WidthFixed, S(62));
                ImGui::TableSetupColumn("Конец, с", ImGuiTableColumnFlags_WidthFixed, S(62));
                const std::string armHdr = ProcArmHeader();
                ImGui::TableSetupColumn(armHdr.c_str(), ImGuiTableColumnFlags_WidthFixed,
                                        std::max(ProcArmSide() ? S(150) : S(68), ImGui::CalcTextSize(armHdr.c_str()).x + S(6)));
                std::string hdr[heel::kMaxPosts], hdrH[heel::kMaxPosts];
                for (int k = 0; k < np; k++)
                {
                    const int p = posts[k];
                    hdr[k] = "Крен " + PostTitle(p);
                    hdrH[k] = np == 1 ? std::string("h, м") : "h " + SensorTitleOfPost(p) + ", м";
                    ImGui::TableSetupColumn(hdr[k].c_str(), ImGuiTableColumnFlags_WidthStretch);
                    if (showDelta)
                        ImGui::TableSetupColumn("Δθ", ImGuiTableColumnFlags_WidthFixed, S(62));
                    ImGui::TableSetupColumn(hdrH[k].c_str(), ImGuiTableColumnFlags_WidthFixed,
                                            np == 1 ? S(52.f) : std::max(S(70.f), ImGui::CalcTextSize(hdrH[k].c_str()).x + S(4)));
                }
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 3 * S(24) + 2 * S(3));
                HeadersRowHint(4, kArmTip);
                int action = 0, actionAt = -1; // 1 — разделить, 2 — слить со следующим, 3 — удалить
                for (std::size_t i = 0; i < proc_.pos.size(); i++)
                {
                    heel::Position& ps = proc_.pos[i];
                    const bool sel = proc_.sel == static_cast<int>(i);
                    ImGui::TableNextRow();
                    if (sel)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, U32(Alpha(pal.accent, darkTheme ? 0.22f : 0.12f)));
                    ImGui::PushID(static_cast<int>(i));
                    const ImVec4 tc = ps.use ? pal.text : pal.muted;
                    // В расчёте
                    ImGui::TableNextColumn();
                    if (ui::Checkbox("##use", &ps.use))
                    {
                        proc_.posEdited = true;
                        proc_.dirty = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Учитывать положение в расчёте и отчёте");
                    // Номер (цвет — как заливка на графике); щелчок — выбрать (границы — на графике)
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    TextColored(ps.use ? kMeasColors[i % 10] : pal.muted, "●");
                    ImGui::SameLine(0, S(4));
                    if (ImGui::Selectable(Fmt("%d", static_cast<int>(i) + 1).c_str(), sel, 0,
                                          ImVec2(0, ImGui::GetFrameHeight())))
                        proc_.sel = sel ? -1 : static_cast<int>(i);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Выбрать положение: его границы можно перетаскивать на графике");
                    // Границы
                    for (double* t : {&ps.t0, &ps.t1})
                    {
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::PushStyleColor(ImGuiCol_Text, tc);
                        ImGui::PushID(t == &ps.t0 ? "t0" : "t1");
                        if (ImGui::InputDouble("##t", t, 0.0, 0.0, "%.1f"))
                        {
                            *t = std::clamp(*t, tBeg, tEnd);
                            proc_.posEdited = true;
                            proc_.dirty = true;
                            proc_.sel = static_cast<int>(i);
                        }
                        ImGui::PopID();
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s участка положения, с от начала записи (%.1f…%.1f с)",
                                              t == &ps.t0 ? "Начало" : "Конец", tBeg, tEnd);
                    }
                    // Плечо
                    ImGui::TableNextColumn();
                    bool reference = false;
                    for (int k = 0; k < np; k++)
                        reference |= ps.res[posts[k]].reference;
                    if (reference && !settings_.rsGroupMode)
                    {
                        // Начальное положение: плечо необязательно; задано — плечи всех положений от ДП
                        const bool own = std::isfinite(ps.arm);
                        double arm = own ? ps.arm : (ProcArmSide() ? heel::ArmFromSide(0.0, settings_.armFrom, settings_.heelB) : 0.0);
                        if (ProcArmField("##arm0", arm, own ? pal.accent : ImVec4(0, 0, 0, 0), !own))
                        {
                            ps.arm = std::clamp(arm, -1e7, 1e7);
                            proc_.posEdited = true;
                            proc_.dirty = true;
                        }
                        if (!own && !ImGui::IsItemActive())
                        {
                            const ImVec2 r0 = ImGui::GetItemRectMin();
                            ImGui::GetWindowDrawList()->AddText(
                                ImVec2(r0.x + ImGui::GetStyle().FramePadding.x, r0.y + ImGui::GetStyle().FramePadding.y),
                                U32(pal.muted), "нач.");
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Начальное положение: от него считаются Δθ и перенос груза остальных.\n"
                                              "Плечо здесь необязательно. Если задать (плечо груза от ДП, + к правому\n"
                                              "борту), плечи всех положений вводятся от ДП, а перенос считается как\n"
                                              "разность с начальным (удобно, когда меряют расстояние от борта).");
                    }
                    else if (reference)
                    {
                        ImGui::AlignTextToFramePadding();
                        Muted("нач.");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Начальное положение: от него считаются Δθ и перенос груза остальных");
                    }
                    else if (settings_.rsGroupMode)
                    {
                        ImGui::AlignTextToFramePadding();
                        const double ga = i < proc_.groupArm.size() ? proc_.groupArm[i] : heel::kNaN;
                        if (std::isfinite(ga))
                            Muted(NumFmt(ga, std::max(3, settings_.digArm)).c_str(), ga);
                        else
                            Muted("—");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Крен-балласт группами: плечо положения — эквивалентное (накопленный момент / P),\n"
                                              "моменты — в виде «Перемещения»");
                    }
                    else
                    {
                        const bool own = std::isfinite(ps.arm);
                        double arm = own ? ps.arm : settings_.heelL;
                        if (ProcArmField("##arm", arm, own ? pal.accent : tc))
                        {
                            ps.arm = std::clamp(arm, -1e7, 1e7);
                            proc_.posEdited = true;
                            proc_.dirty = true;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(own ? "Плечо переноса груза от начального положения, м (L = %.2f м — кнопка «Всем»)"
                                                  : "Плечо переноса груза от начального положения, м: пока = L (%.2f м).\n"
                                                    "Со знаком: + к правому борту, − к левому (все положительные —\n"
                                                    "борт по знаку Δθ, как раньше).",
                                              settings_.heelL);
                    }
                    // Посты: крен ± СКО, Δθ, h
                    for (int k = 0; k < np; k++)
                    {
                        const int p = posts[k];
                        const auto& st = ps.st[p];
                        const auto& res = ps.res[p];
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        const double level = ps.Level(p);
                        const auto& est = ps.est[p];
                        if (!std::isfinite(level))
                            TextColored(pal.err, "нет данных");
                        else
                        {
                            TextColored(tc, "%s", text::Angle(level, 3).c_str());
                            if (std::isfinite(st.sd))
                            {
                                ImGui::SameLine(0, S(5));
                                FontScope f(fontSmall);
                                Muted("±%.3f", st.sd);
                            }
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped) && std::isfinite(level))
                            ImGui::SetTooltip("Крен положения (%s): %s — окно %.1f…%.1f с\n"
                                              "Простое среднее исходных на участке %.1f…%.1f с: %s, СКО (качка) %.3f°,\n"
                                              "точек %d, выбросов отброшено %d\nΔθ от начального положения: %s",
                                              filt::MethodName(ProcSettings().filter.method), text::Angle(level, 3).c_str(),
                                              est.winT0, est.winT1, std::min(ps.t0, ps.t1), std::max(ps.t0, ps.t1),
                                              text::Angle(st.mean, 3).c_str(), st.sd, st.n, st.rejected,
                                              text::Angle(res.dTheta, 3).c_str());
                        if (showDelta)
                        {
                            ImGui::TableNextColumn();
                            ImGui::AlignTextToFramePadding();
                            if (std::isfinite(res.dTheta))
                                TextColored(res.reference ? pal.muted : tc, "%s", text::Angle(res.dTheta, 2).c_str());
                        }
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        if (res.counted)
                            TextColored(tc, "%.3f", res.h);
                        else if (ps.use && !res.reference)
                        {
                            Muted("—");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Не учитывается: %s", res.note.c_str());
                        }
                    }
                    // Действия
                    ImGui::TableNextColumn();
                    const float fb = S(24), fh = ImGui::GetFrameHeight();
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, S(2)));
                    if (Button("÷", BtnKind::Normal, ImVec2(fb, fh), std::fabs(ps.t1 - ps.t0) < 4.0, "Слишком короткое"))
                    {
                        action = 1;
                        actionAt = static_cast<int>(i);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Разделить положение пополам");
                    ImGui::SameLine(0, S(3));
                    if (Button("↓", BtnKind::Normal, ImVec2(fb, fh), i + 1 >= proc_.pos.size(), "Это последнее положение"))
                    {
                        action = 2;
                        actionAt = static_cast<int>(i);
                    }
                    if (ImGui::IsItemHovered() && i + 1 < proc_.pos.size())
                        ImGui::SetTooltip("Слить со следующим положением");
                    ImGui::SameLine(0, S(3));
                    if (Button("✕", BtnKind::Normal, ImVec2(fb, fh)))
                    {
                        action = 3;
                        actionAt = static_cast<int>(i);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Удалить положение (Del; отмена — Ctrl+Z)");
                    ImGui::PopStyleVar();
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (action && actionAt >= 0 && actionAt < static_cast<int>(proc_.pos.size()))
                {
                    const auto at = static_cast<std::size_t>(actionAt);
                    if (action == 1)
                    {
                        heel::Position a = proc_.pos[at], b = a;
                        const double mid = 0.5 * (a.t0 + a.t1);
                        a.t1 = mid;
                        b.t0 = mid;
                        b.arm = heel::kNaN;
                        proc_.pos[at] = a;
                        proc_.pos.insert(proc_.pos.begin() + static_cast<std::ptrdiff_t>(at) + 1, b);
                    }
                    else if (action == 2 && at + 1 < proc_.pos.size())
                    {
                        auto& a = proc_.pos[at];
                        const auto& b = proc_.pos[at + 1];
                        a.t0 = std::min({a.t0, a.t1, b.t0, b.t1});
                        a.t1 = std::max({a.t0, a.t1, b.t0, b.t1});
                        proc_.pos.erase(proc_.pos.begin() + static_cast<std::ptrdiff_t>(at) + 1);
                    }
                    else if (action == 3)
                    {
                        proc_.undoValid = true;
                        proc_.undoIndex = actionAt;
                        proc_.undoPos = proc_.pos[at];
                        proc_.pos.erase(proc_.pos.begin() + static_cast<std::ptrdiff_t>(at));
                    }
                    ProcSortPositions();
                    proc_.sel = action == 3 ? -1 : actionAt;
                    proc_.posEdited = true;
                    proc_.dirty = true;
                }
            }
            ImGui::PopStyleVar(2);
        }
    }
    EndCard();
}

void App::ProcSeriesCard(float w, float h)
{
    if (BeginCard("##proc_series", ImVec2(w, h)))
    {
        const float right = ProcCardTitle("Запись с положениями", 1);
        const int post = ProcShownPost();
        const std::vector<int> used = PostsInUse();
        if (used.size() > 1)
        {
            ImGui::SameLine(0, S(14));
            std::vector<std::string> labels;
            std::vector<const char*> lp;
            for (const int p : used)
                labels.push_back(SensorTitleOfPost(p));
            for (const auto& l : labels)
                lp.push_back(l.c_str());
            int show = post;
            if (Segmented("spost", &show, lp.data(), used.data(), static_cast<int>(used.size())))
                proc_.show = show;
        }
        ImGui::SameLine(0, S(10));
        ProcFilterButton();
        HeaderHint(right, Fmt("ось %s · щелчок по положению — выбрать (крупно справа), границы перетаскиваются",
                              settings_.heelAxis == 0 ? "X" : "Y"),
                   Fmt("ось %s", settings_.heelAxis == 0 ? "X" : "Y"));
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (proc_.sel >= 0 && proc_.sel < static_cast<int>(proc_.pos.size()))
        {
            // Выбрано положение: слева вся запись, справа — положение крупно (исходный, обработанный, среднее)
            const float gap = S(12);
            const float lw = std::floor((avail.x - gap) * (proc_.expanded == 1 ? 0.6f : 0.5f));
            ProcSeriesPlot(post, lw, avail.y, false);
            ImGui::SameLine(0, gap);
            ProcPositionPlot(post, proc_.sel, avail.x - lw - gap, avail.y, false);
        }
        else
            ProcSeriesPlot(post, avail.x, avail.y, false);
    }
    EndCard();
}

// Вся запись поста: линия крена, положения — заливка с номером, их средний крен — линия; у выбранного положения
// границы перетаскиваются (кроме картинки для отчёта).
void App::ProcSeriesPlot(int post, float w, float h, bool forExport)
{
    if (!ProcCont() || !proc_.files)
        return;
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    if (post < 0 || post >= heel::kMaxPosts || m.post[post].file < 0)
        return;
    const heel::File& f = (*proc_.files)[static_cast<std::size_t>(m.post[post].file)];
    const auto& rows = f.log.rows;
    if (rows.empty())
        return;
    const int axis = settings_.heelAxis;
    ProcPlotStyle(forExport);
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const auto& r : rows)
        if (const double v = r.Calc(axis); std::isfinite(v))
        {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    if (!(hi >= lo))
    {
        lo = -1;
        hi = 1;
    }
    const double pad = std::max((hi - lo) * 0.06, 0.05);
    const double tBeg = rows.front().t, tEnd = rows.back().t;
    // В имени графика — и сама запись (строк, длительность): у другой записи пределы осей — заново по ней (иначе
    // остаются от прошлой: ImPlotCond_Once запоминает их по имени графика)
    char id[96];
    std::snprintf(id, sizeof(id), "##series%d_%d_%d_%d_%zu_%lld", post, proc_.rec, axis, forExport ? 1 : 0, rows.size(),
                  static_cast<long long>(std::llround(tEnd * 1000.0)));
    ImPlotFlags flags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoLegend;
    if (forExport)
        flags |= ImPlotFlags_NoMouseText;
    if (!ImPlot::BeginPlot(id, ImVec2(w, h), flags))
        return;
    ImPlot::SetupAxes("Время от начала записи, с", axis == 0 ? "Крен (CalcX), °" : "Крен (CalcY), °",
                      ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
    const ImPlotCond cond = forExport ? ImPlotCond_Always : ImPlotCond_Once; // на экране — масштаб колесом
    ImPlot::SetupAxisLimits(ImAxis_X1, tBeg, std::max(tBeg + 1.0, tEnd), cond);
    ImPlot::SetupAxisLimits(ImAxis_Y1, lo - pad, hi + pad * 3.5, cond); // сверху — место под номера
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.1f");
    ImPlot::SetupFinish();
    const ImPlotRect lim = ImPlot::GetPlotLimits();
    const float lineW = S(forExport ? 1.3f : 1.4f);

    // Заливка положений
    ImPlot::PushPlotClipRect();
    ImDrawList* dl = ImPlot::GetPlotDrawList();
    for (std::size_t i = 0; i < proc_.pos.size(); i++)
    {
        const auto& p = proc_.pos[i];
        const bool sel = !forExport && proc_.sel == static_cast<int>(i);
        const ImVec4 c = p.use ? kMeasColors[i % 10] : pal.muted;
        const ImVec2 a = ImPlot::PlotToPixels(std::min(p.t0, p.t1), lim.Y.Max);
        const ImVec2 b = ImPlot::PlotToPixels(std::max(p.t0, p.t1), lim.Y.Min);
        dl->AddRectFilled(a, b, U32(Alpha(c, sel ? (darkTheme ? 0.34f : 0.26f) : (darkTheme ? 0.20f : 0.13f))));
        if (sel)
            dl->AddRect(a, b, U32(Alpha(c, 0.9f)), 0.f, 0, S(1.5f));
    }
    ImPlot::PopPlotClipRect();

    // Запись: исходный сигнал — тонко и бледнее, обработанный — поверх
    const bool haveProc = !proc_.procY[post].empty();
    const ImVec4 rawCol = darkTheme ? ImVec4(0.80f, 0.84f, 0.90f, 1.f) : ImVec4(0.20f, 0.24f, 0.30f, 1.f);
    ImPlot::SetNextLineStyle(haveProc ? Alpha(rawCol, 0.45f) : rawCol, haveProc ? S(1.f) : lineW);
    ImPlot::PlotLine("Исходный", &rows[0].t, axis == 0 ? &rows[0].calcX : &rows[0].calcY, static_cast<int>(rows.size()), 0, 0,
                     static_cast<int>(sizeof(csvlog::Row)));
    if (haveProc)
    {
        ImPlot::SetNextLineStyle(pal.accent, lineW);
        ImPlot::PlotLine("Обработанный", proc_.procT[post].data(), proc_.procY[post].data(),
                         static_cast<int>(std::min(proc_.procT[post].size(), proc_.procY[post].size())));
    }

    // Средний крен положений, номера и уровни
    for (std::size_t i = 0; i < proc_.pos.size(); i++)
    {
        const auto& p = proc_.pos[i];
        const double level = p.Level(post);
        const ImVec4 c = p.use ? kMeasColors[i % 10] : pal.muted;
        const double t0 = std::min(p.t0, p.t1), t1 = std::max(p.t0, p.t1);
        if (std::isfinite(level))
        {
            const double xs[2] = {t0, t1}, ys[2] = {level, level};
            ImPlot::SetNextLineStyle(Mix(c, darkTheme ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), 0.15f), S(2.6f));
            ImPlot::PlotLine(Fmt("##lvl%d", static_cast<int>(i)).c_str(), xs, ys, 2);
        }
        ImPlot::PushStyleColor(ImPlotCol_InlayText, Mix(c, pal.text, 0.35f));
        {
            FontScope fb(fontBold);
            ImPlot::PlotText(Fmt("%d", static_cast<int>(i) + 1).c_str(), 0.5 * (t0 + t1), lim.Y.Max,
                             ImVec2(0, S(11)));
        }
        if (std::isfinite(level))
        {
            FontScope fs(fontSmall);
            const std::string lv = text::Angle(level, 2);
            if (ImPlot::PlotToPixels(t1, 0).x - ImPlot::PlotToPixels(t0, 0).x > ImGui::CalcTextSize(lv.c_str()).x + S(4))
                ImPlot::PlotText(lv.c_str(), 0.5 * (t0 + t1), lim.Y.Max, ImVec2(0, S(28)));
        }
        ImPlot::PopStyleColor();
    }

    if (!forExport)
    {
        // Границы выбранного положения — перетаскиваются
        if (proc_.sel >= 0 && proc_.sel < static_cast<int>(proc_.pos.size()))
        {
            auto& p = proc_.pos[static_cast<std::size_t>(proc_.sel)];
            for (int k = 0; k < 2; k++)
            {
                double* t = k == 0 ? &p.t0 : &p.t1;
                if (ImPlot::DragLineX(k, t, pal.accent, S(2.f)))
                {
                    *t = std::clamp(*t, tBeg, tEnd);
                    proc_.posEdited = true;
                    proc_.dirty = true;
                }
            }
        }
        // Щелчок — выбрать положение под курсором (или снять выбор)
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
        {
            const double x = ImPlot::GetPlotMousePos().x;
            const double tol = 6.0 * (lim.X.Max - lim.X.Min) / std::max(1.f, ImPlot::GetPlotSize().x);
            bool nearBorder = false;
            if (proc_.sel >= 0 && proc_.sel < static_cast<int>(proc_.pos.size()))
            {
                const auto& p = proc_.pos[static_cast<std::size_t>(proc_.sel)];
                nearBorder = std::fabs(x - p.t0) < tol || std::fabs(x - p.t1) < tol;
            }
            if (!nearBorder)
            {
                proc_.sel = -1;
                for (std::size_t i = 0; i < proc_.pos.size(); i++)
                    if (x >= std::min(proc_.pos[i].t0, proc_.pos[i].t1) && x <= std::max(proc_.pos[i].t0, proc_.pos[i].t1))
                        proc_.sel = static_cast<int>(i);
            }
        }
    }
    ImPlot::EndPlot();
}

// Картинка всей записи поста для отчёта: заголовок, итог, график с положениями.
void App::ProcExportSeries(int post, float w, float h)
{
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    const heel::File& f = (*proc_.files)[static_cast<std::size_t>(m.post[post].file)];
    {
        FontScope fh(fontH2);
        ImGui::Text("Пост «%s» — запись опыта, крен по оси %s", PostTitle(post).c_str(), settings_.heelAxis == 0 ? "X" : "Y");
    }
    {
        FontScope fs(fontSmall);
        const auto& s = proc_.posSum;
        const char* mu = settings_.heelSmall ? "кг" : "т";
        Muted("%s · D = %s %s, P = %s %s · положений %d · h = %s м (среднее), по МНК %s м",
              f.name.c_str(), Fmt(NumFmt(settings_.heelD, settings_.digD).c_str(), settings_.heelD).c_str(), mu,
              Fmt(NumFmt(settings_.heelP, settings_.digP).c_str(), settings_.heelP).c_str(), mu,
              static_cast<int>(proc_.pos.size()), std::isfinite(s.h) ? Fmt("%.3f", s.h).c_str() : "—",
              std::isfinite(s.ls.h) ? Fmt("%.3f", s.ls.h).c_str() : "—");
        Muted("Обработка: %s · бледная линия — исходный сигнал, синяя — обработанный, цветная — крен положения",
              filt::Describe(ProcSettings().filter).c_str());
    }
    ImGui::Spacing();
    (void) h;
    ProcSeriesPlot(post, w, ImGui::GetContentRegionAvail().y, true);
}

// «Обработка: метод ▾» — метод оценки крена положения и его параметры (core/Filters.hpp).
void App::ProcFilterButton()
{
    const auto m = static_cast<filt::Method>(std::clamp(settings_.fltMethod, 0, static_cast<int>(filt::Method::Count) - 1));
    // Подпись по месту в строке заголовка (до переключателя режима): полная, короче или только метод
    std::string label = std::string("Обработка: ") + filt::MethodName(m) + " ▼";
    {
        const float room = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight() - S(24) -
                           ImGui::GetCursorScreenPos().x;
        auto width = [](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x + 2 * ImGui::GetStyle().FramePadding.x; };
        if (width(label) > room)
            label = std::string(filt::MethodName(m)) + " ▼";
        if (width(label) > room)
            label = "Обработка ▼";
    }
    if (Button(label.c_str(), BtnKind::Normal))
        ImGui::OpenPopup("##filter");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", filt::Describe(ProcSettings().filter).c_str());
    if (!ImGui::BeginPopup("##filter"))
        return;
    bool changed = false;
    {
        FontScope f(fontBold);
        ImGui::TextUnformatted("Крен положения — метод");
    }
    for (int i = 0; i < filt::kBuiltinCount; i++)
        if (ImGui::RadioButton(filt::MethodName(static_cast<filt::Method>(i)), &settings_.fltMethod, i))
            changed = true;
    ProcLoadMethods();
    if (!proc_.methods.empty())
    {
        FontScope fs(fontSmall);
        Muted("Свои методы (формулы):");
    }
    for (const auto& um : proc_.methods)
    {
        const bool on = settings_.fltMethod == static_cast<int>(filt::Method::User) && settings_.fltUser == um.name;
        if (ImGui::RadioButton(Fmt("%s##um_%s", um.name.c_str(), um.name.c_str()).c_str(), on))
        {
            settings_.fltMethod = static_cast<int>(filt::Method::User);
            settings_.fltUser = um.name;
            changed = true;
        }
    }
    if (Button("Свои методы (формулы)…", BtnKind::Normal, ImVec2(S(260), 0)))
    {
        proc_.methodWin = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
    auto num = [&](const char* label, double* v, double step, const char* fmt, double lo, double hi, const char* tip) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
        ImGui::SameLine(S(250));
        ImGui::SetNextItemWidth(S(140));
        if (InputDoubleStep(label, v, step, step * 10, fmt))
        {
            *v = std::clamp(*v, lo, hi);
            changed = true;
        }
    };
    if (m == filt::Method::MedianEma)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Медиана: последних отсчётов");
        ImGui::SameLine(S(250));
        ImGui::SetNextItemWidth(S(140));
        if (InputIntStep("##medn", &settings_.fltMedianN, 2, 2))
        {
            settings_.fltMedianN = std::clamp(settings_.fltMedianN | 1, 1, 99);
            changed = true;
        }
        num("EMA: частота среза fс, Гц", &settings_.fltFc, 0.05, "%.2f", 0.01, 10.0,
            "α = 2π·fс / (2π·fс + fоп) — формула (12) статьи; fоп — частота опроса по записи");
    }
    if (m == filt::Method::SavGol)
        num("Окно Савицкого–Голая, с", &settings_.fltSgWin, 0.5, "%.1f", 0.5, 30.0, "Полином 2-й степени по этому окну");
    if (m == filt::Method::MedianEma || m == filt::Method::SavGol || m == filt::Method::Kalman)
    {
        // Окно среднего: по умолчанию — весь диапазон положения (Правила, прил. 3-10 и 3-13: ось колебаний — по всей
        // записи перебежки); остальные — для сравнения с прежними версиями и статьёй
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Окно среднего");
        Hint("«Весь диапазон» (по умолчанию с 1.8) — весь выделенный диапазон положения без переходных начала и конца "
             "(крен ещё не успокоился после переноса или уже уходит к следующему; края на графике не закрашены). Так "
             "обрабатывают инклинограмму Правила (Инструктивные указания по кренованию, прил. 3-10 и 3-13: ось колебаний "
             "проводится через всю запись перебежки).\n«Вторая половина» — 1.7.1, «3·Tc в конце» — статья (13), версии "
             "1.4–1.7, «Строго весь» — весь выделенный диапазон без исключения краёв. Сохраняется в проекте.");
        static const char* wl[] = {"Весь диапазон", "Вторая половина", "3·Tc в конце", "Строго весь"};
        static const int wv[] = {0, 1, 2, 3};
        if (Segmented("fwin", &settings_.fltWindow, wl, wv, 4))
            changed = true;
        if (m != filt::Method::Kalman && settings_.fltWindow != 0 && settings_.fltWindow != 3)
            num("Период качки Tc, с (0 — по записи)", &settings_.fltTc, 1.0, "%.0f", 0.0, 120.0,
                "Окно не короче 3·Tc — формула (13) статьи");
    }
    {
        FontScope f(fontSmall);
        ImGui::PushTextWrapPos(S(460));
        Muted("%s", filt::Describe(ProcSettings().filter).c_str());
        ImGui::PopTextWrapPos();
    }
    if (changed)
    {
        proc_.dirty = true;
        SaveSettingsSoon();
    }
    ImGui::EndPopup();
}

// Положение крупно: исходный сигнал (бледно), обработанный (или кривая метода), среднее по обработанному на окне.
void App::ProcPositionPlot(int post, int index, float w, float h, bool forExport)
{
    if (!ProcCont() || !proc_.files || index < 0 || index >= static_cast<int>(proc_.pos.size()))
        return;
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    if (post < 0 || post >= heel::kMaxPosts || m.post[post].file < 0)
        return;
    const heel::File& f = (*proc_.files)[static_cast<std::size_t>(m.post[post].file)];
    const auto& rows = f.log.rows;
    const heel::Position& p = proc_.pos[static_cast<std::size_t>(index)];
    const filt::Estimate& e = p.est[post];
    const int axis = settings_.heelAxis;
    const double t0 = std::min(p.t0, p.t1), t1 = std::max(p.t0, p.t1);
    const double margin = std::max(3.0, 0.12 * (t1 - t0));
    const double a = t0 - margin, b = t1 + margin;
    ProcPlotStyle(forExport);
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const auto& r : rows)
        if (r.t >= t0 && r.t <= t1)
            if (const double v = r.Calc(axis); std::isfinite(v))
            {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
    if (!(hi >= lo))
    {
        lo = -1;
        hi = 1;
    }
    const double pad = std::max((hi - lo) * 0.25, 0.1);
    char id[64];
    std::snprintf(id, sizeof(id), "Положение %d##pos%d_%d_%d", index + 1, post, index, forExport ? 1 : 0);
    ImPlotFlags flags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;
    if (forExport)
        flags |= ImPlotFlags_NoMouseText;
    if (!ImPlot::BeginPlot(id, ImVec2(w, h), flags))
        return;
    ImPlot::SetupAxes("Время, с", "Крен, °", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimits(ImAxis_X1, a, b, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, lo - pad, hi + pad, ImPlotCond_Always);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.2f");
    ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);
    ImPlot::SetupFinish();
    const ImPlotRect lim = ImPlot::GetPlotLimits();
    const ImVec4 c = kMeasColors[static_cast<std::size_t>(index) % 10];
    // Участок положения и окно среднего
    ImPlot::PushPlotClipRect();
    ImDrawList* dl = ImPlot::GetPlotDrawList();
    dl->AddRectFilled(ImPlot::PlotToPixels(t0, lim.Y.Max), ImPlot::PlotToPixels(t1, lim.Y.Min),
                      U32(Alpha(c, darkTheme ? 0.10f : 0.07f)));
    if (std::isfinite(e.winT0) && e.winT1 > e.winT0)
        dl->AddRectFilled(ImPlot::PlotToPixels(e.winT0, lim.Y.Max), ImPlot::PlotToPixels(e.winT1, lim.Y.Min),
                          U32(Alpha(c, darkTheme ? 0.16f : 0.12f)));
    ImPlot::PopPlotClipRect();
    // Исходный сигнал на участке ± поля
    std::size_t i0 = 0, i1 = rows.size();
    while (i0 < rows.size() && rows[i0].t < a)
        i0++;
    while (i1 > i0 && rows[i1 - 1].t > b)
        i1--;
    const ImVec4 rawCol = darkTheme ? ImVec4(0.80f, 0.84f, 0.90f, 1.f) : ImVec4(0.20f, 0.24f, 0.30f, 1.f);
    if (i1 > i0)
    {
        ImPlot::SetNextLineStyle(Alpha(rawCol, 0.45f), S(1.f));
        ImPlot::PlotLine("Исходный сигнал", &rows[i0].t, axis == 0 ? &rows[i0].calcX : &rows[i0].calcY,
                         static_cast<int>(i1 - i0), 0, 0, static_cast<int>(sizeof(csvlog::Row)));
    }
    // Обработанный: весь ряд записи (Медиана + EMA, Савицкий–Голай) на окне графика, иначе — кривая метода по участку
    const auto method = ProcSettings().filter.method;
    const char* procName = method == filt::Method::Kalman       ? "Уровень (Калман)"
                           : method == filt::Method::DampedSine ? "Затухающая синусоида"
                           : method == filt::Method::Mean       ? nullptr
                                                                : "Обработанный сигнал";
    if (procName)
    {
        std::vector<double> xs, ys;
        if (!proc_.procY[post].empty())
        {
            for (std::size_t k = 0; k < proc_.procT[post].size(); k++)
                if (proc_.procT[post][k] >= a && proc_.procT[post][k] <= b)
                {
                    xs.push_back(proc_.procT[post][k]);
                    ys.push_back(proc_.procY[post][k]);
                }
        }
        else
        {
            xs = e.t;
            ys = e.y;
        }
        ImPlot::SetNextLineStyle(pal.accent, S(2.f));
        ImPlot::PlotLine(procName, xs.data(), ys.data(), static_cast<int>(std::min(xs.size(), ys.size())));
    }
    // Среднее по обработанному — на окне, пунктиром продлено на весь участок
    const double level = p.Level(post);
    if (std::isfinite(level))
    {
        const double wa = std::isfinite(e.winT0) && e.winT1 > e.winT0 ? e.winT0 : t0;
        const double wb = std::isfinite(e.winT1) && e.winT1 > e.winT0 ? e.winT1 : t1;
        const std::string lab = Fmt("Среднее %s", text::Angle(level, 3).c_str());
        const double xs[2] = {wa, wb}, ys[2] = {level, level};
        ImPlot::SetNextLineStyle(Mix(c, darkTheme ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), 0.2f), S(3.f));
        ImPlot::PlotLine(lab.c_str(), xs, ys, 2);
        std::vector<double> dx, dy;
        const double perPx = (lim.X.Max - lim.X.Min) / std::max(1.f, ImPlot::GetPlotSize().x);
        for (double x = t0; x < t1; x += 14 * perPx * uiScale)
        {
            dx.insert(dx.end(), {x, std::min(x + 8 * perPx * uiScale, t1), heel::kNaN});
            dy.insert(dy.end(), {level, level, heel::kNaN});
        }
        ImPlot::SetNextLineStyle(Alpha(c, 0.8f), S(1.5f));
        ImPlot::PlotLine(lab.c_str(), dx.data(), dy.data(), static_cast<int>(dx.size()));
        ImPlot::PushStyleColor(ImPlotCol_InlayText, pal.text);
        {
            FontScope fb(fontBold);
            ImPlot::PlotText(Fmt("%d: %s", index + 1, text::Angle(level, 3).c_str()).c_str(), 0.5 * (t0 + t1), lim.Y.Max,
                             ImVec2(0, S(12)));
        }
        ImPlot::PopStyleColor();
    }
    ImPlot::EndPlot();
}

// Все положения поста сеткой (картинка для отчёта): по каждому — исходный, обработанный и среднее.
void App::ProcExportPositions(int post, float w, float h)
{
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    const heel::File& f = (*proc_.files)[static_cast<std::size_t>(m.post[post].file)];
    {
        FontScope fh(fontH2);
        ImGui::Text("Пост «%s» — положения груза, крен по оси %s", PostTitle(post).c_str(),
                    settings_.heelAxis == 0 ? "X" : "Y");
    }
    {
        FontScope fs(fontSmall);
        const std::string line = f.name + " · обработка: " + filt::Describe(ProcSettings().filter) +
                                 " · бледная линия — исходный сигнал, синяя — обработанный, толстая — среднее по "
                                 "обработанному на окне (выделено), пунктир — оно же на всём положении";
        ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    const int n = static_cast<int>(proc_.pos.size());
    const int cols = 2, rowsN = (n + cols - 1) / cols;
    const float gap = S(10);
    const float cw = std::floor((w - gap) / cols);
    const float ch = std::floor((ImGui::GetContentRegionAvail().y - gap * static_cast<float>(rowsN - 1) - S(24)) /
                                static_cast<float>(std::max(1, rowsN)));
    (void) h;
    for (int i = 0; i < n; i++)
    {
        if (i % cols)
            ImGui::SameLine(0, gap);
        else if (i)
            ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        ProcPositionPlot(post, i, cw, ch, true);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Датчики и посты: у каждого датчика — пост со своим названием (датчики заменяют вески: нос, корма, мидель, …)
// ---------------------------------------------------------------------------------------------------------------

namespace
{

// «2=Нос;3=Корма;101=Мидель» <-> карта (в названиях «;» и «=» заменяются на «,» и «-»).
std::map<int, std::string> ParseNames(const std::string& s)
{
    std::map<int, std::string> out;
    std::size_t a = 0;
    while (a < s.size())
    {
        std::size_t b = s.find(';', a);
        if (b == std::string::npos)
            b = s.size();
        const std::string item = s.substr(a, b - a);
        const std::size_t eq = item.find('=');
        if (eq != std::string::npos && eq > 0)
            out[std::atoi(item.substr(0, eq).c_str())] = item.substr(eq + 1, 60);
        a = b + 1;
    }
    return out;
}

std::string JoinNames(const std::map<int, std::string>& m)
{
    std::string s;
    for (const auto& [k, v] : m)
    {
        std::string clean = v;
        for (auto& c : clean)
            c = c == ';' ? ',' : c == '=' ? '-' : c;
        s += (s.empty() ? "" : ";") + std::to_string(k) + "=" + clean;
    }
    return s;
}

} // namespace

std::string App::SensorTitle(int key) const
{
    if (const auto it = proc_.names.find(key); it != proc_.names.end() && !it->second.empty())
        return it->second;
    const int d = heel::DefaultPost(key);
    if (d == heel::kBow)
        return "Нос";
    if (d == heel::kStern)
        return "Корма";
    return heel::SensorShort(key);
}

std::string App::SensorTitleOfPost(int post) const
{
    for (const auto& [key, p] : proc_.postOf)
        if (p == post)
            return SensorTitle(key);
    return heel::DefaultPostName(post);
}

std::string App::PostTitle(int post) const
{
    std::string sens;
    for (const auto& [key, p] : proc_.postOf)
        if (p == post)
            sens += (sens.empty() ? "" : ", ") + heel::SensorShort(key);
    return SensorTitleOfPost(post) + (sens.empty() ? std::string() : " (" + sens + ")");
}

// Датчики -> посты по порядку (Д2 и старый датчик 1 — первыми), снятые с расчёта — -1; постов не больше kMaxPosts.
void App::ProcLoadNames()
{
    if (!proc_.namesLoaded)
    {
        proc_.names = ParseNames(settings_.sensorNames);
        proc_.namesLoaded = true;
    }
}

void App::ProcAssignPosts()
{
    ProcLoadNames();
    std::vector<int> keys = proc_.keys;
    std::stable_sort(keys.begin(), keys.end(), [](int a, int b) {
        auto rank = [](int k) {
            const int d = heel::DefaultPost(k);
            return d < 0 ? 10 : d;
        };
        return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
    });
    proc_.postOf.clear();
    int next = 0;
    for (const int k : keys)
    {
        if (proc_.skip.count(k) || next >= heel::kMaxPosts)
        {
            if (!proc_.skip.count(k))
                ProcLog(heel::SensorName(k) + ": постов больше " + std::to_string(heel::kMaxPosts) + " — не учитывается");
            proc_.postOf[k] = -1;
            continue;
        }
        proc_.postOf[k] = next++;
    }
}

std::vector<int> App::PostsInUse() const
{
    std::vector<int> out;
    for (int p = 0; p < heel::kMaxPosts; p++)
    {
        bool any = false;
        if (ProcCont())
            any = proc_.meas[static_cast<std::size_t>(proc_.rec)].post[p].file >= 0;
        else
            for (const auto& m : proc_.meas)
                any |= m.post[p].file >= 0;
        if (any)
            out.push_back(p);
    }
    return out;
}

void App::ProcLoadInvert()
{
    if (proc_.invertLoaded)
        return;
    proc_.invertLoaded = true;
    proc_.invert.clear();
    const std::string& s = settings_.sensorInvert;
    std::size_t at = 0;
    while (at < s.size())
    {
        std::size_t end = s.find(',', at);
        if (end == std::string::npos)
            end = s.size();
        if (end > at)
            proc_.invert.insert(std::atoi(s.substr(at, end - at).c_str()));
        at = end + 1;
    }
}

void App::ProcApplyInvert()
{
    ProcLoadInvert();
    if (!proc_.filesRaw)
    {
        proc_.files.reset();
        return;
    }
    bool any = false;
    for (const auto& f : *proc_.filesRaw)
        any |= proc_.invert.count(f.sensor) > 0;
    proc_.files = any ? std::make_shared<const std::vector<heel::File>>(heel::ApplyInvert(*proc_.filesRaw, proc_.invert))
                      : proc_.filesRaw;
}

void App::ProcessSetSensorInvert(int key, bool inverted)
{
    ProcLoadInvert();
    if (inverted)
        proc_.invert.insert(key);
    else
        proc_.invert.erase(key);
    std::string s;
    for (const int k : proc_.invert)
        s += (s.empty() ? "" : ",") + std::to_string(k);
    settings_.sensorInvert = s;
    SaveSettingsSoon();
    ProcApplyInvert();
    if (proc_.files)
        ProcLog(heel::SensorName(key) + (inverted ? ": знак обратный (ось +X — к левому борту)" : ": знак прямой"));
    proc_.dirty = true;
}

void App::ProcessSetPositionSpans(const std::vector<std::pair<double, double>>& spans)
{
    proc_.pos.clear();
    for (const auto& [a, b] : spans)
    {
        heel::Position p;
        p.t0 = a;
        p.t1 = b;
        proc_.pos.push_back(p);
    }
    proc_.sel = -1;
    proc_.posEdited = true;
    proc_.dirty = true;
}

void App::SaveSensorNames()
{
    settings_.sensorNames = JoinNames(proc_.names);
    SaveSettingsSoon();
    ProcRefreshLogNames();
}

std::string App::ProcSummaryText() const
{
    int used = 0;
    std::set<std::string> formats;
    bool byTime = false;
    if (proc_.files)
        for (const auto& m : proc_.meas)
            for (const auto& pd : m.post)
                if (pd.file >= 0)
                {
                    used++;
                    const auto& f = (*proc_.files)[static_cast<std::size_t>(pd.file)];
                    formats.insert(csvlog::FormatName(f.log.format));
                    byTime |= f.log.time != csvlog::TimeSource::Ms;
                }
    std::string fmt;
    for (const auto& x : formats)
        fmt += (fmt.empty() ? "" : ", ") + x;
    std::string posts;
    for (const int key : proc_.keys)
        if (const auto it = proc_.postOf.find(key); it != proc_.postOf.end() && it->second >= 0)
            posts += (posts.empty() ? "" : ", ") + heel::SensorShort(key) + " — «" + SensorTitle(key) + "»";
    // Перенос перед скобкой: иначе ImGui переносит строку по точке внутри «1.4+»
    return Fmt("Замеров: %d, файлов: %d\n(%s; %s)%s", static_cast<int>(proc_.meas.size()), used, posts.c_str(), fmt.c_str(),
               byTime ? "; без Ms — время по столбцу Time" : "");
}

std::string App::ProcRecordText() const
{
    if (!ProcCont() || !proc_.files)
        return {};
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    const int mp = heel::MainPost(m);
    if (mp < 0)
        return {};
    const auto& f = (*proc_.files)[static_cast<std::size_t>(m.post[mp].file)];
    return Fmt("Одна запись — весь опыт: %s (%s), %.0f с; положений груза: %d (ось %s)", f.name.c_str(),
               PostTitle(mp).c_str(), f.log.Duration(), static_cast<int>(proc_.pos.size()), settings_.heelAxis == 0 ? "X" : "Y");
}

// Переименовали датчик — строки журнала с постами (последние) пересобираются с текущими названиями.
void App::ProcRefreshLogNames()
{
    auto refresh = [&](const char* prefix, const std::string& text) {
        if (text.empty())
            return;
        for (auto it = proc_.log.rbegin(); it != proc_.log.rend(); ++it)
            if (it->first.rfind(prefix, 0) == 0)
            {
                it->first = text;
                return;
            }
    };
    refresh("Замеров: ", ProcSummaryText());
    refresh("Одна запись — весь опыт: ", ProcRecordText());
}

// «Датчики: Д2 [Нос ▼]  Д3 [Корма ▼]» — название поста каждого датчика (своё или из списка) и «учитывать».
void App::ProcSensorNames(float right, const std::function<void(float)>& flow)
{
    (void) right;
    const std::vector<int> keys = proc_.keys; // копия: смена учёта пересобирает proc_.keys
    if (keys.empty())
        return;
    const float fw = S(128);
    float need = ImGui::CalcTextSize("Датчики:").x;
    for (const int k : keys)
        need += S(12) + ImGui::CalcTextSize(heel::SensorShort(k).c_str()).x + S(6) + fw + ImGui::GetFrameHeight();
    flow(need);
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Датчики:");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Название поста каждого датчика: где он стоит на судне (нос, корма, мидель-шпангоут, …).\n"
                          "Датчики заменяют вески: любой может стоять где угодно. Название — в таблице, на графиках\n"
                          "и в отчёте; запоминается. ▼ — готовые названия и «не учитывать».");
    bool regroup = false;
    for (const int k : keys)
    {
        ImGui::SameLine(0, S(12));
        ImGui::AlignTextToFramePadding();
        const bool off = proc_.skip.count(k) > 0;
        const int addr = k >= 100 ? k - 100 : k;
        TextColored(off ? pal.muted : k > 0 && k < 100 && (addr == 2 || addr == 3) ? pal.sensor[addr - 2] : pal.text, "%s",
                    heel::SensorShort(k).c_str());
        if (ImGui::IsItemHovered())
        {
            std::string tip = heel::SensorName(k);
            if (k == heel::kNoSensor && proc_.files)
            {
                tip = "Файлы без номера датчика в имени (не «…_Dk», не «M_NNN_k»):";
                int shown = 0;
                for (const auto& f : *proc_.files)
                    if (f.sensor == heel::kNoSensor && f.log.error.empty() && shown++ < 12)
                        tip += "\n  " + f.name;
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::SameLine(0, S(6));
        ImGui::PushID(k);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", SensorTitle(k).c_str());
        ImGui::SetNextItemWidth(fw);
        if (off)
            ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
        if (ImGui::InputText("##name", buf, sizeof(buf)))
        {
            proc_.names[k] = buf;
            SaveSensorNames();
            proc_.dirty = true;
        }
        if (off)
            ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        if (ImGui::ArrowButton("##pick", ImGuiDir_Down))
            ImGui::OpenPopup("##names");
        if (ImGui::BeginPopup("##names"))
        {
            for (const char* n : kPostPresets)
                if (ImGui::Selectable(n, SensorTitle(k) == n))
                {
                    proc_.names[k] = n;
                    SaveSensorNames();
                    proc_.dirty = true;
                }
            ImGui::Separator();
            bool inv = proc_.invert.count(k) > 0;
            if (ui::Checkbox("Обратный знак (ось +X — к левому борту)", &inv))
                ProcessSetSensorInvert(k, inv);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Датчик поставлен «задом наперёд»: крен на правый борт он показывает с минусом.\n"
                                  "Углы его файлов (X и Y) берутся с обратным знаком — файлы не меняются.\n"
                                  "Проверка: груз к правому борту должен давать крен «+» (правый борт).");
            bool use = !off;
            if (ui::Checkbox("Учитывать в расчёте", &use))
            {
                if (use)
                    proc_.skip.erase(k);
                else
                    proc_.skip.insert(k);
                regroup = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    if (regroup)
    {
        ProcRegroup(true);
        if (proc_.mode == 1 && !ProcCont())
            ProcChooseMode(false);
        ProcRecompute();
        ProcRefreshLogNames();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Свои методы обработки — формулы (core/Expr.hpp, core/UserMethod.hpp)
// ---------------------------------------------------------------------------------------------------------------

namespace
{

// Имя файла метода из его названия (знаки, недопустимые в имени файла, — «_»).
std::string MethodFileName(const std::string& name)
{
    std::string s;
    for (const char c : name)
        s += (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
              static_cast<unsigned char>(c) < 0x20)
                 ? '_'
                 : c;
    return (s.empty() ? std::string("метод") : s) + ".krenomer-method";
}

// Поле ввода строки (буфер — на кадр; ImGui держит свой буфер, пока поле в фокусе).
bool InputStr(const char* id, std::string& v, float width, std::size_t maxLen = 1000)
{
    std::vector<char> buf(maxLen + 1, 0);
    std::snprintf(buf.data(), buf.size(), "%s", v.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buf.data(), buf.size()))
    {
        v = buf.data();
        return true;
    }
    return false;
}

const char* kSyntaxHelp =
    "Стадия: y[n] = формула. x — вход стадии (у первой — исходный крен), x[n-k], x[k], y[n-k] (k ≥ 1) — прошлые значения;\n"
    "n — номер отсчёта, t — время, с; fs — частота опроса, Гц; dt = 1/fs; Tc — период качки, с; pi; константы метода.\n"
    "Действия: + − * / ^, скобки, < <= > >= == != (1 или 0), and or not, условие  c ? a : b  или  if(c, a, b).\n"
    "Функции: abs sqrt exp ln log10 sin cos tan atan atan2 pow min max mean floor ceil round sign clamp.\n"
    "Ряды по входу стадии: median(x, N) — медиана N последних, sma(x, N) — среднее N последних, ema(x, α).\n"
    "Начальное значение — y[n-k] до начала записи (по умолчанию x[0]). Выход стадии — вход следующей.\n"
    "Среднее: по окну последних T с положения (window), по всему положению (all) или формулой (expr) с\n"
    "avg(), avgw(T), med(), medw(T), last(), T0, T1. Нечисловой результат (1/0) заменяется предыдущим значением.\n"
    "Пример — метод статьи: const fc = 0.5; const a = 2*pi*fc/(2*pi*fc + fs); стадии median(x, 3) и\n"
    "a*x + (1 - a)*y[n-1]; среднее — window max(3*Tc, (T1 - T0)/2).\n"
    "T0, T1 — начало и конец установившегося крена положения (без переходных начала и конца участка).";

} // namespace

void App::ProcLoadMethods()
{
    if (proc_.methodsLoaded)
        return;
    proc_.methodsLoaded = true;
    if (!opt_.useSettingsFile)
        return;
    std::error_code ec;
    const fs::path dir = PcSettings::Dir() / L"methods";
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec) || it->path().extension() != L".krenomer-method")
            continue;
        std::ifstream in(it->path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        um::Method m;
        if (um::FromText(text, m))
            proc_.methods.push_back(m);
    }
    std::sort(proc_.methods.begin(), proc_.methods.end(), [](const um::Method& a, const um::Method& b) { return a.name < b.name; });
}

void App::ProcSaveMethod(const um::Method& m)
{
    if (!opt_.useSettingsFile)
        return;
    std::error_code ec;
    const fs::path dir = PcSettings::Dir() / L"methods";
    fs::create_directories(dir, ec);
    const fs::path file = dir / text::PathFromUtf8(MethodFileName(m.name));
    fs::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << um::ToText(m);
        if (!out)
            return;
    }
    fs::rename(tmp, file, ec);
}

void App::ProcDeleteMethodFile(const std::string& name)
{
    if (!opt_.useSettingsFile)
        return;
    std::error_code ec;
    fs::remove(PcSettings::Dir() / L"methods" / text::PathFromUtf8(MethodFileName(name)), ec);
}

std::shared_ptr<const um::Compiled> App::ProcUserCompiled(const um::Method& m, um::Problem* problem) const
{
    um::Problem pr;
    auto c = um::Build(m, pr);
    if (problem)
        *problem = pr;
    return c;
}

void App::ProcessUseUserMethod(const um::Method& m, bool apply)
{
    ProcLoadMethods();
    bool found = false;
    for (std::size_t i = 0; i < proc_.methods.size(); i++)
        if (proc_.methods[i].name == m.name)
        {
            proc_.methods[i] = m;
            proc_.methodSel = static_cast<int>(i);
            found = true;
        }
    if (!found)
    {
        proc_.methods.push_back(m);
        proc_.methodSel = static_cast<int>(proc_.methods.size()) - 1;
    }
    if (!apply)
        return;
    settings_.fltMethod = static_cast<int>(filt::Method::User);
    settings_.fltUser = m.name;
    proc_.dirty = true;
}

// Предпросмотр на выбранном положении (или первом): исходный, текущий метод расчёта и свой метод — линии и средние.
void App::ProcMethodPreview(const um::Method& m, float w, float h)
{
    if (!ProcCont() || !proc_.files || proc_.pos.empty())
    {
        MutedWrapped("Предпросмотр — на записи всего опыта: откройте папку с записью (режим «Весь опыт одной записью») "
                     "и выберите положение в таблице.");
        return;
    }
    const int idx = proc_.sel >= 0 && proc_.sel < static_cast<int>(proc_.pos.size()) ? proc_.sel : std::min<int>(1, static_cast<int>(proc_.pos.size()) - 1);
    const auto& rec = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    const int post = ProcShownPost();
    if (post < 0 || rec.post[post].file < 0)
        return;
    const auto& rows = (*proc_.files)[static_cast<std::size_t>(rec.post[post].file)].log.rows;
    std::vector<double> t, x;
    heel::Series((*proc_.files)[static_cast<std::size_t>(rec.post[post].file)].log, settings_.heelAxis, t, x);
    const heel::Position& p = proc_.pos[static_cast<std::size_t>(idx)];
    const double t0 = std::min(p.t0, p.t1), t1 = std::max(p.t0, p.t1);
    // Свой метод
    um::Problem pr;
    const auto c = ProcUserCompiled(m, &pr);
    filt::Estimate mine;
    std::vector<double> mineY;
    if (c)
    {
        filt::Params fp = ProcSettings().filter;
        fp.method = filt::Method::User;
        fp.custom = c;
        mineY = filt::ProcessSeries(t, x, fp);
        mine = filt::EstimateSpan(t, x, mineY, t0, t1, fp);
    }
    const filt::Estimate& cur = p.est[post];
    {
        FontScope f(fontSmall);
        Muted("Положение %d, %s · текущий метод (%s): %s · свой: %s · разница %s", idx + 1, PostTitle(post).c_str(),
              filt::MethodName(ProcSettings().filter.method), text::Angle(p.Level(post), 3).c_str(),
              text::Angle(mine.level, 3).c_str(),
              std::isfinite(mine.level) && std::isfinite(p.Level(post)) ? Fmt("%.3f°", mine.level - p.Level(post)).c_str() : "—");
    }
    ProcPlotStyle(false);
    const double margin = std::max(3.0, 0.12 * (t1 - t0));
    double lo = 1e300, hi = -1e300;
    for (std::size_t i = 0; i < t.size(); i++)
        if (t[i] >= t0 && t[i] <= t1 && std::isfinite(x[i]))
        {
            lo = std::min(lo, x[i]);
            hi = std::max(hi, x[i]);
        }
    if (!(hi >= lo))
    {
        lo = -1;
        hi = 1;
    }
    const double pad = std::max((hi - lo) * 0.2, 0.1);
    if (!ImPlot::BeginPlot("##umprev", ImVec2(w, h - ImGui::GetTextLineHeightWithSpacing()), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect))
        return;
    ImPlot::SetupAxes("Время, с", "Крен, °", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimits(ImAxis_X1, t0 - margin, t1 + margin, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, lo - pad, hi + pad, ImPlotCond_Always);
    ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);
    ImPlot::SetupFinish();
    auto window = [&](const std::vector<double>& ys, const ImVec4& col, const char* label, int stride, const double* ts) {
        std::vector<double> xs2, ys2;
        for (std::size_t i = 0; i < ys.size(); i++)
        {
            const double tt = ts ? ts[i * static_cast<std::size_t>(stride)] : t[i];
            if (tt >= t0 - margin && tt <= t1 + margin)
            {
                xs2.push_back(tt);
                ys2.push_back(ys[i]);
            }
        }
        ImPlot::SetNextLineStyle(col, S(1.8f));
        ImPlot::PlotLine(label, xs2.data(), ys2.data(), static_cast<int>(xs2.size()));
    };
    const ImVec4 rawCol = darkTheme ? ImVec4(0.80f, 0.84f, 0.90f, 0.45f) : ImVec4(0.20f, 0.24f, 0.30f, 0.45f);
    {
        std::vector<double> xs2, ys2;
        for (std::size_t i = 0; i < rows.size(); i++)
            if (t[i] >= t0 - margin && t[i] <= t1 + margin)
            {
                xs2.push_back(t[i]);
                ys2.push_back(x[i]);
            }
        ImPlot::SetNextLineStyle(rawCol, S(1.f));
        ImPlot::PlotLine("Исходный", xs2.data(), ys2.data(), static_cast<int>(xs2.size()));
    }
    const ImVec4 curCol = pal.accent, mineCol = ImVec4(0.90f, 0.45f, 0.10f, 1.f);
    if (!proc_.procY[post].empty())
        window(proc_.procY[post], curCol, "Текущий метод", 1, nullptr);
    else if (!cur.y.empty())
    {
        ImPlot::SetNextLineStyle(curCol, S(1.8f));
        ImPlot::PlotLine("Текущий метод", cur.t.data(), cur.y.data(), static_cast<int>(std::min(cur.t.size(), cur.y.size())));
    }
    if (!mineY.empty())
        window(mineY, mineCol, Fmt("«%s»", m.name.c_str()).c_str(), 1, nullptr);
    auto level = [&](double v, double a, double b, const ImVec4& col, const char* label) {
        if (!std::isfinite(v))
            return;
        const bool win = std::isfinite(a) && std::isfinite(b) && b > a;
        const double xs2[2] = {win ? a : t0, win ? b : t1}, ys2[2] = {v, v};
        ImPlot::SetNextLineStyle(col, S(3.f));
        ImPlot::PlotLine(label, xs2, ys2, 2);
    };
    level(p.Level(post), cur.winT0, cur.winT1, Mix(curCol, ImVec4(0, 0, 0, 1), 0.25f),
          Fmt("Среднее текущего %s", text::Angle(p.Level(post), 3).c_str()).c_str());
    level(mine.level, mine.winT0, mine.winT1, Mix(mineCol, ImVec4(0, 0, 0, 1), 0.2f),
          Fmt("Среднее своего %s", text::Angle(mine.level, 3).c_str()).c_str());
    ImPlot::EndPlot();
}

// Окно «Свой метод обработки»: список методов, редактор (константы, стадии, правило среднего), ошибки с местом,
// предпросмотр на положении, файлы .krenomer-method (импорт / экспорт).
void App::ProcMethodWindow()
{
    if (!proc_.methodWin)
        return;
    ProcLoadMethods();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(ds.x - S(40), S(1180)), std::min(ds.y - S(40), S(740))), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.bg);
    if (!ImGui::Begin("Свой метод обработки (формулы)##umwin", &proc_.methodWin, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = S(12), lw = S(250);
    // Список методов
    if (BeginCard("##umlist", ImVec2(lw, avail.y)))
    {
        CardTitle("Методы", "Свои методы — в расчёте вместе со встроенными (кнопка «Обработка: … ▼»)");
        for (int i = 0; i < static_cast<int>(proc_.methods.size()); i++)
        {
            const bool used = settings_.fltMethod == static_cast<int>(filt::Method::User) &&
                              settings_.fltUser == proc_.methods[static_cast<std::size_t>(i)].name;
            ImGui::PushID(i);
            if (ImGui::Selectable(Fmt("%s%s", used ? "● " : "", proc_.methods[static_cast<std::size_t>(i)].name.c_str()).c_str(),
                                  proc_.methodSel == i))
                proc_.methodSel = i;
            ImGui::PopID();
        }
        if (proc_.methods.empty())
            SmallMuted("Своих методов пока нет — создайте из готового ниже.");
        ImGui::Spacing();
        {
            FontScope f(fontSmall);
            Muted("Новый из готового:");
        }
        const struct
        {
            const char* label;
            um::Method (*make)();
        } presets[] = {{"Медиана + EMA (статья)", um::PresetArticle},
                       {"Простое среднее", um::PresetMean},
                       {"Пример из трёх стадий", um::PresetExample}};
        for (const auto& pr : presets)
            if (Button(pr.label, BtnKind::Normal, ImVec2(-FLT_MIN, 0)))
            {
                um::Method m = pr.make();
                std::string base = m.name;
                for (int k = 2; std::any_of(proc_.methods.begin(), proc_.methods.end(),
                                            [&](const um::Method& q) { return q.name == m.name; });
                     k++)
                    m.name = base + " " + std::to_string(k);
                proc_.methods.push_back(m);
                proc_.methodSel = static_cast<int>(proc_.methods.size()) - 1;
            }
        ImGui::Spacing();
        if (Button("Импорт…", BtnKind::Normal, ImVec2(-FLT_MIN, 0)))
        {
            std::string path;
            if (dialogs::PickFile(path, L"Метод обработки (*.krenomer-method)\0*.krenomer-method\0Все файлы\0*.*\0",
                                  L"Метод обработки из файла"))
            {
                std::ifstream in(text::PathFromUtf8(path), std::ios::binary);
                const std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                um::Method m;
                std::string err;
                if (um::FromText(txt, m, &err))
                {
                    proc_.methods.push_back(m);
                    proc_.methodSel = static_cast<int>(proc_.methods.size()) - 1;
                    proc_.methodMsg = "✓ Импортирован «" + m.name + "»";
                }
                else
                    proc_.methodMsg = "✗ Файл не прочитан: " + err;
            }
        }
        if (!proc_.methodMsg.empty())
        {
            FontScope f(fontSmall);
            ImGui::PushStyleColor(ImGuiCol_Text, proc_.methodMsg.rfind("✗", 0) == 0 ? pal.err : pal.ok);
            ImGui::TextWrapped("%s", proc_.methodMsg.c_str());
            ImGui::PopStyleColor();
        }
    }
    EndCard();
    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    const float rw = avail.x - lw - gap;
    if (proc_.methodSel < 0 || proc_.methodSel >= static_cast<int>(proc_.methods.size()))
    {
        if (BeginCard("##umempty", ImVec2(rw, avail.y)))
        {
            CardTitle("Как писать формулы");
            FontScope f(fontSmall);
            ImGui::TextUnformatted(kSyntaxHelp);
        }
        EndCard();
        ImGui::EndGroup();
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }
    um::Method& m = proc_.methods[static_cast<std::size_t>(proc_.methodSel)];
    const std::string oldName = m.name;
    um::Problem problem;
    const auto compiled = ProcUserCompiled(m, &problem);
    const float editH = std::floor(avail.y * 0.6f);
    if (BeginCard("##umedit", ImVec2(rw, editH)))
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Название");
        ImGui::SameLine(0, S(8));
        InputStr("##umname", m.name, S(300), 80);
        ImGui::SameLine(0, S(8));
        Hint(kSyntaxHelp);
        const float bw = S(118);
        ImGui::SameLine(rw - S(28) - 3 * bw - 2 * S(8));
        if (Button("В расчёт", BtnKind::Primary, ImVec2(bw, 0), compiled == nullptr, "Сначала исправьте ошибку"))
        {
            ProcSaveMethod(m);
            settings_.fltMethod = static_cast<int>(filt::Method::User);
            settings_.fltUser = m.name;
            SaveSettingsSoon();
            proc_.dirty = true;
            proc_.methodMsg = "✓ «" + m.name + "» — метод расчёта";
        }
        ImGui::SameLine(0, S(8));
        if (Button("Экспорт…", BtnKind::Normal, ImVec2(bw, 0)))
        {
            std::string path = MethodFileName(m.name);
            if (dialogs::SaveFile(path, L"Метод обработки (*.krenomer-method)\0*.krenomer-method\0", L"Сохранить метод в файл",
                                  L"krenomer-method"))
            {
                std::ofstream out(text::PathFromUtf8(path), std::ios::binary | std::ios::trunc);
                out << um::ToText(m);
                proc_.methodMsg = out ? "✓ Сохранён в файл" : "✗ Файл не записан";
            }
        }
        ImGui::SameLine(0, S(8));
        if (Button("Удалить", BtnKind::Danger, ImVec2(bw, 0)))
        {
            ProcDeleteMethodFile(m.name);
            if (settings_.fltUser == m.name && settings_.fltMethod == static_cast<int>(filt::Method::User))
                settings_.fltMethod = static_cast<int>(filt::Method::Kalman);
            proc_.methods.erase(proc_.methods.begin() + proc_.methodSel);
            proc_.methodSel = -1;
            proc_.dirty = true;
            EndCard();
            ImGui::EndGroup();
            ImGui::End();
            ImGui::PopStyleColor();
            return;
        }
        ImGui::Spacing();
        const float fw = rw - S(28);
        bool changed = false;
        // Константы
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Константы");
        }
        int delConst = -1;
        for (std::size_t i = 0; i < m.consts.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i) + 100);
            ImGui::AlignTextToFramePadding();
            Muted("const");
            ImGui::SameLine(0, S(6));
            changed |= InputStr("##cn", m.consts[i].first, S(90), 30);
            ImGui::SameLine(0, S(6));
            ImGui::TextUnformatted("=");
            ImGui::SameLine(0, S(6));
            changed |= InputStr("##cv", m.consts[i].second, fw - S(200));
            ImGui::SameLine(0, S(6));
            if (Button("✕", BtnKind::Normal, ImVec2(S(26), 0)))
                delConst = static_cast<int>(i);
            ImGui::PopID();
        }
        if (delConst >= 0)
        {
            m.consts.erase(m.consts.begin() + delConst);
            changed = true;
        }
        if (Button("+ константа", BtnKind::Normal, ImVec2(S(130), 0), static_cast<int>(m.consts.size()) >= um::kMaxConsts))
        {
            m.consts.push_back({"k" + std::to_string(m.consts.size() + 1), "1"});
            changed = true;
        }
        // Стадии
        ImGui::Spacing();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Стадии (выход одной — вход следующей)");
        }
        int delStage = -1;
        for (std::size_t i = 0; i < m.stages.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i) + 200);
            ImGui::AlignTextToFramePadding();
            TextColored(pal.accent, "%d.", static_cast<int>(i) + 1);
            ImGui::SameLine(0, S(6));
            Muted("y[n] =");
            ImGui::SameLine(0, S(6));
            changed |= InputStr("##sf", m.stages[i].formula, fw - S(330));
            ImGui::SameLine(0, S(8));
            Muted("начало");
            ImGui::SameLine(0, S(6));
            changed |= InputStr("##si", m.stages[i].init, S(110));
            ImGui::SameLine(0, S(6));
            if (Button("✕", BtnKind::Normal, ImVec2(S(26), 0)))
                delStage = static_cast<int>(i);
            ImGui::PopID();
        }
        if (delStage >= 0)
        {
            m.stages.erase(m.stages.begin() + delStage);
            changed = true;
        }
        if (Button("+ стадия", BtnKind::Normal, ImVec2(S(130), 0), static_cast<int>(m.stages.size()) >= um::kMaxStages))
        {
            m.stages.push_back({"x", "x[0]"});
            changed = true;
        }
        // Среднее
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Среднее");
        }
        ImGui::SameLine(0, S(10));
        static const char* kinds[] = {"Окно в конце, с", "Всё положение", "Формула"};
        static const int kindValues[] = {0, 1, 2};
        int kind = static_cast<int>(m.mean);
        if (Segmented("umk", &kind, kinds, kindValues, 3))
        {
            m.mean = static_cast<um::MeanKind>(kind);
            if (m.mean == um::MeanKind::Window && m.meanArg.empty())
                m.meanArg = "3*Tc";
            if (m.mean == um::MeanKind::Expr && m.meanArg.find('(') == std::string::npos)
                m.meanArg = "avgw(3*Tc)";
            changed = true;
        }
        if (m.mean != um::MeanKind::All)
        {
            ImGui::SameLine(0, S(10));
            changed |= InputStr("##ma", m.meanArg, std::max(S(160), ImGui::GetContentRegionAvail().x));
        }
        // Ошибка — с местом
        ImGui::Spacing();
        if (!compiled)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.err);
            ImGui::TextWrapped("✗ %s", problem.Text().c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            FontScope f(fontSmall);
            ImGui::PushStyleColor(ImGuiCol_Text, pal.ok);
            ImGui::TextWrapped("✓ Формулы разобраны%s", compiled->nonFinite ? Fmt(" · нечисловых значений заменено: %ld", compiled->nonFinite).c_str() : "");
            ImGui::PopStyleColor();
        }
        if (changed && settings_.fltMethod == static_cast<int>(filt::Method::User) && settings_.fltUser == oldName)
        {
            settings_.fltUser = m.name;
            proc_.dirty = true;
        }
        if (changed || m.name != oldName)
        {
            if (m.name != oldName)
                ProcDeleteMethodFile(oldName);
            ProcSaveMethod(m);
        }
    }
    EndCard();
    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    if (BeginCard("##umprevcard", ImVec2(rw, ImGui::GetContentRegionAvail().y)))
    {
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Предпросмотр на положении");
        }
        const ImVec2 a = ImGui::GetContentRegionAvail();
        ProcMethodPreview(m, a.x, a.y);
    }
    EndCard();
    ImGui::EndGroup();
    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace ui



