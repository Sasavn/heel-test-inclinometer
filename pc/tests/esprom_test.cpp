// Keep test assertions active in Release/MSVC builds too.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/fw/EspRom.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <set>
using namespace fw::esprom;
static void set32(Bytes& b,std::size_t p,std::uint32_t n){for(unsigned i=0;i<4;i++)b.at(p+i)=std::uint8_t(n>>(8*i));}
static void set16(Bytes& b,std::size_t p,unsigned n){b.at(p)=std::uint8_t(n);b.at(p+1)=std::uint8_t(n>>8);}
static Bytes image(bool app){
    Bytes b(24,0);b[0]=0xe9;b[1]=1;b[12]=18;b[23]=1;set16(b,15,100);set16(b,17,199);
    detail::put(b,0x4ff00000);detail::put(b,app?256:64);std::size_t p=b.size();b.resize(p+(app?256:64),0);
    if(app){set32(b,p,0xabcd5432);std::memcpy(b.data()+80,"krenomer-esp32",14);std::memcpy(b.data()+48,"1.0.0-esp32",11);}
    // Include escaped bytes in both request payloads and image digests.
    b.back()=0xc0;b[b.size()-2]=0xdb;std::uint8_t sum=0xef;for(std::size_t i=p;i<b.size();i++)sum^=b[i];
    b.resize((b.size()+16)&~std::size_t(15),0);b.back()=sum;auto digest=Sha256(b);b.insert(b.end(),digest.begin(),digest.end());return b;
}
static Part part(PartKind kind,std::uint32_t offset,Bytes data){Part p;p.kind=kind;p.offset=offset;p.data=std::move(data);return p;}
static std::vector<Part> plan(){
    Bytes table(3072,0xff);const std::uint32_t offsets[]={0x9000,0xf000,0x11000,0x20000,0x620000},sizes[]={0x6000,0x2000,0x1000,0x600000,0x600000};const std::uint8_t types[]={1,1,1,0,0},subtypes[]={2,0,1,0x10,0x11};const char* names[]={"nvs","otadata","phy_init","ota_0","ota_1"};
    for(unsigned i=0;i<5;i++){unsigned p=i*32;std::fill(table.begin()+p,table.begin()+p+32,0);set16(table,p,0x50aa);table[p+2]=types[i];table[p+3]=subtypes[i];set32(table,p+4,offsets[i]);set32(table,p+8,sizes[i]);std::memcpy(table.data()+p+12,names[i],std::strlen(names[i]));}
    set16(table,160,0xebeb);auto md5=Md5(Bytes(table.begin(),table.begin()+160));std::copy(md5.begin(),md5.end(),table.begin()+176);
    return {part(PartKind::Bootloader,0x2000,image(false)),part(PartKind::Partitions,0x8000,table),part(PartKind::OtaData,0xf000,Bytes(8192,0xff)),part(PartKind::Application,0x20000,image(true))};
}
static std::uint32_t otaCrc(std::uint32_t seq){
    // esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4), little endian.
    std::uint32_t crc=0;
    for(unsigned i=0;i<4;i++){crc^=(seq>>(8*i))&255;for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}
    return ~crc;
}
struct Mock: ITransport {
    Bytes flash=Bytes(16*1024*1024,0xa5),packet;
    std::deque<std::uint8_t> rx;
    std::vector<unsigned> ops,sequences;std::vector<std::uint32_t> begins;
    unsigned uart=1,registerWrites=0,chip=18,revision=100,flags=0,crypt=0,resets=0,ends=0,dataCount=0,corruptMd5=0;
    unsigned md5Count=0,corruptMd5At=0,disconnectBegin=0;
    unsigned timeoutData=0,checksumData=0,brokenData=0,disconnectData=0;
    bool silent=false,badHeader=false,stub=false,shortSecurity=false;
    std::size_t writeChunk=99999,readChunk=99999;
    std::uint32_t base=0,blocks=0,next=0;unsigned lastEnd=99;
    Mock(){ // A valid-looking old boot selection for ota_1; sector kept unless reset.
        std::fill(flash.begin()+0xf000,flash.begin()+0x11000,0xff);set32(flash,0xf000,2);set32(flash,0xf018,2);set32(flash,0xf01c,otaCrc(2));
    }
    unsigned bootSelected()const{
        // Model IDF selector for the seeded valid ota entry; FF -> ota_0 fallback.
        std::uint32_t seq=detail::u32(flash,0xf000);
        bool valid=seq!=0xffffffffu && detail::u32(flash,0xf018)==2 && detail::u32(flash,0xf01c)==otaCrc(seq);
        return valid?(seq-1)%2:0;
    }
    bool reset(ResetMode mode,unsigned timeout)override{assert(mode==ResetMode::EnterBootloader && timeout);resets++;rx.clear();packet.clear();return true;}
    std::size_t read(std::uint8_t* p,std::size_t n,unsigned timeout)override{assert(timeout);n=std::min(n,std::min(readChunk,rx.size()));for(std::size_t i=0;i<n;i++){p[i]=rx.front();rx.pop_front();}return n;}
    std::size_t write(const std::uint8_t* p,std::size_t n,unsigned timeout)override{
        assert(timeout);n=std::min(n,writeChunk);for(std::size_t i=0;i<n;i++){
            if(p[i]==0xc0 && packet.empty()){packet.push_back(0xc0);continue;}
            if(p[i]==0xc0 && packet.size()>1){packet.push_back(0xc0);auto request=SlipDecode(packet);packet.clear();handle(request);}
            else if(!packet.empty())packet.push_back(p[i]);
        }return n;
    }
    void reply(unsigned op,Bytes payload=Bytes(),std::uint32_t value=0,unsigned status=0,unsigned reason=0){
        payload.push_back(std::uint8_t(status));payload.push_back(std::uint8_t(reason));payload.push_back(0);payload.push_back(0);
        Bytes b={1,std::uint8_t(op),std::uint8_t(payload.size()),std::uint8_t(payload.size()>>8)};detail::put(b,value);b.insert(b.end(),payload.begin(),payload.end());if(badHeader && op!=8)b[2]++;
        auto wire=SlipEncode(b);rx.insert(rx.end(),wire.begin(),wire.end());
    }
    void handle(const Bytes& b){
        assert(b.size()>=8 && b[0]==0 && detail::u16(b,2)==b.size()-8);unsigned op=b[1];ops.push_back(op);Bytes d(b.begin()+8,b.end());if(silent)return;
        if(op==8){assert(d.size()==36 && d[0]==7 && d[3]==0x20 && d[35]==0x55);for(unsigned i=0;i<8;i++)reply(op,Bytes(),stub?0:0x20120707);return;}
        if(op==0x14){Bytes s(20,0);set32(s,0,flags);s[4]=std::uint8_t(crypt);set32(s,12,chip);set32(s,16,999);if(shortSecurity)s.resize(12);reply(op,s);return;}
        if(op==0x0a){unsigned address=detail::u32(d,0);
            if(address==0x4ff3fec8 || address==0x4ffbfec8){reply(op,Bytes(),uart);return;}
            if(address==0x5011601c){reply(op,Bytes(),0x123);return;}
            assert(address==0x5012d04c);unsigned major=revision/100,minor=revision%100;reply(op,Bytes(),((major&3)<<4)|((major>>2)<<23)|minor);return;}
        if(op==9){assert(chip==18 && flags==0 && crypt==0 && uart==6 && d.size()==16);unsigned address=detail::u32(d,0);assert(address==0x50116018||address==0x50116000||address==0x50116020||address==0x5011601c);registerWrites++;reply(op);return;}
        assert(chip==18 && flags==0 && crypt==0);
        if(op==0x0d){assert(d==Bytes(8,0));reply(op);return;}
        if(op==0x0b){assert(d.size()==24 && detail::u32(d,4)==flash.size() && detail::u32(d,12)==4096);reply(op);return;}
        if(op==2){assert(d.size()==20 && detail::u32(d,8)==1024 && detail::u32(d,16)==0);base=detail::u32(d,12);blocks=detail::u32(d,4);next=0;begins.push_back(base);
            unsigned size=detail::u32(d,0);assert(base+size<=flash.size());assert(base+size<=0x9000 || base>=0xf000);std::fill(flash.begin()+base,flash.begin()+base+size,0xff);if(disconnectBegin && begins.size()==disconnectBegin)throw std::runtime_error("Power lost after erase");reply(op);return;}
        if(op==3){assert(d.size()==1040 && detail::u32(d,0)==1024 && detail::u32(d,8)==0 && detail::u32(d,12)==0);unsigned seq=detail::u32(d,4);sequences.push_back(seq);dataCount++;
            unsigned checksum=0xef;for(std::size_t i=16;i<d.size();i++)checksum^=d[i];assert(detail::u32(b,4)==checksum);
            assert(seq<blocks && (seq==next || (next && seq==next-1)));
            if(disconnectData && dataCount==disconnectData)throw std::runtime_error("Power lost");
            if(checksumData){checksumData--;reply(op,Bytes(),0,1,7);return;}
            std::copy(d.begin()+16,d.end(),flash.begin()+base+seq*1024);next=seq+1;
            if(timeoutData){timeoutData--;return;} // response lost AFTER programming
            if(brokenData){brokenData--;const std::uint8_t wire[]={0xc0,0xdb,0x11,0xc0};rx.insert(rx.end(),wire,wire+4);return;}
            reply(op);return;
        }
        if(op==0x13){assert(d.size()==16);unsigned pos=detail::u32(d,0),size=detail::u32(d,4);auto digest=Md5(Bytes(flash.begin()+pos,flash.begin()+pos+size));std::string h=detail::hex(digest.data(),digest.size());md5Count++;if(corruptMd5 || md5Count==corruptMd5At){h[0]=h[0]=='0'?'1':'0';if(corruptMd5)corruptMd5--;}reply(op,Bytes(h.begin(),h.end()));return;}
        if(op==4){assert(d.size()==4);lastEnd=detail::u32(d,0);ends++;reply(op);return;}
        assert(false);
    }
};
static void revisionRange(std::vector<Part>& p,unsigned minimum,unsigned maximum){
    for(auto& v:p)if(v.kind==PartKind::Bootloader || v.kind==PartKind::Application){set16(v.data,15,minimum);set16(v.data,17,maximum);v.data.resize(v.data.size()-32);auto sha=Sha256(v.data);v.data.insert(v.data.end(),sha.begin(),sha.end());}
}
static unsigned tests=0;
static void check(bool value){assert(value);tests++;}
static void crypto(){
    Bytes abc={'a','b','c'};auto md5=Md5(abc);auto sha=Sha256(abc);
    check(detail::hex(md5.data(),16)=="900150983cd24fb0d6963f7d28e17f72");check(detail::hex(sha.data(),32)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto m0=Md5(Bytes());auto s0=Sha256(Bytes());check(detail::hex(m0.data(),16)=="d41d8cd98f00b204e9800998ecf8427e");check(detail::hex(s0.data(),32)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Bytes raw={0,0xc0,0xdb,0xff};check(SlipEncode(raw)==Bytes({0xc0,0,0xdb,0xdc,0xdb,0xdd,0xff,0xc0}));check(SlipDecode(SlipEncode(raw))==raw);
    for(const Bytes& b:std::vector<Bytes>{{},{0xc0},{0xc0,0xdb,0xc0},{0xc0,0xdb,1,0xc0},{0xc0,1,0xc0,2,0xc0}}){bool fail=false;try{SlipDecode(b);}catch(const detail::Failure& e){fail=e.code==Error::BrokenFrame;}check(fail);}
}
static void preflight(){
    auto p=plan();check(ValidatePlan(p).ok());
    for(unsigned i=0;i<4;i++){auto bad=p;bad.erase(bad.begin()+i);Mock m;auto r=Run(m,bad);check(r.error==Error::InvalidPlan && m.resets==0 && m.ops.empty());}
    {auto b=p;b[2].data[0]=0;check(ValidatePlan(b).error==Error::BadImage);}
    {auto b=p;b[2].offset=0x9000;check(ValidatePlan(b).error==Error::InvalidPlan);}
    {auto b=p;b[2].data.resize(4096);check(ValidatePlan(b).error==Error::InvalidPlan);}
    {auto b=p;b[0].offset=0;check(ValidatePlan(b).error==Error::InvalidPlan);}
    {auto b=p;b[3].data[40]^=1;check(ValidatePlan(b).error==Error::Checksum);}
    {auto b=p;b[1].data[176]^=1;check(ValidatePlan(b).error==Error::Checksum);}
    {auto b=p;b[3].data[12]=13;check(ValidatePlan(b).error==Error::BadImage);}
    {auto b=p;b[0].data.resize(0x7000);check(ValidatePlan(b).error==Error::InvalidPlan);}
    {auto b=p;b[3].offset=0xfff000;check(ValidatePlan(b).error==Error::InvalidPlan);}
    {auto b=p;b.push_back(b.front());check(ValidatePlan(b).error==Error::InvalidPlan);}
    {Options o;o.capacity=8*1024*1024;check(ValidatePlan(p,o).error==Error::InvalidPlan);}
}
static void identity(){
    auto p=plan();
    {Mock m;m.chip=13;auto r=Run(m,p);check(r.error==Error::WrongChip && !r.writesStarted && m.ops==std::vector<unsigned>({8,0x14}));}
    {Mock m;m.revision=200;auto r=Run(m,p);check(r.error==Error::WrongRevision && !r.writesStarted && m.begins.empty());}
    // Actual v1 artifacts target silicon 100..199: rev300 must stop before
    // SPI attach, register writes, erase, DATA or OtaData modification.
    {Mock m;m.revision=300;Bytes oldOta(m.flash.begin()+0xf000,m.flash.begin()+0x11000);
        auto r=Run(m,p);check(r.error==Error::WrongRevision && !r.writesStarted && !r.ended && m.begins.empty() && m.dataCount==0 && m.registerWrites==0 && m.ends==0);
        check(m.ops==std::vector<unsigned>({8,0x14,0x0a}) && std::equal(oldOta.begin(),oldOta.end(),m.flash.begin()+0xf000));}
    // Make ONLY one header incompatible at a time, keeping the other compatible
    // and both SHA256 digests correct. Each header independently gates writes.
    for(PartKind incompatible:{PartKind::Bootloader,PartKind::Application}){
        auto q=p;revisionRange(q,100,399);
        for(auto& v:q)if(v.kind==incompatible){set16(v.data,17,199);v.data.resize(v.data.size()-32);auto hash=Sha256(v.data);v.data.insert(v.data.end(),hash.begin(),hash.end());}
        Mock m;m.revision=300;auto r=Run(m,q);
        check(r.error==Error::WrongRevision && !r.writesStarted && m.ops==std::vector<unsigned>({8,0x14,0x0a}) && m.begins.empty() && m.dataCount==0 && m.registerWrites==0 && m.ends==0);
    }
    {Mock m;m.flags=1;auto r=Run(m,p);check(r.error==Error::Security && !r.writesStarted);}
    {Mock m;m.flags=4;check(Run(m,p).error==Error::Security);}
    {Mock m;m.crypt=1;check(Run(m,p).error==Error::Security);}
    {Mock m;m.stub=true;check(Run(m,p).error==Error::Security && m.ops.size()==1);}
    {Mock m;m.shortSecurity=true;check(Run(m,p).error==Error::BrokenFrame && !m.ends);}
    {Mock m;m.badHeader=true;check(Run(m,p).error==Error::BrokenFrame && !m.ends);}
    {auto q=p;revisionRange(q,300,399);Mock m;m.revision=301;auto r=Run(m,q);check(r.error==Error::UnsupportedRevision && m.begins.empty() && !m.registerWrites);}
    {Mock m;m.silent=true;check(Run(m,p).error==Error::Timeout && !m.ends);}
}
static void success(){
    auto p=plan();Mock m;m.writeChunk=7;m.readChunk=1;Bytes nvs(m.flash.begin()+0x9000,m.flash.begin()+0xf000);check(m.bootSelected()==1);
    unsigned verified=0;auto r=Run(m,p,Options(),[&](const Progress& v){if(v.verified)verified++;});
    check(r.ok() && r.verified && r.ended && m.ends==1 && m.lastEnd==0 && verified==4);
    check(r.chipId==18 && r.revision==100 && r.apiVersion==999);
    check(m.begins==std::vector<std::uint32_t>({0x20000,0x2000,0x8000,0xf000}));
    check(std::equal(nvs.begin(),nvs.end(),m.flash.begin()+0x9000) && m.bootSelected()==0);
    check(m.flash[0x11000]==0xa5 && m.flash[0x620000]==0xa5);
    std::uint64_t size=0;for(const auto& v:p){size+=v.data.size();check(std::equal(v.data.begin(),v.data.end(),m.flash.begin()+v.offset));}check(r.verifiedBytes==size);
    {Mock t;Options o;o.reboot=false;check(Run(t,p,o).ok() && t.lastEnd==1);}
    {Mock t;t.uart=6;check(Run(t,p).ok() && t.registerWrites==6);}
    {Mock t;t.revision=101;check(Run(t,p).ok());}
}
static void faults(){
    auto p=plan();
    {Mock m;m.timeoutData=1;auto r=Run(m,p);check(r.ok() && m.sequences.size()>1 && m.sequences[0]==0 && m.sequences[1]==0);}
    {Mock m;m.checksumData=1;check(Run(m,p).ok() && m.sequences[0]==m.sequences[1]);}
    {Mock m;m.brokenData=1;check(Run(m,p).ok() && m.sequences[0]==m.sequences[1]);}
    {Mock m;m.timeoutData=10;auto r=Run(m,p);check(r.error==Error::Timeout && r.recoveryOffset==0x20000 && !r.ended && !m.ends && m.bootSelected()==1);}
    {Mock m;m.checksumData=10;auto r=Run(m,p);check(r.error==Error::Checksum && !m.ends);}
    {Mock m;m.corruptMd5=1;auto r=Run(m,p);check(r.error==Error::Verify && !r.ended && !m.ends && m.begins.size()==1 && m.bootSelected()==1);}
    {Mock m;m.disconnectData=2;auto r=Run(m,p);check(r.error==Error::Transport && !r.ended && !m.ends && m.bootSelected()==1);
        m.disconnectData=0;check(Run(m,p).ok() && m.bootSelected()==0 && m.ends==1);}
    {Mock m;auto r=Run(m,p,Options(),ProgressFn(),[]{return true;});check(r.error==Error::Cancelled && m.resets==0);}
    for(unsigned at=1;at<=4;at++){
        Mock m;m.corruptMd5At=at;auto r=Run(m,p);check(r.error==Error::Verify && !r.ended && !m.ends);
    }
    for(unsigned at=1;at<=4;at++){
        Mock m;Bytes nvs(m.flash.begin()+0x9000,m.flash.begin()+0xf000);m.disconnectBegin=at;
        auto r=Run(m,p);check(r.error==Error::Transport && !m.ends && r.writesStarted);
        m.disconnectBegin=0;check(Run(m,p).ok() && m.bootSelected()==0 && std::equal(nvs.begin(),nvs.end(),m.flash.begin()+0x9000));
    }
    for(unsigned at=1;at<=13;at++){
        Mock m;m.disconnectData=at;auto r=Run(m,p);check(r.error==Error::Transport && !m.ends);
        m.disconnectData=0;check(Run(m,p).ok() && m.bootSelected()==0);
    }
    {Mock m;bool cancel=false;auto r=Run(m,p,Options(),[&](const Progress& v){if(v.verified)cancel=true;},[&]{return cancel;});check(r.error==Error::Cancelled && !m.ends && m.bootSelected()==1);}
}
static Bytes loadActual(const std::string& path){
    std::ifstream in(path.c_str(),std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("Cannot open package file: "+path);
    auto size=in.tellg();if(size<=0 || size>16*1024*1024)throw std::runtime_error("Invalid package file size: "+path);
    Bytes b(static_cast<std::size_t>(size));in.seekg(0);if(!in.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size())))throw std::runtime_error("Cannot read package file: "+path);return b;
}
static int actualPackage(const std::string& root,unsigned deviceRevision){
    std::vector<Part> p={part(PartKind::Bootloader,0x2000,loadActual(root+"/bootloader.bin")),
        part(PartKind::Partitions,0x8000,loadActual(root+"/partition-table.bin")),
        part(PartKind::OtaData,0xf000,loadActual(root+"/ota_data_initial.bin")),
        part(PartKind::Application,0x20000,loadActual(root+"/krenomer-esp32.bin"))};
    Result validated=ValidatePlan(p);if(!validated.ok())throw std::runtime_error("Actual package preflight failed: "+validated.message);
    bool compatible=true;
    for(const auto& v:p)if(v.kind==PartKind::Bootloader || v.kind==PartKind::Application){
        unsigned lo=detail::u16(v.data,15),hi=detail::u16(v.data,17);if(!lo)lo=unsigned(v.data[14])*100;
        std::cout<<(v.kind==PartKind::Bootloader?"bootloader":"application")<<" bytes="<<v.data.size()<<" min="<<lo<<" max="<<hi<<"\n";
        compatible=compatible && deviceRevision>=lo && (!hi || deviceRevision<=hi);
    }
    Mock m;m.revision=deviceRevision;Bytes nvs(m.flash.begin()+0x9000,m.flash.begin()+0xf000);
    std::uint64_t verified=0;auto r=Run(m,p,Options(),[&](const Progress& v){if(v.verified)verified++;});
    check(std::equal(nvs.begin(),nvs.end(),m.flash.begin()+0x9000));
    if(compatible){
        if(!r.ok())throw std::runtime_error("Compatible actual package ROM mock failed: "+r.message);
        check(r.verified && r.ended && m.ends==1 && verified==4 && m.bootSelected()==0);
        check(m.begins==std::vector<std::uint32_t>({0x20000,0x2000,0x8000,0xf000}));
        std::uint64_t bytes=0;for(const auto& v:p){bytes+=v.data.size();check(std::equal(v.data.begin(),v.data.end(),m.flash.begin()+v.offset));}check(r.verifiedBytes==bytes);
        std::cout<<"ACTUAL PACKAGE ROM MOCK PASS device_rev="<<deviceRevision<<" compatible=true parts_verified="<<verified<<" bytes_verified="<<r.verifiedBytes<<" flash_data_packets="<<m.dataCount<<" NVS=preserved ota_select=0 END=1\n";
    }else{
        check(r.error==Error::WrongRevision && !r.writesStarted && !r.ended && !r.verified);
        check(m.begins.empty() && m.dataCount==0 && m.registerWrites==0 && m.ends==0 && m.bootSelected()==1);
        check(m.ops==std::vector<unsigned>({8,0x14,0x0a}));
        std::cout<<"ACTUAL PACKAGE ROM MOCK PASS device_rev="<<deviceRevision<<" compatible=false error=WrongRevision erase=0 DATA=0 register_writes=0 END=0 NVS=preserved ota_select=1\n";
    }
    std::cout<<"Actual-package checks: "<<tests<<" (mock transport only; no physical device)\n";return 0;
}
int main(int argc,char** argv){
    try{
        if(argc==4 && std::string(argv[1])=="--package"){
            char* end=nullptr;unsigned long revision=std::strtoul(argv[3],&end,10);
            if(!argv[3][0] || *end || revision>715 || revision%100>15)throw std::runtime_error("Invalid silicon revision (major*100+minor, minor 0..15)");
            return actualPackage(argv[2],static_cast<unsigned>(revision));
        }
        if(argc!=1)throw std::runtime_error("Usage: esprom_test [--package ACTUAL_FOLDER DEVICE_REVISION]");
        crypto();preflight();identity();success();faults();std::cout<<"ESP32-P4 ROM mock tests: PASS ("<<tests<<" checks)\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}return 0;
}
