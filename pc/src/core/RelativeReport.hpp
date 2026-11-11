#pragma once
#include "RelativeLog.hpp"
#include "Xlsx.hpp"

namespace level
{
struct ReportOptions
{
    int unit=0; // 0 degrees, 1 mm/m, 2 height in mm on a stated baseline
    double baselineMm=1000;
    bool tolerance=false;
    std::array<double,2> limit{0.1,0.1}; // always degrees, before display filtering
    std::string program="Регистратор крена / Угломер", when, platform;
};
inline double Display(double angle,const ReportOptions& options)
{ return options.unit==0?angle:options.unit==1?Slope(angle):Height(angle,options.baselineMm); }
inline const char* Unit(const ReportOptions& options)
{ return options.unit==0?"°":options.unit==1?"мм/м":"мм"; }
inline bool InTolerance(const Row& row,const ReportOptions& options)
{ return row.numeric() && std::fabs(row.sample.value[0])<=options.limit[0] && std::fabs(row.sample.value[1])<=options.limit[1]; }

struct Curve { std::vector<double> t,x,y; };
inline Curve ThinCurve(const Curve& in,std::size_t maximum=12000)
{
    if(in.t.size()<=maximum || maximum<12)return in;
    Curve out;
    const auto stride=(in.t.size()+maximum/6-1)/(maximum/6);
    for(std::size_t start=0;start<in.t.size();start+=stride)
    {
        const auto end=std::min(start+stride,in.t.size());
        std::array<std::size_t,6> take{start,end-1,start,start,start,start};bool gapX=false,gapY=false;
        for(std::size_t i=start;i<end;i++)
        {
            gapX|=!std::isfinite(in.x[i]);gapY|=!std::isfinite(in.y[i]);
            if(std::isfinite(in.x[i]))
            {if(!std::isfinite(in.x[take[2]]) || in.x[i]<in.x[take[2]])take[2]=i;if(!std::isfinite(in.x[take[3]]) || in.x[i]>in.x[take[3]])take[3]=i;}
            if(std::isfinite(in.y[i]))
            {if(!std::isfinite(in.y[take[4]]) || in.y[i]<in.y[take[4]])take[4]=i;if(!std::isfinite(in.y[take[5]]) || in.y[i]>in.y[take[5]])take[5]=i;}
        }
        std::sort(take.begin(),take.end());auto finish=std::unique(take.begin(),take.end());
        for(auto it=take.begin();it!=finish;++it)
        {const auto i=*it;out.t.push_back(in.t[i]);out.x.push_back(gapX?NaN:in.x[i]);out.y.push_back(gapY?NaN:in.y[i]);}
    }
    return out;
}
inline Curve BuildCurve(const Log& log,const ReportOptions& options,bool presentationFilter=false)
{
    Curve curve;std::array<double,2> ema{NaN,NaN};unsigned warm=0;
    std::uint64_t generation=0;int reference=0,object=0;
    for(const auto& row:log.rows)
    {
        const auto& s=row.sample;
        const bool boundary=generation!=s.generation || reference!=s.reference || object!=s.object;
        if(boundary && !curve.t.empty())
        {curve.t.push_back(row.seconds);curve.x.push_back(NaN);curve.y.push_back(NaN);}
        if(boundary || !row.numeric()){ema={NaN,NaN};warm=0;}
        generation=s.generation;reference=s.reference;object=s.object;
        auto values=s.value;
        if(row.numeric() && presentationFilter)
        {
            ++warm;
            for(unsigned a=0;a<2;a++)ema[a]=std::isfinite(ema[a])?ema[a]+0.2*(values[a]-ema[a]):values[a];
            values=warm>=5?ema:std::array<double,2>{NaN,NaN};
        }
        curve.t.push_back(row.seconds);
        curve.x.push_back(row.numeric()?Display(values[0],options):NaN);
        curve.y.push_back(row.numeric()?Display(values[1],options):NaN);
    }
    return curve;
}

inline std::vector<xlsx::Sheet> BuildBook(const Log& log,const ReportOptions& options,
                                          const std::vector<xlsx::Image>& images={})
{
    if(options.unit<0 || options.unit>2 || (options.unit==2 && (!std::isfinite(options.baselineMm) || options.baselineMm<=0)))return {};
    if(options.tolerance && (!std::isfinite(options.limit[0]) || !std::isfinite(options.limit[1]) || options.limit[0]<0.01 || options.limit[1]<0.01 || options.limit[0]>90 || options.limit[1]>90))return {};
    xlsx::Sheet summary("Угломер");summary.header=false;summary.print=true;summary.widths={35,30,30,26,26,26};
    summary.AddRow({xlsx::Text("Угломер — относительный наклон объекта к опоре")});
    summary.AddRow({xlsx::Text("Программа"),xlsx::Text(options.program)});
    summary.AddRow({xlsx::Text("Сохранено"),xlsx::Text(options.when)});
    summary.AddRow({xlsx::Text("Платформа источника"),xlsx::Text(options.platform)});
    summary.AddRow({xlsx::Text("Журнал"),xlsx::Text(log.source)});
    summary.AddRow({xlsx::Text("Единицы представления"),xlsx::Text(Unit(options))});
    summary.AddRow({xlsx::Text("База для пересчёта, мм"),options.unit==2?xlsx::Num(options.baselineMm,3):xlsx::Empty()});
    summary.AddRow({xlsx::Text("Пользовательский допуск X/Y, °"),options.tolerance?xlsx::Num(options.limit[0],3):xlsx::Text("выключен"),options.tolerance?xlsx::Num(options.limit[1],3):xlsx::Empty()});
    summary.AddRow({xlsx::Text("Формула"),xlsx::Text("Δ = Объект − Опора; результат = Δ − REL-ноль. Нули отдельных головок не используются.")});
    summary.AddRow({xlsx::Text("Пересчёт"),xlsx::Text("мм/м = 1000·tan(θ); мм = база_мм·tan(θ). При |θ| ≥ 89,9° пересчёт не выводится.")});
    summary.AddRow({xlsx::Text("Происхождение времени"),xlsx::Text("Метки локального приёма. Аппаратной синхронизации головок нет.")});
    summary.AddRow({xlsx::Text("Границы метода"),xlsx::Text("Относительный наклон двух головок; не измерение плоскостности, профиля или точности поверхности.")});
    std::size_t valid=0,gaps=0;for(const auto& row:log.rows)row.numeric()?++valid:++gaps;
    summary.AddRow({xlsx::Text("Пригодных пар"),xlsx::Num(static_cast<double>(valid),0)});
    summary.AddRow({xlsx::Text("Разрывов / непригодных строк"),xlsx::Num(static_cast<double>(gaps),0)});
    summary.AddRow({xlsx::Text("Непрочитанных строк"),xlsx::Num(static_cast<double>(log.skipped),0)});
    summary.AddRow({xlsx::Text("Проверка допуска"),xlsx::Text("Проверяется по исходному REL-результату в градусах; HOLD и сглаживание её не меняют.")});
    summary.AddRow({});
    summary.AddRow({xlsx::Text("Сегмент / поколение"),xlsx::Text("Опора / объект"),xlsx::Text("Пар"),xlsx::Text("Min X, °"),xlsx::Text("Max X, °"),xlsx::Text("Min/Max Y, °")});
    struct Segment{std::uint64_t generation=0;int reference=0,object=0;std::size_t count=0;std::array<double,2> lo{NaN,NaN},hi{NaN,NaN};};
    std::vector<Segment> segments;
    for(const auto& row:log.rows)
    {
        const auto& s=row.sample;
        if(segments.empty() || segments.back().generation!=s.generation || segments.back().reference!=s.reference || segments.back().object!=s.object)
            segments.push_back({s.generation,s.reference,s.object});
        if(row.numeric())
        {
            auto& segment=segments.back();++segment.count;
            for(unsigned axis=0;axis<2;axis++)
            {segment.lo[axis]=std::isfinite(segment.lo[axis])?std::min(segment.lo[axis],s.value[axis]):s.value[axis];segment.hi[axis]=std::isfinite(segment.hi[axis])?std::max(segment.hi[axis],s.value[axis]):s.value[axis];}
        }
    }
    for(std::size_t i=0;i<segments.size();i++)
    {
        const auto& s=segments[i];
        summary.AddRow({xlsx::Text(std::to_string(i+1)+" / "+std::to_string(s.generation)),xlsx::Text(std::to_string(s.reference)+" / "+std::to_string(s.object)),
            xlsx::Num(static_cast<double>(s.count),0),xlsx::Num(s.lo[0],3),xlsx::Num(s.hi[0],3),
            xlsx::Text(s.count?Numeric(s.lo[1])+" / "+Numeric(s.hi[1]):"—")});
    }
    xlsx::Sheet data("Измерения");data.landscape=true;data.print=true;data.printTitleRows=1;
    std::vector<std::string_view> headers;csvlog::detail::SplitInto(CsvHeader,';',headers);
    std::vector<xlsx::Cell> cells;for(auto h:headers)cells.push_back(xlsx::Text(std::string(h)));
    cells.push_back(xlsx::Text("t, с"));cells.push_back(xlsx::Text(std::string("X, ")+Unit(options)));
    cells.push_back(xlsx::Text(std::string("Y, ")+Unit(options)));cells.push_back(xlsx::Text("Пригодна"));cells.push_back(xlsx::Text("В допуске"));
    data.AddRow(std::move(cells));data.widths=std::vector<double>(33,16);data.widths[26]=22;
    xlsx::Sheet quality("Журнал качества");quality.print=true;quality.widths={16,22,18,75,22};
    quality.AddRow({xlsx::Text("t, с"),xlsx::Text("Событие"),xlsx::Text("Quality"),xlsx::Text("Причина"),xlsx::Text("Поколение")});
    for(const auto& row:log.rows)
    {
        const auto& s=row.sample;
        auto integer=[](std::uint64_t v){return xlsx::Text(std::to_string(v));}; // uint64 provenance must survive Excel's 15-digit precision
        cells={integer(row.eventMs),xlsx::Num(s.object,0),xlsx::Num(s.reference,0),xlsx::Num(s.object,0),
            integer(s.time[0]),integer(s.time[1]),integer(s.sequence[0]),integer(s.sequence[1]),integer(s.age[0]),integer(s.age[1]),integer(s.skew),
            xlsx::Num(s.quality,0),xlsx::Num(s.ready?1:0,0),xlsx::Num(s.axesConfirmed?1:0,0),
            xlsx::Num(s.ref[0],3),xlsx::Num(s.ref[1],3),xlsx::Num(s.obj[0],3),xlsx::Num(s.obj[1],3),
            xlsx::Num(s.raw[0],3),xlsx::Num(s.raw[1],3),xlsx::Num(s.offsetValid?1:0,0),xlsx::Num(s.offset[0],3),xlsx::Num(s.offset[1],3),
            xlsx::Num(s.value[0],3),xlsx::Num(s.value[1],3),integer(s.generation),xlsx::Text(row.event),integer(row.observedUs),
            xlsx::Num(row.seconds,3),row.numeric()?xlsx::Num(Display(s.value[0],options),3):xlsx::Empty(),
            row.numeric()?xlsx::Num(Display(s.value[1],options),3):xlsx::Empty(),xlsx::Text(row.numeric()?"да":"нет"),
            row.numeric() && options.tolerance?xlsx::Text(InTolerance(row,options)?"да":"нет"):xlsx::Empty()};
        data.AddRow(std::move(cells));
        if(!row.numeric())quality.AddRow({xlsx::Num(row.seconds,3),xlsx::Text(row.event),xlsx::Num(s.quality,0),xlsx::Text(QualityText(s.quality)),integer(s.generation)});
    }
    if(quality.rows.size()==1)quality.AddRow({xlsx::Text("Непригодных строк нет")});
    for(auto& sheet:std::array<xlsx::Sheet*,3>{&summary,&data,&quality})
        for(auto& row:sheet->rows)for(auto& cell:row){cell.st.wrap=true;cell.styled=true;}
    if(!images.empty())
    {
        summary.images=images;
        const int start=static_cast<int>(summary.rows.size())+2;
        for(std::size_t i=0;i<summary.images.size();i++){summary.images[i].row=start+static_cast<int>(i)*38;summary.images[i].col=0;}
    }
    return {std::move(summary),std::move(data),std::move(quality)};
}
} // namespace level
