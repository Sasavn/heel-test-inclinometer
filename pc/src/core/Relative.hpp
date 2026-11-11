#pragma once
// Relative inclinometer data. A pair is a device publication, never a GUI redraw.
// The two heads are not hardware-synchronised; receive provenance is retained.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include "Json.hpp"

namespace level
{
inline constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr std::uint64_t MaxAgeUs = 500000, MaxSkewUs = 50000;
enum Quality : unsigned { Missing=1, Stale=2, Skew=4, Axes=8, Warmup=16, Range=32, Error=64 };

struct Status
{
    std::string mode;
    int reference=2, object=3;
    bool ready=false, axesConfirmed=false, offsetValid=false;
    unsigned quality=Missing;
    std::array<double,2> ref{NaN,NaN}, obj{NaN,NaN}, raw{NaN,NaN}, offset{0,0}, value{NaN,NaN};
    std::array<std::uint64_t,2> time{}, sequence{}, age{};
    std::uint64_t skew=0, generation=0;
    std::uint64_t maxAge=MaxAgeUs, maxSkew=MaxSkewUs;
    bool rolesValid() const { return (reference==2 || reference==3) && (object==2 || object==3) && reference!=object; }
    bool usable() const
    {
        const auto difference=time[0]>time[1]?time[0]-time[1]:time[1]-time[0];
        const bool basic=mode=="level" && rolesValid() && ready && quality==0 && axesConfirmed &&
            sequence[0]>0 && sequence[1]>0 && maxAge>0 && maxSkew>0 &&
            age[0]<=std::min(maxAge,MaxAgeUs) && age[1]<=std::min(maxAge,MaxAgeUs) &&
            skew<=std::min(maxSkew,MaxSkewUs) && difference==skew &&
            std::isfinite(value[0]) && std::isfinite(value[1]) &&
            std::fabs(value[0])<=360 && std::fabs(value[1])<=360 &&
            std::isfinite(raw[0]) && std::isfinite(raw[1]);
        if(!basic)return false;
        // CSV firmware fields are independently rounded to 0.001 degrees.
        // A small serialization allowance is not a measurement tolerance.
        for(unsigned axis=0;axis<2;axis++)
            if(!std::isfinite(ref[axis]) || !std::isfinite(obj[axis]) ||
               std::fabs(ref[axis])>90 || std::fabs(obj[axis])>90 ||
               std::fabs(raw[axis]-(obj[axis]-ref[axis]))>0.005 ||
               (offsetValid && !std::isfinite(offset[axis])) ||
               std::fabs(value[axis]-(raw[axis]-(offsetValid?offset[axis]:0)))>0.005)return false;
        return true;
    }
};

inline bool ExactUnsigned(const json::Value& v,std::uint64_t& out)
{
    if(!v.IsNumber() || !std::isfinite(v.n) || v.n<0 || v.n>9007199254740991.0 || std::floor(v.n)!=v.n) return false;
    out=static_cast<std::uint64_t>(v.n); return true;
}

inline bool ParseStatus(std::string_view input,Status& out,std::string* error=nullptr)
{
    json::Value j;
    auto fail=[&](const char* reason){if(error)*error=reason; return false;};
    if(!json::Parse(input,j,error) || !j.IsObject()) return false;
    Status s;
    s.mode=j["mode"].Str();
    std::uint64_t reference,object,q;
    if((s.mode!="heel" && s.mode!="level") || !ExactUnsigned(j["reference"],reference) ||
       !ExactUnsigned(j["object"],object) || reference>3 || object>3 ||
       !j["ready"].IsBool() || !ExactUnsigned(j["quality"],q) || q>127) return fail("Неполный ответ relative status");
    s.reference=static_cast<int>(reference); s.object=static_cast<int>(object);
    if(!s.rolesValid()) return fail("Некорректные роли датчиков");
    s.ready=j["ready"].Bool(); s.quality=static_cast<unsigned>(q);
    s.axesConfirmed=j["axes_confirmed"].IsBool() && j["axes_confirmed"].Bool();
    s.offsetValid=j["rel_offset"].IsBool() && j["rel_offset"].Bool();
    auto number=[&](const char* key){const auto& v=j[key];return v.IsNumber() && std::isfinite(v.n)?v.n:NaN;};
    s.ref={number("ref_x"),number("ref_y")}; s.obj={number("obj_x"),number("obj_y")};
    s.raw={number("raw_x"),number("raw_y")}; s.value={number("x"),number("y")};
    s.offset={number("offset_x"),number("offset_y")};
    if(!ExactUnsigned(j["ref_us"],s.time[0]) || !ExactUnsigned(j["obj_us"],s.time[1]) ||
       !ExactUnsigned(j["ref_seq"],s.sequence[0]) || !ExactUnsigned(j["obj_seq"],s.sequence[1]) ||
       !ExactUnsigned(j["skew_us"],s.skew) || !ExactUnsigned(j["generation"],s.generation)) return fail("Нет меток принятой пары");
    // Old status replies did not carry ages/measurements. They remain readable,
    // but cannot pass usable() or be presented as a numeric relative result.
    if(!ExactUnsigned(j["ref_age_us"],s.age[0])) s.age[0]=MaxAgeUs+1;
    if(!ExactUnsigned(j["obj_age_us"],s.age[1])) s.age[1]=MaxAgeUs+1;
    if(j.Has("max_age_us") && !ExactUnsigned(j["max_age_us"],s.maxAge)) return fail("Некорректный предел возраста");
    if(j.Has("max_skew_us") && !ExactUnsigned(j["max_skew_us"],s.maxSkew)) return fail("Некорректный предел пары");
    out=std::move(s); return true;
}

inline std::string QualityText(unsigned q)
{
    if(q==0)return "Пара пригодна";
    std::string out;
    auto add=[&](unsigned bit,const char* message){if(q&bit){if(!out.empty())out+="; ";out+=message;}};
    add(Missing,"нет ответа датчика"); add(Stale,"данные устарели"); add(Skew,"ответы разнесены во времени");
    add(Axes,"подтвердите ориентацию осей"); add(Warmup,"ожидание новых ответов обеих головок");
    add(Range,"вне диапазона"); add(Error,"ошибка датчика");
    return out.empty()?"Неизвестное качество":out;
}

inline double Slope(double degrees)
{
    return std::isfinite(degrees) && std::fabs(degrees)<89.9 ? 1000.0*std::tan(degrees*3.14159265358979323846/180.0):NaN;
}
inline double Height(double degrees,double baselineMm)
{
    return std::isfinite(baselineMm) && baselineMm>0?Slope(degrees)*baselineMm/1000.0:NaN;
}

struct Row
{
    std::uint64_t id=0;
    std::uint64_t eventMs=0;
    double seconds=0;
    Status sample;
    std::string event="SAMPLE";
    std::uint64_t observedUs=0;
    bool numeric() const { return event=="SAMPLE" && sample.usable(); }
};

class Tracker
{
public:
    bool Feed(const Status& s,std::int64_t nowMs)
    {
        lastReplyMs=nowMs;
        health=s;
        if(nowMs<lastNowMs_){lastNowMs_=nowMs;return Gap(nowMs,Stale,"CLOCK");}
        lastNowMs_=nowMs;
        if(boundary_ && (s.generation!=generation_ || s.reference!=reference_ || s.object!=object_))
        {
            Gap(nowMs,Warmup,"BOUNDARY"); hasPair_=false;used_={};usedTime_={};ResetExtrema();
        }
        generation_=s.generation; reference_=s.reference; object_=s.object; boundary_=true;
        if(!s.usable())
        {
            unsigned q=s.quality;
            if(!s.axesConfirmed)q|=Axes;
            if(s.age[0]>std::min(s.maxAge,MaxAgeUs) || s.age[1]>std::min(s.maxAge,MaxAgeUs))q|=Stale;
            if(s.skew>std::min(s.maxSkew,MaxSkewUs))q|=Skew;
            if(!q)q=Missing;
            return Gap(nowMs,q);
        }
        if(hasPair_)
        {
            if(s.sequence[0]<used_[0] || s.sequence[1]<used_[1] ||
               s.time[0]<usedTime_[0] || s.time[1]<usedTime_[1]) return Gap(nowMs,Stale,"ORDER");
            if(s.sequence[0]<=used_[0] || s.sequence[1]<=used_[1])return false;
        }
        if(startMs_<0)startMs_=nowMs;
        Row row;row.id=++rowCounter_;row.eventMs=static_cast<std::uint64_t>(std::max<std::int64_t>(0,nowMs));row.seconds=static_cast<double>(nowMs-startMs_)/1000.0;row.sample=s;
        row.observedUs=static_cast<std::uint64_t>(std::max<std::int64_t>(0,nowMs))*1000;
        rows.push_back(row);Trim();
        current=s; valid=true; gap_=false; hasPair_=true; used_=s.sequence; usedTime_=s.time;
        for(unsigned axis=0;axis<2;axis++)
        {
            minimum[axis]=std::isfinite(minimum[axis])?std::min(minimum[axis],s.value[axis]):s.value[axis];
            maximum[axis]=std::isfinite(maximum[axis])?std::max(maximum[axis],s.value[axis]):s.value[axis];
        }
        return true;
    }
    bool Expire(std::int64_t nowMs)
    {
        return lastReplyMs>=0 && (nowMs<lastReplyMs || nowMs-lastReplyMs>500)?Gap(nowMs,Stale):false;
    }
    bool Gap(std::int64_t nowMs,unsigned q,const std::string& event="GAP")
    {
        valid=false;health.ready=false;health.quality=q;
        if(!(q&Warmup) && event!="BOUNDARY" && (health.sequence[0] || health.sequence[1]))
        {
            // A failed/expired publication consumes a recovery boundary too.
            // Re-labeling one old head as healthy must not create a fresh pair.
            hasPair_=true;
            for(unsigned i=0;i<2;i++){used_[i]=std::max(used_[i],health.sequence[i]);usedTime_[i]=std::max(usedTime_[i],health.time[i]);}
        }
        if(gap_ && lastGap_==q && event=="GAP")return false;
        if(startMs_<0)startMs_=nowMs;
        Row row;row.id=++rowCounter_;row.eventMs=static_cast<std::uint64_t>(std::max<std::int64_t>(0,nowMs));row.seconds=static_cast<double>(std::max<std::int64_t>(0,nowMs-startMs_))/1000.0;
        row.sample=health;row.sample.ready=false;row.sample.quality=q;
        row.sample.value={NaN,NaN};row.sample.raw={NaN,NaN};row.event=event;
        row.observedUs=static_cast<std::uint64_t>(std::max<std::int64_t>(0,nowMs))*1000;
        rows.push_back(row);Trim();gap_=true;lastGap_=q;return true;
    }
    void ResetExtrema(){minimum={NaN,NaN};maximum={NaN,NaN};}
    std::deque<Row> rows;
    Status current,health;
    bool valid=false;
    std::int64_t lastReplyMs=-1;
    std::array<double,2> minimum{NaN,NaN},maximum{NaN,NaN};
private:
    void Trim(){while(rows.size()>6000)rows.pop_front();}
    bool hasPair_=false,boundary_=false,gap_=false;
    unsigned lastGap_=0;
    int reference_=2,object_=3;
    std::uint64_t generation_=0;
    std::uint64_t rowCounter_=0;
    std::array<std::uint64_t,2> used_{},usedTime_{};
    std::int64_t startMs_=-1,lastNowMs_=0;
};
} // namespace level
