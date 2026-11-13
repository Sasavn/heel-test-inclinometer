// Native ImGui + real App export, no platform window, COM enumeration or device I/O.
// Artifacts stay under the request folder, never the tracked fixture directories.
#include "TestUtil.hpp"
#include "VodnikData.hpp"
#include "ui/App.hpp"
#include "ui/Fonts.hpp"
#include "ui/Theme.hpp"
#include "core/ExportTarget.hpp"
#include "core/IncliningProtocolDocx.hpp"
#include "core/HeelReport.hpp"
#include "core/Png.hpp"
#include "core/XlsxRead.hpp"
#include <imgui_internal.h>
#include <implot.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <thread>
#include <tuple>

namespace fs = std::filesystem;
namespace
{
std::string Read(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void Write(const fs::path& p, const std::string& s)
{
    std::ofstream out(p, std::ios::binary); out << s; CHECK(static_cast<bool>(out));
}
struct Stand
{
    ui::App& app;
    std::int64_t time = 0;
    ImVec2 size{1280, 800};
    void Frame(ImGuiID activate = 0, ImGuiWindow* focus = nullptr)
    {
        auto& io = ImGui::GetIO(); io.DisplaySize = size; io.DeltaTime = 1.f / 30;
        ImGui::NewFrame();
        if (activate)
        {
            if (focus) ImGui::FocusWindow(focus);
            auto& g = *GImGui;
            g.NavActivateId = g.NavActivateDownId = g.NavActivatePressedId = activate;
            g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
        }
        app.Render(); ImGui::Render();
    }
    void Tick()
    {
        app.Link().Step(time += 50); app.Tick();
    }
    void Wait()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        do { Tick(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        while (app.ProcessBusy() && std::chrono::steady_clock::now() < deadline);
        CHECK(!app.ProcessBusy());
    }
    ImGuiWindow* Content()
    {
        for (auto* w : GImGui->Windows)
            if (w->Active && std::string(w->Name).find("/##formal_content") != std::string::npos) return w;
        return nullptr;
    }
    bool Activate(ImGuiWindow* w, ImGuiID id)
    {
        if (!w) return false;
        // Scroll through the actual rendered form. IDs still identify the same row/cell.
        for (float scroll = 0; scroll <= w->ScrollMax.y + w->InnerRect.GetHeight(); scroll += w->InnerRect.GetHeight() * .75f)
        {
            ImGui::SetScrollY(w, std::min(scroll, w->ScrollMax.y)); Frame(); Frame(id, w);
            if (GImGui->ActiveId == id) return true;
        }
        return false;
    }
    bool Press(ImGuiWindow* w, ImGuiID id, const std::function<bool()>& done)
    {
        if (!w) return false;
        for (float scroll = 0; scroll <= w->ScrollMax.y + w->InnerRect.GetHeight(); scroll += w->InnerRect.GetHeight() * .75f)
        {
            ImGui::SetScrollY(w, std::min(scroll, w->ScrollMax.y)); Frame(); Frame(id, w);
            if (done()) return true;
        }
        return false;
    }
    bool Enter(ImGuiWindow* w, ImGuiID id, const std::string& value)
    {
        if (!Activate(w, id)) return false;
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true); Frame(); io.AddKeyEvent(ImGuiKey_A, true); Frame(); Frame();
        io.AddKeyEvent(ImGuiKey_A, false); Frame(); io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
        io.AddInputCharactersUTF8(value.c_str()); Frame();
        ImGui::ClearActiveID(); Frame();
        return true;
    }
};
ImGuiID ChildId(ImGuiWindow* w, const std::string& scope, const char* label)
{
    return ImHashStr(label, 0, ImHashStr(scope.c_str(), 0, w->ID));
}
void CheckLayout(Stand& st)
{
    for (int n = 0; n < 4; ++n) st.Frame();
    auto* editor = ImGui::FindWindowByName("Данные протокола…");
    CHECK(editor && editor->Active);
    if (!editor) return;
    CHECK(editor->Pos.x >= 0 && editor->Pos.y >= 0);
    CHECK(editor->Pos.x + editor->Size.x <= st.size.x + 1);
    CHECK(editor->Pos.y + editor->Size.y <= st.size.y + 1);
    for (auto* w : GImGui->Windows)
    {
        if (!w->Active || w->Hidden || (w != editor && w != st.Content())) continue;
        CHECK_MSG(w->ScrollMax.x < .5f, "horizontal clipping: %s %.1f", w->Name, w->ScrollMax.x);
        // Content may scroll vertically, but its viewport must fit inside the parent and display.
        CHECK(w->Pos.x + w->Size.x <= st.size.x + 1);
        CHECK(w->Pos.y + w->Size.y <= st.size.y + 1);
    }
}
} // namespace

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    fs::path parent = "C:/Projects/ClaudeExp/inclinometer-feedback/2026-11-13-report-template-745975/ui-test-output";
    for (int i = 1; i + 1 < argc; ++i) if (std::string(argv[i]) == "--out") parent = text::PathFromUtf8(argv[++i]);
    const auto reserved = exporttarget::Reserve(parent, "native-ui-run"); CHECK(static_cast<bool>(reserved));
    if (!reserved) return TestSummary();
    const auto out = reserved.dir;
    fs::create_directories(out / "records"); fs::create_directories(out / "other-records");
    // Existing numerical fixture, with explicit test provenance. Nothing becomes a schema default.
    for (int pos = 0; pos < vodnik::kPositions; ++pos)
        for (int v = 0; v < 2; ++v)
        {
            std::ostringstream csv; csv << "CalcX;CalcY;Ms\n" << std::setprecision(15);
            for (int j = 0; j < 300; ++j) csv << vodnik::ThetaDeg(v, pos) << ";0;" << j * 100 << '\n';
            char name[60]; std::snprintf(name, sizeof(name), "2026-05-04_M%03d_D%d.CSV", pos + 1, v + 2);
            Write(out / "records" / name, csv.str());
        }
    Write(out / "other-records" / "2026-11-13_M001_D2.CSV", "CalcX;CalcY;Ms\n0;0;0\n1;0;1000\n");
    const auto png = png::EncodeRgb(2, 2, std::vector<unsigned char>(12, 255));
    Write(out / "page.png", {reinterpret_cast<const char*>(png.data()), png.size()});
    Write(out / "page.pdf", "%PDF-1.4\n% Test attachment, not a signed certificate\n%%EOF\n");

    ImGui::CreateContext(); ImPlot::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr; io.DisplaySize = {1280, 800};
    ui::ApplyTheme(false, 1); ui::SetupFonts(io, 1); ImPlot::StyleColorsLight();
    unsigned char* px = nullptr; int tw = 0, th = 0; io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th); io.Fonts->SetTexID(1);
    SECTION("font atlas covers every schema label, title and column, including Greek symbols");
    auto glyphs = [&](const std::string& label) {
        const char* at = label.data(); const char* end = at + label.size();
        while (at < end)
        {
            unsigned int codepoint = 0;
            const int consumed = ImTextCharFromUtf8(&codepoint, at, end);
            CHECK_MSG(consumed > 0, "invalid UTF-8 in schema label: %s", label.c_str());
            if (consumed <= 0) break;
            at += consumed;
            if (codepoint < 32) continue;
            CHECK_MSG(ui::fontBody && codepoint <= IM_UNICODE_CODEPOINT_MAX &&
                          ui::fontBody->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr,
                      "schema glyph U+%04X missing: %s", codepoint, label.c_str());
        }
    };
    for (const auto& f : heel::formal::FieldSpecs()) glyphs(f.label);
    for (const auto& t : heel::formal::TableSpecs()) { glyphs(t.title); for (const auto& c : t.columns) glyphs(c); }
    {
        ui::AppOptions opt; opt.threaded = false; opt.useSettingsFile = false; opt.noHardware = true; opt.startDemo = false;
        ui::App app(opt); app.SetProjectAutosaveAllowed(false); app.SetPage(ui::Page::Process);
        Stand st{app};
        app.ProcessOpen(text::PathToUtf8(out / "records")); st.Wait(); st.Frame();
        CHECK(app.ProcessMeasurements() == 11);
        app.ProcessSetMode(0); app.ProcessSetShip(vodnik::kD, vodnik::kMass[0]);
        app.ProcessSetRsInfo(vodnik::kH, vodnik::kLmax, vodnik::kD0);
        app.ProcessSetRsCorrections(vodnik::kDh, vodnik::kDworst);
        app.ProcessSetRsGroupMode(true);
        app.ProcessSetRsGroups({{"TEST group 1", vodnik::kMass[0]}, {"TEST group 2", vodnik::kMass[1]}});
        for (int i = 0; i < 10; ++i) app.ProcessSetRsMove(i, vodnik::kGroup[i], vodnik::kArm[i]);
        st.Tick();
        CHECK_NEAR(app.ProcessRs().hMain, 1.123, .001, "native App numerical fixture");

        SECTION("entire schema, empty values, Unicode/quotes/newlines and attachment pages survive project JSON");
        const auto& fields = heel::formal::FieldSpecs(); const auto& tables = heel::formal::TableSpecs();
        CHECK(!fields.empty() && !tables.empty());
        if (fields.empty() || tables.empty()) return TestSummary();
        auto data = heel::formal::DefaultData();
        const std::string special = "Тестовое судно «UI» \"кавычки\"\nСтрока 2 \\ путь";
        for (const auto& f : fields) data.fields[f.id] = f.id == fields.back().id ? "" : special + " " + f.id;
        for (const auto& t : tables)
        {
            auto& rows = data.tables[t.id]; rows.clear();
            for (int r = 0; r < (t.id == tables.front().id ? 45 : 2); ++r)
            {
                std::vector<std::string> row;
                for (const auto& c : t.columns) row.push_back(special + " " + c + " " + std::to_string(r));
                rows.push_back(row);
            }
        }
        data.attachments = {{"scales_verification", text::PathToUtf8(out / "page.png"), "TEST поверка, страница 1"},
                            {"scales_verification", text::PathToUtf8(out / "page.png"), "TEST поверка, страница 2"},
                            {"tape_calibration", text::PathToUtf8(out / "page.pdf"), "TEST акт PDF"}};
        app.ProcessSetFormalProtocol(data);
        const auto project = out / "Редактор.krenomer";
        CHECK(app.ProcessSaveProject(text::PathToUtf8(project)));
        proj::Project restored; std::string err;
        CHECK(proj::Load(project, restored, &err));
        CHECK(heel::formal::ToJson(restored.formalProtocol) == heel::formal::ToJson(data));
        CHECK(app.ProcessOpenProject(text::PathToUtf8(project))); st.Wait();
        CHECK(heel::formal::ToJson(app.ProcessFormalProtocol()) == heel::formal::ToJson(data));

        SECTION("real editor entry from report menu and native text input without truncation");
        app.ProcessShowSaveMenu(1); st.Frame(); st.Frame();
        ImGuiWindow* menu = nullptr;
        for (auto* w : GImGui->Windows) if (w->Active && (w->Flags & ImGuiWindowFlags_Popup)) menu = w;
        CHECK(menu != nullptr);
        if (menu) st.Frame(menu->GetID("Данные протокола…"), menu);
        st.Frame(); CHECK(app.ProcessFormalProtocolOpen());
        auto* content = st.Content(); CHECK(content != nullptr);
        const auto& first = fields.front(); app.ProcessFormalProtocolSection(first.section); st.Frame();
        content = st.Content();
        std::string longText;
        for (int n = 0; n < 150; ++n) longText += "Длинное имя \"UI\"; ";
        if (content) CHECK(st.Enter(content, ChildId(content, first.id, "##value"), longText));
        CHECK(app.ProcessFormalProtocol().fields.at(first.id) == longText);
        SECTION("native table add, cell input, row removal, attachment input and multiple pages");
        const auto& table = tables.front();
        app.ProcessFormalProtocolSection(table.section); st.Frame(); st.Frame(); content = st.Content();
        const auto nrows = app.ProcessFormalProtocol().tables.at(table.id).size();
        if (content)
        {
            CHECK(st.Press(content, ChildId(content, table.id, "Добавить строку"), [&] {
                return app.ProcessFormalProtocol().tables.at(table.id).size() == nrows + 1;
            }));
            int row = 0, col = 0;
            const auto scope = ImHashData(&row, sizeof(row), ImHashStr(table.id.c_str(), 0, content->ID));
            const auto cellId = ImHashStr("##cell", 0, ImHashData(&col, sizeof(col), scope));
            CHECK(st.Enter(content, cellId, special));
            CHECK(app.ProcessFormalProtocol().tables.at(table.id)[0][0] == special);
            CHECK(st.Press(content, ImHashStr("Удалить строку", 0, scope), [&] {
                return app.ProcessFormalProtocol().tables.at(table.id).size() == nrows;
            }));
        }
        app.ProcessFormalProtocolSection(0); st.Frame(); st.Frame(); content = st.Content();
        const auto nattach = app.ProcessFormalProtocol().attachments.size();
        if (content)
        {
            CHECK(st.Press(content, content->GetID("Добавить вложение"), [&] {
                return app.ProcessFormalProtocol().attachments.size() == nattach + 1;
            }));
            int index = static_cast<int>(nattach);
            const auto scope = ImHashData(&index, sizeof(index), content->ID);
            CHECK(app.ProcessFormalProtocol().attachments.back().slot == "scales_verification");
            CHECK(st.Enter(content, ImHashStr("##caption", 0, scope), "Тестовая поверка \"UI\"\nстраница 3"));
            const auto advanced = ImHashStr("Дополнительно", 0, scope);
            CHECK(st.Press(content, advanced, [&] { return content->StateStorage.GetInt(advanced) != 0; }));
            CHECK(st.Enter(content, ImHashStr("##path", 0, advanced), text::PathToUtf8(out / "page.png")));
            CHECK(app.ProcessFormalProtocol().attachments.back().path == text::PathToUtf8(out / "page.png"));
            CHECK(st.Press(content, ImHashStr("##place", 0, scope), [&] { return GImGui->OpenPopupStack.Size > 0; }));
            ImRect placeAnchor = content->NavRectRel[ImGuiNavLayer_Main]; placeAnchor.Translate(content->Pos);
            ImGui::ScrollToRect(content, placeAnchor, ImGuiScrollFlags_KeepVisibleEdgeY); st.Frame();
            ImGuiWindow* placesPopup = nullptr;
            for (auto* w : GImGui->Windows) if (w->Active && (w->Flags & ImGuiWindowFlags_Popup)) placesPopup = w;
            CHECK(placesPopup != nullptr);
            if (placesPopup)
            {
                io.AddMousePosEvent(placesPopup->Pos.x + ImGui::GetStyle().WindowPadding.x + 20,
                                    placesPopup->DC.CursorStartPos.y + 4.5f * (ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y));
                st.Frame(); io.AddMouseButtonEvent(0, true); st.Frame(); io.AddMouseButtonEvent(0, false); st.Frame();
                io.AddMousePosEvent(-100, -100); st.Frame();
            }
            CHECK(app.ProcessFormalProtocol().attachments.back().slot == "organization_logo");
            CHECK(st.Press(content, ImHashStr("Удалить вложение", 0, scope), [&] {
                return app.ProcessFormalProtocol().attachments.size() == nattach;
            }));
        }
        app.ProcessShowFormalProtocol(false); st.Frame(); app.ProcessShowFormalProtocol(true); st.Frame();
        CHECK(app.ProcessFormalProtocol().fields.at(first.id) == longText);

        SECTION("Russian controls serialize center mode and inventory confirmation codes");
        for (const auto& choice : std::vector<std::tuple<std::string, std::string, std::string>>{
                 {"center_mode", "Ручной ввод ЦТ", "manual"}, {"center_mode", "По гидростатике", "hydro"},
                 {"center_mode", "Не выбрано", ""},
                 {"excess_inventory_complete", "Опись проверена", "yes"}, {"excess_inventory_complete", "Опись не подтверждена", ""},
                 {"missing_inventory_complete", "Опись проверена", "yes"}, {"missing_inventory_complete", "Опись не подтверждена", ""}})
        {
            const auto& [id, label, value] = choice;
            const auto f = std::find_if(fields.begin(), fields.end(), [&](const auto& x) { return x.id == id; });
            CHECK(f != fields.end());
            if (f == fields.end()) continue;
            app.ProcessFormalProtocolSection(f->section); st.Frame(); st.Frame(); content = st.Content();
            CHECK(content != nullptr);
            if (!content) continue;
            CHECK(st.Press(content, ChildId(content, id, "##value"), [&] { return GImGui->OpenPopupStack.Size > 0; }));
            // Programmatic activation may target a clipped item. Bring the real item rectangle
            // into view before the popup's second (auto-size) frame.
            ImRect anchor = content->NavRectRel[ImGuiNavLayer_Main]; anchor.Translate(content->Pos);
            ImGui::ScrollToRect(content, anchor, ImGuiScrollFlags_KeepVisibleEdgeY);
            st.Frame();
            ImGuiWindow* popup = nullptr;
            for (auto* w : GImGui->Windows) if (w->Active && (w->Flags & ImGuiWindowFlags_Popup)) popup = w;
            CHECK(popup != nullptr);
            if (popup)
            {
                const int option = value == "hydro" ? 2 : value == "manual" || value == "yes" ? 1 : 0;
                auto& mouse = ImGui::GetIO();
                const float x = popup->Pos.x + ImGui::GetStyle().WindowPadding.x + 20;
                const float y = popup->DC.CursorStartPos.y + (option + .5f) * (ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y);
                mouse.AddMousePosEvent(x, y); st.Frame();
                mouse.AddMouseButtonEvent(0, true); st.Frame();
                mouse.AddMouseButtonEvent(0, false); st.Frame();
                mouse.AddMousePosEvent(-100, -100); st.Frame();
            }
            st.Frame();
            CHECK(app.ProcessFormalProtocol().fields.at(id) == value);
        }
        SECTION("17 sections at 1024 and 1280: no horizontal clipping; final table rows scroll into view");
        for (const ImVec2 size : {ImVec2(1024, 768), ImVec2(1280, 800)})
        {
            st.size = size;
            for (int section = 0; section < 17; ++section)
            {
                app.ProcessFormalProtocolSection(section); CheckLayout(st);
                if (auto* w = st.Content()) { ImGui::SetScrollY(w, w->ScrollMax.y); CheckLayout(st); }
            }
        }
        app.ProcessShowFormalProtocol(false); st.size = {1280, 800}; st.Frame();
        CHECK(app.ProcessSaveProject(text::PathToUtf8(project)));
        CHECK(proj::Load(project, restored, &err));
        CHECK(restored.formalProtocol.fields.at(first.id) == longText);

        SECTION("App::ProcessSaveReport writes full DOCX in both Excel modes and reserves fresh sets");
        app.ProcessSetReportFolder(text::PathToUtf8(out)); app.ProcessSetReportName("UI-native-export");
        std::string previous;
        for (bool full : {false, true})
        {
            app.ProcessSetReportFull(full); app.ProcessSaveReport(); st.Tick();
            // Mutate after the async snapshot. Export must keep the preceding values.
            auto changed = app.ProcessFormalProtocol(); changed.fields[first.id] = "AFTER_ASYNC_SNAPSHOT";
            app.ProcessSetFormalProtocol(changed); st.Wait();
            const auto dir = text::PathFromUtf8(app.ProcessReportDir());
            CHECK(app.ProcessReportDir() != previous); previous = app.ProcessReportDir();
            const auto docx = dir / text::PathFromUtf8(heel::formal::kDocxName);
            CHECK(fs::exists(docx)); CHECK(fs::exists(dir / text::PathFromUtf8(heel::kReportName)));
            if (fs::exists(docx)) { const auto bytes = Read(docx); CHECK(bytes.size() > 1000 && bytes.substr(0, 2) == "PK");
                std::string documentXml;
                CHECK(xlsxread::detail::Extract(bytes, "word/document.xml", documentXml));
                CHECK(documentXml.find("AFTER_ASYNC_SNAPSHOT") == std::string::npos);
                CHECK(documentXml.find("Длинное имя") != std::string::npos);
                for (std::size_t section = 1; section < heel::formal::SectionNames().size(); ++section) CHECK(documentXml.find(heel::formal::SectionNames()[section]) != std::string::npos);
            }
            const auto workbook = Read(dir / text::PathFromUtf8(heel::kReportName));
            std::string workbookXml;
            CHECK(xlsxread::detail::Extract(workbook, "xl/workbook.xml", workbookXml));
            CHECK((workbookXml.find("Расчёт по шагам") != std::string::npos) == full);
            auto bundle = dir / text::PathFromUtf8(heel::formal::kDocxName); bundle.replace_extension(); bundle += "_files";
            const auto manifest = Read(bundle / "manifest.txt");
            CHECK(!manifest.empty());
            const auto entry = manifest.find(std::string("legacy | ") + heel::kReportName + " | ");
            CHECK(entry != std::string::npos);
            if (entry != std::string::npos)
                CHECK(manifest.substr(entry, manifest.find('\n', entry) - entry).find("ОТСУТСТВУЕТ") == std::string::npos);
            bool copiedWorkbook = false;
            if (fs::is_directory(bundle)) for (const auto& f : fs::directory_iterator(bundle))
                if (f.path().extension() == ".xlsx") copiedWorkbook |= Read(f.path()) == workbook;
            CHECK(copiedWorkbook);
            CHECK(fs::exists(dir / text::PathFromUtf8(heel::PlotFileName("График", "Нос"))));
            changed.fields[first.id] = longText; app.ProcessSetFormalProtocol(changed);
        }
        SECTION("DOCX failure reports a partial set while preserving the workbook and PNG files");
        Write(out / "empty.png", "");
        auto badData = app.ProcessFormalProtocol();
        badData.attachments.push_back({"scales_verification", text::PathToUtf8(out / "empty.png"), "TEST unreadable empty attachment"});
        app.ProcessSetFormalProtocol(badData); app.ProcessSaveReport(); st.Wait();
        const auto partial = text::PathFromUtf8(app.ProcessReportDir());
        CHECK(fs::exists(partial / text::PathFromUtf8(heel::kReportName)));
        CHECK(fs::exists(partial / text::PathFromUtf8(heel::PlotFileName("График", "Нос"))));
        bool partialNotice = false;
        for (const auto& toast : app.Toasts()) partialNotice |= toast.kind == 2 && toast.text.find("не полностью") != std::string::npos;
        CHECK(partialNotice);
        app.ProcessSetFormalProtocol(data);
        SECTION("autosave sees formal edits, old projects and a new experiment start blank");
        app.SetProjectAutosaveAllowed(true);
        auto changed = app.ProcessFormalProtocol(); changed.fields[first.id] = "Автосохранение UI";
        app.ProcessSetFormalProtocol(changed);
        for (int n = 0; n < 100; ++n) st.Tick();
        CHECK(proj::Load(project, restored, &err));
        CHECK(restored.formalProtocol.fields.at(first.id) == "Автосохранение UI");
        app.SetProjectAutosaveAllowed(false);
        app.ProcessNewProject(); st.Wait();
        const auto defaults = heel::formal::ToJson(heel::formal::DefaultData());
        CHECK(heel::formal::ToJson(app.ProcessFormalProtocol()) == defaults);
        app.ProcessSetFormalProtocol(data);
        app.ProcessOpen(text::PathToUtf8(out / "other-records")); st.Wait();
        CHECK(heel::formal::ToJson(app.ProcessFormalProtocol()) == defaults);
        proj::Project old; old.folder = text::PathToUtf8(out / "records");
        auto oldText = proj::ToJson(old);
        // Strip the additive property to reproduce the actual pre-feature format.
        const auto begin = oldText.find(",\n  \"formal_protocol\":");
        const auto end = oldText.find(",\n  \"result\":", begin);
        CHECK(begin != std::string::npos && end != std::string::npos);
        if (begin != std::string::npos && end != std::string::npos) oldText.erase(begin, end - begin);
        Write(out / "old.krenomer", oldText);
        app.ProcessSetFormalProtocol(data);
        CHECK(app.ProcessOpenProject(text::PathToUtf8(out / "old.krenomer"))); st.Wait();
        CHECK(heel::formal::ToJson(app.ProcessFormalProtocol()) == defaults);
        CHECK(app.Link().Snapshot().cmdsTx == 0);
    }
    ImPlot::DestroyContext(); ImGui::DestroyContext();
    std::printf("UI artifacts: %s\n", text::PathToUtf8(out).c_str());
    return TestSummary();
}
