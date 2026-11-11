#include "TestUtil.hpp"
#include "core/RelativeReport.hpp"
#include "core/Protocol.hpp"

level::Status Pair(std::uint64_t n=1)
{
    level::Status s;s.mode="level";s.ready=true;s.axesConfirmed=true;s.quality=0;s.generation=1;
    s.time={n*100000,n*100000+20000};s.sequence={n,n};s.age={30000,10000};s.skew=20000;
    s.ref={10,-2};s.obj={10.1,-1.8};s.raw={0.1,0.2};s.value=s.raw;s.offset={0,0};return s;
}
int main(int argc,char** argv)
{
    testutil::Init(argc,argv);
    SECTION("relative geometry and domain");
    CHECK_NEAR(level::Slope(45),1000,1e-9,"exact tangent, not the small-angle approximation");
    CHECK_NEAR(level::Height(-45,600),-600,1e-9,"explicit baseline in millimetres");
    CHECK(std::isnan(level::Slope(90)) && std::isnan(level::Slope(180)));
    CHECK(std::isnan(level::Height(1,0)));
    auto s=Pair();CHECK(s.usable());
    s.value[0]=999;CHECK(!s.usable());s=Pair();s.obj[0]=level::NaN;CHECK(!s.usable());
    s=Pair();s.axesConfirmed=false;CHECK(!s.usable());
    s=Pair();s.age[0]=500001;CHECK(!s.usable());
    s=Pair();s.skew=50001;CHECK(!s.usable());
    s=Pair();s.skew=19999;CHECK(!s.usable());
    s=Pair();s.offsetValid=true;s.offset={0.02,0.1};s.value={0.08,0.1};CHECK(s.usable());
    CHECK_NEAR(s.value[0],s.obj[0]-s.ref[0]-s.offset[0],1e-12,"REL offset separate from heads");

    SECTION("wire nullable values and provenance");
    const std::string wire=R"({"mode":"level","reference":2,"object":3,"ready":true,"quality":0,"axes_confirmed":true,"ref_us":100000,"obj_us":120000,"ref_seq":1,"obj_seq":1,"ref_age_us":30000,"obj_age_us":10000,"skew_us":20000,"generation":1,"rel_offset":false,"ref_x":10,"ref_y":-2,"obj_x":10.1,"obj_y":-1.8,"raw_x":0.1,"raw_y":0.2,"x":0.1,"y":0.2,"offset_x":0,"offset_y":0,"max_age_us":500000,"max_skew_us":50000})";
    level::Status parsed;std::string error;
    CHECK(proto::ParseRelativeStatus(wire,parsed,&error));CHECK(parsed.usable());
    auto broken=wire;broken.replace(broken.find("\"x\":0.1"),7,"\"x\":null");
    CHECK(proto::ParseRelativeStatus(broken,parsed,&error));CHECK(!parsed.usable());
    broken=wire;broken.replace(broken.find("\"ref_seq\":1"),11,"\"ref_seq\":1.5");
    CHECK(!proto::ParseRelativeStatus(broken,parsed,&error));
    broken=wire;broken.replace(broken.find("\"quality\":0"),11,"\"quality\":128");
    CHECK(!proto::ParseRelativeStatus(broken,parsed,&error));
    CHECK(!proto::ParseRelativeStatus(R"({"v":1,"sens":[]})",parsed));
    CHECK(!proto::ParseVer({"BWM427 firmware v1.8 board=tab5 chip=esp32p4 proto=3"}).IsTab5());
    CHECK(proto::ParseVer({"BWM427 fw=1.6.0 board=stm32f411 chip=stm32f411 proto=2"}).version=="1.6.0");

    SECTION("pair publication, gaps, recovery, generation");
    level::Tracker tracker;
    CHECK(tracker.Feed(Pair(1),100));CHECK(tracker.valid && tracker.rows.size()==1);
    CHECK(!tracker.Feed(Pair(1),120));CHECK(tracker.rows.size()==1);
    auto partial=Pair(2);partial.sequence[1]=1;partial.time[1]=120000;partial.skew=80000;
    CHECK(tracker.Feed(partial,200));CHECK(!tracker.valid && !tracker.rows.back().numeric());
    CHECK(!tracker.Feed(partial,210));
    CHECK(tracker.Feed(Pair(1),220));CHECK(!tracker.valid); // out-of-order gap event, never a recovery
    CHECK(!tracker.Feed(Pair(2),230));CHECK(!tracker.valid); // skew publication consumed reference sequence 2
    CHECK(tracker.Feed(Pair(3),250));CHECK(tracker.valid);
    CHECK(tracker.Expire(751));CHECK(!tracker.valid);
    CHECK(!tracker.Expire(760));
    CHECK(!tracker.Feed(Pair(3),770));CHECK(!tracker.valid);
    CHECK(tracker.Feed(Pair(4),800));CHECK(tracker.valid);
    auto changed=Pair(5);changed.generation=2;changed.offsetValid=true;changed.offset={0.1,0.2};changed.value={0,0};
    CHECK(tracker.Feed(changed,900));CHECK(tracker.valid);
    CHECK_NEAR(tracker.minimum[0],0,1e-12,"generation resets extrema");
    CHECK(tracker.rows[tracker.rows.size()-2].event=="BOUNDARY");

    SECTION("REL CSV and XLSX preserve audit and gaps");
    std::string csv=std::string(level::CsvHeader)+"\r\n";
    for(const auto& row:tracker.rows)csv+=level::CsvRow(row);
    level::Log log;CHECK(level::ParseCsv(csv,log));CHECK(log.rows.size()==tracker.rows.size());
    CHECK(log.numeric==4);CHECK(log.rows.front().numeric());
    CHECK(log.rows.front().sample.sequence[0]==1);
    CHECK_NEAR(log.rows.front().sample.value[1],0.2,1e-9,"comma decimal round trip");
    CHECK(!level::ParseCsv("Date;Time;RawX;RawY\n10;20;1;2\n",log));
    CHECK(level::ParseCsv(csv,log));
    level::ReportOptions options;options.unit=1;options.tolerance=true;options.limit={0.05,0.1};
    auto book=level::BuildBook(log,options);
    CHECK(book.size()==3 && book[0].name=="Угломер" && book[1].name=="Измерения");
    CHECK(book[1].rows.size()==log.rows.size()+1);
    CHECK(book[1].rows[1][29].kind==xlsx::Cell::Kind::Number);
    CHECK_NEAR(book[1].rows[1][29].num,level::Slope(0.1),1e-12,"report applies chosen units");
    CHECK(book[1].rows[2][29].kind==xlsx::Cell::Kind::Empty); // the first gap stays blank
    CHECK(book[1].rows[1][32].text=="нет");
    auto zip=xlsx::Build(book);CHECK(zip.size()>1000);
    level::Log round;CHECK(level::ParseAny(zip,round));CHECK(round.fromXlsx && round.numeric==log.numeric);
    CHECK(round.rows.size()==log.rows.size());
    CHECK(round.rows.front().sample.time==log.rows.front().sample.time);
    options.unit=2;options.baselineMm=0;CHECK(level::BuildBook(log,options).empty());
    CHECK(!level::ParseCsv("Reference;Object;RelDX;RefSeq\n2;3;0.1;1\n",round));

    SECTION("duplicate or half-updated samples are not extra measurements");
    level::Row r;r.sample=Pair(1);r.event="SAMPLE";r.observedUs=1000000;r.eventMs=1000;
    std::string duplicate=std::string(level::CsvHeader)+"\n"+level::CsvRow(r);
    r.observedUs+=100000;r.eventMs+=100;duplicate+=level::CsvRow(r);
    CHECK(level::ParseCsv(duplicate,round));CHECK(round.numeric==1 && round.rejected==1);
    CHECK(!round.rows.back().numeric() && round.rows.back().event=="REJECT");
    r.sample.sequence={2,1};r.observedUs+=100000;r.eventMs+=100;duplicate+=level::CsvRow(r);
    CHECK(level::ParseCsv(duplicate,round));CHECK(round.numeric==1 && round.rejected==2);
    SECTION("firmware recovery marker does not consume the following sample");
    r.sample=Pair(3);r.event="RECOVERY";r.observedUs=3001000;r.eventMs=3001;
    std::string recovery=std::string(level::CsvHeader)+"\n"+level::CsvRow(r);
    r.event="SAMPLE";r.eventMs=3000;recovery+=level::CsvRow(r); // same observed timestamp, as emitted by STM32
    CHECK(level::ParseCsv(recovery,round));
    CHECK(round.rows.size()==2 && round.rows[0].event=="RECOVERY");
    CHECK(round.numeric==1 && round.rejected==0 && round.rows[1].numeric());
    SECTION("graph thinning keeps extrema and never bridges gaps");
    level::Curve curve;
    for(int i=0;i<20000;i++){curve.t.push_back(i);curve.x.push_back(i==12345?1000.0:0.0);curve.y.push_back(-curve.x.back());}
    curve.x[3000]=level::NaN;
    const auto thin=level::ThinCurve(curve,1200);
    CHECK(thin.t.size()<=1200);
    CHECK(std::find(thin.x.begin(),thin.x.end(),1000.0)!=thin.x.end());
    CHECK(std::find(thin.y.begin(),thin.y.end(),-1000.0)!=thin.y.end());
    CHECK(std::any_of(thin.x.begin(),thin.x.end(),[](double x){return std::isnan(x);}));
    CHECK(std::is_sorted(thin.t.begin(),thin.t.end()));
    csvlog::Log ordinary;CHECK(!csvlog::Parse(csv,ordinary));
    CHECK(ordinary.error.find("угломера")!=std::string::npos);
    return TestSummary();
}
