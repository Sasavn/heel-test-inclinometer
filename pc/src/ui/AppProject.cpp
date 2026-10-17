// «Обработка»: проект опыта кренования — файл *.krenomer (core/Project.hpp). Сохранить / открыть проект, недавние
// проекты, автосохранение «Опыт.krenomer» рядом с записями (через 3 с после правки; отключается в настройках) и
// открытие папки опыта вместе с её проектом. Опыт без проекта начинается с чистого листа: плечи, перемещения,
// снятые замеры и правки табл. 6.5 другого опыта не переносятся.
#include "App.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <system_error>
#include <type_traits>

#include <imgui.h>

#include "../core/FileBusy.hpp"
#include "../core/TextUtil.hpp"
#include "Dialogs.hpp"
#include "Widgets.hpp"

namespace ui
{

namespace fs = std::filesystem;

namespace
{

constexpr int kAutosaveDelayMs = 3000;
const wchar_t kProjectFilter[] = L"Проект кренования (*.krenomer)\0*.krenomer\0Все файлы\0*.*\0";

std::string FileNameOf(const std::string& path) { return text::PathToUtf8(text::PathFromUtf8(path).filename()); }

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Состояние <-> проект
// ---------------------------------------------------------------------------------------------------------------

proj::Project App::ProcProjectNow() const
{
    proj::Project p;
    p.program = WindowTitle();
    p.saved = text::LocalTimeString(std::time(nullptr));
    p.folder = proc_.outDir.empty() ? std::string(proc_.dir) : proc_.outDir;
    p.files = proc_.picked;
    p.model = settings_.heelSmall;
    p.D = settings_.heelD;
    p.P = settings_.heelP;
    p.L = settings_.heelL;
    p.B = settings_.heelB;
    p.armFrom = settings_.armFrom;
    p.axis = settings_.heelAxis;
    p.windowS = settings_.heelWinS;
    p.thresholdDeg = settings_.heelThr;
    p.plateauS = settings_.heelPlateauS;
    p.stepDeg = settings_.heelStep;
    p.method = settings_.fltMethod;
    p.medianN = settings_.fltMedianN;
    p.fcHz = settings_.fltFc;
    p.tcS = settings_.fltTc;
    p.sgWindowS = settings_.fltSgWin;
    p.window = settings_.fltWindow;
    if (p.method == static_cast<int>(filt::Method::User))
    {
        p.userMethod = settings_.fltUser;
        for (const auto& m : proc_.methods)
            if (m.name == settings_.fltUser)
                p.userMethodText = um::ToText(m);
    }
    for (const int k : proc_.keys)
    {
        proj::Sensor s;
        s.key = k;
        if (const auto it = proc_.names.find(k); it != proc_.names.end())
            s.name = it->second;
        s.inverted = proc_.invert.count(k) > 0;
        s.use = proc_.skip.count(k) == 0;
        p.sensors.push_back(s);
    }
    p.rsH = settings_.rsH;
    p.rsLmax = settings_.rsLmax;
    p.rsD0 = settings_.rsD0;
    p.rsSpread = settings_.rsSpread;
    p.rsMain = settings_.rsMain;
    p.rsMainGroups = settings_.rsMainGroups;
    p.rsInstrument = settings_.rsInstrument;
    p.rsDh = settings_.rsDh;
    p.rsDworst = settings_.rsDworst;
    p.rsGroupMode = settings_.rsGroupMode;
    p.rsGroups = ProcGroups();
    p.rsMoves = proc_.rsMoves;
    p.mode = proc_.mode;
    p.modeManual = proc_.modeUser;
    if (ProcCont() && proc_.files)
    {
        const auto& m = proc_.meas[static_cast<std::size_t>(proc_.rec)];
        if (const int mp = heel::MainPost(m); mp >= 0)
            p.recordFile = (*proc_.files)[static_cast<std::size_t>(m.post[mp].file)].name;
        p.positionsEdited = proc_.posEdited;
        p.positions = proc_.pos;
    }
    p.arms = proc_.arm;
    for (const auto& [n, off] : proc_.off)
        if (off)
            p.excluded[n] = true;
    p.rsManual = proc_.rsManual;
    p.h = ProcCont() ? proc_.posSum.h : proc_.sum.h;
    p.hMain = proc_.rs.hMain;
    return p;
}

std::string App::ProcProjectSnapshot() const
{
    if (!proc_.files)
        return {};
    return proj::ToJson(ProcProjectNow(), false);
}

std::string App::ProcProjectTarget() const
{
    if (!proc_.projectPath.empty())
        return proc_.projectPath;
    if (proc_.outDir.empty())
        return {};
    return text::PathToUtf8(text::PathFromUtf8(proc_.outDir) / proj::kAutoName);
}

// ---------------------------------------------------------------------------------------------------------------
// Сохранить
// ---------------------------------------------------------------------------------------------------------------

bool App::ProcSaveProjectTo(const std::string& path, bool autosave)
{
    if (path.empty() || !proc_.files || proc_.load.valid())
        return false;
    if (proc_.dirty)
        ProcRecompute();
    fs::path file = text::PathFromUtf8(path);
    // Файл проекта открыт другой программой — под следующим свободным номером «Опыт (2).krenomer»
    std::string movedTo;
    if (filebusy::Busy(file))
    {
        const int n = filebusy::FreeSuffix(file.parent_path(), {text::PathToUtf8(file.filename())});
        if (n > 1)
        {
            file = file.parent_path() / text::PathFromUtf8(filebusy::SuffixName(text::PathToUtf8(file.filename()), n));
            movedTo = text::PathToUtf8(file);
        }
    }
    const std::string savedPath = movedTo.empty() ? path : movedTo;
    std::error_code ec;
    const bool existed = fs::exists(file, ec);
    std::string err;
    if (!proj::Save(file, ProcProjectNow(), &err))
    {
        ProcLog("✗ Проект " + FileNameOf(path) + " не сохранён: " + err, Shown(path));
        Notify("Проект не сохранён: " + err, 2, 6000);
        if (autosave)
            proc_.projAutoFailed = true; // не повторять каждые 3 с
        return false;
    }
    proc_.projectPath = savedPath;
    proc_.projSnap = ProcProjectSnapshot();
    proc_.projSaveAt = 0;
    settings_.AddRecentProject(savedPath);
    SaveSettingsSoon();
    if (!movedTo.empty())
    {
        ProcLog("⚠ " + FileNameOf(path) + " открыт в другой программе — проект сохранён как " + FileNameOf(movedTo),
                Shown(movedTo));
        Notify("Проект открыт в другой программе — сохранено как " + FileNameOf(movedTo), 0, 12000,
               text::PathToUtf8(file.parent_path()));
        return true;
    }
    if (!autosave)
    {
        ProcLog("✓ Проект сохранён: " + FileNameOf(path), Shown(path));
        Notify("Проект сохранён: " + Shown(path), 1, 5000);
    }
    else if (!existed)
        ProcLog("Проект сохраняется автоматически рядом с записями: " + FileNameOf(path) +
                    " (отключается в «Настройках»)",
                Shown(path));
    return true;
}

bool App::ProcessSaveProject(const std::string& path)
{
    return ProcSaveProjectTo(path.empty() ? ProcProjectTarget() : path, false);
}

// Автосохранение: после пересчёта состояние сравнивается с открытым / сохранённым; изменилось — запись через 3 с
// после последней правки. Открытие папки или проекта само по себе файл не пишет.
void App::TickProject()
{
    if (!proc_.files || proc_.load.valid() || proc_.projPending || proc_.meas.empty())
        return;
    const bool on = projectAutosaveAllowed_ && settings_.projectAutosave && !proc_.projAutoFailed;
    if (!on)
    {
        proc_.projSaveAt = 0;
        proc_.projCheck = false;
        return;
    }
    if (proc_.projCheck)
    {
        proc_.projCheck = false;
        // Отсчёт 3 с — от последнего настоящего изменения (пересчёт без изменений срок не отодвигает)
        std::string snap = ProcProjectSnapshot();
        if (snap != proc_.projSnap && (snap != proc_.projSeen || !proc_.projSaveAt))
            proc_.projSaveAt = link_->NowMs() + kAutosaveDelayMs;
        proc_.projSeen = std::move(snap);
    }
    if (proc_.projSaveAt && link_->NowMs() >= proc_.projSaveAt)
    {
        proc_.projSaveAt = 0;
        if (ProcProjectSnapshot() != proc_.projSnap)
            ProcSaveProjectTo(ProcProjectTarget(), true);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Открыть
// ---------------------------------------------------------------------------------------------------------------

void App::ProcApplyProjectSettings(const proj::Project& p)
{
    settings_.heelSmall = p.model;
    settings_.heelD = p.D;
    settings_.heelP = p.P;
    settings_.heelL = p.L;
    settings_.heelB = p.B;
    settings_.armFrom = p.armFrom;
    settings_.heelAxis = p.axis;
    settings_.heelWinS = p.windowS;
    settings_.heelThr = p.thresholdDeg;
    settings_.heelPlateauS = p.plateauS;
    settings_.heelStep = p.stepDeg;
    settings_.fltMethod = p.method;
    settings_.fltMedianN = p.medianN;
    settings_.fltFc = p.fcHz;
    settings_.fltTc = p.tcS;
    settings_.fltSgWin = p.sgWindowS;
    settings_.fltWindow = p.window;
    // Свой метод: нет в программе — взять из проекта (и сохранить); есть, но другой — под своим именем «… (проект)»
    if (p.method == static_cast<int>(filt::Method::User))
    {
        ProcLoadMethods();
        um::Method fromProject;
        const bool have = !p.userMethodText.empty() && um::FromText(p.userMethodText, fromProject);
        const um::Method* own = nullptr;
        for (const auto& m : proc_.methods)
            if (m.name == p.userMethod)
                own = &m;
        if (have)
        {
            fromProject.name = p.userMethod;
            if (own && um::ToText(*own) != um::ToText(fromProject))
            {
                fromProject.name = p.userMethod + " (проект)";
                ProcLog("Свой метод «" + p.userMethod + "» в программе изменён после сохранения проекта — взят метод из "
                        "проекта: «" + fromProject.name + "»");
            }
            if (!own || fromProject.name != p.userMethod)
            {
                ProcessUseUserMethod(fromProject, false);
                ProcSaveMethod(fromProject);
            }
            settings_.fltUser = fromProject.name;
        }
        else if (own)
            settings_.fltUser = p.userMethod;
        else
        {
            settings_.fltMethod = static_cast<int>(filt::Method::Kalman);
            ProcLog("✗ Свой метод «" + p.userMethod + "» не найден — расчёт методом Калмана");
        }
    }
    // Датчики: названия, знак, участие
    ProcLoadNames();
    ProcLoadInvert();
    for (const auto& s : p.sensors)
    {
        if (s.name.empty())
            proc_.names.erase(s.key);
        else
            proc_.names[s.key] = s.name;
        if (s.inverted)
            proc_.invert.insert(s.key);
        else
            proc_.invert.erase(s.key);
        if (s.use)
            proc_.skip.erase(s.key);
        else
            proc_.skip.insert(s.key);
    }
    std::string inv;
    for (const int k : proc_.invert)
        inv += (inv.empty() ? "" : ",") + std::to_string(k);
    settings_.sensorInvert = inv;
    SaveSensorNames();
    // Правила РС
    settings_.rsH = p.rsH;
    settings_.rsLmax = p.rsLmax;
    settings_.rsD0 = p.rsD0;
    settings_.rsSpread = p.rsSpread;
    settings_.rsMain = p.rsMain;
    settings_.rsMainGroups = p.rsMainGroups;
    settings_.rsInstrument = p.rsInstrument;
    settings_.rsDh = p.rsDh;
    settings_.rsDworst = p.rsDworst;
    settings_.rsGroupMode = p.rsGroupMode;
    ProcGroups();
    proc_.rsGroups = p.rsGroups;
    ProcSaveGroups();
    SaveSettingsSoon();
}

void App::ProcApplyProjectExperiment(const proj::Project& p)
{
    if (p.mode == 1 && ProcCont() && proc_.files)
    {
        bool recChanged = false;
        if (!p.recordFile.empty())
        {
            int found = -1;
            for (std::size_t i = 0; i < proc_.meas.size() && found < 0; i++)
            {
                const int mp = heel::MainPost(proc_.meas[i]);
                if (mp >= 0 && heel::RecordNameKey((*proc_.files)[static_cast<std::size_t>(proc_.meas[i].post[mp].file)].name) ==
                                   heel::RecordNameKey(p.recordFile))
                    found = static_cast<int>(i);
            }
            if (found < 0)
                ProcLog("✗ Запись " + p.recordFile + " из проекта не найдена в папке — взята " + ProcRecordText());
            else if (found != proc_.rec)
            {
                proc_.rec = found;
                recChanged = true;
            }
        }
        if (!p.positions.empty())
        {
            proc_.pos = p.positions;
            proc_.posEdited = p.positionsEdited;
            proc_.sel = -1;
        }
        else if (recChanged)
            ProcFindPositions();
    }
    proc_.rsManual = p.rsManual;
    proc_.dirty = true;
    ProcRecompute();
    std::string line = "Проект открыт: " + FileNameOf(proc_.projectPath);
    if (!p.saved.empty())
        line += " (сохранён " + p.saved + ")";
    const double h = ProcCont() ? proc_.posSum.h : proc_.sum.h;
    if (std::isfinite(p.h) && std::isfinite(h) && std::fabs(h - p.h) > 1e-6 * std::max(1.0, std::fabs(p.h)))
        ProcLog(line + Fmt(" — h при сохранении %.4f, сейчас %.4f: изменились файлы записей или метод обработки?", p.h, h),
                Shown(proc_.projectPath));
    else
        ProcLog(line, Shown(proc_.projectPath));
}

bool App::ProcessOpenProject(const std::string& path)
{
    if (ProcessBusy())
    {
        Notify("Идёт чтение или сохранение — откройте проект чуть позже", 2, 4000);
        return false;
    }
    proj::Project p;
    std::string err;
    if (!proj::Load(text::PathFromUtf8(path), p, &err))
    {
        ProcLog("✗ Проект " + FileNameOf(path) + " не открыт: " + err, Shown(path));
        if (!p.missing.empty())
        {
            // Записи не нашлись — проект разобран: записи можно указать вручную («Указать файлы…»)
            proc_.projMissing = std::make_shared<proj::Project>(p);
            proc_.projMissingPath = path;
            Notify("Проект открыт не полностью: не найдены записи — укажите файлы", 2, 7000);
        }
        else
            Notify("Проект не открыт: " + err, 2, 7000);
        return false;
    }
    return ProcOpenProjectLoaded(path, p);
}

// Указать записи проекта вручную (когда они не нашлись): по одному файлу на запись проекта, в любом порядке — запись
// проекта ищется по похожему имени, иначе — по порядку.
bool App::ProcessProjectPickFiles(const std::vector<std::string>& files)
{
    if (!proc_.projMissing || files.empty() || ProcessBusy())
        return false;
    proj::Project p = *proc_.projMissing;
    std::vector<std::string> picked = files;
    std::vector<std::string> out(p.files.size());
    // по ключу имени (без приписок копии и регистра)
    for (std::size_t i = 0; i < p.files.size(); i++)
    {
        const std::string want = heel::RecordNameKey(text::PathToUtf8(text::PathFromUtf8(p.files[i]).filename()));
        for (std::size_t k = 0; k < picked.size(); k++)
            if (!picked[k].empty() && heel::RecordNameKey(FileNameOf(picked[k])) == want)
            {
                out[i] = picked[k];
                picked[k].clear();
                break;
            }
    }
    // остальные — по порядку
    std::size_t k = 0;
    for (auto& o : out)
        if (o.empty())
        {
            while (k < picked.size() && picked[k].empty())
                k++;
            if (k < picked.size())
                o = picked[k++];
        }
    for (; k < picked.size(); k++)
        if (!picked[k].empty())
            out.push_back(picked[k]); // лишние — тоже в чтение
    out.erase(std::remove(out.begin(), out.end(), std::string()), out.end());
    p.files = out;
    p.folder = text::PathToUtf8(text::PathFromUtf8(out.front()).parent_path());
    std::string list;
    for (const auto& f : out)
        list += (list.empty() ? "" : ", ") + FileNameOf(f);
    p.notes.push_back("Записи проекта указаны вручную: " + list);
    p.missing.clear();
    return ProcOpenProjectLoaded(proc_.projMissingPath, p);
}

bool App::ProcOpenProjectLoaded(const std::string& path, const proj::Project& p)
{
    proc_.projMissing.reset();
    proc_.projMissingPath.clear();
    for (const auto& n : p.notes)
        ProcLog(n, Shown(path));
    ProcApplyProjectSettings(p);
    proc_.projectPath = path;
    settings_.AddRecentProject(path);
    SaveSettingsSoon();
    proc_.projPending = std::make_shared<proj::Project>(p);
    proc_.picked = p.files;
    std::snprintf(proc_.dir, sizeof(proc_.dir), "%s", p.folder.c_str());
    proc_.opened = true;
    proc_.paramsAuto = false;
    proc_.dirty = true;
    ProcLoad();
    return true;
}

// Записи проекта не найдены: что не нашлось, «Указать файлы…» и «Закрыть».
void App::ProcProjectMissingCard(float w)
{
    if (!proc_.projMissing)
        return;
    if (BeginCard("##proj_missing", ImVec2(w, 0)))
    {
        std::string list;
        for (const auto& m : proc_.projMissing->missing)
            list += (list.empty() ? "" : ", ") + m;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        TextColored(pal.warn, "⚠ Проект %s: не найдены записи — %s. Их нет ни рядом с проектом, ни по прежнему пути, ни под "
                              "похожим именем («… (1)», «… - копия»). Укажите файлы — плечи, положения и правки "
                              "проекта применятся к ним.",
                    FileNameOf(proc_.projMissingPath).c_str(), list.c_str());
        ImGui::PopTextWrapPos();
        if (Button("Указать файлы…", BtnKind::Primary, ImVec2(0, 0), ProcessBusy(), "Идёт чтение"))
        {
            std::vector<std::string> picked;
            const std::string dir = text::PathToUtf8(text::PathFromUtf8(proc_.projMissingPath).parent_path());
            if (dialogs::PickFiles(picked, dir, L"Файлы записей (*.CSV, *.TXT)\0*.CSV;*.TXT\0Все файлы\0*.*\0",
                                   L"Записи проекта"))
                ProcessProjectPickFiles(picked);
        }
        ImGui::SameLine(0, S(8));
        if (Button("Закрыть", BtnKind::Normal))
        {
            proc_.projMissing.reset();
            proc_.projMissingPath.clear();
        }
    }
    EndCard();
}

// В папке записей есть проект кренования (*.krenomer; «Опыт.krenomer» — первым, иначе самый свежий): не открывать
// молча — спросить, открыть его или начать новый проект (чистые плечи). Спрашивается при каждом открытии папки (кроме
// явного «Открыть проект»). true — вопрос задан, записи пока не читаются.
bool App::ProcAskProject(const std::string& folder)
{
    if (folder.empty())
        return false;
    const fs::path dir = text::PathFromUtf8(folder);
    std::error_code ec;
    fs::path file;
    if (fs::is_regular_file(dir / proj::kAutoName, ec))
        file = dir / proj::kAutoName;
    else
    {
        fs::file_time_type best{};
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == L".krenomer")
            {
                const auto t = fs::last_write_time(it->path(), ec);
                if (file.empty() || t > best)
                {
                    file = it->path();
                    best = t;
                }
            }
    }
    if (file.empty())
        return false;
    proj::Project p;
    std::string err;
    if (!proj::Load(file, p, &err) && p.missing.empty())
    {
        ProcLog("✗ " + FileNameOf(text::PathToUtf8(file)) + " в папке не прочитан: " + err + " — папка открыта без проекта",
                Shown(text::PathToUtf8(file)));
        return false;
    }
    auto a = std::make_shared<std::remove_reference_t<decltype(*proc_.ask)>>();
    a->path = text::PathToUtf8(file);
    a->folder = folder;
    a->saved = p.saved;
    a->program = p.program;
    a->positions = static_cast<int>(p.positions.size());
    a->measurements = static_cast<int>(p.arms.size());
    a->h = p.h;
    proc_.ask = a;
    return true;
}

// «Проект ▼ → Новый проект кренования»: записи текущей папки заново — чистые плечи, без прежних правок положений
void App::ProcessNewProject()
{
    if (ProcessBusy())
        return;
    if (!proc_.outDir.empty() && proc_.picked.empty())
        std::snprintf(proc_.dir, sizeof(proc_.dir), "%s", proc_.outDir.c_str());
    proc_.askSkip = true;
    proc_.freshStart = true;
    ProcLog("Новый проект кренования по записям папки");
    ProcLoad();
}

void App::ProcessAnswerProject(int answer)
{
    if (!proc_.ask)
        return;
    const auto a = proc_.ask;
    proc_.ask.reset();
    if (answer == 1)
        ProcessOpenProject(a->path);
    else if (answer == 2)
    {
        // новый проект кренования: записи этой папки, плечи и правки — чистые
        proc_.askSkip = true;
        proc_.freshStart = true;
        std::snprintf(proc_.dir, sizeof(proc_.dir), "%s", a->folder.c_str());
        proc_.picked.clear();
        ProcLog("Новый проект кренования по записям папки (проект " + FileNameOf(a->path) + " не открыт)");
        ProcLoad();
    }
    else
        ProcLog("Папка " + Shown(a->folder) + " не прочитана: в ней проект " + FileNameOf(a->path) + " — выберите, что открыть");
}

// Диалог «в папке есть проект»: «Открыть проект из этой папки» / «Создать новый проект кренования» / «Отмена».
void App::ProcAskProjectDialog()
{
    if (!proc_.ask)
        return;
    const char* id = "Проект кренования в этой папке##askproj";
    if (!ImGui::IsPopupOpen(id))
        ImGui::OpenPopup(id);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(vp->WorkSize.x - S(40), S(620)), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(16)));
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove))
    {
        const auto a = proc_.ask;
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted("В папке с записями уже есть сохранённый проект. Открыть его — или начать новый проект "
                               "кренования по этим записям?");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (BeginKV("##askkv", S(150)))
        {
            KV("Проект", FileNameOf(a->path));
            KV("Сохранён", a->saved.empty() ? std::string("—") : a->saved);
            KV("Положений груза", a->positions ? std::to_string(a->positions) : std::string("— (файл = замер)"));
            if (std::isfinite(a->h) && a->h != 0.0)
                KV("h при сохранении", Fmt("%.4f м", a->h));
            if (!a->program.empty())
                KV("Программа", a->program);
            EndKV();
        }
        ImGui::Spacing();
        const float bw = (ImGui::GetContentRegionAvail().x - S(8)) * 0.5f;
        int answer = -1;
        if (Button("Открыть проект из этой папки", BtnKind::Orange, ImVec2(bw, S(36))))
            answer = 1;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Плечи, положения, параметры и правки — как сохранены в проекте");
        ImGui::SameLine(0, S(8));
        if (Button("Создать новый проект кренования", BtnKind::Normal, ImVec2(bw, S(36))))
            answer = 2;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Загрузить записи из папки с чистыми плечами и без прежних правок (проект не трогается)");
        if (Button("Отмена", BtnKind::Normal, ImVec2(S(120), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            answer = 0;
        if (answer >= 0)
        {
            ImGui::CloseCurrentPopup();
            ProcessAnswerProject(answer);
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------------------------------------------
// Кнопки и меню
// ---------------------------------------------------------------------------------------------------------------

// Окна выбора файла проекта (меню «Проект ▼», Ctrl+O, Ctrl+Shift+S). В стенде и тестах не вызываются.
void App::ProcOpenProjectDialog()
{
    if (ProcessBusy())
        return;
    std::string path = proc_.projectPath.empty() ? std::string() : proc_.projectPath;
    if (dialogs::PickFile(path, kProjectFilter, L"Открыть проект опыта"))
        ProcessOpenProject(path);
}

void App::ProcSaveProjectAsDialog()
{
    if (!proc_.files || proc_.meas.empty() || ProcessBusy())
        return;
    const std::string target = ProcProjectTarget();
    std::string path = target.empty() ? std::string("Опыт.krenomer") : target;
    if (dialogs::SaveFile(path, kProjectFilter, L"Сохранить проект опыта", L"krenomer"))
        ProcessSaveProject(path);
}

void App::ProcProjectMenus(bool open, bool save)
{
    const bool busy = ProcessBusy();
    const bool canSave = proc_.files && !proc_.meas.empty() && !busy;
    if (open)
    {
        if (ImGui::MenuItem("Новый проект кренования", "Ctrl+N", false, !busy && (proc_.dir[0] || !proc_.outDir.empty())))
            ProcessNewProject();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Записи этой папки заново — с чистыми плечами и без прежних правок положений\n"
                              "(сохранённый проект не трогается)");
        if (ImGui::MenuItem("Открыть файл проекта…", "Ctrl+O", false, !busy))
            ProcOpenProjectDialog();
        if (!settings_.recentProjects.empty())
        {
            ImGui::Separator();
            Muted("Недавние проекты");
            for (std::size_t i = 0; i < settings_.recentProjects.size(); i++)
            {
                const std::string& rp = settings_.recentProjects[i];
                std::error_code ec;
                const bool exists = fs::exists(text::PathFromUtf8(rp), ec);
                const fs::path pp = text::PathFromUtf8(rp);
                const std::string label = text::PathToUtf8(pp.parent_path().filename()) + " \\ " +
                                          text::PathToUtf8(pp.filename()) + "##recent" + std::to_string(i);
                if (ImGui::MenuItem(label.c_str(), nullptr, rp == proc_.projectPath, exists && !busy))
                {
                    ProcessOpenProject(rp);
                    break;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s%s", Shown(rp).c_str(), exists ? "" : "\n(файла нет — переложен или удалён)");
            }
        }
    }
    if (open && save)
        ImGui::Separator();
    if (save)
    {
        const std::string target = ProcProjectTarget();
        if (ImGui::MenuItem("Сохранить проект", "Ctrl+S", false, canSave && !target.empty()))
            ProcessSaveProject();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !target.empty())
            ImGui::SetTooltip("%s", Shown(target).c_str());
        if (ImGui::MenuItem("Сохранить проект как…", "Ctrl+Shift+S", false, canSave))
            ProcSaveProjectAsDialog();
        const std::string folder = proc_.outDir.empty() ? std::string(proc_.dir) : proc_.outDir;
        if (ImGui::MenuItem("Открыть папку проекта", nullptr, false, !folder.empty()))
            dialogs::OpenFolder(folder);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !folder.empty())
            ImGui::SetTooltip("%s — в Проводнике (записи, отчёт, проект)", Shown(folder).c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Автосохранение рядом с записями", nullptr, settings_.projectAutosave))
        {
            settings_.projectAutosave = !settings_.projectAutosave;
            proc_.projAutoFailed = false;
            SaveSettingsSoon();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("«Опыт.krenomer» в папке записей — через 3 с после правки; папка с ним открывается\n"
                              "вместе с проектом (плечи, положения, правки — как были)");
        FontScope f(fontSmall);
        if (proc_.projectPath.empty())
            Muted("Проект не сохранён");
        else
            Muted("Проект: %s", Shown(proc_.projectPath).c_str());
    }
}

// Кнопки проекта в шапке «Обработки», от x вправо: «Открыть проект ▼» и «Сохранить проект» с «▼» (узкое окно — одна
// «Проект ▼»). measure — только ширина.
float App::ProcProjectButtons(float x, bool narrow, bool measure)
{
    const float gap = S(8);
    const float arrowW = ImGui::GetFrameHeight();
    const float openW = ImGui::CalcTextSize("Открыть проект ▼").x + S(24);
    const float saveW = ImGui::CalcTextSize("Сохранить проект").x + S(24);
    const float oneW = ImGui::CalcTextSize("Проект ▼").x + S(24);
    const float total = narrow ? oneW : openW + gap + saveW + S(2) + arrowW;
    if (measure)
        return total;
    const float y = ImGui::GetCursorPosY();
    auto popupAt = [&](const char* id) {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(ImVec2(a.x, b.y + S(4)), ImGuiCond_Appearing);
        (void) id;
    };
    const bool busy = ProcessBusy();
    const bool canSave = proc_.files && !proc_.meas.empty() && !busy;
    ImGui::SetCursorPos(ImVec2(x, y));
    if (narrow)
    {
        if (Button("Проект ▼", BtnKind::Normal, ImVec2(oneW, 0)) || proc_.projMenu > 0)
        {
            ImGui::OpenPopup("##proj_all");
            proc_.projMenu = 0;
        }
        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##proj_all"))
            ImGui::SetTooltip("Проект опыта (*.krenomer): новый (Ctrl+N), открыть (Ctrl+O), сохранить (Ctrl+S),\n"
                              "сохранить как (Ctrl+Shift+S), недавние, автосохранение");
        popupAt("##proj_all");
        if (ImGui::BeginPopup("##proj_all"))
        {
            ProcProjectMenus(true, true);
            if (proc_.projMenu < 0)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (proc_.projMenu < 0)
            proc_.projMenu = 0;
        return total;
    }
    if (Button("Открыть проект ▼", BtnKind::Normal, ImVec2(openW, 0)) || proc_.projMenu == 1)
    {
        ImGui::OpenPopup("##proj_open");
        proc_.projMenu = 0;
    }
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##proj_open"))
        ImGui::SetTooltip("Открыть сохранённый опыт (*.krenomer): записи, параметры, положения и правки");
    popupAt("##proj_open");
    if (ImGui::BeginPopup("##proj_open"))
    {
        ProcProjectMenus(true, false);
        if (proc_.projMenu < 0)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, gap);
    const std::string target = ProcProjectTarget();
    if (Button("Сохранить проект", BtnKind::Normal, ImVec2(saveW, 0), !canSave || target.empty(),
               busy ? "Идёт чтение или сохранение" : "Нет прочитанных записей"))
        ProcessSaveProject();
    if (ImGui::IsItemHovered() && canSave && !target.empty())
        ImGui::SetTooltip("Сохранить проект опыта (Ctrl+S): %s", Shown(target).c_str());
    ImGui::SameLine(0, S(2));
    const ImVec2 saveMin = ImGui::GetItemRectMin();
    if (Button("▼##projsave", BtnKind::Normal, ImVec2(arrowW, 0)) || proc_.projMenu == 2)
    {
        ImGui::OpenPopup("##proj_save");
        proc_.projMenu = 0;
    }
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##proj_save"))
        ImGui::SetTooltip("Сохранить как…, автосохранение");
    ImGui::SetNextWindowPos(ImVec2(saveMin.x, ImGui::GetItemRectMax().y + S(4)), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##proj_save"))
    {
        ProcProjectMenus(false, true);
        if (proc_.projMenu < 0)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (proc_.projMenu < 0)
        proc_.projMenu = 0;
    return total;
}

} // namespace ui
