// Existing per-post reversal: no second flip and no automatic orientation detection.
#include "TestUtil.hpp"
#include <chrono>
#include "core/HeelData.hpp"
#include "core/HeelReport.hpp"
#include "core/PcSettings.hpp"
#include "core/Project.hpp"
int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    std::setvbuf(stdout,nullptr,_IONBF,0);
    std::vector<heel::File> raw(2);
    for (int i=0; i<2; ++i) {
        raw[i].sensor=i+2;
        csvlog::Row left, right, zero, missing;
        left.t=0; left.calcX=2; left.calcY=3; right.calcX=-2; right.calcY=-3;
        zero.calcX=zero.calcY=0;
        raw[i].log.rows={left,right,zero,missing};
    }
    for (const auto& keys : {std::set<int>{}, std::set<int>{2}, std::set<int>{3}, std::set<int>{2,3}}) {
        auto out=heel::ApplyInvert(raw,keys);
        for(int i=0;i<2;++i) for(int axis=0;axis<2;++axis) {
            double sign=keys.count(i+2)?-1:1;
            CHECK(out[i].log.rows[0].Calc(axis)==sign*raw[i].log.rows[0].Calc(axis));
            CHECK(out[i].log.rows[1].Calc(axis)==sign*raw[i].log.rows[1].Calc(axis));
            CHECK(out[i].log.rows[2].Calc(axis)==0);
            CHECK(std::isnan(out[i].log.rows[3].Calc(axis)));
        }
        // The report consumes the already reversed selected-axis statistics without another flip.
        for(int axis=0;axis<2;++axis) {
            heel::Settings settings; settings.axis=axis;
            auto reportFiles=out;
            heel::Measurement m;
            for(int post=0;post<2;++post) {
                reportFiles[post].log.rows.resize(1);
                m.post[post].file=post;
                m.post[post].st=heel::TailStats(reportFiles[post].log,axis,20);
            }
            const auto sheets=heel::BuildReport(settings,reportFiles,{m},{},{});
            CHECK(!sheets.empty() && sheets[0].rows.size()>2);
            if(!sheets.empty() && sheets[0].rows.size()>2)
                for(int post=0;post<2;++post)
                    CHECK_NEAR(sheets[0].rows[post+1][4].num,
                               (keys.count(post+2)?-1:1)*raw[post].log.rows[0].Calc(axis),1e-12,"exported angle sign");
        }
        // UI reapplies from raw, never from the already reversed output.
        auto again=heel::ApplyInvert(raw,keys);
        CHECK(again[0].log.rows[0].calcY==out[0].log.rows[0].calcY);
        CHECK(raw[0].log.rows[0].calcY==3);
    }
    proj::Project p; p.axis=1; p.folder="records";
    p.sensors={{2,"Bow",true,true},{3,"Stern",true,true}};
    proj::Project q; std::string error;
    CHECK_MSG(proj::FromJson(proj::ToJson(p),q,&error), "%s", error.c_str());
    CHECK(q.axis==1 && q.sensors.size()==2);
    if(q.sensors.size()==2) CHECK(q.sensors[0].inverted && q.sensors[1].inverted);
    CHECK(!proj::Sensor{}.inverted);
    auto old=proj::ToJson(p);
    const std::string field=", \"inverted\": true";
    for(auto at=old.find(field);at!=std::string::npos;at=old.find(field)) old.erase(at,field.size());
    CHECK_MSG(proj::FromJson(old,q,&error), "%s", error.c_str());
    CHECK(q.sensors.size()==2);
    if(q.sensors.size()==2) CHECK(!q.sensors[0].inverted && !q.sensors[1].inverted);
    const auto temp = std::filesystem::temp_directory_path() /
        ("reverse-regression-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temp);
    const auto ini = temp / "settings.ini";
    PcSettings cfg;
    CHECK(cfg.sensorInvert.empty());
    cfg.sensorInvert = "2,3";
    cfg.Save(ini);
    PcSettings restored;
    restored.Load(ini);
    CHECK(restored.sensorInvert == "2,3");
    p.folder = text::PathToUtf8(temp);
    const auto project = temp / "reverse.krenomer";
    CHECK_MSG(proj::Save(project,p,&error), "%s", error.c_str());
    CHECK_MSG(proj::Load(project,q,&error), "%s", error.c_str());
    CHECK(q.axis==1 && q.sensors.size()==2);
    if(q.sensors.size()==2) CHECK(q.sensors[0].inverted && q.sensors[1].inverted);
    std::filesystem::remove(ini);
    std::filesystem::remove(project);
    std::filesystem::remove(temp);
    return TestSummary();
}
