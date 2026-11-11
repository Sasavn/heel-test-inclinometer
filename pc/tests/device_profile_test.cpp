// Headless identities, policy, connection epochs and extended simulator contract.
#include "TestUtil.hpp"
#include "core/DeviceProfile.hpp"
#include "comm/DeviceLink.hpp"
#include "comm/SimConnection.hpp"
#include <algorithm>
#include <deque>

namespace
{
struct Script final : IConnection
{
    bool open = true, statusReplies = true, modeReplies = true;
    std::uint64_t epoch = 1;
    std::string identity = "BWM427 fw=1.8.5-esp32 board=tab5 chip=esp32p4 proto=2";
    std::string status = "{\"v\":1,\"sens\":[],\"fw\":\"1.8.5-esp32\",\"board\":\"tab5\",\"platform\":\"esp32p4\",\"proto\":2,\"dual_mode_cap\":true,\"instrument_control_cap\":true,\"relative_data_cap\":true,\"instrument\":\"heel\",\"xfer\":\"idle\"}";
    std::vector<std::string> writes;
    std::deque<std::string> pending;
    void Tick(std::int64_t) override { while (!pending.empty()) { auto s = pending.front(); pending.pop_front(); EmitLine(s); } }
    bool IsOpen() override { return open; }
    bool Write(const std::string& line) override
    {
        auto cmd = line.substr(0, line.find('\r')); writes.push_back(cmd);
        if (cmd == "ver") { pending.push_back(identity); pending.push_back("OK"); }
        else if (cmd == "status") { if (statusReplies) { pending.push_back(status); pending.push_back("OK"); } }
        else if (cmd.rfind("instrument ", 0) == 0) { if (modeReplies) pending.push_back("OK instrument"); }
        else pending.push_back("OK");
        return true;
    }
    void SetEnabled(bool value) override { open = value; }
    bool Enabled() const override { return true; }
    void Reject(std::int64_t, bool) override { open = false; }
    void Reopen(std::int64_t) override { ++epoch; }
    std::string PortName() override { return "FAKE-USB"; }
    std::uint64_t OpenCount() override { return epoch; }
    std::vector<PortInfo> Ports() override { return {}; }
    void SetPortChoice(const std::string&) override {}
    bool IsDemo() const override { return false; }
    std::string Problem() override { return {}; }
};

void Profiles()
{
    SECTION("positive identities and capability flags, never transport/version guesses");
    proto::DeviceStatus s;
    s.protocol = 2;
    auto tab = proto::ParseVer({"BWM427 fw=1.8.5-esp32 board=tab5 chip=esp32p4 proto=2"});
    CHECK(tab.version == "1.8.5-esp32");
    CHECK(device::Detect(tab, s, true).IsTab5());
    auto stm = proto::ParseVer({"BWM427 fw=1.6.0 board=stm32f411 chip=stm32f411 proto=2"});
    CHECK(device::Detect(stm, s, true).platform == device::Platform::Stm32);
    auto legacy = proto::ParseVer({"BWM427 inclinometer firmware v1.5, build Oct 7 2026"});
    const proto::DeviceStatus legacyStatus;
    CHECK(device::Detect(legacy, legacyStatus, true).platform == device::Platform::Stm32Legacy15);
    s.protocol = 3; CHECK(!device::Detect(tab, s, true).Known());
    s.protocol = 1; CHECK(!device::Detect(tab, s, true).Known());
    s.protocol = 2; s.fw = "1.8.4-esp32"; CHECK(!device::Detect(tab, s, true).Known());
    s.fw.clear();
    CHECK(!device::Detect(tab, s, false).Known());
    CHECK(!device::Detect(proto::ParseVer({"BWM427 fw=9 board=other chip=other proto=2"}), s, true).Known());
    CHECK(!device::Detect(proto::ParseVer({"BWM427 fw=1.8.5 board=tab5 chip=esp32p4 proto=99"}), s, true).Known());
    s.board = "stm32f411";
    CHECK(!device::Detect(tab, s, true).Known());
    s = {}; s.protocol = 2;
    const auto p = device::Detect(tab, s, true);
    CHECK(!p.AddressChange() && !p.Setting("batalarm"));
    CHECK(!p.ValidGap(14) && p.ValidGap(15) && p.ValidGap(100) && !p.ValidGap(101));
    CHECK(!p.ValidGap(15.5) && !p.ValidGap(proto::kNaN));
    CHECK(device::Detect(stm, s, true).AddressChange());
    CHECK(device::Detect(legacy, legacyStatus, true).ValidGap(2));
    s.dualModeCap = true; // Tab5 1.8.4: dual-mode alone did not expose the new CLI.
    CHECK(!device::InstrumentControl(p, s));
    s.instrumentControlCap = true;
    CHECK(device::InstrumentControl(p, s));
    CHECK(!device::InstrumentControl({}, s));

    SECTION("fresh status belongs to the current connection and expires");
    CHECK(device::FreshStatus(true, true, 110, 100, 3110));
    CHECK(!device::FreshStatus(true, true, 110, 100, 3111));
    CHECK(!device::FreshStatus(true, true, 99, 100, 110));
    CHECK(!device::FreshStatus(true, true, 120, 100, 110));
    CHECK(!device::FreshStatus(false, true, 110, 100, 110));
    CHECK(!device::FreshStatus(true, false, 110, 100, 110));
    CHECK(device::UsbCandidate("0483:5740"));
    CHECK(device::UsbCandidate("303A:1001"));
    CHECK(device::UsbCandidate("USB\\VID_303A&PID_1001&MI_00"));
    CHECK(!device::UsbCandidate("303a:9999"));
    CHECK(!device::UsbCandidate("0483:df11"));
    device::Activity a;
    CHECK(!a.Blocked());
    a.levelRecording = true; CHECK(a.Blocked());
    a = {}; a.pcRecording = true; CHECK(a.Blocked());
    a = {}; a.deviceRecording = true; CHECK(a.Blocked() && !a.Blocked(true));
    a = {}; a.firmware = true; CHECK(a.Blocked(true));
    a = {}; a.transfer = true; CHECK(a.Blocked());
    a = {}; a.settings = true; CHECK(a.Blocked());
}

void RecordingIdentities()
{
    SECTION("recording reconnect identity: known same/different/unknown UID");
    const auto version = [](const std::string& uid) {
        return proto::ParseVer({"BWM427 fw=1.6.0 board=stm32f411 chip=stm32f411 proto=2",
                                "HAL 1.8.3 UID " + uid});
    };
    const auto first = device::RecordingIdentity(version("0123456789ABCDEF01234567"));
    CHECK(!first.empty());
    CHECK(first == device::RecordingIdentity(version("0123456789ABCDEF01234567")));
    CHECK(first != device::RecordingIdentity(version("0123456789ABCDEF01234568")));
    auto unknown = version(""); unknown.details.clear();
    CHECK(device::RecordingIdentity(unknown).empty());
    CHECK(device::RecordingIdentity(version("000000000000000000000000")).empty());
    CHECK(device::RecordingIdentity(version("0123456789ABCDEG01234567")).empty());
    CHECK(device::RecordingIdentity(version("0123456789ABCDEF")).empty());
}

void UpdaterPolicy()
{
    SECTION("updater-owned commands keep platform/busy guards and external mutations stay blocked");
    auto connection = std::make_unique<Script>(); auto* wire = connection.get();
    DeviceLink link(std::move(connection), false);
    std::int64_t t = 0;
    auto run = [&](int ms) { for (auto end = t + ms; t < end; t += 10) link.Step(t); };
    auto denied = [&](const std::string& cmd, Origin origin) {
        const auto r = link.Send(cmd, origin);
        CHECK_MSG(r->done && !r->ok, "%s", cmd.c_str());
    };
    auto allowed = [&](const std::string& cmd) {
        const auto r = link.Send(cmd, Origin::Firmware); run(80);
        CHECK_MSG(r->done && r->ok, "%s: %s", cmd.c_str(), r->final.c_str());
        CHECK(std::find(wire->writes.begin(), wire->writes.end(), cmd) != wire->writes.end());
    };
    run(500);
    const auto queued = link.Send("ota begin queued");
    link.SetControlLocks(false, true); run(50);
    CHECK(queued->done && !queued->ok);
    CHECK(std::find(wire->writes.begin(), wire->writes.end(), queued->cmd) == wire->writes.end());
    const auto writes = wire->writes.size();
    for (auto origin : {Origin::App, Origin::User, Origin::Internal, Origin::Poll})
        for (const char* cmd : {"zero", "rec on", "reset", "ota begin external", "ota chunk 0 00", "ota end", "ota abort"})
            denied(cmd, origin);
    denied("set freq 10", Origin::Firmware); // origin grants only firmware commands
    CHECK(wire->writes.size() == writes);
    denied("boot force", Origin::Firmware); // Tab5 is never a DFU target
    allowed("ota begin owned");
    link.SetPollPaused(true); run(3100);
    denied("ota begin stale", Origin::Firmware);
    allowed("ota chunk 0 00"); // retry after lost ACK can outlive the status TTL
    allowed("ota end");
    allowed("ota abort");
    // A new epoch cannot inherit an old updater's identity/capabilities.
    wire->statusReplies = false; ++wire->epoch; run(600);
    CHECK(!link.IsConnected());
    denied("ota chunk 0 00", Origin::Firmware);
    wire->statusReplies = true; run(2500);
    CHECK(link.IsConnected()); // first status is retried despite polling pause
    link.SetPollPaused(false);
    wire->identity = "BWM427 fw=1.6.0 board=stm32f411 chip=stm32f411 proto=2";
    wire->status = R"({"v":1,"sens":[],"fw":"1.6.0","board":"stm32f411","platform":"stm32f411","proto":2,"rec_mask":1,"xfer":"idle"})";
    ++wire->epoch; run(600);
    denied("boot", Origin::Firmware);
    link.SetControlLocks(true, true);
    denied("boot force", Origin::Firmware); // force never interrupts PC recording
    link.SetControlLocks(false, true);
    allowed("boot force"); // explicit updater force can interrupt SD recording
    wire->status = R"({"v":1,"sens":[],"fw":"1.6.0","board":"stm32f411","platform":"stm32f411","proto":2,"xfer":"get"})";
    link.Send("status"); run(80);
    denied("boot force", Origin::Firmware);
    wire->identity = "BWM427 fw=9 board=other chip=other proto=2";
    wire->status = R"({"v":1,"sens":[],"proto":2})";
    ++wire->epoch; run(600);
    denied("boot force", Origin::Firmware);
    denied("ota begin unknown", Origin::Firmware);
    denied("ota chunk 0 00", Origin::Firmware);
}

void LinkEpochs()
{
    SECTION("USB handshake, no speculative mode, status reset and stale command rejection");
    auto connection = std::make_unique<Script>(); auto* wire = connection.get();
    DeviceLink link(std::move(connection), false);
    std::int64_t t = 0;
    auto run = [&](int ms) { for (auto end = t + ms; t < end; t += 10) link.Step(t); };
    run(500);
    auto snap = link.Snapshot();
    CHECK(snap.haveVer && snap.haveStatus && snap.connectSeq == 1);
    CHECK(device::Detect(snap.ver, snap.status, true).IsTab5());
    CHECK(link.Send("addr 2 3")->done.load());
    CHECK(link.Send("set batalarm 10")->done.load());
    CHECK(link.Send("set gap 14")->done.load());
    const auto gap = link.Send("set gap 15"); run(100); CHECK(gap->done && gap->ok);
    auto mode = link.Send("instrument level"); run(100);
    CHECK(mode->ok && link.Snapshot().status.instrument == "heel"); // OK is not mode confirmation
    link.SetControlLocks(true, false);
    CHECK(link.Send("instrument level")->done.load());
    CHECK(link.Send("zero")->done.load());
    CHECK(link.Send("ota begin 100")->done.load());
    link.SetControlLocks(false, true);
    CHECK(link.Send("set freq 10")->done.load());
    CHECK(link.Send("rec on")->done.load());
    link.SetControlLocks(false, false);
    wire->statusReplies = false;
    wire->identity = "BWM427 inclinometer firmware v1.5, build Oct 7 2026";
    ++wire->epoch; run(10);
    snap = link.Snapshot();
    CHECK(!snap.haveStatus && !snap.haveVer && snap.status.instrument.empty());
    CHECK(!snap.status.instrumentControlCap && snap.status.recMask == 0);
    run(600);
    CHECK(link.Snapshot().haveVer && !link.Snapshot().haveStatus);
    const auto denied = link.Send("set freq 10");
    CHECK(denied->done && !denied->ok);
    wire->status = "{\"v\":1,\"sens\":[],\"fw\":\"1.5\",\"xfer\":\"idle\"}";
    wire->statusReplies = true; run(3500);
    snap = link.Snapshot();
    CHECK(snap.haveStatus && !snap.status.dualModeCap);
    CHECK(device::Detect(snap.ver, snap.status, true).platform == device::Platform::Stm32Legacy15);
    CHECK(link.Send("instrument level")->done.load());
    wire->statusReplies = false; link.SetPollPaused(true); run(3100);
    const auto stale = link.Send("zero"); CHECK(stale->done && !stale->ok);
}

void Simulator(bool tab5)
{
    SECTION(tab5 ? "Tab5 extended relative CLI and pair provenance" : "STM32 extended relative CLI and pair provenance");
    auto sim = std::make_shared<SimDevice>(3, 1791400000); sim->SetProfile(tab5);
    DeviceLink link(std::make_unique<SimConnection>(sim), false);
    std::int64_t t = 0;
    auto run = [&](int ms) { for (auto end = t + ms; t < end; t += 10) link.Step(t); };
    auto command = [&](const std::string& cmd) { auto r = link.Send(cmd); run(80); CHECK_MSG(r->done && r->ok, "%s: %s", cmd.c_str(), r->final.c_str()); return r; };
    auto relative = [&]() { auto r = command("relative status"); level::Status s; CHECK(!r->lines.empty()); if (!r->lines.empty()) CHECK(level::ParseStatus(r->lines[0], s)); return s; };
    run(600);
    CHECK(device::Detect(link.Snapshot().ver, link.Snapshot().status, true).Known());
    CHECK(link.Snapshot().status.instrumentControlCap && link.Snapshot().status.relativeDataCap);
    command("instrument level");
    CHECK(link.Snapshot().status.instrument == "heel");
    command("status"); CHECK(link.Snapshot().status.instrument == "level");
    auto s = relative(); CHECK(!s.ready && !s.axesConfirmed);
    command("relative axes confirm"); run(300);
    s = relative(); CHECK(s.usable());
    CHECK_NEAR(s.raw[0], s.obj[0] - s.ref[0], 1e-6, "relative Object - Reference");
    CHECK_NEAR(s.value[0], s.raw[0], 1e-6, "no REL offset initially");
    const auto zeroBefore = link.Snapshot().status.sens[0].ox;
    command("relative zero"); run(200);
    auto z = relative(); CHECK(z.offsetValid && z.generation > s.generation);
    CHECK_NEAR(z.value[0], z.raw[0] - z.offset[0], 2e-6, "REL zero is separate from head zero");
    command("status"); CHECK_NEAR(link.Snapshot().status.sens[0].ox, zeroBefore, 0, "head zero unchanged");
    command("relative reset"); run(200); CHECK(!relative().offsetValid);
    auto invalid = link.Send("relative roles 2 2"); run(100); CHECK(invalid->done && !invalid->ok);
    command("relative roles 3 2"); run(100); s = relative(); CHECK(!s.ready && !s.axesConfirmed);
    command("relative axes confirm"); run(200); CHECK(relative().usable());
    auto controls = sim->GetControls(); controls.sensorOn[1] = false; sim->SetControls(controls);
    run(600); s = relative(); CHECK(!s.ready && (s.quality & level::Missing));
    CHECK(!std::isfinite(s.value[0]));
    const auto seq = s.sequence; run(200); CHECK(relative().sequence == seq);
}

void RepeatedPolls()
{
    SECTION("repeated relative polls do not publish pairs; boundaries need fresh heads");
    SimDevice sim(6, 1791400000); sim.SetProfile(false);
    std::vector<std::string> lines;
    sim.SetEmitter([&](const auto& line) { lines.push_back(line); });
    auto cmd = [&](const std::string& text, std::int64_t t) { lines.clear(); sim.Command(text, t); };
    auto read = [&](std::int64_t t) { cmd("relative status", t); level::Status s; CHECK(lines.size() == 2); if (!lines.empty()) CHECK(level::ParseStatus(lines.front(), s)); return s; };
    sim.Tick(1000);
    cmd("instrument level", 1000); CHECK(lines.back() == "OK instrument");
    cmd("relative axes confirm", 1000); CHECK(lines.back() == "OK relative");
    CHECK(!read(1000).ready);
    sim.Tick(1100);
    const auto first = read(1100); CHECK(first.usable());
    const auto again = read(1100);
    CHECK(first.sequence == again.sequence && first.time == again.time && first.generation == again.generation);
    cmd("relative zero", 1100); CHECK(lines.back() == "OK relative");
    const auto zero = read(1100);
    CHECK(!zero.ready && zero.offsetValid && zero.generation > first.generation);
    CHECK(!std::isfinite(zero.value[0]));
    sim.Tick(1200); CHECK(read(1200).usable());
    auto controls = sim.GetControls(); controls.recSwitch = true; sim.SetControls(controls); sim.Tick(1300);
    cmd("instrument heel", 1300); CHECK(lines.back().rfind("ERR busy", 0) == 0);
    CHECK(read(1300).mode == "level");
}
} // namespace

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Profiles(); RecordingIdentities(); UpdaterPolicy(); LinkEpochs(); Simulator(false); Simulator(true); RepeatedPolls();
    return TestSummary();
}
