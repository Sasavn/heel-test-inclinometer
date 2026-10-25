// Headless offline clock tests. No COM, GUI or external network.
#if defined(_WIN32) && __has_include("comm/TcpConnection.hpp")
#include "comm/TcpConnection.hpp"
#define CLOCK_TCP_TEST 1
#endif
#include "TestUtil.hpp"
#include "core/ClockSync.hpp"
#include "comm/DeviceLink.hpp"
#include "comm/SimConnection.hpp"
#include <deque>
#include <thread>
#include <chrono>

static proto::DeviceStatus Status(const std::string& extra)
{
    proto::DeviceStatus s;
    CHECK(proto::ParseStatus("{\"v\":1,\"sens\":[]," + extra + "}", s));
    return s;
}

// Independent protocol peer with real SimDevice for all ordinary commands.
class Peer final : public IConnection
{
public:
    SimDevice sim{13, 1791400000};
    std::deque<std::string> pending;
    std::vector<std::string> written;
    bool open = true, capability = true, drop = false, disconnect = false;
    std::string ack = "OK time";
    Peer()
    {
        sim.SetEmitter([this](const std::string& line) {
            auto out = line;
            if (capability && !out.empty() && out[0] == '{')
                out.insert(1, "\"time_tz_offset\":true,\"timezone_minutes\":180,");
            pending.push_back(out);
        });
    }
    void Tick(std::int64_t) override
    {
        while (open && !pending.empty()) { auto l=pending.front();pending.pop_front();EmitLine(l); }
    }
    bool Write(const std::string& data) override
    {
        std::string c=data;
        while (!c.empty() && (c.back()=='\r' || c.back()=='\n')) c.pop_back();
        written.push_back(c);
        if (c.rfind("set time ",0)==0)
        {
            if (disconnect) {open=false; return false;}
            if (!drop) pending.push_back(ack);
        }
        else sim.Command(c, 0);
        return open;
    }
    bool IsOpen() override {return open;}
    void SetEnabled(bool on) override {open=on;}
    bool Enabled() const override {return open;}
    void Reject(std::int64_t,bool) override {open=false;}
    void Reopen(std::int64_t) override {open=false;}
    std::string PortName() override {return "offline-peer";}
    std::uint64_t OpenCount() override {return 1;}
    std::vector<PortInfo> Ports() override {return {};}
    void SetPortChoice(const std::string&) override {}
    bool IsDemo() const override {return true;}
    std::string Problem() override {return "offline disconnect";}
};

static void PureTests()
{
    CHECK(!clockpc::CurrentStatus(true, 100, 200));
    CHECK(!clockpc::CurrentStatus(false, 200, 200));
    CHECK(clockpc::CurrentStatus(true, 200, 200));
    SECTION("capability must be explicit boolean; UTC zero and invalid offsets");
    CHECK(!Status("\"time_tz_offset\":1").timeTzOffset);
    CHECK(!Status("\"time_tz_offset\":\"true\"").timeTzOffset);
    CHECK(!Status("\"rtc\":true").timeTzOffset);
    auto s=Status("\"time_tz_offset\":true,\"timezone_minutes\":0");
    CHECK(s.timeTzOffset && s.timezoneMinutes && *s.timezoneMinutes==0);
    CHECK(!Status("\"timezone_minutes\":303").timezoneMinutes);
    CHECK(!Status("\"timezone_minutes\":841").timezoneMinutes);
    CHECK(!Status("\"timezone_minutes\":180.5").timezoneMinutes);
    SECTION("offset rollover and fractional-hour zones");
    std::tm utc{}, local{};utc.tm_yday=0;utc.tm_hour=0;local.tm_yday=364;local.tm_hour=12;
    CHECK(clockpc::OffsetMinutes(local,utc)==-720);
    utc.tm_yday=364;utc.tm_hour=23;local.tm_yday=0;local.tm_hour=13;
    CHECK(clockpc::OffsetMinutes(local,utc)==840);
    utc={};local={};local.tm_hour=5;local.tm_min=45;
    CHECK(clockpc::OffsetMinutes(local,utc)==345);
    SECTION("current PC offset includes DST, no hardcoded UTC+3");
#ifdef _WIN32
    const char* before=std::getenv("TZ"); const std::string saved=before?before:"";
    const std::pair<const char*, int> zones[]={{"UTC0",0},{"MSK-3",180},{"NPT-5:45",345},{"EST5EDT",-240}};
    for (auto [zone,offset]:zones)
    {
        _putenv_s("TZ",zone);_tzset();
        const auto command=clockpc::Command(1782864000,true); // July 1 2026 UTC
        CHECK(command && command->ends_with(" tz="+std::to_string(offset)));
        const auto legacy=clockpc::Command(1782864000,false);
        CHECK(legacy && legacy->size()==28 && legacy->find("tz=")==std::string::npos);
    }
    _putenv_s("TZ",saved.c_str());_tzset();
#endif
    SECTION("canonical status epoch: different zones, day and year rollover");
    s.timeTzOffset=true;s.timezoneMinutes=180;s.time="2026-01-01 02:00:00";
    const auto epoch=clockpc::StatusTime(s);
    s.timezoneMinutes=0;s.time="2025-12-31 23:00:00";
    CHECK(clockpc::StatusTime(s)==epoch);
    s.timezoneMinutes=-720;s.time="2025-12-31 11:00:00";
    CHECK(clockpc::StatusTime(s)==epoch);
    s.timezoneMinutes=840;s.time="2026-01-01 13:00:00";
    CHECK(clockpc::StatusTime(s)==epoch);
    s.time="2026-02-30 13:00:00";CHECK(clockpc::StatusTime(s)==-1);
    s.timeTzOffset=false;s.time="2026-01-01 12:00:00";
    CHECK(clockpc::StatusTime(s)==text::ParseLocalTime(s.time));
    SECTION("ACK must confirm time; legacy absent RTC is partial success");
    CHECK(clockpc::Acknowledgement(true,"OK time")==clockpc::Ack::Complete);
    CHECK(clockpc::Acknowledgement(true,"OK time=2026-10-26 10:00:00 (rtc yes)")==clockpc::Ack::Complete);
    CHECK(clockpc::Acknowledgement(true,"OK time=2026-10-26 10:00:00 (rtc no)")==clockpc::Ack::SystemOnly);
    for (auto reply:{"OK","OK timeout","ERR RTC; system clock may be updated","нет ответа"})
        CHECK(clockpc::Acknowledgement(true,reply)==clockpc::Ack::Failed);
    CHECK(clockpc::Acknowledgement(false,"OK time")==clockpc::Ack::Failed);
}
static void UsbModel()
{
    SECTION("USB model through actual SimConnection and DeviceLink, strict legacy command");
    auto sim=std::make_shared<SimDevice>(1,1791400000);
    DeviceLink link(std::make_unique<SimConnection>(sim),false);
    std::int64_t t=0;for (;t<2000;t+=10) link.Step(t);
    CHECK(link.IsConnected());auto status=link.Snapshot().status;CHECK(!status.timeTzOffset);
    auto cmd=clockpc::Command(1782864000,status.timeTzOffset);CHECK(cmd.has_value());if(!cmd)return;
    auto q=link.Send(*cmd);for(;t<6000 && !q->done;t+=10)link.Step(t);
    CHECK(q->done && clockpc::Acknowledgement(q->ok,q->final)==clockpc::Ack::Complete);
    for(std::int64_t end=t+1500;t<end;t+=10)link.Step(t);
    CHECK(link.Snapshot().status.time.substr(0,16)==cmd->substr(9,16));
}
static void RequestTests()
{
    SECTION("IConnection requests: ACK, ERR RTC, unrelated OK, timeout, disconnect, retry");
    for(int mode=0;mode<5;++mode)
    {
        auto peer=std::make_unique<Peer>();auto* p=peer.get();
        DeviceLink link(std::move(peer),false);std::int64_t t=0;
        for(;t<1000;t+=10)link.Step(t);
        CHECK(link.IsConnected());CHECK(link.Snapshot().status.timeTzOffset);
        for(const auto& c:p->written) CHECK(c.rfind("set time ",0)!=0); // no auto sync
        if(mode==1)p->ack="ERR clock/RTC/timezone; system clock may be updated";
        if(mode==2)p->ack="OK";
        if(mode==3)p->drop=true;
        if(mode==4)p->disconnect=true;
        auto command=clockpc::Command(1782864000,link.Snapshot().status.timeTzOffset);
        CHECK(command && command->find(" tz=")!=std::string::npos);if(!command)continue;
        auto q=link.Send(*command);
        CHECK(!q->done); // send is never confirmation
        for(std::int64_t end=t+5000;t<end && !q->done;t+=10)link.Step(t);
        CHECK(q->done);
        CHECK((clockpc::Acknowledgement(q->ok,q->final)==clockpc::Ack::Complete)==(mode==0));
        if(mode==1)CHECK(q->final==p->ack);
        if(mode==3)CHECK(q->timedOut);
        if(mode==4)CHECK(q->linkLost);
        if(mode==1){p->ack="OK time";auto retry=link.Send(*command);
            for(std::int64_t end=t+4000;t<end && !retry->done;t+=10)link.Step(t);
            CHECK(retry->done && clockpc::Acknowledgement(retry->ok,retry->final)==clockpc::Ack::Complete);}
    }
}
#ifdef CLOCK_TCP_TEST
static void TcpTest(bool rtcFailure)
{
    SECTION("actual TcpConnection + DeviceLink loopback only: auth, split ACK, timezone, RTC failure");
    WSADATA ws{};CHECK(WSAStartup(MAKEWORD(2,2),&ws)==0);
    SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    CHECK(server!=INVALID_SOCKET);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    CHECK(bind(server,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);CHECK(listen(server,1)==0);
    int len=sizeof(a);CHECK(getsockname(server,reinterpret_cast<sockaddr*>(&a),&len)==0);
    std::string clockCommand;bool auth=false;
    std::thread worker([&]{
        fd_set rd;FD_ZERO(&rd);FD_SET(server,&rd);timeval wait{5,0};
        if(select(0,&rd,nullptr,nullptr,&wait)<=0)return;
        SOCKET c=accept(server,nullptr,nullptr);if(c==INVALID_SOCKET)return;
        DWORD ms=3000;setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&ms),sizeof(ms));
        SimDevice sim(3,1791400000);
        auto emit=[&](std::string line){if(!line.empty()&&line[0]=='{')line.insert(1,"\"time_tz_offset\":true,\"timezone_minutes\":180,");
            line+="\r\n";for(std::size_t i=0;i<line.size();){int n=send(c,line.data()+i,static_cast<int>(line.size()-i),0);if(n<=0)break;i+=n;}};
        sim.SetEmitter(emit);std::string pending;char buf[512];bool done=false;
        while(!done){int n=recv(c,buf,sizeof(buf),0);if(n<=0)break;pending.append(buf,n);
            for(auto e=pending.find('\n');e!=std::string::npos;e=pending.find('\n')){
                auto cmd=pending.substr(0,e);pending.erase(0,e+1);if(!cmd.empty()&&cmd.back()=='\r')cmd.pop_back();
                if(!auth){auth=cmd=="auth offline-test";emit(auth?"OK":"ERR AUTH");}
                else if(cmd.rfind("set time ",0)==0){clockCommand=cmd;
                    if(!rtcFailure)send(c,"O",1,0);std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    emit(rtcFailure?"ERR clock/RTC/timezone; system clock may be updated":"K time");done=true;break;}
                else sim.Command(cmd,0);
            }
        }
        // Keep the socket alive until DeviceLink consumes the terminator.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));shutdown(c,SD_BOTH);closesocket(c);
    });
    {
        DeviceLink link(std::make_unique<TcpConnection>("127.0.0.1",ntohs(a.sin_port),"offline-test"),false);
        std::int64_t t=0;RequestPtr request;
        for(;t<5000;t+=2){link.Step(t);auto s=link.Snapshot();
            if(!request&&s.haveStatus){auto cmd=clockpc::Command(1782864000,s.status.timeTzOffset);CHECK(cmd.has_value());if(cmd)request=link.Send(*cmd);}
            if(request&&request->done)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        CHECK(request && request->done);
        if(request)CHECK((clockpc::Acknowledgement(request->ok,request->final)==clockpc::Ack::Complete)==!rtcFailure);
    }
    worker.join();CHECK(auth);CHECK(clockCommand.find(" tz=")!=std::string::npos);
    closesocket(server);WSACleanup();
}
#endif
int main(int argc,char** argv)
{
    testutil::Init(argc,argv);PureTests();UsbModel();RequestTests();
#ifdef CLOCK_TCP_TEST
    TcpTest(false);TcpTest(true);
#endif
    return TestSummary();
}
