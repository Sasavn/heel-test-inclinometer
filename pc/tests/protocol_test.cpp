// Разбор протокола прибора: строки ответа, шапка и строки потока stream (по именам колонок, с новыми колонками в
// конце и без шапки), JSON status (образец — вывод host-теста прошивки tests/data/status_fw.json), ver, files,
// base64 / CRC-32 и приём файла get (G / D / E) в файл с проверкой.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestUtil.hpp"
#include "comm/LineSplitter.hpp"
#include "core/Codec.hpp"
#include "core/Download.hpp"
#include "core/Json.hpp"
#include "core/Protocol.hpp"
#include "core/TextUtil.hpp"

namespace fs = std::filesystem;

static std::string ReadText(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void TestLines()
{
    SECTION("строки ответа: OK / ERR / DFU / поток");
    CHECK(proto::IsTerminator("OK"));
    CHECK(proto::IsTerminator("OK 12"));
    CHECK(proto::IsTerminator("OK freq=10 Hz (flash save pending)"));
    CHECK(proto::IsTerminator("ERR unknown command 'x' (try: help)"));
    CHECK(proto::IsTerminator("ERR"));
    CHECK(proto::IsTerminator("DFU..."));
    CHECK(proto::IsTerminator("RESET..."));
    CHECK(proto::IsOkLine("DFU..."));
    CHECK(!proto::IsOkLine("ERR busy"));
    CHECK(!proto::IsTerminator("OKAY"));
    CHECK(!proto::IsTerminator("ERRORS"));
    CHECK(!proto::IsTerminator("sensor D2 addr=2 OK x=1.000"));
    CHECK(!proto::IsTerminator("{\"v\":1}"));
    CHECK(!proto::IsTerminator("F,OK.CSV,1,2026-10-07 10:00"));
    CHECK(proto::IsStreamLine("S,123,4"));
    CHECK(proto::IsStreamHeader("# S,t_ms,loop_ms"));
    CHECK(!proto::IsStreamLine("# S,t_ms"));

    // Строки режутся USB по 64 байта, \r\n
    std::vector<std::string> got;
    LineSplitter sp([&](std::string l) { got.push_back(std::move(l)); });
    const std::string data = "OK stream every 100 ms\r\nS,1,2,O,0.1\r\nS,2,2,O,0.2\r\nERR x\r\n";
    for (std::size_t i = 0; i < data.size(); i += 7)
        sp.Feed(data.data() + i, std::min<std::size_t>(7, data.size() - i));
    CHECK(got.size() == 4);
    CHECK(got.size() == 4 && got[0] == "OK stream every 100 ms" && got[1] == "S,1,2,O,0.1" && got[3] == "ERR x");
    sp.Feed("partial", 7);
    CHECK(got.size() == 4);
    sp.Reset();
    sp.Feed("\r\nnext\n", 7);
    CHECK(got.size() == 5 && got.back() == "next");
}

static void TestStream()
{
    SECTION("поток stream: шапка, колонки по именам");
    proto::StreamHeader h;
    CHECK(proto::ParseStreamHeader("# S,t_ms,loop_ms,s2,x2,y2,err2,s3,x3,y3,err3,sd,rate_hz,file,gap_ms,cpu,bat_v,"
                                   "bat_low,ok2,to2,bad2,lat2,lmin2,lavg2,lmax2,ok3,to3,bad3,lat3,lmin3,lavg3,lmax3,cyc_us,"
                                   "idle_us",
                                   h));
    CHECK(h.addrs.size() == 2 && h.addrs[0] == 2 && h.addrs[1] == 3);
    CHECK(h.Index("t_ms") == 0 && h.Index("idle_us") == 32);
    proto::StreamSample s;
    // Строка в формате stream_emit() прошивки
    CHECK(proto::ParseStreamLine("S,123456,3,O,1.234,-0.567,0,L,-12.500,0.000,7,REC,9.9,7,15,12,12.10,0,1234,2,1,3012,"
                                 "2901,3105,3398,0,0,0,0,0,0,0,23153,914",
                                 h, s));
    CHECK(s.tMs == 123456 && s.loopMs == 3);
    CHECK(s.sens.size() == 2);
    const auto* d2 = s.Sensor(2);
    const auto* d3 = s.Sensor(3);
    CHECK(d2 && d2->st == 'O');
    CHECK(d3 && d3->st == 'L');
    if (d2 && d3)
    {
        CHECK_NEAR(d2->x, 1.234, 1e-9, "x2");
        CHECK_NEAR(d2->y, -0.567, 1e-9, "y2");
        CHECK_NEAR(d3->x, -12.5, 1e-9, "x3");
        CHECK(d3->err == 7);
        CHECK(d2->ok == 1234 && d2->to == 2 && d2->bad == 1 && d2->lat == 3012 && d2->lmin == 2901 && d2->lavg == 3105 &&
              d2->lmax == 3398);
    }
    CHECK(s.sd == "REC" && s.file == 7 && s.gapMs == 15 && s.cpu == 12 && s.batLow == 0);
    CHECK_NEAR(s.rateHz, 9.9, 1e-9, "rate");
    CHECK_NEAR(s.batV, 12.10, 1e-9, "bat_v");
    CHECK(s.cycUs == 23153 && s.idleUs == 914);

    // Новая прошивка: колонки переставлены и добавлены в конец — разбор по именам
    proto::StreamHeader h2;
    CHECK(proto::ParseStreamHeader("# S,t_ms,loop_ms,s2,x2,y2,err2,s3,x3,y3,err3,sd,rate_hz,file,gap_ms,cpu,bat_v,bat_low,"
                                   "ok2,to2,bad2,lat2,lmin2,lavg2,lmax2,ok3,to3,bad3,lat3,lmin3,lavg3,lmax3,cyc_us,idle_us,"
                                   "roll2,temp",
                                   h2));
    CHECK(proto::ParseStreamLine("S,5,1,O,0.100,0.200,0,O,0.300,0.400,0,READY,10.0,8,15,9,11.90,0,1,0,0,1,1,1,1,1,0,0,1,1,1,"
                                 "1,22000,800,0.55,36.6",
                                 h2, s));
    CHECK(s.Sensor(3) && s.Sensor(3)->y == 0.4);
    CHECK(s.idleUs == 800);
    // Старая прошивка: короче (без шины) — недостающее по умолчанию
    proto::StreamHeader h3;
    CHECK(proto::ParseStreamHeader("# S,t_ms,loop_ms,s2,x2,y2,err2,s3,x3,y3,err3,sd,rate_hz,file,gap_ms", h3));
    CHECK(proto::ParseStreamLine("S,5,1,O,nan,0.200,0,A,0.000,0.000,0,NOCARD,0.0,0,15", h3, s));
    CHECK(s.Sensor(2) && std::isnan(s.Sensor(2)->x));
    CHECK(s.Sensor(3) && s.Sensor(3)->st == 'A');
    CHECK(s.cpu == -1 && std::isnan(s.batV) && s.Sensor(2)->ok == -1);
    // Без шапки — шапка прошивки 1.3
    CHECK(proto::DefaultStreamHeader().Valid() && proto::DefaultStreamHeader().addrs.size() == 2);
    CHECK(!proto::ParseStreamLine("X,1", h, s));
    CHECK(!proto::ParseStreamLine("S,abc", h, s));
}

static void TestJson()
{
    SECTION("JSON: разборщик");
    json::Value v;
    std::string err;
    const std::string doc = "{\"a\":1,\"b\":[1,2.5,-3e2],\"c\":{\"d\":\"x\\\"y\\\\z\\u0041\"},\"e\":true,\"f\":null,\"g\":false}";
    CHECK(json::Parse(doc, v, &err));
    CHECK(v["a"].Int() == 1);
    CHECK(v["b"].Size() == 3 && v["b"][2].Num() == -300.0);
    CHECK(v["c"]["d"].Str() == "x\"y\\zA");
    CHECK(v["e"].Bool() && v["f"].IsNull() && !v["g"].Bool(true));
    CHECK(v["нет"].IsNull() && v["b"][7].IsNull());
    CHECK(json::Parse("  [ ]  ", v) && v.IsArray() && v.Size() == 0);
    CHECK(!json::Parse("{\"a\":1,}", v, &err));
    CHECK(!json::Parse("{\"a\":1", v, &err));
    CHECK(!json::Parse("{\"a\":1} x", v, &err));
    CHECK(!json::Parse("{a:1}", v, &err));
    CHECK(!json::Parse("", v, &err));
    CHECK(json::Parse("{\"s\":\"кириллица\"}", v) && v["s"].Str() == "кириллица");
}

static void TestStatus()
{
    SECTION("status: JSON прошивки (образец host-теста)");
    const std::string path = testutil::TestDataPath("status_fw.json");
    CHECK_MSG(!path.empty(), "нет tests/data/status_fw.json");
    std::string line = ReadText(path);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    proto::DeviceStatus s;
    std::string err;
    CHECK_MSG(proto::ParseStatus(line, s, &err), "%s", err.c_str());
    CHECK(s.v == 1 && s.fw == "1.3");
    CHECK(s.upS == 1000 && s.ms == 1000000);
    CHECK(s.time == "2026-10-07 20:41:05" && s.rtc);
    CHECK(s.freq == 10 && s.gap == 15 && s.rollWin == 20 && s.rollHz == 5);
    CHECK_NEAR(s.alpha, 0.15, 1e-9, "alpha");
    CHECK_NEAR(s.batAlarm, 10.0, 1e-9, "batalarm");
    CHECK_NEAR(s.rollCalm, 1.5, 1e-9, "rollcalm");
    CHECK_NEAR(s.rollHyst, 0.2, 1e-9, "rollhyst");
    CHECK(s.theme == "light" && s.savePending);
    CHECK_NEAR(s.rate, 10.0, 1e-9, "rate");
    CHECK(s.cpu == 12 && s.loops == 2400 && s.loopMax == 5);
    CHECK(s.sens.size() == 2);
    const auto* d2 = s.Sensor(2);
    const auto* d3 = s.Sensor(3);
    CHECK(d2 && d3);
    if (d2 && d3)
    {
        CHECK(d2->Ok() && !d3->Ok() && d3->st == "LOST");
        CHECK_NEAR(d2->x, 1.234, 1e-9, "x2");
        CHECK_NEAR(d2->y, -0.567, 1e-9, "y2");
        CHECK_NEAR(d2->ox, 0.12, 1e-9, "ox2");
        CHECK_NEAR(d2->rollX, 0.123, 1e-9, "roll_x2");
        CHECK(d2->calmX && !d2->calmY && d2->fill == 20);
        CHECK(d2->ok == 1234 && d2->err == 3 && d2->lastErr == "TIMEOUT");
        CHECK(d2->ageMs && *d2->ageMs == 10);
        CHECK(d2->to == 2 && d2->crc == 1 && d2->bad == 0 && d2->tmoUs == 30000);
        CHECK(d2->lat.n == 4 && d2->lat.last == 1500 && d2->lat.min == 1400 && d2->lat.avg == 1550 && d2->lat.max == 1700);
        CHECK(d2->done.avg == 2450);
        CHECK(!d3->ageMs && d3->lastErr == "-" && d3->calmY);
    }
    CHECK(!s.stable && s.stabAddr && *s.stabAddr == 3 && s.stabAxis == "X" && s.stabFill == 12);
    CHECK_NEAR(s.span, 2.5, 1e-9, "span");
    CHECK(s.busCyc.n == 10 && s.busCyc.avg == 22000 && s.busGap.max == 15300 && s.busIdle.min == 60000);
    CHECK_NEAR(s.maxRate, 45.45, 1e-9, "max_rate");
    CHECK(s.sd == "READY" && s.file == 7 && s.names.size() == 2 && s.names[0] == "2026-10-07_M007_D2.CSV");
    CHECK(!s.sdFreeMb && s.sdTotalMb == 7600);
    CHECK_NEAR(s.batV, 12.10, 1e-9, "bat_v");
    CHECK(s.batPresent && !s.batLow && !s.batPct);
    CHECK(!s.recSw && s.svc == "IDLE" && s.xfer == "idle" && !s.Recording());

    // Неизвестные поля новой прошивки — не мешают; без v / sens — не status
    const std::string future = R"({"v":2,"sens":[],"new_field":[1,2,{"x":1}],"sd":"RECORDING"})";
    CHECK(proto::ParseStatus(future, s));
    CHECK(s.v == 2 && s.Recording() && s.sens.empty());
    const std::string noV = R"({"sens":[]})";
    CHECK(!proto::ParseStatus(noV, s));
    CHECK(!proto::ParseStatus("OK", s));
}

static void TestVerFiles()
{
    SECTION("ver и files");
    auto v = proto::ParseVer({"BWM427 inclinometer firmware v1.3, build Oct  7 2026 20:46:12",
                              "HAL 1.8.1, SYSCLK 96 MHz, UID 0034003A3133510B37363834"});
    CHECK(v.ours && v.version == "1.3" && v.build == "Oct  7 2026 20:46:12");
    CHECK(v.details.rfind("HAL 1.8.1", 0) == 0);
    v = proto::ParseVer({"BWM427 inclinometer firmware, build Oct  6 2026 15:21:00"}); // до version.h
    CHECK(v.ours && v.version.empty() && v.build == "Oct  6 2026 15:21:00");
    v = proto::ParseVer({"Arduino Uno"});
    CHECK(!v.ours);

    proto::FileEntry f;
    CHECK(proto::ParseFileLine("F,2026-10-06_M007_D2.CSV,123456,2026-10-06 14:36", f));
    CHECK(f.name == "2026-10-06_M007_D2.CSV" && f.size == 123456 && f.date == "2026-10-06 14:36");
    CHECK(f.measurement == 7 && f.sensorAddr == 2 && f.day == "2026-10-06");
    CHECK(proto::ParseFileLine("F,M_005_3.csv,10,2026-01-01 00:00", f));
    CHECK(f.measurement == 5 && f.sensorAddr == 3);
    CHECK(proto::ParseFileLine("F,my,file.csv,10,2026-01-01 00:00", f)); // запятая в имени
    CHECK(f.name == "my,file.csv" && f.size == 10 && f.measurement == -1);
    CHECK(proto::ParseFileLine("F,2026-10-06_M1234_D3.CSV,1,2026-10-06 14:36", f) && f.measurement == 1234);
    CHECK(!proto::ParseFileLine("F,x,abc,2026", f));
    CHECK(!proto::ParseFileLine("G,x,1,0", f));
}

static void TestCodec()
{
    SECTION("base64 и CRC-32");
    std::vector<std::uint8_t> out;
    CHECK(codec::Base64Decode("Zm9vYmFy", out) && std::string(out.begin(), out.end()) == "foobar");
    out.clear();
    CHECK(codec::Base64Decode("Zm9vYg==", out) && std::string(out.begin(), out.end()) == "foob");
    out.clear();
    CHECK(codec::Base64Decode("Zm8=", out) && std::string(out.begin(), out.end()) == "fo");
    out.clear();
    CHECK(codec::Base64Decode("", out) && out.empty());
    CHECK(!codec::Base64Decode("Zm9", out));
    CHECK(!codec::Base64Decode("Zm=v", out));
    CHECK(!codec::Base64Decode("Zg==Zg==", out));
    CHECK(!codec::Base64Decode("Z!9v", out));
    std::vector<std::uint8_t> all(256);
    for (int i = 0; i < 256; i++)
        all[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    for (std::size_t n = 0; n <= 49; n++)
    {
        out.clear();
        const std::string e = codec::Base64Encode(all.data() + 100, n);
        CHECK(codec::Base64Decode(e, out) && out.size() == n && std::equal(out.begin(), out.end(), all.begin() + 100));
    }
    const char* s = "123456789";
    CHECK(codec::Crc32(0, reinterpret_cast<const std::uint8_t*>(s), 9) == 0xCBF43926u);
    const auto whole = codec::Crc32(0, all.data(), 256);
    auto part = codec::Crc32(0, all.data(), 100);
    part = codec::Crc32(part, all.data() + 100, 156);
    CHECK(whole == part);
}

static void TestDownload()
{
    SECTION("приём файла get: G / D / E -> файл, CRC");
    const fs::path dir = fs::temp_directory_path() / L"krenomer_test_dl";
    std::error_code ec;
    fs::remove_all(dir, ec);
    // Данные с нулями и всеми байтами, 1000 байт — строки D по 48 байт, как в прошивке
    std::vector<std::uint8_t> data(1000);
    for (std::size_t i = 0; i < data.size(); i++)
        data[i] = static_cast<std::uint8_t>((i * 131 + 7) & 0xFF);
    auto feed = [&](DownloadSink& sink, std::size_t corruptAt, bool withE, std::uint32_t crcOverride) {
        sink.OnLine("G,a.csv,1000,0");
        for (std::size_t i = 0; i < data.size(); i += 48)
        {
            std::vector<std::uint8_t> chunk(data.begin() + static_cast<std::ptrdiff_t>(i),
                                            data.begin() + static_cast<std::ptrdiff_t>(std::min(data.size(), i + 48)));
            if (corruptAt >= i && corruptAt < i + chunk.size())
                chunk[corruptAt - i] ^= 0x40;
            sink.OnLine("D," + codec::Base64Encode(chunk.data(), chunk.size()));
        }
        if (withE)
        {
            char e[64];
            std::snprintf(e, sizeof(e), "E,1000,%08x",
                          crcOverride ? crcOverride : codec::Crc32(0, data.data(), data.size()));
            sink.OnLine(e);
        }
    };
    // Успех
    {
        DownloadSink sink(dir / L"a.csv", 1000);
        feed(sink, SIZE_MAX, true, 0);
        CHECK(sink.Received() == 1000 && sink.Total() == 1000);
        CHECK(sink.GetResult() == DownloadSink::Result::Running);
        sink.OnFinish(true, "OK");
        CHECK_MSG(sink.GetResult() == DownloadSink::Result::Ok, "%s", sink.Message().c_str());
        std::ifstream in(dir / L"a.csv", std::ios::binary);
        std::vector<std::uint8_t> back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(back == data);
        CHECK(!fs::exists(dir / L"a.csv.part", ec));
    }
    // Повреждённый байт — CRC не сошёлся, .part удалён, прежний файл цел
    {
        DownloadSink sink(dir / L"a.csv", 1000);
        feed(sink, 500, true, 0);
        sink.OnFinish(true, "OK");
        CHECK(sink.GetResult() == DownloadSink::Result::CrcMismatch);
        CHECK(!fs::exists(dir / L"a.csv.part", ec));
        CHECK(fs::file_size(dir / L"a.csv", ec) == 1000);
    }
    // Нет строки E
    {
        DownloadSink sink(dir / L"b.csv", 1000);
        feed(sink, SIZE_MAX, false, 0);
        sink.OnFinish(true, "OK");
        CHECK(sink.GetResult() == DownloadSink::Result::BadData);
        CHECK(!fs::exists(dir / L"b.csv", ec) && !fs::exists(dir / L"b.csv.part", ec));
    }
    // Прибор прервал (запись началась)
    {
        DownloadSink sink(dir / L"c.csv", 1000);
        sink.OnLine("G,c.csv,1000,0");
        sink.OnLine("D,AAAA");
        sink.OnFinish(false, "ERR aborted: recording started");
        CHECK(sink.GetResult() == DownloadSink::Result::DeviceError);
        CHECK(sink.Message() == "ERR aborted: recording started");
        CHECK(!fs::exists(dir / L"c.csv.part", ec));
    }
    // Мусор вместо base64, данные без G
    {
        DownloadSink sink(dir / L"d.csv", 10);
        sink.OnLine("G,d.csv,10,0");
        sink.OnLine("D,@@@@");
        CHECK(sink.GetResult() == DownloadSink::Result::BadData);
        DownloadSink s2(dir / L"e.csv", 10);
        s2.OnLine("D,AAAA");
        CHECK(s2.GetResult() == DownloadSink::Result::BadData);
    }
    // Кириллица в пути
    {
        DownloadSink sink(dir / L"Замеры" / L"f.csv", 3);
        sink.OnLine("G,f.csv,3,0");
        sink.OnLine("D,Zm9v");
        sink.OnLine("E,3,8c736521");
        sink.OnFinish(true, "OK");
        CHECK_MSG(sink.GetResult() == DownloadSink::Result::Ok, "%s", sink.Message().c_str());
        CHECK(fs::file_size(dir / L"Замеры" / L"f.csv", ec) == 3);
    }
    fs::remove_all(dir, ec);
}

static void TestText()
{
    SECTION("текст: размеры, длительности, углы, склонения");
    CHECK(text::Bytes(950) == "950 Б");
    CHECK(text::Bytes(12600) == "12,3 КБ");
    CHECK(text::MegaBytes(7580) == "7,4 ГБ");
    CHECK(text::Duration(45) == "45 с");
    CHECK(text::Duration(723) == "12 мин 03 с");
    CHECK(text::Duration(7500) == "2 ч 05 мин");
    CHECK(text::Angle(1.2344) == "+1.234°");
    CHECK(text::Angle(-0.5675) == "\xE2\x88\x92" "0.568°" || text::Angle(-0.5675) == "\xE2\x88\x92" "0.567°");
    CHECK(text::Angle(-0.0001) == "+0.000°");
    CHECK(text::Angle(proto::kNaN) == "—");
    CHECK(std::string(text::Plural(1, "файл", "файла", "файлов")) == "файл");
    CHECK(std::string(text::Plural(3, "файл", "файла", "файлов")) == "файла");
    CHECK(std::string(text::Plural(12, "файл", "файла", "файлов")) == "файлов");
    CHECK(std::string(text::Plural(21, "файл", "файла", "файлов")) == "файл");
    CHECK(proto::Fixed(0.15, 2) == "0.15" && proto::Fixed(10, 1) == "10.0");
    CHECK(proto::InRange(50, proto::limits::kFreq) && !proto::InRange(51, proto::limits::kFreq));
    CHECK(proto::InRange(0.10, proto::limits::kRollCalm) && !proto::InRange(0.09, proto::limits::kRollCalm));
}

int main(int argc, char** argv)
{
    testutil::Init(argc, argv);
    TestLines();
    TestStream();
    TestJson();
    TestStatus();
    TestVerFiles();
    TestCodec();
    TestDownload();
    TestText();
    return TestSummary();
}
