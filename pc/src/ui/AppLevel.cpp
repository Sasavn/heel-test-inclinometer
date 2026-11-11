#include "App.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <imgui.h>
#include <implot.h>
#include "../core/ExportTarget.hpp"
#include "../core/Png.hpp"
#include "../core/RelativeReport.hpp"
#include "Dialogs.hpp"
#include "Offscreen.hpp"
#include "Widgets.hpp"

namespace ui
{
namespace fs=std::filesystem;
struct LevelExportResult { bool ok=false;std::string path,message; };
struct LevelUiState
{
    level::Tracker live;
    level::Log imported;
    level::ReportOptions options;
    std::future<level::Log> loading;
    std::future<LevelExportResult> saving;
    RequestPtr poll,control;
    std::uint64_t connection=0,cursor=0;
    std::int64_t nextPoll=0;
    std::string message,folder,reportParent,reportDir,recordPath;
    std::ofstream record;
    bool hasImported=false,saveRequested=false,hold=false,filter=false;
    int tab=0,reference=2,object=3;
    level::Status held;
    std::int64_t heldAt=0;
    std::vector<level::Row> heldRows;
    level::Log liveCache,heldCache;
    std::uint64_t liveCacheId=0,importRevision=0;
    std::int64_t heldCacheAt=-1;
    level::Curve curveCache;
    std::string curveKey;
    ~LevelUiState(){if(record.is_open()){record<<"# END PC capture\n";record.flush();}}
};

namespace
{
level::Log LiveLog(const LevelUiState& state,bool held=false)
{
    level::Log out;
    if(held && state.hold)out.rows=state.heldRows;
    else out.rows.assign(state.live.rows.begin(),state.live.rows.end());
    out.source="Наблюдение PC: не полный поток всех ответов RS485";
    for(const auto& row:out.rows)if(row.numeric())++out.numeric;
    return out;
}
const level::Log& LiveDisplayLog(LevelUiState& state)
{
    const auto last=state.live.rows.empty()?0:state.live.rows.back().id;
    if(last!=state.liveCacheId || state.liveCache.rows.size()!=state.live.rows.size())
    {state.liveCache=LiveLog(state);state.liveCacheId=last;}
    return state.liveCache;
}
void DrawCurve(const level::Log& log,const level::ReportOptions& options,bool filter,float height,
               const level::Curve* prepared=nullptr)
{
    level::Curve fresh;
    if(!prepared){fresh=level::ThinCurve(level::BuildCurve(log,options,filter));prepared=&fresh;}
    const auto& curve=*prepared;
    const std::string y=std::string("Относительный наклон, ")+level::Unit(options);
    if(ImPlot::BeginPlot("##levelcurve",ImVec2(-1,height),ImPlotFlags_NoMenus))
    {
        ImPlot::SetupAxes("Время наблюдения, с",y.c_str(),ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        if(!curve.t.empty())
        {
            ImPlot::SetNextLineStyle(pal.accent,2);
            ImPlot::PlotLine("X",curve.t.data(),curve.x.data(),static_cast<int>(curve.t.size()));
            ImPlot::SetNextLineStyle(pal.warn,2);
            ImPlot::PlotLine("Y",curve.t.data(),curve.y.data(),static_cast<int>(curve.t.size()));
        }
        ImPlot::EndPlot();
    }
}
const level::Curve& DisplayCurve(LevelUiState& state,const level::Log& log,bool filter,bool imported)
{
    const std::string key=(imported?"file-"+std::to_string(state.importRevision):state.hold?"hold-"+std::to_string(state.heldAt):"live-"+std::to_string(state.liveCacheId))+"/"+
        std::to_string(state.options.unit)+"/"+std::to_string(state.options.baselineMm)+"/"+std::to_string(filter);
    if(state.curveKey!=key){state.curveCache=level::ThinCurve(level::BuildCurve(log,state.options,filter));state.curveKey=key;}
    return state.curveCache;
}
bool Ready(const std::future<level::Log>& task)
{return task.valid() && task.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}
bool Ready(const std::future<LevelExportResult>& task)
{return task.valid() && task.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}
}

LevelUiState& App::LevelState()
{
    if(!level_){level_=std::make_shared<LevelUiState>();level_->folder=settings_.recordDir.empty()?settings_.downloadDir:settings_.recordDir;}
    return *level_;
}
bool App::LevelRecording() const {return level_ && level_->record.is_open();}
void App::ResetLevelConnection()
{
    if(!level_)return;
    auto& s=*level_;s.live=level::Tracker{};s.connection=std::numeric_limits<std::uint64_t>::max();
    s.poll.reset();s.control.reset();s.hold=false;s.nextPoll=0;
    s.liveCache=level::Log{};s.liveCacheId=0;s.curveKey.clear();
}
bool App::LevelBusy() const {return level_ && (level_->loading.valid() || level_->saving.valid() || level_->saveRequested);}
std::size_t App::LevelRows() const {return level_?(level_->hasImported?level_->imported.rows.size():level_->live.rows.size()):0;}
std::size_t App::LevelValidRows() const
{
    if(!level_)return 0;
    if(level_->hasImported)return level_->imported.numeric;
    return static_cast<std::size_t>(std::count_if(level_->live.rows.begin(),level_->live.rows.end(),[](const auto& row){return row.numeric();}));
}
const std::string& App::LevelReportDir() const
{static const std::string empty;return level_?level_->reportDir:empty;}
void App::LevelSetView(int view){LevelState().tab=std::clamp(view,0,2);}
void App::LevelSetUnit(int unit,double baseline){auto& s=LevelState();s.options.unit=std::clamp(unit,0,2);s.options.baselineMm=baseline;}

bool App::LevelImportFile(const std::string& path)
{
    auto& state=LevelState();
    if(LevelRecording() || LevelBusy() || path.empty())return false;
    state.loading=std::async(std::launch::async,[path]{level::Log log;level::ReadFile(text::PathFromUtf8(path),log);return log;});
    state.message="Чтение журнала угломера…";state.tab=2;return true;
}
bool App::LevelSaveReport(const std::string& parent)
{
    auto& state=LevelState();
    if(LevelRecording() || LevelBusy() || parent.empty())return false;
    const auto count=state.tab==2 && state.hasImported?state.imported.rows.size():state.live.rows.size();
    if(!count){Notify("Нет журнала угломера для отчёта",2);return false;}
    state.reportParent=parent;state.saveRequested=true;state.message="Подготовка отчёта угломера…";return true;
}
bool App::LevelStartRecording(const std::string& parent)
{
    auto& state=LevelState();
    if(DeviceOperationBlock(true))return false;
    if(LevelRecording() || LevelBusy() || !HaveStatus() || snap_.status.instrument!="level" || !snap_.status.relativeDataCap ||
       !DeviceProfile().Known() || !state.live.valid || PcRecording() || updater_.Busy() || ota_.Busy() || recovery_.Busy() || dl_.running || state.control)return false;
    const auto target=exporttarget::Reserve(text::PathFromUtf8(parent),"Угломер_PC");
    if(!target){state.message=target.error;Notify(state.message,2);return false;}
    const auto file=target.dir/"PC_REL.CSV";
    state.record.open(file,std::ios::binary);
    if(!state.record){state.message="Не удалось открыть журнал для записи";Notify(state.message,2);return false;}
    state.record<<level::CsvHeader<<"\n# PC observed pairs; not lossless UART acquisition; no hardware synchronization\n";
    state.record.flush();
    if(!state.record){state.record.close();state.message="Не удалось записать заголовок журнала";Notify(state.message,2);return false;}
    state.recordPath=text::PathToUtf8(file);
    state.hasImported=false;
    state.cursor=state.live.rows.empty()?0:state.live.rows.back().id;
    state.message="Запись наблюдаемых пар на ПК: "+Shown(state.recordPath);
    link_->SetControlLocks(true,updater_.Busy() || ota_.Busy() || recovery_.Busy());
    Notify("Запись угломера на ПК начата",1);return true;
}
void App::LevelStopRecording()
{
    if(!LevelRecording())return;
    auto& state=*level_;
    for(const auto& row:state.live.rows)if(row.id>state.cursor)
    {state.record<<level::CsvRow(row);state.cursor=row.id;}
    state.record<<"# END PC capture\n";state.record.flush();
    const bool ok=static_cast<bool>(state.record);state.record.close();
    link_->SetControlLocks(PcRecording(),updater_.Busy() || ota_.Busy() || recovery_.Busy());
    const std::string path=state.recordPath;
    state.message=ok?"Журнал сохранён: "+Shown(path):"Ошибка записи журнала: "+Shown(path);
    Notify(state.message,ok?1:2,5000,text::PathToUtf8(text::PathFromUtf8(path).parent_path()));
    if(!LevelBusy())LevelImportFile(path);
}

void App::TickLevel()
{
    if(!level_ && !UsingLevel())return;
    auto& state=LevelState();const auto now=link_->NowMs();
    if(state.connection!=snap_.connectSeq)
    {
        if(LevelRecording())LevelStopRecording();
        state.connection=snap_.connectSeq;state.live=level::Tracker{};state.hold=false;
        state.poll.reset();state.control.reset();state.nextPoll=now;state.liveCache=level::Log{};state.liveCacheId=0;state.curveKey.clear();
    }
    if(Ready(state.loading))
    {
        try
        {
            auto imported=state.loading.get();
            if(imported.error.empty())
            {
                state.imported=std::move(imported);state.hasImported=true;state.tab=2;++state.importRevision;
                state.message=Fmt("Журнал: %zu строк, пригодных пар %zu, отклонено %zu",state.imported.rows.size(),state.imported.numeric,state.imported.rejected);
            }
            else {state.message=imported.error;Notify(state.message,2);}
        }
        catch(const std::exception& e){state.message=e.what();Notify("Ошибка чтения журнала: "+state.message,2);}
    }
    if(Ready(state.saving))
    {
        try
        {
            const auto result=state.saving.get();state.message=result.message;
            if(result.ok)state.reportDir=result.path;
            Notify(result.message,result.ok?1:2,6000,result.path);
        }
        catch(const std::exception& e){state.message=e.what();Notify("Ошибка сохранения: "+state.message,2);}
    }
    if(state.control && state.control->done.load())
    {
        state.message=state.control->final;
        if(!state.control->ok)Notify("Угломер: "+state.message,2);
        state.control.reset();state.hold=false;state.nextPoll=now;
        link_->FastPoll(now+1500);
    }
    if(state.poll && state.poll->done.load())
    {
        bool found=false;
        if(state.poll->ok)
            for(const auto& line:state.poll->lines)
            {
                level::Status sample;std::string error;
                if(level::ParseStatus(line,sample,&error))
                {state.live.Feed(sample,now);state.reference=sample.reference;state.object=sample.object;found=true;break;}
            }
        if(!found)
        {state.live.Gap(now,level::Error);state.message=state.poll->ok?"Нет полного ответа угломера; проверьте версию прошивки":state.poll->final;}
        state.poll.reset();state.nextPoll=now+(found?100:500);
    }
    const bool connected=HaveStatus() && UsingLevel() && DeviceProfile().Known() && snap_.status.relativeDataCap;
    if(!connected)
    {
        if(!state.live.rows.empty())state.live.Gap(now,level::Missing);
        if(LevelRecording())LevelStopRecording();
    }
    else
    {
        state.live.Expire(now);
        if(state.hold && state.held.generation!=state.live.health.generation)state.hold=false;
        if(!state.poll && !state.control && now>=state.nextPoll && !snap_.busy && !snap_.getActive && !dl_.running &&
           !updater_.Busy() && !ota_.Busy() && !recovery_.Busy() && !PcRecording())
            state.poll=link_->Send("relative status",Origin::Poll,1500);
    }
    if(LevelRecording())
    {
        for(const auto& row:state.live.rows)if(row.id>state.cursor)
        {state.record<<level::CsvRow(row);state.cursor=row.id;}
        state.record.flush();
        if(!state.record){state.message="Ошибка записи на диск";LevelStopRecording();}
    }
    if(state.saveRequested)
    {
        state.saveRequested=false;
        const auto log=state.tab==2 && state.hasImported?state.imported:LiveLog(state);
        auto options=state.options;options.program=WindowTitle();options.when=text::LocalTimeString(std::time(nullptr));
        options.platform=state.tab==2 && state.hasImported?"По журналу; платформа не выводится из имени файла":DeviceProfile().Name();
        const auto frame=offscreen::Capture(1200,760,1.0f,[log,options](float,float h)
        {
            ImGui::TextUnformatted("Угломер: относительный наклон объекта к опоре");
            MutedWrapped("Исходный REL-результат, без презентационного фильтра. Разрывы и изменения REL-нуля не соединяются.");
            DrawCurve(log,options,false,h-ImGui::GetCursorPosY()-S(15));
        });
        const auto parent=text::PathFromUtf8(state.reportParent);
        state.saving=std::async(std::launch::async,[log,options,frame,parent]() ->LevelExportResult
        {
            const auto target=exporttarget::Reserve(parent,"Отчёт_Угломер");
            if(!target)return {false,{},target.error};
            const auto dir=target.dir;const auto path=text::PathToUtf8(dir);
            auto fail=[&](const std::string& message){return LevelExportResult{false,path,message};};
            const auto rgb=offscreen::Rasterize(*frame);
            xlsx::Image image;image.png=png::EncodeRgb(frame->w,frame->h,rgb);image.width=frame->w;image.height=frame->h;image.name="Угломер X/Y";
            if(image.png.empty())return fail("Не удалось подготовить график угломера");
            std::ofstream pngFile(dir/text::PathFromUtf8("График_Угломер.png"),std::ios::binary);
            pngFile.write(reinterpret_cast<const char*>(image.png.data()),static_cast<std::streamsize>(image.png.size()));pngFile.close();
            if(!pngFile)return fail("График не сохранён");
            std::ofstream csv(dir/"Journal_REL.CSV",std::ios::binary);csv<<level::CsvHeader<<'\n';
            for(const auto& row:log.rows)csv<<level::CsvRow(row);csv.close();
            if(!csv)return fail("Журнал отчёта не сохранён");
            const auto book=level::BuildBook(log,options,{image});std::string error;
            if(book.empty() || !xlsx::Write(dir/text::PathFromUtf8("Отчет_Угломер.xlsx"),book,&error))return fail("Книга угломера не сохранена: "+error);
            return {true,path,"Отчёт угломера и график сохранены: "+path};
        });
    }
}

void App::PageLevel()
{
    auto& state=LevelState();const float width=ImGui::GetContentRegionAvail().x;
    const bool online=HaveStatus() && DeviceProfile().Known() && snap_.status.relativeDataCap && snap_.status.instrument=="level";
    const bool busy=DeviceOperationBlock()!=nullptr || state.control;
    auto command=[&](const std::string& cmd)
    {
        state.poll.reset(); // discard any pre-change reply still owned by DeviceLink
        state.control=link_->Send(cmd);state.hold=false;
        state.live.Gap(link_->NowMs(),level::Warmup,"BOUNDARY");state.live.ResetExtrema();
    };
    BeginCard("level-heading");
    {FontScope font(fontBig);ImGui::TextUnformatted("Угломер");}
    MutedWrapped("Относительный наклон объекта к опоре. Отдельный REL-ноль; прежние нули датчиков не меняются.");
    if(online)TextColored(state.live.valid?pal.ok:pal.warn,"%s",state.live.valid?"Пара пригодна":level::QualityText(state.live.health.quality).c_str());
    else MutedWrapped("Для измерения подключите прибор с поддержкой угломера. Журнал REL.CSV / XLSX можно открыть без прибора.");
    if(state.hold)TextColored(pal.warn,"HOLD: показания зафиксированы; качество живой пары проверяется отдельно");
    EndCard();ImGui::Dummy(ImVec2(0,S(10)));
    const char* tabs[]={"Измерение","Наблюдение","Журнал и отчёт"};const int values[]={0,1,2};
    Tabs("level-tabs",&state.tab,tabs,values,3);
    if(state.tab!=2)
    {
        BeginCard("level-config");
        if(state.tab==0)
        {
        ImGui::TextUnformatted("Опора");ImGui::SameLine();ImGui::SetNextItemWidth(S(90));
        ImGui::BeginDisabled(!online || busy);
        const char* heads[]={"Д2","Д3"};int reference=state.reference-2;
        if(ImGui::Combo("##level-reference",&reference,heads,2))command("relative roles "+std::to_string(reference+2)+" "+std::to_string(3-reference));
        ImGui::SameLine();ImGui::Text("Объект: Д%d",state.object);
        if(width>S(850))ImGui::SameLine(0,S(18));
        if(Button("Оси совпадают",BtnKind::Normal))command("relative axes confirm");
        ImGui::SameLine();if(Button("Сброс осей",BtnKind::Normal))command("relative axes reset");
        if(Button("REL-ноль",BtnKind::Primary))command("relative zero");
        ImGui::SameLine();if(Button("Сброс REL",BtnKind::Normal))command("relative reset");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(Button(state.hold?"Снять HOLD":"HOLD",BtnKind::Normal,{},!state.hold && !state.live.valid))
        {
            state.hold=!state.hold;
            if(state.hold){state.held=state.live.current;state.heldAt=link_->NowMs();state.heldRows.assign(state.live.rows.begin(),state.live.rows.end());}
        }
        ImGui::Separator();
        }
        const char* units[]={"Градусы","мм/м","мм на базе"};ImGui::SetNextItemWidth(S(150));ImGui::Combo("##level-units",&state.options.unit,units,3);
        if(state.options.unit==2){ImGui::SameLine();ImGui::SetNextItemWidth(S(120));ImGui::InputDouble("База, мм",&state.options.baselineMm,0,0,"%.1f");}
        if(state.tab==1)
        {
            if(width>S(850))ImGui::SameLine(0,S(18));
            Checkbox("Сглаживание графика (EMA 0,2; прогрев 5 пар)",&state.filter);
            if(width>S(850))ImGui::SameLine(0,S(18));
            if(Button(state.hold?"Снять HOLD":"HOLD",BtnKind::Normal,{},!state.hold && !state.live.valid))
            {
                state.hold=!state.hold;
                if(state.hold){state.held=state.live.current;state.heldAt=link_->NowMs();state.heldRows.assign(state.live.rows.begin(),state.live.rows.end());}
            }
        }
        EndCard();ImGui::Dummy(ImVec2(0,S(10)));
        const auto shown=state.hold?state.held:state.live.current;
        std::array<double,2> display{level::NaN,level::NaN};
        if(state.hold || state.live.valid)display=shown.value;
        if(state.tab==0)
        {
        for(unsigned axis=0;axis<2;axis++)
        {
            if(axis)ImGui::SameLine(0,S(10));
            BeginCard(axis?"level-y":"level-x",ImVec2((width-S(10))/2,compactUi?S(112):S(134)));
            ImGui::Text("%s · %s",axis?"Y":"X",level::Unit(state.options));
            const double value=level::Display(display[axis],state.options);
            {FontScope font(fontBig);if(std::isfinite(value))ImGui::Text("%+.3f",value);else ImGui::TextUnformatted("—");}
            MutedWrapped(state.hold?"Зафиксировано (HOLD)":state.live.valid?"Объект − Опора − REL-ноль":"Нет пригодной свежей пары");
            EndCard();
        }
        ImGui::Dummy(ImVec2(0,S(10)));
        BeginCard("level-tolerance");
        Checkbox("Пользовательский допуск, °",&state.options.tolerance);
        if(state.options.tolerance)
        {
            ImGui::SetNextItemWidth(S(100));ImGui::InputDouble("X##level-limit",&state.options.limit[0],0,0,"%.3f");ImGui::SameLine();
            ImGui::SetNextItemWidth(S(100));ImGui::InputDouble("Y##level-limit",&state.options.limit[1],0,0,"%.3f");
            const auto& live=state.live.current;
            const bool valid=state.live.valid && std::isfinite(state.options.limit[0]) && std::isfinite(state.options.limit[1]) &&
                state.options.limit[0]>=0.01 && state.options.limit[1]>=0.01 && state.options.limit[0]<=90 && state.options.limit[1]<=90;
            if(valid)TextColored(std::fabs(live.value[0])<=state.options.limit[0] && std::fabs(live.value[1])<=state.options.limit[1]?pal.ok:pal.err,"%s",
                std::fabs(live.value[0])<=state.options.limit[0] && std::fabs(live.value[1])<=state.options.limit[1]?"В заданном допуске":"Вне заданного допуска");
            else MutedWrapped("Допуск сейчас не проверяется: нет пригодной пары или неверный предел");
        }
        MutedWrapped("Проверка по живому REL-результату без сглаживания. Аппаратной синхронизации головок нет.");
        if(state.live.valid)
        {
            const auto& s=state.live.current;
            ImGui::Text("Пара %llu / %llu · разброс времени %.1f мс",static_cast<unsigned long long>(s.sequence[0]),static_cast<unsigned long long>(s.sequence[1]),s.skew/1000.0);
        }
        EndCard();
        }
        if(state.tab==1)
        {
            BeginCard("level-observation");
            auto displayNumber=[&](unsigned axis){const double v=level::Display(display[axis],state.options);return std::isfinite(v)?proto::Fixed(v,3):std::string("—");};
            ImGui::Text("X: %s · Y: %s %s%s",displayNumber(0).c_str(),displayNumber(1).c_str(),level::Unit(state.options),state.hold?" · HOLD":"");
            if(state.live.valid)Muted("Опора Д%d → Объект Д%d · разброс времени %.1f мс",state.reference,state.object,state.live.current.skew/1000.0);
            if(state.hold && state.heldCacheAt!=state.heldAt){state.heldCache=LiveLog(state,true);state.heldCacheAt=state.heldAt;}
            const auto& log=state.hold?state.heldCache:LiveDisplayLog(state);
            DrawCurve(log,state.options,state.filter,std::clamp(ImGui::GetContentRegionAvail().y-S(105),S(180),S(310)),&DisplayCurve(state,log,state.filter,false));
            MutedWrapped("Min/Max исходного REL с границы поколения; фильтр влияет только на график:");
            auto number=[](double value){return std::isfinite(value)?proto::Fixed(value,3):std::string("—");};
            ImGui::Text("X %s … %s°; Y %s … %s°",number(state.live.minimum[0]).c_str(),number(state.live.maximum[0]).c_str(),number(state.live.minimum[1]).c_str(),number(state.live.maximum[1]).c_str());
            if(width>S(850))ImGui::SameLine(0,S(18));
            if(Button("Сбросить Min/Max",BtnKind::Normal))state.live.ResetExtrema();
            EndCard();
        }
    }
    else
    {
        BeginCard("level-file-actions");
        if(Button("Открыть REL.CSV / XLSX…",BtnKind::Primary,{},LevelRecording() || LevelBusy()))
        {
            std::string path;if(dialogs::PickFile(path,L"Журнал угломера\0*.csv;*.CSV;*.xlsx\0Все файлы\0*.*\0",L"Открыть журнал угломера"))LevelImportFile(path);
        }
        ImGui::SameLine();
        if(Button("Сохранить отчёт…",BtnKind::Primary,{},LevelRecording() || LevelBusy() || LevelRows()==0))
        {std::string parent=state.folder;if(dialogs::PickFolder(parent,L"Папка для отчёта угломера")){state.folder=parent;LevelSaveReport(parent);}}
        if(LevelRecording())
        {if(Button("Остановить запись на ПК",BtnKind::Primary))LevelStopRecording();}
        else if(Button("Запись угломера на ПК…",BtnKind::Normal,{},!online || !state.live.valid || PcRecording() || dl_.running || updater_.Busy() || ota_.Busy() || recovery_.Busy() || state.control || LevelBusy()))
        {std::string parent=state.folder;if(dialogs::PickFolder(parent,L"Папка для журнала угломера")){state.folder=parent;LevelStartRecording(parent);}}
        if(online && DeviceProfile().IsTab5())
        {
            ImGui::SameLine();
            if(Button(Recording()?"Остановить запись Tab5":"Запись на Tab5",BtnKind::Normal,{},LevelRecording() || PcRecording() || updater_.Busy() || ota_.Busy() || recovery_.Busy() || dl_.running || state.control))
                command(Recording()?"rec off":"rec on");
        }
        MutedWrapped("PC-журнал содержит наблюдаемые пары, а не каждый ответ UART. Полную запись прибора сохраняйте на его носитель. По остановке PC-журнал открывается целиком.");
        if(LevelBusy())MutedWrapped("Чтение / сохранение…");
        if(!state.message.empty())MutedWrapped(Shown(state.message).c_str());
        EndCard();ImGui::Dummy(ImVec2(0,S(10)));
        const auto& log=state.hasImported?state.imported:LiveDisplayLog(state);
        BeginCard("level-log-view");
        MutedWrapped(Shown(log.source).c_str());
        ImGui::Text("Строк: %zu · пригодных пар: %zu · пропущено строк: %zu",log.rows.size(),log.numeric,log.skipped);
        DrawCurve(log,state.options,false,S(280),&DisplayCurve(state,log,false,state.hasImported));
        if(log.rows.size()>12000)MutedWrapped("График прорежён с сохранением экстремумов; разрывы не соединяются. В таблице и журнале все строки.");
        if(ImGui::BeginTable("level-quality",5,ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,ImVec2(0,S(190))))
        {
            ImGui::TableSetupColumn("t, с");ImGui::TableSetupColumn("Событие");ImGui::TableSetupColumn("X, °");ImGui::TableSetupColumn("Y, °");ImGui::TableSetupColumn("Качество");ImGui::TableHeadersRow();
            ImGuiListClipper clipper;clipper.Begin(static_cast<int>(log.rows.size()));
            while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;i++)
            {
                const auto& row=log.rows[static_cast<std::size_t>(i)];ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);ImGui::Text("%.3f",row.seconds);
                ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(row.event.c_str());
                for(int axis=0;axis<2;axis++){ImGui::TableSetColumnIndex(axis+2);if(row.numeric())ImGui::Text("%+.3f",row.sample.value[axis]);else ImGui::TextUnformatted("—");}
                ImGui::TableSetColumnIndex(4);ImGui::TextUnformatted(row.numeric()?"пригодна":level::QualityText(row.sample.quality).c_str());
            }
            ImGui::EndTable();
        }
        EndCard();
    }
}
} // namespace ui
