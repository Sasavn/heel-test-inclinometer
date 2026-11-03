/* Recompile the actual unchanged PC parser against the additive wire contract. */
#include "../../pc/src/core/Protocol.hpp"
#include <cassert>
#include <cstdio>
int main(){
 proto::DeviceStatus s;std::string err;
 assert(proto::ParseStatus(R"({"v":2,"fw":"1.8.0-esp32","dual_mode_cap":true,"instrument":"level","sens":[{"addr":2,"st":"OK","x":1.2,"y":-0.4,"live_x":1.2,"live_y":-0.4},{"addr":3,"st":"OK","x":1.28,"y":-0.44}],"rec_sync_cap":true,"rec_sync":false})",s,&err));
 assert(s.fw=="1.8.0-esp32"&&s.sens.size()==2&&s.Sensor(2)&&s.Sensor(3));
 assert(s.Sensor(2)->x==1.2&&s.Sensor(3)->y==-.44);
 proto::RawSample raw;assert(proto::ParseSampleLine("R,2,1234,567890,-46,-17,-458,0,118",raw));
 assert(raw.addr==2&&raw.n==1234&&raw.tMs==567890&&raw.rawX==-46&&raw.offX==-458);
 proto::FileEntry file;
 assert(proto::ParseFileLine("F,2026-11-04_M001_D2.CSV,1024,04.11.2026",file)&&file.sensorAddr==2);
 assert(proto::ParseFileLine("F,2026-11-04_M001_REL.CSV,2048,04.11.2026",file));
 assert(file.size==2048); /* Listed/exportable, not mislabelled as a physical head. */
 assert(file.sensorAddr!=2&&file.sensorAddr!=3);
 puts("PASS recompiled PC parser: additive status keys, unchanged raw samples/D2 files, REL file remains distinct");
}
