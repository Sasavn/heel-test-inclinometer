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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../comm/DeviceLink.hpp"
#include "../comm/SimDevice.hpp"
#include "../core/Download.hpp"
#include "../core/HeelData.hpp"
#include "../core/PcRecorder.hpp"
#include "../core/PcSettings.hpp"
#include "../fw/Firmware.hpp"

namespace ui
{

inline constexpr const char* kAppVersion = "1.2";

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
    void ProcessSaveReport();
    const heel::Summary& ProcessSummary() const { return proc_.sum; }
    std::size_t ProcessMeasurements() const { return proc_.meas.size(); }

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
    void ProcLog(const std::string& s);
    void ProcParams(float w);
    void ProcTable(float w, float h);
    void ProcResult(float w, float h);
    void ProcPlots(float w, float h);
    void ProcPanes(int post, float w, float h, bool stacked, bool forExport);
    void ProcExportPage(int post, float w, float h);

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
        std::shared_ptr<const std::vector<heel::File>> files; // прочитанные файлы
        std::vector<heel::Measurement> meas;
        heel::Summary sum;
        std::map<int, int> postOf;                            // датчик (heel::SensorKey) -> пост: 0, 1, -1 — нет
        std::vector<int> keys;                                // датчики прочитанных файлов
        std::map<int, double> arm;                            // плечо, заданное в таблице: замер -> |l|, м
        std::map<int, bool> off;                              // замеры, снятые с расчёта
        std::vector<std::string> log;                         // журнал (коротко)
        std::future<heel::Loaded> load;
        std::shared_ptr<std::atomic<int>> loadDone;
        int loadTotal = 0;
        std::future<std::vector<std::string>> save; // отчёт: строки для журнала
        bool saveRequested = false;                 // картинки графиков — в Tick, между кадрами
        int show = 0;                               // графики: 0 — нос, 1 — корма
        bool opened = false, dirty = false, logScroll = false;
    } proc_;
};

} // namespace ui
