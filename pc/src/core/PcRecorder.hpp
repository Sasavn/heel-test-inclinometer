#pragma once
// Запись на ПК: отсчёты датчиков (строки R команды samples, прошивка 1.5) — сразу в CSV на компьютере, по файлу на
// датчик, тем же форматом, что файлы на карте прибора (sd_logger.c, sd_format_row): шапка
// «Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms», разделитель «;», десятичная запятая, конец строки LF.
// Числа приходят от прибора уже целыми (сотые, тысячные, десятые — округлены как для карты), CalcX = RawX·10 −
// OffsetX считается в целых, как в прошивке, — «-0,000» не бывает. Date/Time — часы ПК в момент приёма строки,
// Ms — миллисекунды от первого отсчёта записи по часам прибора (t_ms, разность uint32 — переполнение не мешает).
//
// Имена: «ГГГГ-ММ-ДД_ЧЧ-ММ-СС[_метка]_PC_Dk.CSV» (время начала по ПК, k — адрес датчика). Существующие файлы не
// перезаписываются: если такие уже есть — « (2)», « (3)», … Файл датчика заводится с его первым отсчётом (датчик
// может появиться посреди записи). Строки копятся в памяти и раз в секунду дописываются в файл (fflush): при сбое
// программы теряется не больше ~1 с.
//
// Остановка: файлы закрываются; по желанию — книга Excel «…_PC.xlsx» (Xlsx.hpp): лист на датчик («Д2», «Д3»; те
// же столбцы, числа — числами с 2 / 3 / 1 знаками, Date и Time — текстом, шапка жирная) и лист «Сводка» (начало,
// конец, строки, пропуски, перерывы связи). Книга собирается из записанных CSV в отдельном потоке.
//
// Пропуски: номер n каждого датчика растёт на 1 с каждым отсчётом — скачок больше 1 значит потерянные отсчёты
// (прибор не успел отправить строку, перерыв связи). n или t_ms пошли назад — прибор перезагрузился: пропуски не
// считаются, Ms продолжается по часам ПК (столбец Ms не идёт назад).
//
// Все методы — из одного потока (интерфейс: App::Tick); поток сборки .xlsx работает только с копиями.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "Protocol.hpp"
#include "TextUtil.hpp"
#include "Xlsx.hpp"

namespace pcrec
{

inline constexpr const char* kHeader = "Date;Time;RawX;RawY;OffsetX;OffsetY;CalcX;CalcY;BatV;Ms\n";
inline constexpr std::size_t kXlsxMaxRows = 300000; // больше — .xlsx не собирается (память; данные — в CSV)
inline constexpr std::size_t kSheetMaxRows = 1048575; // строк данных на листе Excel (1 048 576 с шапкой)

// Часы ПК, мс с 1970 (метки строк Date/Time).
inline std::int64_t WallNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline std::tm LocalTm(std::int64_t wallMs)
{
    const std::time_t t = static_cast<std::time_t>(wallMs / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// Целое v / 10^dec с dec знаками после десятичной ЗАПЯТОЙ, как put_scaled() прошивки: (-46, 2) -> "-0,46",
// (0, 3) -> "0,000", (118, 1) -> "11,8", (105, 0) -> "105".
inline std::string Scaled(std::int64_t v, int dec)
{
    std::uint64_t scale = 1;
    for (int k = 0; k < dec; k++)
        scale *= 10;
    const std::uint64_t a = v < 0 ? 0ull - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
    char b[48];
    if (dec > 0)
        std::snprintf(b, sizeof(b), "%s%llu,%0*llu", v < 0 ? "-" : "", static_cast<unsigned long long>(a / scale), dec,
                      static_cast<unsigned long long>(a % scale));
    else
        std::snprintf(b, sizeof(b), "%s%llu", v < 0 ? "-" : "", static_cast<unsigned long long>(a));
    return b;
}

// Строка CSV одного отсчёта (с '\n'), как sd_format_row(): tm — дата и время (часы ПК), ms — от начала записи.
// "08.10.2026;16:25:31;-0,46;-0,17;-0,458;-0,171;-0,002;0,001;11,8;0"
inline std::string FormatRow(const std::tm& tm, const proto::RawSample& s, std::uint32_t ms)
{
    char d[40];
    std::snprintf(d, sizeof(d), "%02d.%02d.%04d;%02d:%02d:%02d;", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    const std::int64_t cx = static_cast<std::int64_t>(s.rawX) * 10 - s.offX;
    const std::int64_t cy = static_cast<std::int64_t>(s.rawY) * 10 - s.offY;
    std::string r = d;
    r.reserve(80);
    r += Scaled(s.rawX, 2);
    r += ';';
    r += Scaled(s.rawY, 2);
    r += ';';
    r += Scaled(s.offX, 3);
    r += ';';
    r += Scaled(s.offY, 3);
    r += ';';
    r += Scaled(cx, 3);
    r += ';';
    r += Scaled(cy, 3);
    r += ';';
    r += Scaled(s.batV, 1);
    r += ';';
    r += std::to_string(ms);
    r += '\n';
    return r;
}

// "-0,46" (dec = 2) -> -46. Ровно dec знаков после запятой (dec = 0 — без запятой); иначе false.
inline bool ParseScaled(std::string_view s, int dec, std::int64_t& v)
{
    bool neg = false;
    if (!s.empty() && s[0] == '-')
    {
        neg = true;
        s.remove_prefix(1);
    }
    const auto comma = s.find(',');
    std::string_view ip = s, fp;
    if (dec > 0)
    {
        if (comma == std::string_view::npos || s.size() - comma - 1 != static_cast<std::size_t>(dec))
            return false;
        ip = s.substr(0, comma);
        fp = s.substr(comma + 1);
    }
    else if (comma != std::string_view::npos)
        return false;
    if (!proto::AllDigits(ip) || (dec > 0 && !proto::AllDigits(fp)) || ip.size() > 12)
        return false;
    std::int64_t x = proto::ToInt(ip, 0);
    for (char c : fp)
        x = x * 10 + (c - '0');
    v = neg ? -x : x;
    return true;
}

// Строка CSV формата карты, разобранная обратно (книга .xlsx, тесты).
struct CsvRow
{
    std::string date, time;
    std::int64_t rawX = 0, rawY = 0, offX = 0, offY = 0, calcX = 0, calcY = 0, batV = 0, ms = 0;
};

inline bool ParseRow(std::string_view line, CsvRow& r)
{
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.remove_suffix(1);
    const auto v = proto::Split(line, ';');
    if (v.size() != 10)
        return false;
    CsvRow x;
    x.date = std::string(v[0]);
    x.time = std::string(v[1]);
    if (!ParseScaled(v[2], 2, x.rawX) || !ParseScaled(v[3], 2, x.rawY) || !ParseScaled(v[4], 3, x.offX) ||
        !ParseScaled(v[5], 3, x.offY) || !ParseScaled(v[6], 3, x.calcX) || !ParseScaled(v[7], 3, x.calcY) ||
        !ParseScaled(v[8], 1, x.batV) || !ParseScaled(v[9], 0, x.ms))
        return false;
    r = std::move(x);
    return true;
}

// Метка замера для имени файла: без \/:*?"<>| и управляющих знаков, пробелы по краям убраны, до 40 знаков.
inline std::string CleanLabel(const std::string& label)
{
    std::string s;
    for (const char ch : label)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F || std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos)
            continue;
        s += ch;
    }
    const auto a = s.find_first_not_of(" .");
    if (a == std::string::npos)
        return {};
    s = s.substr(a);
    while (!s.empty() && (s.back() == ' ' || s.back() == '.'))
        s.pop_back();
    // не длиннее 40 знаков (UTF-8: по границе символа)
    std::size_t chars = 0;
    for (std::size_t i = 0; i < s.size(); i++)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && ++chars > 40)
        {
            s.resize(i);
            break;
        }
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

// «2026-10-08_17-19-28» или «2026-10-08_17-19-28_опыт 3»
inline std::string BaseName(std::int64_t startWallMs, const std::string& label)
{
    const std::tm tm = LocalTm(startWallMs);
    char b[40];
    std::snprintf(b, sizeof(b), "%04d-%02d-%02d_%02d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
    const std::string l = CleanLabel(label);
    return l.empty() ? std::string(b) : std::string(b) + "_" + l;
}

inline std::string CsvName(const std::string& base, int addr)
{
    return base + "_PC_D" + std::to_string(addr) + ".CSV";
}

inline std::string XlsxName(const std::string& base)
{
    return base + "_PC.xlsx";
}

// Перерыв связи во время записи (часы ПК, мс); to = 0 — ещё идёт.
struct Gap
{
    std::int64_t from = 0, to = 0;
};

// Итог одного датчика (для листа «Сводка»).
struct SensorSummary
{
    int addr = 0;
    std::string fileName;
    std::uint64_t rows = 0, lost = 0;
};

inline std::string WallString(std::int64_t wallMs)
{
    const std::tm tm = LocalTm(wallMs);
    char b[32];
    std::snprintf(b, sizeof(b), "%02d.%02d.%04d %02d:%02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour,
                  tm.tm_min, tm.tm_sec);
    return b;
}

// Книга Excel из CSV записи: лист на датчик (если строк больше, чем помещается на лист, — «Д2 (2)», …) и «Сводка».
// csvs — (адрес, файл CSV). false — ошибка (текст в err).
inline bool WriteXlsx(const std::filesystem::path& file, const std::vector<std::pair<int, std::filesystem::path>>& csvs,
                      const std::vector<std::pair<std::string, xlsx::Cell>>& summary, std::string* err)
{
    try
    {
        std::vector<xlsx::Sheet> sheets;
        for (const auto& [addr, path] : csvs)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                if (err)
                    *err = "не удалось прочитать " + text::PathToUtf8(path.filename());
                return false;
            }
            std::string line;
            int part = 1;
            auto newSheet = [&] {
                sheets.emplace_back("Д" + std::to_string(addr) + (part > 1 ? " (" + std::to_string(part) + ")" : ""));
                auto& sh = sheets.back();
                sh.widths = {12, 10, 9, 9, 10, 10, 10, 10, 7, 10};
                sh.AddRow({xlsx::Text("Date"), xlsx::Text("Time"), xlsx::Text("RawX"), xlsx::Text("RawY"),
                           xlsx::Text("OffsetX"), xlsx::Text("OffsetY"), xlsx::Text("CalcX"), xlsx::Text("CalcY"),
                           xlsx::Text("BatV"), xlsx::Text("Ms")});
                part++;
            };
            newSheet();
            bool header = true;
            while (std::getline(in, line))
            {
                if (header)
                {
                    header = false;
                    if (line.rfind("Date;", 0) == 0)
                        continue;
                }
                CsvRow r;
                if (!ParseRow(line, r))
                    continue; // недописанная строка в конце (сбой) — пропустить
                if (sheets.back().rows.size() > kSheetMaxRows)
                    newSheet();
                sheets.back().AddRow({xlsx::Text(r.date), xlsx::Text(r.time), xlsx::Num(r.rawX / 100.0, 2),
                                      xlsx::Num(r.rawY / 100.0, 2), xlsx::Num(r.offX / 1000.0, 3),
                                      xlsx::Num(r.offY / 1000.0, 3), xlsx::Num(r.calcX / 1000.0, 3),
                                      xlsx::Num(r.calcY / 1000.0, 3), xlsx::Num(r.batV / 10.0, 1),
                                      xlsx::Num(static_cast<double>(r.ms), 0)});
            }
        }
        xlsx::Sheet sum("Сводка");
        sum.widths = {30, 44};
        sum.AddRow({xlsx::Text("Запись на ПК"), xlsx::Text("")});
        for (const auto& [k, v] : summary)
            sum.AddRow({xlsx::Text(k), v});
        sheets.push_back(std::move(sum));
        return xlsx::Write(file, sheets, err);
    }
    catch (const std::bad_alloc&)
    {
        if (err)
            *err = "не хватило памяти";
        return false;
    }
}

class Recorder
{
public:
    struct Options
    {
        std::filesystem::path dir;
        std::string label;  // метка замера в имени файла (может быть пустой)
        bool xlsx = false;  // по остановке собрать книгу .xlsx
        std::string device; // «прибор v1.5, COM5» — в «Сводку»
    };

    struct Sensor
    {
        int addr = 0;
        std::filesystem::path path;
        std::string fileName;    // UTF-8, только имя
        std::uint64_t rows = 0;
        std::uint64_t lost = 0;  // пропущено отсчётов (скачки n)
        double hz = 0.0;         // строк в секунду (последние 2 с)
        std::string error;       // не удалось создать / записать файл

        // служебное
        std::FILE* file = nullptr;
        std::string buf;
        bool haveN = false;
        std::uint32_t lastN = 0;
        std::deque<std::int64_t> times; // время связи (мс) последних строк — частота
    };

    Recorder() = default;
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    ~Recorder()
    {
        if (running_)
            Stop(WallNowMs(), lastLink_);
        if (worker_.joinable())
            worker_.join();
    }

    // Начать: папка создаётся; имена — от времени начала по часам ПК (wallMs); длительность — по времени связи
    // (linkMs: монотонное, не зависит от перевода часов). false — папку не создать (err).
    bool Start(const Options& o, std::int64_t wallMs, std::int64_t linkMs, std::string* err = nullptr)
    {
        if (running_ || Converting())
            return false;
        if (worker_.joinable())
            worker_.join();
        std::error_code ec;
        std::filesystem::create_directories(o.dir, ec);
        if (!std::filesystem::is_directory(o.dir, ec))
        {
            if (err)
                *err = "не удалось создать папку " + text::PathToUtf8(o.dir);
            return false;
        }
        opt_ = o;
        sens_.clear();
        gaps_.clear();
        base_ = UniqueBase(o.dir, BaseName(wallMs, o.label));
        startWall_ = wallMs;
        stopWall_ = 0;
        startLink_ = lastLink_ = stopLink_ = linkMs;
        lastFlush_ = -1;
        haveT0_ = false;
        reboots_ = 0;
        xlsxPath_.clear();
        xlsxError_.clear();
        xlsxOk_ = false;
        running_ = true;
        return true;
    }

    // Отсчёт: строка в буфер файла датчика (файл заводится с первым отсчётом). linkMs — время связи (частота).
    void Add(const proto::RawSample& s, std::int64_t wallMs, std::int64_t linkMs)
    {
        if (!running_)
            return;
        Sensor& d = SensorFor(s.addr);
        if (d.haveN)
        {
            const std::uint32_t step = s.n - d.lastN;
            if (step >= 1 && step < 0x80000000u)
                d.lost += step - 1; // n пошёл назад (перезагрузка) или повтор — пропуски не считаются
        }
        d.haveN = true;
        d.lastN = s.n;
        const std::uint32_t ms = MsOf(s, wallMs);
        if (!d.file && d.error.empty())
            Open(d);
        if (d.file && d.buf.size() < 8u * 1024 * 1024) // диск не принимает — не копить без конца
            d.buf += FormatRow(LocalTm(wallMs), s, ms);
        d.rows++;
        lastLink_ = std::max(lastLink_, linkMs);
        d.times.push_back(linkMs);
        while (d.times.size() > 400)
            d.times.pop_front();
    }

    // Раз в проход: раз в секунду строки — в файлы; частота по датчикам.
    void Tick(std::int64_t linkMs)
    {
        if (!running_)
            return;
        lastLink_ = std::max(lastLink_, linkMs);
        for (auto& d : sens_)
        {
            while (!d.times.empty() && linkMs - d.times.front() > 2000)
                d.times.pop_front();
            d.hz = static_cast<double>(d.times.size()) / 2.0;
        }
        if (lastFlush_ < 0)
            lastFlush_ = linkMs;
        if (linkMs - lastFlush_ >= 1000)
        {
            lastFlush_ = linkMs;
            Flush();
        }
    }

    // Связь с прибором пропала / вернулась (отсчёты снова идут) — перерыв в «Сводке».
    void LinkLost(std::int64_t wallMs)
    {
        if (running_ && (gaps_.empty() || gaps_.back().to != 0))
            gaps_.push_back({wallMs, 0});
    }
    void LinkBack(std::int64_t wallMs)
    {
        if (running_ && !gaps_.empty() && gaps_.back().to == 0)
            gaps_.back().to = wallMs;
    }

    // Остановить: дописать и закрыть файлы; книга .xlsx (если заказана) — в отдельном потоке (Converting()).
    void Stop(std::int64_t wallMs, std::int64_t linkMs)
    {
        if (!running_)
            return;
        LinkBack(wallMs);
        Flush();
        for (auto& d : sens_)
            if (d.file)
            {
                if (std::fclose(d.file) != 0 && d.error.empty())
                    d.error = "ошибка записи (диск заполнен?)";
                d.file = nullptr;
            }
        running_ = false;
        stopWall_ = wallMs;
        stopLink_ = std::max(lastLink_, linkMs);
        if (!opt_.xlsx || TotalRows() == 0)
            return;
        if (TotalRows() > kXlsxMaxRows)
        {
            xlsxError_ = "строк больше " + std::to_string(kXlsxMaxRows) + " — .xlsx не собран, данные — в CSV";
            return;
        }
        std::vector<std::pair<int, std::filesystem::path>> csvs;
        for (const auto& d : sens_)
            if (!d.path.empty())
                csvs.emplace_back(d.addr, d.path);
        std::sort(csvs.begin(), csvs.end());
        auto summary = Summary();
        const std::filesystem::path file = opt_.dir / text::PathFromUtf8(XlsxName(base_));
        xlsxPath_ = file;
        converting_ = true;
        worker_ = std::thread([this, file, csvs = std::move(csvs), summary = std::move(summary)] {
            std::string e;
            const bool ok = WriteXlsx(file, csvs, summary, &e);
            xlsxOk_ = ok;
            xlsxError_ = ok ? std::string() : e;
            converting_.store(false, std::memory_order_release);
        });
    }

    // Подождать сборку .xlsx (тесты, закрытие программы).
    void WaitConverted()
    {
        if (worker_.joinable())
            worker_.join();
    }

    bool Running() const { return running_; }
    bool Converting() const { return converting_.load(std::memory_order_acquire); }
    const std::vector<Sensor>& Sensors() const { return sens_; }
    const std::vector<Gap>& Gaps() const { return gaps_; }
    const std::string& Base() const { return base_; }
    const Options& Opts() const { return opt_; }
    std::int64_t StartWallMs() const { return startWall_; }
    std::int64_t StopWallMs() const { return stopWall_; }
    int Reboots() const { return reboots_; }
    // Итог книги (читать, когда !Converting())
    bool XlsxOk() const { return !Converting() && xlsxOk_; }
    std::string XlsxError() const { return Converting() ? std::string() : xlsxError_; }
    std::filesystem::path XlsxPath() const { return xlsxPath_; }
    // Ошибка записи файлов (первая)
    std::string Error() const
    {
        for (const auto& d : sens_)
            if (!d.error.empty())
                return d.fileName + ": " + d.error;
        return {};
    }

    std::uint64_t TotalRows() const
    {
        std::uint64_t n = 0;
        for (const auto& d : sens_)
            n += d.rows;
        return n;
    }
    std::uint64_t TotalLost() const
    {
        std::uint64_t n = 0;
        for (const auto& d : sens_)
            n += d.lost;
        return n;
    }
    // Длительность записи, мс, по времени связи (идёт — до linkNow)
    std::int64_t ElapsedMs(std::int64_t linkNow) const
    {
        return std::max<std::int64_t>(0, (running_ ? linkNow : stopLink_) - startLink_);
    }

private:
    static std::string Lower(std::string s)
    {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

    // Имя без совпадений с файлами в папке: «база», «база (2)», … (проверяются все «база_PC…»)
    static std::string UniqueBase(const std::filesystem::path& dir, const std::string& base)
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            names.push_back(Lower(text::PathToUtf8(it->path().filename())));
        for (int k = 1;; k++)
        {
            const std::string b = k == 1 ? base : base + " (" + std::to_string(k) + ")";
            const std::string prefix = Lower(b + "_PC");
            bool used = false;
            for (const auto& n : names)
                used = used || n.rfind(prefix, 0) == 0;
            if (!used)
                return b;
        }
    }

    Sensor& SensorFor(int addr)
    {
        for (auto& d : sens_)
            if (d.addr == addr)
                return d;
        Sensor d;
        d.addr = addr;
        sens_.push_back(std::move(d));
        std::sort(sens_.begin(), sens_.end(), [](const Sensor& a, const Sensor& b) { return a.addr < b.addr; });
        for (auto& x : sens_)
            if (x.addr == addr)
                return x;
        return sens_.back();
    }

    bool Open(Sensor& d)
    {
        std::error_code ec;
        std::string name = CsvName(base_, d.addr);
        std::filesystem::path p = opt_.dir / text::PathFromUtf8(name);
        for (int k = 2; std::filesystem::exists(p, ec) && k < 100; k++) // файл появился после начала записи
        {
            name = base_ + "_PC_D" + std::to_string(d.addr) + " (" + std::to_string(k) + ").CSV";
            p = opt_.dir / text::PathFromUtf8(name);
        }
#ifdef _WIN32
        d.file = _wfopen(p.c_str(), L"wbx");
#else
        d.file = std::fopen(p.string().c_str(), "wbx");
#endif
        d.fileName = name;
        if (!d.file)
        {
            d.error = "не удалось создать файл";
            return false;
        }
        d.path = p;
        d.buf = kHeader;
        return true;
    }

    void Flush()
    {
        for (auto& d : sens_)
        {
            if (!d.file || d.buf.empty())
                continue;
            static constexpr const char* kWriteErr = "ошибка записи (диск заполнен?)";
            const std::size_t w = std::fwrite(d.buf.data(), 1, d.buf.size(), d.file);
            d.buf.erase(0, w);
            if (!d.buf.empty() || std::fflush(d.file) != 0)
            {
                d.error = kWriteErr; // недописанное остаётся в буфере — повтор через секунду (до 8 МБ)
                continue;
            }
            if (d.error == kWriteErr)
                d.error.clear(); // место освободили — запись идёт дальше
        }
    }

    // Ms строки: от первого отсчёта записи по часам прибора; перезагрузка прибора (t_ms назад) — продолжить по ПК.
    std::uint32_t MsOf(const proto::RawSample& s, std::int64_t wallMs)
    {
        if (!haveT0_)
        {
            haveT0_ = true;
            t0_ = lastT_ = s.tMs;
            msBase_ = 0;
            lastMs_ = 0;
            lastWall_ = wallMs;
            return 0;
        }
        const auto step = static_cast<std::int32_t>(s.tMs - lastT_);
        if (step < -2000)
        {
            msBase_ = lastMs_ + std::max<std::int64_t>(0, wallMs - lastWall_);
            t0_ = s.tMs;
            reboots_++;
        }
        if (step > 0 || step < -2000)
            lastT_ = s.tMs;
        const std::uint32_t d = s.tMs - t0_;
        const std::int64_t ms = msBase_ + (d >= 0x80000000u ? 0 : static_cast<std::int64_t>(d)); // чуть раньше начала — 0
        lastMs_ = std::max(lastMs_, ms);
        lastWall_ = wallMs;
        return static_cast<std::uint32_t>(ms);
    }

    std::vector<std::pair<std::string, xlsx::Cell>> Summary() const
    {
        std::vector<std::pair<std::string, xlsx::Cell>> s;
        s.emplace_back("Начало (часы ПК)", xlsx::Text(WallString(startWall_)));
        s.emplace_back("Конец", xlsx::Text(WallString(stopWall_)));
        s.emplace_back("Длительность", xlsx::Text(text::Duration(static_cast<std::uint64_t>(ElapsedMs(stopLink_) / 1000))));
        if (!opt_.device.empty())
            s.emplace_back("Прибор", xlsx::Text(opt_.device));
        if (!CleanLabel(opt_.label).empty())
            s.emplace_back("Метка замера", xlsx::Text(CleanLabel(opt_.label)));
        for (const auto& d : sens_)
        {
            const std::string k = "Датчик Д" + std::to_string(d.addr);
            s.emplace_back(k + ": файл", xlsx::Text(d.fileName));
            s.emplace_back(k + ": строк", xlsx::Num(static_cast<double>(d.rows), 0));
            s.emplace_back(k + ": пропущено отсчётов", xlsx::Num(static_cast<double>(d.lost), 0));
        }
        std::int64_t gapMs = 0;
        for (const auto& g : gaps_)
            gapMs += std::max<std::int64_t>(0, (g.to ? g.to : stopWall_) - g.from);
        s.emplace_back("Перерывов связи", xlsx::Num(static_cast<double>(gaps_.size()), 0));
        if (!gaps_.empty())
            s.emplace_back("Перерывы связи, всего", xlsx::Text(text::Duration(static_cast<std::uint64_t>(gapMs / 1000))));
        for (std::size_t i = 0; i < gaps_.size(); i++)
            s.emplace_back("Перерыв " + std::to_string(i + 1),
                           xlsx::Text(WallString(gaps_[i].from) + " — " + WallString(gaps_[i].to ? gaps_[i].to : stopWall_)));
        if (reboots_)
            s.emplace_back("Перезагрузок прибора", xlsx::Num(reboots_, 0));
        s.emplace_back("Формат", xlsx::Text("как файлы на карте прибора; Date/Time — часы ПК, Ms — часы прибора"));
        return s;
    }

    Options opt_;
    std::vector<Sensor> sens_;
    std::vector<Gap> gaps_;
    std::string base_;
    bool running_ = false;
    std::int64_t startWall_ = 0, stopWall_ = 0;
    std::int64_t startLink_ = 0, stopLink_ = 0, lastLink_ = 0;
    std::int64_t lastFlush_ = -1;
    // Ms
    bool haveT0_ = false;
    std::uint32_t t0_ = 0, lastT_ = 0;
    std::int64_t msBase_ = 0, lastMs_ = 0, lastWall_ = 0;
    int reboots_ = 0;
    // .xlsx
    std::thread worker_;
    std::atomic<bool> converting_{false};
    bool xlsxOk_ = false;
    std::string xlsxError_;
    std::filesystem::path xlsxPath_;
};

} // namespace pcrec
