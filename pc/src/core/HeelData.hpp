#pragma once
// Данные опыта кренования для «Обработки»: файлы замеров в папке -> замеры (по номеру из имени) и посты (по датчику)
// -> установившийся крен каждого файла (среднее за последние N с, СКО, число точек) -> точки для heel::Solve
// (Heel.hpp) -> h по замерам и постам, итог.
//
// Имена файлов: «ГГГГ-ММ-ДД_MNNN_Dk.CSV» (прошивка 1.4+, k — Modbus-адрес датчика), старые «M_NNN_k.CSV» (k — 1 нос,
// 2 корма у исходной прошивки) и записи на ПК «ГГГГ-ММ-ДД_ЧЧ-ММ-СС[_метка]_PC_Dk.CSV» (одна запись — один замер;
// номера 1001, 1002, … по времени начала, в таблице — время и метка). Пост датчика по умолчанию: Д2 — нос, Д3 —
// корма; старые 1 — нос, 2 — корма (меняется в интерфейсе).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "CsvLog.hpp"
#include "Heel.hpp"
#include "Protocol.hpp"
#include "TextUtil.hpp"

namespace heel
{

inline constexpr int kBow = 0, kStern = 1; // посты
inline constexpr const char* kPostName[2] = {"Нос", "Корма"};

// Исходные данные обработки (значения по умолчанию — как в BWM427_Analyzer, ось — X: датчики ставят поперёк судна).
struct Settings
{
    double D = 1500.0;        // водоизмещение, т
    double P = 15.0;          // масса кренящего груза, т
    double L = 4.5;           // плечо переноса по умолчанию, м
    double windowS = 30.0;    // крен — среднее за последние N с файла
    double thresholdDeg = 0.1; // |θ| не больше — замер на этом посту не учитывается
    int axis = 0;             // 0 — CalcX, 1 — CalcY
};

// Файл замера.
struct File
{
    std::string name;            // имя файла
    std::filesystem::path path;
    int number = -1;             // номер замера из имени; -1 — имя не по образцу (или запись на ПК)
    int sensor = -1;             // ключ датчика (SensorKey)
    std::string pcRun;           // запись на ПК: «ГГГГ-ММ-ДД_ЧЧ-ММ-СС[_метка]» (номер дают по порядку)
    csvlog::Log log;

    bool Measurement() const { return number >= 0 || !pcRun.empty(); }
};

inline constexpr int kPcRunBase = 1000; // номера замеров для записей на ПК: 1001, 1002, …

// «2026-10-08_17-19-28_опыт 3» -> «17:19:28 опыт 3»
inline std::string PcRunLabel(const std::string& run)
{
    if (run.size() >= 19 && run[10] == '_' && run[13] == '-' && run[16] == '-')
    {
        std::string s = run.substr(11, 2) + ":" + run.substr(14, 2) + ":" + run.substr(17, 2);
        if (run.size() > 20 && run[19] == '_')
            s += " " + run.substr(20);
        return s;
    }
    return run;
}

// Ключ датчика: адрес из нового имени (…_Dk) или 100 + k для старого (M_NNN_k) — у них разная нумерация постов.
inline int SensorKey(int addr, bool oldName)
{
    return oldName ? 100 + addr : addr;
}

inline std::string SensorName(int key)
{
    return key >= 100 ? "датчик " + std::to_string(key - 100) + " (M_NNN_" + std::to_string(key - 100) + ")"
                      : "Д" + std::to_string(key);
}

// Пост по умолчанию: 0 — нос, 1 — корма, -1 — не учитывать.
inline int DefaultPost(int key)
{
    switch (key)
    {
    case 2: return kBow;
    case 3: return kStern;
    case 101: return kBow;
    case 102: return kStern;
    default: return -1;
    }
}

// Файлы *.CSV в папке (без подпапок), по имени.
inline std::vector<std::filesystem::path> ListCsv(const std::filesystem::path& dir, std::string* err = nullptr)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
        if (err)
            *err = "папки нет";
        return out;
    }
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        std::string ext = text::PathToUtf8(it->path().extension());
        for (auto& c : ext)
            c = static_cast<char>((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
        if (ext == ".CSV")
            out.push_back(it->path());
    }
    if (ec && err)
        *err = "не удалось прочитать папку";
    std::sort(out.begin(), out.end());
    return out;
}

inline File LoadFile(const std::filesystem::path& p)
{
    File f;
    f.path = p;
    f.name = text::PathToUtf8(p.filename());
    proto::FileEntry e;
    e.name = f.name;
    proto::ParseMeasurementName(e);
    f.number = e.measurement;
    const bool oldName = f.name.size() > 2 && (f.name[0] == 'M' || f.name[0] == 'm') && f.name[1] == '_';
    f.sensor = e.sensorAddr >= 0 ? SensorKey(e.sensorAddr, oldName) : -1;
    if (f.number < 0 && f.name.size() > 4)
    {
        // Запись на ПК: «…_PC_Dk.CSV»
        std::string up;
        for (const char c : f.name)
            up += static_cast<char>((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
        const std::size_t pc = up.rfind("_PC_D");
        const std::size_t end = up.size() - 4;
        if (up.compare(end, 4, ".CSV") == 0 && pc != std::string::npos && pc > 0 && pc + 5 < end &&
            proto::AllDigits(std::string_view(up).substr(pc + 5, end - pc - 5)))
        {
            f.pcRun = f.name.substr(0, pc);
            f.sensor = SensorKey(static_cast<int>(proto::ToInt(std::string_view(up).substr(pc + 5, end - pc - 5))), false);
        }
    }
    if (f.Measurement())
        csvlog::ReadFile(p, f.log);
    return f;
}

struct Loaded
{
    std::vector<File> files;
    std::string folder; // куда сохранять отчёт
};

// Прочитать файлы (поток интерфейса не ждёт: вызывается из std::async). done — сколько прочитано.
inline Loaded LoadAll(const std::vector<std::filesystem::path>& paths, const std::string& folder,
                      std::atomic<int>* done = nullptr)
{
    Loaded r;
    r.folder = folder;
    for (const auto& p : paths)
    {
        r.files.push_back(LoadFile(p));
        if (done)
            done->fetch_add(1);
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// Установившийся крен файла
// ---------------------------------------------------------------------------------------------------------------

struct Stats
{
    double mean = kNaN, sd = kNaN; // °
    int n = 0;                     // точек в окне
    double from = kNaN, to = kNaN; // окно, с от начала записи
    bool whole = false;            // файл короче окна — взят целиком
};

// Среднее и СКО угла по оси axis за последние windowS с файла (от времени последней строки).
inline Stats TailStats(const csvlog::Log& log, int axis, double windowS)
{
    Stats s;
    if (log.rows.empty())
        return s;
    const double tEnd = log.rows.back().t;
    const double from = tEnd - windowS;
    s.to = tEnd;
    s.from = std::max(from, log.rows.front().t);
    s.whole = log.rows.front().t >= from;
    double sum = 0;
    for (auto it = log.rows.rbegin(); it != log.rows.rend() && it->t >= from; ++it)
    {
        const double v = it->Calc(axis);
        if (std::isfinite(v))
        {
            sum += v;
            s.n++;
        }
    }
    if (s.n == 0)
        return s;
    s.mean = sum / s.n;
    if (s.n >= 2)
    {
        double v2 = 0;
        for (auto it = log.rows.rbegin(); it != log.rows.rend() && it->t >= from; ++it)
        {
            const double v = it->Calc(axis);
            if (std::isfinite(v))
                v2 += (v - s.mean) * (v - s.mean);
        }
        s.sd = std::sqrt(v2 / (s.n - 1));
    }
    return s;
}

// Последнее напряжение АКБ в файле (как «АКБ (В)» в отчёте старой программы).
inline double LastBattery(const csvlog::Log& log)
{
    for (auto it = log.rows.rbegin(); it != log.rows.rend(); ++it)
        if (std::isfinite(it->batV))
            return it->batV;
    return kNaN;
}

// ---------------------------------------------------------------------------------------------------------------
// Замеры
// ---------------------------------------------------------------------------------------------------------------

struct PostData
{
    int file = -1;         // индекс в files; -1 — файла на этом посту нет
    Stats st;
    double batV = kNaN;
    PointResult res;       // h, учтён ли, почему нет
};

struct Measurement
{
    int number = 0;
    std::string label;     // «19»; у записи на ПК — «17:19:28 метка»
    bool use = true;       // в расчёте (флажок в таблице)
    double arm = kNaN;     // |l|, м (задаёт интерфейс: своё или L)
    PostData post[2];      // нос, корма
};

struct Summary
{
    double h = kNaN, sd = kNaN; // среднее h и СКО, м
    int n = 0;                  // учтённых точек (замер × пост)
    int measurements = 0;       // замеров в расчёте
    Fit ls;                     // МНК
};

// Замеры по номеру; файлы — по постам (postOf: ключ датчика -> пост, нет ключа — DefaultPost). notes — что не
// вошло и почему (для журнала).
inline std::vector<Measurement> Group(const std::vector<File>& files, const std::map<int, int>& postOf,
                                      std::vector<std::string>* notes = nullptr)
{
    std::map<int, Measurement> byNum;
    auto note = [&](const std::string& s) {
        if (notes)
            notes->push_back(s);
    };
    std::map<std::string, int> pcNum; // записи на ПК по порядку имён (время начала)
    for (const File& f : files)
        if (!f.pcRun.empty())
            pcNum[f.pcRun] = 0;
    int k = kPcRunBase;
    for (auto& [run, n] : pcNum)
        n = ++k;
    for (std::size_t i = 0; i < files.size(); i++)
    {
        const File& f = files[i];
        if (!f.Measurement())
        {
            note(f.name + ": имя не по образцу …_MNNN_Dk.CSV / M_NNN_k.CSV / …_PC_Dk.CSV — пропущен");
            continue;
        }
        const int number = f.pcRun.empty() ? f.number : pcNum[f.pcRun];
        if (!f.log.error.empty())
        {
            note(f.name + ": " + f.log.error + " — пропущен");
            continue;
        }
        const auto it = postOf.find(f.sensor);
        const int post = it != postOf.end() ? it->second : DefaultPost(f.sensor);
        if (post < 0 || post > 1)
        {
            note(f.name + ": " + SensorName(f.sensor) + " без поста — не учитывается");
            continue;
        }
        Measurement& m = byNum[number];
        m.number = number;
        m.label = f.pcRun.empty() ? std::to_string(number) : PcRunLabel(f.pcRun);
        PostData& pd = m.post[post];
        if (pd.file >= 0)
        {
            note(f.name + ": на посту «" + kPostName[post] + "» замера " + m.label + " уже есть " +
                 files[static_cast<std::size_t>(pd.file)].name + " — не учтён");
            continue;
        }
        pd.file = static_cast<int>(i);
    }
    std::vector<Measurement> out;
    for (auto& [n, m] : byNum)
        out.push_back(std::move(m));
    return out;
}

// Крен каждого файла за окно, h по точкам, итог и МНК. Плечо замера — m.arm (NaN — s.L).
inline Summary Compute(const Settings& s, const std::vector<File>& files, std::vector<Measurement>& ms)
{
    std::vector<Point> pts;
    std::vector<PostData*> where;
    Summary sum;
    for (Measurement& m : ms)
    {
        if (m.use)
            sum.measurements++;
        for (int p = 0; p < 2; p++)
        {
            PostData& pd = m.post[p];
            pd.res = PointResult{};
            if (pd.file < 0)
                continue;
            const File& f = files[static_cast<std::size_t>(pd.file)];
            pd.st = TailStats(f.log, s.axis, s.windowS);
            pd.batV = LastBattery(f.log);
            if (!m.use)
            {
                pd.res.note = "замер не в расчёте";
                continue;
            }
            Point pt;
            pt.number = m.number;
            pt.post = p;
            pt.thetaDeg = pd.st.mean;
            pt.arm = std::isfinite(m.arm) ? std::fabs(m.arm) : s.L;
            pts.push_back(pt);
            where.push_back(&pd);
        }
    }
    const Result r = Solve(pts, s.D, s.P, s.thresholdDeg);
    for (std::size_t i = 0; i < where.size(); i++)
        where[i]->res = r.points[i];
    sum.h = r.h;
    sum.sd = r.sd;
    sum.n = r.n;
    sum.ls = r.ls;
    return sum;
}

} // namespace heel
