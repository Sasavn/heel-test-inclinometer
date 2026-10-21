#pragma once
// Native C++11 ESP32-P4 ROM recovery; no OS calls, port discovery, Python or stub.
// Protocol checked against bundled official esptool loader.py and esp32p4.py.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace fw { namespace esprom {
using Bytes=std::vector<std::uint8_t>;
enum class ResetMode { EnterBootloader, RunApplication };
struct ITransport {
    virtual ~ITransport() {}
    // Adapter opens ONLY user-selected port at 115200 8N1 before Run().
    // Return bytes transferred (may be partial), zero on timeout, throw on I/O.
    // Each call must honor timeoutMs. reset(EnterBootloader) purges RX too.
    virtual std::size_t write(const std::uint8_t*,std::size_t,unsigned timeoutMs)=0;
    virtual std::size_t read(std::uint8_t*,std::size_t,unsigned timeoutMs)=0;
    virtual bool reset(ResetMode,unsigned timeoutMs)=0;
};
enum class PartKind { Bootloader, Partitions, Application, OtaData };
struct Part { PartKind kind=PartKind::Application;std::uint32_t offset=0;Bytes data;std::string name; };
struct Options {
    std::uint32_t capacity=16u*1024*1024;
    unsigned minRevision=100,maxRevision=65535;
    unsigned commandTimeoutMs=3000,eraseTimeoutMs=120000,resetTimeoutMs=5000;
    unsigned dataRetries=2; // retries same DATA sequence, never advances offset
    bool reboot=true;
};
enum class Error { None, InvalidPlan, BadImage, WrongChip, WrongRevision,
    Security, UnsupportedRevision, Timeout, BrokenFrame, Checksum, RomStatus, Transport, Verify, Cancelled };
struct Result {
    Error error=Error::None;std::string message;
    std::uint32_t chipId=0,securityFlags=0,apiVersion=0;
    unsigned revision=0;
    std::uint64_t verifiedBytes=0;
    std::uint32_t recoveryOffset=0; // failed part start; retry whole vetted plan
    bool writesStarted=false,verified=false,ended=false;
    bool ok()const{return error==Error::None;}
};
struct Progress {std::uint32_t partOffset=0,acknowledgedBytes=0,totalBytes=0;bool verified=false;};
using ProgressFn=std::function<void(const Progress&)>;
using CancelFn=std::function<bool()>;
// Explicit factory_parts list loaded and authenticated by caller. Exactly four
// parts: bootloader@0x2000, partition-table@0x8000, ota_0 app@0x20000.
// Plus OtaData@0xf000, exactly 8192 FF bytes, written LAST after app verification.
// No NVS/full-flash writes. Engine rechecks erase footprints, image chip,
// XOR+SHA256, partition MD5/layout, ROM chip/revision/security before any erase.
// Failure never sends END or RunApplication reset. Recovery reruns whole plan;
// no offset resume is safe after a lost connection. Synchronous: call in worker.
Result ValidatePlan(const std::vector<Part>&,const Options& options=Options());
Result Run(ITransport&,const std::vector<Part>&,const Options& options=Options(),
           ProgressFn progress=ProgressFn(),CancelFn cancel=CancelFn());
Bytes SlipEncode(const Bytes&);
Bytes SlipDecode(const Bytes&);
std::array<std::uint8_t,16> Md5(const Bytes&);
std::array<std::uint8_t,32> Sha256(const Bytes&);
namespace detail {
inline std::uint16_t u16(const Bytes& b,std::size_t p){return std::uint16_t(b.at(p))|(std::uint16_t(b.at(p+1))<<8);}
inline std::uint32_t u32(const Bytes& b,std::size_t p){return std::uint32_t(b.at(p))|(std::uint32_t(b.at(p+1))<<8)|(std::uint32_t(b.at(p+2))<<16)|(std::uint32_t(b.at(p+3))<<24);}
inline void put(Bytes& b,std::uint32_t v){for(unsigned i=0;i<4;i++)b.push_back(std::uint8_t(v>>(i*8)));}
inline std::uint32_t rol(std::uint32_t v,unsigned n){return (v<<n)|(v>>(32-n));}
inline std::uint32_t ror(std::uint32_t v,unsigned n){return (v>>n)|(v<<(32-n));}
inline Bytes padded(const Bytes& in,bool big){Bytes b=in;std::uint64_t bits=std::uint64_t(b.size())*8;b.push_back(0x80);while(b.size()%64!=56)b.push_back(0);for(unsigned i=0;i<8;i++)b.push_back(std::uint8_t(bits>>((big?7-i:i)*8)));return b;}
inline std::string hex(const std::uint8_t* p,std::size_t n){static const char* h="0123456789abcdef";std::string s;for(std::size_t i=0;i<n;i++){s+=h[p[i]>>4];s+=h[p[i]&15];}return s;}
struct Failure:std::runtime_error {Error code;Failure(Error c,const std::string& s):std::runtime_error(s),code(c){}};
inline void require(bool v,Error e,const char* why){if(!v)throw Failure(e,why);}
}
inline std::array<std::uint8_t,16> Md5(const Bytes& in){
    static const std::uint32_t k[64]={0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const unsigned shift[16]={7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21};
    Bytes b=detail::padded(in,false);std::uint32_t h[4]={0x67452301,0xefcdab89,0x98badcfe,0x10325476};
    for(std::size_t p=0;p<b.size();p+=64){std::uint32_t a=h[0],v=h[1],c=h[2],d=h[3];for(unsigned i=0;i<64;i++){
        std::uint32_t f;unsigned g;
        if(i<16){f=(v&c)|(~v&d);g=i;}else if(i<32){f=(d&v)|(~d&c);g=(5*i+1)%16;}else if(i<48){f=v^c^d;g=(3*i+5)%16;}else{f=c^(v|~d);g=(7*i)%16;}
        std::uint32_t t=d;d=c;c=v;v+=detail::rol(a+f+k[i]+detail::u32(b,p+g*4),shift[(i/16)*4+i%4]);a=t;
    }h[0]+=a;h[1]+=v;h[2]+=c;h[3]+=d;}
    std::array<std::uint8_t,16> out{};for(unsigned i=0;i<16;i++)out[i]=std::uint8_t(h[i/4]>>(8*(i%4)));return out;
}
inline std::array<std::uint8_t,32> Sha256(const Bytes& in){
    static const std::uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    Bytes b=detail::padded(in,true);std::uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for(std::size_t p=0;p<b.size();p+=64){std::uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=(std::uint32_t(b[p+i*4])<<24)|(std::uint32_t(b[p+i*4+1])<<16)|(std::uint32_t(b[p+i*4+2])<<8)|b[p+i*4+3];
        for(unsigned i=16;i<64;i++){std::uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(detail::ror(x,7)^detail::ror(x,18)^(x>>3))+w[i-7]+(detail::ror(y,17)^detail::ror(y,19)^(y>>10));}
        std::uint32_t a=h[0],v=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for(unsigned i=0;i<64;i++){std::uint32_t t=z+(detail::ror(e,6)^detail::ror(e,11)^detail::ror(e,25))+((e&f)^(~e&g))+k[i]+w[i];std::uint32_t u=(detail::ror(a,2)^detail::ror(a,13)^detail::ror(a,22))+((a&v)^(a&c)^(v&c));z=g;g=f;f=e;e=d+t;d=c;c=v;v=a;a=t+u;}
        h[0]+=a;h[1]+=v;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    }std::array<std::uint8_t,32> out{};for(unsigned i=0;i<32;i++)out[i]=std::uint8_t(h[i/4]>>(24-8*(i%4)));return out;
}
inline Bytes SlipEncode(const Bytes& b){Bytes o;o.reserve(b.size()+2);o.push_back(0xc0);for(auto v:b){if(v==0xc0){o.push_back(0xdb);o.push_back(0xdc);}else if(v==0xdb){o.push_back(0xdb);o.push_back(0xdd);}else o.push_back(v);}o.push_back(0xc0);return o;}
inline Bytes SlipDecode(const Bytes& b){
    detail::require(b.size()>=2 && b.front()==0xc0 && b.back()==0xc0,Error::BrokenFrame,"Missing SLIP delimiters");Bytes o;
    for(std::size_t i=1;i+1<b.size();i++){std::uint8_t v=b[i];if(v==0xdb){detail::require(i+2<b.size(),Error::BrokenFrame,"Truncated SLIP escape");v=b[++i];detail::require(v==0xdc||v==0xdd,Error::BrokenFrame,"Invalid SLIP escape");v=v==0xdc?0xc0:0xdb;}else detail::require(v!=0xc0,Error::BrokenFrame,"Unexpected SLIP delimiter");o.push_back(v);}return o;
}
namespace detail {
inline std::uint32_t roundSector(std::size_t n){return std::uint32_t((n+4095)&~std::size_t(4095));}
inline void checkImage(const Bytes& b,bool app,unsigned revision=0){
    require(b.size()>=48 && b.size()<=0x600000,Error::BadImage,"Image size outside limits");
    require(b[0]==0xe9 && b[1]>0 && b[1]<=16 && u16(b,12)==18,Error::BadImage,"Expected ESP32-P4 image");
    require(b[23]==1,Error::BadImage,"SHA256-appended image required");
    std::size_t p=24;std::uint8_t sum=0xef;
    for(unsigned i=0;i<b[1];i++){require(p+8<=b.size(),Error::BadImage,"Segment header truncated");std::uint32_t len=u32(b,p+4);p+=8;require(len<=b.size()-p && len%4==0,Error::BadImage,"Segment truncated or unaligned");for(std::size_t j=0;j<len;j++)sum^=b[p+j];p+=len;}
    std::size_t end=(p+16)&~std::size_t(15);require(end<=b.size() && b.size()==end+32,Error::BadImage,"Image trailer size mismatch");require(b[end-1]==sum,Error::Checksum,"Image segment XOR mismatch");
    auto hash=Sha256(Bytes(b.begin(),b.begin()+end));require(std::equal(hash.begin(),hash.end(),b.begin()+end),Error::Checksum,"Image SHA256 mismatch");
    if(app){require(b.size()>=288 && u32(b,28)>=256 && u32(b,32)==0xabcd5432,Error::BadImage,"Application descriptor missing");
        const char* expected="krenomer-esp32";require(std::equal(expected,expected+14,b.begin()+80) && b[94]==0,Error::BadImage,"Wrong application project");}
    unsigned minimum=u16(b,15);if(!minimum)minimum=unsigned(b[14])*100;
    unsigned maximum=u16(b,17);require(!maximum || maximum>=minimum,Error::BadImage,"Invalid image revision interval");
    if(revision){require(revision>=minimum && (!maximum||revision<=maximum),Error::WrongRevision,"Image incompatible with silicon revision");}
}
inline void validate(const std::vector<Part>& parts,const Options& o){
    require(parts.size()==4,Error::InvalidPlan,"Exactly bootloader, partitions, app and OtaData required");
    require(o.capacity==16u*1024*1024 && o.minRevision<=o.maxRevision && o.commandTimeoutMs>0 && o.eraseTimeoutMs>0 && o.resetTimeoutMs>0 && o.dataRetries<=5,Error::InvalidPlan,"Invalid Tab5 capacity/options");
    const Part *boot=nullptr,*table=nullptr,*app=nullptr,*ota=nullptr;
    for(const auto& p:parts){
        const Part** slot=nullptr;
        switch(p.kind){case PartKind::Bootloader:slot=&boot;break;case PartKind::Partitions:slot=&table;break;case PartKind::Application:slot=&app;break;case PartKind::OtaData:slot=&ota;break;default:throw Failure(Error::InvalidPlan,"Unknown part kind");}
        require(!*slot,Error::InvalidPlan,"Duplicate part kind");*slot=&p;
        require(!p.data.empty() && p.data.size()<=o.capacity && p.offset%4096==0 && std::uint64_t(p.offset)+roundSector(p.data.size())<=o.capacity,Error::InvalidPlan,"Part outside flash or unaligned");
    }
    require(boot&&table&&app&&ota,Error::InvalidPlan,"Missing required part");
    require(boot->offset==0x2000 && boot->data.size()<=0x6000 && table->offset==0x8000 && table->data.size()<=0x1000 && app->offset==0x20000 && ota->offset==0xf000 && ota->data.size()==8192,Error::InvalidPlan,"Unexpected Tab5 offsets/erase footprint");
    require(std::all_of(ota->data.begin(),ota->data.end(),[](std::uint8_t v){return v==0xff;}),Error::BadImage,"OtaData must be exact FF initialization image");
    for(std::size_t i=0;i<parts.size();i++)for(std::size_t j=i+1;j<parts.size();j++)require(std::uint64_t(parts[i].offset)+roundSector(parts[i].data.size())<=parts[j].offset || std::uint64_t(parts[j].offset)+roundSector(parts[j].data.size())<=parts[i].offset,Error::InvalidPlan,"Erase footprints overlap");
    checkImage(boot->data,false);checkImage(app->data,true);
    // Fixed current Tab5 layout: changing partition layout is a separate migration,
    // not recovery. Validate every record so no manifest can relabel NVS as app.
    const Bytes& b=table->data;require(b.size()%32==0,Error::BadImage,"Partition table length");
    const std::uint32_t offsets[5]={0x9000,0xf000,0x11000,0x20000,0x620000};
    const std::uint32_t sizes[5]={0x6000,0x2000,0x1000,0x600000,0x600000};
    const std::uint8_t types[5]={1,1,1,0,0},subtypes[5]={2,0,1,0x10,0x11};
    const char* names[5]={"nvs","otadata","phy_init","ota_0","ota_1"};
    require(b.size()>=192,Error::BadImage,"Partition table truncated");
    for(unsigned i=0;i<5;i++){std::size_t p=i*32;require(u16(b,p)==0x50aa && b[p+2]==types[i] && b[p+3]==subtypes[i] && u32(b,p+4)==offsets[i] && u32(b,p+8)==sizes[i] && u32(b,p+28)==0,Error::BadImage,"Partition layout/flags mismatch");
        std::size_t len=std::strlen(names[i]);require(std::equal(names[i],names[i]+len,b.begin()+p+12)&&b[p+12+len]==0,Error::BadImage,"Partition label mismatch");}
    require(u16(b,160)==0xebeb && std::all_of(b.begin()+162,b.begin()+176,[](std::uint8_t v){return v==0xff;}),Error::BadImage,"Partition MD5 record missing");
    auto hash=Md5(Bytes(b.begin(),b.begin()+160));require(std::equal(hash.begin(),hash.end(),b.begin()+176),Error::Checksum,"Partition table MD5 mismatch");
    require(std::all_of(b.begin()+192,b.end(),[](std::uint8_t v){return v==0xff;}),Error::BadImage,"Unexpected partition records/trailer");
    require(roundSector(app->data.size())<=sizes[3],Error::InvalidPlan,"Application exceeds ota_0");
}
}
inline Result ValidatePlan(const std::vector<Part>& parts,const Options& o){Result r;try{detail::validate(parts,o);}catch(const detail::Failure& e){r.error=e.code;r.message=e.what();}catch(const std::exception& e){r.error=Error::InvalidPlan;r.message=e.what();}return r;}
namespace detail {
class Engine {
    ITransport& io;const Options& options;ProgressFn progress;CancelFn cancel;
    using Clock=std::chrono::steady_clock;
    struct Reply{std::uint32_t value=0;Bytes data;};
    void cancelled(){if(cancel && cancel())throw Failure(Error::Cancelled,"Cancelled; rerun complete recovery plan in ROM mode");}
    unsigned remaining(Clock::time_point until){auto n=std::chrono::duration_cast<std::chrono::milliseconds>(until-Clock::now()).count();require(n>0,Error::Timeout,"ROM command deadline expired");return unsigned(n);}
    void writeAll(const Bytes& b,unsigned timeout){auto until=Clock::now()+std::chrono::milliseconds(timeout);std::size_t p=0;while(p<b.size()){cancelled();auto n=io.write(b.data()+p,b.size()-p,remaining(until));require(n>0,Error::Timeout,"ROM write timed out");require(n<=b.size()-p,Error::Transport,"Transport write count invalid");p+=n;}}
    Bytes frame(Clock::time_point until){Bytes b;bool started=false;unsigned noise=0;for(;;){cancelled();std::uint8_t v=0;auto n=io.read(&v,1,remaining(until));require(n==1, n==0?Error::Timeout:Error::Transport,"ROM read timeout/invalid count");
        if(!started){if(v==0xc0){started=true;b.push_back(v);}else require(++noise<=256,Error::BrokenFrame,"Too much non-SLIP boot noise");continue;}
        if(v==0xc0){if(b.size()==1)continue;b.push_back(v);return SlipDecode(b);}b.push_back(v);require(b.size()<=16384,Error::BrokenFrame,"ROM frame exceeds size limit");}}
    Reply response(std::uint8_t op,Clock::time_point until){
        for(unsigned ignored=0;ignored<16;ignored++){Bytes b=frame(until);require(b.size()>=12 && b[0]==1 && u16(b,2)==b.size()-8,Error::BrokenFrame,"Invalid ROM response header/length");
            if(b[1]!=op){require(b[1]==8 && op!=8,Error::BrokenFrame,"Unexpected ROM opcode");continue;}
            Reply r;r.value=u32(b,4);r.data.assign(b.begin()+8,b.end());return r;}
        throw Failure(Error::BrokenFrame,"Excess unsolicited ROM responses");
    }
    Reply command(std::uint8_t op,const Bytes& data,std::uint32_t checksum=0,unsigned timeout=0){
        cancelled();if(!timeout)timeout=options.commandTimeoutMs;
        require(data.size()<=65535,Error::BrokenFrame,"ROM request too large");Bytes b={0,op,std::uint8_t(data.size()),std::uint8_t(data.size()>>8)};put(b,checksum);b.insert(b.end(),data.begin(),data.end());
        auto until=Clock::now()+std::chrono::milliseconds(timeout);writeAll(SlipEncode(b),remaining(until));return response(op,until);
    }
    void status(const Reply& r,std::size_t expected){
        require(r.data.size()==expected+4,Error::BrokenFrame,"ROM response payload/status length mismatch");
        if(r.data[expected]){Error code=r.data[expected+1]==7?Error::Checksum:Error::RomStatus;throw Failure(code,"ROM rejected command; status="+std::to_string(r.data[expected])+" reason="+std::to_string(r.data[expected+1]));}
    }
    void drain(){std::uint8_t b[256];for(unsigned i=0;i<64;i++){auto n=io.read(b,sizeof(b),1);require(n<=sizeof(b),Error::Transport,"Invalid drain count");if(!n)return;}throw Failure(Error::BrokenFrame,"Cannot drain ROM transport");}
    Bytes words(std::initializer_list<std::uint32_t> values){Bytes b;for(auto v:values)put(b,v);return b;}
public:
    Engine(ITransport& t,const Options& o,ProgressFn p,CancelFn c):io(t),options(o),progress(p),cancel(c){}
    void execute(const std::vector<Part>& parts,Result& result){
        cancelled();require(io.reset(ResetMode::EnterBootloader,options.resetTimeoutMs),Error::Transport,"Cannot enter ROM bootloader on selected port");
        Bytes sync={7,7,0x12,0x20};sync.insert(sync.end(),32,0x55);
        Reply first=command(8,sync);status(first,0);bool rom=first.value!=0;
        auto until=Clock::now()+std::chrono::milliseconds(options.commandTimeoutMs);
        for(unsigned i=0;i<7;i++){Reply r=response(8,until);status(r,0);rom=rom||r.value!=0;}
        require(rom,Error::Security,"Stub detected: reset into actual ROM, no stub supported");
        Reply security=command(0x14,Bytes());status(security,20);
        result.chipId=u32(security.data,12);result.securityFlags=u32(security.data,0);result.apiVersion=u32(security.data,16);
        require(result.chipId==18,Error::WrongChip,"Selected ROM is not ESP32-P4 chip ID 18");
        require(!(result.securityFlags&5) && security.data[4]==0,Error::Security,"Secure boot/download or flash encryption unsupported; no writes performed");
        Reply efuse=command(0x0a,words({0x5012d04c}));status(efuse,0);std::uint32_t v=efuse.value;
        result.revision=unsigned((((v>>23)&1)<<2)|((v>>4)&3))*100+unsigned(v&15);
        require(result.revision>=options.minRevision && result.revision<=options.maxRevision,Error::WrongRevision,"Silicon outside caller revision interval");
        for(const auto& p:parts)if(p.kind==PartKind::Bootloader || p.kind==PartKind::Application)checkImage(p.data,p.kind==PartKind::Application,result.revision);
        require(result.revision!=301 && result.revision!=302,Error::UnsupportedRevision,"P4 ECO6/ECO7 flash power sequencing not implemented; no writes performed");
        // Native USB Serial/JTAG ROM leaves RTC watchdogs active. Read console
        // selector only AFTER chip+revision+security checks, then use the P4
        // watchdog sequence from the target specification (no eFuse writes).
        Reply console=command(0x0a,words({result.revision<300?0x4ff3fec8u:0x4ffbfec8u}));status(console,0);
        if(console.value==6){
            auto wr=[&](std::uint32_t address,std::uint32_t value){status(command(9,words({address,value,0xffffffffu,0})),0);};
            wr(0x50116018,0x50d83aa1);wr(0x50116000,0);wr(0x50116018,0);
            wr(0x50116020,0x50d83aa1);
            Reply swd=command(0x0a,words({0x5011601c}));status(swd,0);
            wr(0x5011601c,swd.value|(1u<<18));wr(0x50116020,0);
        }
        status(command(0x0d,words({0,0})),0);
        status(command(0x0b,words({0,options.capacity,65536,4096,256,0xffff})),0);
        // Application verified before any boot-selection reset. Never erase NVS,
        // phy_init or ota_1; partial failure stays in ROM with no automatic reboot.
        const PartKind order[]={PartKind::Application,PartKind::Bootloader,PartKind::Partitions,PartKind::OtaData};
        for(auto kind:order){const Part* part=nullptr;for(const auto& p:parts)if(p.kind==kind)part=&p;require(part!=nullptr,Error::InvalidPlan,"Missing part");const Part& p=*part;
            result.recoveryOffset=p.offset;cancelled();std::uint32_t blocks=std::uint32_t((p.data.size()+1023)/1024);
            result.writesStarted=true;status(command(2,words({roundSector(p.data.size()),blocks,1024,p.offset,0}),0,options.eraseTimeoutMs),0);
            for(std::uint32_t seq=0;seq<blocks;seq++){
                Bytes block(1024,0xff);std::size_t pos=std::size_t(seq)*1024,len=std::min<std::size_t>(1024,p.data.size()-pos);std::copy(p.data.begin()+pos,p.data.begin()+pos+len,block.begin());
                std::uint32_t checksum=0xef;for(auto b:block)checksum^=b;
                Bytes data=words({1024,seq,0,0});data.insert(data.end(),block.begin(),block.end());
                for(unsigned attempt=0;;attempt++){
                    try{status(command(3,data,checksum),0);break;}catch(const Failure& f){
                        if(attempt>=options.dataRetries || (f.code!=Error::Timeout && f.code!=Error::BrokenFrame && f.code!=Error::Checksum))throw;
                        cancelled();drain(); // retry EXACT same sequence/data, never guessed offset
                    }
                }
                if(progress){Progress update;update.partOffset=p.offset;update.acknowledgedBytes=std::uint32_t(pos+len);update.totalBytes=std::uint32_t(p.data.size());progress(update);}
            }
            Reply digest=command(0x13,words({p.offset,std::uint32_t(p.data.size()),0,0}),0,options.eraseTimeoutMs);status(digest,32);
            auto local=Md5(p.data);std::string remote(digest.data.begin(),digest.data.begin()+32);
            for(char& ch:remote)if(ch>='A'&&ch<='F')ch=char(ch-'A'+'a');
            require(remote==hex(local.data(),local.size()),Error::Verify,"Flash MD5 mismatch; remain in ROM and retry whole plan");
            result.verifiedBytes+=p.data.size();if(progress){Progress update;update.partOffset=p.offset;update.acknowledgedBytes=update.totalBytes=std::uint32_t(p.data.size());update.verified=true;progress(update);}
        }
        result.verified=true;cancelled();status(command(4,words({options.reboot?0u:1u})),0);result.ended=true;result.recoveryOffset=0;
    }
};
}
inline Result Run(ITransport& transport,const std::vector<Part>& parts,const Options& o,ProgressFn progress,CancelFn cancel){
    Result result=ValidatePlan(parts,o);if(!result.ok())return result;
    try{detail::Engine engine(transport,o,progress,cancel);engine.execute(parts,result);result.message="All four parts ROM-MD5 verified; NVS preserved";}
    catch(const detail::Failure& e){result.error=e.code;result.message=e.what();}
    catch(const std::exception& e){result.error=Error::Transport;result.message=e.what();}
    catch(...){result.error=Error::Transport;result.message="Transport/callback failure";}
    return result;
}
}} // namespace fw::esprom
