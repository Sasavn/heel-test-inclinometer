#pragma once
// Tab5/STM32 _REL.CSV is an audit of pairs; it must not enter the heel pipeline.
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <vector>
#include "Relative.hpp"
#include "CsvLog.hpp"
#include "TextUtil.hpp"

namespace level
{
inline constexpr const char* CsvHeader="EventMs;EventSensor;Reference;Object;RefUs;ObjUs;RefSeq;ObjSeq;RefAgeUs;ObjAgeUs;SkewUs;Quality;Ready;AxesConfirmed;RefX;RefY;ObjX;ObjY;LiveDX;LiveDY;OffsetValid;OffsetX;OffsetY;RelDX;RelDY;Generation;Event;ObservedUs";
struct Log
{
    std::vector<Row> rows;
    std::string source,error;
    std::size_t skipped=0, rejected=0, numeric=0;
    bool fromXlsx=false;
};

inline std::string ColumnName(std::string_view input)
{
    input=csvlog::detail::Trim(input);
    if(input.size()>1 && input.front()=='"' && input.back()=='"') input=input.substr(1,input.size()-2);
    std::string out;
    for(char c:input)if(c!=' ' && c!='\t')out+=c>='A' && c<='Z'?static_cast<char>(c+'a'-'A'):c;
    return out;
}
inline bool UnsignedText(std::string_view text,std::uint64_t& value)
{
    text=csvlog::detail::Trim(text);
    if(text.size()>1 && text.front()=='"' && text.back()=='"') text=text.substr(1,text.size()-2);
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return !text.empty() && result.ec==std::errc{} && result.ptr==text.data()+text.size();
}
inline bool IsRelativeHeader(std::string_view line)
{
    const auto name=ColumnName(line);
    return name.find("reference")!=std::string::npos && name.find("object")!=std::string::npos &&
        name.find("reldx")!=std::string::npos &&
        name.find("refseq")!=std::string::npos;
}

inline bool ParseCsv(std::string_view text,Log& out)
{
    out=Log{};
    std::string decoded;
    if(csvlog::detail::FromUtf16(text,decoded))text=decoded;
    if(text.starts_with("\xef\xbb\xbf"))text.remove_prefix(3);
    std::map<std::string,std::size_t> columns;
    char sep=';'; bool header=false;
    std::vector<std::string_view> cells;
    std::optional<std::uint64_t> firstObserved,lastObserved;
    std::array<std::uint64_t,2> used{};
    std::uint64_t generation=0;
    int reference=0,object=0;
    bool havePair=false;
    for(std::size_t from=0;from<text.size();)
    {
        std::size_t to=text.find_first_of("\r\n",from);if(to==std::string_view::npos)to=text.size();
        auto line=csvlog::detail::Trim(text.substr(from,to-from));from=to;
        while(from<text.size() && (text[from]=='\r' || text[from]=='\n'))++from;
        if(line.empty() || line.front()=='#' || line.starts_with("sep="))continue;
        if(!header)
        {
            if(!IsRelativeHeader(line)){++out.skipped;continue;}
            sep=csvlog::detail::GuessSep(line);
            csvlog::detail::SplitInto(line,sep,cells);
            for(std::size_t i=0;i<cells.size();++i)columns[ColumnName(cells[i])]=i;
            const char* required[]={"eventms","reference","object","refus","objus","refseq","objseq",
                "refageus","objageus","skewus","quality","ready","axesconfirmed","refx","refy","objx","objy",
                "livedx","livedy","offsetvalid","offsetx","offsety","reldx","reldy","generation"};
            for(const char* name:required)if(!columns.count(name)){out.error="В журнале угломера нет столбца "+std::string(name);return false;}
            header=true;continue;
        }
        csvlog::detail::SplitInto(line,sep,cells);
        auto get=[&](const char* key)->std::string_view{const auto it=columns.find(key);return it!=columns.end() && it->second<cells.size()?cells[it->second]:std::string_view{};};
        auto number=[&](const char* key){return csvlog::detail::Number(get(key));};
        auto integer=[&](const char* key,std::uint64_t& value){return UnsignedText(get(key),value);};
        std::uint64_t r=0,o=0,q=0,ready=0,axes=0,offset=0,gen=0,eventMs=0,observed=0;
        Row row; auto& s=row.sample;s.mode="level";
        bool valid=integer("reference",r) && integer("object",o) && integer("quality",q) &&
            integer("ready",ready) && integer("axesconfirmed",axes) && integer("offsetvalid",offset) &&
            integer("generation",gen) && integer("eventms",eventMs) &&
            integer("refus",s.time[0]) && integer("objus",s.time[1]) &&
            integer("refseq",s.sequence[0]) && integer("objseq",s.sequence[1]) &&
            integer("refageus",s.age[0]) && integer("objageus",s.age[1]) && integer("skewus",s.skew);
        if(!valid || r>3 || o>3 || q>127 || ready>1 || axes>1 || offset>1){++out.skipped;continue;}
        if(!integer("observedus",observed))
        {
            if(eventMs>std::numeric_limits<std::uint64_t>::max()/1000){++out.skipped;continue;}
            observed=eventMs*1000;
        }
        s.reference=static_cast<int>(r);s.object=static_cast<int>(o);s.quality=static_cast<unsigned>(q);
        s.ready=ready!=0;s.axesConfirmed=axes!=0;s.offsetValid=offset!=0;s.generation=gen;
        s.ref={number("refx"),number("refy")};s.obj={number("objx"),number("objy")};
        s.raw={number("livedx"),number("livedy")};s.offset={number("offsetx"),number("offsety")};
        s.value={number("reldx"),number("reldy")};
        row.event=std::string(get("event"));if(row.event.empty())row.event="SAMPLE";
        row.observedUs=observed;row.eventMs=eventMs;row.id=out.rows.size()+1;
        if(!firstObserved)firstObserved=observed;
        row.seconds=observed>=*firstObserved?static_cast<double>(observed-*firstObserved)/1e6:0;
        if(reference!=s.reference || object!=s.object || generation!=gen)
        {reference=s.reference;object=s.object;generation=gen;havePair=false;used={};}
        if(lastObserved && observed<*lastObserved)
        {row.event="ORDER";s.ready=false;s.quality|=Stale;++out.rejected;}
        else if(row.event=="SAMPLE")
        {
            if(!s.usable())
            {row.event="REJECT";s.ready=false;if(!s.quality)s.quality|=Error;++out.rejected;}
            else if(havePair && (s.sequence[0]<=used[0] || s.sequence[1]<=used[1]))
            {row.event="REJECT";s.ready=false;s.quality|=Warmup;++out.rejected;}
            else {used=s.sequence;havePair=true;++out.numeric;}
        }
        if(!row.numeric() && !s.usable() && !(s.quality&Warmup) && row.event!="BOUNDARY" && (s.sequence[0] || s.sequence[1]))
        {for(unsigned axis=0;axis<2;axis++)used[axis]=std::max(used[axis],s.sequence[axis]);havePair=true;}
        lastObserved=lastObserved?std::max(*lastObserved,observed):observed;
        out.rows.push_back(std::move(row));
        if(out.rows.size()>500000){out.error="Журнал больше 500000 строк; разделите запись";return false;}
    }
    if(!header){out.error="Это не журнал угломера REL.CSV";return false;}
    if(out.rows.empty()){out.error="В журнале нет читаемых строк";return false;}
    return true;
}

inline bool ParseAny(std::string_view bytes,Log& out)
{
    if(!xlsxread::IsZip(bytes))return ParseCsv(bytes,out);
    std::vector<xlsxread::Sheet> sheets;std::string error;
    if(!xlsxread::ToCsv(bytes,sheets,&error)){out=Log{};out.error=error;return false;}
    for(const auto& sheet:sheets)if(ParseCsv(sheet.csv,out)){out.fromXlsx=true;return true;}
    out.error="В книге нет таблицы пар угломера";return false;
}
inline bool ReadFile(const std::filesystem::path& path,Log& out)
{
    std::error_code ec;
    if(std::filesystem::file_size(path,ec)>256u*1024u*1024u && !ec){out=Log{};out.error="Файл больше 256 МБ";return false;}
    std::ifstream in(path,std::ios::binary);
    if(!in){out=Log{};out.error="Не удалось открыть журнал угломера";return false;}
    const std::string bytes((std::istreambuf_iterator<char>(in)),{});
    const bool ok=ParseAny(bytes,out);
    out.source=text::PathToUtf8(path.filename());return ok;
}
inline std::string Numeric(double v)
{
    if(!std::isfinite(v))return {};
    std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::fixed<<std::setprecision(6)<<v;
    auto out=stream.str();std::replace(out.begin(),out.end(),'.',',');return out;
}
inline std::string CsvRow(const Row& row)
{
    const auto& s=row.sample;std::ostringstream o;o.imbue(std::locale::classic());
    o<<row.eventMs<<';'<<s.object<<';'<<s.reference<<';'<<s.object<<';'
     <<s.time[0]<<';'<<s.time[1]<<';'<<s.sequence[0]<<';'<<s.sequence[1]<<';'<<s.age[0]<<';'<<s.age[1]<<';'
     <<s.skew<<';'<<s.quality<<';'<<(s.ready?1:0)<<';'<<(s.axesConfirmed?1:0)<<';'
     <<Numeric(s.ref[0])<<';'<<Numeric(s.ref[1])<<';'<<Numeric(s.obj[0])<<';'<<Numeric(s.obj[1])<<';'
     <<Numeric(s.raw[0])<<';'<<Numeric(s.raw[1])<<';'<<(s.offsetValid?1:0)<<';'
     <<Numeric(s.offset[0])<<';'<<Numeric(s.offset[1])<<';'
     <<Numeric(s.value[0])<<';'<<Numeric(s.value[1])<<';'<<s.generation<<';'<<row.event<<';'<<row.observedUs<<'\n';
    return o.str();
}
} // namespace level
