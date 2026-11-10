// Synthetic, headless two-post report contract. No vessel/hardware/normative claim.
// --repro-empty-post failed four checks on base c3ab941 and now guards the fix:
// a file without selected-axis data must not satisfy the post-completeness check.
#include "TestUtil.hpp"
#include "core/HeelProtocol.hpp"
#include <iomanip>
#include <sstream>

namespace
{
constexpr double kSqrt3 = 1.7320508075688772935;
constexpr double kMeanH = (kSqrt3 + 1.0) / 2.0;
constexpr double kRsH = 1.5278874536821952234; // 24/(5*pi), independently: 100/(100*37.5*pi/180)

heel::Settings Settings(int axis = 0)
{
    heel::Settings s;
    s.D = 100; s.P = 10; s.L = 10; s.axis = axis;
    s.filter.method = filt::Method::Mean;
    s.filter.window = filt::Window::Strict;
    return s;
}

// Each plateau lasts 60 s. The other axis remains finite, so a missing selected
// axis tests the actual CSV parser's partial-axis handling, not a fabricated Log.
heel::File Csv(const std::string& name, const std::vector<double>& levels, int axis = 0, int hz = 10)
{
    std::ostringstream csv;
    csv << std::setprecision(15)
        << "RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";
    for (std::size_t p = 0; p < levels.size(); ++p)
        for (int j = 0; j < 60 * hz; ++j)
        {
            csv << "101;102;100;100;";
            auto value = [&](double v) { if (std::isfinite(v)) csv << v; };
            value(axis == 0 ? levels[p] : 0.25); csv << ';';
            value(axis == 1 ? levels[p] : 0.25);
            csv << ";12;" << (p * 60000 + j * 1000 / hz) << '\n';
        }
    heel::File f;
    f.name = name;
    f.path = name;
    heel::NameInfo(f);
    CHECK(csvlog::Parse(csv.str(), f.log));
    return f;
}

std::vector<heel::Position> Windows()
{
    std::vector<heel::Position> pos(3);
    for (int i = 0; i < 3; ++i)
    {
        pos[i].t0 = i * 60 + 5; pos[i].t1 = i * 60 + 45;
        pos[i].arm = i == 0 ? 0.0 : i == 1 ? 10.0 : -10.0;
    }
    return pos;
}

const xlsx::Sheet* Sheet(const std::vector<xlsx::Sheet>& book, const std::string& name)
{
    for (const auto& s : book) if (s.name == name) return &s;
    CHECK_MSG(false, "missing sheet %s", name.c_str());
    return nullptr;
}

int Header(const xlsx::Sheet& sheet, const std::string& name, std::size_t& row)
{
    for (row = 0; row < sheet.rows.size(); ++row)
        for (std::size_t c = 0; c < sheet.rows[row].size(); ++c)
            if (sheet.rows[row][c].text == name) return static_cast<int>(c);
    CHECK_MSG(false, "missing header %s", name.c_str());
    return -1;
}

int CheckState(const heel::RsCase& k, const std::string& id)
{
    for (const auto& c : k.checks) if (c.id == id) return c.state;
    CHECK_MSG(false, "missing check %s", id.c_str());
    return -2;
}

void CsvAndDiscrete()
{
    SECTION("CSV addresses, Calc precedence, Raw-Offset fallback, remapping, NaN tail");
    csvlog::Log fallback;
    CHECK(csvlog::Parse("RawX;RawY;OffsetX;OffsetY;Ms\n9;8;4;2;0\n", fallback));
    CHECK_NEAR(fallback.rows[0].calcX, 5, 0, "RawX - OffsetX only without CalcX column");
    CHECK_NEAR(fallback.rows[0].calcY, 6, 0, "RawY - OffsetY");
    CHECK(csvlog::Parse("RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;Ms\n9;8;4;2;;6;0\n", fallback));
    CHECK(std::isnan(fallback.rows[0].calcX)); // present-but-empty Calc is not reconstructed
    CHECK(heel::DefaultPost(2) == heel::kBow && heel::DefaultPost(3) == heel::kStern);

    auto files = std::vector<heel::File>{Csv("2026-11-10_M001_D2.CSV", {30}, 0, 10),
                                       Csv("2026-11-10_M001_D3.CSV", {45}, 0, 1)};
    CHECK(files[0].sensor == 2 && files[1].sensor == 3);
    CHECK_NEAR(files[0].log.rows[0].calcX, 30, 0, "Calc is used directly despite Raw=101 and Offset=100");
    auto ms = heel::Group(files, {});
    CHECK(ms.size() == 1);
    if (ms.size() != 1) return;
    CHECK(ms[0].post[0].file == 0 && ms[0].post[1].file == 1);
    const auto s = Settings();
    const auto sum = heel::Compute(s, files, ms);
    CHECK(sum.n == 2 && sum.measurements == 1);
    CHECK(ms[0].post[0].st.n > ms[0].post[1].st.n); // 10:1 sample count must not weight h
    CHECK_NEAR(ms[0].post[0].res.h, kSqrt3, 1e-12, "30 deg: h = cot(30) = sqrt(3)");
    CHECK_NEAR(ms[0].post[1].res.h, 1, 1e-12, "45 deg: h = cot(45) = 1");
    CHECK_NEAR(sum.h, kMeanH, 1e-12, "equal weight of the two post h values");
    CHECK_NEAR(sum.sd, (kSqrt3 - 1) / std::sqrt(2.0), 1e-12, "sample SD across post h values");
    CHECK(std::fabs(sum.h - 1.3032253728412058) > 0.05); // cot(mean angle 37.5) is a different result
    auto mapped = heel::Group(files, {{2, 1}, {3, 0}});
    CHECK(mapped[0].post[0].file == 1 && mapped[0].post[1].file == 0);

    auto rs = heel::RsFromMeasurements(s, files, ms, {}, sum.h);
    CHECK(rs.virtualZero && rs.r.m.size() == 1);
    if (!rs.r.m.empty())
    {
        CHECK_NEAR(rs.r.m[0].dTheta, 37.5, 1e-12, "table uses mean INCREMENT, not mean h");
        CHECK_NEAR(rs.r.m[0].h, kRsH, 1e-12, "table uses radians, not tangent");
    }
    auto noBow = heel::Group(files, {{2, -1}});
    const auto one = heel::Compute(s, files, noBow);
    CHECK(one.n == 1);
    CHECK_NEAR(one.h, 1, 1e-12, "missing bow leaves stern h");
    const auto oneRs = heel::RsFromMeasurements(s, files, noBow, {}, one.h);
    CHECK(CheckState(oneRs, "2 прибора") == 0);
    CHECK(CheckState(oneRs, "сверка постов") == -1);

    files[0].log.rows.back().calcX = heel::kNaN;
    const auto stats = heel::TailStats(files[0].log, 0, s.windowS);
    CHECK(stats.n == ms[0].post[0].st.n - 1);
    CHECK_NEAR(stats.mean, 30, 0, "NaN omitted, never converted to zero");
    ms[0].use = false;
    CHECK(heel::Compute(s, files, ms).n == 0);
}

void CheckBooks(const heel::Settings& s, const std::vector<heel::File>& files,
                const std::vector<heel::Measurement>& ms, const std::vector<heel::Position>& pos,
                const heel::Summary& sum, const heel::RsCase& rs)
{
    SECTION("short/full XLSX retain both posts and distinct angle/h aggregation");
    heel::ReportInfo info; info.program = "SYNTHETIC TWO-POST TEST: 30/45 deg are not vessel data";
    const auto steps = heel::BuildSteps(s, files, ms, true, 0, pos, sum, rs);
    CHECK(!steps.empty());
    const auto shortBook = heel::BuildBook(s, files, ms, true, 0, pos, sum, rs, info, {});
    const auto fullBook = heel::BuildBook(s, files, ms, true, 0, pos, sum, rs, info, {}, nullptr, &steps);
    CHECK(fullBook.size() == shortBook.size() + 1);
    CHECK(fullBook.size() > 1 && fullBook[1].name == "Расчёт по шагам");
    for (const auto* book : {&shortBook, &fullBook})
    {
        const auto* pr = Sheet(*book, "Протокол");
        const auto* positions = Sheet(*book, "Положения");
        const auto* checks = Sheet(*book, "Проверки");
        CHECK(Sheet(*book, "Сырые_данные") != nullptr);
        if (!pr || !positions || !checks) continue;
        std::size_t row = 0;
        const int cb = Header(*pr, "Крен Нос", row);
        const auto headRow = row;
        const int cs = Header(*pr, "Крен Корма", row);
        const int ca = Header(*pr, "Крен, среднее", row);
        const int ch = Header(*pr, "h, м", row);
        if (cb >= 0 && cs >= 0 && ca >= 0 && ch >= 0 && headRow + 2 < pr->rows.size())
        {
            const auto& data = pr->rows[headRow + 2]; // second position, first transfer
            CHECK_NEAR(data[cb].num, 32, 1e-12, "protocol bow absolute level");
            CHECK_NEAR(data[cs].num, 52, 1e-12, "protocol stern absolute level after one reverse");
            CHECK_NEAR(data[ca].num, 42, 1e-12, "displayed angle is mean absolute level");
            CHECK_NEAR(data[ch].num, kMeanH, 1e-12, "displayed h is mean of post heights");
        }
        // Original detailed table is retained and has separate results per post.
        CHECK(positions->rows.size() >= 7);
        if (positions->rows.size() >= 7)
        {
            CHECK(positions->rows[3][5].text == "Нос" && positions->rows[4][5].text == "Корма");
            CHECK_NEAR(positions->rows[3][13].num, kSqrt3, 1e-12, "detailed bow h");
            CHECK_NEAR(positions->rows[4][13].num, 1, 1e-12, "detailed stern h");
        }
        CHECK(Header(*checks, "h_i Нос, м", row) >= 0);
        CHECK(Header(*checks, "h_i Корма, м", row) >= 0);
        // Parse serialized ZIP with the reader, not a second invocation of report math.
        const auto zip = xlsx::Build(*book);
        CHECK(zip.size() > 1000 && zip.substr(0, 2) == "PK");
        std::vector<xlsxread::Sheet> read;
        std::string error;
        CHECK_MSG(xlsxread::ToCsv(zip, read, &error), "%s", error.c_str());
        CHECK(read.size() == book->size());
    }
}

void Continuous(int axis)
{
    SECTION(axis == 0 ? "continuous X: own zero per post, opposite orientation" : "continuous Y: own zero per post, opposite orientation");
    const auto s = Settings(axis);
    const auto raw = std::vector<heel::File>{Csv("2026-11-10_M002_D2.CSV", {2, 32, -28}, axis),
                                           Csv("2026-11-10_M002_D3.CSV", {-7, -52, 38}, axis)};
    const auto files = heel::ApplyInvert(raw, {3});
    auto ms = heel::Group(files, {});
    CHECK(ms.size() == 1);
    if (ms.size() != 1) return;
    CHECK(!files[0].inverted && files[1].inverted);
    CHECK_NEAR(raw[1].log.rows[0].Calc(axis), -7, 0, "raw source untouched");
    CHECK_NEAR(files[1].log.rows[0].Calc(axis), 7, 0, "reverse selected axis exactly once");
    CHECK_NEAR(files[1].log.rows[0].rawX, -101, 0, "reverse also transforms Raw");
    CHECK_NEAR(files[1].log.rows[0].offY, -100, 0, "reverse also transforms Offset");
    auto pos = Windows();
    const auto sum = heel::ComputePositions(s, files, ms[0], pos);
    CHECK(sum.n == 4 && sum.measurements == 3);
    CHECK(pos[0].res[0].reference && pos[0].res[1].reference);
    CHECK_NEAR(pos[1].res[0].dTheta, 30, 1e-12, "bow has own 2 deg zero");
    CHECK_NEAR(pos[1].res[1].dTheta, 45, 1e-12, "stern has own 7 deg zero");
    CHECK_NEAR(pos[2].res[0].dTheta, -30, 1e-12, "bow opposite transfer");
    CHECK_NEAR(pos[2].res[1].dTheta, -45, 1e-12, "stern opposite transfer");
    for (int i : {1, 2})
    {
        CHECK_NEAR(pos[i].res[0].h, kSqrt3, 1e-12, "bow height both sides");
        CHECK_NEAR(pos[i].res[1].h, 1, 1e-12, "stern height both sides");
    }
    CHECK_NEAR(sum.h, kMeanH, 1e-12, "all four point heights have equal weight");
    const auto rs = heel::RsFromPositions(s, files, ms[0], pos, {}, sum.h);
    CHECK(rs.r.m.size() == 2 && rs.r.n == 2 && rs.c.instruments == 2);
    CHECK(CheckState(rs, "2 прибора") == 1);
    CHECK(CheckState(rs, "сверка постов") == 0); // mismatch warns; does not auto-discard the posts
    CHECK(!rs.r.ok664 && std::isnan(rs.r.err)); // two sensors do not create eight transfers
    if (rs.r.m.size() == 2)
    {
        CHECK_NEAR(rs.r.m[0].dM, 100, 0, "first moment = 10 t * 10 m");
        CHECK_NEAR(rs.r.m[0].dThetaPost[0], 30, 1e-12, "bow increment");
        CHECK_NEAR(rs.r.m[0].dThetaPost[1], 45, 1e-12, "stern increment");
        CHECK_NEAR(rs.r.m[0].hPost[0], 1.909859317102744, 1e-12, "bow table h = 6/pi");
        CHECK_NEAR(rs.r.m[0].hPost[1], 1.2732395447351628, 1e-12, "stern table h = 4/pi");
        CHECK_NEAR(rs.r.m[0].h, kRsH, 1e-12, "combined h is reciprocal mean increment");
        CHECK_NEAR(rs.r.m[1].dTheta, -75, 1e-12, "second increment averages -60/-90");
        CHECK_NEAR(rs.r.m[1].h, kRsH, 1e-12, "second table height");
        CHECK_NEAR(rs.r.maxSpread, 30, 1e-12, "spread compares increments, not absolute zero offsets");
    }
    for (int post : {0, 1})
    {
        auto selected = s; selected.rsInstrument = post;
        const auto one = heel::RsFromPositions(selected, files, ms[0], pos, {}, sum.h);
        CHECK_NEAR(one.r.m[0].h, post == 0 ? 1.909859317102744 : 1.2732395447351628, 1e-12, "explicit instrument selector");
    }
    auto corrected = s; corrected.rsDh = 0.1;
    CHECK_NEAR(heel::RsFromPositions(corrected, files, ms[0], pos, {}, sum.h).hMain,
               kMeanH - 0.1, 1e-12, "main from initial is mean h minus entered correction");
    corrected.rsMain = 1;
    CHECK_NEAR(heel::RsFromPositions(corrected, files, ms[0], pos, {}, sum.h).hMain,
               kRsH - 0.1, 1e-12, "table main selected separately");

    auto wrong = Windows();
    const auto wrongSum = heel::ComputePositions(s, raw, ms[0], wrong);
    CHECK(wrongSum.n == 2); // signed arms reject opposite-sign uncorrected stern
    CHECK(!wrong[1].res[1].counted && !wrong[2].res[1].counted);
    CHECK_NEAR(wrongSum.h, kSqrt3, 1e-12, "no automatic reverse from the number of sensors");

    auto detected = heel::FindPositions(s, files, ms[0]);
    CHECK(detected.size() == 3);
    // A wildly different stern must not change boundaries chosen on the bow.
    auto different = files;
    different[1] = Csv("2026-11-10_M002_D3.CSV", {12, 12, 12}, axis);
    const auto same = heel::FindPositions(s, different, ms[0]);
    CHECK(same.size() == detected.size());
    for (std::size_t i = 0; i < same.size() && i < detected.size(); ++i)
    {
        CHECK_NEAR(same[i].t0, detected[i].t0, 0, "common windows come only from main post");
        CHECK_NEAR(same[i].t1, detected[i].t1, 0, "stern is measured inside bow windows");
    }
    auto sternOnly = heel::Group(files, {{2, -1}});
    CHECK(heel::MainPost(sternOnly[0]) == 1);
    CHECK(heel::FindPositions(s, files, sternOnly[0]).size() == 3);
    if (axis == 0) CheckBooks(s, files, ms, pos, sum, rs);
}

void MissingBetweenPositions()
{
    SECTION("partial NaN: use only consecutive valid post pairs, no zero substitution");
    const auto s = Settings();
    const std::vector<heel::File> files = {Csv("2026-11-10_M003_D2.CSV", {0, 30, 0}),
                                         Csv("2026-11-10_M003_D3.CSV", {0, heel::kNaN, 0})};
    const auto ms = heel::Group(files, {});
    CHECK(ms.size() == 1);
    if (ms.size() != 1) return;
    auto pos = Windows();
    const auto sum = heel::ComputePositions(s, files, ms[0], pos);
    CHECK(sum.n == 1); // only bow's nonzero position; the final zero is below the threshold
    CHECK(!pos[1].res[1].counted && std::isnan(pos[1].Level(1)));
    const auto rs = heel::RsFromPositions(s, files, ms[0], pos, {}, sum.h);
    CHECK(rs.r.m.size() == 2);
    CHECK(rs.c.instruments == 1);
    CHECK(CheckState(rs, "2 прибора") == 0);
    if (rs.r.m.size() == 2)
    {
        CHECK_NEAR(rs.r.m[0].dTheta, 30, 1e-12, "one valid increment survives missing stern");
        CHECK_NEAR(rs.r.m[1].dTheta, -30, 1e-12, "stern needs BOTH adjacent endpoints");
        CHECK(std::isnan(rs.r.m[0].dThetaPost[1]) && std::isnan(rs.r.m[1].dThetaPost[1]));
        CHECK(std::isnan(rs.r.maxSpread));
    }

    SECTION("unequal coverage weights individual heights, not row averages");
    const std::vector<heel::File> uneven = {Csv("2026-11-10_M005_D2.CSV", {0, 30, 45}),
                                          Csv("2026-11-10_M005_D3.CSV", {0, 45, heel::kNaN})};
    const auto um = heel::Group(uneven, {});
    auto up = Windows(); up[2].arm = 20;
    const auto us = heel::ComputePositions(s, uneven, um[0], up);
    CHECK(us.n == 3);
    // Three available heights are sqrt(3), 1, 2, hence (sqrt(3)+3)/3.
    CHECK_NEAR(us.h, (kSqrt3 + 3) / 3, 1e-12, "three points have equal weight despite uneven coverage");
    CHECK(std::fabs(us.h - (kSqrt3 + 5) / 4) > 0.1);
    const auto ur = heel::RsFromPositions(s, uneven, um[0], up, {}, us.h);
    CHECK(ur.r.m.size() == 2 && ur.r.n == 2);
    CHECK(ur.c.instruments == 1);
    CHECK(CheckState(ur, "2 прибора") == 0);
    if (ur.r.m.size() == 2)
    {
        CHECK(std::isfinite(ur.r.m[0].dThetaPost[1])); // first transfer has both posts
        CHECK(std::isnan(ur.r.m[1].dThetaPost[1])); // second lacks stern; bow still moved 15 deg
        CHECK(std::isfinite(ur.r.m[1].h) && !ur.r.m[1].excluded);
    }
    // Only included finite-h transfers determine completeness. Excluding the
    // incomplete transfer leaves one fully covered transfer and restores 2.
    const auto excluded = heel::RsFromPositions(s, uneven, um[0], up, {{3, 0}}, us.h);
    CHECK(excluded.r.n == 1);
    CHECK(excluded.c.instruments == 2);
    CHECK(CheckState(excluded, "2 прибора") == 1);

    SECTION("constant different post zero offsets cancel in increment spread");
    const std::vector<heel::File> offsets = {Csv("2026-11-10_M006_D2.CSV", {2, 32, -28}),
                                           Csv("2026-11-10_M006_D3.CSV", {7, 37, -23})};
    const auto om = heel::Group(offsets, {});
    auto op = Windows();
    const auto os = heel::ComputePositions(s, offsets, om[0], op);
    const auto ok = heel::RsFromPositions(s, offsets, om[0], op, {}, os.h);
    CHECK_NEAR(os.h, kSqrt3, 1e-12, "own zero gives same height on both posts");
    CHECK_NEAR(ok.r.maxSpread, 0, 1e-12, "5 deg absolute offset is not an increment mismatch");
    CHECK(CheckState(ok, "сверка постов") == 1);
}

void ReproEmptyPost()
{
    SECTION("fixed regression: empty selected-axis post fails post-completeness check");
    for (int axis : {0, 1})
    {
        const auto s = Settings(axis);
        const std::vector<heel::File> files = {Csv("2026-11-10_M004_D2.CSV", {0, 30, -30}, axis),
                                             Csv("2026-11-10_M004_D3.CSV", {heel::kNaN, heel::kNaN, heel::kNaN}, axis)};
        auto ms = heel::Group(files, {});
        CHECK(ms.size() == 1);
        if (ms.size() != 1) continue;
        auto pos = Windows();
        const auto sum = heel::ComputePositions(s, files, ms[0], pos);
        const auto k = heel::RsFromPositions(s, files, ms[0], pos, {}, sum.h);
        CHECK(sum.n == 2 && std::isnan(pos[1].Level(1)));
        std::printf("EMPTY_POST axis=%d continuous instruments=%d check=%d finite_h=%d\n",
                    axis, k.c.instruments, CheckState(k, "2 прибора"), std::isfinite(k.hMain));
        CHECK(k.c.instruments == 1);
        CHECK_MSG(CheckState(k, "2 прибора") == 0, "empty selected-axis stern fails post completeness");
        const auto discreteSum = heel::Compute(s, files, ms);
        const auto d = heel::RsFromMeasurements(s, files, ms, {}, discreteSum.h);
        std::printf("EMPTY_POST axis=%d discrete instruments=%d check=%d\n", axis, d.c.instruments, CheckState(d, "2 прибора"));
        CHECK(d.c.instruments == 1);
        CHECK_MSG(CheckState(d, "2 прибора") == 0, "discrete: do not infer usable instruments from filenames");
    }
}
} // namespace

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool repro = false;
    for (int i = 1; i < argc; ++i) repro |= std::string(argv[i]) == "--repro-empty-post";
    if (repro) ReproEmptyPost();
    else { CsvAndDiscrete(); Continuous(0); Continuous(1); MissingBetweenPositions(); }
    return TestSummary();
}
