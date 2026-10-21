#include "TestUtil.hpp"
#include "comm/TcpConnection.hpp"
#include "fw/EspOta.hpp"
#include "fw/Firmware.hpp"
#include <chrono>
#include <cstring>
#include <deque>
#include <sstream>
#include <thread>
using Bytes = std::vector<unsigned char>;
static void U32(Bytes &b, std::size_t o, std::uint32_t v)
{
    for (int i = 0; i < 4; i++)
        b[o + i] = static_cast<unsigned char>(v >> (8 * i));
}
static Bytes Image(std::string ver = "1.5.1-esp32", std::string project = "krenomer-esp32", int chip = 18)
{
    Bytes b(24 + 8 + 512, 0);
    b[0] = 0xe9;
    b[1] = 1;
    b[12] = static_cast<unsigned char>(chip);
    b[23] = 1;
    U32(b, 28, 512);
    U32(b, 32, 0xabcd5432);
    std::copy(ver.begin(), ver.end(), b.begin() + 48);
    std::copy(project.begin(), project.end(), b.begin() + 80);
    unsigned char c = 0xef;
    for (std::size_t i = 32; i < b.size(); ++i)
        c ^= b[i];
    b.resize((b.size() + 16) & ~std::size_t(15), 0);
    b.back() = c;
    auto h = fw::Sha256(b.data(), b.size());
    b.insert(b.end(), h.begin(), h.end());
    return b;
}
// Independent transport peer: no COM, no sockets, deterministic power loss and
// boot slots.
class Peer : public IConnection
{
  public:
    bool open = true, enabled = true, dropAck = false, disconnected = false, powercut = false, rollback = false,
         badOffset = false, wrongInfo = false, confirmed = true, closeWithCommit=false, closeAfterLines=false;
    unsigned opens = 1, bootSlot = 0;
    std::size_t offset = 0;
    bool active = false;
    unsigned begins = 0, duplicates = 0;
    Bytes bytes;
    std::string version = "1.5.0-esp32", nextVersion, expectedSha;
    std::deque<std::string> lines;
    void Tick(std::int64_t) override
    {
        while (!lines.empty())
        {
            auto l = lines.front();
            lines.pop_front();
            EmitLine(l);
        }
        if(closeAfterLines){closeAfterLines=false;open=false;}
    }
    bool IsOpen() override
    {
        return open && enabled;
    }
    bool Write(const std::string &d) override
    {
        std::string cmd = d;
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
            cmd.pop_back();
        std::istringstream in(cmd);
        std::string a, b;
        in >> a >> b;
        auto reply = [&](std::string s) { lines.push_back(std::move(s)); };
        if (a == "ver")
        {
            reply("BWM427 v" + version + " board=tab5 chip=esp32p4 proto=2");
            reply("OK");
        }
        else if (cmd == "ota info")
        {
            reply(std::string("{\"board\":\"") + (wrongInfo ? "other" : "tab5") +
                  "\",\"chip\":\"esp32p4\",\"proto\":2,\"chip_revision\":100,"
                  "\"version\":\"" +
                  version + "\",\"max_size\":6291456,\"recording\":false,\"boot_confirmed\":" +
                  (confirmed ? "true" : "false") + "}");
            reply("OK");
        }
        else if (b == "begin")
        {
            std::string board, chip;
            int proto;
            std::size_t size;
            in >> board >> chip >> proto >> nextVersion >> size >> expectedSha;
            if (board != "tab5" || chip != "esp32p4" || proto != 2)
            {
                reply("ERR OTA TARGET");
                return true;
            }
            ++begins;
            active = true;
            offset = 0;
            bytes.clear();
            reply("OK ota offset=0");
        }
        else if (b == "chunk")
        {
            std::size_t pos;
            std::string hex;
            in >> pos >> hex;
            Bytes part;
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
                part.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            if (pos == offset)
            {
                bytes.insert(bytes.end(), part.begin(), part.end());
                offset += part.size();
            }
            else if (pos + part.size() == offset && std::equal(part.begin(), part.end(), bytes.begin() + pos))
                ++duplicates;
            else
            {
                reply("ERR OTA OFFSET");
                return true;
            }
            if (powercut)
            {
                open = false;
                active = false;
                disconnected = true;
                return true;
            }
            if (dropAck)
            {
                dropAck = false;
                return true;
            }
            reply("OK ota offset=" + std::to_string(offset + (badOffset ? 1 : 0)));
        }
        else if (b == "end")
        {
            auto h = fw::Sha256(bytes.data(), bytes.size());
            if (fw::Hex(h.data(), 32) != expectedSha)
            {
                reply("ERR OTA HASH");
                return true;
            }
            active = false;
            bootSlot = rollback ? 0 : 1;
            reply("OK ota committed scheduled reboot");
            if(closeWithCommit)closeAfterLines=true;
        }
        else if (b == "abort")
        {
            active = false;
            reply("OK ota aborted");
        }
        else if (a == "status")
        {
            reply("{\"v\":1,\"sens\":[],\"bat_present\":false}");
            reply("OK");
        }
        else
            reply("OK");
        return true;
    }
    void SetEnabled(bool b) override
    {
        enabled = b;
    }
    bool Enabled() const override
    {
        return enabled;
    }
    void Reject(std::int64_t, bool) override
    {
        open = false;
    }
    void Reopen(std::int64_t) override
    {
        open = false;
    }
    std::string PortName() override
    {
        return "VIRTUAL TAB5";
    }
    std::uint64_t OpenCount() override
    {
        return opens;
    }
    std::vector<PortInfo> Ports() override
    {
        return {};
    }
    void SetPortChoice(const std::string &) override
    {
    }
    bool IsDemo() const override
    {
        return false;
    }
    std::string Problem() override
    {
        return {};
    }
    void Boot()
    {
        lines.clear();
        open = false;
        active = false;
    }
    void Return()
    {
        open = true;
        ++opens;
        if (!rollback)
            version = nextVersion;
    }
};
struct Rig
{
    Peer *peer;
    DeviceLink link;
    fw::EspOta ota;
    std::int64_t now = 0;
    std::filesystem::path path;
    Rig() : peer(new Peer), link(std::unique_ptr<IConnection>(peer), false)
    {
        path = std::filesystem::current_path() / "esp32-test-application.bin";
        auto b = Image();
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<char *>(b.data()), b.size());
        f.close();
        Run(1000);
    }
    ~Rig()
    {
        ota.Cancel();
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    void Run(int ms)
    {
        for (int i = 0; i < ms; i += 10)
        {
            now += 10;
            link.Step(now);
            ota.Tick(now);
        }
    }
    bool Start()
    {
        return ota.Start(&link, text::PathToUtf8(path), now);
    }
    void Finish()
    {
        for (int i = 0; i < 2000 && ota.GetState() != fw::EspOta::State::WaitReboot && ota.Busy(); i += 10)
            Run(10);
    }
    void Reboot()
    {
        peer->Boot();
        Run(100);
        peer->Return();
        Run(2000);
    }
};

static void TcpLoopbackTest(bool authorized)
{
    WSADATA ws{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &ws) == 0);
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(server != INVALID_SOCKET);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    CHECK(bind(server, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0);
    CHECK(listen(server, 1) == 0);
    int len = sizeof(a);
    CHECK(getsockname(server, reinterpret_cast<sockaddr *>(&a), &len) == 0);
    std::string first;
    bool exchange = false;
    std::thread peer([&] {
        SOCKET c = accept(server, nullptr, nullptr);
        if (c == INVALID_SOCKET)
            return;
        DWORD timeout = 3000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        char b[256];
        while (first.find('\n') == std::string::npos)
        {
            int n = recv(c, b, sizeof(b), 0);
            if (n <= 0)
                break;
            first.append(b, n);
        }
        if (authorized)
        {
            send(c, "O", 1, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            send(c, "K\r\n", 3, 0);
            std::string cmd;
            while (cmd.find('\n') == std::string::npos)
            {
                int n = recv(c, b, sizeof(b), 0);
                if (n <= 0)
                    break;
                cmd.append(b, n);
            }
            exchange = cmd == "ver\n";
            static constexpr char reply[]="BWM427 v1.5.0-esp32 board=tab5 chip=esp32p4 proto=2\nOK\n";
            send(c,reply,static_cast<int>(sizeof(reply)-1),0);
        }
        else
            send(c, "ERR AUTH\n", 9, 0);
        shutdown(c, SD_BOTH);
        closesocket(c);
    });
    TcpConnection conn("127.0.0.1", ntohs(a.sin_port), "local-test-token");
    bool sawOpen = false, gotLine = false;
    conn.SetLineHandler([&](std::string line) {
        if (line.rfind("BWM427", 0) == 0)
            gotLine = true;
    });
    for (int i = 0; i < 1000; ++i)
    {
        conn.Tick(i * 2);
        if (conn.IsOpen() && !sawOpen)
        {
            sawOpen = true;
            CHECK(conn.Write("ver\n"));
        }
        if ((authorized && gotLine) || (!authorized && conn.Problem() == "TCP authentication failed"))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    peer.join();
    CHECK(first == "auth local-test-token\n");
    CHECK(sawOpen == authorized);
    if (authorized)
    {
        CHECK(exchange);
        CHECK(gotLine);
    }
    else
        CHECK(conn.Problem() == "TCP authentication failed");
    closesocket(server);
    WSACleanup();
}

class ProductionPeer : public Peer
{
  public:
    using Reply = void (*)(void *, const char *);
    using Init = int (*)();
    using Line = void (*)(const char *, Reply, void *);
    using TickFn = unsigned (*)();
    using Boot = void (*)();
    HMODULE library;
    Line line;
    TickFn tick;
    Boot boot;
    bool rebootStarted = false;
    std::int64_t backAt = 0;
    explicit ProductionPeer(const char *path)
    {
        library = LoadLibraryA(path);
        if (!library)
            throw std::runtime_error("Cannot load production service bridge");
        auto init = reinterpret_cast<Init>(GetProcAddress(library, "bridge_init"));
        line = reinterpret_cast<Line>(GetProcAddress(library, "bridge_line"));
        tick = reinterpret_cast<TickFn>(GetProcAddress(library, "bridge_tick"));
        boot = reinterpret_cast<Boot>(GetProcAddress(library, "bridge_boot"));
        if (!init || !line || !tick || !boot || init() != 0)
            throw std::runtime_error("Cannot initialize production bridge");
    }
    ~ProductionPeer() override
    {
        FreeLibrary(library);
    }
    bool Write(const std::string &data) override
    {
        if (data.rfind("ota ", 0) != 0)
            return Peer::Write(data);
        std::string command = data;
        while (!command.empty() && (command.back() == '\r' || command.back() == '\n'))
            command.pop_back();
        line(
            command.c_str(),
            [](void *ctx, const char *response) { static_cast<ProductionPeer *>(ctx)->lines.push_back(response); },
            this);
        return true;
    }
    void Tick(std::int64_t now) override
    {
        if (tick() && !rebootStarted)
        {
            rebootStarted = true;
            backAt = now + 100;
            Boot();
        }
        if (backAt && now >= backAt)
        {
            backAt = 0;
            boot();
            open = true;
            ++opens;
        }
        Peer::Tick(now);
    }
};
static void ProductionIntegration(const char *dll)
{
    SECTION("C++ PC OTA against actual production C service parser (mock flash)");
    auto peer = std::make_unique<ProductionPeer>(dll);
    auto *raw = peer.get();
    DeviceLink link(std::move(peer), false);
    fw::EspOta ota;
    std::int64_t now = 0;
    auto run = [&](int ms) {
        for (int i = 0; i < ms; i += 10)
        {
            now += 10;
            link.Step(now);
            ota.Tick(now);
        }
    };
    run(1000);
    CHECK(link.IsConnected());
    auto path = std::filesystem::current_path() / "service-integration-app.bin";
    auto image = Image("1.5.0-esp32");
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<char *>(image.data()), image.size());
    }
    CHECK(ota.Start(&link, text::PathToUtf8(path), now));
    run(6000);
    CHECK_MSG(ota.GetState() == fw::EspOta::State::Done, "%s", ota.Message().c_str());
    CHECK(raw->rebootStarted);
    CHECK(link.Snapshot().connectSeq == 2);
    ota.Cancel();
    std::filesystem::remove(path);
}
int main(int argc, char **argv)
{
    testutil::Init(argc, argv);
    SECTION("Winsock loopback auth gating, split replies and disconnect");
    TcpLoopbackTest(true);
    TcpLoopbackTest(false);
    SECTION("ESP32-P4 image architecture, board, corruption and full flash");
    auto good = Image();
    CHECK(fw::CheckEspImage(good).ok);
    CHECK(!fw::CheckEspImage(Image("1.5.1-esp32", "krenomer-esp32", 13)).ok);
    CHECK(!fw::CheckEspImage(Image("1.5.1-esp32", "foreign-board")).ok);
    auto bad = good;
    bad[350] ^= 1;
    CHECK(!fw::CheckEspImage(bad).ok);
    bad = good;
    bad.back() ^= 1;
    CHECK(!fw::CheckEspImage(bad).ok);
    bad = good;
    bad.resize(bad.size() + 4096);
    CHECK(!fw::CheckEspImage(bad).ok);
    CHECK(!fw::CheckEspImage(Bytes(4096, 0)).ok);
    SECTION("Version and platform identification, no battery");
    auto v = proto::ParseVer({"BWM427 v1.5.0-esp32 board=tab5 chip=esp32p4 proto=2"});
    CHECK(v.IsTab5());
    CHECK(proto::VersionAtLeast(v.version, 1, 5).value_or(false));
    CHECK(!proto::ParseVer({"BWM427 v1.5 board=tab5 chip=esp32c6 proto=2"}).IsTab5());
    proto::DeviceStatus status;
    CHECK(proto::ParseStatus("{\"v\":1,\"sens\":[],\"bat_present\":false}", status));
    CHECK(!status.batPresent);
    CHECK(proto::ParseStatus(R"({"v":1,"sens":[],"cpu":null,"rate":null,"bat_present":false,"bat_low":true,"bat_pct":0,"sd":"ERROR","rec_mask":3})",status));
    CHECK(status.cpu==-1 && std::isnan(status.rate));
    CHECK(!status.batLow && !status.batPct && !status.haveLoopCounters && !status.haveFallbacks);
    CHECK(status.Recording());
    SECTION("Virtual full transfer and version after reboot");
    {
        Rig r;
        CHECK(r.link.IsConnected());
        CHECK(r.Start());
        r.Finish();
        CHECK(r.ota.GetState() == fw::EspOta::State::WaitReboot);
        CHECK(r.peer->bytes == good);
        CHECK(r.peer->bootSlot == 1);
        r.Reboot();
        CHECK(r.ota.GetState() == fw::EspOta::State::Done);
    }
    SECTION("Commit ACK and disconnect in same frame still verifies after reboot");
    {Rig r;r.peer->closeWithCommit=true;CHECK(r.Start());r.Finish();CHECK(r.ota.GetState()==fw::EspOta::State::WaitReboot);r.Reboot();CHECK(r.ota.GetState()==fw::EspOta::State::Done);}
    SECTION("Wrong device board info blocks begin");
    {
        Rig r;
        r.peer->wrongInfo = true;
        CHECK(r.Start());
        r.Run(1000);
        CHECK(r.ota.GetState() == fw::EspOta::State::Failed);
        CHECK(r.peer->begins == 0);
    }
    SECTION("Lost acknowledgement retries identical chunk");
    {
        Rig r;
        r.peer->dropAck = true;
        CHECK(r.Start());
        r.Run(6000);
        CHECK(r.peer->duplicates == 1);
        CHECK(r.ota.GetState() == fw::EspOta::State::WaitReboot);
    }
    SECTION("Wrong offset fails safely");
    {
        Rig r;
        r.peer->badOffset = true;
        CHECK(r.Start());
        r.Run(1000);
        CHECK(r.ota.GetState() == fw::EspOta::State::Failed);
        CHECK(r.peer->bootSlot == 0);
    }
    SECTION("Power loss retains old slot; reconnect and fresh retry");
    {
        Rig r;
        r.peer->powercut = true;
        CHECK(r.Start());
        r.Run(1000);
        CHECK(r.ota.GetState() == fw::EspOta::State::Failed);
        CHECK(r.peer->bootSlot == 0);
        CHECK(!r.peer->active);
        r.peer->powercut = false;
        r.peer->open = true;
        ++r.peer->opens;
        r.Run(1000);
        CHECK(r.Start());
        r.Finish();
        CHECK(r.peer->begins == 2);
        r.Reboot();
        CHECK(r.ota.GetState() == fw::EspOta::State::Done);
    }
    SECTION("Rollback version mismatch is not reported as success");
    {
        Rig r;
        r.peer->rollback = true;
        CHECK(r.Start());
        r.Finish();
        r.Reboot();
        CHECK(r.ota.GetState() == fw::EspOta::State::Failed);
        CHECK(r.peer->bootSlot == 0);
    }
    SECTION("Pending trial waits for boot self-test; timeout fails");
    {
        Rig r;
        CHECK(r.Start());
        r.Finish();
        r.peer->confirmed = false;
        r.Reboot();
        CHECK(r.ota.GetState() == fw::EspOta::State::Verify);
        r.peer->confirmed = true;
        r.Run(2000);
        CHECK(r.ota.GetState() == fw::EspOta::State::Done);
    }
    SECTION("Unconfirmed trial never reports success");
    {
        Rig r;
        CHECK(r.Start());
        r.Finish();
        r.peer->confirmed = false;
        r.Reboot();
        r.Run(121000);
        CHECK(r.ota.GetState() == fw::EspOta::State::Failed);
    }
    SECTION("Cancel aborts and same-version recovery allowed");
    {
        Rig r;
        CHECK(r.Start());
        r.Run(50);
        r.ota.Cancel();
        r.Run(100);
        CHECK(!r.peer->active);
        CHECK(r.peer->bootSlot == 0);
        r.peer->version = "1.5.1-esp32";
        r.link.Send("ver");
        r.Run(300);
        CHECK(r.Start());
        r.Finish();
        CHECK(r.ota.GetState() == fw::EspOta::State::WaitReboot);
    }
    if (argc > 1 && std::string(argv[1]) == "--image" && argc > 2)
    {
        std::ifstream f(text::PathFromUtf8(argv[2]), std::ios::binary);
        Bytes data((std::istreambuf_iterator<char>(f)), {});
        auto image = fw::CheckEspImage(data);
        CHECK_MSG(image.ok, "%s", image.error.c_str());
        std::printf("Actual app: %s, %zu bytes\n", image.version.c_str(), data.size());
    }
    if (argc > 2 && std::string(argv[1]) == "--service-dll")
        ProductionIntegration(argv[2]);
    return TestSummary();
}
