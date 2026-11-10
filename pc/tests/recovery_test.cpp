#include "TestUtil.hpp"
#include "fw/RomRecovery.hpp"
#include <fstream>
using Bytes = std::vector<unsigned char>;
static void Put(Bytes &b, std::size_t o, std::uint32_t v)
{
    for (int i = 0; i < 4; i++)
        b[o + i] = static_cast<unsigned char>(v >> (8 * i));
}
static void Save(const std::filesystem::path &path, const Bytes &b)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(b.data()), b.size());
}
static Bytes Image()
{
    Bytes b(544, 0);
    b[0] = 0xe9;
    b[1] = 1;
    b[12] = 18;
    b[23] = 1;
    Put(b, 28, 512);
    Put(b, 32, 0xabcd5432);
    std::string v = "1.5.0-esp32", p = "krenomer-esp32";
    std::copy(v.begin(), v.end(), b.begin() + 48);
    std::copy(p.begin(), p.end(), b.begin() + 80);
    unsigned char c = 0xef;
    for (std::size_t i = 32; i < b.size(); ++i)
        c ^= b[i];
    b.resize(560);
    b.back() = c;
    auto h = fw::Sha256(b.data(), b.size());
    b.insert(b.end(), h.begin(), h.end());
    return b;
}
static Bytes Table()
{
    Bytes b(192, 0xff);
    const std::uint32_t offsets[] = {0x9000, 0xf000, 0x11000, 0x20000, 0x620000},
                        sizes[] = {0x6000, 0x2000, 0x1000, 0x600000, 0x600000};
    const unsigned char types[] = {1, 1, 1, 0, 0}, subs[] = {2, 0, 1, 0x10, 0x11};
    const char *labels[] = {"nvs", "otadata", "phy_init", "ota_0", "ota_1"};
    for (int i = 0; i < 5; i++)
    {
        std::size_t p = i * 32;
        std::fill(b.begin() + p, b.begin() + p + 32, static_cast<unsigned char>(0));
        b[p] = 0xaa;
        b[p + 1] = 0x50;
        b[p + 2] = types[i];
        b[p + 3] = subs[i];
        Put(b, p + 4, offsets[i]);
        Put(b, p + 8, sizes[i]);
        std::copy(labels[i], labels[i] + std::strlen(labels[i]), b.begin() + p + 12);
    }
    b[160] = b[161] = 0xeb;
    auto md5 = fw::esprom::Md5(Bytes(b.begin(), b.begin() + 160));
    std::copy(md5.begin(), md5.end(), b.begin() + 176);
    return b;
}
int main(int argc, char **argv)
{
    testutil::Init(argc, argv);
    auto dir = std::filesystem::current_path() / "recovery-test-package";
    std::filesystem::create_directories(dir);
    auto app = Image();
    Save(dir / "app.bin", app);
    Save(dir / "boot.bin", app);
    Save(dir / "table.bin", Table());
    Save(dir / "ota.bin", Bytes(8192, 0xff));
    auto image = fw::CheckEspImage(app);
    std::string valid =
        "{\"format\":1,\"board\":\"tab5\",\"chip\":\"esp32p4\",\"protocol\":2,\"version\":\"1.5.0-esp32\",\"size\":" +
        std::to_string(app.size()) + ",\"sha256\":\"" + image.sha +
        "\",\"factory_parts\":{\"0x2000\":\"boot.bin\",\"0x8000\":\"table.bin\",\"0xf000\":\"ota.bin\",\"0x20000\":"
        "\"app.bin\"}}";
    auto load = [&](std::string s) {
        std::ofstream f(dir / "package.json");
        f << s;
        f.close();
        return fw::LoadFactory(text::PathToUtf8(dir / "package.json"));
    };
    SECTION("Factory preflight verifies four parts and preserves NVS boundaries without I/O");
    auto plan = load(valid);
    CHECK_MSG(plan.ok, "%s", plan.error.c_str());
    CHECK(plan.parts.size() == 4);
    CHECK(plan.version == "1.5.0-esp32");
    SECTION("Reject wrong board, escaped path, stale otadata and corrupt package");
    auto bad = valid;
    bad.replace(bad.find("tab5"), 4, "other");
    CHECK(!load(bad).ok);
    bad = valid;
    bad.replace(bad.find("app.bin"), 7, "../app.bin");
    CHECK(!load(bad).ok);
    Save(dir / "ota.bin", Bytes(8192, 0));
    CHECK(!load(valid).ok);
    Save(dir / "ota.bin", Bytes(8192, 0xff));
    auto corrupt = app;
    corrupt[400] ^= 1;
    Save(dir / "app.bin", corrupt);
    CHECK(!load(valid).ok);
    Save(dir / "app.bin", app);
    bad = valid;
    bad.replace(bad.find(image.sha), 64, std::string(64, '0'));
    CHECK(!load(bad).ok);
    SECTION("Explicit port validation does not open devices");
    CHECK(fw::WinRomTransport::ValidPort("COM10"));
    CHECK(!fw::WinRomTransport::ValidPort("COM0"));
    CHECK(!fw::WinRomTransport::ValidPort("AUTO"));
    CHECK(!fw::WinRomTransport::ValidPort("\\\\.\\COM1"));
    if (argc > 2 && std::string(argv[1]) == "--manifest")
    {
        auto actual = fw::LoadFactory(argv[2]);
        CHECK_MSG(actual.ok, "%s", actual.error.c_str());
        std::printf("Actual factory preflight: %s, %zu parts\n", actual.version.c_str(), actual.parts.size());
    }
    for (const char *name : {"app.bin", "boot.bin", "table.bin", "ota.bin", "package.json"})
    {
        std::error_code ec;
        std::filesystem::remove(dir / name, ec);
    }
    std::error_code ec;
    std::filesystem::remove(dir, ec);
    return TestSummary();
}
