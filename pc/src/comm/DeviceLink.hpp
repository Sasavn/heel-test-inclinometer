#pragma once
// Связь с прибором: очередь команд, сборка ответов, поток stream, опрос status, история для графиков, журнал
// терминала. Работает в своём потоке (или по Step() из тестов и стенда снимков — с модельным временем); интерфейс
// не ждёт порт никогда: Send() только ставит команду в очередь и сразу возвращает Request, готовность которого
// интерфейс проверяет в следующих кадрах (Request::done).
//
// Порядок: одна команда «в полёте»; её ответ — строки до OK…/ERR… (строки S и шапка потока — отдельно, в любой
// момент). Подключение: порт открылся -> через 200 мс ver (до трёх попыток) -> ответ «BWM427…» — прибор наш
// (stream N, status), иначе порт отвергается. Связь зависла (3 команды подряд без ответа) — порт переоткрывается.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Connection.hpp"
#include "../core/History.hpp"
#include "../core/Protocol.hpp"

class DownloadSink;

enum class Origin
{
    User,     // терминал
    App,      // кнопки интерфейса
    Poll,     // опрос status (в терминале скрыт)
    Internal, // ver при подключении, stream
};

struct Request
{
    std::string cmd;
    Origin origin = Origin::App;
    int timeoutMs = 3000;
    bool inactivity = false; // таймаут — от последней строки ответа (get)
    bool syncAbort = false;  // get abort после подключения: ответ — только «OK aborted» / «OK nothing to abort»
    std::shared_ptr<DownloadSink> sink;

    // Итог (читать после done == true)
    std::atomic<bool> done{false};
    bool ok = false;           // последняя строка OK…
    bool timedOut = false;     // нет ответа
    bool linkLost = false;     // связь пропала / не подключено
    bool aborted = false;      // get abort
    std::vector<std::string> lines; // строки ответа без последней (у get — пусто)
    std::string final;         // последняя строка или описание ошибки

    // Служебное (поток связи)
    std::int64_t deadline = 0;
    std::uint64_t dataLines = 0;
};

using RequestPtr = std::shared_ptr<Request>;

enum class LinkState
{
    Off,       // отключено пользователем
    Searching, // порт не открыт: ищем прибор
    Probing,   // порт открыт, ждём ответа на ver
    Connected,
};

struct TermLine
{
    std::uint64_t seq = 0;
    std::int64_t ms = 0;
    char kind = '<';   // '>' — команда, '<' — ответ, '!' — событие связи, 'S' — поток
    Origin origin = Origin::App;
    std::string text;
};

// Копия состояния для кадра интерфейса.
struct LinkSnapshot
{
    LinkState state = LinkState::Searching;
    bool demo = false;
    std::string port;
    std::string problem;         // почему не подключено
    std::vector<PortInfo> ports;

    proto::VersionInfo ver;
    bool haveVer = false;

    proto::DeviceStatus status;
    bool haveStatus = false;
    std::int64_t statusAtMs = 0;
    std::uint64_t statusSeq = 0;

    proto::StreamSample sample;
    bool haveSample = false;
    std::int64_t sampleAtMs = 0;
    double streamHz = 0.0;       // строк S в секунду
    int streamMs = 0;            // заказанный период

    std::int64_t nowMs = 0;
    std::int64_t connectedAtMs = 0;
    std::uint64_t connectSeq = 0; // растёт при каждом подключении
    std::uint64_t linesRx = 0, cmdsTx = 0, timeouts = 0, reconnects = 0, badLines = 0;
    bool busy = false;           // есть команды в очереди / в полёте
    bool pollPaused = false;
    bool getActive = false;
};

class DeviceLink
{
public:
    // threaded = false: поток не запускается, время двигает Step(nowMs) снаружи (тесты, снимки экрана).
    DeviceLink(std::unique_ptr<IConnection> conn, bool threaded = true);
    ~DeviceLink();

    DeviceLink(const DeviceLink&) = delete;
    DeviceLink& operator=(const DeviceLink&) = delete;

    // Один шаг: порт, принятые строки, очередь, таймауты, опрос. Вызывает поток связи (или тест).
    void Step(std::int64_t nowMs);
    std::int64_t NowMs() const { return now_.load(); }

    // Команда в очередь. Ответ — в Request (done, ok, lines, final).
    RequestPtr Send(const std::string& cmd, Origin origin = Origin::App, int timeoutMs = 3000);
    // get <name>: строки G/D/E — в sink; таймаут — 5 с тишины.
    RequestPtr SendGet(const std::string& name, std::shared_ptr<DownloadSink> sink);
    // Прервать идущую передачу (get abort вне очереди).
    void AbortTransfer();

    void SetEnabled(bool on);
    void SetPortChoice(const std::string& port);
    void SetStreamPeriod(int ms);
    // Пауза опроса status и замедление потока до kPausedStreamMs (скачивание файлов: строки D идут быстрее,
    // а графики «Измерения» всё равно живут).
    void SetPollPaused(bool paused);
    static constexpr int kPausedStreamMs = 500;
    // Быстрее опрашивать status (смена адреса — ждём итог): до момента untilMs, период periodMs.
    void FastPoll(std::int64_t untilMs, int periodMs = 300);

    LinkSnapshot Snapshot() const;
    bool IsConnected() const { return state_.load() == LinkState::Connected; }
    bool IsDemo() const;
    History& GetHistory() { return history_; }
    IConnection* Connection() { return conn_.get(); }

    // Строки журнала с seq > after (не больше max последних).
    std::vector<TermLine> TermSince(std::uint64_t after, std::size_t max = 2000) const;
    void Log(char kind, const std::string& text, Origin origin = Origin::App);

private:
    void Run();
    void OnOpened(std::int64_t now);
    void OnClosed(std::int64_t now, const std::string& why);
    void HandleLine(const std::string& line, std::int64_t now);
    void Complete(const RequestPtr& r, bool ok, const std::string& final, std::int64_t now);
    void FailAll(const std::string& why, bool linkLost);
    void AfterReply(const RequestPtr& r, std::int64_t now);
    void LogLocked(char kind, const std::string& text, Origin origin, std::int64_t now);

    std::unique_ptr<IConnection> conn_;
    History history_;

    // Принятые строки (поток порта -> поток связи)
    std::mutex rxMutex_;
    std::vector<std::string> rx_;

    // Всё остальное — под m_
    mutable std::mutex m_;
    std::deque<RequestPtr> queue_;
    RequestPtr inFlight_;
    std::atomic<LinkState> state_{LinkState::Searching};
    std::uint64_t lastOpenCount_ = 0;
    bool wasOpen_ = false;
    std::int64_t probeAt_ = 0;
    int probeTries_ = 0;
    RequestPtr probe_;
    proto::VersionInfo ver_;
    bool haveVer_ = false;
    proto::DeviceStatus status_;
    bool haveStatus_ = false;
    std::int64_t statusAt_ = 0;
    std::uint64_t statusSeq_ = 0;
    proto::StreamHeader header_;
    proto::StreamSample sample_;
    bool haveSample_ = false;
    std::int64_t sampleAt_ = 0;
    std::deque<std::int64_t> sampleTimes_;
    int streamMs_ = 100;
    std::int64_t lastStreamCmd_ = -100000;
    std::int64_t lastStatusReq_ = -100000;
    std::int64_t fastPollUntil_ = 0;
    int fastPollMs_ = 300;
    bool pollPaused_ = false;
    bool streamDirty_ = false;
    bool abortWanted_ = false;
    bool expectAbortReply_ = false;
    std::int64_t abortSentAt_ = 0;
    int consecutiveTimeouts_ = 0;
    std::int64_t connectedAt_ = 0;
    std::uint64_t connectSeq_ = 0;
    std::uint64_t linesRx_ = 0, cmdsTx_ = 0, timeouts_ = 0, reconnects_ = 0, badLines_ = 0;
    std::string lastProblem_;
    bool rejectWanted_ = false;
    bool probeOurs_ = false;  // в ответе на ver была строка BWM427…
    bool probeStray_ = false; // до неё — хвост чужой передачи (строки D/E/G/F, лишний OK)
    bool rejectForeign_ = false;

    std::deque<TermLine> term_;
    std::uint64_t termSeq_ = 0;

    std::atomic<std::int64_t> now_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::condition_variable cv_;
    std::mutex cvMutex_;
};
