#pragma once
// (Взят из ShagomerPCModule/tests/TestUtil.hpp; переменная данных — KRENOMER_TEST_DATA_DIR.)
// Мини-фреймворк тестов (без зависимостей): проверки работают и в Release (assert там выключен),
// TestSummary() печатает итог и даёт код возврата (0 — все проверки прошли).
//
//   int main(int argc, char** argv)
//   {
//       testutil::Init(argc, argv);         // UTF-8 в консоли Windows, --verbose
//       SECTION("допуски шага");
//       CHECK(a == b);
//       CHECK_MSG(n == 3, "лопастей %d", n);  // printf-сообщение печатается при ошибке
//       CHECK_NEAR(pitch, 900.0, 0.5, "Hв*"); // печатает факт, ожидание и разницу
//       CHECK_RANGE(sigma, 3.5, 6.5, "σ_pool");
//       return TestSummary();
//   }
//
// --verbose: CHECK_NEAR / CHECK_RANGE печатают и прошедшие проверки (фактические значения).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace testutil
{

inline int g_checks = 0;
inline int g_failures = 0;
inline bool g_verbose = false;
inline std::string g_section;
inline int g_sectionFailures = 0; // ошибок на начало текущего раздела

inline void Init(int argc = 0, char** argv = nullptr)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // русский текст в консоли
#endif
    for (int i = 1; i < argc; i++)
    {
        if (argv && (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0))
            g_verbose = true;
    }
}

// Итог прошлого раздела: «ok», если в нём не было ошибок.
inline void EndSection()
{
    if (!g_section.empty() && g_failures == g_sectionFailures)
        std::printf("    ok\n");
    g_section.clear();
}

inline void BeginSection(const char* name)
{
    EndSection();
    g_section = name;
    g_sectionFailures = g_failures;
    std::printf("[%s]\n", name);
}

inline void Fail(const char* file, int line, const char* expr)
{
    g_failures++;
    std::printf("  FAIL %s:%d: %s", file, line, expr);
}

inline bool Near(double a, double b, double tol)
{
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) && std::isnan(b); // NAN ожидался — NAN получен
    return std::abs(a - b) <= tol;
}

inline void CheckNear(const char* file, int line, double a, double b, double tol, const char* label)
{
    g_checks++;
    if (!Near(a, b, tol))
    {
        Fail(file, line, label);
        std::printf(" = %.6g, ожидалось %.6g ± %.3g (разница %.3g)\n", a, b, tol, a - b);
    }
    else if (g_verbose)
    {
        std::printf("  ok %s = %.6g (ожид. %.6g ± %.3g)\n", label, a, b, tol);
    }
}

inline void CheckRange(const char* file, int line, double v, double lo, double hi, const char* label)
{
    g_checks++;
    if (!(v >= lo && v <= hi))
    {
        Fail(file, line, label);
        std::printf(" = %.6g, ожидалось %.6g…%.6g\n", v, lo, hi);
    }
    else if (g_verbose)
    {
        std::printf("  ok %s = %.6g (%.6g…%.6g)\n", label, v, lo, hi);
    }
}

// Печатает итог «N проверок, M ошибок — OK/FAIL» и возвращает код выхода.
inline int Summary()
{
    EndSection();
    std::printf("\n%d проверок, %d ошибок — %s\n", g_checks, g_failures, g_failures == 0 ? "OK" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}

// Папка tests/data: KRENOMER_TEST_DATA_DIR (CMake / buildtest.cmd), переменная окружения
// KRENOMER_TEST_DATA_DIR, иначе tests/data или ../tests/data от текущей папки.
inline std::filesystem::path TestDataDir()
{
    std::error_code ec;
#ifdef KRENOMER_TEST_DATA_DIR
    {
        const std::filesystem::path p{KRENOMER_TEST_DATA_DIR};
        if (std::filesystem::is_directory(p, ec))
            return p;
    }
#endif
#ifdef _MSC_VER
#pragma warning(suppress : 4996) // getenv: только чтение, потокобезопасность не нужна
#endif
    if (const char* env = std::getenv("KRENOMER_TEST_DATA_DIR"))
    {
        const std::filesystem::path p{env};
        if (std::filesystem::is_directory(p, ec))
            return p;
    }
    for (const char* rel : {"tests/data", "../tests/data", "../../tests/data"})
    {
        const std::filesystem::path p{rel};
        if (std::filesystem::is_directory(p, ec))
            return p;
    }
    return {};
}

// Полный путь к файлу данных (строкой для fopen/ifstream); "" — не найден.
inline std::string TestDataPath(const char* name)
{
    const auto dir = TestDataDir();
    if (dir.empty())
        return {};
    const auto p = dir / name;
    std::error_code ec;
    return std::filesystem::exists(p, ec) ? p.string() : std::string{};
}

} // namespace testutil

#define CHECK(cond)                                                                                          \
    do                                                                                                       \
    {                                                                                                        \
        testutil::g_checks++;                                                                                \
        if (!(cond))                                                                                         \
        {                                                                                                    \
            testutil::Fail(__FILE__, __LINE__, #cond);                                                       \
            std::printf("\n");                                                                               \
        }                                                                                                    \
    } while (0)

#define CHECK_MSG(cond, ...)                                                                                 \
    do                                                                                                       \
    {                                                                                                        \
        testutil::g_checks++;                                                                                \
        if (!(cond))                                                                                         \
        {                                                                                                    \
            testutil::Fail(__FILE__, __LINE__, #cond);                                                       \
            std::printf(" -- ");                                                                             \
            std::printf(__VA_ARGS__);                                                                        \
            std::printf("\n");                                                                               \
        }                                                                                                    \
    } while (0)

// |a − b| <= tol (NAN == NAN); печатает факт, ожидание, допуск и разницу.
#define CHECK_NEAR(a, b, tol, label)                                                                         \
    testutil::CheckNear(__FILE__, __LINE__, static_cast<double>(a), static_cast<double>(b),                   \
                        static_cast<double>(tol), label)

// lo <= v <= hi (NAN — ошибка).
#define CHECK_RANGE(v, lo, hi, label)                                                                        \
    testutil::CheckRange(__FILE__, __LINE__, static_cast<double>(v), static_cast<double>(lo),                 \
                         static_cast<double>(hi), label)

// Новый раздел вывода: SECTION("название"); — печатает «[название]», в конце раздела — «ok».
#define SECTION(name) testutil::BeginSection(name)

inline int TestSummary()
{
    return testutil::Summary();
}
