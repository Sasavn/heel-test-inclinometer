// Ordinary workbook regression. Uses anonymized historical samples by default;
// KRENOMER_M025_TEST_DIR optionally selects immutable real M025 input copies.
#include "TestUtil.hpp"
#include "core/HeelProtocol.hpp"
#include "core/Png.hpp"
#include <fstream>
#include <future>
#include <csignal>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    std::signal(SIGABRT, [](int) { std::_Exit(86); }); // failed checked-STL subprocess: no crash dialog
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#endif
    bool full = argc > 1 && std::string(argv[1]) == "--full";
    int side = 1;
    fs::path input = fs::path(KRENOMER_TEST_DATA_DIR) / "m021";
    bool real = false;
#ifdef _WIN32
    if (const auto* p = _wgetenv(L"KRENOMER_M025_TEST_DIR")) { input = p; real = true; }
#else
    if (const auto* p = std::getenv("KRENOMER_M025_TEST_DIR")) { input = p; real = true; }
#endif
    heel::Settings s;
    s.units.model = true;
    s.axis = real ? 1 : 0;
    // Deliberately synthetic layout-test parameters, NOT M025 vessel data.
    s.D = 1; s.P = 1; s.B = 500; s.L = 10; s.armFrom = side;
    std::vector<fs::path> paths;
    for (const auto& f : fs::directory_iterator(input))
        if (f.path().extension() == ".CSV") paths.push_back(f.path());
    auto loaded = heel::LoadAll(paths, text::PathToUtf8(input), nullptr, &s);
    auto meas = heel::Group(loaded.files, {{2, 0}, {3, 1}});
    CHECK(!meas.empty());
    if (meas.empty()) return TestSummary();
    auto pos = heel::FindPositions(s, loaded.files, meas.front());
    CHECK(!pos.empty());
    for (std::size_t i = 0; i < pos.size(); ++i) pos[i].arm = static_cast<double>(i) * 10;
    auto sum = heel::ComputePositions(s, loaded.files, meas.front(), pos);
    auto rs = heel::RsFromPositions(s, loaded.files, meas.front(), pos, {}, sum.h);
    const auto steps = heel::BuildSteps(s, loaded.files, meas, true, 0, pos, sum, rs);
    heel::ReportInfo info;
    info.program = "DIAGNOSTIC LAYOUT TEST - D/P/B ARE SYNTHETIC";
    std::string warning;
    std::vector<unsigned char> rgb(12 * 12 * 3, 255);
    heel::ReportImage image;
    image.caption = "Layout test"; image.png = png::EncodeRgb(12, 12, rgb);
    image.w = image.h = 12; image.series = true;
    std::printf("BUILD_BOOK real_M025=%d posts=2 from_port=1 full=%d positions=%zu\n", real, full, pos.size());
    std::fflush(stdout);
    // Same worker boundary as App::TickProcess; exceptions propagate normally
    // through future.get(), and checked STL diagnoses the original bad index.
    auto saved = std::async(std::launch::async, [&] {
        return heel::BuildBook(s, loaded.files, meas, true, 0, pos, sum, rs, info, {image}, &warning,
                               full ? &steps : nullptr);
    });
    auto book = saved.get();
    CHECK(!book.empty());
    CHECK(book.front().name == "Протокол");
    bool sideHeader = false, hHeader = false;
    for (const auto& row : book.front().rows)
        for (const auto& cell : row)
        {
            sideHeader |= cell.text.find("От ЛБ") != std::string::npos;
            hHeader |= cell.text == "h, м";
        }
    CHECK(sideHeader && hHeader);
    if (full) CHECK(book.size() > 1 && book[1].name == "Расчёт по шагам");
    const auto bytes = xlsx::Build(book);
    CHECK(bytes.size() > 1000 && bytes.substr(0, 2) == "PK");
    std::printf("BOOK_OK bytes=%zu sheets=%zu\n", bytes.size(), book.size());

    SECTION("wide rows keep every cell");
    xlsx::Sheet wide("Wide");
    heel::proto::Grid grid(wide, 10);
    std::vector<heel::proto::Grid::Col> cells;
    for (int i = 0; i < 15; ++i) cells.push_back(heel::proto::H(std::to_string(i)));
    grid.Row(cells);
    CHECK(grid.Cols() == 15 && wide.rows[0].size() == 15);
    CHECK(wide.rows[0].back().text == "14");
    bool rejected = false;
    try { grid.Put(0, 15, 14, xlsx::Text("invalid"), {}); }
    catch (const std::out_of_range&) { rejected = true; }
    CHECK(rejected);
    return TestSummary();
}
