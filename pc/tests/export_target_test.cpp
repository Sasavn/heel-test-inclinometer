#include "core/ExportTarget.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#endif
namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); ++checks; } while (0)
int main()
{
    unsigned checks = 0;
    auto fixture = exporttarget::Reserve(fs::temp_directory_path(), "krenomer-export-target-test");
    if (!fixture) { std::cerr << fixture.error; return 1; }
    try
    {
        const auto unique = exporttarget::UniqueFileNames({"Нос.png", "нос.PNG", "Нос:.png", "Нос?.png", "НОС (2).png", "Ёж.png", "ёж.png", "CON.png", "con.PNG", "a/b.png", "a\\b.png"});
        CHECK(unique.size() == 11);
        CHECK(unique[0] == "Нос.png"); CHECK(unique[1] == "нос (2).PNG");
        CHECK(unique[2] == "Нос_.png"); CHECK(unique[3] == "Нос_ (2).png");
        CHECK(unique[4] == "НОС (2) (2).png"); CHECK(unique[6] == "ёж (2).png");
        for (std::size_t i = 0; i < unique.size(); ++i)
        {
            CHECK(exporttarget::ValidUtf8(unique[i]));
            CHECK(unique[i].find('/') == std::string::npos); CHECK(unique[i].find('\\') == std::string::npos);
            for (std::size_t j = 0; j < i; ++j) CHECK(!exporttarget::SameFileName(unique[i], unique[j]));
            std::ofstream(fixture.dir / fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(unique[i].data()), unique[i].size()))) << i;
        }
        for (std::size_t i = 0; i < unique.size(); ++i)
        {
            std::ifstream file(fixture.dir / fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(unique[i].data()), unique[i].size())));
            std::size_t value = unique.size(); file >> value; CHECK(value == i);
        }
        auto a = exporttarget::Reserve(fixture.dir, "Experiment"); CHECK(a);
        std::ofstream(a.dir / "old.csv") << "unchanged";
        auto b = exporttarget::Reserve(fixture.dir, "Experiment"); CHECK(b); CHECK(b.dir != a.dir);
        CHECK(b.dir.filename() == "Experiment (2)");
        std::ifstream old(a.dir / "old.csv"); std::string contents; old >> contents; CHECK(contents == "unchanged"); old.close();
        std::ofstream(fixture.dir / "Collision") << "existing file";
        auto collision = exporttarget::Reserve(fixture.dir, "Collision"); CHECK(collision); CHECK(collision.dir.filename() == "Collision (2)");
        auto ru = exporttarget::Reserve(fixture.dir, "Опыт кренования №1"); CHECK(ru); CHECK(ru.dir.filename() == fs::path(u8"Опыт кренования №1"));
        CHECK(fs::path(u8"Опыт кренования №1").wstring() == L"\u041e\u043f\u044b\u0442 \u043a\u0440\u0435\u043d\u043e\u0432\u0430\u043d\u0438\u044f \u21161");
        CHECK(fs::path(u8"COM¹").wstring() == L"COM\u00b9");
        CHECK(exporttarget::SafeBasename("COM¹") == "_COM¹");
        for (const auto* name : {"CON", "nul.txt", "COM1", "LPT9.csv", "COM¹", "..", "a/b\\c:*?\"<>|. "})
        { auto r = exporttarget::Reserve(fixture.dir, name); CHECK(r); CHECK(r.dir.parent_path() == fixture.dir); }
        CHECK(!exporttarget::Reserve(fixture.dir, std::string("bad\xc0\xaf", 5)));
        CHECK(!exporttarget::Reserve(fixture.dir / "missing", "test"));
        CHECK(!exporttarget::Reserve(fixture.dir / "Collision", "test"));
        CHECK(!exporttarget::Reserve({}, "test"));
        auto longName = exporttarget::Reserve(fixture.dir, std::string(400, 'x')); CHECK(longName);
        std::vector<exporttarget::Result> concurrent(24); std::vector<std::thread> threads;
        for (std::size_t i = 0; i < concurrent.size(); ++i)
            threads.emplace_back([&, i] { concurrent[i] = exporttarget::Reserve(fixture.dir, "Concurrent"); });
        for (auto& t : threads) t.join();
        for (std::size_t i = 0; i < concurrent.size(); ++i)
        { CHECK(concurrent[i]); for (std::size_t j = 0; j < i; ++j) CHECK(concurrent[i].dir != concurrent[j].dir); }
        // A partial write is kept in its own new directory; old report is untouched.
        std::ofstream(b.dir / "partial.csv") << "partial";
        std::error_code ec; CHECK(!fs::remove(b.dir, ec)); CHECK(ec); CHECK(fs::exists(b.dir / "partial.csv"));
#ifdef _WIN32
        auto denied = exporttarget::Reserve(fixture.dir, "Denied"); CHECK(denied);
        std::ofstream(denied.dir / "kept.csv") << "kept";
        DWORD bytes = 0; GetFileSecurityW(denied.dir.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &bytes);
        std::vector<unsigned char> original(bytes);
        CHECK(GetFileSecurityW(denied.dir.c_str(), DACL_SECURITY_INFORMATION, reinterpret_cast<PSECURITY_DESCRIPTOR>(original.data()), bytes, &bytes));
        PSECURITY_DESCRIPTOR restrictive = nullptr;
        CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(D;;0x6;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1, &restrictive, nullptr));
        const bool applied = SetFileSecurityW(denied.dir.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, restrictive) != 0;
        LocalFree(restrictive); CHECK(applied);
        auto refused = exporttarget::Reserve(denied.dir, "MustNotExist");
        std::ofstream failedWrite(denied.dir / "new.csv");
        const bool writeRefused = !failedWrite; failedWrite.close();
        const bool restored = SetFileSecurityW(denied.dir.c_str(), DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, reinterpret_cast<PSECURITY_DESCRIPTOR>(original.data())) != 0;
        CHECK(restored); CHECK(!refused); CHECK(!refused.error.empty()); CHECK(writeRefused); CHECK(!fs::exists(denied.dir / "new.csv"));
        std::ifstream kept(denied.dir / "kept.csv"); kept >> contents; CHECK(contents == "kept"); kept.close();
        std::ifstream unchanged(a.dir / "old.csv"); unchanged >> contents; CHECK(contents == "unchanged"); unchanged.close();
#else
        fs::permissions(a.dir, fs::perms::owner_read | fs::perms::owner_exec);
        auto refused = exporttarget::Reserve(a.dir, "MustNotExist");
        fs::permissions(a.dir, fs::perms::owner_all);
        CHECK(!refused); // Run as an ordinary user, not root.
#endif
        fs::remove_all(fixture.dir); // Only this test's atomically reserved fixture.
        std::cout << "export_target_test: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << "\nFixture retained: " << fixture.dir << '\n'; return 1; }
}
