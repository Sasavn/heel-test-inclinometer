// Стенд снимков экрана без окна и видеокарты: весь интерфейс (ui::App) с прибором-имитатором в модельном времени,
// кадры ImGui растеризуются программно в PNG (растеризатор и PngWriter — из стенда ui_smoke ShagomerPCModule).
// Сценарий проходит все страницы и основные состояния: нет связи, измерение (покой / качка / запись, датчик пропал,
// АКБ), тёмная тема, малый экран, запись на ПК (прошивка 1.4 — недоступна; идёт, обрыв связи, готово с .xlsx),
// диагностика, настройки (ошибка ввода, смена адреса), файлы (скачивание идёт / готово), перепрошивка (идёт /
// готово), терминал. Заодно — проверки: индексы отрисовки в пределах, кадр не пустой.
//
// Запуск: ui_shots [--out ПАПКА] (по умолчанию ./shots)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <functional>

#include "PngWriter.hpp"
#include "RulesMockups.hpp"
#include "TestUtil.hpp"
#include "VodnikData.hpp"
#include "core/FileBusy.hpp"
#include "core/TextUtil.hpp"
#include "fw/Firmware.hpp"
#include "ui/App.hpp"
#include "ui/Fonts.hpp"
#include "ui/Theme.hpp"

namespace fs = std::filesystem;

namespace
{

// Папка для файлов стенда: KRENOMER_SHOTS_DIR, иначе %TEMP%. На снимках вместо неё — «D:\Кренование» (ShownPaths).
fs::path ShotsWorkDir()
{
    if (const wchar_t* e = _wgetenv(L"KRENOMER_SHOTS_DIR"); e && *e)
        return fs::path(e);
    return fs::temp_directory_path();
}

// Пути на снимках: настоящие папки (стенд, %TEMP%, исходники, профиль пользователя) показываются как
// «D:\Кренование\…» (или KRENOMER_SHOTS_SHOW_DIR) — снимки идут в публичный README. Файлы пишутся по настоящим путям.
std::vector<std::pair<std::string, std::string>> ShownPaths(const fs::path& work)
{
    std::wstring root = L"D:\\Кренование";
    if (const wchar_t* e = _wgetenv(L"KRENOMER_SHOTS_SHOW_DIR"); e && *e)
        root = e;
    const std::string shown = text::PathToUtf8(fs::path(root));
    std::vector<std::pair<std::string, std::string>> map;
    auto add = [&](const fs::path& p, const char* sub) {
        std::error_code ec;
        std::string s = text::PathToUtf8(fs::weakly_canonical(p, ec).make_preferred());
        while (s.size() > 3 && (s.back() == '\\' || s.back() == '/'))
            s.pop_back();
        if (s.size() > 3)
            map.push_back({s, shown + sub});
    };
    add(work, "");
    add(fs::temp_directory_path(), "");
    add(fs::path(KRENOMER_SOURCE_DIR) / "..", "\\diplom_project");
    for (const auto& [var, sub] : {std::pair{L"APPDATA", "\\Настройки"}, std::pair{L"LOCALAPPDATA", "\\Программы"},
                                   std::pair{L"USERPROFILE", "\\Пользователь"}})
        if (const wchar_t* e = _wgetenv(var); e && *e)
            add(fs::path(e), sub);
    return map;
}

const unsigned char* g_tex = nullptr;
int g_texW = 0, g_texH = 0;
std::string g_out = "shots";
std::string g_mockOut; // --mock-out: куда класть макеты (по умолчанию — туда же, куда снимки)
std::string g_sizesOut; // --sizes-out: основные страницы при 1280×720, 1366×768, 1920×1080 (сюда же — sizes.txt)
bool g_sizesStrict = false; // --sizes-strict: замечания раскладки этих размеров — ошибки
int g_shots = 0;
long long g_badIdx = 0;

// Программная растеризация ImDrawData -> RGB (Shot() из ShagomerPCModule/tests/ui_smoke/UiProbe.hpp).
bool Rasterize(const std::string& path, const ImVec4& bg)
{
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd)
        return false;
    const int W = static_cast<int>(dd->DisplaySize.x), H = static_cast<int>(dd->DisplaySize.y);
    std::vector<float> img(static_cast<std::size_t>(W) * H * 3);
    for (int i = 0; i < W * H; i++)
    {
        img[static_cast<std::size_t>(i) * 3 + 0] = bg.x;
        img[static_cast<std::size_t>(i) * 3 + 1] = bg.y;
        img[static_cast<std::size_t>(i) * 3 + 2] = bg.z;
    }
    for (ImDrawList* dl : dd->CmdLists)
        for (const ImDrawCmd& cmd : dl->CmdBuffer)
        {
            if (cmd.UserCallback)
                continue;
            const int cx0 = std::max(0, static_cast<int>(cmd.ClipRect.x)), cy0 = std::max(0, static_cast<int>(cmd.ClipRect.y));
            const int cx1 = std::min(W, static_cast<int>(std::ceil(cmd.ClipRect.z)));
            const int cy1 = std::min(H, static_cast<int>(std::ceil(cmd.ClipRect.w)));
            for (unsigned int k = 0; k + 2 < cmd.ElemCount; k += 3)
            {
                const ImDrawVert* v[3];
                bool bad = false;
                for (int j = 0; j < 3; j++)
                {
                    const unsigned int idx = dl->IdxBuffer[static_cast<int>(cmd.IdxOffset + k + j)] + cmd.VtxOffset;
                    if (idx >= static_cast<unsigned int>(dl->VtxBuffer.Size))
                    {
                        bad = true;
                        break;
                    }
                    v[j] = &dl->VtxBuffer[static_cast<int>(idx)];
                }
                if (bad)
                {
                    g_badIdx++;
                    continue;
                }
                const float x0 = v[0]->pos.x, y0 = v[0]->pos.y, x1 = v[1]->pos.x, y1 = v[1]->pos.y, x2 = v[2]->pos.x,
                            y2 = v[2]->pos.y;
                const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                if (std::abs(area) < 1e-6f)
                    continue;
                const int bx0 = std::max(cx0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
                const int by0 = std::max(cy0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
                const int bx1 = std::min(cx1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
                const int by1 = std::min(cy1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));
                for (int py = by0; py < by1; py++)
                    for (int px = bx0; px < bx1; px++)
                    {
                        const float sx = px + 0.5f, sy = py + 0.5f;
                        const float w0 = ((x1 - sx) * (y2 - sy) - (x2 - sx) * (y1 - sy)) / area;
                        const float w1 = ((x2 - sx) * (y0 - sy) - (x0 - sx) * (y2 - sy)) / area;
                        const float w2 = 1.f - w0 - w1;
                        if (w0 < 0.f || w1 < 0.f || w2 < 0.f)
                            continue;
                        const float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x;
                        const float t = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
                        const int tx = std::clamp(static_cast<int>(u * g_texW), 0, g_texW - 1);
                        const int ty = std::clamp(static_cast<int>(t * g_texH), 0, g_texH - 1);
                        const unsigned char* texel = g_tex + (static_cast<std::size_t>(ty) * g_texW + tx) * 4;
                        float c[4];
                        for (int ch = 0; ch < 4; ch++)
                        {
                            const float a = static_cast<float>((v[0]->col >> (ch * 8)) & 0xFF);
                            const float b = static_cast<float>((v[1]->col >> (ch * 8)) & 0xFF);
                            const float d = static_cast<float>((v[2]->col >> (ch * 8)) & 0xFF);
                            c[ch] = (w0 * a + w1 * b + w2 * d) / 255.f * (texel[ch] / 255.f);
                        }
                        float* dst = &img[(static_cast<std::size_t>(py) * W + px) * 3];
                        for (int ch = 0; ch < 3; ch++)
                            dst[ch] = c[ch] * c[3] + dst[ch] * (1.f - c[3]);
                    }
            }
        }
    std::vector<unsigned char> rgb(img.size());
    long long nonBg = 0;
    for (std::size_t i = 0; i < img.size(); i++)
    {
        rgb[i] = static_cast<unsigned char>(std::clamp(img[i], 0.f, 1.f) * 255.f + 0.5f);
        if (i % 3 == 0)
            nonBg += std::fabs(img[i] - bg.x) > 0.02f;
    }
    CHECK_MSG(nonBg > W * H / 50, "%s: кадр почти пустой", path.c_str());
    return uismoke::png::WriteRgb(path, W, H, rgb);
}

struct Stand
{
    ui::App* app = nullptr;
    std::int64_t t = 0;
    ImVec2 size{1280, 800};

    void Frame()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = size;
        io.DeltaTime = 1.f / 30.f;
        ImGui::NewFrame();
        app->Render();
        ImGui::Render();
    }

    // Модельное время: связь — шагами по 10 мс, логика окна — каждые 50 мс, кадр — раз в секунду (окна ImGui
    // живут, всплывающие подсказки и модальные окна открываются).
    void Run(std::int64_t ms)
    {
        for (std::int64_t e = t + ms; t < e; t += 10)
        {
            app->Link().Step(t);
            if (t % 50 == 0)
                app->Tick();
            if (t % 1000 == 0)
                Frame();
        }
    }

    // fitPage — страница помещается без вертикальной прокрутки. Горизонтальной прокрутки не должно быть нигде,
    // кроме журналов (терминал, перепрошивка), где длинные строки прокручиваются.
    void Shot(const char* name, bool fitPage = false)
    {
        app->Tick();
        for (int i = 0; i < 6; i++) // раскладка ImGui устанавливается за пару кадров (полосы прокрутки, автовысота)
            Frame();
        for (ImGuiWindow* w : GImGui->Windows)
        {
            if (!w->Active || w->Hidden)
                continue;
            const std::string wn = w->Name;
            if (w->ScrollMax.x > 0.5f && wn.find("##termlog") == std::string::npos &&
                wn.find("##fwlog") == std::string::npos)
                CHECK_MSG(false, "%s: горизонтальная прокрутка %.0f px в окне «%s»", name, w->ScrollMax.x, wn.c_str());
            // «Обработка» с 1.7.1 прокручивается целиком (таблицы — во всю высоту): вертикальная прокрутка страницы
            // допустима, горизонтальная — нет
            if (fitPage && app->GetPage() != ui::Page::Process && wn.find("##page") != std::string::npos &&
                wn.find("/##page") != std::string::npos && wn.rfind('/') == wn.find("/##page"))
                CHECK_MSG(w->ScrollMax.y <= 0.5f, "%s: страница не помещается по высоте (%.0f px)", name, w->ScrollMax.y);
        }
        const std::string path = (fs::path(g_out) / (std::string(name) + ".png")).string();
        Save(path, name);
    }

    // Замечания раскладки (без проверок): горизонтальная прокрутка, страница не помещается, — строкой.
    std::string Survey()
    {
        app->Tick();
        for (int i = 0; i < 6; i++)
            Frame();
        std::string out;
        for (ImGuiWindow* w : GImGui->Windows)
        {
            if (!w->Active || w->Hidden)
                continue;
            const std::string wn = w->Name;
            if (w->ScrollMax.x > 0.5f && wn.find("##termlog") == std::string::npos && wn.find("##fwlog") == std::string::npos)
                out += "  гориз. прокрутка " + std::to_string(static_cast<int>(w->ScrollMax.x)) + " px: " + wn + "\n";
            if (app->GetPage() != ui::Page::Process && wn.find("/##page") != std::string::npos &&
                wn.rfind('/') == wn.find("/##page") && w->ScrollMax.y > 0.5f)
                out += "  страница длиннее окна на " + std::to_string(static_cast<int>(w->ScrollMax.y)) + " px\n";
        }
        return out;
    }

    void Save(const std::string& path, const char* name)
    {
        const auto t0 = std::chrono::steady_clock::now();
        CHECK_MSG(Rasterize(path, ui::pal.bg), "не записан %s", path.c_str());
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %s.png (%.0f×%.0f, %.1f с)\n", name, size.x, size.y, s);
        g_shots++;
    }
};

std::string FirmwareImage()
{
    // Настоящий образ из сборки прошивки, иначе — синтетический с той же строкой версии
    const fs::path src = fs::path(KRENOMER_SOURCE_DIR);
    for (const char* rel : {"../BWM427_Inclinometer/build_pc/BWM427_Inclinometer.bin",
                            "../BWM427_Inclinometer/build/BWM427_Inclinometer.bin"})
    {
        std::error_code ec;
        if (fs::is_regular_file(src / rel, ec))
            return text::PathToUtf8(fs::weakly_canonical(src / rel, ec));
    }
    std::vector<std::uint8_t> d(80000, 0xFF);
    const std::uint32_t sp = 0x20020000u, rst = 0x08000199u;
    std::memcpy(d.data(), &sp, 4);
    std::memcpy(d.data() + 4, &rst, 4);
    const std::string tag = "BWM427 inclinometer firmware v1.5, build Oct  8 2026 17:33:25";
    std::memcpy(d.data() + 0x2000, tag.c_str(), tag.size() + 1);
    const fs::path p = ShotsWorkDir() / L"bwm427-fw-1.5.bin";
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    return text::PathToUtf8(p);
}

// Опыт кренования для «Обработки»: 8 переносов груза P = 15 т (D = 1500 т, h = 0,8 м), плечи ±2,25 / ±4,5 м и
// два возврата в ноль; файлы прибора 1.4 (Д2 — нос, Д3 — корма), 2 мин по 10 Гц: переход от прежнего крена с
// затухающими колебаниями, лёгкая качка, шум датчика 0,01°. Крен — по оси X.
const double kHeelArm[8] = {2.25, 4.5, 2.25, 0.0, -2.25, -4.5, -2.25, 0.0};

void MakeHeelSet(const fs::path& dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto dec = [](double v, int d) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.*f", d, v);
        for (char* p = b; *p; p++)
            if (*p == '.')
                *p = ',';
        return std::string(b);
    };
    unsigned seed = 1;
    auto noise = [&] { // ±0,01° (шаг датчика)
        seed = seed * 1103515245u + 12345u;
        return ((seed >> 16) % 3) * 0.01 - 0.01;
    };
    double prev[2] = {0.0, 0.0};
    for (int m = 0; m < 8; m++)
    {
        const double theta = std::atan(15.0 * kHeelArm[m] / (1500.0 * 0.8)) * 180.0 / 3.14159265358979;
        for (int s = 0; s < 2; s++)
        {
            const double target = theta * (s ? 1.012 : 1.0) + (s ? -0.006 : 0.004);
            std::string csv = "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";
            const int start = 10 * 3600 + m * 240;
            for (int k = 0; k < 1200; k++)
            {
                const double t = k / 10.0;
                const double x = target + (prev[s] - target) * std::exp(-t / 14.0) * std::cos(t * 0.62) +
                                 0.03 * std::sin(t * 0.71 + m + s) + noise();
                const double y = 0.12 * (s ? -1 : 1) + 0.02 * std::sin(t * 0.5) + noise();
                const double rx = std::round(x * 100.0) / 100.0, ry = std::round(y * 100.0) / 100.0;
                const int sec = start + k / 10;
                char tm[48];
                std::snprintf(tm, sizeof(tm), "08.10.2026;%02d:%02d:%02d;", sec / 3600, (sec / 60) % 60, sec % 60);
                csv += tm + dec(rx, 2) + ";" + dec(ry, 2) + ";0,000;0,000;" + dec(rx, 3) + ";" + dec(ry, 3) + ";" +
                       dec(12.6 - 0.05 * m - k * 0.00002, 1) + ";" + std::to_string(k * 100 + (s ? 37 : 12)) + "\n";
            }
            prev[s] = target;
            char name[64];
            std::snprintf(name, sizeof(name), "2026-10-08_M%03d_D%d.CSV", m + 1, s + 2);
            std::ofstream(dir / name, std::ios::binary).write(csv.data(), static_cast<std::streamsize>(csv.size()));
        }
    }
}

std::string ReadText(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Опыт кренования с двумя весками (tests/VodnikData.hpp) как «файл = замер»: 11 замеров × посты Д2 и Д3, крен
// постоянный 30 с (подмешивание ±0,00045° — среднее записанных с 0,001° равно истинному).
void MakeVodnikSet(const fs::path& dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (int pos = 0; pos < vodnik::kPositions; pos++)
        for (int v = 0; v < 2; v++)
        {
            const double th = vodnik::ThetaDeg(v, pos);
            std::string csv = "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";
            for (int k = 0; k < 300; k++)
            {
                const double x = th + ((k % 10) - 4.5) * 0.0001;
                const int sec = 10 * 3600 + pos * 600 + k / 10;
                char line[160];
                std::snprintf(line, sizeof(line), "04.05.2026;%02d:%02d:%02d;%.2f;0.00;0.000;0.000;%.3f;0.000;12.4;%d\n",
                              sec / 3600, (sec / 60) % 60, sec % 60, x, x, k * 100);
                csv += line;
            }
            // десятичная запятая (как в файлах прибора 1.4+), дата остаётся с точками
            for (std::size_t i = 0; i < csv.size(); i++)
                if (csv[i] == '.' && csv.compare(i, 6, ".05.20") != 0 && csv.compare(i, 6, ".2026;") != 0)
                    csv[i] = ',';
            char name[64];
            std::snprintf(name, sizeof(name), "2026-05-04_M%03d_D%d.CSV", pos + 1, v + 2);
            std::ofstream(dir / name, std::ios::binary).write(csv.data(), static_cast<std::streamsize>(csv.size()));
        }
}

} // namespace

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    for (int i = 1; i + 1 < argc; i++)
        if (std::strcmp(argv[i], "--out") == 0)
            g_out = argv[i + 1];
        else if (std::strcmp(argv[i], "--mock-out") == 0)
            g_mockOut = argv[i + 1];
        else if (std::strcmp(argv[i], "--sizes-out") == 0)
            g_sizesOut = argv[i + 1];
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--sizes-strict") == 0)
            g_sizesStrict = true;
    // Отчёт программы по проекту: KRENOMER_REPORT_PROJECT — проект (*.krenomer), KRENOMER_REPORT_SHOW — «папка=как
    // показывать» (пути в отчёте). Книга и картинки — в папку записей, как кнопкой «Сохранить отчёт»; снимки — в --out.
    // Через переменные окружения: пути с кириллицей в argv на Windows не доходят в UTF-8.
    std::string reportProject, reportShowFrom, reportShowAs;
    if (const wchar_t* e = _wgetenv(L"KRENOMER_REPORT_PROJECT"); e && *e)
        reportProject = text::Narrow(e);
    if (const wchar_t* e = _wgetenv(L"KRENOMER_REPORT_SHOW"); e && *e)
    {
        const std::string v = text::Narrow(e);
        const auto eq = v.find('=');
        if (eq != std::string::npos)
        {
            reportShowFrom = v.substr(0, eq);
            reportShowAs = v.substr(eq + 1);
        }
    }
    std::error_code ec;
    fs::create_directories(g_out, ec);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 800);
    io.IniFilename = nullptr;
    ui::ApplyTheme(false, 1.f);
    ImPlot::StyleColorsLight();
    ui::SetupFonts(io, 1.f);
    unsigned char* px = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);
    g_tex = px;
    g_texW = tw;
    g_texH = th;
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    std::printf("атлас шрифтов %d × %d\n", tw, th);
    CHECK(ui::fontBody && ui::fontBody != ui::fontBig); // шрифты из ресурсов, не встроенный ImGui
    CHECK(tw <= 2048 && th <= 2048);

    if (!reportProject.empty())
    {
        SECTION("отчёт по проекту");
        ui::AppOptions o{false, false, false};
        o.noHardware = true;
        ui::App app(o);
        app.SetProjectAutosaveAllowed(false);
        if (!reportShowFrom.empty())
            app.SetShownPaths({{reportShowFrom, reportShowAs}});
        Stand st{&app};
        app.SetPage(ui::Page::Process);
        st.Run(300);
        auto waitBusy = [&] {
            for (int i = 0; i < 3000 && app.ProcessBusy(); i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                st.Run(50);
            }
        };
        waitBusy();
        CHECK(app.ProcessOpenProject(reportProject));
        waitBusy();
        st.Run(1000);
        std::printf("h от начального %.5f (МНК %.5f), основной %.5f, h_k %.5f\n", app.ProcessPositionsSummary().h,
                    app.ProcessPositionsSummary().ls.h, app.ProcessRs().hMain, app.ProcessRs().r.hk);
        for (const auto& q : app.ProcessPositions())
            std::printf("  %7.2f…%7.2f  плечо %7.1f  Д2 %+8.4f  Д3 %+8.4f\n", q.t0, q.t1, q.arm, q.Level(0), q.Level(1));
        st.Shot("report_process");
        app.ProcessSaveReport();
        st.Run(100);
        waitBusy();
        st.Run(300);
        return TestSummary();
    }

    // 1. Нет связи (настоящий поиск COM-порта; прибора на этом ПК нет)
    SECTION("нет связи");
    {
        ui::AppOptions o{false, false, false};
        o.noHardware = true; // настоящие COM-порты ПК не трогаем
        ui::App app(o);
        app.SetShownPaths(ShownPaths(ShotsWorkDir()));
        Stand st{&app};
        st.Run(1500);
        st.Shot("00_not_connected");
        st.size = ImVec2(1024, 700);
        st.Shot("00_not_connected_1024x700");
        st.size = ImVec2(1280, 800);
    }

    // 2. Демо-режим
    const std::time_t simStart = std::time(nullptr);
    ui::App app(ui::AppOptions{false, true, false, simStart, 7});
    app.SetShownPaths(ShownPaths(ShotsWorkDir()));
    app.SetProjectAutosaveAllowed(false); // «Опыт.krenomer» — только там, где проверяется автосохранение
    Stand st{&app};
    SECTION("измерение");
    st.Run(95000);
    app.SetChartWindow(60);
    st.Shot("01_measure", true);
    st.Run(17000);
    st.Shot("02_measure_calm", true);

    SECTION("запись, датчик пропал, АКБ");
    {
        auto c = app.Sim()->GetControls();
        c.recSwitch = true;
        c.motion = 2;
        c.batteryLow = true;
        app.Sim()->SetControls(c);
        st.Run(30000);
        c.sensorOn[1] = false;
        app.Sim()->SetControls(c);
        st.Run(6000);
        app.SetChartWindow(120);
        st.Shot("03_measure_recording", true);
        c.recSwitch = false;
        c.motion = 1;
        c.batteryLow = false;
        c.sensorOn[1] = true;
        app.Sim()->SetControls(c);
        st.Run(30000);
    }

    SECTION("10 минут графика, тёмная тема, малый экран");
    st.Run(560000);
    app.SetChartWindow(600);
    st.Shot("04_measure_10min", true);
    ui::ApplyTheme(true, 1.f);
    ImPlot::StyleColorsDark();
    app.SetChartWindow(60);
    st.Shot("05_measure_dark", true);
    ui::ApplyTheme(false, 1.f);
    ImPlot::StyleColorsLight();
    st.size = ImVec2(1024, 700);
    st.Shot("06_measure_1024x700", true);
    app.SetPage(ui::Page::Settings);
    st.Shot("06_settings_1024x700");
    app.SetPage(ui::Page::Files);
    st.Run(1500);
    st.Shot("06_files_1024x700");
    app.SetPage(ui::Page::Measure);
    st.size = ImVec2(1280, 800);

    SECTION("запись на ПК");
    {
        const fs::path pc = ShotsWorkDir() / L"Записи на ПК";
        fs::remove_all(pc, ec);
        app.SetRecordDir(text::PathToUtf8(pc), true);
        app.SetRecordLabel("опыт 1");
        // Прошивка 1.4: записи на ПК нет — кнопка недоступна, подсказка
        app.Sim()->SetFwVersion("1.4");
        app.Link().SetEnabled(false);
        st.Run(1500);
        app.Link().SetEnabled(true);
        st.Run(2500);
        CHECK(!app.StartPcRecording());
        st.Shot("16_measure_pc_old_fw", true);
        app.Sim()->SetFwVersion("1.5");
        app.Link().SetEnabled(false);
        st.Run(1500);
        app.Link().SetEnabled(true);
        st.Run(2500);
        CHECK(app.StartPcRecording());
        st.Run(40000);
        app.Link().SetEnabled(false); // связь пропала посреди записи
        st.Run(4000);
        st.Shot("16_measure_pc_nolink"); // сверху — панель «Прибор не подключён»: страница прокручивается
        app.Link().SetEnabled(true);
        st.Run(25000);
        CHECK(app.PcRecording());
        st.Shot("16_measure_pc_recording", true);
        ui::ApplyTheme(true, 1.f);
        ImPlot::StyleColorsDark();
        st.Shot("16_measure_pc_recording_dark", true);
        ui::ApplyTheme(false, 1.f);
        ImPlot::StyleColorsLight();
        st.size = ImVec2(1024, 700);
        st.Shot("16_measure_pc_recording_1024x700", true);
        st.size = ImVec2(1280, 800);
        app.StopPcRecording();
        for (int i = 0; i < 200 && app.PcRecorder()->Converting(); i++) // книга .xlsx — в отдельном потоке
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            st.Run(50);
        }
        st.Run(500);
        const auto* r = app.PcRecorder();
        CHECK(r && !r->Running() && r->XlsxOk() && r->Gaps().size() == 1 && r->Sensors().size() == 2);
        CHECK_MSG(r && r->TotalRows() > 1100 && r->TotalLost() > 50, "строк %llu, потеряно %llu",
                  static_cast<unsigned long long>(r ? r->TotalRows() : 0), static_cast<unsigned long long>(r ? r->TotalLost() : 0));
        st.Shot("17_measure_pc_done", true);
        std::size_t csv = 0, xl = 0;
        for (const auto& e : fs::directory_iterator(pc, ec))
        {
            csv += e.path().extension() == ".CSV";
            xl += e.path().extension() == ".xlsx";
        }
        CHECK_MSG(csv == 2 && xl == 1, "CSV %zu, xlsx %zu", csv, xl);
        // Файлы оставлены для проверки (openpyxl): <ShotsWorkDir>\Записи на ПК
    }

    SECTION("диагностика");
    app.SetPage(ui::Page::Diag);
    app.RequestDiag();
    st.Run(1500);
    st.Shot("07_diag_text"); // окно «Снимок diag»
    app.CloseDiagText();
    st.Shot("07_diag", true);

    SECTION("настройки");
    app.SetPage(ui::Page::Settings);
    st.Run(500);
    app.EditSetting("freq", 75);   // вне пределов
    app.EditSetting("alpha", 0.30); // изменено
    app.EditSetting("rollcalm", 1.0);
    {
        auto c = app.Sim()->GetControls();
        c.sensorOn[1] = false; // Д3 сняли, на шине новый датчик (адрес 1)
        app.Sim()->SetControls(c);
        st.Run(1500);
        app.StartAddressChange(1, 3);
        st.Run(800);
        st.Shot("08_settings_addr_progress");
        st.Run(3000);
        app.SyncTime();
        st.Run(1500);
        st.Shot("09_settings", true);
    }

    SECTION("файлы");
    app.SetPage(ui::Page::Files);
    st.Run(1500);
    CHECK(app.FileCount() >= 12);
    app.SelectAllFiles(false);
    {
        const fs::path dl = ShotsWorkDir() / L"Скачано с карты";
        fs::remove_all(dl, ec);
        app.SetDownloadDir(text::PathToUtf8(dl));
        app.Sim()->SetXferBytesPerMs(25.0);
        // Два последних замера (4 файла)
        app.SelectAllFiles(true);
        app.StartDownload();
        st.Run(9000);
        CHECK(app.DownloadRunning());
        st.Shot("10_files_download", true);
        app.Sim()->SetXferBytesPerMs(400.0);
        for (int i = 0; i < 600 && app.DownloadRunning(); i++)
            st.Run(500);
        CHECK(!app.DownloadRunning());
        st.Shot("11_files_done", true);
        std::size_t n = 0;
        for (const auto& e : fs::directory_iterator(dl, ec))
            n += e.path().extension() == ".CSV";
        CHECK_MSG(n == app.FileCount(), "скачано %zu из %zu", n, app.FileCount());
        fs::remove_all(dl, ec);
    }

    SECTION("перепрошивка");
    app.SetPage(ui::Page::Firmware);
    app.SetFirmwarePath(FirmwareImage());
    st.Run(2500);
    CHECK(app.StartFirmwareUpdate(false));
    for (int i = 0; i < 400 && app.Updater().GetStep() != fw::Updater::Step::Download; i++)
        st.Run(20);
    st.Run(1400);
    st.Shot("12_firmware_progress", true);
    for (int i = 0; i < 400 && app.Updater().Busy(); i++)
        st.Run(100);
    CHECK_MSG(app.Updater().GetStep() == fw::Updater::Step::Done, "%s", app.Updater().Error().c_str());
    st.Run(1000);
    st.Shot("13_firmware_done", true);

    SECTION("терминал");
    app.SetPage(ui::Page::Terminal);
    for (const char* c : {"ver", "set freq 10", "stream", "zero reset", "foo", "help"})
    {
        app.TerminalSend(c);
        st.Run(400);
    }
    st.Shot("14_terminal", true);

    SECTION("обработка: опыт кренования по файлам");
    {
        const fs::path dir = ShotsWorkDir() / L"Опыт кренования";
        fs::remove_all(dir, ec);
        MakeHeelSet(dir);
        // Чтение и сохранение — в фоне (настоящие потоки): ждать по часам ПК
        auto wait = [&] {
            for (int i = 0; i < 3000 && app.ProcessBusy(); i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                st.Run(50);
            }
        };
        app.SetPage(ui::Page::Process);
        app.ProcessOpen(text::PathToUtf8(dir));
        wait();
        for (int m = 0; m < 8; m++)
            if (kHeelArm[m] != 4.5) // плечи со знаком (+ к правому борту); возврат в ноль — плечо 0
                app.ProcessSetArm(m + 1, kHeelArm[m]);
        st.Run(500);
        CHECK(app.ProcessMeasurements() == 8);
        const auto& sum = app.ProcessSummary();
        CHECK(sum.n == 12); // 6 переносов × 2 поста; возвраты в ноль — ниже порога
        CHECK_RANGE(sum.h, 0.76, 0.84, "h (среднее) по файлам стенда");
        CHECK_RANGE(sum.ls.h, 0.78, 0.82, "h по МНК по файлам стенда");
        st.Shot("18_process", true);
        app.ProcessShowPost(1);
        ui::ApplyTheme(true, 1.f);
        ImPlot::StyleColorsDark();
        st.Shot("18_process_dark", true);
        ui::ApplyTheme(false, 1.f);
        ImPlot::StyleColorsLight();
        st.size = ImVec2(1024, 700);
        st.Shot("18_process_1024x700");
        st.size = ImVec2(1280, 800);
        app.ProcessShowPost(0);
        // Отчёт: книга и две картинки графиков (свой контекст ImGui без окна)
        app.ProcessSaveReport();
        st.Run(100);
        wait();
        CHECK(!app.ProcessBusy());
        for (const wchar_t* f : {L"Отчет_Кренование.xlsx", L"График_Нос.png", L"График_Корма.png"})
            CHECK_MSG(fs::file_size(dir / f, ec) > 10000, "нет файла отчёта %s", text::PathToUtf8(f).c_str());
        fs::copy_file(dir / L"График_Нос.png", fs::path(g_out) / "18_process_export_bow.png",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(dir / L"График_Корма.png", fs::path(g_out) / "18_process_export_stern.png",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(dir / L"Отчет_Кренование.xlsx", fs::path(g_out) / "18_process_report.xlsx",
                      fs::copy_options::overwrite_existing, ec);
        st.Shot("18_process_saved", true);
    }

    SECTION("обработка: весь опыт одной записью (синтетика как у руководителя), старые форматы");
    {
        // Синтетическая запись прошивки 1.5: 10 положений груза по 40–60 с, качка, всплески на переносах
        const fs::path dir = ShotsWorkDir() / L"Опыт одной записью";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        fs::copy_file(testutil::TestDataDir() / "continuous" / "2026-10-09_M020_D2.CSV", dir / "2026-10-09_M020_D2.CSV",
                      fs::copy_options::overwrite_existing, ec);
        auto wait = [&] {
            for (int i = 0; i < 3000 && app.ProcessBusy(); i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                st.Run(50);
            }
        };
        app.SetPage(ui::Page::Process);
        CHECK(app.ProcessAxis() == 0); // по умолчанию — X
        st.Run(7000);                  // уведомление о прошлом отчёте погасло
        app.ProcessOpen(text::PathToUtf8(dir));
        wait();
        st.Run(300);
        CHECK(app.ProcessContinuous()); // режим выбран сам: в записи больше трёх положений
        CHECK_MSG(app.ProcessPositions().size() == 10, "положений %zu", app.ProcessPositions().size());
        // Плечи переноса от начального положения: l = h·D·tg Δθ / P при h = 0,8 м (D = 1500 т, P = 15 т)
        const double arms[10] = {0, 4.19, 8.41, 11.24, 14.83, 3.49, 8.41, 13.39, 17.0, 0.42};
        for (int i = 1; i < 10; i++)
            app.ProcessSetPositionArm(i, arms[i]);
        app.ProcessSelectPosition(3);
        st.Run(300);
        const auto& ps = app.ProcessPositionsSummary();
        CHECK_MSG(ps.n == 9, "учтено положений %d", ps.n); // 10 − начальное
        CHECK_RANGE(ps.ls.h, 0.75, 0.85, "h по МНК (непрерывная запись)");
        st.Shot("19_process_continuous", true);
        ui::ApplyTheme(true, 1.f);
        ImPlot::StyleColorsDark();
        st.Shot("19_process_continuous_dark", true);
        ui::ApplyTheme(false, 1.f);
        ImPlot::StyleColorsLight();
        st.size = ImVec2(1024, 700);
        st.Shot("19_process_continuous_1024x700");
        st.size = ImVec2(1280, 800);
        app.ProcessSelectPosition(-1);
        app.ProcessSaveReport();
        st.Run(100);
        wait();
        for (const wchar_t* f : {L"Отчет_Кренование.xlsx", L"График_Нос.png"})
            CHECK_MSG(fs::file_size(dir / f, ec) > 10000, "нет файла отчёта %s", text::PathToUtf8(f).c_str());
        fs::copy_file(dir / L"График_Нос.png", fs::path(g_out) / "19_process_continuous_export.png",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(dir / L"Отчет_Кренование.xlsx", fs::path(g_out) / "19_process_continuous_report.xlsx",
                      fs::copy_options::overwrite_existing, ec);

        // Правила РС на синтетике: плечи со знаком под h = 0,8 м по табл. 6.5, перенос 5 записан на 12 % длиннее —
        // замер 5 исключается по 6.6.1; график контроля и проверка; отчёт — лист «Проверки» и График_контроля.png
        {
            std::vector<double> lv;
            for (const auto& q : app.ProcessPositions())
                lv.push_back(q.Level(0));
            const auto rsArms = testutil::RsArms(lv, 1500.0, 15.0, 0.8, 5);
            for (int i = 1; i < static_cast<int>(rsArms.size()); i++)
                app.ProcessSetPositionArm(i, rsArms[static_cast<std::size_t>(i)]);
            st.Run(7000); // уведомление о сохранении погасло
            const auto& rc = app.ProcessRs();
            CHECK(!rc.legacy && rc.r.m.size() == 9);
            CHECK(rc.r.m.size() == 9 && rc.r.m[4].autoExcluded && rc.r.n == 8);
            CHECK_RANGE(rc.r.hk, 0.795, 0.805, "h_k по табл. 6.5 (синтетика)");
            app.ProcessSetView(1);
            st.Run(300);
            st.Shot("26_rules_control", true);
            app.ProcessSetView(2);
            st.Run(300);
            st.Shot("26_rules_checks", true);
            st.size = ImVec2(1024, 700);
            st.Shot("26_rules_checks_1024x700");
            st.size = ImVec2(1280, 800);
            ui::ApplyTheme(true, 1.f);
            ImPlot::StyleColorsDark();
            st.Shot("26_rules_checks_dark", true);
            ui::ApplyTheme(false, 1.f);
            ImPlot::StyleColorsLight();
            app.ProcessSaveReport();
            st.Run(100);
            wait();
            CHECK_MSG(fs::file_size(dir / L"График_контроля.png", ec) > 10000, "нет графика контроля");
            fs::copy_file(dir / L"График_контроля.png", fs::path(g_out) / "26_rules_control_export.png",
                          fs::copy_options::overwrite_existing, ec);
            fs::copy_file(dir / L"Отчет_Кренование.xlsx", fs::path(g_out) / "26_rules_report.xlsx",
                          fs::copy_options::overwrite_existing, ec);
            app.ProcessSetView(0);
        }

        // Файлы старых прошивок и после Excel: всё понято, непонятное — в журнале
        const fs::path old = ShotsWorkDir() / L"Старые файлы";
        fs::remove_all(old, ec);
        fs::create_directories(old, ec);
        const fs::path src = testutil::TestDataDir() / "old";
        for (const auto& e : fs::recursive_directory_iterator(src, ec))
            if (e.is_regular_file(ec)) // из подпапок — с её именем впереди (analyzer_M_001.CSV)
                fs::copy_file(e.path(),
                              old / (e.path().parent_path() == src ? e.path().filename()
                                                                   : fs::path(e.path().parent_path().filename().wstring() +
                                                                              L"_" + e.path().filename().wstring())),
                              fs::copy_options::overwrite_existing, ec);
        st.Run(7000);
        app.ProcessOpen(text::PathToUtf8(old));
        wait();
        st.Run(300);
        CHECK(!app.ProcessContinuous());
        CHECK_MSG(app.ProcessMeasurements() == 8, "замеров %zu", app.ProcessMeasurements());
        st.Shot("20_process_old_files", true);

        // Настоящая запись руководителя (tests/data/real: Ms, CalcX, CalcY из его книги), плечи не заданы (= L)
        const fs::path real = ShotsWorkDir() / L"Опыт руководителя";
        fs::remove_all(real, ec);
        fs::create_directories(real, ec);
        fs::copy_file(testutil::TestDataDir() / "real" / "2026-10-09_M020_D2.CSV", real / "2026-10-09_M020_D2.CSV",
                      fs::copy_options::overwrite_existing, ec);
        st.Run(7000);
        app.ProcessOpen(text::PathToUtf8(real));
        wait();
        st.Run(300);
        CHECK(app.ProcessContinuous());
        CHECK_MSG(app.ProcessPositions().size() == 10, "положений %zu", app.ProcessPositions().size());
        st.Shot("21_process_real", true);
        // Правила РС на записи руководителя: большое судно, плечи не заданы (= L, без знака — знак по крену)
        app.ProcessSetView(2);
        st.Run(300);
        CHECK(app.ProcessRs().legacy && app.ProcessRs().r.m.size() == 9);
        st.Shot("27_rules_real_big", true);
        app.ProcessSetView(1);
        st.Run(300);
        st.Shot("27_rules_real_big_control", true);
        app.ProcessSetView(0);
        app.ProcessSaveReport();
        st.Run(100);
        wait();
        fs::copy_file(real / L"График_Нос.png", fs::path(g_out) / "21_process_real_export.png",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(real / L"Положения_Нос.png", fs::path(g_out) / "22_process_positions_export.png",
                      fs::copy_options::overwrite_existing, ec);
        CHECK_MSG(fs::file_size(real / L"Положения_Нос.png", ec) > 10000, "нет картинки положений");
        // Положение 2 крупно: исходный сигнал, обработанный (медиана + EMA), среднее по обработанному
        app.ProcessSelectPosition(1);
        st.Run(7000); // уведомление о сохранении погасло
        st.Shot("22_process_position", true);
        ui::ApplyTheme(true, 1.f);
        ImPlot::StyleColorsDark();
        st.Shot("22_process_position_dark", true);
        ui::ApplyTheme(false, 1.f);
        ImPlot::StyleColorsLight();
        // Малое судно (модель): D и P в кг, плечи в мм; h — ещё и в мм
        app.ProcessSetSmallShip(true);
        for (int i = 1; i < 10; i++)
            app.ProcessSetPositionArm(i, 50.0 * i);
        st.Run(300);
        st.Shot("23_process_small_ship", true);
        // Правила РС на модели (кг, мм): плечей в записи нет — для снимка плечи со знаком, как их записал бы
        // оператор (с точностью 5 мм) у модели с h ≈ 60 мм (D = 50 кг, P = 1 кг)
        {
            const auto& pp = app.ProcessPositions();
            for (int i = 1; i < static_cast<int>(pp.size()); i++)
            {
                const double rad = (pp[static_cast<std::size_t>(i)].Level(0) - pp[0].Level(0)) * 3.14159265358979 / 180.0;
                app.ProcessSetPositionArm(i, 5.0 * std::round(60.0 * 50.0 * rad / 1.0 / 5.0));
            }
        }
        st.Run(300);
        CHECK(!app.ProcessRs().legacy);
        app.ProcessSetView(1);
        st.Run(300);
        st.Shot("27_rules_real_small_control", true);
        app.ProcessSetView(2);
        st.Run(300);
        st.Shot("27_rules_real_small", true);
        app.ProcessSetView(0);
        app.ProcessSetSmallShip(false);
        // Окно «Свой метод обработки»: пресет «статья» формулами, предпросмотр на положении 2 рядом с Калманом
        app.ProcessSelectPosition(1);
        app.ProcessUseUserMethod(um::PresetArticle(), false);
        app.ProcessShowMethodWindow(true);
        st.Run(300);
        st.Shot("24_process_user_method");
        app.ProcessShowMethodWindow(false);
        app.ProcessSelectPosition(-1);
        st.Run(300);

        // Названия датчиков (посты): опыт с Д2 и Д3 — «Нос, ЛБ» и «Мидель»
        app.ProcessOpen(text::PathToUtf8(ShotsWorkDir() / L"Опыт кренования"));
        wait();
        app.ProcessSetSensorName(2, "Нос, ЛБ");
        app.ProcessSetSensorName(3, "Мидель");
        st.Run(7000);
        st.Shot("25_process_sensor_names", true);
        app.ProcessSetSensorName(2, "Нос");
        app.ProcessSetSensorName(3, "Корма");
        st.Run(300);

        // Опыт с двумя весками (протокол кренования): группы крен-балласта и перемещения, среднее двух приборов,
        // поправка на свободные поверхности, Δ наихудшего варианта — вид «Перемещения» и «Правила РС»
        const fs::path vd = ShotsWorkDir() / L"Опыт с весками";
        fs::remove_all(vd, ec);
        MakeVodnikSet(vd);
        app.ProcessOpen(text::PathToUtf8(vd));
        wait();
        app.ProcessSetSensorName(2, "Весок 1");
        app.ProcessSetSensorName(3, "Весок 2");
        app.ProcessSetShip(vodnik::kD, vodnik::kMass[0]);
        app.ProcessSetRsGroupMode(true);
        app.ProcessSetRsGroups({{"Группа 1", vodnik::kMass[0]}, {"Группа 2", vodnik::kMass[1]}});
        for (int i = 0; i < 10; i++)
            app.ProcessSetRsMove(i, vodnik::kGroup[i], vodnik::kArm[i]);
        app.ProcessSetRsCorrections(vodnik::kDh, vodnik::kDworst);
        app.ProcessSetRsInfo(vodnik::kH, vodnik::kLmax, vodnik::kD0);
        st.Run(7000);
        CHECK(!app.ProcessContinuous() && app.ProcessMeasurements() == 11);
        {
            const auto& rc = app.ProcessRs();
            CHECK(rc.groupMode && rc.postIds.size() == 2 && rc.r.m.size() == 10 && rc.r.n == 9);
            CHECK(rc.r.m.size() == 10 && rc.r.m[1].autoExcluded);
            CHECK_NEAR(rc.r.hk, 1.154, 0.001, "h_k опыта с весками (протокол 1,154)");
            CHECK_NEAR(rc.r.hFinal, 1.123, 0.001, "исправленная h (протокол 1,123)");
        }
        // Проект опыта: сохранить в соседнюю папку, открыть другой опыт с другими параметрами, открыть проект — всё
        // как было и тот же h
        {
            const double hMain = app.ProcessRs().hMain, hk = app.ProcessRs().r.hk;
            const fs::path vproj = ShotsWorkDir() / L"Проекты" / L"Опыт с весками.krenomer";
            fs::remove_all(vproj.parent_path(), ec);
            CHECK(app.ProcessSaveProject(text::PathToUtf8(vproj)));
            CHECK(fs::exists(vproj, ec) && app.ProcessProjectPath() == text::PathToUtf8(vproj));
            CHECK(ReadText(vproj).find("\"folder\": \"..\\\\Опыт с весками\"") != std::string::npos);
            app.ProcessSetRsGroupMode(false);
            app.ProcessSetRsCorrections(0.0, 0.0);
            app.ProcessSetShip(1500.0, 15.0);
            app.ProcessOpen(text::PathToUtf8(ShotsWorkDir() / L"Опыт кренования"));
            wait();
            st.Run(300);
            CHECK(!app.ProcessRs().groupMode && app.ProcessProjectPath().empty());
            CHECK(app.ProcessOpenProject(text::PathToUtf8(vproj)));
            wait();
            st.Run(300);
            const auto& rc = app.ProcessRs();
            CHECK(!app.ProcessContinuous() && app.ProcessMeasurements() == 11);
            CHECK(rc.groupMode && rc.postIds.size() == 2 && rc.r.m.size() == 10 && rc.r.m[1].autoExcluded);
            CHECK_NEAR(rc.hMain, hMain, 1e-9, "опыт с весками: проект открыт — тот же h");
            CHECK_NEAR(rc.r.hk, hk, 1e-9, "опыт с весками: проект открыт — тот же h_k");
            CHECK_NEAR(rc.r.hFinal, 1.123, 0.001, "опыт с весками из проекта: исправленная h (протокол 1,123)");
        }
        app.ProcessSetView(3);
        st.Run(300);
        st.Shot("28_vodnik_moves", true);
        CHECK_NEAR(app.ProcessRs().hMain, 1.123, 0.001, "основной в режиме групп (табл. 6.5, испр.) — без явного выбора");
        app.ProcessSetView(2);
        st.Run(300);
        st.Shot("28_vodnik_rules", true);
        st.size = ImVec2(1024, 700);
        st.Shot("28_vodnik_rules_1024x700");
        app.ProcessSetView(3);
        st.Shot("28_vodnik_moves_1024x700");
        st.size = ImVec2(1280, 800);
        app.ProcessSaveReport();
        st.Run(100);
        wait();
        fs::copy_file(vd / L"Отчет_Кренование.xlsx", fs::path(g_out) / "28_vodnik_report.xlsx",
                      fs::copy_options::overwrite_existing, ec);
        app.ProcessSetView(0);
        app.ProcessSetRsGroupMode(false);
        app.ProcessSetRsCorrections(0.0, 0.0);
        app.ProcessSetRsInfo(0.0, 0.0, 0.0);
        app.ProcessSetShip(1500.0, 15.0);
        app.ProcessSetSensorName(2, "Нос");
        app.ProcessSetSensorName(3, "Корма");
        st.Run(300);

        // Опыт на модели отсека (M021): малое судно, два датчика, 9 положений (2 коротких — вручную), плечи от ДП;
        // KRENOMER_M021_DIR — папка с исходными файлами (иначе — урезанная копия tests/data/m021)
        {
            const fs::path md = ShotsWorkDir() / L"Модель отсека";
            fs::remove_all(md, ec);
            fs::create_directories(md, ec);
            fs::path mSrc = testutil::TestDataDir() / "m021";
            if (const char* e = std::getenv("KRENOMER_M021_DIR"); e && *e)
                mSrc = text::PathFromUtf8(e);
            for (const auto& e : fs::directory_iterator(mSrc, ec))
                if (e.path().extension() == ".CSV")
                    fs::copy_file(e.path(), md / e.path().filename(), fs::copy_options::overwrite_existing, ec);
            app.ProcessSetSmallShip(true);
            app.ProcessOpen(text::PathToUtf8(md));
            wait();
            app.ProcessSetShip(53.5, 2.5);
            CHECK(app.ProcessContinuous());
            const std::vector<std::pair<double, double>> spans = {{5, 36},          {58.7, 71.2},  {88.9, 101.8},
                                                                  {114.5, 125},     {133.2, 140.3}, {164.3, 211},
                                                                  {226.3, 232.0},   {238.7, 259},   {269.6, 282.2}};
            app.ProcessSetPositionSpans(spans);
            const double armDp[9] = {-20, 45, 95, 145, 205, -55, -105, -155, -205};
            for (int i = 0; i < 9; i++)
                app.ProcessSetPositionArm(i, armDp[i]);
            app.ProcessSelectPosition(4);
            st.Run(7000);
            const auto& rc = app.ProcessRs();
            CHECK(rc.r.m.size() == 8 && rc.r.n == 8);
            CHECK_NEAR(rc.r.hk, 0.0531, 0.001, "M021: h_k по табл. 6.5");
            CHECK_RANGE(app.ProcessPositionsSummary().h, 0.045, 0.052, "M021: h от начального");
            {
                // Плечи «от ПрБ»: расстояние от правого борта до центра груза, B = 550 мм — те же плечи от ДП и h
                const double h = app.ProcessPositionsSummary().h, hk = rc.r.hk;
                const double fromPrb[9] = {295, 230, 180, 130, 70, 330, 380, 430, 480};
                app.ProcessSetArmFrom(2, 550.0);
                for (int i = 0; i < 9; i++)
                    app.ProcessSetPositionArmInput(i, fromPrb[i]);
                st.Run(300);
                for (int i = 0; i < 9 && i < static_cast<int>(app.ProcessPositions().size()); i++)
                    CHECK_NEAR(app.ProcessPositions()[i].arm, armDp[i], 1e-9, "от ПрБ -> плечо от ДП");
                CHECK_NEAR(app.ProcessPositionsSummary().h, h, 1e-9, "плечи от ПрБ: тот же h");
                CHECK_NEAR(app.ProcessRs().r.hk, hk, 1e-9, "плечи от ПрБ: тот же h_k");
                app.ProcessSetView(0);
                app.ProcessSetSubView(1);
                st.Run(300);
                st.Shot("36_arms_from_prb");
                app.ProcessSetSubView(-1);
                app.ProcessSetArmFrom(0, 0.0);
                st.Run(300);
            }
            {
                const double h = app.ProcessPositionsSummary().h, hk = rc.r.hk;
                // Автосохранение: правка -> через 3 с «Опыт.krenomer» рядом с записями
                const fs::path autoFile = md / L"Опыт.krenomer";
                CHECK(!fs::exists(autoFile, ec)); // открытие папки само файл не пишет
                app.SetProjectAutosaveAllowed(true);
                app.ProcessSetPositionArm(0, armDp[0]);
                st.Run(1000);
                CHECK(!fs::exists(autoFile, ec)); // не сразу
                st.Run(3000);
                CHECK_MSG(fs::exists(autoFile, ec), "нет автосохранения Опыт.krenomer");
                CHECK(app.ProcessProjectPath() == text::PathToUtf8(autoFile));
                app.SetProjectAutosaveAllowed(false);
                const fs::path mproj = ShotsWorkDir() / L"Проекты" / L"Модель отсека M021.krenomer";
                CHECK(app.ProcessSaveProject(text::PathToUtf8(mproj)));
                // Другой опыт, затем папка M021 снова — в ней «Опыт.krenomer»: не открывается молча, а спрашивается
                // «открыть проект из этой папки / создать новый проект»; «Отмена» — папка не читается
                app.ProcessOpen(text::PathToUtf8(vd));
                wait();
                st.Run(300);
                CHECK(app.ProcessProjectPath().empty() && !app.ProcessAskingProject());
                app.ProcessOpen(text::PathToUtf8(md));
                wait();
                st.Run(1100);
                CHECK(app.ProcessAskingProject());
                st.Shot("38_ask_project");
                ui::ApplyTheme(true, 1.f);
                ImPlot::StyleColorsDark();
                st.Shot("38_ask_project_dark");
                ui::ApplyTheme(false, 1.f);
                ImPlot::StyleColorsLight();
                app.ProcessAnswerProject(0);
                st.Run(300);
                CHECK(!app.ProcessAskingProject() && app.ProcessMeasurements() == 11); // отмена: прежний опыт (весков) на месте
                // «Создать новый проект кренования»: записи M021 с чистыми плечами, проект не открыт
                app.ProcessOpen(text::PathToUtf8(md));
                wait();
                st.Run(300);
                CHECK(app.ProcessAskingProject());
                app.ProcessAnswerProject(2);
                wait();
                st.Run(300);
                CHECK(!app.ProcessAskingProject() && app.ProcessContinuous() && app.ProcessProjectPath().empty());
                {
                    bool fresh = !app.ProcessPositions().empty();
                    for (const auto& q : app.ProcessPositions())
                        fresh = fresh && std::isnan(q.arm);
                    CHECK_MSG(fresh, "новый проект: плечи не чистые");
                }
                // «Открыть проект из этой папки» — плечи и границы как были
                app.ProcessOpen(text::PathToUtf8(vd));
                wait();
                app.ProcessOpen(text::PathToUtf8(md));
                wait();
                st.Run(300);
                CHECK(app.ProcessAskingProject());
                app.ProcessAnswerProject(1);
                wait();
                st.Run(300);
                CHECK(app.ProcessContinuous() && app.ProcessPositions().size() == 9);
                CHECK(app.ProcessProjectPath() == text::PathToUtf8(autoFile));
                CHECK(!app.ProcessPositions().empty() && app.ProcessPositions()[0].arm == armDp[0] &&
                      app.ProcessPositions()[8].arm == armDp[8] && app.ProcessPositions()[4].t0 == 133.2);
                CHECK_NEAR(app.ProcessPositionsSummary().h, h, 1e-9, "M021: папка с проектом — тот же h");
                CHECK_NEAR(app.ProcessRs().r.hk, hk, 1e-9, "M021: папка с проектом — тот же h_k");
                // Новый опыт без проекта начинается с чистого листа: плечи прошлой обработки не переносятся
                fs::remove(autoFile, ec);
                app.ProcessOpen(text::PathToUtf8(vd));
                wait();
                app.ProcessOpen(text::PathToUtf8(md));
                wait();
                st.Run(300);
                CHECK(app.ProcessProjectPath().empty() && app.ProcessContinuous());
                bool clean = !app.ProcessPositions().empty();
                for (const auto& q : app.ProcessPositions())
                    clean = clean && std::isnan(q.arm);
                CHECK_MSG(clean, "M021 без проекта: плечи перенеслись из прошлой обработки");
                // Проект из списка недавних (сохранён в другую папку) — тот же h
                CHECK(app.ProcessOpenProject(text::PathToUtf8(mproj)));
                wait();
                st.Run(300);
                CHECK(app.ProcessPositions().size() == 9 && app.ProcessPositions()[3].arm == armDp[3]);
                CHECK_NEAR(app.ProcessPositionsSummary().h, h, 1e-9, "M021: проект открыт — тот же h");
                CHECK_NEAR(app.ProcessRs().r.hk, hk, 1e-9, "M021: проект открыт — тот же h_k");
                // Испорченный проект — понятная ошибка в журнале, опыт не трогается
                const fs::path broken = ShotsWorkDir() / L"Проекты" / L"Испорчен.krenomer";
                std::ofstream(broken, std::ios::binary) << "{\"krenomer_project\": 9}";
                CHECK(!app.ProcessOpenProject(text::PathToUtf8(broken)));
                CHECK_NEAR(app.ProcessPositionsSummary().h, h, 1e-9, "M021: испорченный проект не тронул опыт");
                app.ProcessSelectPosition(4);
                st.Run(7500); // всплывающие сообщения — погасли
                // Кнопки и меню проекта
                st.Shot("33_project_buttons", true);
                app.ProcessShowProjectMenu(1);
                st.Run(100);
                st.Shot("33_project_open_menu");
                app.ProcessShowProjectMenu(2);
                st.Run(100);
                st.Shot("33_project_save_menu");
                app.ProcessShowProjectMenu(-1);
                st.size = ImVec2(1024, 700);
                app.ProcessShowProjectMenu(1);
                st.Run(100);
                st.Shot("33_project_menu_1024x700");
                app.ProcessShowProjectMenu(-1);
                st.Run(100);
                st.size = ImVec2(1280, 800);
                st.Run(100);
            }
            st.Shot("29_m021_positions", true);
            // Окна «Подробнее» карточек-показателей и меню «Сохранить отчёт ▼»
            for (int c = 0; c < 4; c++)
            {
                app.ProcessShowKpiDetail(c);
                st.Run(300);
                CHECK(app.ProcessKpiDetail() == c);
                st.Shot(("37_kpi_detail_" + std::to_string(c)).c_str());
            }
            app.ProcessShowKpiDetail(-1);
            st.Run(2100); // кадр — раз в секунду стенда
            CHECK(app.ProcessKpiDetail() == -1);
            {
                const auto st2 = app.ProcessSteps();
                const steps::Section* hk = steps::Find(st2, "hk");
                CHECK_MSG(hk && !hk->rows.empty() && hk->rows[0].result == steps::F(app.ProcessRs().r.hk, 4) + " м",
                          "окно: h_k шага = расчёт");
            }
            app.ProcessShowSaveMenu(1);
            st.Run(100);
            st.Shot("37_report_menu");
            app.ProcessShowSaveMenu(-1);
            st.Run(100);
            app.ProcessShowRecordsMenu(1);
            st.Run(1100);
            st.Shot("40_records_menu");
            app.ProcessShowRecordsMenu(-1);
            st.Run(1100);
            app.ProcessShowProjectMenu(1);
            st.Run(1100);
            st.Shot("40_project_menu");
            app.ProcessShowProjectMenu(-1);
            st.Run(1100);
            // Горячие клавиши (1.8.2): нажатия — через очередь ввода ImGui, как от клавиатуры
            {
                enum
                {
                    kCtrl = 1,
                    kShift = 2,
                    kAlt = 4
                };
                auto press = [&](ImGuiKey k, int mods = 0) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.AddKeyEvent(ImGuiMod_Ctrl, (mods & kCtrl) != 0);
                    io.AddKeyEvent(ImGuiMod_Shift, (mods & kShift) != 0);
                    io.AddKeyEvent(ImGuiMod_Alt, (mods & kAlt) != 0);
                    st.Frame();
                    io.AddKeyEvent(k, true);
                    st.Frame();
                    io.AddKeyEvent(k, false);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    io.AddKeyEvent(ImGuiMod_Shift, false);
                    io.AddKeyEvent(ImGuiMod_Alt, false);
                    st.Frame();
                };
                CHECK(app.GetPage() == ui::Page::Process);
                press(ImGuiKey_F1);
                CHECK_MSG(app.GetPage() == ui::Page::Help, "F1 — не справка");
                press(ImGuiKey_F1);
                CHECK_MSG(app.GetPage() == ui::Page::Process, "F1 ещё раз — не назад");
                press(ImGuiKey_1, kCtrl);
                CHECK_MSG(app.GetPage() == ui::Page::Measure, "Ctrl+1 — не «Измерение»");
                press(ImGuiKey_7, kCtrl);
                CHECK_MSG(app.GetPage() == ui::Page::Terminal, "Ctrl+7 — не «Терминал»");
                press(ImGuiKey_3, kCtrl);
                CHECK_MSG(app.GetPage() == ui::Page::Settings, "Ctrl+3 — не «Настройки»");
                press(ImGuiKey_2, kCtrl);
                CHECK_MSG(app.GetPage() == ui::Page::Process, "Ctrl+2 — не «Обработка»");
                press(ImGuiKey_3, kCtrl | kShift);
                CHECK_MSG(app.ProcessSubView() == 2, "Ctrl+Shift+3 — не «Графики»");
                press(ImGuiKey_2, kCtrl | kShift);
                CHECK_MSG(app.ProcessSubView() == 1, "Ctrl+Shift+2 — не «Положения и расчёт»");
                // Положения: следующее / предыдущее, удалить и вернуть, добавить
                const int n = static_cast<int>(app.ProcessPositions().size());
                app.ProcessSelectPosition(-1);
                press(ImGuiKey_PageDown);
                CHECK_MSG(app.ProcessSelected() == 0, "PgDn: выбрано %d", app.ProcessSelected());
                press(ImGuiKey_PageDown);
                CHECK(app.ProcessSelected() == 1);
                press(ImGuiKey_RightArrow, kAlt);
                CHECK(app.ProcessSelected() == 2);
                press(ImGuiKey_PageUp);
                press(ImGuiKey_LeftArrow, kAlt);
                CHECK_MSG(app.ProcessSelected() == 0, "PgUp, Alt+←: выбрано %d", app.ProcessSelected());
                press(ImGuiKey_PageUp);
                CHECK(app.ProcessSelected() == 0); // у первого — остаётся
                press(ImGuiKey_PageDown);
                const auto delPos = app.ProcessPositions()[1];
                press(ImGuiKey_Delete);
                CHECK_MSG(static_cast<int>(app.ProcessPositions().size()) == n - 1 && app.ProcessSelected() == -1,
                          "Del: положений %d (было %d)", static_cast<int>(app.ProcessPositions().size()), n);
                press(ImGuiKey_Z, kCtrl);
                CHECK_MSG(static_cast<int>(app.ProcessPositions().size()) == n, "Ctrl+Z не вернул положение");
                CHECK(app.ProcessSelected() == 1 && app.ProcessPositions()[1].t0 == delPos.t0 &&
                      app.ProcessPositions()[1].t1 == delPos.t1);
                press(ImGuiKey_Z, kCtrl); // второй раз — нечего возвращать
                CHECK(static_cast<int>(app.ProcessPositions().size()) == n);
                press(ImGuiKey_Insert);
                CHECK(app.LastHotkey() == "Ins");
                if (static_cast<int>(app.ProcessPositions().size()) == n + 1)
                {
                    press(ImGuiKey_Delete); // новое выбрано — оно и удаляется
                    CHECK(static_cast<int>(app.ProcessPositions().size()) == n);
                }
                st.Run(1100);
                CHECK_NEAR(app.ProcessRs().r.hk, 0.0531, 0.001, "M021: h_k после клавиш положений");
                // Ctrl+E — карточка развёрнута, Esc — назад; Ctrl+B — панель значками и обратно
                press(ImGuiKey_E, kCtrl);
                CHECK_MSG(app.ProcessExpanded() != 0, "Ctrl+E не развернул карточку");
                press(ImGuiKey_Escape);
                CHECK_MSG(app.ProcessExpanded() == 0, "Esc не свернул карточку");
                const bool rail = app.NavCollapsedNow();
                press(ImGuiKey_B, kCtrl);
                CHECK_MSG(app.NavCollapsedNow() != rail, "Ctrl+B не переключил панель");
                press(ImGuiKey_B, kCtrl);
                CHECK(app.NavCollapsedNow() == rail);
                app.SetNavCollapsed(-1);
                // Меню: Esc закрывает
                app.ProcessShowSaveMenu(1);
                st.Frame();
                st.Frame();
                CHECK(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
                press(ImGuiKey_1, kCtrl); // при открытом меню страницы не переключаются
                CHECK(app.GetPage() == ui::Page::Process);
                press(ImGuiKey_Escape);
                st.Frame();
                CHECK_MSG(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
                          "Esc не закрыл меню отчёта");
                // Ctrl+O в стенде — без окна выбора файла; страница — «Обработка»
                press(ImGuiKey_O, kCtrl);
                CHECK(app.LastHotkey() == "Ctrl+O" && app.GetPage() == ui::Page::Process);
                // В поле ввода клавиши — тексту (кроме F1, Esc, Ctrl+S): поиск справки, Ctrl+F
                press(ImGuiKey_F1);
                press(ImGuiKey_F, kCtrl);
                st.Frame();
                CHECK_MSG(ImGui::GetIO().WantTextInput, "Ctrl+F: поле поиска справки не в фокусе");
                ImGui::GetIO().AddInputCharactersUTF8("плеч");
                st.Frame();
                st.Frame();
                CHECK_MSG(app.HelpFound() >= 4, "поиск «плеч»: найдено %d", app.HelpFound());
                press(ImGuiKey_1, kCtrl);
                CHECK_MSG(app.GetPage() == ui::Page::Help, "Ctrl+1 в поле поиска переключил страницу");
                press(ImGuiKey_Delete);
                CHECK(static_cast<int>(app.ProcessPositions().size()) == n);
                press(ImGuiKey_S, kCtrl);
                CHECK_MSG(app.LastHotkey() == "Ctrl+S", "Ctrl+S в поле ввода не сработал");
                press(ImGuiKey_F1);
                CHECK_MSG(app.GetPage() == ui::Page::Process, "F1 в поле поиска не вернул назад");
                app.HelpSetSearch("");
                app.ProcessSelectPosition(-1);
                app.ProcessSetSubView(-1);
                st.Run(1100);
            }
            // Справка: страница, поиск, горячие клавиши; светлая / тёмная, 1280 × 800 и 1280 × 720
            {
                st.Run(6000); // сообщение «Проект сохранён» (Ctrl+S выше) гаснет
                app.SetPage(ui::Page::Help);
                st.Run(1100);
                st.Shot("41_help", true);
                CHECK(app.HelpSection() == 0);
                app.HelpGoTo(8);
                st.Run(1100);
                st.Shot("41_help_keys", true);
                CHECK_MSG(app.HelpSection() == 8, "оглавление: раздел %d вместо 8", app.HelpSection());
                app.HelpGoTo(3);
                st.Run(1100);
                st.Shot("41_help_processing", true);
                app.HelpSetSearch("плеч");
                st.Run(1100);
                st.Shot("41_help_search", true);
                CHECK(app.HelpFound() >= 4);
                app.HelpSetSearch("абракадабра");
                st.Run(1100);
                CHECK(app.HelpFound() == 0);
                app.HelpSetSearch("");
                app.HelpGoTo(0);
                ui::ApplyTheme(true, 1.f);
                ImPlot::StyleColorsDark();
                st.Run(1100);
                st.Shot("41_help_dark", true);
                st.size = ImVec2(1280, 720);
                st.Run(1100);
                st.Shot("41_help_1280x720_dark", true);
                ui::ApplyTheme(false, 1.f);
                ImPlot::StyleColorsLight();
                st.Shot("41_help_1280x720", true);
                app.HelpGoTo(1);
                st.Run(1100);
                st.Shot("41_help_1280x720_prep", true);
                st.size = ImVec2(1024, 700);
                app.HelpGoTo(0);
                st.Run(1100);
                st.Shot("41_help_1024x700", true);
                st.size = ImVec2(1280, 800);
                app.SetPage(ui::Page::Process);
                st.Run(1100);
            }
            // Дашборд: тёмная тема, развёрнутая карточка записи (на всю область) и назад
            ui::ApplyTheme(true, 1.f);
            ImPlot::StyleColorsDark();
            st.Shot("29_m021_dark", true);
            // Свёрнутая левая панель (в любом режиме), «Обработка» 1280 × 720 — светлая и тёмная
            app.SetNavCollapsed(1);
            st.Run(1100);
            st.Shot("39_nav_collapsed_dark");
            ui::ApplyTheme(false, 1.f);
            ImPlot::StyleColorsLight();
            st.Shot("39_nav_collapsed");
            app.SetNavCollapsed(-1);
            st.size = ImVec2(1280, 720);
            st.Run(1100);
            st.Shot("39_process_1280x720");
            ui::ApplyTheme(true, 1.f);
            ImPlot::StyleColorsDark();
            st.Shot("39_process_1280x720_dark");
            st.size = ImVec2(1280, 800);
            st.Run(1100);
            ui::ApplyTheme(false, 1.f);
            ImPlot::StyleColorsLight();
            app.ProcessSetExpanded(1);
            st.Run(300);
            st.Shot("29_m021_expanded", true);
            app.ProcessSetExpanded(0);
            app.ProcessSetTab(1);
            st.Run(300);
            st.Shot("29_m021_tab_rules", true);
            app.ProcessSetTab(0);
            app.ProcessSelectPosition(-1);
            app.ProcessSetView(2);
            st.Run(300);
            st.Shot("29_m021_rules", true);
            app.ProcessSetView(1);
            st.Run(300);
            st.Shot("29_m021_control", true);
            app.ProcessSetView(0);
            app.ProcessSaveReport();
            st.Run(100);
            wait();
            for (const wchar_t* f : {L"Отчет_Кренование.xlsx", L"График_Д2.png", L"График_Д3.png", L"Положения_Д2.png",
                                     L"График_контроля.png"})
                if (fs::exists(md / f, ec))
                    fs::copy_file(md / f, fs::path(g_out) / (L"29_m021_" + std::wstring(f)),
                                  fs::copy_options::overwrite_existing, ec);
            CHECK_MSG(fs::file_size(md / L"Отчет_Кренование.xlsx", ec) > 10000, "нет отчёта M021");
            // Полный отчёт: + лист «Расчёт по шагам» (больше краткого); копия — для превью в Excel
            {
                const auto brief = fs::file_size(md / L"Отчет_Кренование.xlsx", ec);
                app.ProcessSetReportFull(true);
                app.ProcessSaveReport();
                st.Run(100);
                wait();
                app.ProcessSetReportFull(false);
                CHECK_MSG(fs::file_size(md / L"Отчет_Кренование.xlsx", ec) > brief, "полный отчёт не больше краткого");
                fs::copy_file(md / L"Отчет_Кренование.xlsx", fs::path(g_out) / L"37_m021_full_report.xlsx",
                              fs::copy_options::overwrite_existing, ec);
                st.Run(7000);
            }
#ifdef _WIN32
            // Отчёт открыт в Excel (файл держат без совместной записи и удаления): сохранение не молчит — весь
            // комплект под «(2)», в журнале и всплывающем сообщении «сохранено как …» с «Открыть папку»
            {
                HANDLE held = CreateFileW((md / L"Отчет_Кренование.xlsx").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                CHECK(held != INVALID_HANDLE_VALUE);
                CHECK(filebusy::Busy(md / L"Отчет_Кренование.xlsx") && !filebusy::Busy(md / L"График_Нос.png"));
                const auto before = fs::last_write_time(md / L"Отчет_Кренование.xlsx", ec);
                app.ProcessSaveReport();
                st.Run(100);
                wait();
                st.Run(100);
                CloseHandle(held);
                CHECK(fs::last_write_time(md / L"Отчет_Кренование.xlsx", ec) == before);
                CHECK_MSG(fs::file_size(md / L"Отчет_Кренование (2).xlsx", ec) > 10000, "нет «Отчет_Кренование (2).xlsx»");
                CHECK(fs::exists(md / L"График_Нос (2).png", ec) && fs::exists(md / L"Положения_Нос (2).png", ec) &&
                      fs::exists(md / L"График_контроля (2).png", ec));
                bool toast = false;
                for (const auto& t : app.Toasts())
                    toast |= t.text.find("сохранено как Отчет_Кренование (2).xlsx") != std::string::npos && !t.folder.empty();
                CHECK_MSG(toast, "нет сообщения «сохранено как …»");
                st.Shot("35_report_busy", true);
                st.Run(13000); // сообщение погасло
            }
#endif
            app.ProcessSetSmallShip(false);
            app.ProcessSetShip(1500.0, 15.0);
            st.Run(300);
        }

        // Опыт M024, присланный через Telegram: записи с припиской « (1)», проект ссылается на имена без неё и на
        // чужой диск — записи находятся по похожему имени; проект без записей рядом — «Указать файлы…»
        {
            const fs::path tg = ShotsWorkDir() / L"Из Telegram";
            fs::remove_all(tg, ec);
            fs::create_directories(tg, ec);
            std::vector<std::string> tgFiles;
            for (const char* n : {"2026-10-16_M024_D2", "2026-10-16_M024_D3"})
            {
                const fs::path dst = tg / text::PathFromUtf8(std::string(n) + " (1).CSV");
                fs::copy_file(testutil::TestDataDir() / "m024" / (std::string(n) + ".CSV"), dst,
                              fs::copy_options::overwrite_existing, ec);
                tgFiles.push_back(text::PathToUtf8(dst));
            }
            proj::Project p;
            p.folder = "K:\\Опыты\\Бассейн";
            p.files = {"K:\\Опыты\\Бассейн\\2026-10-16_M024_D3.CSV", "K:\\Опыты\\Бассейн\\2026-10-16_M024_D2.CSV"};
            p.model = true;
            p.D = 52.5;
            p.P = 2.5;
            p.L = 240;
            p.method = 0; // Медиана + EMA (статья) — как на картинке руководителя
            p.mode = 1;
            p.recordFile = "2026-10-16_M024_D2.CSV";
            p.positionsEdited = true;
            const double sp[9][3] = {{2, 38.986, 5},          {49.886, 93.986, 95},   {102.686, 142.086, 170},
                                     {148.686, 182.286, 220}, {189.186, 231.286, 280}, {279.786, 310.886, -35},
                                     {321.286, 356.386, -100}, {366.786, 407.086, -180}, {428.786, 473.194, -280}};
            for (const auto& q : sp)
            {
                heel::Position pos;
                pos.t0 = q[0];
                pos.t1 = q[1];
                pos.arm = q[2];
                p.positions.push_back(pos);
            }
            const fs::path tgProj = tg / L"Опыт.krenomer";
            CHECK(proj::Save(tgProj, p));
            CHECK(app.ProcessOpenProject(text::PathToUtf8(tgProj)));
            wait();
            st.Run(300);
            CHECK(app.ProcessContinuous() && app.ProcessPositions().size() == 9 && !app.ProcessProjectMissing());
            CHECK(app.ProcessPositions().size() == 9 && std::fabs(app.ProcessPositions()[7].Level(1) + 4.958) < 0.02);
            app.ProcessShowPost(1);
            app.ProcessSelectPosition(7);
            st.Run(300);
            st.Shot("34_m024_position8", true);
            // Тот же проект в папке без записей (и без похожих имён) — карточка «Указать файлы…»
            const fs::path lost = ShotsWorkDir() / L"Проект без записей";
            fs::remove_all(lost, ec);
            CHECK(proj::Save(lost / L"Опыт.krenomer", p));
            CHECK(!app.ProcessOpenProject(text::PathToUtf8(lost / L"Опыт.krenomer")));
            CHECK(app.ProcessProjectMissing());
            st.Run(7500);
            st.Shot("34_project_missing");
            CHECK(app.ProcessProjectPickFiles({tgFiles[1], tgFiles[0]}));
            wait();
            st.Run(300);
            CHECK(!app.ProcessProjectMissing() && app.ProcessContinuous() && app.ProcessPositions().size() == 9);
            CHECK(app.ProcessPositions().size() == 9 && std::fabs(app.ProcessPositions()[7].Level(1) + 4.958) < 0.02);
            app.ProcessShowPost(0);
            app.ProcessSelectPosition(-1);
            app.ProcessSetSmallShip(false);
            app.ProcessSetShip(1500.0, 15.0);
            st.Run(300);
        }
    }

    // Основные страницы при разных размерах окна (HD ready и Full HD): снимки и замечания раскладки
    if (!g_sizesOut.empty())
    {
        SECTION("размеры окна: 1280×720, 1366×768, 1920×1080 — основные страницы");
        fs::create_directories(g_sizesOut, ec);
        // «Обработка» — на опыте M021 (модель, два датчика, 9 положений)
        const fs::path md = ShotsWorkDir() / L"Модель отсека";
        fs::remove_all(md, ec);
        fs::create_directories(md, ec);
        for (const auto& e : fs::directory_iterator(testutil::TestDataDir() / "m021", ec))
            fs::copy_file(e.path(), md / e.path().filename(), fs::copy_options::overwrite_existing, ec);
        app.ProcessSetSmallShip(true);
        app.ProcessOpen(text::PathToUtf8(md));
        for (int i = 0; i < 3000 && app.ProcessBusy(); i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            st.Run(50);
        }
        app.ProcessSetShip(53.5, 2.5);
        app.ProcessSetPositionSpans({{5, 36}, {58.7, 71.2}, {88.9, 101.8}, {114.5, 125}, {133.2, 140.3}, {164.3, 211},
                                     {226.3, 232.0}, {238.7, 259}, {269.6, 282.2}});
        const double armDp[9] = {-20, 45, 95, 145, 205, -55, -105, -155, -205};
        for (int i = 0; i < 9; i++)
            app.ProcessSetPositionArm(i, armDp[i]);
        app.ProcessSelectPosition(4);
        st.Run(7000);
        std::string report;
        struct Pg
        {
            ui::Page page;
            const char* name;
            int view = -1; // «Обработка»: подпункт (-1 — сам по размеру окна)
            int tab = 0;
        };
        const Pg pages[] = {{ui::Page::Measure, "measure"},
                            {ui::Page::Process, "process"},
                            {ui::Page::Process, "process_table", 1},
                            {ui::Page::Process, "process_rules", 1, 1},
                            {ui::Page::Process, "process_graphs", 2},
                            {ui::Page::Settings, "settings"},
                            {ui::Page::Diag, "diag"},
                            {ui::Page::Files, "files"},
                            {ui::Page::Firmware, "firmware"},
                            {ui::Page::Terminal, "terminal"}};
        const ImVec2 sizes[] = {{1024, 768}, {1152, 864}, {1280, 720}, {1366, 768}, {1920, 1080}};
        const ImVec2 keep = st.size;
        for (const ImVec2& sz : sizes)
            for (const Pg& pg : pages)
            {
                st.size = sz;
                app.SetPage(pg.page);
                app.ProcessSetSubView(pg.view);
                app.ProcessSetTab(pg.tab);
                st.Run(300);
                const std::string name =
                    std::to_string(static_cast<int>(sz.x)) + "x" + std::to_string(static_cast<int>(sz.y)) + "_" + pg.name;
                const std::string issues = st.Survey();
                report += name + (issues.empty() ? ": ок\n" : ":\n" + issues);
                if (g_sizesStrict)
                    CHECK_MSG(issues.empty(), "%s:\n%s", name.c_str(), issues.c_str());
                st.Save((fs::path(g_sizesOut) / (name + ".png")).string(), name.c_str());
            }
        st.size = keep;
        app.ProcessSetSubView(-1);
        app.ProcessSetTab(0);
        std::ofstream(fs::path(g_sizesOut) / "sizes.txt", std::ios::binary) << report;
        std::printf("%s", report.c_str());
        app.ProcessSetSmallShip(false);
        app.ProcessSetShip(1500.0, 15.0);
        st.Run(300);
    }

    SECTION("о программе");
    app.SetPage(ui::Page::Measure);
    app.ShowAbout(true);
    st.Run(1000);
    st.Shot("15_about");

    // Макеты предложений по Правилам РС (не функции программы): отдельные экраны в стиле программы
    SECTION("макеты: мастер опыта, проверка по Правилам, протокол");
    {
        const mock::Experiment ex = mock::FromSynthetic(testutil::TestDataDir() / "continuous" / "2026-10-09_M020_D2.CSV");
        CHECK_MSG(ex.m.size() == 9 && ex.excluded >= 0, "замеров %zu, исключён %d", ex.m.size(), ex.excluded);
        CHECK_RANGE(ex.hk, 0.75, 0.85, "h_k по табл. 6.5 после отбраковки");
        const std::string outDir = g_mockOut.empty() ? g_out : g_mockOut;
        fs::create_directories(outDir, ec);
        auto shot = [&](const char* name, const std::function<void()>& body, bool dark) {
            ui::ApplyTheme(dark, 1.f);
            if (dark)
                ImPlot::StyleColorsDark();
            else
                ImPlot::StyleColorsLight();
            for (int i = 0; i < 4; i++)
            {
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2(1280, 800);
                io.DeltaTime = 1.f / 30.f;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
                ImGui::Begin("##mock", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImGui::PopStyleVar();
                body();
                ImGui::End();
                ImGui::Render();
            }
            const std::string path = (fs::path(outDir) / (std::string(name) + ".png")).string();
            CHECK_MSG(Rasterize(path, ui::pal.bg), "не записан %s", path.c_str());
            std::printf("  %s\n", path.c_str());
            g_shots++;
        };
        shot("30_mock_wizard", [] { mock::Wizard(); }, false);
        shot("31_mock_checks", [&] { mock::Checks(ex); }, false);
        shot("32_mock_protocol", [&] { mock::Protocol(ex); }, false);
        shot("31_mock_checks_dark", [&] { mock::Checks(ex); }, true);
        ui::ApplyTheme(false, 1.f);
        ImPlot::StyleColorsLight();
    }

    CHECK_MSG(g_badIdx == 0, "индексов вне буфера: %lld", g_badIdx);
    std::printf("снимков: %d в %s\n", g_shots, fs::absolute(g_out).string().c_str());
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return TestSummary();
}
