#include "comm/TcpConnection.hpp"
#include "comm/DeviceLink.hpp"
#include "comm/SimDevice.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#define check(b) do { if(!(b)) throw std::runtime_error("transport assertion line " + std::to_string(__LINE__)); } while(false)
struct Server {
 SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP), peer=INVALID_SOCKET;
 unsigned port;
 Server() { sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); check(bind(s,(sockaddr*)&a,sizeof(a))==0); int n=sizeof(a); getsockname(s,(sockaddr*)&a,&n); port=ntohs(a.sin_port); listen(s,1); u_long one=1; ioctlsocket(s,FIONBIO,&one); }
 void acceptTick() { if(peer==INVALID_SOCKET) peer=accept(s,nullptr,nullptr); }
 ~Server(){if(peer!=INVALID_SOCKET)closesocket(peer);closesocket(s);}
};
static void pump(TcpConnection& c,Server& s,int start,int count=30) {for(int i=0;i<count;++i){c.Tick(start+i);s.acceptTick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}

// Socket delivery uses wall time; all protocol decisions use the explicit virtual clock.
static void fullLink(int authAt, bool loseVer) {
 Server server;
 auto tcp=std::make_unique<TcpConnection>("127.0.0.1",server.port,"test-only");
 DeviceLink link(std::move(tcp),false);
 SimDevice device(1,1791400000);
 device.SetEmitter([&](const std::string& line){auto data=line+"\r\n";check(send(server.peer,data.data(),(int)data.size(),0)==(int)data.size());});
 std::string rx; bool auth=false; int firstVer=-1,connected=-1,status=-1,verCount=0;
 auto settle=[&](int now){
  for(int i=0;i<100;++i){
   link.Step(now);server.acceptTick();
   if(server.peer!=INVALID_SOCKET){u_long one=1;ioctlsocket(server.peer,FIONBIO,&one);char b[4096];int n=recv(server.peer,b,sizeof(b),0);if(n>0)rx.append(b,n);
    size_t pos;
    while((pos=rx.find('\n'))!=std::string::npos){auto cmd=rx.substr(0,pos);rx.erase(0,pos+1);if(!cmd.empty()&&cmd.back()=='\r')cmd.pop_back();
     if(cmd.rfind("auth ",0)==0){check(cmd=="auth test-only");auth=true;}
     else {if(cmd=="ver"){if(firstVer<0)firstVer=now;++verCount;if(loseVer&&verCount==1)continue;}device.Command(cmd,now);}
    }
   }
   if(link.IsConnected()&&connected<0)connected=now;
   if(link.Snapshot().haveStatus&&status<0)status=now;
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
 };
 settle(0);check(auth);check(!link.IsConnected());
 send(server.peer,"OK\n",3,0);settle(authAt);
 check(link.Connection()->IsOpen());check(!link.IsConnected());
 settle(authAt+199);check(firstVer<0);
 settle(authAt+200);check(firstVer==authAt+200);
 if(loseVer){check(!link.IsConnected());settle(authAt+1200);settle(authAt+1499);check(verCount==1);settle(authAt+1500);}
 check(connected==authAt+(loseVer?1500:200));check(status==connected);check(link.Snapshot().haveVer);
 for(const auto& line:link.TermSince(0))check(line.text.find("test-only")==std::string::npos);

 // Production splitter: split one R across two writes, then deliver multiple lines together.
 link.SetSamples(true);settle(connected+1);check(link.Snapshot().samplesOn);
 const auto before=link.Snapshot();
 send(server.peer,"R,2,10,100,1",12,0);settle(connected+10);
 check(link.Snapshot().samplesRx==before.samplesRx);
 const std::string tail=",2,0,0,40\nR,2,13,101,1,2,0,0,40\nR,broken\n";
 check(send(server.peer,tail.data(),(int)tail.size(),0)==(int)tail.size());settle(connected+20);
 check(link.Snapshot().samplesRx==before.samplesRx+2);
 check(link.Snapshot().sampleSeqGaps==before.sampleSeqGaps+2);
 check(link.Snapshot().badLines==before.badLines+1);
 // An intentional OFF interval and late R must not inflate the next ON interval.
 auto raw=[&](unsigned n,int at){auto data="R,2,"+std::to_string(n)+",100,1,2,0,0,40\n";check(send(server.peer,data.data(),(int)data.size(),0)==(int)data.size());settle(at);};
 const auto gaps=link.Snapshot().sampleSeqGaps;
 link.SetSamples(false);raw(1000,connected+21);check(link.Snapshot().sampleSeqGaps==gaps);
 settle(connected+22);check(!link.Snapshot().samplesOn);
 raw(2000,connected+23);check(link.Snapshot().sampleSeqGaps==gaps);
 link.SetSamples(true);raw(3000,connected+24);check(link.Snapshot().sampleSeqGaps==gaps);
 settle(connected+25);check(link.Snapshot().samplesOn);
 raw(4000,connected+26);check(link.Snapshot().sampleSeqGaps==gaps);
 raw(4002,connected+27);check(link.Snapshot().sampleSeqGaps==gaps+1);
 raw(3,connected+28);check(link.Snapshot().sampleSeqGaps==gaps+1);
 raw(4,connected+29);check(link.Snapshot().sampleSeqGaps==gaps+1);
 link.SetSamples(false);settle(connected+30);link.SetSamples(true);settle(connected+31);
 raw(0xfffffffeu,connected+32);raw(1,connected+33);check(link.Snapshot().sampleSeqGaps==gaps+3);
 std::cout<<"planned OFF/ON + late R no false gap; forward gap, decrease, uint32 wrap PASS\n";
 const std::string stream="S,100,10,O,1,2,0,O,3,4,0,0,10,,15,1,4,0\n";
 send(server.peer,stream.data(),(int)stream.size(),0);settle(6100);
 check(link.Snapshot().haveSample);
 settle(8099);{auto& h=link.GetHistory();std::lock_guard lock(h.mutex);check(h.Size()>0);check(std::isfinite(h.At(h.Size()-1).x[0]));}
 settle(8100);{auto& h=link.GetHistory();std::lock_guard lock(h.mutex);check(std::isnan(h.At(h.Size()-1).x[0]));}
 send(server.peer,stream.data(),(int)stream.size(),0);settle(8200);
 {auto& h=link.GetHistory();std::lock_guard lock(h.mutex);check(h.Size()>=3);check(std::isfinite(h.At(h.Size()-1).x[0]));check(std::isnan(h.At(h.Size()-2).x[0]));}
 check(link.Snapshot().maxStreamIntervalMs==2100);
 std::cout<<"fragment pending != parse error; multiline R=2 gap_n=2 malformed=1; stale NaN at 2000ms and resume finite PASS\n";
 // EOF is observed at 10000; the existing retry is exactly 1000 ms.
 closesocket(server.peer);server.peer=INVALID_SOCKET;
 settle(10000);check(!link.Connection()->IsOpen());
 settle(10999);check(server.peer==INVALID_SOCKET);
 auth=false;settle(11000);check(auth);check(!link.Connection()->IsOpen());
 send(server.peer,"OK\n",3,0);settle(11000);check(link.Connection()->OpenCount()==2);
 check(!link.IsConnected());settle(11199);check(!link.IsConnected());
 settle(11200);check(link.IsConnected());check(link.Snapshot().haveStatus);
 std::cout<<"EOF=10000 retry_before=10999 blocked retry=11000 ready/status=11200 PASS\n";
 std::cout<<"DeviceLink virtual_ms auth="<<authAt<<" first_ver="<<firstVer<<" ver_attempts_including_reconnect="<<verCount<<" connected="<<connected<<" status="<<status<<" PASS\n";
}
int main(){ WSADATA w{}; WSAStartup(MAKEWORD(2,2),&w);
 for(bool reject:{false,true}) { Server s; TcpConnection c("127.0.0.1",s.port,"test-only"); pump(c,s,0); check(s.peer!=INVALID_SOCKET); char b[128]; int n=recv(s.peer,b,sizeof(b),0); check(n==15 && std::string(b,n)=="auth test-only\n"); check(!c.IsOpen());
 const std::string reply=reject?"ERR auth\nOK\n":"O"; send(s.peer,reply.data(),(int)reply.size(),0); pump(c,s,100); check(!c.IsOpen());
 if(!reject){send(s.peer,"K\r\n",3,0);pump(c,s,4500);check(c.IsOpen());check(c.OpenCount()==1);check(c.Write("ver\r\n"));pump(c,s,4600);n=recv(s.peer,b,sizeof(b),0);check(std::string(b,n)=="ver\r\n");}
 else {check(!c.Write("ver\r\n"));check(c.OpenCount()==0);check(c.Problem()=="TCP authentication failed");pump(c,s,20000);check(!c.IsOpen());check(c.OpenCount()==0);std::cout<<"ERR+OK blocked through 20029; writes refused; opens=0 PASS\n";}
 }
 {Server s;TcpConnection c("127.0.0.1",s.port,"test-only");pump(c,s,0);c.Tick(5001);check(!c.IsOpen());check(c.Problem()=="TCP disconnected; retrying");c.Tick(6000);check(c.Problem()=="TCP disconnected; retrying");c.Tick(6001);check(c.Problem()=="TCP connecting");std::cout<<"auth deadline 5001; retry 6000 blocked, 6001 started PASS\n";}
 {TcpConnection c("invalid",1,"test-only");c.Tick(0);check(!c.IsOpen());check(c.OpenCount()==0);}
 fullLink(0,false);fullLink(4500,false);fullLink(0,true);
 WSACleanup();std::cout<<"loopback transport cases passed\n";
}
