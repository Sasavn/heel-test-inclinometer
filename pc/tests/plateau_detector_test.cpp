// Standalone, no GUI/COM. Compile: g++ -std=c++23 -O2 -Ipc/src
// pc/tests/plateau_detector_test.cpp -o plateau_detector_test.exe
// Optional first argument: repository pc/tests/data directory.
#include "core/Plateaus.hpp"
#include "core/CsvLog.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>

static unsigned checks;
#define CHECK(c) do { ++checks; if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);} } while(0)

static std::vector<plateau::Span> Steps(const std::vector<double>& levels,
    const std::vector<double>& durations, double sign, double offset,
    const plateau::Params& p)
{
    std::vector<double> t,v;
    double start=0;
    for(std::size_t i=0;i<levels.size();++i){
        for(int j=0;j<static_cast<int>(durations[i]*10);++j){
            double s=j*.1;
            t.push_back(start+s);
            v.push_back(offset+sign*(levels[i]+.02*std::sin(s*2.0)));
        }
        start+=durations[i];
    }
    return plateau::Detect(t,v,p);
}

int main(int argc,char**argv){
    // Neither expected position count nor sign is an input to Detect.
    for(int count : {2,3,5,9,11}){
        std::vector<double> levels,durations(count,40.0);
        for(int i=0;i<count;i++)levels.push_back(i*.35);
        plateau::Params p;p.minStepDeg=.2;
        auto a=Steps(levels,durations,1,0,p);
        auto b=Steps(levels,durations,-1,7,p);
        CHECK(a.size()==levels.size());CHECK(b.size()==a.size());
        for(std::size_t i=0;i<a.size();i++){
            CHECK(std::fabs(a[i].mean-levels[i])<.01);
            CHECK(std::fabs(b[i].mean-(7-levels[i]))<.01);
            CHECK(std::fabs(a[i].t0-b[i].t0)<.11);
            CHECK(std::fabs(a[i].t1-b[i].t1)<.11);
            CHECK(a[i].Duration()>=p.minLenS);
        }
    }
    // A genuine short stop at a local extremum must not be globally deleted
    // just to remove a similarly shaped transport stop in one experiment.
    auto shortStop=Steps({0,2,1},{40,18,40},1,0,{});
    CHECK(shortStop.size()==3);
    CHECK(shortStop[1].Duration()>=10&&shortStop[1].Duration()<20);
    CHECK(std::fabs(shortStop[1].mean-2)<.01);
    CHECK(Steps({0},{8},1,0,{}).empty());
    CHECK(Steps({0},{40},1,0,{}).size()==1);
    CHECK(plateau::Detect({}, {}, {}).empty());
    CHECK(plateau::Detect({0,1},{0,1},{}).empty());
    if(argc>1){
        // Historical regression, not new claims about ground-truth positions.
        struct Case{const char*file;std::size_t x,y;};
        const Case cases[]={
            {"real/2026-10-09_M020_D2.CSV",10,1},
            {"m021/2026-10-15_M021_D2.CSV",6,1},
            {"m021/2026-10-15_M021_D3.CSV",7,1},
            {"m024/2026-10-16_M024_D2.CSV",9,1},
            {"m024/2026-10-16_M024_D3.CSV",9,1}};
        for(auto c:cases){
            csvlog::Log log;CHECK(csvlog::ReadFile(std::filesystem::path(argv[1])/c.file,log));
            for(int axis=0;axis<2;axis++){
                std::vector<double> t,v,inv;
                for(auto&r:log.rows){t.push_back(r.t);v.push_back(r.Calc(axis));inv.push_back(-r.Calc(axis));}
                auto a=plateau::Detect(t,v,{}),b=plateau::Detect(t,inv,{});
                CHECK(a.size()==(axis?c.y:c.x));CHECK(a.size()==b.size());
                for(std::size_t i=0;i<a.size();i++){
                    CHECK(std::fabs(a[i].t0-b[i].t0)<1e-9);
                    CHECK(std::fabs(a[i].t1-b[i].t1)<1e-9);
                    CHECK(std::fabs(a[i].mean+b[i].mean)<1e-9);
                }
                std::printf("HISTORICAL %s axis=%d spans=%zu sign-parity PASS\n",c.file,axis,a.size());
            }
        }
    }
    std::printf("PLATEAU_DETECTOR %u checks PASS; does not claim M025 automatic nine-position acceptance\n",checks);
}
