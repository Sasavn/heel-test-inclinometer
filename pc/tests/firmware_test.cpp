// Перепрошивка: проверка образа .bin (векторы, размер, строка версии; настоящий образ из сборки прошивки, если
// есть), разбор прогресса dfu-util, весь сценарий обновления с прибором-имитатором и «dfu-util» демо-режима
// (boot -> DFU -> запись -> чтение и сравнение -> запуск -> снова на связи), отказ во время записи и boot force,
// запуск внешнего процесса с перехватом вывода.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "TestUtil.hpp"
#include "comm/DeviceLink.hpp"
#include "comm/SimConnection.hpp"
#include "fw/Firmware.hpp"

namespace fs = std::filesystem;

namespace
{

std::vector<std::uint8_t> FakeImage(std::size_t size, const std::string& version)
{
    std::vector<std::uint8_t> d(size, 0xFF);
    auto put = [&](std::size_t o, std::uint32_t v) {
        for (int i = 0; i < 4; i++)
            d[o + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    put(0, 0x20020000u); // вершина стека
    put(4, 0x08000199u); // Reset_Handler (Thumb)
    const std::string tag = "BWM427 inclinometer firmware v" + version + ", build Oct  8 2026 10:00:00";
    std::copy(tag.begin(), tag.end(), d.begin() + 0x2000);
    d[0x2000 + tag.size()] = 0;
    return d;
}

fs::path WriteTemp(const std::vector<std::uint8_t>& d, const wchar_t* name)
{
    const fs::path p = fs::temp_directory_path() / name;
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    return p;
}

} // namespace

static void TestImage()
{
    SECTION("образ прошивки: проверка");
    auto img = fw::CheckImage(FakeImage(80000, "1.4"));
    CHECK(img.ok && img.version == "1.4" && img.build == "Oct  8 2026 10:00:00");
    CHECK(!fw::CheckImage(std::vector<std::uint8_t>(100, 0)).ok);
    CHECK(!fw::CheckImage(FakeImage(400 * 1024, "1.4")).ok); // залез бы в сектор настроек
    auto bad = FakeImage(80000, "1.4");
    bad[3] = 0x10; // стек 0x10020000
    CHECK(!fw::CheckImage(bad).ok);
    bad = FakeImage(80000, "1.4");
    bad[4] = 0x98; // чётный вектор сброса
    CHECK(!fw::CheckImage(bad).ok);
    bad = FakeImage(80000, "1.4");
    bad[0x2000] = 'X'; // нет строки прибора
    CHECK(!fw::CheckImage(bad).ok);

    // Настоящий образ из сборки прошивки (если собран)
    for (const char* rel : {"../BWM427_Inclinometer/build_pc/BWM427_Inclinometer.bin",
                            "../BWM427_Inclinometer/build/BWM427_Inclinometer.bin",
                            "../../BWM427_Inclinometer/build_pc/BWM427_Inclinometer.bin"})
    {
        const fs::path p = fs::path(KRENOMER_TEST_DATA_DIR) / ".." / ".." / rel;
        std::vector<std::uint8_t> d;
        if (fw::ReadFile(p, d))
        {
            const auto real = fw::CheckImage(d);
            CHECK_MSG(real.ok, "%s", real.error.c_str());
            CHECK(!real.version.empty());
            std::printf("  настоящий образ: %zu байт, версия %s, сборка %s\n", d.size(), real.version.c_str(),
                        real.build.c_str());
            break;
        }
    }

    SECTION("прогресс dfu-util");
    CHECK(fw::ParseDfuProgress("Download\t[=========                ]  36%        28672 bytes") == 36);
    CHECK(fw::ParseDfuProgress("Upload\t[=========================] 100%        81920 bytes") == 100);
    CHECK(fw::ParseDfuProgress("Download\t[                         ]   0%            0 bytes") == 0);
    CHECK(fw::ParseDfuProgress("Opening DFU capable USB device...") == -1);
    CHECK(fw::ParseDfuProgress("100% done") == -1);
}

static void TestUpdateDemo()
{
    SECTION("обновление с имитатором: все шаги");
    auto sim = std::make_shared<SimDevice>(3, 1791400000);
    DeviceLink link(std::make_unique<SimConnection>(sim), false);
    std::int64_t t = 0;
    fw::DemoDfuBackend backend(sim, [&] { return t; });
    fw::Updater up(&link, &backend);
    auto run = [&](std::int64_t ms) {
        for (std::int64_t e = t + ms; t < e; t += 20)
        {
            link.Step(t);
            up.Tick(t);
        }
    };
    run(1500);
    CHECK(link.IsConnected());
    const fs::path img = WriteTemp(FakeImage(81920, "1.4"), L"krenomer_test_fw.bin");
    CHECK(up.Start(text::PathToUtf8(img), false, t));
    CHECK(up.GetStep() == fw::Updater::Step::Boot);
    bool sawDfu = false, sawDownload = false, sawReadback = false;
    for (int i = 0; i < 3000 && up.Busy(); i++)
    {
        run(20);
        sawDfu |= sim->InDfu();
        sawDownload |= up.GetStep() == fw::Updater::Step::Download;
        sawReadback |= up.GetStep() == fw::Updater::Step::Readback;
    }
    CHECK_MSG(up.GetStep() == fw::Updater::Step::Done, "шаг %d, ошибка: %s", static_cast<int>(up.GetStep()),
              up.Error().c_str());
    CHECK(sawDfu && sawDownload && sawReadback);
    CHECK(up.NewVersion() == "1.3"); // имитатор «прошивается» без смены версии
    CHECK(link.IsConnected());
    bool verified = false;
    for (const auto& l : up.Log())
        verified |= l.text.find("проверка прошла") != std::string::npos;
    CHECK(verified);

    SECTION("обновление: идёт запись — отказ, затем boot force");
    auto c = sim->GetControls();
    c.recSwitch = true;
    sim->SetControls(c);
    run(1500);
    CHECK(up.Start(text::PathToUtf8(img), false, t));
    run(1000);
    CHECK(up.GetStep() == fw::Updater::Step::Failed && up.NeedsForce());
    CHECK(up.Start(text::PathToUtf8(img), true, t));
    for (int i = 0; i < 3000 && up.Busy(); i++)
        run(20);
    CHECK_MSG(up.GetStep() == fw::Updater::Step::Done, "%s", up.Error().c_str());

    SECTION("обновление: плохой образ, нет прибора");
    const fs::path badImg = WriteTemp(std::vector<std::uint8_t>(5000, 0), L"krenomer_test_bad.bin");
    CHECK(!up.Start(text::PathToUtf8(badImg), false, t));
    CHECK(up.Error().find("образ не годится") != std::string::npos);
    CHECK(!up.Start("C:\\нет\\такого\\файла.bin", false, t));
    std::error_code ec;
    fs::remove(img, ec);
    fs::remove(badImg, ec);
}

static void TestProcess()
{
#ifdef _WIN32
    SECTION("внешний процесс: вывод и код выхода");
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::string cmd = text::PathToUtf8(fs::path(sys) / L"cmd.exe");
    std::string err;
    auto p = fw::WinProcess::Start(cmd, {"/c", "echo line1&& echo line2&& exit 3"}, &err);
    CHECK_MSG(p != nullptr, "%s", err.c_str());
    if (p)
    {
        for (int i = 0; i < 500 && p->Running(); i++)
            Sleep(10);
        Sleep(50);
        const auto lines = p->TakeLines();
        CHECK(lines.size() == 2 && lines[0] == "line1" && lines[1] == "line2");
        CHECK(p->ExitCode() == 3);
    }
    CHECK(fw::WinProcess::Start("C:\\нет\\dfu-util.exe", {"-l"}, &err) == nullptr && !err.empty());

    fw::RealDfuBackend real;
    std::vector<std::string> searched;
    const std::string dfu = real.FindDfuUtil(&searched);
    CHECK(searched.size() >= 3);
    std::printf("  dfu-util: %s\n", dfu.empty() ? "не найден" : dfu.c_str());
    bool driver = false;
    std::printf("  USB DFU 0483:DF11 сейчас: %s\n", real.DfuDevicePresent(&driver) ? "есть" : "нет");
    if (!dfu.empty())
    {
        auto v = fw::WinProcess::Start(dfu, {"--version"}, &err);
        CHECK(v != nullptr);
        if (v)
        {
            for (int i = 0; i < 500 && v->Running(); i++)
                Sleep(10);
            Sleep(50);
            const auto lines = v->TakeLines();
            CHECK(!lines.empty() && lines[0].rfind("dfu-util", 0) == 0);
        }
    }
#endif
}

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    TestImage();
    TestUpdateDemo();
    TestProcess();
    return TestSummary();
}
