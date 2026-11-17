#ifdef _WIN32
#include "../src/ui/WindowsEventWait.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
namespace {
using Api=ui::detail::WindowsWaitApi;
using Wait=ui::WindowsEventWait;
int checks=0,closed=0;DWORD fallbackMs=0,lastSleep=0;
void Check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
HANDLE WINAPI Standard(LPSECURITY_ATTRIBUTES a,LPCWSTR n,DWORD flags,DWORD access){
    if(flags)return nullptr;auto api=Api::System();return api.createEx?api.createEx(a,n,flags,access):CreateWaitableTimerW(a,FALSE,n);
}
HANDLE WINAPI NoLegacy(LPSECURITY_ATTRIBUTES,BOOL,LPCWSTR){return nullptr;}
BOOL WINAPI NoArm(HANDLE,const LARGE_INTEGER*,LONG,PTIMERAPCROUTINE,LPVOID,BOOL){return FALSE;}
BOOL WINAPI CountClose(HANDLE h){++closed;return CloseHandle(h);}
DWORD WINAPI CountWait(DWORD n,const HANDLE*h,DWORD ms,DWORD mask,DWORD flags){
    Check(ms>=1&&ms<=1000,"finite ceil timeout");Check(mask==QS_ALLINPUT&&flags==MWMO_INPUTAVAILABLE,"already-seen input wakes wait");
    if(!n)fallbackMs=ms;return MsgWaitForMultipleObjectsEx(n,h,ms,mask,flags);
}
DWORD WINAPI TimerWaitFailure(DWORD n,const HANDLE*h,DWORD ms,DWORD mask,DWORD flags){return n?WAIT_FAILED:CountWait(n,h,ms,mask,flags);}
DWORD WINAPI AllWaitFailure(DWORD,const HANDLE*,DWORD,DWORD,DWORD){return WAIT_FAILED;}
void WINAPI CountSleep(DWORD ms){lastSleep=ms;}
void TestTimer(Api api,Wait::Mode expected,const char* name){
    api.close=CountClose;int before=closed;
    {Wait w(api);Check(w.TimerMode()==expected,name);auto t=std::chrono::steady_clock::now();w.Wait(.00035);
     Check(std::chrono::steady_clock::now()-t>=std::chrono::microseconds(250),"sub-ms wait blocks");w.Wait(0);w.Wait(-1);w.Wait(INFINITY);w.Wait(NAN);}
    Check(closed==before+1,"RAII closes one timer");
}
}
int main(){
    Api native=Api::System();native.wait=CountWait;
    {Wait w;std::printf("Native timer mode: %d (0=highres, 1=standard, 2=messages)\n",int(w.TimerMode()));}
    Api standard=native;standard.createEx=Standard;TestTimer(standard,Wait::Mode::StandardTimer,"forced standard timer");
    Api legacy=native;legacy.createEx=nullptr;TestTimer(legacy,Wait::Mode::StandardTimer,"Ex missing uses legacy timer");
    Api none=native;none.createEx=nullptr;none.create=NoLegacy;
    {Wait w(none);Check(w.TimerMode()==Wait::Mode::MessagesOnly,"all timer creation fails");w.Wait(.00001);Check(fallbackMs==1,"tiny fallback ceil to 1 ms");w.Wait(.0011);Check(fallbackMs==2,"fractional fallback ceil");}
    Api broken=native;broken.arm=NoArm;broken.close=CountClose;int before=closed;
    {Wait w(broken);w.Wait(.0004);Check(w.TimerMode()==Wait::Mode::MessagesOnly,"arm failure disables timer");w.Wait(.0004);}
    Check(closed==before+1,"broken timer closed once");
    Api failed=native;failed.wait=TimerWaitFailure;
    {Wait w(failed);w.Wait(.0004);Check(w.TimerMode()==Wait::Mode::MessagesOnly,"timer wait failure disables timer");Check(fallbackMs==1,"failed wait bounded fallback");}
    none.wait=AllWaitFailure;none.sleep=CountSleep;
    {Wait w(none);w.Wait(.0001);Check(lastSleep==1,"message-wait failure cannot spin");}
    MSG msg{};PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    DWORD target=GetCurrentThreadId();std::atomic<bool> posted{false};
    auto sender=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(10));posted=PostThreadMessageW(target,WM_APP+73,123,456)!=FALSE;});
    Wait w;auto start=std::chrono::steady_clock::now();
    do{w.Wait(.25);}while(!posted.load()&&std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
    sender.join();
    Check(posted.load(),"PostThreadMessage succeeded within bounded test");
    Check(std::chrono::steady_clock::now()-start<std::chrono::seconds(2),"input wakes wait");
    Check(PeekMessageW(&msg,nullptr,WM_APP+73,WM_APP+73,PM_REMOVE)!=FALSE,"wait does not remove input");
    Check(msg.wParam==123&&msg.lParam==456,"message preserved");
    std::printf("PASS: %d checks: standard/missing APIs, arm/wait failures, ceil fallback, RAII, message wake\n",checks);
}
#else
int main(){return 0;}
#endif
