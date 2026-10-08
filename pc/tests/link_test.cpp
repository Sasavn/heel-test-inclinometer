// Связь с прибором (DeviceLink) без железа: модельное время, прибор-имитатор (тот же протокол, что прошивка) и
// канал-сценарий. Подключение и проверка ver, поток stream -> история, опрос status, команды set / set time / zero /
// addr, files и скачивание с проверкой CRC, отмена передачи, запись (отказы), перезагрузка прибора и
// переподключение, чужой порт, зависший прибор, строки потока посреди ответа; samples (строки R) и запись на ПК
// (pcrec::Recorder): файлы CSV по датчикам, Ms, CalcX, пропуски, обрыв связи и перезагрузка прибора посреди записи,
// книга .xlsx, старая прошивка без samples.
#include <chrono>
#include <cstdio>
#include <deque>
#include <thread>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "TestUtil.hpp"
#include "comm/DeviceLink.hpp"
#include "comm/SimConnection.hpp"
#include "core/Download.hpp"
#include "core/PcRecorder.hpp"

namespace fs = std::filesystem;

namespace
{

struct Rig
{
    std::shared_ptr<SimDevice> sim;
    std::unique_ptr<DeviceLink> link;
    std::int64_t t = 0;

    explicit Rig(std::uint32_t seed = 1)
    {
        sim = std::make_shared<SimDevice>(seed, 1791400000); // часы имитатора — фиксированные
        link = std::make_unique<DeviceLink>(std::make_unique<SimConnection>(sim), false);
    }

    void Run(std::int64_t ms, std::int64_t step = 10)
    {
        for (std::int64_t e = t + ms; t < e; t += step)
            link->Step(t);
    }

    // Ждать (модельно) готовности запроса, не дольше limit мс.
    bool Wait(const RequestPtr& r, std::int64_t limit = 10000)
    {
        for (std::int64_t e = t + limit; t < e && !r->done.load(); t += 10)
            link->Step(t);
        return r->done.load();
    }

    bool WaitConnected(std::int64_t limit = 5000)
    {
        for (std::int64_t e = t + limit; t < e && !link->IsConnected(); t += 10)
            link->Step(t);
        return link->IsConnected();
    }
};

// Канал-сценарий: на каждую команду — заранее заданные строки (или тишина).
class FakeConnection final : public IConnection
{
public:
    std::vector<std::string> written;
    std::function<std::vector<std::string>(const std::string&)> reply;
    bool open = true;
    int rejects = 0, reopens = 0;
    bool lastForeign = false;
    std::uint64_t opens = 1;

    void Tick(std::int64_t) override
    {
        while (!pending.empty())
        {
            EmitLine(pending.front());
            pending.pop_front();
        }
    }
    bool IsOpen() override { return open; }
    bool Write(const std::string& data) override
    {
        std::string cmd = data;
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
            cmd.pop_back();
        written.push_back(cmd);
        if (reply)
            for (auto& l : reply(cmd))
                pending.push_back(l);
        return open;
    }
    void SetEnabled(bool) override {}
    bool Enabled() const override { return true; }
    void Reject(std::int64_t, bool foreign) override
    {
        lastForeign = foreign;
        rejects++;
        open = false;
    }
    void Reopen(std::int64_t) override { reopens++; }
    std::string PortName() override { return open ? "COM9" : ""; }
    std::uint64_t OpenCount() override { return opens; }
    std::vector<PortInfo> Ports() override { return {}; }
    void SetPortChoice(const std::string&) override {}
    bool IsDemo() const override { return false; }
    std::string Problem() override { return {}; }

    std::deque<std::string> pending;
};

std::string ReadAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

static void TestConnectAndStream()
{
    SECTION("подключение: ver, поток, история, status");
    Rig r;
    CHECK(r.WaitConnected(3000));
    auto s = r.link->Snapshot();
    CHECK(s.state == LinkState::Connected && s.haveVer && s.ver.version == "1.5");
    CHECK(s.demo && s.port == "ДЕМО");
    r.Run(3000);
    s = r.link->Snapshot();
    CHECK(s.haveSample && s.sample.sens.size() == 2);
    CHECK_RANGE(s.streamHz, 8.0, 12.0, "строк S в секунду (заказано 100 мс)");
    CHECK(s.haveStatus && s.status.sens.size() == 2 && s.status.freq == 10);
    CHECK(s.statusSeq >= 2); // опрос раз в секунду
    {
        auto& h = r.link->GetHistory();
        std::lock_guard lock(h.mutex);
        CHECK_RANGE(static_cast<double>(h.Size()), 25, 35, "отсчётов в истории за ~3 с");
        if (h.Size() > 2)
        {
            CHECK(h.At(h.Size() - 1).t > h.At(0).t);
            CHECK(std::isfinite(h.At(h.Size() - 1).x[0]) && std::isfinite(h.At(h.Size() - 1).y[1]));
        }
    }
    // Период потока: 200 мс
    r.link->SetStreamPeriod(200);
    r.Run(3000);
    s = r.link->Snapshot();
    CHECK_RANGE(s.streamHz, 4.0, 6.0, "строк S в секунду (200 мс)");
    CHECK(s.timeouts == 0 && s.badLines == 0);
}

static void TestCommands()
{
    SECTION("команды: set, set time, zero, ver, неизвестная");
    Rig r;
    CHECK(r.WaitConnected());
    auto q = r.link->Send("set freq 20");
    CHECK(r.Wait(q) && q->ok && q->final.rfind("OK freq=20 Hz", 0) == 0);
    q = r.link->Send("set alpha 0.5");
    CHECK(r.Wait(q) && q->ok && q->final.rfind("OK alpha=0.50", 0) == 0);
    q = r.link->Send("set freq 99"); // прибор ограничивает сам
    CHECK(r.Wait(q) && q->ok && q->final.rfind("OK freq=50 Hz", 0) == 0);
    q = r.link->Send("set freq abc");
    CHECK(r.Wait(q) && !q->ok && q->final.rfind("ERR usage", 0) == 0);
    q = r.link->Send("set time 2026-10-08 09:30:00");
    CHECK(r.Wait(q) && q->ok && q->final == "OK time=2026-10-08 09:30:00 (rtc yes)");
    q = r.link->Send("set time 2026-13-08 09:30:00");
    CHECK(r.Wait(q) && !q->ok);
    r.Run(1500);
    auto s = r.link->Snapshot();
    CHECK(s.status.freq == 50 && s.status.time.rfind("2026-10-08 09:30:0", 0) == 0);
    q = r.link->Send("zero");
    CHECK(r.Wait(q) && q->ok && q->final == "OK zero set: D2 D3");
    r.Run(1500);
    s = r.link->Snapshot();
    CHECK(s.status.Sensor(2) && std::fabs(s.status.Sensor(2)->x) < 0.5);
    q = r.link->Send("ver");
    CHECK(r.Wait(q) && q->ok && q->lines.size() == 2);
    q = r.link->Send("frobnicate");
    CHECK(r.Wait(q) && !q->ok && q->final == "ERR unknown command 'frobnicate' (try: help)");
    q = r.link->Send("diag");
    CHECK(r.Wait(q) && q->ok && q->lines.size() > 10 && q->lines[0] == "BWM427 diag");
}

static void TestFilesAndDownload()
{
    SECTION("files и скачивание: CRC, отмена, повтор");
    Rig r;
    CHECK(r.WaitConnected());
    auto q = r.link->Send("files", Origin::App, 15000);
    CHECK(r.Wait(q) && q->ok);
    std::vector<proto::FileEntry> files;
    for (const auto& l : q->lines)
    {
        proto::FileEntry f;
        if (proto::ParseFileLine(l, f))
            files.push_back(f);
    }
    CHECK(files.size() == 12 && q->final == "OK 12");
    CHECK(!files.empty() && files[0].measurement == 1 && files[0].sensorAddr == 2);

    const fs::path dir = fs::temp_directory_path() / L"krenomer_link_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    r.link->SetPollPaused(true);
    // Два файла подряд
    for (int k = 0; k < 2; k++)
    {
        const auto& f = files[static_cast<std::size_t>(10 + k)];
        auto sink = std::make_shared<DownloadSink>(dir / f.name, f.size);
        auto g = r.link->SendGet(f.name, sink);
        CHECK(r.Wait(g, 60000));
        CHECK_MSG(g->ok && sink->GetResult() == DownloadSink::Result::Ok, "%s / %s", g->final.c_str(), sink->Message().c_str());
        CHECK(ReadAll(dir / f.name) == r.sim->FileContent(f.name));
        CHECK(sink->Received() == f.size);
    }
    // Во время скачивания поток не выключен, а замедлен (графики живут)
    CHECK_RANGE(r.link->Snapshot().streamHz, 1.0, 3.5, "строк S в секунду при скачивании");
    // Отмена посреди передачи
    {
        const auto& f = files[0];
        auto sink = std::make_shared<DownloadSink>(dir / L"cancel.csv", f.size);
        auto g = r.link->SendGet(f.name, sink);
        r.Run(200);
        CHECK(!g->done.load() && sink->Received() > 0 && sink->Received() < f.size);
        r.link->AbortTransfer();
        CHECK(r.Wait(g, 3000));
        CHECK(g->aborted && sink->GetResult() == DownloadSink::Result::Aborted);
        CHECK(!fs::exists(dir / L"cancel.csv.part", ec) && !fs::exists(dir / L"cancel.csv", ec));
        // Связь после отмены в порядке: ответ ver — не «OK aborted»
        auto v = r.link->Send("ver");
        CHECK(r.Wait(v) && v->ok && v->lines.size() == 2);
    }
    // Нет такого файла
    {
        auto sink = std::make_shared<DownloadSink>(dir / L"none.csv", 0);
        auto g = r.link->SendGet("NONE.CSV", sink);
        CHECK(r.Wait(g) && !g->ok && g->final == "ERR no such file");
        CHECK(sink->GetResult() == DownloadSink::Result::DeviceError);
    }
    r.link->SetPollPaused(false);
    r.Run(2500);
    CHECK(r.link->Snapshot().streamHz > 5.0); // поток снова идёт
    fs::remove_all(dir, ec);
}

static void TestRecording()
{
    SECTION("запись замера: отказы files / get / boot / addr");
    Rig r;
    CHECK(r.WaitConnected());
    auto c = r.sim->GetControls();
    c.recSwitch = true;
    r.sim->SetControls(c);
    r.Run(2500);
    auto s = r.link->Snapshot();
    CHECK(s.status.Recording() && s.status.recMask == 3 && s.status.rows > 0);
    auto q = r.link->Send("files");
    CHECK(r.Wait(q) && q->final == "ERR busy recording");
    q = r.link->Send("boot");
    CHECK(r.Wait(q) && q->final.rfind("ERR recording to SD in progress", 0) == 0);
    q = r.link->Send("addr 1 3");
    CHECK(r.Wait(q) && q->final == "ERR busy recording");
    // Запись началась посреди передачи — передача прервана
    c.recSwitch = false;
    r.sim->SetControls(c);
    r.Run(1500);
    auto sink = std::make_shared<DownloadSink>(fs::temp_directory_path() / L"krenomer_rec.csv", 0);
    auto files = r.link->Send("files");
    CHECK(r.Wait(files) && files->ok && files->final == "OK 14"); // + 2 файла нового замера
    r.link->SetPollPaused(true);
    proto::FileEntry first;
    CHECK(!files->lines.empty() && proto::ParseFileLine(files->lines.front(), first));
    auto g = r.link->SendGet(first.name, sink);
    r.Run(100);
    c.recSwitch = true;
    r.sim->SetControls(c);
    CHECK(r.Wait(g));
    CHECK(sink->GetResult() == DownloadSink::Result::DeviceError || sink->GetResult() == DownloadSink::Result::Ok);
}

static void TestReboot()
{
    SECTION("перезагрузка прибора: связь пропала и вернулась");
    Rig r;
    CHECK(r.WaitConnected());
    const auto seq = r.link->Snapshot().connectSeq;
    auto q = r.link->Send("reset");
    CHECK(r.Wait(q) && q->ok && q->final == "RESET...");
    r.Run(500);
    CHECK(r.link->Snapshot().state == LinkState::Searching);
    // Команда без связи — сразу отказ, а не вечное ожидание
    auto z = r.link->Send("ver");
    CHECK(z->done.load() && z->linkLost);
    CHECK(r.WaitConnected(6000));
    auto s = r.link->Snapshot();
    CHECK(s.connectSeq == seq + 1 && s.reconnects == 1);
    r.Run(2000);
    CHECK(r.link->Snapshot().streamHz > 5.0);

    // boot: прибор в загрузчике, порта нет
    q = r.link->Send("boot");
    CHECK(r.Wait(q) && q->final == "DFU...");
    r.Run(1000);
    CHECK(r.sim->InDfu() && r.link->Snapshot().state == LinkState::Searching);
    r.sim->LeaveDfu(r.t, "1.4"); // «прошили» старую версию: samples нет
    CHECK(r.WaitConnected(5000));
    CHECK(r.link->Snapshot().ver.version == "1.4" && r.link->Snapshot().samplesSupport == -1);
}

static void TestAddress()
{
    SECTION("смена адреса: addr и итог в status.svc");
    Rig r;
    CHECK(r.WaitConnected());
    r.Run(1500);
    auto q = r.link->Send("addr 1 2"); // Д2 отвечает — занято
    CHECK(r.Wait(q) && !q->ok && q->final == "ERR addr 1->2 rejected: address 2 answers on the bus");
    auto c = r.sim->GetControls();
    c.sensorOn[1] = false; // Д3 сняли с шины, ставим новый датчик с адресом 1
    r.sim->SetControls(c);
    r.Run(1000);
    q = r.link->Send("addr 1 3");
    CHECK(r.Wait(q) && q->ok);
    r.link->FastPoll(r.t + 5000, 300);
    r.Run(400);
    CHECK(r.link->Snapshot().status.svc == "BUSY");
    r.Run(2500);
    CHECK(r.link->Snapshot().status.svc == "OK");
    r.Run(1500);
    const auto snap = r.link->Snapshot();
    const auto* d3 = snap.status.Sensor(3);
    CHECK_MSG(d3 && d3->Ok(), "Д3: %s", d3 ? d3->st.c_str() : "нет");
    q = r.link->Send("addr 0 3");
    CHECK(r.Wait(q) && q->final == "ERR addresses must be 1..247 and differ");
}

static void TestForeignAndHung()
{
    SECTION("чужой порт, зависший прибор, поток посреди ответа");
    // Чужое устройство: на ver отвечает не BWM427
    {
        auto fake = std::make_unique<FakeConnection>();
        auto* f = fake.get();
        f->reply = [](const std::string& cmd) -> std::vector<std::string> {
            if (cmd == "ver")
                return {"Hello from Arduino", "OK"};
            return {};
        };
        DeviceLink link(std::move(fake), false);
        for (std::int64_t t = 0; t < 2000; t += 10)
            link.Step(t);
        CHECK_MSG(f->rejects == 1 && !link.IsConnected(), "rejects %d, connected %d, written %zu", f->rejects,
                  link.IsConnected() ? 1 : 0, f->written.size());
        CHECK(f->lastForeign); // ответил не как BWM427 — точно чужой
        CHECK(f->written.size() == 1);
    }
    // Молчит совсем: три попытки ver, затем отказ
    {
        auto fake = std::make_unique<FakeConnection>();
        auto* f = fake.get();
        DeviceLink link(std::move(fake), false);
        for (std::int64_t t = 0; t < 6000; t += 10)
            link.Step(t);
        CHECK(f->rejects == 1 && !f->lastForeign); // молчит — может, ещё загружается
        int vers = 0;
        for (const auto& w : f->written)
            vers += w == "ver";
        CHECK(vers == 3);
    }
    // Подключился, потом перестал отвечать: после 3 команд без ответа — переоткрыть порт
    {
        auto fake = std::make_unique<FakeConnection>();
        auto* f = fake.get();
        bool alive = true;
        f->reply = [&](const std::string& cmd) -> std::vector<std::string> {
            if (!alive)
                return {};
            if (cmd == "ver")
                return {"BWM427 inclinometer firmware v1.3, build x", "OK"};
            if (cmd.rfind("stream", 0) == 0)
                return {"OK stream off"};
            if (cmd == "status")
                return {R"({"v":1,"sens":[]})", "OK"};
            return {"OK"};
        };
        DeviceLink link(std::move(fake), false);
        std::int64_t t = 0;
        for (; t < 1500; t += 10)
            link.Step(t);
        CHECK(link.IsConnected());
        alive = false;
        for (; t < 12000; t += 10)
            link.Step(t);
        CHECK(f->reopens >= 1);
        CHECK(link.Snapshot().timeouts >= 3);
    }
    // Подключились, а прибор ещё досылает передачу прежнему хосту (старая прошивка без сброса по DTR):
    // хвост D/E и лишний OK до ответа на ver не путают ответы, затем get abort
    {
        auto fake = std::make_unique<FakeConnection>();
        auto* f = fake.get();
        f->pending = {"D,QUJD", "D,REVG", "E,6,12345678", "OK"};
        int vers = 0;
        f->reply = [&vers](const std::string& cmd) -> std::vector<std::string> {
            if (cmd == "ver" && vers++ == 0) // хвост — только перед первым ответом
                return {"D,R0hJ", "BWM427 inclinometer firmware v1.3, build x", "HAL 1.8.1", "OK"};
            if (cmd == "ver")
                return {"BWM427 inclinometer firmware v1.3, build x", "HAL 1.8.1", "OK"};
            if (cmd == "get abort")
                return {"D,SktM", "OK aborted"};
            if (cmd.rfind("stream", 0) == 0)
                return {"OK stream every 100 ms"};
            if (cmd == "status")
                return {R"({"v":1,"freq":7,"sens":[]})", "OK"};
            return {"OK"};
        };
        // Хвост приходит сразу после открытия, до ver
        f->open = true;
        DeviceLink link(std::move(fake), false);
        for (std::int64_t t = 0; t < 3000; t += 10)
            link.Step(t);
        CHECK(link.IsConnected() && f->rejects == 0);
        const auto s = link.Snapshot();
        CHECK(s.haveVer && s.ver.version == "1.3");
        CHECK(s.haveStatus && s.status.freq == 7);
        bool aborted = false;
        for (const auto& w : f->written)
            aborted |= w == "get abort";
        CHECK(aborted);
        auto v = link.Send("ver");
        for (std::int64_t t = 3000; t < 4000 && !v->done.load(); t += 10)
            link.Step(t);
        CHECK(v->ok && v->lines.size() == 2);
    }
    // Строки потока и шапка посреди ответа на status
    {
        auto fake = std::make_unique<FakeConnection>();
        auto* f = fake.get();
        f->reply = [](const std::string& cmd) -> std::vector<std::string> {
            if (cmd == "ver")
                return {"BWM427 inclinometer firmware v1.3, build x", "HAL 1.8.1", "OK"};
            if (cmd.rfind("stream", 0) == 0)
                return {"# S,t_ms,loop_ms,s2,x2,y2,err2", "OK stream every 100 ms"};
            if (cmd == "status")
                return {"S,10,1,O,1.000,2.000,0", R"({"v":1,"freq":12,"sens":[{"addr":2,"st":"OK","x":1.5}]})",
                        "S,20,1,O,1.100,2.100,0", "OK"};
            return {};
        };
        DeviceLink link(std::move(fake), false);
        for (std::int64_t t = 0; t < 2500; t += 10)
            link.Step(t);
        const auto s = link.Snapshot();
        CHECK(s.haveStatus && s.status.freq == 12 && s.status.sens.size() == 1);
        CHECK(s.haveSample && s.sample.tMs == 20 && s.sample.sens.size() == 1);
        CHECK(s.badLines == 0);
        auto& h = link.GetHistory();
        std::lock_guard lock(h.mutex);
        CHECK(h.Size() >= 2);
    }
}

static int CountWritten(const std::vector<TermLine>& log, const std::string& cmd)
{
    int n = 0;
    for (const auto& l : log)
        n += l.kind == '>' && l.text == cmd;
    return n;
}

static void TestSamples()
{
    SECTION("samples: строки R в очередь, включение после подключения, старая прошивка");
    {
        Rig r;
        CHECK(r.WaitConnected());
        r.Run(1500);
        auto s = r.link->Snapshot();
        CHECK(s.samplesSupport == 1 && !s.samplesOn && !s.samplesWanted);
        CHECK(CountWritten(r.link->TermSince(0), "samples on") == 0); // без записи — не включается
        r.link->SetSamples(true);
        r.Run(300);
        s = r.link->Snapshot();
        CHECK(s.samplesOn && s.samplesWanted);
        std::vector<RxSample> got;
        r.link->TakeSamples(got); // начало — отбросить
        got.clear();
        r.Run(3000);
        const std::size_t taken = r.link->TakeSamples(got);
        CHECK(taken == got.size());
        CHECK_RANGE(static_cast<double>(got.size()), 55, 65, "строк R за 3 с (2 датчика × 10 Гц)");
        std::uint32_t lastN[2] = {}, lastT = 0;
        bool haveN[2] = {}, consecutive = true, timeUp = true, sane = true;
        for (const auto& x : got)
        {
            const int i = x.s.addr == 2 ? 0 : x.s.addr == 3 ? 1 : -1;
            if (i < 0)
            {
                sane = false;
                continue;
            }
            if (haveN[i] && x.s.n != lastN[i] + 1)
                consecutive = false;
            haveN[i] = true;
            lastN[i] = x.s.n;
            if (x.s.tMs < lastT)
                timeUp = false;
            lastT = x.s.tMs;
            sane = sane && x.s.batV > 100 && x.s.batV < 130 && std::abs(x.s.rawX) < 1000 && x.wallMs > 1700000000000LL;
        }
        CHECK(consecutive && timeUp && sane && haveN[0] && haveN[1]);
        // строки R при записи в журнал не пишутся (до 50 в секунду), ответ samples — пишется
        int rLines = 0;
        for (const auto& l : r.link->TermSince(0))
            rLines += l.text.rfind("R,", 0) == 0;
        CHECK(rLines == 0);
        CHECK(s.badLines == 0 && r.link->Snapshot().timeouts == 0);
        // Выключить: samples off, строки больше не копятся
        r.link->SetSamples(false);
        r.Run(300);
        CHECK(!r.link->Snapshot().samplesOn && CountWritten(r.link->TermSince(0), "samples off") == 1);
        got.clear();
        r.link->TakeSamples(got);
        r.Run(1000);
        got.clear();
        CHECK(r.link->TakeSamples(got) == 0);
        // Переподключение посреди записи: прибор выключил samples при открытии порта — программа включает снова
        r.link->SetSamples(true);
        r.Run(500);
        r.link->SetEnabled(false);
        r.Run(1000);
        CHECK(!r.link->Snapshot().samplesOn);
        r.link->SetEnabled(true);
        CHECK(r.WaitConnected());
        r.Run(800);
        CHECK(r.link->Snapshot().samplesOn && CountWritten(r.link->TermSince(0), "samples on") == 3);
        // Пользователь выключил samples в терминале — включаются снова (идёт запись)
        auto q = r.link->Send("samples off", Origin::User);
        CHECK(r.Wait(q) && q->ok && q->final == "OK samples off, dropped 0");
        r.Run(300);
        CHECK(r.link->Snapshot().samplesOn);
    }
    {
        // Прошивка 1.4: samples не отправляется вовсе, запись на ПК недоступна
        Rig r;
        r.sim->SetFwVersion("1.4");
        CHECK(r.WaitConnected());
        r.link->SetSamples(true);
        r.Run(2000);
        const auto s = r.link->Snapshot();
        CHECK(s.samplesSupport == -1 && !s.samplesOn && s.ver.version == "1.4");
        CHECK(CountWritten(r.link->TermSince(0), "samples on") == 0);
        auto q = r.link->Send("samples on", Origin::User); // вручную — как у прошивки 1.4
        CHECK(r.Wait(q) && !q->ok && q->final == "ERR unknown command 'samples' (try: help)");
    }
    {
        // Версия не разобрана: пробуем samples; «ERR unknown command» — больше не пытаемся
        auto fake = std::make_unique<FakeConnection>();
        auto* f = fake.get();
        f->reply = [](const std::string& cmd) -> std::vector<std::string> {
            if (cmd == "ver")
                return {"BWM427 inclinometer firmware vdev, build x", "OK"};
            if (cmd.rfind("stream", 0) == 0)
                return {"OK stream every 100 ms"};
            if (cmd == "status")
                return {R"({"v":1,"sens":[]})", "OK"};
            return {"ERR unknown command '" + cmd.substr(0, cmd.find(' ')) + "' (try: help)"};
        };
        DeviceLink link(std::move(fake), false);
        link.SetSamples(true);
        for (std::int64_t t = 0; t < 8000; t += 10)
            link.Step(t);
        int sent = 0;
        for (const auto& w : f->written)
            sent += w == "samples on";
        const auto s = link.Snapshot();
        CHECK_MSG(sent == 1 && s.samplesSupport == -1 && !s.samplesMsg.empty(), "samples on × %d, support %d", sent,
                  s.samplesSupport);
    }
    {
        // Строки R посреди ответа на команду и мусорная строка R
        auto fake = std::make_unique<FakeConnection>();
        auto* f = fake.get();
        f->reply = [](const std::string& cmd) -> std::vector<std::string> {
            if (cmd == "ver")
                return {"BWM427 inclinometer firmware v1.5, build x", "OK"};
            if (cmd.rfind("stream", 0) == 0)
                return {"OK stream every 100 ms"};
            if (cmd == "samples on")
                return {"R,2,7,1000,-46,-17,-458,0,118", "OK samples on, dropped 0"};
            if (cmd == "status")
                return {"R,2,8,1066,-45,-17,-458,0,118", R"({"v":1,"freq":9,"sens":[]})", "R,3,5,1070,1,2,3,4,118",
                        "R,3,x", "OK"};
            return {"OK"};
        };
        DeviceLink link(std::move(fake), false);
        link.SetSamples(true);
        for (std::int64_t t = 0; t < 2500; t += 10)
            link.Step(t);
        std::vector<RxSample> got;
        link.TakeSamples(got);
        const auto s = link.Snapshot();
        CHECK(s.haveStatus && s.status.freq == 9 && s.samplesOn);
        bool n7 = false, n8 = false, d3 = false;
        for (const auto& x : got)
        {
            n7 |= x.s.addr == 2 && x.s.n == 7 && x.s.rawX == -46 && x.s.offX == -458 && x.s.batV == 118;
            n8 |= x.s.addr == 2 && x.s.n == 8 && x.s.tMs == 1066;
            d3 |= x.s.addr == 3 && x.s.n == 5 && x.s.offY == 4;
        }
        CHECK_MSG(n7 && n8 && d3, "строк R %zu", got.size());
        CHECK(s.badLines >= 1); // «R,3,x»
    }
}

// Отсчёты из очереди связи — в запись (как App::TickRecord)
static void Pump(Rig& r, pcrec::Recorder& rec, std::int64_t ms)
{
    std::vector<RxSample> buf;
    for (std::int64_t e = r.t + ms; r.t < e; r.t += 10)
    {
        r.link->Step(r.t);
        if (r.t % 50 == 0)
        {
            buf.clear();
            r.link->TakeSamples(buf);
            for (const auto& x : buf)
                rec.Add(x.s, x.wallMs, x.linkMs);
            if (!r.link->IsConnected())
                rec.LinkLost(pcrec::WallNowMs());
            else if (r.link->Snapshot().samplesOn)
                rec.LinkBack(pcrec::WallNowMs());
            rec.Tick(r.t);
        }
    }
}

struct CsvCheck
{
    std::size_t rows = 0;
    bool header = false, parsed = true, calcOk = true, msUp = true, noNegZero = true, noCr = true, dateOk = true;
    std::int64_t firstMs = -1, lastMs = -1;
    std::size_t offsetChanges = 0;
};

static CsvCheck CheckCsv(const fs::path& p)
{
    CsvCheck c;
    const std::string all = ReadAll(p);
    c.noCr = all.find('\r') == std::string::npos;
    c.noNegZero = all.find("-0,000") == std::string::npos && all.find(";-0,00;") == std::string::npos &&
                  all.find(";-0,0;") == std::string::npos;
    std::size_t a = 0;
    std::int64_t prevOff = 0;
    bool first = true;
    while (a < all.size())
    {
        std::size_t b = all.find('\n', a);
        if (b == std::string::npos)
            b = all.size();
        const std::string line = all.substr(a, b - a);
        a = b + 1;
        if (first)
        {
            first = false;
            c.header = line == "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms";
            continue;
        }
        pcrec::CsvRow r;
        if (!pcrec::ParseRow(line, r))
        {
            c.parsed = false;
            continue;
        }
        c.rows++;
        c.calcOk = c.calcOk && r.calcX == r.rawX * 10 - r.offX && r.calcY == r.rawY * 10 - r.offY;
        c.dateOk = c.dateOk && r.date.size() == 10 && r.date[2] == '.' && r.date[5] == '.' && r.time.size() == 8 &&
                   r.time[2] == ':' && r.time[5] == ':';
        if (c.firstMs < 0)
            c.firstMs = r.ms;
        else if (r.ms < c.lastMs)
            c.msUp = false;
        c.lastMs = r.ms;
        if (c.rows > 1 && r.offX != prevOff)
            c.offsetChanges++;
        prevOff = r.offX;
    }
    return c;
}

static void TestPcRecording()
{
    SECTION("запись на ПК: CSV по датчикам, ноль посреди записи, обрыв связи, перезагрузка, .xlsx");
    const fs::path dir = fs::temp_directory_path() / L"krenomer_pcrec_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    Rig r(3);
    CHECK(r.WaitConnected());
    r.Run(1500);

    pcrec::Recorder rec;
    pcrec::Recorder::Options o;
    o.dir = dir;
    o.label = "опыт 1";
    o.xlsx = true;
    o.device = "имитатор";
    const std::int64_t wall0 = pcrec::WallNowMs();
    std::string err;
    CHECK(rec.Start(o, wall0, r.t, &err));
    CHECK(rec.Base() == pcrec::BaseName(wall0, "опыт 1") && rec.Base().find("_опыт 1") != std::string::npos);
    r.link->SetSamples(true);
    Pump(r, rec, 6000);
    // Ноль посреди записи: OffsetX/Y и CalcX/Y меняются, CalcX = RawX − OffsetX по-прежнему точно
    auto q = r.link->Send("zero");
    CHECK(r.Wait(q) && q->ok);
    Pump(r, rec, 6000);
    CHECK(rec.Sensors().size() == 2 && rec.TotalLost() == 0);
    const std::uint64_t before = rec.TotalRows();
    CHECK_RANGE(static_cast<double>(before), 220, 260, "строк за 12 с (2 датчика × 10 Гц)");
    CHECK_RANGE(rec.Sensors()[0].hz, 8.0, 12.0, "частота строк Д2, Гц");

    // Файл уже на диске (сброс раз в секунду), до остановки
    const fs::path d2 = dir / text::PathFromUtf8(pcrec::CsvName(rec.Base(), 2));
    const fs::path d3 = dir / text::PathFromUtf8(pcrec::CsvName(rec.Base(), 3));
    CHECK(fs::exists(d2, ec) && fs::exists(d3, ec));
    CHECK_RANGE(static_cast<double>(CheckCsv(d2).rows), static_cast<double>(rec.Sensors()[0].rows) - 12,
                static_cast<double>(rec.Sensors()[0].rows), "строк Д2 в файле до остановки (теряется не больше ~1 с)");

    // Прибор «не успевает»: каждая 7-я строка R выброшена — пропуски по n
    r.sim->SetSamplesDropEvery(7);
    Pump(r, rec, 3000);
    r.sim->SetSamplesDropEvery(0);
    const std::uint64_t lostDrop = rec.TotalLost();
    CHECK_RANGE(static_cast<double>(lostDrop), 6, 11, "потеряно при выбросе каждой 7-й строки за 3 с");

    // Обрыв связи на ~3 с: файлы открыты, после переподключения samples on снова, пропуски — по n
    r.link->SetEnabled(false);
    Pump(r, rec, 3000);
    CHECK(!r.link->IsConnected() && rec.Gaps().size() == 1 && rec.Gaps()[0].to == 0);
    r.link->SetEnabled(true);
    Pump(r, rec, 3000);
    CHECK(r.link->IsConnected() && r.link->Snapshot().samplesOn);
    CHECK(rec.Gaps().size() == 1 && rec.Gaps()[0].to != 0);
    const std::uint64_t lostGap = rec.TotalLost() - lostDrop;
    CHECK_RANGE(static_cast<double>(lostGap), 50, 80, "потеряно за ~3,3 с без связи (2 × 10 Гц)");

    // Перезагрузка прибора: n и t_ms с нуля — Ms не идёт назад, пропуски не считаются
    const std::uint64_t lostBefore = rec.TotalLost();
    q = r.link->Send("reset");
    CHECK(r.Wait(q) && q->final == "RESET...");
    Pump(r, rec, 7000);
    CHECK(r.link->IsConnected() && rec.Reboots() == 1 && rec.TotalLost() == lostBefore);
    CHECK(rec.Gaps().size() == 2);
    const std::uint64_t rowsAll = rec.TotalRows();

    rec.Stop(pcrec::WallNowMs(), r.t);
    r.link->SetSamples(false);
    CHECK_RANGE(static_cast<double>(rec.ElapsedMs(r.t)), 27900, 28500, "длительность записи (время связи), мс");
    CHECK(!rec.Running());
    rec.WaitConverted();
    CHECK_MSG(rec.XlsxOk(), "xlsx: %s", rec.XlsxError().c_str());
    CHECK(rec.Error().empty());

    // Файлы: по датчику, формат карты
    std::uint64_t fileRows = 0;
    for (const auto& p : {d2, d3})
    {
        const CsvCheck c = CheckCsv(p);
        CHECK_MSG(c.header && c.parsed && c.calcOk && c.msUp && c.noNegZero && c.noCr && c.dateOk,
                  "%s: header %d parsed %d calc %d msUp %d -0 %d cr %d date %d", text::PathToUtf8(p.filename()).c_str(),
                  c.header, c.parsed, c.calcOk, c.msUp, c.noNegZero, c.noCr, c.dateOk);
        CHECK(c.firstMs >= 0 && c.firstMs < 100);
        CHECK(c.offsetChanges == 2); // ноль задан, перезагрузка его сбросила
        // Ms последней строки: 21 с до перезагрузки (по часам прибора, и без связи) + ~4 с после
        CHECK_RANGE(static_cast<double>(c.lastMs), 23000, 28000, "Ms последней строки");
        fileRows += c.rows;
    }
    CHECK(fileRows == rowsAll);
    const std::string xlsx = ReadAll(rec.XlsxPath());
    CHECK(xlsx.size() > 1000 && xlsx.rfind("PK", 0) == 0 && xlsx.find("xl/worksheets/sheet3.xml") != std::string::npos);
    CHECK(rec.XlsxPath().filename() == text::PathFromUtf8(pcrec::XlsxName(rec.Base())));

    // Новая запись в ту же секунду — не перезаписывает: « (2)»
    pcrec::Recorder rec2;
    o.xlsx = false;
    CHECK(rec2.Start(o, wall0, r.t, &err));
    CHECK(rec2.Base() == pcrec::BaseName(wall0, "опыт 1") + " (2)");
    proto::RawSample one;
    one.addr = 2;
    rec2.Add(one, wall0, r.t);
    rec2.Stop(wall0 + 1000, r.t + 1000);
    CHECK(rec2.TotalRows() == 1 && fs::exists(dir / text::PathFromUtf8(pcrec::CsvName(rec2.Base(), 2)), ec));
    CHECK(CheckCsv(d2).rows + CheckCsv(d3).rows == rowsAll); // прежние файлы целы
    // Файлы оставлены для проверки openpyxl (scratch-скрипт): %TEMP%\krenomer_pcrec_test
}

static void TestThreaded()
{
    SECTION("поток связи в реальном времени (как в программе)");
    auto sim = std::make_shared<SimDevice>(5);
    DeviceLink link(std::make_unique<SimConnection>(sim), true);
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    while (!link.IsConnected() && elapsed() < 5.0)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(link.IsConnected());
    CHECK_RANGE(elapsed(), 0.1, 2.0, "подключение, с");
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    auto s = link.Snapshot();
    CHECK(s.haveSample && s.haveStatus);
    CHECK_RANGE(s.streamHz, 7.0, 13.0, "строк S в секунду");
    // Команды из «потока интерфейса», ответ — асинхронно
    std::vector<RequestPtr> reqs;
    for (int i = 0; i < 20; i++)
        reqs.push_back(link.Send(i % 2 ? "ver" : "set freq 10"));
    for (int i = 0; i < 300 && !reqs.back()->done.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    int ok = 0;
    for (const auto& r : reqs)
        ok += r->done.load() && r->ok;
    CHECK(ok == 20);
    CHECK(link.Snapshot().timeouts == 0);
    // Отсчёты samples в реальном времени
    link.SetSamples(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    std::vector<RxSample> got;
    link.TakeSamples(got);
    CHECK_RANGE(static_cast<double>(got.size()), 12, 30, "строк R за ~1 с в реальном времени");
}

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    TestThreaded();
    TestConnectAndStream();
    TestCommands();
    TestFilesAndDownload();
    TestRecording();
    TestReboot();
    TestAddress();
    TestForeignAndHung();
    TestSamples();
    TestPcRecording();
    return TestSummary();
}
