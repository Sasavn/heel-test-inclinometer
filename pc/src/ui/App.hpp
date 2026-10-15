#pragma once
// Окно программы: шапка (связь, порт, демо), меню слева и страницы — «Измерение», «Диагностика», «Настройки»,
// «Файлы на карте», «Обработка», «Прошивка», «Терминал». Состояние страниц — здесь; отрисовка — App*.cpp.
//
// Поток интерфейса не ждёт прибор никогда: команды уходят в очередь DeviceLink, результат проверяется в следующих
// кадрах; скачивание файлов и перепрошивка — конечные автоматы, которые двигает Tick() (и при свёрнутом окне).
#include <atomic>
#include <cstdint>
#include <ctime>
#include <future>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../comm/DeviceLink.hpp"
#include "../comm/SimDevice.hpp"
#include "../core/Download.hpp"
#include "../core/HeelData.hpp"
#include "../core/HeelRs.hpp"
#include "../core/UserMethod.hpp"
#include "../core/PcRecorder.hpp"
#include "../core/PcSettings.hpp"
#include "../fw/Firmware.hpp"

namespace ui
{

inline constexpr const char* kAppVersion = "1.5.1";

enum class Page
{
    Measure,
    Diag,
    Settings,
    Files,
    Firmware,
    Terminal,
    Process, // «Обработка» (опыт кренования по файлам)
    Count
};

struct AppOptions
{
    bool threaded = true;         // поток связи (false — время двигает Link().Step() снаружи: стенд снимков)
    bool startDemo = false;       // сразу демо-режим
    bool useSettingsFile = true;  // читать/писать %APPDATA%\Krenomer\krenomer.ini
    std::time_t simStart = 0;     // часы имитатора (0 — сейчас)
    std::uint32_t simSeed = 12345;
    bool noHardware = false;      // без демо — «прибора нет», COM-порты не трогать (стенд снимков)
    std::string editsDir;         // правки положений «Обработки»; пусто — %APPDATA%\Krenomer\edits (если useSettingsFile)
};

struct Toast
{
    std::string text;
    int kind = 0; // 0 — информация, 1 — успех, 2 — ошибка
    std::int64_t until = 0;
};

class App
{
public:
    explicit App(AppOptions opt = {});
    ~App();

    void Tick();   // логика (каждый проход цикла окна, и свёрнутого)
    void Render(); // весь интерфейс (между ImGui::NewFrame и ImGui::Render)

    std::string WindowTitle() const;
    bool DarkTheme() const { return settings_.darkTheme; }
    bool ThemeChanged()
    {
        const bool c = themeChanged_;
        themeChanged_ = false;
        return c;
    }

    // --- Для стенда снимков и тестов ---
    // Пути на экране и в отчётах: начало «из» заменяется на «в» (стенд снимков для README показывает нейтральные
    // папки вместо настоящих). Файлы пишутся по настоящим путям. В обычной программе список пуст.
    void SetShownPaths(std::vector<std::pair<std::string, std::string>> map) { shownPaths_ = std::move(map); }
    std::string Shown(const std::string& s) const;
    void SetPage(Page p) { page_ = p; }
    Page GetPage() const { return page_; }
    DeviceLink& Link() { return *link_; }
    SimDevice* Sim() { return sim_.get(); }
    void StartDemo(bool on);
    std::int64_t Now() const { return link_->NowMs(); }
    void RequestFileList();
    void SelectAllFiles(bool on);
    void StartDownload();
    std::size_t FileCount() const { return files_.list.size(); }
    bool DownloadRunning() const { return dl_.running; }
    void SetDownloadDir(const std::string& dir);
    void SetFirmwarePath(const std::string& path);
    bool StartFirmwareUpdate(bool force);
    fw::Updater& Updater() { return updater_; }
    void StartAddressChange(int from, int to);
    void SetChartWindow(int seconds) { settings_.chartWindowS = seconds; }
    void TerminalSend(const std::string& cmd);
    void EditSetting(const std::string& name, double value); // как ввод в поле «Настроек»
    void ApplySettings();
    void SyncTime();
    void ShowAbout(bool on) { aboutOpen_ = on; }
    void CloseDiagText() { diag_.closeText = true; }
    void RequestDiag()
    {
        if (!diag_.diagReq)
            diag_.diagReq = link_->Send("diag", Origin::App, 4000);
    }
    void ShowDemoPanel(bool on) { demoPanel_ = on; }
    // Запись на ПК (AppRecord.cpp)
    bool StartPcRecording();
    void StopPcRecording();
    bool PcRecording() const { return pcrec_.rec && pcrec_.rec->Running(); }
    void SetRecordDir(const std::string& dir, bool xlsx);
    void SetRecordLabel(const std::string& label);
    const pcrec::Recorder* PcRecorder() const { return pcrec_.rec.get(); }
    // Обработка (AppProcess.cpp)
    void ProcessOpen(const std::string& dir); // папка с файлами замеров -> чтение в фоне
    bool ProcessBusy() const;                 // идёт чтение или сохранение отчёта
    void ProcessSetArm(int number, double arm);
    void ProcessShowPost(int post) { proc_.show = post; }
    // Свой метод: добавить (или заменить по имени) и выбрать в расчёте; открыть окно формул
    void ProcessUseUserMethod(const um::Method& m, bool apply = true);
    void ProcessShowMethodWindow(bool on) { proc_.methodWin = on; }
    void ProcessSetSensorName(int key, const std::string& name)
    {
        proc_.names[key] = name;
        proc_.dirty = true;
        ProcRefreshLogNames();
    }
    void ProcessSaveReport();
    const heel::Summary& ProcessSummary() const { return proc_.sum; }
    std::size_t ProcessMeasurements() const { return proc_.meas.size(); }
    bool ProcessContinuous() const { return proc_.mode == 1 && proc_.rec >= 0; } // «одна запись — весь опыт»
    void ProcessSetMode(int mode);                                              // 0 — файл = замер, 1 — запись
    const std::vector<heel::Position>& ProcessPositions() const { return proc_.pos; }
    const heel::Summary& ProcessPositionsSummary() const { return proc_.posSum; }
    void ProcessSetPositionArm(int index, double arm);
    void ProcessSelectPosition(int index) { proc_.sel = index; }
    int ProcessAxis() const { return settings_.heelAxis; }
    void ProcessSetSmallShip(bool model) { ProcSetSmallShip(model); }
    void ProcessSetArmDefault(double L) { settings_.heelL = L; proc_.dirty = true; }
    // Правила РС: вид карточки графиков (0 — крен, 1 — график контроля, 2 — проверка), данные 6.6.3, основной
    // расчёт, ручное «исключить / учитывать» замера табл. 6.5 (номер положения или замера, -1 — авто)
    void ProcessSetView(int view) { proc_.view = view; }
    void ProcessSetRsInfo(double h, double lMax, double D0)
    {
        settings_.rsH = h;
        settings_.rsLmax = lMax;
        settings_.rsD0 = D0;
        proc_.dirty = true;
    }
    // Основной расчёт h — для текущего режима момента (в режиме групп -1 — «не выбран»: табл. 6.5)
    void ProcessSetRsMain(int main)
    {
        (settings_.rsGroupMode ? settings_.rsMainGroups : settings_.rsMain) = main;
        proc_.dirty = true;
    }
    // Табл. 6.5 по среднему приборов (-1) или по посту; поправки Δh и Δ наихудшего варианта; судно (D, P)
    void ProcessSetRsInstrument(int post) { settings_.rsInstrument = post; proc_.dirty = true; }
    void ProcessSetRsCorrections(double dh, double Dworst)
    {
        settings_.rsDh = dh;
        settings_.rsDworst = Dworst;
        proc_.dirty = true;
    }
    void ProcessSetShip(double D, double P)
    {
        settings_.heelD = D;
        settings_.heelP = P;
        proc_.dirty = true;
    }
    // Крен-балласт группами: режим, группы, плечо перемещения группы на шаге (0 — не двигали)
    void ProcessSetRsGroupMode(bool on) { settings_.rsGroupMode = on; proc_.dirty = true; }
    void ProcessSetRsGroups(const std::vector<heel::RsGroup>& g);
    void ProcessSetRsMove(int step, int group, double arm);
    // Знак датчика: true — обратный (ось +X — к левому борту); положения вручную (стенд): участки t0…t1, с
    void ProcessSetSensorInvert(int key, bool inverted);
    bool ProcessSensorInverted(int key) const { return proc_.invert.count(key) > 0; }
    void ProcessSetPositionSpans(const std::vector<std::pair<double, double>>& spans);
    void ProcessSetRsManual(int number, int state);
    const heel::RsCase& ProcessRs() const { return proc_.rs; }

private:
    // Каркас
    void RenderTopBar(float h);
    void RenderSidebar(float w, float h);
    void RenderPage();
    void RenderToasts();
    void RenderAbout();
    void RenderDemoPanel();
    void NotConnectedPanel(const char* what);
    void Notify(const std::string& text, int kind = 0, int ms = 4000);
    void MakeLink(bool demo);
    bool Connected() const { return snap_.state == LinkState::Connected; }
    bool HaveStatus() const { return Connected() && snap_.haveStatus; }
    bool Recording() const { return HaveStatus() && snap_.status.Recording(); }

    // Страницы (App*.cpp)
    void PageMeasure();
    void PageDiag();
    void PageSettings();
    void PageFiles();
    void PageFirmware();
    void PageTerminal();

    // Части страниц
    void MeasureBanner(float w, float h);
    void MeasureTiles(float w, float h);
    void SensorCard(int idx, float w, float h);
    void Charts(float w, float h);
    void SettingsDeviceCard();
    void SettingsClockCard();
    void SettingsZeroCard();
    void SettingsAddrCard();
    void SettingsPcCard();
    void TickSettings();
    void TickFiles();
    void TickDiag();
    void RecordCard(float w, bool full); // «Запись на ПК» на странице «Измерение»
    void TickRecord();
    void PageProcess();
    void TickProcess();
    heel::Settings ProcSettings() const;
    void ProcLoad();      // прочитать файлы (выбранные или все *.CSV папки) в фоне
    void ProcRegroup(bool quiet = false); // замеры и посты заново (после чтения, смены постов; quiet — без журнала)
    void ProcRecompute(); // окно, h, итог
    void ProcLog(const std::string& s, const std::string& tip = {});
    void ProcParams(float w);
    void ProcTable(float w, float h);
    void ProcResult(float w, float h);
    void ProcPlots(float w, float h);
    void ProcPanes(int post, float w, float h, bool stacked, bool forExport);
    void ProcExportPage(int post, float w, float h);
    // Непрерывная запись (одна запись — весь опыт)
    bool ProcCont() const { return proc_.mode == 1 && proc_.rec >= 0 && proc_.rec < static_cast<int>(proc_.meas.size()); }
    void ProcChooseMode(bool fromLoad); // режим и запись: по содержимому (если не выбран вручную)
    void ProcFindPositions(bool restore = true); // положения заново (поиск по основному посту; restore — с правками)
    void ProcSortPositions();
    void ProcPositions(float w, float h);
    void ProcSeriesCard(float w, float h);
    void ProcModeSwitch(float right);
    void ProcFilterButton();                        // «Обработка: метод ▾» — метод и параметры обработки сигнала
    void ProcPositionPlot(int post, int index, float w, float h, bool forExport); // положение крупно: 3 линии
    void ProcExportPositions(int post, float w, float h);                          // все положения — сеткой (PNG)
    void ProcSetSmallShip(bool model);
    // Посты = датчики со своими названиями
    void ProcAssignPosts();                         // датчики -> посты 0…3 (по порядку, без снятых)
    std::string SensorTitle(int key) const;         // название датчика (своё или по умолчанию)
    std::string PostTitle(int post) const;          // «Нос (Д2)»
    std::string SensorTitleOfPost(int post) const;  // «Нос»
    std::vector<int> PostsInUse() const;            // посты, у которых есть файлы (в замерах или записи)
    void ProcSensorNames(float right, const std::function<void(float)>& flow); // поля названий датчиков
    void SaveSensorNames();
    std::string ProcSummaryText() const;            // «Замеров: …, файлов: … (Д2 — Нос, …; формат)»
    std::string ProcRecordText() const;             // «Одна запись — весь опыт: файл (пост), …»
    void ProcRefreshLogNames();                     // строки журнала с постами — по текущим названиям
    void ProcApplyInvert();                         // files = filesRaw с обратным знаком у обращённых датчиков
    void ProcLoadInvert();
    // Свои методы (формулы, core/UserMethod.hpp)
    void ProcLoadMethods();
    void ProcSaveMethod(const um::Method& m);
    void ProcDeleteMethodFile(const std::string& name);
    std::shared_ptr<const um::Compiled> ProcUserCompiled(const um::Method& m, um::Problem* problem) const;
    void ProcMethodWindow();                        // окно «Свой метод обработки»
    void ProcMethodPreview(const um::Method& m, float w, float h);
    std::string NumFmt(double v, int digits) const;
    std::filesystem::path EditsDir() const;         // правки положений; пусто — не сохранять
    std::string EditsKeyNow() const;                // ключ правок текущей записи (пусто — нет)
    void ProcSeriesPlot(int post, float w, float h, bool forExport);
    void ProcExportSeries(int post, float w, float h);
    int ProcShownPost() const;
    // Правила РС (AppRules.cpp)
    void ProcViewSwitch();                                      // «Крен | Контроль | Правила РС» в заголовке карточки
    void ProcControlPlot(float w, float h, bool forExport);     // график контроля «кренящий момент – tg θ» (п. 5.5)
    void ProcExportControl(float w, float h);                   // он же — картинкой для отчёта
    void ProcRsView(float w, float h);                          // требования, оценка 6.6, табл. 6.5
    std::string ProcRsStatus(int* state) const;                 // строка итога проверки для карточки h
    void ProcResultStrip(float w);                              // свёрнутая карточка h (виды Правил РС)
    void ProcMovesView(float w, float h);                       // группы крен-балласта и перемещения, табл. 6.5 полностью
    const std::vector<heel::RsGroup>& ProcGroups() const;       // группы (из настроек при первом обращении)
    void ProcSaveGroups();
    void ProcSaveButtons(float saveW, float openW);             // «Сохранить отчёт», «Открыть папку»
    void ProcLogLines();                                        // строки журнала
    void SaveSettingsSoon(); // настройки — в файл через ~1 с (не только при выходе)
    void SaveSettingsNow();
    // Поле пути: как InputText, а при подмене путей (стенд) — показ подменённого пути без правки.
    bool PathInput(const char* id, char* buf, std::size_t size, int flags = 0);
    std::vector<std::pair<std::string, std::string>> shownPaths_;

    AppOptions opt_;
    PcSettings settings_;
    std::shared_ptr<SimDevice> sim_;
    std::unique_ptr<DeviceLink> link_;
    std::unique_ptr<fw::IDfuBackend> dfuReal_, dfuDemo_;
    fw::Updater updater_;
    LinkSnapshot snap_;
    Page page_ = Page::Measure;
    std::vector<Toast> toasts_;
    bool themeChanged_ = false;
    bool aboutOpen_ = false;
    bool demoPanel_ = true;
    int portIndex_ = 0;
    std::uint64_t lastConnectSeq_ = 0;
    float bottomH_ = 0.f; // «Файлы»: высота нижней панели (прошлый кадр)

    // Измерение
    struct
    {
        bool paused = false;
        double pausedAt = 0.0;
    } meas_;

    // Диагностика
    struct
    {
        RequestPtr diagReq, resetReq;
        std::vector<std::string> diagText;
        std::string diagAt, resetMsg;
        bool showText = false;  // открыть окно со снимком diag
        bool closeText = false; // закрыть его (стенд снимков)
    } diag_;

    // Настройки
    struct SetField
    {
        const char* key; // freq, alpha, …
        double value = 0;
        bool edited = false;
    };
    struct
    {
        SetField f[9] = {{"freq"}, {"alpha"}, {"gap"}, {"rollwin"}, {"rollhz"}, {"rollcalm"}, {"rollhyst"}, {"batalarm"},
                         {"theme"}};
        std::vector<std::pair<int, RequestPtr>> applying; // (поле, команда set)
        std::vector<std::string> applyLog;
        bool applyOk = true;
        RequestPtr timeReq;
        std::string timeMsg;
        bool timeOk = true;
        RequestPtr zeroReq;
        std::string zeroMsg;
        // смена адреса
        int addrFrom = 1, addrTo = 2;
        bool addrConfirm = false, addrOnlyOne = false;
        int addrPhase = 0; // 0 — нет, 1 — команда, 2 — ждём итог, 3 — успех, 4 — ошибка
        RequestPtr addrReq;
        std::int64_t addrDeadline = 0;
        std::uint64_t addrSeq = 0;
        std::string addrMsg;
        int streamMs = 100;
    } set_;

    // Файлы
    struct FileJob
    {
        proto::FileEntry f;
        std::shared_ptr<DownloadSink> sink;
        RequestPtr req;
        int state = 0; // 0 — ждёт, 1 — идёт, 2 — готово, 3 — ошибка, 4 — пропущен
        int attempts = 0;
        std::string msg;
    };
    struct
    {
        std::vector<proto::FileEntry> list;
        std::map<std::string, bool> sel;
        std::map<std::string, std::string> note; // имя -> «скачан …» / ошибка
        std::map<std::string, int> noteKind;     // 1 — успех, 2 — ошибка, 3 — есть в папке
        RequestPtr listReq;
        std::string listMsg;
        bool listed = false;
        char dir[1024] = {};
    } files_;
    struct
    {
        std::vector<FileJob> jobs;
        int current = -1;
        bool running = false, cancel = false;
        std::int64_t startMs = 0, fileStartMs = 0, waitUntil = 0;
        std::uint64_t doneBytes = 0;
        double speed = 0.0; // байт/с
        std::uint64_t lastBytes = 0;
        std::int64_t lastSpeedMs = 0;
        std::string summary;
    } dl_;

    // Запись на ПК
    struct
    {
        std::unique_ptr<pcrec::Recorder> rec;
        char dir[1024] = {};
        char label[96] = {};
        bool linkLost = false;   // связь пропала во время записи (перерыв)
        bool converting = false; // ждём книгу .xlsx
        std::vector<RxSample> buf;
    } pcrec_;

    // Прошивка
    struct
    {
        char path[1024] = {};
        std::string checkedPath;
        std::int64_t checkedMtime = 0;
        fw::ImageInfo image;
        bool imageRead = false;
        std::string dfuPath;
        std::vector<std::string> dfuSearched;
        std::int64_t dfuCheckedAt = -100000;
        bool dfuPresent = false, dfuDriver = false;
        bool confirm = false, confirmForce = false;
        RequestPtr bootReq;
    } fw_;

    // Терминал
    struct
    {
        std::vector<TermLine> lines;
        std::uint64_t lastSeq = 0;
        char input[256] = {};
        std::vector<std::string> history;
        int histPos = -1;
        bool scrollToEnd = true;
        bool focusInput = false;
    } term_;

    // Обработка
    struct
    {
        char dir[1024] = {};                                  // папка с файлами замеров
        std::vector<std::string> picked;                      // выбранные файлы (пусто — все *.CSV папки)
        std::string outDir;                                   // папка прочитанных файлов — туда отчёт
        std::shared_ptr<const std::vector<heel::File>> files; // прочитанные файлы (обращённые датчики — с обратным знаком)
        std::shared_ptr<const std::vector<heel::File>> filesRaw; // как прочитаны
        std::set<int> invert;                                 // датчики с обратным знаком (ось +X — к ЛБ)
        bool invertLoaded = false;
        std::vector<heel::Measurement> meas;
        heel::Summary sum;
        std::map<int, int> postOf;                            // датчик (heel::SensorKey) -> пост 0…3, -1 — не учитывать
        std::map<int, std::string> names;                     // датчик -> название поста («Нос», «Мидель», …)
        std::set<int> skip;                                   // датчики, снятые с расчёта
        bool namesLoaded = false;
        // Свои методы обработки (формулы)
        std::vector<um::Method> methods;      // сохранённые (Dir()/methods/*.krenomer-method)
        bool methodsLoaded = false;
        bool methodWin = false;               // окно «Свой метод обработки»
        int methodSel = -1;                   // выбранный в окне
        std::string cacheText;                // текст метода, для которого собран cache
        std::shared_ptr<const um::Compiled> cache;
        std::string methodMsg;                // сообщение окна (сохранено, импортировано, ошибка файла)
        std::vector<int> keys;                                // датчики прочитанных файлов
        std::map<int, double> arm;                            // плечо, заданное в таблице: замер -> |l|, м
        std::map<int, bool> off;                              // замеры, снятые с расчёта
        std::vector<std::pair<std::string, std::string>> log; // журнал (коротко): строка и подсказка при наведении
        std::future<heel::Loaded> load;
        std::shared_ptr<std::atomic<int>> loadDone;
        int loadTotal = 0;
        std::future<std::vector<std::string>> save; // отчёт: строки для журнала
        bool saveRequested = false;                 // картинки графиков — в Tick, между кадрами
        int show = 0;                               // графики: 0 — нос, 1 — корма
        bool opened = false, dirty = false, logScroll = false;
        // Непрерывная запись
        int mode = 0;                     // 0 — файл = замер, 1 — одна запись — весь опыт
        bool modeUser = false;            // режим выбран вручную (иначе — по содержимому при каждом чтении)
        int rec = -1;                     // запись (индекс в meas)
        std::vector<heel::Position> pos;  // положения груза (найдены или поправлены вручную)
        heel::Summary posSum;
        int sel = -1;                     // выбранное положение: его границы двигаются на графике
        bool posEdited = false;           // положения правились руками
        std::int64_t editsSaveAt = 0;     // правки — в файл (через ~1 с после последней)
        std::vector<double> procT[heel::kMaxPosts], procY[heel::kMaxPosts]; // обработанный сигнал записи по постам
        // Правила РС
        int view = 0;                     // карточка графиков: 0 — крен, 1 — график контроля, 2 — Правила РС
        heel::RsCase rs;                  // табл. 6.5, оценка 6.6, требования (ProcRecompute)
        std::map<int, int> rsManual;      // замер табл. 6.5 (номер конечного положения / замера): 0 — исключить, 1 — учитывать
        std::vector<heel::RsGroup> rsGroups;  // группы крен-балласта (сохраняются в настройках)
        bool rsGroupsLoaded = false;
        std::vector<std::vector<double>> rsMoves; // перемещения: [шаг][группа] — плечо со знаком (единицы плеча)
        std::vector<double> groupArm;     // режим групп: эквивалентное плечо положения (накопленный момент / P)
        bool rsLegacyLogged = false;      // в журнале уже сказано про плечи без знака
    } proc_;
    std::int64_t settingsSaveAt_ = 0;     // SaveSettingsSoon: когда записать (0 — не нужно)
};

} // namespace ui
