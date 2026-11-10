#include "ui/App.hpp"
#include "core/TextUtil.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <source_location>
#include <imgui.h>
#include <implot.h>
void check(bool x, std::source_location loc = std::source_location::current()) { if (!x) { std::cerr << "failed line " << loc.line() << std::endl; std::exit(2); } }
int main() {
 ImGui::CreateContext(); ImPlot::CreateContext();
 proto::DeviceStatus status;
 check(proto::ParseStatus(R"({"v":1,"sens":[],"rec_sync_cap":true,"rec_sync":false,"rec_sw":false})",status));
 check(status.recSyncCap && status.recSync == false && status.actualRec == false);
 check(proto::ParseStatus(R"({"v":1,"sens":[],"rec_sync_cap":1,"rec_sync":1,"rec_sw":1})",status));
 check(!status.recSyncCap && !status.recSync && !status.actualRec);
 ui::AppOptions o; o.threaded=false; o.startDemo=true; o.useSettingsFile=false;
 o.noHardware=true;
 ui::App a(o); long long t=0;
 auto run=[&](int ms) { for(int i=0;i<ms;i+=10) { a.Link().Step(t+=10); a.Tick(); } };
 auto cmd=[&](const char* s) { auto q=a.Link().Send(s); run(1500); check(q->done); return q; };
 auto dir=std::filesystem::temp_directory_path()/(sizeof(void*) == 8 ? "krenomer-recsync-tests64" : "krenomer-recsync-tests32");
 std::filesystem::create_directories(dir); a.SetRecordDir(text::PathToUtf8(dir),false);
 run(4000); check(!a.PcRecording()); check(!a.ZeroRecordingWarning());
 a.RequestRecordSync(true); run(3000);
 check(a.Link().Snapshot().status.recSync.value_or(false));
 check(a.StartPcRecording()); run(3000);
 check(a.Link().Snapshot().status.actualRec.value_or(false)); check(a.ZeroRecordingWarning());
 a.StopPcRecording(); run(3000); check(!a.Link().Snapshot().status.actualRec.value_or(true));
 check(!a.ZeroRecordingWarning());
 auto commands=[&]() { int n=0; for (const auto& l : a.Link().TermSince(0)) if(l.kind=='>' && (l.text=="rec on" || l.text=="rec off")) ++n; return n; };
 auto before=commands();
 auto c=a.Sim()->GetControls(); c.recSwitch=true; a.Sim()->SetControls(c); run(3000);
 check(a.PcRecording()); c.recSwitch=false; a.Sim()->SetControls(c); run(3000); check(!a.PcRecording()); check(commands()==before);
 // Repeated snapshots must never echo device edges as rec commands.
 auto tx=a.Link().Snapshot().cmdsTx; run(3000); check(!a.PcRecording());
 check(a.Link().Snapshot().cmdsTx > tx); // polling still active
 // Fast local START -> STOP before either command ACK reaches the App.
 check(a.StartPcRecording()); a.StopPcRecording(); run(4000);
 check(!a.PcRecording()); check(!a.Link().Snapshot().status.actualRec.value_or(true));
 // OFF disables both local->device and device->local synchronization.
 a.RequestRecordSync(false); run(3000);
 check(a.StartPcRecording()); run(2000); check(!a.Link().Snapshot().status.actualRec.value_or(true));
 a.StopPcRecording(); c=a.Sim()->GetControls(); c.recSwitch=true; a.Sim()->SetControls(c); run(3000); check(!a.PcRecording());
 c.recSwitch=false; a.Sim()->SetControls(c); run(2000);
 a.RequestRecordSync(true); run(3000);
 // Missing capability also gates synchronization.
 c=a.Sim()->GetControls(); c.recSyncCapability=false; a.Sim()->SetControls(c); run(2000);
 c.recSwitch=true; a.Sim()->SetControls(c); run(3000); check(!a.PcRecording());
 c.recSwitch=false; c.recSyncCapability=true; a.Sim()->SetControls(c); run(3000);
 // A local filesystem failure must not fake a start or retry a held edge.
 auto bad=dir/"not-a-directory"; { std::ofstream f(bad); f << "x"; }
 a.SetRecordDir(text::PathToUtf8(bad),false);
 c.recSwitch=true; a.Sim()->SetControls(c); run(3000); check(!a.PcRecording());
 a.SetRecordDir(text::PathToUtf8(dir),false); run(2000); check(!a.PcRecording());
 c.recSwitch=false; a.Sim()->SetControls(c); run(2000);
 c.persistenceFailure=true; a.Sim()->SetControls(c); check(!cmd("set recsync 0")->ok);
 check(a.Link().Snapshot().status.recSync.value_or(false));
 c.persistenceFailure=false; c.card=false; a.Sim()->SetControls(c); run(2000);
 check(a.StartPcRecording()); run(3000); check(a.PcRecording());
 check(!a.Link().Snapshot().status.actualRec.value_or(true));
 a.StopPcRecording(); run(2000);
 a.Link().SetEnabled(false); run(1000); check(!a.ZeroRecordingWarning());
 c.card=true; c.clockError=true; c.recSwitch=false; a.Sim()->SetControls(c);
 a.Link().SetEnabled(true); run(5000); check(a.StartPcRecording()); run(3000);
 check(a.PcRecording()); check(!a.Link().Snapshot().status.actualRec.value_or(true));
 a.StopPcRecording(); run(2000); a.Link().SetEnabled(false); run(1000);
 c.clockError=false; c.card=true; c.recSwitch=true; a.Sim()->SetControls(c);
 a.Link().SetEnabled(true); run(5000); check(!a.PcRecording());
 // Connected stream activity is not a fresh recording status.
 check(a.ZeroRecordingWarning()); a.Link().SetPollPaused(true); run(4500);
 check(!a.ZeroRecordingWarning());
 c.recSwitch=false; a.Sim()->SetControls(c); run(2000);
 a.Link().SetPollPaused(false); run(3000); check(!a.PcRecording());
 // An acknowledged old setting result cannot trigger a new-generation start.
 c.recSwitch=false; a.Sim()->SetControls(c); run(3000);
 a.RequestRecordSync(false);
 for(int i=0;i<100;++i) a.Link().Step(t+=10); // ACK available, App has not consumed it
 a.Link().SetEnabled(false); run(1000);
 a.Link().SetEnabled(true); run(5000); check(!a.PcRecording());
 check(!a.Link().Snapshot().status.recSync.value_or(true));
 std::cout << "record sync integration OK\n";
}
