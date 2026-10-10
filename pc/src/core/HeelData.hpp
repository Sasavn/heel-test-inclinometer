#pragma once
// Данные опыта кренования для «Обработки»: файлы замеров в папке -> замеры (по номеру из имени) и посты (по датчику)
// -> установившийся крен каждого файла (среднее за последние N с, СКО, число точек) -> точки для heel::Solve
// (Heel.hpp) -> h по замерам и постам, итог.
//
// Читаются ВСЕ *.CSV и *.TXT папки (или выбранные файлы): формат узнаётся по содержимому (CsvLog.hpp), а имя только
// группирует файлы в замеры. Имена: «ГГГГ-ММ-ДД_MNNN_Dk.CSV» (прошивка 1.0+, k — Modbus-адрес датчика), старые
// «M_NNN_k.CSV» (k — 1 нос, 2 корма у исходной прошивки) и «M_NNN.CSV» (самая первая прошивка, один датчик), записи
// на ПК «ГГГГ-ММ-ДД_ЧЧ-ММ-СС[_метка]_PC_Dk.CSV» (одна запись — один замер; номера 1001, 1002, … по времени начала,
// в таблице — время и метка). Файл с другим именем, но понятным содержимым — отдельная запись (номера 2001, 2002, …,
// в таблице — имя файла). Пост датчика по умолчанию: Д2 — нос, Д3 — корма; старые 1 — нос, 2 — корма; без номера
// датчика — нос (меняется в интерфейсе).
//
// Непрерывная запись (один файл — весь опыт): положения груза ищет Plateaus.hpp по основному посту записи; крен
// каждого поста на участке положения, Δθ от начального положения, h по положениям — heel::SolvePositions (Heel.hpp).
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
#include "Plateaus.hpp"
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
    double minPlateauS = 10.0; // непрерывная запись: положение не короче, с
    double minStepDeg = 0.5;   // непрерывная запись: перепад крена между положениями не меньше, °

    plateau::Params Plateau() const
    {
        plateau::Params p;
        p.minLenS = minPlateauS;
        p.minStepDeg = minStepDeg;
        return p;
    }
};

// Файл замера.
struct File
{
    std::string name;            // имя файла
    std::filesystem::path path;
    int number = -1;             // номер замера из имени; -1 — имя не по образцу (или запись на ПК)
    int sensor = -1;             // ключ датчика (SensorKey); kNoSensor — в имени нет
    std::string pcRun;           // запись на ПК: «ГГГГ-ММ-ДД_ЧЧ-ММ-СС[_метка]» (номер дают по порядку)
    std::string other;           // имя не по образцу: имя без расширения (отдельная запись, номер — по порядку)
    csvlog::Log log;
    int plateaus = 0;            // положений груза в файле (Plateaus.hpp, ось и параметры — при чтении)

    bool Measurement() const { return number >= 0 || !pcRun.empty() || !other.empty(); }
};

inline constexpr int kPcRunBase = 1000;    // номера замеров для записей на ПК: 1001, 1002, …
inline constexpr int kOtherBase = 2000;    // номера для файлов с другими именами: 2001, 2002, …
inline constexpr int kNoSensor = 0;        // ключ датчика, если в имени его нет

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
    if (key == kNoSensor)
        return "датчик ?";
    return key >= 100 ? "датчик " + std::to_string(key - 100) + " (M_NNN_" + std::to_string(key - 100) + ")"
                      : "Д" + std::to_string(key);
}

// Короткое имя датчика для заголовков: «Д2», «M_1», «датчик ?».
inline std::string SensorShort(int key)
{
    if (key == kNoSensor)
        return "датчик ?";
    return key >= 100 ? "M_" + std::to_string(key - 100) : "Д" + std::to_string(key);
}

// Пост по умолчанию: 0 — нос, 1 — корма, -1 — не учитывать.
inline int DefaultPost(int key)
{
    switch (key)
    {
    case kNoSensor: return kBow;
    case 2: return kBow;
    case 3: return kStern;
    case 101: return kBow;
    case 102: return kStern;
    default: return -1;
    }
}

// Файлы *.CSV, *.TXT и *.XLSX в папке, по имени. Если в самой папке таких нет — в её подпапках (на один уровень;
// *sub = true). Книги, которые пишет сама программа (Отчет_Кренование.xlsx, книга записи на ПК «…_PC.xlsx» рядом с её
// CSV), и книга, у которой рядом есть CSV с тем же именем, не берутся — данные те же.
inline std::vector<std::filesystem::path> ListCsv(const std::filesystem::path& dir, std::string* err = nullptr,
                                                  bool* sub = nullptr)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (sub)
        *sub = false;
    if (!std::filesystem::is_directory(dir, ec))
    {
        if (err)
            *err = "папки нет";
        return out;
    }
    auto scan = [&](const std::filesystem::path& d, std::vector<std::filesystem::path>* dirs) {
        std::error_code e;
        for (std::filesystem::directory_iterator it(d, e), end; !e && it != end; it.increment(e))
        {
            if (dirs && it->is_directory(e))
            {
                dirs->push_back(it->path());
                continue;
            }
            if (!it->is_regular_file(e))
                continue;
            std::string ext = text::PathToUtf8(it->path().extension());
            for (auto& c : ext)
                c = static_cast<char>((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
            if (ext == ".CSV" || ext == ".TXT" || ext == ".XLSX")
                out.push_back(it->path());
        }
        return !e;
    };
    std::vector<std::filesystem::path> dirs;
    if (!scan(dir, &dirs) && err)
        *err = "не удалось прочитать папку";
    if (out.empty() && !dirs.empty())
    {
        std::sort(dirs.begin(), dirs.end());
        for (const auto& d : dirs)
            scan(d, nullptr);
        if (sub)
            *sub = !out.empty();
    }
    std::sort(out.begin(), out.end());
    // Книги Excel: свои отчёты и копии CSV — мимо
    auto upper = [](std::string x) {
        for (auto& c : x)
            c = static_cast<char>((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
        return x;
    };
    std::vector<std::string> csvStems;
    for (const auto& q : out)
        if (upper(text::PathToUtf8(q.extension())) == ".CSV")
            csvStems.push_back(upper(text::PathToUtf8(q.stem())));
    std::vector<std::filesystem::path> kept;
    for (const auto& q : out)
    {
        if (upper(text::PathToUtf8(q.extension())) == ".XLSX")
        {
            const std::string name = text::PathToUtf8(q.filename());
            const std::string stem = upper(text::PathToUtf8(q.stem()));
            if (name.rfind("Отчет_Кренование", 0) == 0)
                continue;
            bool copy = false;
            for (const auto& c : csvStems)
                copy |= c == stem || (stem.size() > 3 && stem.compare(stem.size() - 3, 3, "_PC") == 0 &&
                                      c.rfind(stem + "_D", 0) == 0);
            if (copy)
                continue;
        }
        kept.push_back(q);
    }
    return kept;
}

// Номер замера и датчик — по имени файла (если имя по одному из образцов).
inline void NameInfo(File& f)
{
    proto::FileEntry e;
    e.name = text::PathToUtf8(f.path.stem()) + ".CSV"; // и у .TXT / .XLSX — по тому же образцу имени
    proto::ParseMeasurementName(e);
    f.number = e.measurement;
    const bool oldName = f.name.size() > 2 && (f.name[0] == 'M' || f.name[0] == 'm') && f.name[1] == '_';
    f.sensor = e.sensorAddr >= 0 ? SensorKey(e.sensorAddr, oldName) : -1;
    std::string up;
    for (const char c : e.name)
        up += static_cast<char>((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
    const bool csv = up.size() > 4;
    // «M_NNN.CSV» — самая первая прошивка (один датчик): датчик 1
    if (f.number < 0 && csv && up.size() == 9 && up.compare(0, 2, "M_") == 0 &&
        proto::AllDigits(std::string_view(up).substr(2, 3)))
    {
        f.number = static_cast<int>(proto::ToInt(std::string_view(up).substr(2, 3)));
        f.sensor = SensorKey(1, true);
    }
    if (f.number < 0 && csv)
    {
        // Запись на ПК: «…_PC_Dk.CSV»
        const std::size_t pc = up.rfind("_PC_D");
        const std::size_t end = up.size() - 4;
        if (pc != std::string::npos && pc > 0 && pc + 5 < end &&
            proto::AllDigits(std::string_view(up).substr(pc + 5, end - pc - 5)))
        {
            f.pcRun = e.name.substr(0, pc);
            f.sensor = SensorKey(static_cast<int>(proto::ToInt(std::string_view(up).substr(pc + 5, end - pc - 5))), false);
        }
    }
}

// Прочитать файл: формат — по содержимому; имя не по образцу, но файл понятный — отдельная запись.
inline File LoadFile(const std::filesystem::path& p, const Settings* s = nullptr)
{
    File f;
    f.path = p;
    f.name = text::PathToUtf8(p.filename());
    NameInfo(f);
    csvlog::ReadFile(p, f.log);
    if (f.number < 0 && f.pcRun.empty())
    {
        f.other = text::PathToUtf8(p.stem());
        if (f.other.empty())
            f.other = f.name;
        f.sensor = kNoSensor;
    }
    if (s && f.log.error.empty())
    {
        std::vector<double> t, v;
        t.reserve(f.log.rows.size());
        v.reserve(f.log.rows.size());
        for (const auto& r : f.log.rows)
        {
            t.push_back(r.t);
            v.push_back(r.Calc(s->axis));
        }
        f.plateaus = static_cast<int>(plateau::Detect(t, v, s->Plateau()).size());
    }
    return f;
}

struct Loaded
{
    std::vector<File> files;
    std::string folder; // куда сохранять отчёт
};

// Прочитать файлы (поток интерфейса не ждёт: вызывается из std::async). done — сколько прочитано; s — ось и
// параметры поиска положений (для выбора режима «одна запись — весь опыт»), nullptr — не искать.
inline Loaded LoadAll(const std::vector<std::filesystem::path>& paths, const std::string& folder,
                      std::atomic<int>* done = nullptr, const Settings* s = nullptr)
{
    Loaded r;
    r.folder = folder;
    for (const auto& p : paths)
    {
        r.files.push_back(LoadFile(p, s));
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
    std::map<int, int> otherNum; // файлы с другими именами (понятные по содержимому): по порядку
    k = kOtherBase;
    for (std::size_t i = 0; i < files.size(); i++)
        if (!files[i].other.empty() && files[i].log.error.empty())
            otherNum[static_cast<int>(i)] = ++k;
    for (std::size_t i = 0; i < files.size(); i++)
    {
        const File& f = files[i];
        if (!f.log.error.empty())
        {
            if (f.log.error == "не понял формат")
                note("не понял формат: " + f.name + ", первая строка: «" + f.log.firstLine + "» — пропущен");
            else
                note(f.name + ": " + f.log.error + " — пропущен");
            continue;
        }
        if (!f.Measurement())
            continue;
        const int number = !f.pcRun.empty()   ? pcNum[f.pcRun]
                           : !f.other.empty() ? otherNum[static_cast<int>(i)]
                                              : f.number;
        const auto it = postOf.find(f.sensor);
        const int post = it != postOf.end() ? it->second : DefaultPost(f.sensor);
        if (post < 0 || post > 1)
        {
            note(f.name + ": " + SensorName(f.sensor) + " без поста — не учитывается");
            continue;
        }
        Measurement& m = byNum[number];
        m.number = number;
        m.label = !f.pcRun.empty() ? PcRunLabel(f.pcRun) : !f.other.empty() ? f.other : std::to_string(number);
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

// ---------------------------------------------------------------------------------------------------------------
// Непрерывная запись: один файл (на пост) — весь опыт, положения груза — участки установившегося крена
// ---------------------------------------------------------------------------------------------------------------

struct Position
{
    double t0 = kNaN, t1 = kNaN; // участок, с от начала записи
    bool use = true;             // в расчёте
    double arm = kNaN;           // |l| — плечо переноса от начального положения, м; NaN — L
    plateau::Span st[2];         // крен поста на участке (нос, корма)
    PointResult res[2];          // Δθ, h, учтено ли, почему нет
};

// Время и угол по оси axis — отдельными рядами (для Plateaus.hpp).
inline void Series(const csvlog::Log& log, int axis, std::vector<double>& t, std::vector<double>& v)
{
    t.clear();
    v.clear();
    t.reserve(log.rows.size());
    v.reserve(log.rows.size());
    for (const auto& r : log.rows)
    {
        t.push_back(r.t);
        v.push_back(r.Calc(axis));
    }
}

// Основной пост записи — по нему ищутся положения: нос, а без носового файла — корма; -1 — файлов нет.
inline int MainPost(const Measurement& m)
{
    return m.post[0].file >= 0 ? 0 : m.post[1].file >= 0 ? 1 : -1;
}

// Положения груза в записи m (Plateaus.hpp по основному посту).
inline std::vector<Position> FindPositions(const Settings& s, const std::vector<File>& files, const Measurement& m)
{
    std::vector<Position> out;
    const int mp = MainPost(m);
    if (mp < 0)
        return out;
    std::vector<double> t, v;
    Series(files[static_cast<std::size_t>(m.post[mp].file)].log, s.axis, t, v);
    for (const plateau::Span& sp : plateau::Detect(t, v, s.Plateau()))
    {
        Position p;
        p.t0 = sp.t0;
        p.t1 = sp.t1;
        out.push_back(p);
    }
    return out;
}

// Крен постов на участках, Δθ от начального положения, h по положениям, итог и МНК (heel::SolvePositions).
inline Summary ComputePositions(const Settings& s, const std::vector<File>& files, const Measurement& m,
                                std::vector<Position>& pos)
{
    Summary sum;
    std::vector<Point> pts;
    std::vector<bool> use;
    std::vector<std::pair<std::size_t, int>> where;
    for (int p = 0; p < 2; p++)
    {
        std::vector<double> t, v;
        if (m.post[p].file >= 0)
            Series(files[static_cast<std::size_t>(m.post[p].file)].log, s.axis, t, v);
        for (std::size_t i = 0; i < pos.size(); i++)
        {
            Position& ps = pos[i];
            ps.res[p] = PointResult{};
            ps.st[p] = plateau::Span{};
            if (m.post[p].file < 0)
                continue;
            ps.st[p] = plateau::Measure(t, v, std::min(ps.t0, ps.t1), std::max(ps.t0, ps.t1));
            Point pt;
            pt.number = static_cast<int>(i) + 1;
            pt.post = p;
            pt.thetaDeg = ps.st[p].mean;
            pt.arm = std::isfinite(ps.arm) ? std::fabs(ps.arm) : s.L;
            pts.push_back(pt);
            use.push_back(ps.use);
            where.push_back({i, p});
        }
    }
    for (const Position& ps : pos)
        sum.measurements += ps.use ? 1 : 0;
    const Result r = SolvePositions(pts, use, s.D, s.P, s.thresholdDeg);
    for (std::size_t k = 0; k < where.size(); k++)
        pos[where[k].first].res[where[k].second] = r.points[k];
    sum.h = r.h;
    sum.sd = r.sd;
    sum.n = r.n;
    sum.ls = r.ls;
    return sum;
}

// Какой режим подходит файлам: индекс записи (в ms) с наибольшим числом положений груза у основного поста — если их
// не меньше трёх (один файл — весь опыт); -1 — «файл = замер».
inline int SuggestRecord(const std::vector<File>& files, const std::vector<Measurement>& ms)
{
    int best = -1, bestN = 2;
    double bestDur = 0;
    for (std::size_t i = 0; i < ms.size(); i++)
    {
        const int mp = MainPost(ms[i]);
        if (mp < 0)
            continue;
        const File& f = files[static_cast<std::size_t>(ms[i].post[mp].file)];
        const double dur = f.log.Duration();
        if (f.plateaus > bestN || (f.plateaus == bestN && best >= 0 && dur > bestDur))
        {
            best = static_cast<int>(i);
            bestN = f.plateaus;
            bestDur = dur;
        }
    }
    return best;
}

// Самая длинная запись (для режима «одна запись — весь опыт», выбранного вручную); -1 — записей нет.
inline int LongestRecord(const std::vector<File>& files, const std::vector<Measurement>& ms)
{
    int best = -1;
    double bestDur = -1;
    for (std::size_t i = 0; i < ms.size(); i++)
    {
        const int mp = MainPost(ms[i]);
        if (mp < 0)
            continue;
        const double dur = files[static_cast<std::size_t>(ms[i].post[mp].file)].log.Duration();
        if (dur > bestDur)
        {
            best = static_cast<int>(i);
            bestDur = dur;
        }
    }
    return best;
}

} // namespace heel

