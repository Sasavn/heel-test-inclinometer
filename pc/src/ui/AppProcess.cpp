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
#include <limits>
#include <set>
#include <system_error>

#include <imgui.h>
#include <implot.h>

#include "../core/HeelReport.hpp"
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

const char* PostCombo[] = {"Нос", "Корма", "Не учитывать"};

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

// Подсказка в строке заголовка карточки (после последнего элемента), если помещается до x = limit; иначе — короткая,
// иначе — никакой.
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

// Цвета и размеры графиков ImPlot под тему (forExport — свой контекст картинки: размеры по её масштабу).
void PlotStyle(bool forExport)
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
    s.windowS = settings_.heelWinS;
    s.thresholdDeg = settings_.heelThr;
    s.axis = settings_.heelAxis;
    s.minPlateauS = settings_.heelPlateauS;
    s.minStepDeg = settings_.heelStep;
    return s;
}

void App::ProcessSetMode(int mode)
{
    proc_.mode = mode == 1 ? 1 : 0;
    proc_.modeUser = true;
    ProcChooseMode(false);
}

void App::ProcessSetPositionArm(int index, double arm)
{
    if (index >= 0 && index < static_cast<int>(proc_.pos.size()))
    {
        proc_.pos[static_cast<std::size_t>(index)].arm = std::fabs(arm);
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

void App::ProcFindPositions()
{
    proc_.pos.clear();
    proc_.sel = -1;
    proc_.posEdited = false;
    if (!ProcCont() || !proc_.files)
        return;
    const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
    proc_.pos = heel::FindPositions(ProcSettings(), *proc_.files, m);
    const int mp = heel::MainPost(m);
    if (mp >= 0)
    {
        const auto& f = (*proc_.files)[static_cast<std::size_t>(m.post[mp].file)];
        ProcLog(Fmt("Одна запись — весь опыт: %s (%s), %.0f с; найдено положений груза: %d (ось %s)", f.name.c_str(),
                    heel::kPostName[mp], f.log.Duration(), static_cast<int>(proc_.pos.size()),
                    settings_.heelAxis == 0 ? "X" : "Y"));
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

void App::ProcLog(const std::string& s)
{
    proc_.log.push_back(s);
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
    proc_.arm[number] = std::fabs(arm);
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
    // Посты по умолчанию для новых датчиков: Д2 — нос, Д3 — корма (старые имена: 1 — нос, 2 — корма)
    for (const auto& f : *proc_.files)
        if (f.Measurement() && f.sensor >= 0 && !proc_.postOf.count(f.sensor))
            proc_.postOf[f.sensor] = heel::DefaultPost(f.sensor);
    std::set<int> keys;
    for (const auto& f : *proc_.files)
        if (f.Measurement() && f.sensor >= 0)
            keys.insert(f.sensor);
    proc_.keys.assign(keys.begin(), keys.end());
    std::vector<std::string> notes;
    proc_.meas = heel::Group(*proc_.files, proc_.postOf, &notes);
    if (!quiet)
        for (const auto& n : notes)
            ProcLog(n);
    proc_.dirty = true;
}

void App::ProcRecompute()
{
    proc_.dirty = false;
    if (!proc_.files)
    {
        proc_.sum = heel::Summary{};
        return;
    }
    for (auto& m : proc_.meas)
    {
        const auto off = proc_.off.find(m.number);
        m.use = off == proc_.off.end() || !off->second;
        const auto a = proc_.arm.find(m.number);
        m.arm = a != proc_.arm.end() ? a->second : settings_.heelL;
    }
    proc_.sum = heel::Compute(ProcSettings(), *proc_.files, proc_.meas);
    if (ProcCont())
        proc_.posSum = heel::ComputePositions(ProcSettings(), *proc_.files, proc_.meas[static_cast<std::size_t>(proc_.rec)],
                                              proc_.pos);
    else
        proc_.posSum = heel::Summary{};
}

void App::TickProcess()
{
    using namespace std::chrono_literals;
    // Чтение файлов закончено
    if (proc_.load.valid() && proc_.load.wait_for(0s) == std::future_status::ready)
    {
        heel::Loaded r = proc_.load.get();
        if (r.folder != proc_.outDir) // другой опыт: плечи и снятые замеры — заново
        {
            proc_.arm.clear();
            proc_.off.clear();
        }
        proc_.outDir = r.folder;
        proc_.files = std::make_shared<const std::vector<heel::File>>(std::move(r.files));
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
        {
            std::string fmt;
            for (const auto& s : formats)
                fmt += (fmt.empty() ? "" : ", ") + s;
            std::string posts;
            for (const int key : proc_.keys)
                if (const int post = proc_.postOf[key]; post >= 0)
                    posts += (posts.empty() ? "" : ", ") + heel::SensorName(key) + " — " +
                             (post == 0 ? "нос" : "корма");
            // Перенос перед скобкой: иначе ImGui переносит строку по точке внутри «1.4+»
            ProcLog(Fmt("Замеров: %d, файлов: %d\n(%s; %s)%s", static_cast<int>(proc_.meas.size()), used, posts.c_str(),
                        fmt.c_str(), byTime ? "; без Ms — время по столбцу Time" : ""));
        }
        else if (!proc_.files->empty())
            ProcLog("Ни один файл не понят как файл замера (что не так — выше)");
        ProcChooseMode(true); // режим по содержимому: одна запись — весь опыт или файл = замер
        ProcRecompute();
    }

    // Картинки графиков — здесь, между кадрами (свой контекст ImGui), книга и запись файлов — в фоне
    if (proc_.saveRequested && !proc_.load.valid() && proc_.files)
    {
        proc_.saveRequested = false;
        if (proc_.dirty)
            ProcRecompute();
        std::shared_ptr<offscreen::Frame> frames[2];
        const bool cont = ProcCont();
        for (int post = 0; post < 2; post++)
        {
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
        }
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
        proc_.save = std::async(std::launch::async, [=]() {
            std::vector<std::string> out;
            std::error_code ec;
            fs::create_directories(dir, ec);
            std::string warn, err;
            const auto sheets = cont ? heel::BuildPositionsReport(s, *files, meas[static_cast<std::size_t>(rec)], pos,
                                                                  sum, info, &warn)
                                     : heel::BuildReport(s, *files, meas, sum, info, &warn);
            if (xlsx::Write(dir / text::PathFromUtf8(heel::kReportName), sheets, &err))
                out.push_back(std::string("✓ Отчёт сохранён: ") + heel::kReportName);
            else
                out.push_back(std::string("✗ ") + heel::kReportName + " не сохранён: " + err);
            if (!warn.empty())
                out.push_back(warn);
            for (int post = 0; post < 2; post++)
            {
                if (!frames[post])
                    continue;
                const auto rgb = offscreen::Rasterize(*frames[post]);
                if (png::WriteRgb(dir / text::PathFromUtf8(heel::kPlotName[post]), frames[post]->w, frames[post]->h, rgb))
                    out.push_back(Fmt("✓ График сохранён: %s (%d × %d)", heel::kPlotName[post], frames[post]->w,
                                      frames[post]->h));
                else
                    out.push_back(std::string("✗ ") + heel::kPlotName[post] + " не сохранён");
            }
            return out;
        });
    }
    if (proc_.save.valid() && proc_.save.wait_for(0s) == std::future_status::ready)
    {
        const auto lines = proc_.save.get();
        bool ok = true;
        for (const auto& l : lines)
        {
            ProcLog(l);
            ok = ok && l.rfind("✗", 0) != 0;
        }
        Notify(ok ? "Отчёт и графики сохранены в папку " + Shown(proc_.outDir) : lines.front(), ok ? 1 : 2, 6000);
    }
    if (proc_.dirty && !proc_.load.valid())
        ProcRecompute();
}

// ---------------------------------------------------------------------------------------------------------------
// Страница
// ---------------------------------------------------------------------------------------------------------------

void App::PageProcess()
{
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

    const float W = ImGui::GetContentRegionAvail().x;
    const float gap = S(12);
    const float sp = ImGui::GetStyle().ItemSpacing.y;
    ProcParams(W);
    ImGui::Dummy(ImVec2(0, gap - sp));

    const float rowH = ImGui::GetFontSize() + S(4) + 2 * S(3);
    if (ProcCont())
    {
        // Одна запись — весь опыт: положения и итог, ниже — вся запись с положениями
        const float tableH = S(34) + S(28) + rowH * static_cast<float>(std::max<std::size_t>(proc_.pos.size(), 4));
        const bool narrow = W < S(960);
        if (narrow)
        {
            ProcPositions(W, std::max(S(200), tableH));
            ImGui::Dummy(ImVec2(0, gap - sp));
            ProcResult(W, S(250));
        }
        else
        {
            const float avail = ImGui::GetContentRegionAvail().y;
            const float h = std::clamp(tableH, S(200), std::max(S(200), avail - S(250) - gap - sp));
            const float tw = std::floor(W * 0.68f);
            ProcPositions(tw, h);
            ImGui::SameLine(0, gap);
            ProcResult(W - tw - gap, h);
        }
        ImGui::Dummy(ImVec2(0, gap - sp));
        ProcSeriesCard(W, std::max(S(250), ImGui::GetContentRegionAvail().y));
        return;
    }

    // Таблица и итог: высота — по числу замеров (лишние прокручиваются), графикам — не меньше 270
    const float tableH = S(22) + S(28) + rowH * static_cast<float>(std::max<std::size_t>(proc_.meas.size(), 4));
    const bool narrow = W < S(960);
    if (narrow)
    {
        ProcTable(W, std::max(S(180), tableH));
        ImGui::Dummy(ImVec2(0, gap - sp));
        ProcResult(W, S(250));
    }
    else
    {
        const float avail = ImGui::GetContentRegionAvail().y;
        const float h = std::clamp(tableH, S(210), std::max(S(210), avail - S(270) - gap - sp));
        const float tw = std::floor(W * 0.64f);
        ProcTable(tw, h);
        ImGui::SameLine(0, gap);
        ProcResult(W - tw - gap, h);
    }
    ImGui::Dummy(ImVec2(0, gap - sp));
    ProcPlots(W, std::max(S(270), ImGui::GetContentRegionAvail().y));
}

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
        const float btnW = S(156) + S(104) + S(126) + 3 * S(8);
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
        if (Button("Выбрать папку…", BtnKind::Normal, ImVec2(S(156), 0), loading, "Идёт чтение файлов"))
        {
            std::string d = proc_.dir;
            if (dialogs::PickFolder(d))
            {
                std::snprintf(proc_.dir, sizeof(proc_.dir), "%s", d.c_str());
                proc_.picked.clear();
                ProcLoad();
            }
        }
        ImGui::SameLine(0, S(8));
        if (Button("Файлы…", BtnKind::Normal, ImVec2(S(104), 0), loading, "Идёт чтение файлов"))
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
            ImGui::SetTooltip("Выбрать отдельные файлы (Ctrl / Shift — несколько)");
        ImGui::SameLine(0, S(8));
        if (Button(loading ? "Чтение…" : "Перечитать", BtnKind::Normal, ImVec2(S(126), 0), loading))
            ProcLoad();

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
        field("Водоизмещение D, т", "##hd", &settings_.heelD, 10, 100, "%.1f", 0.001, 1e7, S(150),
              "Водоизмещение судна в момент опыта");
        field("Масса груза P, т", "##hp", &settings_.heelP, 0.5, 5, "%.2f", 0.0, 1e6, S(136),
              "Масса кренящего груза, который переносят с борта на борт");
        {
            const char* label = "Плечо L, м";
            flow(ImGui::CalcTextSize(label).x + S(8) + S(136) + S(8) + S(72));
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Плечо переноса груза по умолчанию (как «макс. плечо переноса» старой программы).\n"
                                  "У каждого замера можно задать своё — в таблице, столбец «Плечо l».");
            ImGui::SameLine(0, S(8));
            ImGui::SetNextItemWidth(S(136));
            if (InputDoubleStep("##hl", &settings_.heelL, 0.1, 1.0, "%.2f"))
            {
                settings_.heelL = std::clamp(settings_.heelL, 0.0, 1000.0);
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
                ProcFindPositions();
            if (field("Шаг ≥, °", "##hst", &settings_.heelStep, 0.1, 0.5, "%.2f", 0.05, 20.0, S(120),
                      "Соседние положения отличаются по крену не меньше чем на столько градусов; ближе — одно\n"
                      "положение (качка не делит ступеньку). Изменение — поиск положений заново."))
                ProcFindPositions();
            if (edited && !proc_.posEdited)
                ProcLog("Положения найдены заново — ручные правки сброшены");
        }
        else
            field("Окно, с", "##hw", &settings_.heelWinS, 5, 30, "%.0f", 1.0, 3600.0, S(120),
                  "Установившийся крен — среднее за последние N секунд каждого файла (в старой программе — 30 с)");
        field("Порог, °", "##ht", &settings_.heelThr, 0.05, 0.5, "%.2f", 0.0, 10.0, S(120),
              ProcCont() ? "Положение с |Δθ| (от начального) не больше порога не учитывается: h не считается"
                         : "Замер на посту с |крен| не больше порога не учитывается: h не считается (в старой программе — 0,1°)");
        // Посты: датчик -> нос / корма / не учитывать (датчики прочитанных файлов)
        const std::vector<int> keys = proc_.keys; // копия: смена поста пересобирает proc_.keys
        if (!keys.empty())
        {
            float need = ImGui::CalcTextSize("Посты:").x;
            for (const int k : keys)
                need += S(12) + ImGui::CalcTextSize(heel::SensorShort(k).c_str()).x + S(6) + S(116);
            flow(need);
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Посты:");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Какой датчик на каком посту. По умолчанию Д2 — нос, Д3 — корма\n"
                                  "(старые файлы M_NNN_k: 1 — нос, 2 — корма).");
            for (const int k : keys)
            {
                ImGui::SameLine(0, S(12));
                ImGui::AlignTextToFramePadding();
                const std::string name = heel::SensorName(k);
                const int addr = k >= 100 ? k - 100 : k;
                TextColored(k < 100 && (addr == 2 || addr == 3) ? pal.sensor[addr - 2] : pal.text, "%s",
                            k >= 100 ? Fmt("M_%d", addr).c_str() : name.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", name.c_str());
                ImGui::SameLine(0, S(6));
                int idx = proc_.postOf[k] < 0 ? 2 : proc_.postOf[k];
                ImGui::SetNextItemWidth(S(116));
                ImGui::PushID(k);
                if (ImGui::Combo("##post", &idx, PostCombo, 3))
                {
                    proc_.postOf[k] = idx == 2 ? -1 : idx;
                    ProcRegroup(true);
                    if (proc_.mode == 1 && !ProcCont())
                        ProcChooseMode(false);
                    ProcRecompute();
                }
                ImGui::PopID();
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
            // Заголовки постов — с датчиками: «Крен нос (Д2)»
            std::string sens[2];
            for (const int key : proc_.keys)
            {
                const int post = proc_.postOf[key];
                if (post == 0 || post == 1)
                    sens[post] += (sens[post].empty() ? "" : ", ") +
                                  (key >= 100 ? Fmt("M_%d", key - 100) : heel::SensorName(key));
            }
            const std::string hdr[2] = {
                "Крен нос" + (sens[0].empty() ? std::string() : " (" + sens[0] + ")"),
                "Крен корма" + (sens[1].empty() ? std::string() : " (" + sens[1] + ")")};
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(2)));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(8), S(3)));
            if (ImGui::BeginTable("##proc_t", 7,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingFixedFit,
                                  ImVec2(0, ImGui::GetContentRegionAvail().y)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(24));
                float numW = S(64); // у записей на ПК вместо номера — время и метка
                for (const auto& m : proc_.meas)
                    numW = std::max(numW, ImGui::CalcTextSize(m.label.c_str()).x + S(20));
                ImGui::TableSetupColumn("Замер", ImGuiTableColumnFlags_WidthFixed, std::min(numW, S(170)));
                ImGui::TableSetupColumn("Плечо l, м", ImGuiTableColumnFlags_WidthFixed, S(76));
                ImGui::TableSetupColumn(hdr[0].c_str(), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("h нос, м", ImGuiTableColumnFlags_WidthFixed, S(66));
                ImGui::TableSetupColumn(hdr[1].c_str(), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("h корма, м", ImGuiTableColumnFlags_WidthFixed, S(78));
                ImGui::TableHeadersRow();
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
                        for (int p = 0; p < 2; p++)
                        {
                            tip += std::string("\n") + heel::kPostName[p] + ": ";
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
                    {
                        const bool own = proc_.arm.count(m.number) > 0;
                        double arm = std::isfinite(m.arm) ? m.arm : settings_.heelL;
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::PushStyleColor(ImGuiCol_Text, own ? pal.accent : tc);
                        if (ImGui::InputDouble("##arm", &arm, 0.0, 0.0, "%.2f"))
                        {
                            proc_.arm[m.number] = std::clamp(std::fabs(arm), 0.0, 1000.0);
                            proc_.dirty = true;
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(own ? "Своё плечо переноса этого замера, м (L = %.2f м — кнопка «Всем»)"
                                                  : "Плечо переноса груза, м: пока = L (%.2f м); можно задать своё.\n"
                                                    "Знак не нужен: борт берётся по знаку крена.",
                                              settings_.heelL);
                    }
                    // Посты
                    for (int p = 0; p < 2; p++)
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

void App::ProcResult(float w, float h)
{
    if (BeginCard("##proc_res", ImVec2(w, h)))
    {
        const bool cont = ProcCont();
        const auto& s = cont ? proc_.posSum : proc_.sum;
        {
            FontScope f(fontSmall);
            Muted("Метацентрическая высота h");
        }
        if (cont)
            Hint("Одна запись — весь опыт. Положение 1 (первое учтённое) — начальное: его крен — ноль. Для каждого "
                 "следующего положения Δθ = крен положения − крен начального, l — плечо переноса груза от начального "
                 "положения; h = P·l / (D·tg|Δθ|); |Δθ| не больше порога — не учитывается; итог — среднее.\n\n"
                 "По МНК — наклон k прямой tg Δθ = k·(P·l/D) + b по тем же точкам, h = 1/k.\n\nФормулы будут "
                 "уточняться (модуль Heel.hpp).");
        else
            Hint("h = P·l / (D·tg|θ|) по каждому замеру и посту (θ — средний крен за окно, l — плечо замера); "
                 "|θ| не больше порога — не учитывается; итог — среднее по всем учтённым значениям.\n\n"
                 "По МНК — наклон k прямой tg θ = k·(P·l/D) + b по тем же точкам, h = 1/k (свободный член b "
                 "учитывает начальный крен).\n\nФормулы — как в исходной программе обработки (BWM427_Analyzer) и "
                 "будут уточняться.");
        {
            FontScope f(fontHuge);
            if (std::isfinite(s.h))
                TextColored(pal.text, "%.3f м", s.h);
            else
                Muted("—");
        }
        {
            FontScope f(fontSmall);
            if (s.n)
            {
                // Перенос по ширине: карточка итога в режиме записи уже
                const std::string line =
                    Fmt("среднее по %d %s (%d %s)%s", s.n,
                        text::Plural(static_cast<std::uint64_t>(s.n), "значению", "значениям", "значениям"), s.measurements,
                        cont ? text::Plural(static_cast<std::uint64_t>(s.measurements), "положение", "положения", "положений")
                             : text::Plural(static_cast<std::uint64_t>(s.measurements), "замер", "замера", "замеров"),
                        std::isfinite(s.sd) ? Fmt(" · СКО %.3f м", s.sd).c_str() : "");
                ImGui::PushStyleColor(ImGuiCol_Text, pal.muted);
                ImGui::PushTextWrapPos(0.f);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            else if (cont)
                Muted("%s", proc_.pos.size() < 2 ? "нужно два положения и больше" : "нет учтённых значений (Δθ меньше порога?)");
            else
                Muted("%s", proc_.meas.empty() ? "нет замеров" : "нет учтённых значений (крен меньше порога?)");
        }
        ImGui::Spacing();
        {
            FontScope f(fontBold);
            if (std::isfinite(s.ls.h))
                ImGui::Text("По МНК: %.3f м", s.ls.h);
            else
                Muted("По МНК: —");
        }
        {
            FontScope f(fontSmall);
            if (std::isfinite(s.ls.k))
            {
                if (s.ls.throughOrigin)
                    Muted("%d %s, прямая через ноль (одно плечо)", s.ls.n,
                          text::Plural(static_cast<std::uint64_t>(s.ls.n), "точка", "точки", "точек"));
                else
                    Muted("%d %s · R² %.4f · нач. крен %s", s.ls.n,
                          text::Plural(static_cast<std::uint64_t>(s.ls.n), "точка", "точки", "точек"), s.ls.r2,
                          Deg(std::atan(s.ls.b) * 180.0 / heel::kPi).c_str());
            }
            else
                Muted(" ");
        }
        ImGui::Spacing();
        const bool saving = proc_.save.valid() || proc_.saveRequested;
        const bool canSave = proc_.files && !proc_.meas.empty() && !proc_.load.valid() && (!cont || !proc_.pos.empty());
        const float bw = std::floor((ImGui::GetContentRegionAvail().x - S(8)) * 0.55f);
        if (Button(saving ? "Сохранение…" : "Сохранить отчёт", BtnKind::Primary, ImVec2(bw, 0), saving || !canSave,
                   saving ? "Отчёт сохраняется" : cont ? "Нет положений" : "Нет замеров"))
            ProcessSaveReport();
        if (ImGui::IsItemHovered() && !saving && canSave)
            ImGui::SetTooltip(cont ? "В папку с файлами: %s (листы «Положения» и «Сырые_данные»),\n"
                                     "графики всей записи с положениями: %s / %s"
                                   : "В папку с файлами: %s (листы «Результаты» и «Сырые_данные»),\n%s и %s",
                              heel::kReportName, heel::kPlotName[0], heel::kPlotName[1]);
        ImGui::SameLine(0, S(8));
        if (Button("Открыть папку", BtnKind::Normal, ImVec2(-FLT_MIN, 0), proc_.outDir.empty() && !proc_.dir[0]))
            dialogs::OpenFolder(proc_.outDir.empty() ? std::string(proc_.dir) : proc_.outDir);

        // Журнал (коротко)
        ImGui::Spacing();
        const float lh = std::max(S(40), ImGui::GetContentRegionAvail().y);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.cardAlt);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(4)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(6), S(1)));
        ImGui::BeginChild("##proclog", ImVec2(0, lh), ImGuiChildFlags_AlwaysUseWindowPadding);
        {
            FontScope f(fontSmall);
            if (proc_.log.empty())
                Muted("Журнал: здесь — что прочитано, что пропущено и куда сохранён отчёт.");
            for (const auto& l : proc_.log)
            {
                const bool bad = l.rfind("✗", 0) == 0, good = l.rfind("✓", 0) == 0;
                ImGui::PushStyleColor(ImGuiCol_Text, bad ? pal.err : good ? pal.ok : pal.textDim);
                ImGui::TextWrapped("%s", l.c_str());
                ImGui::PopStyleColor();
            }
            if (proc_.logScroll)
            {
                ImGui::SetScrollHereY(1.f);
                proc_.logScroll = false;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    EndCard();
}

void App::ProcPlots(float w, float h)
{
    if (BeginCard("##proc_plots", ImVec2(w, h)))
    {
        ImGui::AlignTextToFramePadding();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Графики крена");
        }
        ImGui::SameLine(0, S(14));
        std::string labels[2] = {"Нос", "Корма"};
        for (const int key : proc_.keys)
        {
            const int post = proc_.postOf[key];
            if (post == 0 || post == 1)
                labels[post] += (labels[post].find('(') == std::string::npos ? " (" : ", ") +
                                (key >= 100 ? Fmt("M_%d", key - 100) : heel::SensorName(key));
        }
        for (auto& l : labels)
            if (l.find('(') != std::string::npos)
                l += ")";
        const char* lp[2] = {labels[0].c_str(), labels[1].c_str()};
        static const int values[2] = {0, 1};
        Segmented("post", &proc_.show, lp, values, 2);
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        HeaderHint(right - ModeSwitchWidth() - S(16),
                   Fmt("ось %s · пунктир — среднее за последние %.0f с · легенда — средний крен замера",
                       settings_.heelAxis == 0 ? "X" : "Y", settings_.heelWinS),
                   Fmt("ось %s · пунктир — среднее за %.0f с", settings_.heelAxis == 0 ? "X" : "Y", settings_.heelWinS));
        ProcModeSwitch(right);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ProcPanes(proc_.show, avail.x, avail.y, false, false);
    }
    EndCard();
}

// Переключатель режима справа в строке заголовка карточки графиков: right — правый край (ширина — ModeSwitchWidth).
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
    std::string sens;
    for (const int key : proc_.keys)
        if (proc_.postOf[key] == post)
            sens += (sens.empty() ? "" : ", ") + heel::SensorName(key);
    {
        FontScope f(fontH2);
        ImGui::Text("%s пост%s — крен по оси %s", post == 0 ? "Носовой" : "Кормовой",
                    sens.empty() ? "" : (" (" + sens + ")").c_str(), settings_.heelAxis == 0 ? "X" : "Y");
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
    PlotStyle(forExport);
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
        {
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
        }
        const bool canAdd = gapB - gapA >= 4.0;

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
        {
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
        }
        if (ImGui::IsItemHovered() && canAdd)
            ImGui::SetTooltip("Новое положение на самом длинном свободном участке записи (%.1f…%.1f с);\n"
                              "границы — в таблице или перетаскиванием на графике.",
                              gapA, gapB);
        ImGui::SameLine(0, S(8));
        if (Button("Найти заново", BtnKind::Normal, ImVec2(bw2, 0)))
        {
            ProcFindPositions();
            ProcRecompute();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Найти положения груза по записи заново (ручные правки сбросятся)");

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
            int posts[2], np = 0;
            for (int p = 0; p < 2; p++)
                if (m.post[p].file >= 0)
                    posts[np++] = p;
            const bool showDelta = np == 1;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(2)));
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(6), S(3)));
            const int cols = 6 + np * (showDelta ? 3 : 2);
            if (ImGui::BeginTable("##pos_t", cols,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingFixedFit,
                                  ImVec2(0, ImGui::GetContentRegionAvail().y)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(22));
                ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed, S(34));
                ImGui::TableSetupColumn("Начало, с", ImGuiTableColumnFlags_WidthFixed, S(62));
                ImGui::TableSetupColumn("Конец, с", ImGuiTableColumnFlags_WidthFixed, S(62));
                ImGui::TableSetupColumn("Плечо l, м", ImGuiTableColumnFlags_WidthFixed, S(68));
                std::string hdr[2];
                for (int k = 0; k < np; k++)
                {
                    const int p = posts[k];
                    const auto& f = (*files)[static_cast<std::size_t>(m.post[p].file)];
                    hdr[k] = std::string("Крен ") + (p == 0 ? "нос" : "корма") + " (" + heel::SensorShort(f.sensor) + ")";
                    ImGui::TableSetupColumn(hdr[k].c_str(), ImGuiTableColumnFlags_WidthStretch);
                    if (showDelta)
                        ImGui::TableSetupColumn("Δθ", ImGuiTableColumnFlags_WidthFixed, S(62));
                    ImGui::TableSetupColumn(np == 1 ? "h, м" : (p == 0 ? "h нос, м" : "h корма, м"),
                                            ImGuiTableColumnFlags_WidthFixed, S(np == 1 ? 52.f : 70.f));
                }
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 3 * S(24) + 2 * S(3));
                ImGui::TableHeadersRow();
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
                    if (reference)
                    {
                        ImGui::AlignTextToFramePadding();
                        Muted("нач.");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Начальное положение: от него считаются Δθ и плечи переноса остальных");
                    }
                    else
                    {
                        const bool own = std::isfinite(ps.arm);
                        double arm = own ? ps.arm : settings_.heelL;
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::PushStyleColor(ImGuiCol_Text, own ? pal.accent : tc);
                        if (ImGui::InputDouble("##arm", &arm, 0.0, 0.0, "%.2f"))
                        {
                            ps.arm = std::clamp(std::fabs(arm), 0.0, 1000.0);
                            proc_.dirty = true;
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(own ? "Плечо переноса груза от начального положения, м (L = %.2f м — кнопка «Всем»)"
                                                  : "Плечо переноса груза от начального положения, м: пока = L (%.2f м).\n"
                                                    "Знак не нужен: борт берётся по знаку Δθ.",
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
                        if (!std::isfinite(st.mean))
                            TextColored(pal.err, "нет данных");
                        else
                        {
                            TextColored(tc, "%s", text::Angle(st.mean, 3).c_str());
                            if (std::isfinite(st.sd))
                            {
                                ImGui::SameLine(0, S(5));
                                FontScope f(fontSmall);
                                Muted("±%.3f", st.sd);
                            }
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlapped) && std::isfinite(st.mean))
                            ImGui::SetTooltip("Средний крен на участке %.1f…%.1f с (%.1f с): %s, СКО %.3f°, точек %d"
                                              " (выбросов отброшено %d)\nΔθ от начального положения: %s",
                                              std::min(ps.t0, ps.t1), std::max(ps.t0, ps.t1), std::fabs(ps.t1 - ps.t0),
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
                        ImGui::SetTooltip("Удалить положение");
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
                        proc_.pos.erase(proc_.pos.begin() + static_cast<std::ptrdiff_t>(at));
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
        const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
        ImGui::AlignTextToFramePadding();
        {
            FontScope f(fontBold);
            ImGui::TextUnformatted("Запись крена");
        }
        const int post = ProcShownPost();
        if (m.post[0].file >= 0 && m.post[1].file >= 0 && proc_.files)
        {
            ImGui::SameLine(0, S(14));
            std::string labels[2];
            for (int p = 0; p < 2; p++)
                labels[p] = std::string(heel::kPostName[p]) + " (" +
                            heel::SensorShort((*proc_.files)[static_cast<std::size_t>(m.post[p].file)].sensor) + ")";
            const char* lp[2] = {labels[0].c_str(), labels[1].c_str()};
            static const int values[2] = {0, 1};
            int show = post;
            if (Segmented("spost", &show, lp, values, 2))
                proc_.show = show;
        }
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        HeaderHint(right - ModeSwitchWidth() - S(16),
                   Fmt("ось %s · заливка — положения, линия — их средний крен · щелчок — выбрать, границы "
                       "выбранного перетаскиваются",
                       settings_.heelAxis == 0 ? "X" : "Y"),
                   Fmt("ось %s · щелчок — выбрать положение, границы перетаскиваются", settings_.heelAxis == 0 ? "X" : "Y"));
        ProcModeSwitch(right);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
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
    if (post < 0 || post > 1 || m.post[post].file < 0)
        return;
    const heel::File& f = (*proc_.files)[static_cast<std::size_t>(m.post[post].file)];
    const auto& rows = f.log.rows;
    if (rows.empty())
        return;
    const int axis = settings_.heelAxis;
    PlotStyle(forExport);
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
    char id[64];
    std::snprintf(id, sizeof(id), "##series%d_%d_%d_%d", post, proc_.rec, axis, forExport ? 1 : 0);
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

    // Запись
    ImPlot::SetNextLineStyle(darkTheme ? ImVec4(0.80f, 0.84f, 0.90f, 1.f) : ImVec4(0.20f, 0.24f, 0.30f, 1.f), lineW);
    ImPlot::PlotLine("Крен", &rows[0].t, axis == 0 ? &rows[0].calcX : &rows[0].calcY, static_cast<int>(rows.size()), 0, 0,
                     static_cast<int>(sizeof(csvlog::Row)));

    // Средний крен положений, номера и уровни
    for (std::size_t i = 0; i < proc_.pos.size(); i++)
    {
        const auto& p = proc_.pos[i];
        const auto& st = p.st[post];
        const ImVec4 c = p.use ? kMeasColors[i % 10] : pal.muted;
        const double t0 = std::min(p.t0, p.t1), t1 = std::max(p.t0, p.t1);
        if (std::isfinite(st.mean))
        {
            const double xs[2] = {t0, t1}, ys[2] = {st.mean, st.mean};
            ImPlot::SetNextLineStyle(Mix(c, darkTheme ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), 0.15f), S(2.6f));
            ImPlot::PlotLine(Fmt("##lvl%d", static_cast<int>(i)).c_str(), xs, ys, 2);
        }
        ImPlot::PushStyleColor(ImPlotCol_InlayText, Mix(c, pal.text, 0.35f));
        {
            FontScope fb(fontBold);
            ImPlot::PlotText(Fmt("%d", static_cast<int>(i) + 1).c_str(), 0.5 * (t0 + t1), lim.Y.Max,
                             ImVec2(0, S(11)));
        }
        if (std::isfinite(st.mean))
        {
            FontScope fs(fontSmall);
            const std::string lv = text::Angle(st.mean, 2);
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
        ImGui::Text("%s пост (%s) — запись опыта, крен по оси %s", post == 0 ? "Носовой" : "Кормовой",
                    heel::SensorShort(f.sensor).c_str(), settings_.heelAxis == 0 ? "X" : "Y");
    }
    {
        FontScope fs(fontSmall);
        const auto& s = proc_.posSum;
        Muted("%s · D = %.1f т, P = %.2f т · положений %d · h = %s м (среднее), по МНК %s м · заливка — положения груза, "
              "линия — средний крен положения",
              f.name.c_str(), settings_.heelD, settings_.heelP, static_cast<int>(proc_.pos.size()),
              std::isfinite(s.h) ? Fmt("%.3f", s.h).c_str() : "—", std::isfinite(s.ls.h) ? Fmt("%.3f", s.ls.h).c_str() : "—");
    }
    ImGui::Spacing();
    (void) h;
    ProcSeriesPlot(post, w, ImGui::GetContentRegionAvail().y, true);
}

} // namespace ui

