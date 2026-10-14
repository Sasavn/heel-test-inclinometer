// «Обработка» (опыт кренования): чтение файлов замеров всех версий (1.4+ «;» и запятая — настоящие файлы прибора в
// tests/data, до 1.3 «,» с Ms и без, тестовые файлы старого анализатора), группировка по номеру замера и постам
// (и записи на ПК «…_PC_Dk.CSV»), среднее и СКО за последние N с, h = P·l / (D·tg|θ|) против посчитанного вручную,
// порог, МНК (h = 0,8 м точно, в том числе при начальном крене), отчёт Excel (листы, строки, ZIP) и сжатие deflate
// (распаковка тестовым inflate).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "TestUtil.hpp"
#include "core/CsvLog.hpp"
#include "core/Deflate.hpp"
#include "core/Expr.hpp"
#include "core/Filters.hpp"
#include "core/UserMethod.hpp"
#include "core/Heel.hpp"
#include "core/HeelData.hpp"
#include "core/HeelReport.hpp"
#include "core/HeelRs.hpp"
#include "core/RsCheck.hpp"
#include "core/PcSettings.hpp"
#include "core/Plateaus.hpp"
#include "core/Png.hpp"
#include "core/TextUtil.hpp"
#include "core/Xlsx.hpp"

namespace fs = std::filesystem;

namespace
{

const double kDeg = heel::kPi / 180.0;
const double kNaNt = std::numeric_limits<double>::quiet_NaN();

std::string Fmt3(double v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

void WriteText(const fs::path& p, const std::string& s)
{
    std::ofstream(p, std::ios::binary | std::ios::trunc).write(s.data(), static_cast<std::streamsize>(s.size()));
}

std::string ReadBytes(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Распаковка deflate — только блоки с фиксированными кодами (их и пишет Deflate.hpp). Пусто — ошибка потока.
bool Inflate(const std::vector<unsigned char>& in, std::vector<unsigned char>& out)
{
    std::size_t pos = 0;
    int bit = 0;
    auto get = [&](int n) -> int {
        int v = 0;
        for (int i = 0; i < n; i++)
        {
            if (pos >= in.size())
                return -1;
            v |= ((in[pos] >> bit) & 1) << i;
            if (++bit == 8)
            {
                bit = 0;
                pos++;
            }
        }
        return v;
    };
    auto huff = [&](int n, int code) { // код Хаффмана: старшим битом вперёд
        for (int i = 0; i < n; i++)
        {
            const int b = get(1);
            if (b < 0)
                return -1;
            code = (code << 1) | b;
        }
        return code;
    };
    static const int lenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int lenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int distBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int distExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;)
    {
        const int final = get(1), type = get(2);
        if (type != 1)
            return false;
        for (;;)
        {
            int sym = -1;
            int c = huff(7, 0);
            if (c < 0)
                return false;
            if (c <= 0x17)
                sym = 256 + c;
            else
            {
                c = huff(1, c);
                if (c >= 0x30 && c <= 0xBF)
                    sym = c - 0x30;
                else if (c >= 0xC0 && c <= 0xC7)
                    sym = 280 + c - 0xC0;
                else
                {
                    c = huff(1, c);
                    if (c < 0x190 || c > 0x1FF)
                        return false;
                    sym = 144 + c - 0x190;
                }
            }
            if (sym < 256)
                out.push_back(static_cast<unsigned char>(sym));
            else if (sym == 256)
                break;
            else
            {
                const int li = sym - 257;
                if (li < 0 || li > 28)
                    return false;
                const int len = lenBase[li] + get(lenExtra[li]);
                const int di = huff(5, 0);
                if (di < 0 || di > 29)
                    return false;
                const int dist = distBase[di] + get(distExtra[di]);
                if (dist > static_cast<int>(out.size()))
                    return false;
                const std::size_t from = out.size() - static_cast<std::size_t>(dist);
                for (int k = 0; k < len; k++)
                    out.push_back(out[from + static_cast<std::size_t>(k)]);
            }
        }
        if (final)
            return true;
    }
}

// Строки CSV формата 1.4: угол по X и Y, Ms от начала записи.
std::string Fw14(const std::vector<std::pair<double, double>>& xy, int stepMs = 100)
{
    std::string s = "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";
    for (std::size_t i = 0; i < xy.size(); i++)
    {
        auto c = [](double v, int dec) {
            char b[32];
            std::snprintf(b, sizeof(b), "%.*f", dec, v);
            for (char* p = b; *p; p++)
                if (*p == '.')
                    *p = ',';
            return std::string(b);
        };
        const int ms = static_cast<int>(i) * stepMs;
        char t[32];
        std::snprintf(t, sizeof(t), "08.10.2026;10:%02d:%02d;", (ms / 60000) % 60, (ms / 1000) % 60);
        s += t + c(xy[i].first, 2) + ";" + c(xy[i].second, 2) + ";0,000;0,000;" + c(xy[i].first, 3) + ";" +
             c(xy[i].second, 3) + ";12,4;" + std::to_string(ms) + "\n";
    }
    return s;
}

// Установившийся крен theta (по Y) после переходного процесса: 60 с, 10 Гц.
std::string Settling(double from, double theta)
{
    std::vector<std::pair<double, double>> xy;
    for (int i = 0; i < 600; i++)
    {
        const double t = i / 10.0;
        const double y = t < 20 ? from + (theta - from) * t / 20.0 : theta;
        xy.push_back({0.01, y});
    }
    return Fw14(xy);
}

void TestFormats()
{
    SECTION("файлы 1.4+: настоящие файлы прибора (M019, Д2 и Д3)");
    {
        csvlog::Log log;
        CHECK(csvlog::ReadFile(testutil::TestDataDir() / "2026-10-08_M019_D2.CSV", log));
        CHECK(log.error.empty());
        CHECK(log.format == csvlog::Format::Fw14);
        CHECK(log.time == csvlog::TimeSource::Ms);
        CHECK(log.hasRaw && log.hasOffset && log.hasCalc && log.hasBat && log.hasMs && log.hasDate && log.hasTime);
        CHECK_MSG(log.rows.size() == 206, "строк %zu", log.rows.size());
        CHECK(log.skipped == 0);
        if (!log.rows.empty())
        {
            const auto& r = log.rows.front();
            CHECK_NEAR(r.t, 0.074, 1e-12, "t первой строки = Ms/1000");
            CHECK_NEAR(r.rawX, -0.46, 1e-12, "RawX");
            CHECK_NEAR(r.rawY, -0.17, 1e-12, "RawY");
            CHECK_NEAR(r.offX, 0.0, 0, "OffsetX");
            CHECK(!std::signbit(r.offX)); // «0,000»
            CHECK_NEAR(r.calcX, -0.46, 1e-12, "CalcX");
            CHECK_NEAR(r.calcY, -0.17, 1e-12, "CalcY");
            CHECK_NEAR(r.batV, 4.0, 1e-12, "BatV");
            CHECK(r.day == csvlog::DaysFromCivil(2026, 10, 8));
            CHECK(r.sec == 17 * 3600 + 19 * 60 + 28);
            CHECK(csvlog::DateText(r.day) == "08.10.2026");
            CHECK(csvlog::ClockText(r.sec) == "17:19:28");
            CHECK_NEAR(log.rows.back().t, 20.555, 1e-12, "t последней строки");
        }
        // Среднее за последние 10 с (посчитано отдельно по файлу): X −0,46 ровно, Y −0,169801980 ± 0,002441392
        const auto sx = heel::TailStats(log, 0, 10.0);
        const auto sy = heel::TailStats(log, 1, 10.0);
        CHECK(sx.n == 101 && sy.n == 101);
        CHECK_NEAR(sx.mean, -0.46, 1e-12, "D2 X за 10 с");
        CHECK_NEAR(sx.sd, 0.0, 1e-12, "D2 СКО X");
        CHECK_NEAR(sy.mean, -0.169801980, 1e-9, "D2 Y за 10 с");
        CHECK_NEAR(sy.sd, 0.002441392, 1e-9, "D2 СКО Y");
        CHECK(!sx.whole);
        CHECK_NEAR(sx.from, 10.555, 1e-9, "начало окна");
        const auto all = heel::TailStats(log, 0, 30.0);
        CHECK(all.whole && all.n == 206);

        csvlog::Log d3;
        CHECK(csvlog::ReadFile(testutil::TestDataDir() / "2026-10-08_M019_D3.CSV", d3));
        CHECK(d3.rows.size() == 206);
        const auto s3 = heel::TailStats(d3, 0, 10.0);
        CHECK_NEAR(s3.mean, 0.355247525, 1e-9, "D3 X за 10 с");
        CHECK_NEAR(s3.sd, 0.005018777, 1e-9, "D3 СКО X");
        CHECK_NEAR(heel::TailStats(d3, 1, 10.0).mean, -48.409207921, 1e-9, "D3 Y за 10 с");
    }

    SECTION("1.4+: десятичная запятая, «-0,000», CRLF, BOM, оборванная строка");
    {
        const std::string text = "\xEF\xBB\xBF"
                                 "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\r\n"
                                 "08.10.2026;23:59:59;-0,46;1,25;-0,458;0,000;-0,002;1,250;11,8;0\r\n"
                                 "08.10.2026;23:59:59;0,00;-0,01;0,000;0,000;-0,000;-0,010;11,8;66\r\n"
                                 "\r\n"
                                 "09.10.2026;00:00:00;0,01;-0,01;0,000;0,000;0,010;-0,010;11,8;1005\r\n"
                                 "09.10.2026;00:00:01;0,01;-0,";
        csvlog::Log log;
        CHECK(csvlog::Parse(text, log));
        CHECK(log.rows.size() == 3);
        CHECK(log.skipped == 1);
        if (log.rows.size() == 3)
        {
            CHECK_NEAR(log.rows[0].offX, -0.458, 1e-12, "OffsetX с запятой");
            CHECK_NEAR(log.rows[0].calcX, -0.002, 1e-12, "CalcX");
            CHECK(log.rows[1].calcX == 0.0 && !std::signbit(log.rows[1].calcX)); // «-0,000» -> +0
            CHECK(log.rows[1].rawX == 0.0 && !std::signbit(log.rows[1].rawX));
            CHECK_NEAR(log.rows[1].t, 0.066, 1e-12, "t по Ms");
            CHECK(log.rows[2].day == log.rows[0].day + 1 && log.rows[2].sec == 0);
            CHECK_NEAR(log.rows[2].t, 1.005, 1e-12, "t через полночь (Ms)");
        }
    }

    SECTION("до 1.3: «,», точка, Time «ДД.ММ.ГГ ЧЧ:ММ:СС», Ms");
    {
        const std::string text = "Time,RawX,RawY,OffsetX,OffsetY,CalcX,CalcY,BatV,Ms\n"
                                 "08.10.26 17:19:28,1.25,-0.50,0.100,0.000,1.150,-0.500,12.1,0\n"
                                 "08.10.26 17:19:28,1.27,-0.52,0.100,0.000,1.170,-0.520,12.1,40\n"
                                 "08.10.26 17:19:29,1.30,-0.51,0.100,0.000,1.200,-0.510,12.0,1040\n";
        csvlog::Log log;
        CHECK(csvlog::Parse(text, log));
        CHECK(log.format == csvlog::Format::Fw13);
        CHECK(log.time == csvlog::TimeSource::Ms);
        CHECK(log.rows.size() == 3);
        if (log.rows.size() == 3)
        {
            CHECK(log.rows[0].day == csvlog::DaysFromCivil(2026, 10, 8));
            CHECK(log.rows[0].sec == 17 * 3600 + 19 * 60 + 28);
            CHECK_NEAR(log.rows[1].calcX, 1.17, 1e-12, "CalcX");
            CHECK_NEAR(log.rows[2].t, 1.04, 1e-12, "t по Ms");
            CHECK_NEAR(log.rows[2].batV, 12.0, 1e-12, "BatV");
        }
    }

    SECTION("до 1.3 без Ms: время по Time, строки секунды — равномерно, пропуск секунды");
    {
        const std::string text = "Time,RawX,RawY,OffsetX,OffsetY,CalcX,CalcY,BatV\n"
                                 "08.10.26 17:19:28,1,1,0,0,1,1,12\n"
                                 "08.10.26 17:19:28,1,1,0,0,1,1,12\n"
                                 "08.10.26 17:19:28,1,1,0,0,1,1,12\n"
                                 "08.10.26 17:19:29,1,1,0,0,1,1,12\n"
                                 "08.10.26 17:19:29,1,1,0,0,1,1,12\n"
                                 "08.10.26 17:19:31,1,1,0,0,1,1,12\n";
        csvlog::Log log;
        CHECK(csvlog::Parse(text, log));
        CHECK(log.time == csvlog::TimeSource::Time && !log.hasMs);
        const double want[] = {0, 1.0 / 3, 2.0 / 3, 1.0, 1.5, 3.0};
        CHECK(log.rows.size() == 6);
        for (std::size_t i = 0; i < log.rows.size() && i < 6; i++)
            CHECK_NEAR(log.rows[i].t, want[i], 1e-12, "t по Time");
    }

    SECTION("файлы старого анализатора: Time,CalcX,CalcY,BatV (ЧЧ:ММ:СС, через полночь)");
    {
        const std::string text = "Time,CalcX,CalcY,BatV\n"
                                 "23:59:59,0.10,2.000,12.5\n"
                                 "23:59:59,0.11,2.010,12.5\n"
                                 "00:00:00,0.12,2.020,12.5\n";
        csvlog::Log log;
        CHECK(csvlog::Parse(text, log));
        CHECK(log.format == csvlog::Format::Analyzer);
        CHECK(!log.hasRaw && !log.hasMs && !log.hasDate);
        CHECK(log.rows.size() == 3);
        if (log.rows.size() == 3)
        {
            CHECK(std::isnan(log.rows[0].rawX) && std::isnan(log.rows[0].offY));
            CHECK(log.rows[0].day == -1);
            CHECK_NEAR(log.rows[1].t, 0.5, 1e-12, "t");
            CHECK_NEAR(log.rows[2].t, 1.0, 1e-12, "t через полночь");
            CHECK_NEAR(log.rows[2].calcY, 2.02, 1e-12, "CalcY");
        }
    }

    SECTION("без столбцов Calc: Raw − Offset; без времени — 10 строк в секунду; не файл замера");
    {
        csvlog::Log log;
        CHECK(csvlog::Parse("RawX,RawY,OffsetX,OffsetY\n1.5,-2.0,0.5,-0.5\n1.5,-2.0,0.5,-0.5\n", log));
        CHECK(log.rows.size() == 2);
        if (log.rows.size() == 2)
        {
            CHECK_NEAR(log.rows[0].calcX, 1.0, 1e-12, "CalcX = RawX − OffsetX");
            CHECK_NEAR(log.rows[0].calcY, -1.5, 1e-12, "CalcY = RawY − OffsetY");
            CHECK(log.time == csvlog::TimeSource::Rows);
            CHECK_NEAR(log.rows[1].t, 0.1, 1e-12, "t по номеру строки");
        }
        csvlog::Log bad;
        CHECK(!csvlog::Parse("foo,bar\n1,2\n", bad));
        CHECK(!bad.error.empty());
        CHECK(!csvlog::Parse("", bad));
        CHECK(!csvlog::Parse("Time,CalcX,CalcY,BatV\n", bad));
    }

    SECTION("окно: среднее и СКО за последние N с");
    {
        csvlog::Log log;
        for (int i = 0; i < 600; i++)
        {
            csvlog::Row r;
            r.t = i / 10.0;
            r.calcY = i < 300 ? 1.0 : 2.0 + (i % 2 == 0 ? 0.1 : -0.1);
            r.calcX = std::nan("");
            log.rows.push_back(r);
        }
        const auto s = heel::TailStats(log, 1, 20.05); // строки 399…599
        CHECK(s.n == 201);
        CHECK_NEAR(s.mean, 1.999502487562, 1e-11, "среднее (101 × 1,9 и 100 × 2,1)");
        CHECK_NEAR(s.sd, 0.100248447588, 1e-11, "СКО (выборочное)");
        const auto x = heel::TailStats(log, 0, 20.0);
        CHECK(x.n == 0 && std::isnan(x.mean)); // по X данных нет
        CHECK(heel::TailStats(csvlog::Log{}, 1, 30).n == 0);
    }
}

void TestMath()
{
    SECTION("h = P·l / (D·tg|θ|): вручную, знак, порог");
    // 15·4,5 / (1500·tg 2,451°) = 1,051300348 м; tg 0,46° -> 5,604901481 м
    CHECK_NEAR(heel::Height(15, 4.5, 1500, 2.451), 1.051300348, 1e-9, "h(2,451°)");
    CHECK_NEAR(heel::Height(15, 4.5, 1500, -2.451), 1.051300348, 1e-9, "h(−2,451°)");
    CHECK_NEAR(heel::Height(15, 4.5, 1500, -0.46), 5.604901481, 1e-9, "h(−0,46°)");
    CHECK(std::isnan(heel::Height(15, 4.5, 1500, 0.0)));
    CHECK(std::isnan(heel::Height(15, 4.5, 0.0, 1.0)));

    std::vector<heel::Point> pts = {{1, 0, 2.451, 4.5}, {1, 1, -2.451, 4.5}, {2, 0, 0.05, 4.5}, {2, 1, 0.1, 4.5},
                                    {3, 0, std::nan(""), 4.5}, {3, 1, 1.0, 0.0}};
    const auto r = heel::Solve(pts, 1500, 15, 0.1);
    CHECK(r.points.size() == 6);
    CHECK(r.n == 2);
    CHECK(r.points[0].counted && r.points[1].counted);
    CHECK(!r.points[2].counted && r.points[2].note == "крен не больше порога" && std::isnan(r.points[2].h));
    CHECK(!r.points[3].counted); // ровно порог — не учитывается (как |θ| > 0,1 в старой программе)
    CHECK(!r.points[4].counted && r.points[4].note == "нет данных");
    CHECK(!r.points[5].counted && r.points[5].note == "плечо l не задано");
    CHECK_NEAR(r.h, 1.051300348, 1e-9, "среднее h");
    CHECK_NEAR(r.sd, 0.0, 1e-12, "СКО h");
    CHECK_NEAR(r.points[1].x, -15 * 4.5 / 1500.0, 1e-15, "x = −P·l/D (крен влево)");
    const auto none = heel::Solve({{1, 0, 0.01, 4.5}}, 1500, 15, 0.1);
    CHECK(none.n == 0 && std::isnan(none.h) && std::isnan(none.ls.h));

    SECTION("МНК: h = 0,8 м восстанавливается точно (и при начальном крене)");
    {
        const double D = 1500, P = 15, h = 0.8;
        for (const double b : {0.0, 0.002})
        {
            std::vector<heel::Point> ps;
            int num = 1;
            for (const double sign : {1.0, -1.0})
                for (const double l : {1.0, 2.0, 3.0, 4.0})
                {
                    const double x = sign * P * l / D;
                    ps.push_back({num++, 0, std::atan(x / h + b) / kDeg, l});
                }
            const auto res = heel::Solve(ps, D, P, 0.1);
            CHECK(res.n == 8);
            CHECK(res.ls.n == 8 && !res.ls.throughOrigin);
            CHECK_NEAR(res.ls.h, h, 1e-9, b == 0 ? "h по МНК" : "h по МНК при начальном крене");
            CHECK_NEAR(res.ls.b, b, 1e-12, "свободный член b");
            CHECK_NEAR(res.ls.r2, 1.0, 1e-12, "R²");
            if (b == 0)
                CHECK_NEAR(res.h, h, 1e-9, "среднее h без начального крена");
            else
                CHECK_MSG(std::fabs(res.h - h) > 0.002, "среднее h смещено начальным креном: %.4f", res.h);
        }
        // Одно плечо, один борт — прямая через ноль
        std::vector<heel::Point> ps;
        for (int i = 0; i < 3; i++)
            ps.push_back({i + 1, i % 2, std::atan(P * 4.5 / (D * h)) / kDeg, 4.5});
        const auto res = heel::Solve(ps, D, P, 0.1);
        CHECK(res.ls.throughOrigin && res.ls.b == 0.0);
        CHECK_NEAR(res.ls.h, h, 1e-9, "h через ноль");
        // Шум: прямая по 8 точкам с шумом ±0,01° — h близко к 0,8
        std::mt19937 rng(5);
        std::normal_distribution<double> noise(0.0, 0.01);
        ps.clear();
        for (const double sign : {1.0, -1.0})
            for (const double l : {1.0, 2.0, 3.0, 4.0})
                ps.push_back({1, 0, std::atan(sign * P * l / (D * h)) / kDeg + noise(rng), l});
        CHECK_RANGE(heel::Solve(ps, D, P, 0.1).ls.h, 0.78, 0.82, "h по МНК с шумом");
        const auto deg = heel::FitLine({}, {});
        CHECK(deg.n == 0 && std::isnan(deg.h));
    }
}

void TestGroupingAndReport()
{
    SECTION("папка: имена, замеры по номеру, посты по датчику (по умолчанию и смена)");
    const fs::path dir = fs::temp_directory_path() / L"krenomer_process_test_папка";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    // Замер 1: Д2 и Д3 (+1,0° / +1,02°); 2: только Д2 (−2,0°) и Д4; 3: старые имена (+0,05° — ниже порога, +3,0°)
    WriteText(dir / "2026-10-08_M001_D2.CSV", Settling(0.0, 1.0));
    WriteText(dir / "2026-10-08_M001_D3.CSV", Settling(0.0, 1.02));
    WriteText(dir / "2026-10-08_M002_D2.CSV", Settling(1.0, -2.0));
    WriteText(dir / "2026-10-08_M002_D4.CSV", Settling(0.0, 5.0));
    WriteText(dir / "M_003_1.CSV", Settling(-2.0, 0.05));
    WriteText(dir / "m_003_2.csv", Settling(-2.0, 3.0));
    WriteText(dir / "rs485_sweep.csv", "a,b\n1,2\n");
    WriteText(dir / "2026-10-08_M004_D2.CSV", "Date;Time;Foo\n1;2;3\n");
    WriteText(dir / "notes.txt", "-");
    WriteText(dir / L"Отчет_Кренование.xlsx", "PK"); // свой отчёт — не читается
    std::string err;
    const auto paths = heel::ListCsv(dir, &err);
    CHECK(err.empty());
    CHECK_MSG(paths.size() == 9, "CSV и TXT в папке: %zu", paths.size());
    std::atomic<int> done{0};
    const heel::Loaded loaded = heel::LoadAll(paths, text::PathToUtf8(dir), &done);
    CHECK(done.load() == 9);
    const auto& files = loaded.files;
    std::vector<std::string> notes;
    auto ms = heel::Group(files, {}, &notes);
    CHECK_MSG(ms.size() == 3, "замеров %zu", ms.size());
    CHECK_MSG(notes.size() == 4, "заметок %zu", notes.size()); // Д4 без поста; rs485, notes.txt, M004 — не понял формат
    bool understood = false;
    for (const auto& n : notes)
        understood |= n == "не понял формат: rs485_sweep.csv, первая строка: «a,b» — пропущен";
    CHECK(understood);
    auto fileName = [&](const heel::Measurement& m, int post) {
        return m.post[post].file >= 0 ? files[static_cast<std::size_t>(m.post[post].file)].name : std::string("-");
    };
    if (ms.size() == 3)
    {
        CHECK(ms[0].number == 1 && fileName(ms[0], 0) == "2026-10-08_M001_D2.CSV" &&
              fileName(ms[0], 1) == "2026-10-08_M001_D3.CSV");
        CHECK(ms[1].number == 2 && fileName(ms[1], 0) == "2026-10-08_M002_D2.CSV" && ms[1].post[1].file < 0);
        CHECK(ms[2].number == 3 && fileName(ms[2], 0) == "M_003_1.CSV" && fileName(ms[2], 1) == "m_003_2.csv");
    }
    CHECK(heel::SensorKey(1, true) == 101 && heel::DefaultPost(101) == 0 && heel::DefaultPost(102) == 1);
    CHECK(heel::DefaultPost(2) == 0 && heel::DefaultPost(3) == 1 && heel::DefaultPost(4) == -1);
    CHECK(heel::SensorName(3) == "Д3");
    // Смена постов: Д2 — корма, Д3 — нос, Д4 — нос
    notes.clear();
    auto sw = heel::Group(files, {{2, 1}, {3, 0}, {4, 0}}, &notes);
    if (sw.size() == 3)
    {
        CHECK(fileName(sw[0], 0) == "2026-10-08_M001_D3.CSV" && fileName(sw[0], 1) == "2026-10-08_M001_D2.CSV");
        CHECK(fileName(sw[1], 0) == "2026-10-08_M002_D4.CSV" && fileName(sw[1], 1) == "2026-10-08_M002_D2.CSV");
    }
    CHECK(notes.size() == 3); // rs485, notes.txt, M004 — не понял формат
    // Два датчика на один пост: второй не учитывается, с заметкой
    notes.clear();
    auto dup = heel::Group(files, {{3, 0}}, &notes);
    CHECK(dup.size() == 3 && dup[0].post[1].file < 0 && fileName(dup[0], 0) == "2026-10-08_M001_D2.CSV");
    bool dupNote = false;
    for (const auto& n : notes)
        dupNote |= n.find("уже есть") != std::string::npos;
    CHECK(dupNote);

    SECTION("расчёт по папке: окно, h по замерам и постам, итог, МНК, «не в расчёте»");
    heel::Settings s;
    s.axis = 1;
    s.windowS = 30.05; // t 29,9…59,9 с — без сравнения границы на равенство
    ms[0].arm = 2.0; // свои плечи; у замера 2 — L (4,5 м)
    ms[2].arm = 6.0;
    auto sum = heel::Compute(s, files, ms);
    CHECK_NEAR(ms[0].post[0].st.mean, 1.0, 1e-9, "M1 нос: крен");
    CHECK_NEAR(ms[0].post[1].st.mean, 1.02, 1e-9, "M1 корма: крен");
    CHECK(ms[0].post[0].st.n == 301); // t 29,9…59,9 с, 10 Гц (t по Ms — точно)
    CHECK_NEAR(ms[0].post[0].res.h, heel::Height(15, 2.0, 1500, 1.0), 1e-12, "M1 нос: h");
    CHECK_NEAR(ms[1].post[0].res.h, heel::Height(15, 4.5, 1500, 2.0), 1e-12, "M2 нос: h (плечо L)");
    CHECK(!ms[2].post[0].res.counted); // 0,05° — ниже порога
    CHECK_NEAR(ms[2].post[1].res.h, heel::Height(15, 6.0, 1500, 3.0), 1e-12, "M3 корма: h");
    CHECK_NEAR(ms[0].post[0].batV, 12.4, 1e-12, "АКБ — последнее значение");
    const double want = (heel::Height(15, 2.0, 1500, 1.0) + heel::Height(15, 2.0, 1500, 1.02) +
                         heel::Height(15, 4.5, 1500, 2.0) + heel::Height(15, 6.0, 1500, 3.0)) / 4.0;
    CHECK(sum.n == 4 && sum.measurements == 3);
    CHECK_NEAR(sum.h, want, 1e-12, "итоговое среднее h");
    CHECK(sum.ls.n == 4 && std::isfinite(sum.ls.h));
    ms[1].use = false;
    const auto sum2 = heel::Compute(s, files, ms);
    CHECK(sum2.n == 3 && sum2.measurements == 2);
    CHECK(!ms[1].post[0].res.counted && ms[1].post[0].res.note == "замер не в расчёте");
    ms[1].use = true;
    sum = heel::Compute(s, files, ms);
    // По X (0,01° во всех файлах) — всё ниже порога
    heel::Settings sx = s;
    sx.axis = 0;
    auto msx = ms;
    CHECK(heel::Compute(sx, files, msx).n == 0);

    SECTION("отчёт Excel: листы, строки, итог, сырые данные, ZIP");
    heel::ReportInfo info{text::PathToUtf8(dir), "Регистратор крена — программа для ПК (тест)", "2026-10-08 18:00:00"};
    std::string warn;
    const auto sheets = heel::BuildReport(s, files, ms, sum, info, &warn);
    CHECK(warn.empty());
    CHECK(sheets.size() == 3); // + «Сравнение постов»: постов два
    if (sheets.size() == 3)
    {
        const auto& cmp = sheets[2];
        CHECK(cmp.name == "Сравнение постов" && cmp.rows[0].size() == 4 && cmp.rows[0][1].text == "Крен Нос (°)");
        CHECK_NEAR(cmp.rows[1][3].num, ms[0].Spread(), 1e-12, "расхождение постов замера 1");
        CHECK_NEAR(ms[0].Spread(), 0.02, 1e-9, "Д2 1,00° и Д3 1,02°");
    }
    if (sheets.size() >= 2)
    {
        const auto& res = sheets[0];
        CHECK(res.name == "Результаты");
        CHECK(res.rows.size() > 6 && res.rows[0].size() == 10 && res.rows[0][7].text == "h (м)");
        CHECK(res.rows[1][0].text == "2026-10-08_M001_D2.CSV" && res.rows[1][1].text == "Нос");
        CHECK_NEAR(res.rows[1][3].num, 2.0, 0, "плечо в отчёте");
        CHECK_NEAR(res.rows[1][4].num, 1.0, 1e-9, "крен в отчёте");
        CHECK(res.rows[1][4].decimals == 3);
        // 5 файлов в расчёте (M001 ×2, M002 Д2, M003 ×2)
        CHECK(res.rows[6].empty());
        bool total = false, ls = false, inputs = false;
        for (const auto& row : res.rows)
        {
            if (!row.empty() && row[0].text == "ИТОГОВОЕ СРЕДНЕЕ ЗНАЧЕНИЕ:")
            {
                total = true;
                CHECK_NEAR(row[7].num, sum.h, 1e-12, "итог в отчёте");
            }
            if (!row.empty() && row[0].text == "По МНК:")
            {
                ls = true;
                CHECK_NEAR(row[7].num, sum.ls.h, 1e-12, "МНК в отчёте");
            }
            if (!row.empty() && row[0].text == "Водоизмещение D (т)")
                inputs = row.size() > 1 && row[1].num == 1500.0;
        }
        CHECK(total && ls && inputs);
        // Строка ниже порога — без h, с примечанием
        bool below = false;
        for (const auto& row : res.rows)
            if (!row.empty() && row[0].text == "M_003_1.CSV")
                below = row[7].kind == xlsx::Cell::Kind::Empty && row[9].text == "крен не больше порога";
        CHECK(below);
        const auto& raw = sheets[1];
        CHECK(raw.name == "Сырые_данные");
        CHECK_MSG(raw.rows.size() == 1 + 5 * 600, "строк сырых данных %zu", raw.rows.size());
        CHECK(raw.rows[1][1].text == "Нос" && raw.rows[1][3].text == "08.10.2026" && raw.rows[1][4].text == "10:00:00");
        CHECK(raw.rows[1][11].kind == xlsx::Cell::Kind::Number);

        // ZIP без сжатия — XML прямо в байтах; со сжатием — меньше, имена частей те же
        const std::string stored = xlsx::Build(sheets, 0, false);
        const std::string packed = xlsx::Build(sheets, 0, true);
        for (const char* part : {"[Content_Types].xml", "xl/workbook.xml", "xl/styles.xml", "xl/worksheets/sheet1.xml",
                                 "xl/worksheets/sheet2.xml"})
        {
            CHECK_MSG(stored.find(part) != std::string::npos, "нет части %s", part);
            CHECK_MSG(packed.find(part) != std::string::npos, "нет части %s (сжатый)", part);
        }
        CHECK(stored.find("Результаты") != std::string::npos && stored.find("Сырые_данные") != std::string::npos);
        CHECK(stored.find("ИТОГОВОЕ СРЕДНЕЕ ЗНАЧЕНИЕ:") != std::string::npos);
        CHECK_MSG(packed.size() * 4 < stored.size(), "сжатие: %zu из %zu байт", packed.size(), stored.size());
        CHECK(packed.compare(0, 4, "PK\x03\x04") == 0);
        // В файл (папка с кириллицей) — для проверки openpyxl снаружи
        const fs::path out = dir / L"Отчет_Кренование.xlsx";
        std::string werr;
        CHECK_MSG(xlsx::Write(out, sheets, &werr), "%s", werr.c_str());
        CHECK(fs::file_size(out, ec) > 1000);
        std::printf("    отчёт: %s (%llu байт, без сжатия %zu)\n", text::PathToUtf8(out).c_str(),
                    static_cast<unsigned long long>(fs::file_size(out, ec)), stored.size());
    }
}

void TestPcRuns()
{
    SECTION("записи на ПК «…_PC_Dk.CSV»: запись = замер, номера 1001… по времени, метка");
    const fs::path dir = fs::temp_directory_path() / L"krenomer_process_test_пк";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    WriteText(dir / text::PathFromUtf8("2026-10-08_17-25-00_опыт 3_PC_D2.CSV"), Settling(0.0, -2.0));
    WriteText(dir / "2026-10-08_17-19-28_PC_D2.CSV", Settling(0.0, 1.0));
    WriteText(dir / "2026-10-08_17-19-28_PC_D3.CSV", Settling(0.0, 1.01));
    WriteText(dir / "2026-10-08_M005_D2.CSV", Settling(0.0, 3.0));
    WriteText(dir / "2026-10-08_17-30-00_PC_DX.CSV", Settling(0.0, 3.0)); // не адрес — отдельная запись по имени
    const auto loaded = heel::LoadAll(heel::ListCsv(dir), text::PathToUtf8(dir));
    std::vector<std::string> notes;
    auto ms = heel::Group(loaded.files, {}, &notes);
    CHECK_MSG(ms.size() == 4, "замеров %zu", ms.size());
    CHECK(notes.empty());
    if (ms.size() == 4)
    {
        CHECK(ms[0].number == 5 && ms[0].label == "5");
        CHECK(ms[1].number == 1001 && ms[1].label == "17:19:28" && ms[1].post[0].file >= 0 && ms[1].post[1].file >= 0);
        CHECK(ms[2].number == 1002 && ms[2].label == "17:25:00 опыт 3" && ms[2].post[1].file < 0);
        CHECK(ms[3].number == 2001 && ms[3].label == "2026-10-08_17-30-00_PC_DX" && ms[3].post[0].file >= 0);
        heel::Settings s;
        s.axis = 1;
        const auto sum = heel::Compute(s, loaded.files, ms);
        CHECK(sum.n == 5);
        CHECK_NEAR(ms[2].post[0].st.mean, -2.0, 1e-9, "запись на ПК: крен");
        const auto sheets = heel::BuildReport(s, loaded.files, ms, sum, {});
        CHECK(sheets[0].rows[2][2].kind == xlsx::Cell::Kind::Text && sheets[0].rows[2][2].text == "17:19:28");
        CHECK(sheets[0].rows[1][2].kind == xlsx::Cell::Kind::Number && sheets[0].rows[1][2].num == 5);
    }
    CHECK(heel::PcRunLabel("2026-10-08_17-19-28") == "17:19:28");
    CHECK(heel::PcRunLabel("что-то") == "что-то");
}

// Файлы прежних прошивок и после Excel (tests/data/old, make_test_data.py): формат по содержимому, значения.
void TestOldFormats()
{
    SECTION("старые форматы: исходная прошивка, 1.0–1.3, без шапки, выдернутая карта, анализатор, Excel");
    const fs::path old = testutil::TestDataDir() / "old";
    struct Case
    {
        const wchar_t* file;
        csvlog::Format format;
        std::size_t rows;
        int axis;
        double mean;
        char sep;
    };
    const Case cases[] = {
        {L"M_001.CSV", csvlog::Format::FwOld, 300, 1, 2.4525, ','},
        {L"M_002_1.CSV", csvlog::Format::FwOld, 400, 1, -2.314325, ','},
        {L"M_002_2.CSV", csvlog::Format::FwOld, 400, 1, -2.2826175, ','},
        {L"2026-10-07_M004_D2.CSV", csvlog::Format::Fw13, 350, 0, 1.731874285714286, ','},
        {L"M_006_1.CSV", csvlog::Format::FwOld, 200, 1, 1.10511, ','},
        {L"M_007_1.CSV", csvlog::Format::FwOld, 150, 1, -0.8724133333333333, ','},
        {L"analyzer/M_001.CSV", csvlog::Format::Analyzer, 100, 0, 2.44833, ','},
        {L"excel/опыт 3 (Excel).csv", csvlog::Format::FwOld, 1500, 1, 3.217386, ';'},
        {L"excel/опыт 4.txt", csvlog::Format::FwOld, 180, 1, -1.6450444444444445, '\t'},
    };
    for (const Case& c : cases)
    {
        const std::string name = text::PathToUtf8(c.file);
        csvlog::Log log;
        CHECK_MSG(csvlog::ReadFile(old / c.file, log), "%s: %s", name.c_str(), log.error.c_str());
        CHECK_MSG(log.format == c.format, "%s: формат %s", name.c_str(), csvlog::FormatName(log.format));
        CHECK_MSG(log.rows.size() == c.rows, "%s: строк %zu", name.c_str(), log.rows.size());
        CHECK_MSG(log.sep == c.sep, "%s: разделитель", name.c_str());
        const auto st = heel::TailStats(log, c.axis, 1e9);
        CHECK_NEAR(st.mean, c.mean, 1e-9, name.c_str());
        CHECK_MSG(log.Duration() > 9.0, "%s: длительность %.2f с", name.c_str(), log.Duration());
    }
    {
        csvlog::Log log;
        csvlog::ReadFile(old / "M_002_1.CSV", log);
        CHECK(log.time == csvlog::TimeSource::Time && log.rows[0].day == csvlog::DaysFromCivil(2026, 6, 1));
        CHECK_NEAR(log.rows[0].offY, -0.212, 1e-12, "OffsetY старой прошивки");
        CHECK_NEAR(log.rows.back().t, 39.9, 1e-9, "t по Time (10 строк в секунде)");
        csvlog::ReadFile(old / "M_006_1.CSV", log);
        CHECK(log.headerless && log.hasRaw && log.hasOffset && !log.hasMs);
        csvlog::ReadFile(old / "M_007_1.CSV", log);
        CHECK(log.nulBytes && log.skipped == 1);
        csvlog::ReadFile(old / L"excel/опыт 3 (Excel).csv", log);
        CHECK(log.decimalComma && log.time == csvlog::TimeSource::Time);
        CHECK_NEAR(log.rows.back().t, 149.9, 1e-6, "Time без секунд: шаг по числу строк в минуте");
        CHECK_NEAR(log.rows[600].t, 60.0, 1e-6, "вторая минута");
        csvlog::ReadFile(old / L"excel/опыт 4.txt", log);
        CHECK(log.utf16 && log.decimalComma);
        CHECK(!csvlog::ReadFile(old / "readme.txt", log));
        CHECK(log.error == "не понял формат" && log.firstLine == "Здесь файлы старой прошивки.");
        CHECK(csvlog::Details(log).empty());
    }

    SECTION("старые форматы: папка — замеры по именам M_NNN.CSV, M_NNN_k.CSV, другие имена — отдельные записи");
    {
        std::string err;
        bool sub = true;
        const auto paths = heel::ListCsv(old, &err, &sub);
        CHECK(err.empty() && !sub && paths.size() == 7);
        const auto loaded = heel::LoadAll(paths, text::PathToUtf8(old));
        std::vector<std::string> notes;
        const auto ms = heel::Group(loaded.files, {}, &notes);
        CHECK_MSG(ms.size() == 5, "замеров %zu", ms.size());
        CHECK_MSG(notes.size() == 1 && notes[0].rfind("не понял формат: readme.txt", 0) == 0, "заметок %zu", notes.size());
        if (ms.size() == 5)
        {
            CHECK(ms[0].number == 1 && ms[0].post[0].file >= 0 && ms[0].post[1].file < 0); // M_001.CSV — датчик 1, нос
            CHECK(ms[1].number == 2 && ms[1].post[0].file >= 0 && ms[1].post[1].file >= 0);
            CHECK(ms[2].number == 4 && ms[3].number == 6 && ms[4].number == 7);
        }
        // Папка Excel: имена не по образцу — каждая запись своя, подпись — имя файла
        const auto ex = heel::LoadAll(heel::ListCsv(old / "excel"), "");
        notes.clear();
        const auto me = heel::Group(ex.files, {}, &notes);
        CHECK(me.size() == 2 && notes.empty());
        if (me.size() == 2)
            CHECK(me[0].number == 2001 && me[0].label == "опыт 3 (Excel)" && me[1].label == "опыт 4");
        // В самой папке файлов нет — берутся из подпапок
        const fs::path dir = fs::temp_directory_path() / L"krenomer_process_test_подпапки";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "карта", ec);
        fs::copy_file(old / "M_001.CSV", dir / "карта" / "M_001.CSV", ec);
        const auto inSub = heel::ListCsv(dir, &err, &sub);
        CHECK(sub && inSub.size() == 1);
    }
}

// Синтетическая непрерывная запись: n ступенек (уровни, по dur с), качка и шум, всплески на переносах.
void MakeSteps(const std::vector<double>& levels, double dur, double amp, double period, std::vector<double>& t,
               std::vector<double>& v, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 0.02);
    t.clear();
    v.clear();
    const double total = dur * static_cast<double>(levels.size());
    for (double x = 0.0; x < total; x += 0.1)
    {
        const std::size_t k = std::min(levels.size() - 1, static_cast<std::size_t>(x / dur));
        const double e = x - static_cast<double>(k) * dur; // с от переноса
        double y = levels[k] + amp * std::sin(2 * heel::kPi * x / period) + noise(rng);
        if (k > 0 && e < 2.0)
            y += 3.0 * std::sin(heel::kPi * e / 2.0); // всплеск 2 с
        t.push_back(x);
        v.push_back(y);
    }
}

void TestPlateaus()
{
    SECTION("положения груза: ступеньки с качкой и всплесками, слияние, короткие участки, Measure");
    {
        const std::vector<double> lv = {0.0, 2.0, 4.0, -1.0, -3.5};
        std::vector<double> t, v;
        MakeSteps(lv, 45.0, 0.6, 8.0, t, v, 3);
        const auto ps = plateau::Detect(t, v, plateau::Params{});
        CHECK_MSG(ps.size() == lv.size(), "положений %zu", ps.size());
        for (std::size_t i = 0; i < ps.size() && i < lv.size(); i++)
        {
            CHECK_NEAR(ps[i].mean, lv[i], 0.1, "уровень ступеньки");
            CHECK_MSG(ps[i].t0 >= 45.0 * static_cast<double>(i) + 2.0 && ps[i].t1 <= 45.0 * static_cast<double>(i + 1),
                      "участок %zu: %.1f…%.1f с — захватил перенос", i + 1, ps[i].t0, ps[i].t1);
            CHECK_MSG(ps[i].Duration() > 25.0, "участок %zu короткий: %.1f с", i + 1, ps[i].Duration());
            CHECK(ps[i].sd > 0.3 && ps[i].sd < 0.6); // качка входит в СКО
        }
        // Перепад меньше minStep — одно положение; качка с большим периодом и размахом — не делит ступеньку
        MakeSteps({1.0, 1.2, 5.0}, 40.0, 0.9, 14.0, t, v, 4);
        const auto merged = plateau::Detect(t, v, plateau::Params{});
        CHECK_MSG(merged.size() == 2, "положений %zu", merged.size());
        if (merged.size() == 2)
            CHECK_NEAR(merged[1].mean, 5.0, 0.15, "уровень после слияния");
        // Ступенька короче minLen — не положение
        MakeSteps({0.0, 3.0, 6.0}, 8.0, 0.1, 8.0, t, v, 5);
        CHECK(plateau::Detect(t, v, plateau::Params{}).empty());
        CHECK(plateau::Detect({}, {}, plateau::Params{}).empty());
        CHECK(plateau::Detect({0, 1}, {1, 1}, plateau::Params{}).empty());
        // Measure: среднее, СКО, NaN пропускаются
        const auto m = plateau::Measure({0, 1, 2, 3, 4}, {1, 2, std::nan(""), 4, 100}, 0.5, 3.5);
        CHECK(m.n == 2);
        CHECK_NEAR(m.mean, 3.0, 1e-12, "Measure: среднее");
        CHECK_NEAR(m.sd, std::sqrt(2.0), 1e-12, "Measure: СКО");
        const auto f = plateau::MedianFilter({0, 1, 2, 3, 4}, {0, 0, 9, 0, 0}, 2.0);
        CHECK(f.size() == 5 && f[2] == 0.0); // одиночный всплеск — мимо медианы
    }

    SECTION("непрерывная запись (синтетика как у руководителя): 10 положений, уровни ±0,1° от истины");
    {
        csvlog::Log log;
        const fs::path file = testutil::TestDataDir() / "continuous" / "2026-10-09_M020_D2.CSV";
        CHECK(csvlog::ReadFile(file, log));
        CHECK_MSG(log.rows.size() > 5000 && log.format == csvlog::Format::Fw14, "строк %zu", log.rows.size());
        CHECK_NEAR(log.rows[0].offX, 2.701, 1e-12, "OffsetX");
        CHECK_NEAR(log.rows[0].offY, -0.819, 1e-12, "OffsetY");
        CHECK_NEAR(log.rows[0].batV, 11.6, 1e-12, "BatV");
        // Истина
        struct Truth
        {
            double t0, t1, level;
        };
        std::vector<Truth> truth;
        {
            std::ifstream in(testutil::TestDataDir() / "continuous" / "truth.txt");
            std::string line;
            while (std::getline(in, line))
            {
                int n = 0;
                Truth tr{};
                if (!line.empty() && line[0] != '#' &&
                    std::sscanf(line.c_str(), "%d;%lf;%lf;%lf", &n, &tr.t0, &tr.t1, &tr.level) == 4)
                    truth.push_back(tr);
            }
        }
        CHECK(truth.size() == 10);
        std::vector<double> t, v;
        heel::Series(log, 0, t, v);
        const auto ps = plateau::Detect(t, v, plateau::Params{});
        CHECK_MSG(ps.size() == truth.size(), "положений %zu из %zu", ps.size(), truth.size());
        for (std::size_t i = 0; i < ps.size() && i < truth.size(); i++)
        {
            std::printf("    положение %2zu: %6.1f…%6.1f с, %+8.3f° ± %.3f (истина %+7.3f°, %6.1f…%6.1f с)\n", i + 1,
                        ps[i].t0, ps[i].t1, ps[i].mean, ps[i].sd, truth[i].level, truth[i].t0, truth[i].t1);
            CHECK_NEAR(ps[i].mean, truth[i].level, 0.1, "уровень положения");
            CHECK_MSG(ps[i].t0 >= truth[i].t0 && ps[i].t1 <= truth[i].t1 + 0.2, "положение %zu вне ступеньки", i + 1);
            CHECK_MSG(ps[i].Duration() >= 0.6 * (truth[i].t1 - truth[i].t0), "положение %zu: только %.1f с", i + 1,
                      ps[i].Duration());
        }
        // По Y ступенек нет — положений нет; Y и CalcY = RawY − OffsetY
        heel::Series(log, 1, t, v);
        CHECK(plateau::Detect(t, v, plateau::Params{}).size() <= 1);
    }
}

void TestContinuous()
{
    SECTION("непрерывная запись: Δθ от начального положения, h по положениям, МНК h = 0,8 м");
    {
        const double D = 1500, P = 15, h = 0.8, theta0 = 0.3; // начальный крен 0,3° — вычитается
        const double arms[] = {0.0, 2.0, 4.0, 6.0, -2.0, -4.0};
        std::vector<heel::Point> pts;
        std::vector<bool> use;
        for (int i = 0; i < 6; i++)
        {
            pts.push_back({i + 1, 0, theta0 + std::atan(P * arms[i] / (D * h)) / kDeg, std::fabs(arms[i])});
            use.push_back(true);
        }
        pts.push_back({7, 0, 50.0, 3.0}); // не в расчёте
        use.push_back(false);
        const auto r = heel::SolvePositions(pts, use, D, P, 0.1);
        CHECK(r.points.size() == 7);
        CHECK(r.points[0].reference && r.points[0].dTheta == 0.0 && !r.points[0].counted);
        CHECK(!r.points[6].counted && r.points[6].note == "не в расчёте");
        CHECK(r.n == 5);
        for (int i = 1; i < 6; i++)
        {
            CHECK_NEAR(r.points[static_cast<std::size_t>(i)].h, h, 1e-9, "h положения");
            CHECK_NEAR(r.points[static_cast<std::size_t>(i)].dTheta, std::atan(P * arms[i] / (D * h)) / kDeg, 1e-9, "Δθ");
        }
        CHECK_NEAR(r.h, h, 1e-9, "среднее h");
        CHECK_NEAR(r.ls.h, h, 1e-9, "h по МНК");
        // Первое положение снято — начальным становится второе
        use[0] = false;
        const auto r2 = heel::SolvePositions(pts, use, D, P, 0.1);
        CHECK(r2.points[1].reference && !r2.points[0].counted && r2.n == 4);
    }

    SECTION("непрерывная запись: папка -> режим, положения, крен, отчёт");
    {
        const fs::path dir = testutil::TestDataDir() / "continuous";
        heel::Settings s; // ось X по умолчанию
        CHECK(s.axis == 0);
        const auto loaded = heel::LoadAll(heel::ListCsv(dir), text::PathToUtf8(dir), nullptr, &s);
        CHECK(loaded.files.size() == 2); // CSV и truth.txt (не замер)
        const auto ms = heel::Group(loaded.files, {});
        CHECK(ms.size() == 1);
        CHECK(heel::SuggestRecord(loaded.files, ms) == 0);
        CHECK(heel::LongestRecord(loaded.files, ms) == 0);
        if (ms.size() == 1)
        {
            auto pos = heel::FindPositions(s, loaded.files, ms[0]);
            CHECK_MSG(pos.size() == 10, "положений %zu", pos.size());
            for (std::size_t i = 0; i < pos.size(); i++)
                pos[i].arm = 2.0 * static_cast<double>(i);
            pos[3].use = false;
            const auto sum = heel::ComputePositions(s, loaded.files, ms[0], pos);
            CHECK(sum.measurements == 9);
            if (pos.size() == 10)
            {
                CHECK(pos[0].res[0].reference && std::isnan(pos[0].st[1].mean));
                CHECK_NEAR(pos[1].res[0].dTheta, pos[1].Level(0) - pos[0].Level(0), 1e-12, "Δθ (по методу обработки)");
                CHECK(std::isfinite(pos[1].est[0].level) && pos[1].est[0].winT1 >= pos[1].est[0].winT0);
                CHECK_NEAR(pos[1].res[0].h, heel::Height(s.P, 2.0, s.D, pos[1].res[0].dTheta), 1e-12, "h положения 2");
                CHECK(!pos[3].res[0].counted && pos[3].res[0].note == "не в расчёте");
                CHECK(sum.n == 8); // 10 − начальное − снятое
            }
            const auto sheets = heel::BuildPositionsReport(s, loaded.files, ms[0], pos, sum, {});
            CHECK(sheets.size() == 2 && sheets[0].name == "Положения" && sheets[1].name == "Сырые_данные");
            if (sheets.size() == 2)
            {
                CHECK(sheets[0].rows.size() > 12 && sheets[0].rows[1][0].num == 1.0);
                CHECK(sheets[0].rows[2][10].kind == xlsx::Cell::Kind::Number); // Δθ
                CHECK(sheets[0].rows[0][11].text == "Плечо l (м)" && sheets[0].rows[0][9].text == "Крен по методу (°)");
                CHECK(sheets[1].rows.size() == 1 + loaded.files[ms[0].post[0].file >= 0 ? static_cast<std::size_t>(ms[0].post[0].file) : 0].log.rows.size());
                bool inPos = false;
                for (const auto& row : sheets[1].rows)
                    inPos |= row[0].kind == xlsx::Cell::Kind::Number && row[0].num == 10.0;
                CHECK(inPos);
            }
            // Малое судно: тот же опыт в кг и мм — тот же h; в отчёте — единицы кг/мм и h в мм
            heel::Settings sm = s;
            sm.units.model = true;
            sm.D = s.D * 1000.0;
            sm.P = s.P * 1000.0;
            sm.L = s.L * 1000.0;
            auto posMm = pos;
            for (auto& q : posMm)
                q.arm *= 1000.0;
            const auto sumMm = heel::ComputePositions(sm, loaded.files, ms[0], posMm);
            CHECK_NEAR(sumMm.h, sum.h, 1e-12, "h: т и м = кг и мм");
            CHECK_NEAR(sumMm.ls.h, sum.ls.h, 1e-12, "h по МНК: т и м = кг и мм");
            CHECK_NEAR(posMm[2].res[0].h, pos[2].res[0].h, 1e-12, "h положения 3");
            const auto shMm = heel::BuildPositionsReport(sm, loaded.files, ms[0], posMm, sumMm, {});
            CHECK(shMm[0].rows[0][11].text == "Плечо l (мм)" && shMm[0].rows[0][14].text == "h (мм)");
            CHECK_NEAR(shMm[0].rows[3][14].num, posMm[2].res[0].h * 1000.0, 1e-9, "h в мм");
            bool massKg = false;
            for (const auto& row : shMm[0].rows)
                massKg |= !row.empty() && row[0].text == "Водоизмещение D (кг)";
            CHECK(massKg);
            // «Файл = замер» в кг и мм — тот же h
            auto msT = heel::Group(loaded.files, {});
            auto msK = msT;
            for (auto& q : msT)
                q.arm = 2.0;
            for (auto& q : msK)
                q.arm = 2000.0;
            CHECK_NEAR(heel::Compute(sm, loaded.files, msK).h, heel::Compute(s, loaded.files, msT).h, 1e-12,
                       "файл = замер: т и м = кг и мм");

            // Ручные правки положений: сохранить, прочитать (в других единицах), забыть
            const fs::path edits = fs::temp_directory_path() / L"krenomer_process_test_правки";
            std::error_code ec;
            fs::remove_all(edits, ec);
            const std::string key = heel::EditsKey(loaded.files[0], 0);
            CHECK(key.size() == 16 && key != heel::EditsKey(loaded.files[0], 1));
            auto edited = pos;
            edited[1].t0 += 2.5;
            edited[3].use = false;
            edited.erase(edited.begin() + 5);
            CHECK(heel::SavePositionEdits(edits, key, edited, s.units));
            std::vector<heel::Position> back;
            CHECK(heel::LoadPositionEdits(edits, key, back, sm.units)); // прочитаны в мм
            CHECK(back.size() == edited.size() && !back[3].use && std::fabs(back[1].t0 - edited[1].t0) < 1e-3);
            CHECK_NEAR(back[2].arm, edited[2].arm * 1000.0, 1e-6, "плечо из м в мм");
            CHECK(std::isnan(back[0].arm) == std::isnan(edited[0].arm));
            heel::ForgetPositionEdits(edits, key);
            CHECK(!heel::LoadPositionEdits(edits, key, back, s.units));
        }
        // Папка коротких замеров — «файл = замер»
        const auto shortSet = heel::LoadAll({testutil::TestDataDir() / "2026-10-08_M019_D2.CSV"}, "", nullptr, &s);
        CHECK(heel::SuggestRecord(shortSet.files, heel::Group(shortSet.files, {})) == -1);
    }
}

// Правила РС: табл. 6.5 (h_i по приращениям), оценка качества 6.6 (автоисключение по 6.6.1, вероятная ошибка с t_αn
// по табл. 6.6.2, ε по 6.6.3, не менее 8 замеров, 6.6.5), требования, плечи со знаком, вход из положений и замеров.
void TestRules()
{
    SECTION("Правила РС: t_αn по табл. 6.6.2 (n = 8…16), вне таблицы — нет");
    {
        const double want[] = {5.4, 5.0, 4.8, 4.6, 4.5, 4.3, 4.2, 4.1, 4.0};
        for (int n = 8; n <= 16; n++)
            CHECK_NEAR(rs::TAlpha(n), want[n - 8], 1e-12, "t_αn");
        CHECK(std::isnan(rs::TAlpha(7)) && std::isnan(rs::TAlpha(17)) && std::isnan(rs::TAlpha(0)));
    }

    // Опыт по Инструктивным указаниям: 0 → ПБ (три переноса) → 0 → ЛБ (три) → 0; начальный крен 0,3°
    const double D = 1500, P = 15, h = 0.8, theta0 = 0.3;
    const double armsTrue[] = {0, 1.5, 3.0, 4.5, 3.0, 1.5, 0, -1.5, -3.0, -4.5, -3.0, -1.5, 0};
    std::vector<double> theta;
    for (const double a : armsTrue)
        theta.push_back(theta0 + P * a / (D * h) / kDeg); // крен по табл. 6.5 линеен по моменту (рад)

    SECTION("Правила РС: h_i по приращениям = 0,8 м точно, h_k, σ, вероятная ошибка, требования");
    {
        rs::Input in;
        in.D = D;
        in.P = P;
        in.theta = theta;
        in.arm.assign(std::begin(armsTrue), std::end(armsTrue));
        const auto r = rs::Evaluate(in);
        CHECK(r.m.size() == 12 && r.n == 12);
        for (const auto& q : r.m)
            CHECK_NEAR(q.h, h, 1e-9, "h_i (табл. 6.5)");
        CHECK_NEAR(r.hk, h, 1e-9, "h_k");
        CHECK_NEAR(r.m[0].dM, P * 1.5, 1e-12, "ΔM = P·Δl");
        CHECK(r.ok661 && r.excludedCount == 0 && r.ok664 && r.ok663 == -1 && !r.reduced);
        CHECK_NEAR(r.hFinal, h, 1e-9, "итог = h_k");
        CHECK_NEAR(r.tA, 4.5, 1e-12, "t_αn при n = 12");
        CHECK_NEAR(r.errLimit, 0.02 * 1.8, 1e-12, "допуск 6.6.2 при h_k ≤ 2 м");
        rs::Conditions c;
        c.theta0 = theta0;
        for (const double t : theta)
            c.dTheta.push_back(t - theta0);
        c.instruments = 2;
        c.spread = 0.03;
        c.h = r.hFinal;
        const auto checks = rs::Checks(r, c);
        CHECK(checks.size() == 11);
        int bad = 0, unknown = 0;
        for (const auto& k : checks)
        {
            bad += k.state == 0;
            unknown += k.state < 0;
        }
        CHECK_MSG(bad == 0 && unknown == 1, "нарушено %d, без данных %d", bad, unknown); // без данных — только 6.6.3
        CHECK(checks[5].req == "Крен 1°–4° на каждый борт" && checks[5].state == 1); // ±3,22°
        CHECK(checks[7].state == 1 && checks[7].value == "положение 7");           // возврат в ноль
        c.spread = 0.08;                                                            // посты разошлись
        c.instruments = 1;
        c.theta0 = 0.7;
        const auto checks2 = rs::Checks(r, c);
        CHECK(checks2[6].state == 0 && checks2[8].state == 0 && checks2[9].state == 0);
    }

    SECTION("Правила РС: замер с ошибкой 12 % исключается по 6.6.1 (один), h_k = 0,8 м; ручное «учитывать» — 6.6.5");
    {
        rs::Input in;
        in.D = D;
        in.P = P;
        in.theta = theta;
        in.arm = testutil::RsArms(theta, D, P, h, 5); // перенос 5 (3,0 → 1,5 м) записан на 12 % длиннее
        auto r = rs::Evaluate(in);
        CHECK_NEAR(r.m[4].h, h * 1.12 * (1 + 0.0045), 1e-9, "h_5 с ошибкой 12 %");
        CHECK(r.m[4].excluded && r.m[4].autoExcluded && r.excludedCount == 1);
        int autoOut = 0;
        for (const auto& q : r.m)
            autoOut += q.autoExcluded;
        CHECK(autoOut == 1 && r.n == 11 && r.ok661 && r.ok664);
        CHECK_NEAR(r.hk, h, 0.004, "h_k без исключённого");
        CHECK_NEAR(r.tA, 4.6, 1e-12, "t_αn при n = 11");
        double v = 0;
        for (const auto& q : r.m)
            if (!q.excluded)
                v += (q.h - r.hk) * (q.h - r.hk);
        CHECK_NEAR(r.err, 4.6 * std::sqrt(v / (11.0 * 10.0)), 1e-12, "вероятная ошибка 6.6.2");
        CHECK(r.ok662 && !r.reduced);
        // 6.6.3: ε = min(0,05·1,0; 0,10·0,3) = 0,03 -> не менее 0,04 м; Δ0/Δ = 1000/1500
        in.hInfo = 1.0;
        in.lMax = 0.3;
        in.D0 = 1000;
        r = rs::Evaluate(in);
        CHECK_NEAR(r.eps, 0.04, 1e-12, "ε не менее 4 см");
        CHECK_NEAR(r.err663, r.err * 1000.0 / 1500.0, 1e-12, "ошибка·Δ0/Δ");
        CHECK(r.ok663 == 1);
        in.hInfo = 2.0;
        in.lMax = kNaNt;
        CHECK_NEAR(rs::Evaluate(in).eps, 0.10, 1e-12, "ε = 0,05·h без l_max");
        in.hInfo = kNaNt;
        CHECK(rs::Evaluate(in).ok663 == -1); // нет данных — не проверяется
        // Вручную «учитывать» плохой замер: 6.6.1 не выполнен -> 6.6.5: h_k минус вероятная ошибка
        in.manual.assign(12, -1);
        in.manual[4] = 1;
        r = rs::Evaluate(in);
        CHECK(!r.m[4].excluded && r.excludedCount == 0 && r.n == 12 && !r.ok661);
        CHECK(r.reduced && std::isfinite(r.err));
        CHECK_NEAR(r.hFinal, r.hk - r.err, 1e-12, "6.6.5: h_k − вероятная ошибка");
        // Вручную исключён другой: автоисключения нет, плохой остаётся -> 6.6.1 не выполнен
        in.manual.assign(12, -1);
        in.manual[1] = 0;
        r = rs::Evaluate(in);
        CHECK(r.m[1].excluded && !r.m[1].autoExcluded && !r.m[4].excluded && !r.ok661 && r.n == 11);
        // Вручную исключён сам плохой: как автоматически
        in.manual.assign(12, -1);
        in.manual[4] = 0;
        r = rs::Evaluate(in);
        CHECK(r.m[4].excluded && !r.m[4].autoExcluded && r.ok661 && r.n == 11);
    }

    SECTION("Правила РС: граничные случаи — n < 8, h_k > 2 м, нет перемещения, мало положений");
    {
        rs::Input in;
        in.D = D;
        in.P = P;
        in.theta.assign(theta.begin(), theta.begin() + 6);
        in.arm.assign(std::begin(armsTrue), std::begin(armsTrue) + 6);
        auto r = rs::Evaluate(in);
        CHECK(r.n == 5 && !r.ok664 && std::isnan(r.tA) && std::isnan(r.err) && !r.ok662);
        CHECK(r.note.find("меньше 8") != std::string::npos && !r.reduced);
        CHECK_NEAR(r.hFinal, h, 1e-9, "n < 8: итог — h_k (ошибку не посчитать)");
        const auto ch = rs::Checks(r, rs::Conditions{});
        CHECK(ch[0].state == 0 && ch[2].state == 0 && ch[2].value == "n вне 8…16");
        // h_k = 2,5 м: допуск 0,01(4 + h_k)
        rs::Input big = in;
        big.theta.clear();
        for (const double a : armsTrue)
            big.theta.push_back(P * a / (D * 2.5) / kDeg);
        big.arm.assign(std::begin(armsTrue), std::end(armsTrue));
        r = rs::Evaluate(big);
        CHECK_NEAR(r.hk, 2.5, 1e-9, "h_k = 2,5 м");
        CHECK_NEAR(r.errLimit, 0.01 * 6.5, 1e-12, "допуск 6.6.2 при h_k > 2 м");
        CHECK(rs::Checks(r, rs::Conditions{})[2].req == "Вероятная ошибка ≤ 0,01(4 + h_k)");
        // Груз не перемещали (ΔM = 0): не замер
        rs::Input same = in;
        same.arm[1] = same.arm[0]; // первый перенос не записан: груз «не двигался»
        r = rs::Evaluate(same);
        CHECK(std::isnan(r.m[0].h) && r.m[0].excluded && r.n == 4 && r.excludedCount == 0);
        // Одно положение — нет замеров; нет D
        rs::Input one;
        one.D = D;
        one.P = P;
        one.theta = {0.1};
        one.arm = {0.0};
        CHECK(rs::Evaluate(one).note == "нужно два положения и больше");
        rs::Input noD = in;
        noD.D = kNaNt;
        CHECK(rs::Evaluate(noD).note == "не задано водоизмещение");
    }

    SECTION("плечи со знаком (+ к правому борту) = старый ввод модулем со знаком по крену");
    {
        std::vector<heel::Point> sgn, mod;
        for (std::size_t i = 1; i < 13; i++)
        {
            sgn.push_back({static_cast<int>(i), 0, theta[i] - theta0, armsTrue[i]});
            mod.push_back({static_cast<int>(i), 0, theta[i] - theta0, std::fabs(armsTrue[i])});
        }
        const auto a = heel::Solve(sgn, D, P, 0.1), b = heel::Solve(mod, D, P, 0.1);
        CHECK(a.n == b.n && a.n == 10); // возвраты в ноль — ниже порога
        CHECK_NEAR(a.h, b.h, 1e-12, "среднее h: со знаком = модулем");
        CHECK_NEAR(a.ls.h, b.ls.h, 1e-12, "МНК: со знаком = модулем");
        // Знак плеча задаёт борт: плечо +, крен − (неверный знак) -> точка не на прямой
        sgn[7].arm = -sgn[7].arm;
        CHECK(std::fabs(heel::Solve(sgn, D, P, 0.1).ls.h - a.ls.h) > 0.01);
    }

    SECTION("Правила РС на записи: положения (синтетика), плечи со знаком и без, Tc, лист «Проверки»");
    {
        const fs::path dir = testutil::TestDataDir() / "continuous";
        heel::Settings s;
        const auto loaded = heel::LoadAll(heel::ListCsv(dir), text::PathToUtf8(dir), nullptr, &s);
        const auto ms = heel::Group(loaded.files, {});
        CHECK(ms.size() == 1);
        if (ms.size() == 1)
        {
            auto pos = heel::FindPositions(s, loaded.files, ms[0]);
            heel::ComputePositions(s, loaded.files, ms[0], pos);
            std::vector<double> lv;
            for (const auto& q : pos)
                lv.push_back(q.Level(0));
            const auto arms = testutil::RsArms(lv, s.D, s.P, 0.8, 5);
            for (std::size_t i = 0; i < pos.size(); i++)
                pos[i].arm = arms[i];
            const auto sum = heel::ComputePositions(s, loaded.files, ms[0], pos);
            auto k = heel::RsFromPositions(s, loaded.files, ms[0], pos, {}, sum.h);
            CHECK(!k.legacy && k.in.theta.size() == 10 && k.r.m.size() == 9);
            CHECK(k.r.m[4].autoExcluded && k.r.n == 8 && k.r.ok661 && k.r.ok664);
            CHECK_NEAR(k.r.hk, 0.8, 0.004, "h_k на записи");
            CHECK(k.excluded[5] && !k.excluded[4] && k.label[5] == "6");
            CHECK_NEAR(k.tanT[3], std::tan((lv[3] - lv[0]) * kDeg), 1e-12, "график контроля: tg θ");
            CHECK_NEAR(k.M[3], s.P * arms[3], 1e-9, "график контроля: M = P·l");
            CHECK_RANGE(1.0 / (k.fit.k * s.D), 0.70, 0.90, "h по прямой графика контроля");
            CHECK_RANGE(k.tc, 3.0, 15.0, "период качки Tc по записи"); // синтетика: 6–10 с
            CHECK(k.c.instruments == 1 && std::isnan(k.c.spread));
            CHECK_NEAR(k.hMain, sum.h, 1e-12, "основной расчёт по умолчанию — от начального");
            // Ручное «учитывать» по номеру положения
            k = heel::RsFromPositions(s, loaded.files, ms[0], pos, {{6, 1}}, sum.h);
            CHECK(!k.r.m[4].excluded && k.r.reduced);
            // Основной — табл. 6.5
            heel::Settings s2 = s;
            s2.rsMain = 1;
            k = heel::RsFromPositions(s2, loaded.files, ms[0], pos, {}, sum.h);
            CHECK_NEAR(k.hMain, k.r.hFinal, 1e-12, "основной расчёт — табл. 6.5");
            // Лист «Проверки»
            const auto sh = heel::BuildRsSheet(s2, k);
            CHECK(sh.name == "Проверки" && sh.rows.size() > 30);
            bool t65 = false, auto661 = false, tc = false;
            for (const auto& row : sh.rows)
                for (const auto& c : row)
                {
                    t65 |= c.text == "Таблица 6.5: замер";
                    auto661 |= c.text.find("исключён по 6.6.1") != std::string::npos;
                    tc |= c.text.find("Период бортовой качки") != std::string::npos;
                }
            CHECK(t65 && auto661 && tc);
            // Плечи без знака (старый ввод): знак по крену — тот же результат
            const auto good = testutil::RsArms(lv, s.D, s.P, 0.8); // без ошибки: знак плеча = знак Δθ
            auto posSgn = pos, posAbs = pos;
            for (std::size_t i = 0; i < pos.size(); i++)
            {
                posSgn[i].arm = good[i];
                posAbs[i].arm = std::fabs(good[i]);
            }
            const auto kSgn = heel::RsFromPositions(s, loaded.files, ms[0], posSgn, {}, sum.h);
            const auto kAbs = heel::RsFromPositions(s, loaded.files, ms[0], posAbs, {}, sum.h);
            CHECK(!kSgn.legacy && kAbs.legacy && !kAbs.c.signedArms);
            CHECK_NEAR(kAbs.r.hk, kSgn.r.hk, 1e-12, "без знака = со знаком (знак по крену)");
            CHECK_NEAR(kSgn.r.hk, 0.8, 0.002, "h_k без ошибки");
            // Малое судно: кг и мм — тот же h_k
            heel::Settings sm = s;
            sm.units.model = true;
            sm.D = s.D * 1000;
            sm.P = s.P * 1000;
            auto posMm = pos;
            for (auto& q : posMm)
                q.arm *= 1000;
            const auto km = heel::RsFromPositions(sm, loaded.files, ms[0], posMm, {}, sum.h);
            CHECK_NEAR(km.r.hk, k.r.hk, 1e-12, "табл. 6.5: т и м = кг и мм");
            CHECK_NEAR(km.M[3], k.M[3] * 1e6, 1e-3, "момент в кг·мм");
        }
    }
}

// Книги Excel как источник: своя (Xlsx.hpp, со сжатием и без), как пишет Excel (общие строки, даты числами —
// часть книги руководителя), openpyxl (inlineStr, данные на втором листе); распаковка deflate; отбор книг в папке.
void TestXlsxRead()
{
    SECTION("книги .xlsx: Xlsx.hpp, Excel (общие строки, даты числами), openpyxl (второй лист), inflate, папка");
    {
        // Своя книга: шапка 1.4+, Date и Time — строками
        xlsx::Sheet sh("Замер");
        sh.AddRow({xlsx::Text("Date"), xlsx::Text("Time"), xlsx::Text("CalcX"), xlsx::Text("CalcY"), xlsx::Text("Ms")});
        for (int i = 0; i < 50; i++)
            sh.AddRow({xlsx::Text("09.10.2026"), xlsx::Text(i < 25 ? "19:07:20" : "19:07:22"), xlsx::Num(1.0 + i * 0.01, 3),
                       xlsx::Num(-0.5, 3), xlsx::Num(i * 100.0 + 55, 0)});
        for (const bool packed : {false, true})
        {
            const std::string book = xlsx::Build({xlsx::Sheet("Пусто"), sh}, 0, packed);
            csvlog::Log log;
            CHECK_MSG(csvlog::ParseAny(book, log), "своя книга: %s %s", log.error.c_str(), log.firstLine.c_str());
            CHECK(log.fromXlsx && log.sheet == "Замер" && log.rows.size() == 50 && log.time == csvlog::TimeSource::Ms);
            if (log.rows.size() == 50)
            {
                CHECK_NEAR(log.rows[49].calcX, 1.49, 1e-12, "CalcX из книги");
                CHECK_NEAR(log.rows[49].t, 4.955, 1e-12, "t по Ms");
                CHECK(log.rows[0].day == csvlog::DaysFromCivil(2026, 10, 9) && log.rows[0].sec == 19 * 3600 + 7 * 60 + 20);
            }
        }
        // Как пишет Excel: sharedStrings, Date и Time — числа (46304 и доля суток), deflate со своими кодами
        csvlog::Log ex;
        CHECK_MSG(csvlog::ReadFile(testutil::TestDataDir() / "xlsx" / "excel_100.xlsx", ex), "%s", ex.firstLine.c_str());
        CHECK(ex.fromXlsx && ex.format == csvlog::Format::Fw14 && ex.rows.size() == 100);
        if (ex.rows.size() == 100)
        {
            CHECK(ex.rows[0].day == csvlog::DaysFromCivil(2026, 10, 9));
            CHECK(ex.rows[0].sec == 19 * 3600 + 7 * 60 + 20);
            CHECK_NEAR(ex.rows[0].t, 0.055, 1e-12, "Ms первой строки");
            CHECK_NEAR(ex.rows[0].calcX, -0.141, 1e-12, "CalcX первой строки");
            CHECK_NEAR(ex.rows[0].offX, 2.701, 1e-12, "OffsetX");
            CHECK_NEAR(heel::TailStats(ex, 0, 1e9).mean, -0.2319, 1e-9, "среднее CalcX 100 строк");
        }
        // openpyxl: первый лист — заметки, данные — на втором; inlineStr, Date — дата, Time — время
        csvlog::Log op;
        CHECK_MSG(csvlog::ReadFile(testutil::TestDataDir() / "xlsx" / L"опыт_openpyxl.xlsx", op), "%s", op.firstLine.c_str());
        CHECK(op.fromXlsx && op.sheet == "Данные" && op.rows.size() == 200);
        if (op.rows.size() == 200)
        {
            CHECK(op.rows[0].sec == 19 * 3600 + 7 * 60 + 20 && op.rows[0].day == csvlog::DaysFromCivil(2026, 10, 9));
            CHECK_NEAR(heel::TailStats(op, 0, 1e9).mean, -0.0882, 1e-9, "среднее CalcX 200 строк");
        }
        // Не книга и испорченная книга — сообщение, не падение
        csvlog::Log bad;
        CHECK(!csvlog::ParseAny(std::string("PK\x03\x04", 4) + "мусор", bad) && bad.error == "не понял формат");
        std::string cut = xlsx::Build({sh}, 0, true);
        cut.resize(cut.size() / 2);
        CHECK(!csvlog::ParseAny(cut, bad));
        // Inflate: обратно то, что сжал Compress, и блок без сжатия
        std::string text;
        for (int i = 0; i < 5000; i++)
            text += "<c r=\"A" + std::to_string(i) + "\"><v>" + std::to_string(i * 0.25) + "</v></c>";
        const auto z = deflate::Compress(text);
        std::vector<unsigned char> back;
        CHECK(deflate::Inflate(z.data(), z.size(), back) && std::string(back.begin(), back.end()) == text);
        const unsigned char stored[] = {0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c'};
        CHECK(deflate::Inflate(stored, sizeof(stored), back) && back.size() == 3 && back[2] == 'c');
        CHECK(!deflate::Inflate(z.data(), z.size() / 2, back));

        // Папка: свои книги и копии CSV — мимо
        const fs::path dir = fs::temp_directory_path() / L"krenomer_process_test_книги";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        for (const wchar_t* f : {L"A.CSV", L"A.xlsx", L"B_PC.xlsx", L"B_PC_D2.CSV", L"Отчет_Кренование.xlsx", L"C.xlsx"})
            WriteText(dir / f, "x");
        const auto paths = heel::ListCsv(dir);
        std::vector<std::string> names;
        for (const auto& p : paths)
            names.push_back(text::PathToUtf8(p.filename()));
        CHECK_MSG(names == std::vector<std::string>({"A.CSV", "B_PC_D2.CSV", "C.xlsx"}), "файлов %zu", names.size());
        // Имя книги группируется так же, как CSV
        fs::copy_file(testutil::TestDataDir() / "xlsx" / "excel_100.xlsx", dir / "2026-10-09_M020_D2.xlsx", ec);
        const auto f = heel::LoadFile(dir / "2026-10-09_M020_D2.xlsx");
        CHECK(f.number == 20 && f.sensor == 2 && f.other.empty() && f.log.error.empty());
    }
}

// Настоящая запись руководителя (tests/data/real: Ms, CalcX, CalcY из его книги 2026-10-09_M020_D2.xlsx, прошивка
// 1.5): положения груза по CalcX. На глаз (график): ~0 → ~3 → ~6 → ~8 → (провал ~2, ~5 — груз переставляли) → ~10,5 →
// −2,5 → −6 → −9 → −12, всплески на переходах (67 с, 181 с, 380 с, 436 с).
void TestRealRecord()
{
    SECTION("настоящая запись руководителя: положения по CalcX, всплески не внутри положений");
    csvlog::Log log;
    CHECK(csvlog::ReadFile(testutil::TestDataDir() / "real" / "2026-10-09_M020_D2.CSV", log));
    CHECK_MSG(log.rows.size() == 5399 && log.time == csvlog::TimeSource::Ms, "строк %zu", log.rows.size());
    std::vector<double> t, v;
    heel::Series(log, 0, t, v);
    const auto ps = plateau::Detect(t, v, plateau::Params{});
    const double want[] = {0.14, 2.88, 5.68, 8.09, 5.06, 10.43, -2.66, -5.97, -9.09, -11.79};
    CHECK_MSG(ps.size() == 10, "положений %zu", ps.size());
    for (std::size_t i = 0; i < ps.size(); i++)
    {
        std::printf("    положение %2zu: %6.1f…%6.1f с (%5.1f с), %+8.3f° ± %.3f, выбросов %d\n", i + 1, ps[i].t0, ps[i].t1,
                    ps[i].Duration(), ps[i].mean, ps[i].sd, ps[i].rejected);
        if (i < 10)
            CHECK_NEAR(ps[i].mean, want[i], 0.1, "уровень положения");
        CHECK_MSG(ps[i].sd < 0.7, "положение %zu: СКО %.3f — всплеск внутри?", i + 1, ps[i].sd);
        for (const double spike : {181.5, 380.5, 436.0})
            CHECK_MSG(!(spike >= ps[i].t0 && spike <= ps[i].t1), "положение %zu захватило всплеск %.1f с", i + 1, spike);
    }
    // Режим по содержимому — «весь опыт одной записью»
    heel::Settings s;
    const auto loaded = heel::LoadAll({testutil::TestDataDir() / "real" / "2026-10-09_M020_D2.CSV"}, "", nullptr, &s);
    CHECK(heel::SuggestRecord(loaded.files, heel::Group(loaded.files, {})) == 0);
}

// Обработка сигнала положения (Filters.hpp): фильтры по статье, альтернативы, сравнение методов.
void TestFilters()
{
    SECTION("фильтры: медиана по 3 последним, EMA, α по (12), Савицкий–Голай, частота качки, синусоида, Калман");
    {
        const auto med = filt::MedianCausal({1, 9, 2, 3, 100, 4, 5}, 3);
        CHECK(med == std::vector<double>({1, 5, 2, 3, 3, 4, 5}));
        const auto ema = filt::Ema({0, 10, 10}, 0.5);
        CHECK(ema == std::vector<double>({0, 5, 7.5}));
        CHECK_NEAR(filt::AlphaFor(0.5, 20.0), 0.13575, 1e-4, "α при fоп = 20 Гц (статья: 0,136)");
        CHECK_NEAR(filt::AlphaFor(0.5, 10.0), 0.23906, 1e-4, "α при 10 Гц");
        std::vector<double> t, x;
        for (int i = 0; i < 200; i++)
        {
            t.push_back(i * 0.1);
            x.push_back(1.0 + 0.5 * t.back() - 0.02 * t.back() * t.back());
        }
        const auto sg = filt::SavGol(t, x, 4.0, 2);
        CHECK_NEAR(sg[100], x[100], 1e-9, "Савицкий–Голай: парабола без искажения");
        CHECK_NEAR(sg[0], x[0], 1e-9, "Савицкий–Голай: край");
        x.clear();
        for (const double tt : t)
            x.push_back(2.0 + 1.2 * std::exp(-tt / 15.0) * std::sin(2 * heel::kPi * tt / 8.0 + 0.4));
        CHECK_NEAR(filt::DominantOmega(t, x), 2 * heel::kPi / 8.0, 0.05, "частота качки");
        const auto fit = filt::FitDampedSine(t, x);
        CHECK(fit.ok);
        CHECK_NEAR(fit.level, 2.0, 1e-3, "затухающая синусоида: равновесный угол");
        CHECK_NEAR(fit.omega, 2 * heel::kPi / 8.0, 1e-2, "затухающая синусоида: ω");
        const auto kl = filt::KalmanLevel(t, x, 2 * heel::kPi / 8.0);
        CHECK_NEAR(kl.back(), 2.0, 0.05, "Калман: уровень");
        filt::Params p;
        CHECK(p.method == filt::Method::Kalman); // по умолчанию — Калман
        p.method = filt::Method::MedianEma;
        const auto e = filt::EstimateSpan(t, x, filt::ProcessSeries(t, x, p), 0.0, 19.9, p);
        CHECK_MSG(e.alpha > 0.23 && e.alpha < 0.25, "α %.4f", e.alpha);
        CHECK_MSG(e.tcS > 6.0 && e.tcS < 12.0, "Tc %.2f", e.tcS); // 2,5 качания затухающей синусоиды — грубо
        CHECK_NEAR(e.winT0, 0.0, 1e-9, "участок короче 3·Tc — окно Ω на весь участок");
        const auto e2 = filt::EstimateSpan(t, x, filt::ProcessSeries(t, x, p), 0.0, 19.9, [] {
            filt::Params q;
            q.method = filt::Method::MedianEma;
            q.tcS = 4.0;
            return q;
        }());
        CHECK_NEAR(e2.winT0, e2.winT1 - 12.0 + 0.05, 1e-9, "окно Ω = 3·Tc в конце участка");
    }

    SECTION("сравнение методов: синтетика (истина известна), записи с качкой и выбросами, запись руководителя");
    struct Row
    {
        double err = 0, spread = 0, settle = 0;
        int n = 0, nSpread = 0, fail = 0;
    };
    struct Variant
    {
        const char* name;
        filt::Params p;
    };
    std::vector<Variant> vars;
    for (int m = 0; m < filt::kBuiltinCount; m++)
    {
        filt::Params p;
        p.method = static_cast<filt::Method>(m);
        vars.push_back({filt::MethodName(p.method), p});
        if (p.method == filt::Method::MedianEma)
        {
            p.wholeSpan = true;
            vars.push_back({"Медиана + EMA, весь участок", p});
        }
    }
    const int nv = static_cast<int>(vars.size());
    Row rows[3][8];
    auto add = [&](int set, int m, const filt::Quality& q, double truth) {
        Row& r = rows[set][m];
        if (!std::isfinite(q.level))
        {
            r.fail++;
            return;
        }
        r.n++;
        if (std::isfinite(truth))
            r.err += std::fabs(q.level - truth);
        if (std::isfinite(q.spread))
        {
            r.spread += q.spread;
            r.nSpread++;
        }
        r.settle += q.settleS;
    };
    // 1. Одно положение: истина 2,000°, затухающая качка 1,5° (8 с, τ 20 с) + постоянная 0,3° (11 с) + шум 0,05° +
    //    5 одиночных выбросов ±3° и всплеск 1,5 с; участок 3…60 с. Пять реализаций шума.
    for (unsigned seed = 1; seed <= 5; seed++)
    {
        std::mt19937 rng(seed);
        std::normal_distribution<double> nz(0.0, 0.05);
        std::vector<double> t, x;
        for (int i = 0; i <= 600; i++)
        {
            const double tt = i * 0.1;
            double v = 2.0 + 1.5 * std::exp(-tt / 20.0) * std::sin(2 * heel::kPi * tt / 8.0 + 0.3 * seed) +
                       0.3 * std::sin(2 * heel::kPi * tt / 11.0 + seed) + nz(rng);
            if (i % 113 == 57)
                v += (i % 2 ? 3.0 : -3.0);
            if (tt >= 30.0 && tt < 31.5)
                v += 2.5;
            t.push_back(tt);
            x.push_back(v);
        }
        for (int m = 0; m < nv; m++)
            add(0, m, filt::Assess(t, x, 3.0, 60.0, vars[static_cast<std::size_t>(m)].p), 2.0);
    }
    // 2. Синтетическая запись опыта (tests/data/continuous): положения — детектором, истина — truth.txt
    {
        csvlog::Log log;
        csvlog::ReadFile(testutil::TestDataDir() / "continuous" / "2026-10-09_M020_D2.CSV", log);
        std::vector<double> t, x;
        heel::Series(log, 0, t, x);
        const double truth[10] = {0, 3, 6, 8, 10.5, -2.5, -6, -9.5, -12, 0.3};
        const auto ps = plateau::Detect(t, x, plateau::Params{});
        for (std::size_t i = 0; i < ps.size() && i < 10; i++)
            for (int m = 0; m < nv; m++)
                add(1, m, filt::Assess(t, x, ps[i].t0, ps[i].t1, vars[static_cast<std::size_t>(m)].p), truth[i]);
    }
    // 3. Запись руководителя (tests/data/real): истины нет — устойчивость и сходимость
    {
        csvlog::Log log;
        csvlog::ReadFile(testutil::TestDataDir() / "real" / "2026-10-09_M020_D2.CSV", log);
        std::vector<double> t, x;
        heel::Series(log, 0, t, x);
        const auto ps = plateau::Detect(t, x, plateau::Params{});
        std::printf("    запись руководителя, крен положений по методам, °:\n      №  ");
        for (int m = 0; m < nv; m++)
            std::printf(" | %s", vars[static_cast<std::size_t>(m)].name);
        std::printf("\n");
        for (std::size_t i = 0; i < ps.size(); i++)
        {
            std::printf("     %2zu  ", i + 1);
            for (int m = 0; m < nv; m++)
            {
                const auto q = filt::Assess(t, x, ps[i].t0, ps[i].t1, vars[static_cast<std::size_t>(m)].p);
                add(2, m, q, heel::kNaN);
                std::printf(" %+11.3f (%4.0f с) ", q.level, q.settleS);
            }
            std::printf("\n");
        }
    }
    const char* setName[3] = {"одно положение, 5 реализаций (истина 2°)", "синтетическая запись, 10 положений",
                              "запись руководителя M020, 10 положений"};
    for (int s = 0; s < 3; s++)
    {
        std::printf("    %s:\n      | метод | |ошибка|, ° | разброс окон 15 с, ° | до ±0,02° от итога, с |\n",
                    setName[s]);
        for (int m = 0; m < nv; m++)
        {
            const Row& r = rows[s][m];
            std::printf("      | %s | %s | %.3f | %.1f |%s\n", vars[static_cast<std::size_t>(m)].name,
                        s == 2 ? "—" : Fmt3(r.n ? r.err / r.n : heel::kNaN).c_str(),
                        r.nSpread ? r.spread / r.nSpread : heel::kNaN, r.n ? r.settle / r.n : heel::kNaN,
                        r.fail ? " (не сошлось)" : "");
        }
    }
    // Метод статьи на синтетике — в пределах 0,1° от истины
    CHECK(rows[1][0].n == 10 && rows[1][0].err / rows[1][0].n < 0.1);
    CHECK(rows[0][0].n == 5 && rows[0][0].err / rows[0][0].n < 0.15);
}

// Формулы (Expr.hpp) и свои методы (UserMethod.hpp).
void TestUserMethod()
{
    SECTION("формулы: разбор, вычисление, ошибки с позицией, ограничения");
    {
        auto val = [](const char* src) {
            expr::Error err;
            const double v = expr::Constant(src, expr::Env{}, {}, err);
            CHECK_MSG(err.Ok(), "«%s»: %s", src, err.msg.c_str());
            return v;
        };
        CHECK_NEAR(val("1 + 2*3^2"), 19.0, 1e-12, "приоритет");
        CHECK_NEAR(val("2^3^2"), 512.0, 1e-12, "степень справа налево");
        CHECK_NEAR(val("-2^2"), -4.0, 1e-12, "унарный минус");
        CHECK_NEAR(val("(1 + 2) * 3"), 9.0, 1e-12, "скобки");
        CHECK_NEAR(val("max(1, 5, 3) + min(4, 2) + mean(1, 2, 3)"), 9.0, 1e-12, "min/max/mean");
        CHECK_NEAR(val("if(1 > 2, 10, 20) + (1 < 2 ? 3 : 4)"), 23.0, 1e-12, "условие");
        CHECK_NEAR(val("abs(-3) + sqrt(16) + ln(exp(2)) + atan(1)*4/pi"), 10.0, 1e-12, "функции");
        CHECK_NEAR(val("1 and 0 or not 0"), 1.0, 1e-12, "логика");
        CHECK_NEAR(val("2.5e-1 * 4 == 1"), 1.0, 1e-12, "сравнение");
        CHECK(std::isnan(val("sqrt(-1)")) && std::isinf(val("1/0")));
        struct Bad
        {
            const char* src;
            std::size_t pos;
            const char* msg;
        };
        expr::Env env;
        env.vars = {"a"};
        env.allowIndex = env.allowY = env.allowSeries = true;
        const Bad bad[] = {
            {"1 + ", 5, "формула оборвана"},          {"foo(1)", 1, "неизвестная функция «foo»"},
            {"a + b", 5, "неизвестное имя «b»"},      {"(1 + 2", 7, "ожидалась «)»"},
            {"2 $ 3", 3, "непонятный знак"},          {"y[n]", 1, "y[n] ещё не посчитано"},
            {"x[n+1]", 4, "индекс"},                  {"y + 1", 1, "y без индекса"},
            {"abs(1, 2)", 1, "abs(…): нужно 1"},     {"median(a, 3)", 1, "первый аргумент — x"},
            {"1 2", 3, "лишнее в конце"},             {"avg()", 1, "только в правиле среднего"},
        };
        for (const Bad& b : bad)
        {
            expr::Program p;
            expr::Error err;
            CHECK_MSG(!expr::Compile(b.src, env, p, err), "«%s» разобрано, а не должно", b.src);
            CHECK_MSG(err.pos == b.pos && err.msg.find(b.msg) != std::string::npos, "«%s»: знак %zu, «%s»", b.src, err.pos,
                      err.msg.c_str());
        }
        expr::Program p;
        expr::Error err;
        std::string longText(2100, '1');
        CHECK(!expr::Compile(longText, env, p, err) && err.msg.find("длиннее") != std::string::npos);
        std::string deep;
        for (int i = 0; i < 200; i++)
            deep += "(";
        CHECK(!expr::Compile(deep + "1", env, p, err));
        std::string many = "1";
        for (int i = 0; i < 700; i++)
            many += "+1";
        CHECK(!expr::Compile(many, env, p, err) && err.msg.find("сложная") != std::string::npos);
    }

    SECTION("свой метод: текст, ошибки, пресет «статья» = встроенный метод статьи (1e-9), нечисловые значения");
    {
        const um::Method art = um::PresetArticle();
        um::Method back;
        std::string err;
        CHECK(um::FromText(um::ToText(art), back, &err));
        CHECK(back.name == art.name && back.consts == art.consts && back.stages.size() == 2 &&
              back.stages[1].formula == art.stages[1].formula && back.mean == um::MeanKind::Window && back.meanArg == "3*Tc");
        CHECK(!um::FromText("hello", back, &err) && err.find("krenomer-method") != std::string::npos);
        CHECK(!um::FromText("krenomer-method 1\nfoo = 1\n", back, &err) && err.find("строка 2") != std::string::npos);

        um::Problem pr;
        um::Method badM = art;
        badM.stages[1].formula = "a*x + z";
        CHECK(!um::Build(badM, pr) && pr.Text() == "Стадия 2, знак 7: неизвестное имя «z»");
        badM = art;
        badM.consts.push_back({"x", "1"});
        CHECK(!um::Build(badM, pr) && pr.where.find("Константа") == 0);
        badM = art;
        badM.mean = um::MeanKind::Expr;
        badM.meanArg = "avgw(";
        CHECK(!um::Build(badM, pr) && pr.where == "Правило среднего");

        // Пресет «статья» формулами против встроенного метода статьи на синтетической записи
        csvlog::Log log;
        csvlog::ReadFile(testutil::TestDataDir() / "continuous" / "2026-10-09_M020_D2.CSV", log);
        std::vector<double> t, x;
        heel::Series(log, 0, t, x);
        const auto ua = um::Build(art, pr);
        CHECK_MSG(ua != nullptr, "%s", pr.Text().c_str());
        if (ua)
        {
            filt::Params bi;
            bi.method = filt::Method::MedianEma;
            filt::Params us;
            us.method = filt::Method::User;
            us.custom = ua;
            const auto a = filt::ProcessSeries(t, x, bi), b = filt::ProcessSeries(t, x, us);
            double worst = 0;
            for (std::size_t i = 0; i < a.size() && i < b.size(); i++)
                worst = std::max(worst, std::fabs(a[i] - b[i]));
            CHECK(a.size() == b.size() && a.size() == t.size());
            CHECK_MSG(worst < 1e-9, "обработанный сигнал: расхождение %.3g", worst);
            const auto ps = plateau::Detect(t, x, plateau::Params{});
            CHECK(ps.size() == 10);
            for (const auto& sp : ps)
            {
                const auto ea = filt::EstimateSpan(t, x, a, sp.t0, sp.t1, bi);
                const auto eb = filt::EstimateSpan(t, x, b, sp.t0, sp.t1, us);
                CHECK_NEAR(eb.level, ea.level, 1e-9, "крен положения: формулы = встроенный");
                CHECK_NEAR(eb.winT0, ea.winT0, 1e-9, "окно Ω");
            }
            // Через heel: свой метод в настройках расчёта
            std::vector<heel::File> files{heel::LoadFile(testutil::TestDataDir() / "continuous" / "2026-10-09_M020_D2.CSV")};
            auto ms = heel::Group(files, {});
            heel::Settings s1, s2;
            s1.filter = bi;
            s2.filter = us;
            auto p1 = heel::FindPositions(s1, files, ms[0]), p2 = p1;
            heel::ComputePositions(s1, files, ms[0], p1);
            heel::ComputePositions(s2, files, ms[0], p2);
            for (std::size_t i = 0; i < p1.size(); i++)
                CHECK_NEAR(p2[i].Level(0), p1[i].Level(0), 1e-9, "heel: свой метод = встроенный");
        }
        // Нечисловые значения: 1/(x − x) — заменяются предыдущим, считаются
        um::Method nan;
        nan.stages = {{"n < 5 ? x : 1/(x - x) * 0", "x[0]"}};
        nan.mean = um::MeanKind::All;
        const auto nm = um::Build(nan, pr);
        CHECK(nm != nullptr);
        if (nm)
        {
            const auto y = nm->Process(t, x, 0);
            bool finite = true;
            for (const double v : y)
                finite = finite && std::isfinite(v);
            CHECK(finite && nm->nonFinite == static_cast<long>(t.size()) - 5 && y.back() == x[4]);
        }
        // Пример из нескольких стадий и среднего формулой; y[n-2], x[n-3], t, n
        const auto ex = um::Build(um::PresetExample(), pr);
        CHECK_MSG(ex != nullptr, "%s", pr.Text().c_str());
        um::Method rec;
        rec.consts = {{"k", "2"}};
        rec.stages = {{"0.5*(x + x[n-3]) + 0*t + 0*n", "x[0]"}, {"(x + y[n-1] + y[n-2]) / 3 + 0*k", "x[0]"}};
        rec.mean = um::MeanKind::Expr;
        rec.meanArg = "0.5*avgw(10) + 0.5*medw(10) + 0*(T1 - T0) + 0*last()";
        const auto rm = um::Build(rec, pr);
        CHECK_MSG(rm != nullptr, "%s", pr.Text().c_str());
        if (rm && ex)
        {
            filt::Params p;
            p.method = filt::Method::User;
            p.custom = rm;
            const auto pr2 = filt::ProcessSeries(t, x, p);
            const auto e = filt::EstimateSpan(t, x, pr2, 60.0, 110.0, p);
            CHECK_NEAR(e.level, 3.0, 0.15, "своё среднее на положении 2 (истина 3°)");
            p.custom = ex;
            const auto e2 = filt::EstimateSpan(t, x, filt::ProcessSeries(t, x, p), 60.0, 110.0, p);
            CHECK_NEAR(e2.level, 3.0, 0.15, "пример: медиана 5 и EMA дважды");
        }
    }
}

void TestSettings()
{
    SECTION("настройки: ось по умолчанию X, выбор запоминается; ось из файла программы 1.2 не берётся");
    const fs::path dir = fs::temp_directory_path() / L"krenomer_process_test_настройки";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path ini = dir / "krenomer.ini";
    PcSettings def;
    CHECK(def.heelAxis == 0);
    // Файл 1.2: heel_axis=1 без отметки версии — ось X
    WriteText(ini, "heel_d=1200\nheel_axis=1\n");
    PcSettings old;
    old.Load(ini);
    CHECK(old.heelAxis == 0 && old.heelD == 1200.0);
    // Выбор Y — сохраняется и читается
    PcSettings s;
    s.heelAxis = 1;
    s.heelPlateauS = 15;
    s.heelStep = 0.7;
    s.heelSmall = true;
    s.heelP = 0.004; // маленькое число — как введено
    s.digP = 4;
    s.fltMethod = 3;
    s.fltWhole = true;
    s.Save(ini);
    PcSettings back;
    back.Load(ini);
    CHECK(back.heelAxis == 1 && back.heelPlateauS == 15.0 && back.heelStep == 0.7);
    CHECK(back.heelSmall && back.heelP == 0.004 && back.digP == 4 && back.digArm == 3 && back.fltMethod == 3 && back.fltWhole);
    WriteText(ini, "heel_axis_v=2\ndig_arm=1\n"); // у плеч меньше 3 знаков нельзя
    PcSettings arm;
    arm.Load(ini);
    CHECK(arm.digArm == 3);
    CHECK(ReadBytes(ini).find("heel_axis_v=2") != std::string::npos);
}

void TestDeflate()
{
    SECTION("deflate: распаковка тестовым inflate (текст, повторы, случайные байты, длиннее окна)");
    std::mt19937 rng(11);
    std::vector<std::vector<unsigned char>> cases;
    cases.push_back({});
    cases.push_back({'a'});
    {
        std::string s;
        for (int i = 0; i < 20000; i++)
            s += "<c r=\"A" + std::to_string(i) + "\" s=\"5\"><v>" + std::to_string(i * 0.001) + "</v></c>";
        cases.emplace_back(s.begin(), s.end());
    }
    {
        std::vector<unsigned char> v(200000);
        for (auto& c : v)
            c = static_cast<unsigned char>(rng() % 256);
        cases.push_back(v);
    }
    {
        std::vector<unsigned char> v(300000, 7); // длинные повторы и расстояние 32768
        for (std::size_t i = 0; i < v.size(); i += 32768)
            v[i] = static_cast<unsigned char>(i / 32768);
        cases.push_back(v);
    }
    for (const auto& c : cases)
    {
        const auto z = deflate::Compress(c.data(), c.size());
        std::vector<unsigned char> back;
        CHECK_MSG(Inflate(z, back) && back == c, "%zu байт не распаковались обратно", c.size());
    }
    // PNG: IDAT распаковывается, длина = h·(1 + 3w), Adler-32 сходится
    const int w = 37, h = 11;
    std::vector<unsigned char> rgb(static_cast<std::size_t>(w) * h * 3);
    for (std::size_t i = 0; i < rgb.size(); i++)
        rgb[i] = static_cast<unsigned char>(i * 7 % 251);
    const auto file = png::EncodeRgb(w, h, rgb);
    CHECK(file.size() > 50 && std::memcmp(file.data(), "\x89PNG", 4) == 0);
    const std::size_t idat = 8 + 25; // подпись + IHDR
    const std::uint32_t len = (static_cast<std::uint32_t>(file[idat]) << 24) | (file[idat + 1] << 16) |
                              (file[idat + 2] << 8) | file[idat + 3];
    CHECK(std::memcmp(file.data() + idat + 4, "IDAT", 4) == 0);
    std::vector<unsigned char> z(file.begin() + idat + 8 + 2, file.begin() + idat + 8 + len - 4), raw;
    CHECK(Inflate(z, raw) && raw.size() == static_cast<std::size_t>(h) * (1 + 3 * w));
    const std::size_t a = idat + 8 + len - 4;
    const std::uint32_t adler = (static_cast<std::uint32_t>(file[a]) << 24) | (file[a + 1] << 16) | (file[a + 2] << 8) | file[a + 3];
    CHECK(adler == deflate::Adler32(raw.data(), raw.size()));
}

} // namespace

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    TestFormats();
    TestMath();
    TestGroupingAndReport();
    TestPcRuns();
    TestOldFormats();
    TestPlateaus();
    TestContinuous();
    TestRules();
    TestXlsxRead();
    TestRealRecord();
    TestFilters();
    TestUserMethod();
    TestSettings();
    TestDeflate();
    return TestSummary();
}
