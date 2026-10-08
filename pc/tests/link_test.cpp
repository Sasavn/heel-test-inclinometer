// Связь с прибором (DeviceLink) без железа: модельное время, прибор-имитатор (тот же протокол, что прошивка) и
// канал-сценарий. Подключение и проверка ver, поток stream -> история, опрос status, команды set / set time / zero /
// addr, files и скачивание с проверкой CRC, отмена передачи, запись (отказы), перезагрузка прибора и
// переподключение, чужой порт, зависший прибор, строки потока посреди ответа.
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
    CHECK(s.state == LinkState::Connected && s.haveVer && s.ver.version == "1.4");
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
    r.sim->LeaveDfu(r.t, "1.4");
    CHECK(r.WaitConnected(5000));
    CHECK(r.link->Snapshot().ver.version == "1.4");
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
    return TestSummary();
}
