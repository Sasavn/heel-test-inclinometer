#pragma once
// Файл проекта опыта кренования «*.krenomer» (JSON, UTF-8): откуда записи, исходные данные, датчики, метод
// обработки, Правила РС, положения груза и правки — всё, чтобы открыть опыт позже и получить тот же расчёт.
//
// Пути к папке и файлам записей — относительно файла проекта, если они рядом (в его папке, её подпапках или в
// соседней папке), и всегда ещё полным путём (запасной вариант, если проект переложили без записей). Плечи, D, P и L
// — в единицах опыта (ship.model: малое судно — кг и мм, иначе т и м). NaN (не задано) — null.
//
// Версия формата — поле krenomer_project. Новая программа читает старые форматы; файл более новой версии не
// читается (понятная ошибка «обновите программу»).
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "HeelData.hpp"
#include "Json.hpp"
#include "TextUtil.hpp"

namespace proj
{

inline constexpr int kFormat = 1;                         // версия формата файла
inline constexpr const char* kExt = ".krenomer";
inline constexpr const wchar_t* kAutoName = L"Опыт.krenomer"; // автосохранение — рядом с записями

struct Sensor
{
    int key = 0;          // heel::SensorKey
    std::string name;     // название поста («Нос», «Корма»); пусто — по умолчанию
    bool inverted = false; // знак обратный (ось +X — к левому борту)
    bool use = true;      // в расчёте
};

struct Project
{
    int format = kFormat;
    std::string program; // кем сохранён («Регистратор крена — программа для ПК 1.7»)
    std::string saved;   // когда (местное время)

    // Записи: папка и выбранные файлы (пусто — все файлы папки). rel — как записано (относительно проекта; пусто —
    // далеко), abs — полный путь. После Load — уже найденные полные пути в folder / files.
    std::string folder;
    std::vector<std::string> files;
    std::string folderRel;
    std::vector<std::string> filesRel;

    // Судно и поиск положений
    bool model = false;   // малое судно (модель): кг и мм
    double D = 1500.0, P = 15.0, L = 4.5;
    double B = 0.0;       // ширина судна (модели), плечевые единицы; 0 — не задана
    int armFrom = 0;      // плечи вводились: 0 — от ДП, 1 — от ЛБ, 2 — от ПрБ
    int axis = 0;         // 0 — X, 1 — Y
    double windowS = 30.0, thresholdDeg = 0.1, plateauS = 10.0, stepDeg = 0.5;

    // Обработка сигнала (core/Filters.hpp): метод 0…4 — встроенные, 5 — свой (имя и текст — в проекте)
    int method = 3, medianN = 3;
    double fcHz = 0.5, tcS = 0.0, sgWindowS = 4.0;
    bool wholeSpan = false;
    std::string userMethod;     // имя своего метода
    std::string userMethodText; // его текст (um::ToText) — чтобы проект открывался и там, где метода нет

    std::vector<Sensor> sensors;

    // Правила РС
    double rsH = 0.0, rsLmax = 0.0, rsD0 = 0.0; // 0 — не задано
    double rsSpread = 0.1;
    int rsMain = 0, rsMainGroups = -1, rsInstrument = -1;
    double rsDh = 0.0, rsDworst = 0.0;          // 0 — D
    bool rsGroupMode = false;
    std::vector<heel::RsGroup> rsGroups;
    std::vector<std::vector<double>> rsMoves;   // [шаг][группа]

    // Опыт: режим, запись, положения и ручные правки
    int mode = 0;               // 0 — файл = замер, 1 — одна запись — весь опыт
    bool modeManual = false;    // режим выбран вручную
    std::string recordFile;     // запись (имя файла основного поста)
    bool positionsEdited = false;
    std::vector<heel::Position> positions; // t0, t1, use, arm
    std::map<int, double> arms;            // файл = замер: плечо замера (номер -> плечо)
    std::map<int, bool> excluded;          // замеры, снятые с расчёта
    std::map<int, int> rsManual;           // табл. 6.5: номер -> 0 исключить / 1 учитывать

    // Итог при сохранении (для сведения и проверки; при открытии считается заново)
    double h = heel::kNaN, hMain = heel::kNaN;

    // Load: записи, найденные по похожему имени (« (1)», « - копия», регистр), — для журнала; не найденные
    std::vector<std::string> notes;
    std::vector<std::string> missing;
};

// ---------------------------------------------------------------------------------------------------------------
// Пути
// ---------------------------------------------------------------------------------------------------------------

// Путь относительно папки проекта, если он «рядом»: в ней, в её подпапках или на один уровень выше (соседняя папка);
// иначе пусто.
inline std::string RelativeTo(const std::filesystem::path& projectDir, const std::string& target)
{
    if (target.empty() || projectDir.empty())
        return {};
    const std::filesystem::path t = text::PathFromUtf8(target).lexically_normal();
    const std::filesystem::path base = projectDir.lexically_normal();
    if (t.root_name() != base.root_name())
        return {};
    const std::filesystem::path rel = t.lexically_relative(base);
    if (rel.empty())
        return {};
    int up = 0;
    for (const auto& part : rel)
        if (part == "..")
            up++;
    if (up > 1)
        return {};
    return text::PathToUtf8(rel);
}

// Найти путь из проекта: сначала относительно папки проекта, затем полный. Пусто — нигде нет.
inline std::string Resolve(const std::filesystem::path& projectDir, const std::string& rel, const std::string& abs)
{
    std::error_code ec;
    if (!rel.empty())
    {
        std::filesystem::path p = (projectDir / text::PathFromUtf8(rel)).lexically_normal();
        if (!p.has_filename()) // «папка\.» -> «папка\» -> «папка»
            p = p.parent_path();
        if (std::filesystem::exists(p, ec))
            return text::PathToUtf8(p);
    }
    if (!abs.empty() && std::filesystem::exists(text::PathFromUtf8(abs), ec))
        return abs;
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// Запись JSON
// ---------------------------------------------------------------------------------------------------------------

namespace detail
{

inline std::string Str(const std::string& s)
{
    std::string o = "\"";
    for (const char c : s)
    {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"')
            o += "\\\"";
        else if (c == '\\')
            o += "\\\\";
        else if (c == '\n')
            o += "\\n";
        else if (c == '\r')
            o += "\\r";
        else if (c == '\t')
            o += "\\t";
        else if (u < 0x20)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "\\u%04x", u);
            o += b;
        }
        else
            o += c;
    }
    return o + "\"";
}

inline std::string Num(double v)
{
    if (!std::isfinite(v))
        return "null";
    char b[40];
    std::snprintf(b, sizeof(b), "%.10g", v);
    return b;
}

inline std::string Bool(bool b) { return b ? "true" : "false"; }

} // namespace detail

// meta — с полями «кем и когда сохранён» и итогом (для файла); без них — для сравнения состояний (автосохранение).
inline std::string ToJson(const Project& p, bool meta = true)
{
    using detail::Bool;
    using detail::Num;
    using detail::Str;
    std::string o = "{\n";
    o += "  \"krenomer_project\": " + std::to_string(p.format) + ",\n";
    if (meta)
    {
        o += "  \"program\": " + Str(p.program) + ",\n";
        o += "  \"saved\": " + Str(p.saved) + ",\n";
    }
    // Записи
    o += "  \"records\": {\n";
    o += "    \"folder\": " + Str(p.folderRel) + ",\n";
    o += "    \"folder_abs\": " + Str(p.folder) + ",\n";
    o += "    \"files\": [";
    for (std::size_t i = 0; i < p.filesRel.size(); i++)
        o += (i ? ", " : "") + Str(p.filesRel[i]);
    o += "],\n    \"files_abs\": [";
    for (std::size_t i = 0; i < p.files.size(); i++)
        o += (i ? ", " : "") + Str(p.files[i]);
    o += "]\n  },\n";
    // Судно и поиск положений
    o += "  \"ship\": {\"model\": " + Bool(p.model) + ", \"D\": " + Num(p.D) + ", \"P\": " + Num(p.P) + ", \"L\": " +
         Num(p.L) + ", \"axis\": " + Str(p.axis == 1 ? "Y" : "X") + ", \"B\": " + Num(p.B) + ", \"arms_from\": " +
         Str(p.armFrom == 2 ? "ПрБ" : p.armFrom == 1 ? "ЛБ" : "ДП") + "},\n";
    o += "  \"detect\": {\"window_s\": " + Num(p.windowS) + ", \"threshold_deg\": " + Num(p.thresholdDeg) +
         ", \"plateau_s\": " + Num(p.plateauS) + ", \"step_deg\": " + Num(p.stepDeg) + "},\n";
    o += "  \"processing\": {\"method\": " + std::to_string(p.method) + ", \"median_n\": " + std::to_string(p.medianN) +
         ", \"fc_hz\": " + Num(p.fcHz) + ", \"tc_s\": " + Num(p.tcS) + ", \"sg_window_s\": " + Num(p.sgWindowS) +
         ", \"whole_span\": " + Bool(p.wholeSpan) + ",\n    \"user_method\": " + Str(p.userMethod) +
         ", \"user_method_text\": " + Str(p.userMethodText) + "},\n";
    // Датчики
    o += "  \"sensors\": [";
    for (std::size_t i = 0; i < p.sensors.size(); i++)
    {
        const Sensor& s = p.sensors[i];
        o += std::string(i ? "," : "") + "\n    {\"key\": " + std::to_string(s.key) + ", \"name\": " + Str(s.name) +
             ", \"inverted\": " + Bool(s.inverted) + ", \"use\": " + Bool(s.use) + "}";
    }
    o += p.sensors.empty() ? "],\n" : "\n  ],\n";
    // Правила РС
    auto opt = [&](double v) { return v > 0 ? Num(v) : std::string("null"); };
    o += "  \"rules\": {\"h\": " + opt(p.rsH) + ", \"l_max\": " + opt(p.rsLmax) + ", \"d0\": " + opt(p.rsD0) +
         ", \"spread_deg\": " + Num(p.rsSpread) + ", \"main\": " + std::to_string(p.rsMain) +
         ", \"main_groups\": " + std::to_string(p.rsMainGroups) + ", \"instrument\": " + std::to_string(p.rsInstrument) +
         ",\n    \"dh\": " + Num(p.rsDh) + ", \"d_worst\": " + opt(p.rsDworst) + ", \"group_mode\": " +
         Bool(p.rsGroupMode) + ",\n    \"groups\": [";
    for (std::size_t i = 0; i < p.rsGroups.size(); i++)
        o += std::string(i ? ", " : "") + "{\"name\": " + Str(p.rsGroups[i].name) + ", \"mass\": " + Num(p.rsGroups[i].mass) + "}";
    o += "],\n    \"moves\": [";
    for (std::size_t i = 0; i < p.rsMoves.size(); i++)
    {
        o += i ? ", [" : "[";
        for (std::size_t g = 0; g < p.rsMoves[i].size(); g++)
            o += (g ? ", " : "") + Num(p.rsMoves[i][g]);
        o += "]";
    }
    o += "]\n  },\n";
    // Опыт
    o += "  \"experiment\": {\n";
    o += "    \"mode\": " + Str(p.mode == 1 ? "record" : "files") + ", \"mode_manual\": " + Bool(p.modeManual) + ",\n";
    o += "    \"record_file\": " + Str(p.recordFile) + ", \"positions_edited\": " + Bool(p.positionsEdited) + ",\n";
    o += "    \"positions\": [";
    for (std::size_t i = 0; i < p.positions.size(); i++)
    {
        const heel::Position& q = p.positions[i];
        o += std::string(i ? "," : "") + "\n      {\"t0\": " + Num(q.t0) + ", \"t1\": " + Num(q.t1) + ", \"use\": " +
             Bool(q.use) + ", \"arm\": " + Num(q.arm) + "}";
    }
    o += p.positions.empty() ? "],\n" : "\n    ],\n";
    o += "    \"arms\": [";
    {
        bool first = true;
        for (const auto& [n, a] : p.arms)
        {
            o += std::string(first ? "" : ", ") + "{\"n\": " + std::to_string(n) + ", \"arm\": " + Num(a) + "}";
            first = false;
        }
    }
    o += "],\n    \"excluded\": [";
    {
        bool first = true;
        for (const auto& [n, off] : p.excluded)
            if (off)
            {
                o += (first ? "" : ", ") + std::to_string(n);
                first = false;
            }
    }
    o += "],\n    \"rs_manual\": [";
    {
        bool first = true;
        for (const auto& [n, st] : p.rsManual)
        {
            o += std::string(first ? "" : ", ") + "{\"n\": " + std::to_string(n) + ", \"use\": " + Bool(st != 0) + "}";
            first = false;
        }
    }
    o += "]\n  }";
    if (meta)
        o += ",\n  \"result\": {\"h\": " + Num(p.h) + ", \"h_main\": " + Num(p.hMain) + "}";
    o += "\n}\n";
    return o;
}

// ---------------------------------------------------------------------------------------------------------------
// Чтение JSON
// ---------------------------------------------------------------------------------------------------------------

namespace detail
{

// Поля читаются «мягко»: нет поля — значение по умолчанию; поле не того типа — ошибка с его именем.
struct Reader
{
    std::string err;

    void Bad(const std::string& where, const char* what)
    {
        if (err.empty())
            err = "поле «" + where + "»: " + what;
    }
    void Num(const json::Value& v, const std::string& where, double& out, bool nullIsNaN = false)
    {
        if (v.IsNull())
        {
            if (nullIsNaN)
                out = heel::kNaN;
            return;
        }
        if (!v.IsNumber())
            return Bad(where, "ожидается число");
        out = v.n;
    }
    void Opt(const json::Value& v, const std::string& where, double& out) // null — 0 (не задано)
    {
        if (v.IsNull())
        {
            out = 0.0;
            return;
        }
        Num(v, where, out);
    }
    void Int(const json::Value& v, const std::string& where, int& out, int lo, int hi)
    {
        if (v.IsNull())
            return;
        if (!v.IsNumber() || v.n != std::floor(v.n))
            return Bad(where, "ожидается целое число");
        if (v.n < lo || v.n > hi)
            return Bad(where, ("ожидается от " + std::to_string(lo) + " до " + std::to_string(hi)).c_str());
        out = static_cast<int>(v.n);
    }
    void Flag(const json::Value& v, const std::string& where, bool& out)
    {
        if (v.IsNull())
            return;
        if (!v.IsBool())
            return Bad(where, "ожидается true или false");
        out = v.b;
    }
    void Text(const json::Value& v, const std::string& where, std::string& out)
    {
        if (v.IsNull())
            return;
        if (!v.IsString())
            return Bad(where, "ожидается строка");
        out = v.s;
    }
    bool Arr(const json::Value& v, const std::string& where)
    {
        if (v.IsNull())
            return false;
        if (!v.IsArray())
        {
            Bad(where, "ожидается список");
            return false;
        }
        return true;
    }
};

} // namespace detail

// Разбор текста проекта. false — err: что не так (по-русски, для журнала и окна).
inline bool FromJson(std::string_view text, Project& p, std::string* err = nullptr)
{
    auto fail = [&](const std::string& e) {
        if (err)
            *err = e;
        return false;
    };
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    std::size_t first = 0;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\n' || text[first] == '\r' || text[first] == '\t'))
        first++;
    if (first == text.size())
        return fail("файл пуст");
    json::Value v;
    std::string jerr;
    if (!json::Parse(text, v, &jerr))
        return fail("файл повреждён или это не JSON: " + jerr);
    if (!v.IsObject() || !v.Has("krenomer_project"))
        return fail("это не файл проекта «Регистратора крена» (нет поля krenomer_project)");
    const json::Value& ver = v["krenomer_project"];
    if (!ver.IsNumber() || ver.n < 1 || ver.n != std::floor(ver.n))
        return fail("непонятная версия формата проекта (krenomer_project)");
    if (ver.n > kFormat)
        return fail("проект сохранён более новой программой (формат " + std::to_string(static_cast<int>(ver.n)) +
                    ", эта программа понимает до " + std::to_string(kFormat) + ") — обновите программу");
    Project o;
    o.format = static_cast<int>(ver.n);
    detail::Reader r;
    r.Text(v["program"], "program", o.program);
    r.Text(v["saved"], "saved", o.saved);

    const json::Value& rec = v["records"];
    r.Text(rec["folder"], "records.folder", o.folderRel);
    r.Text(rec["folder_abs"], "records.folder_abs", o.folder);
    if (r.Arr(rec["files"], "records.files"))
        for (std::size_t i = 0; i < rec["files"].Size(); i++)
        {
            std::string s;
            r.Text(rec["files"][i], "records.files", s);
            o.filesRel.push_back(s);
        }
    if (r.Arr(rec["files_abs"], "records.files_abs"))
        for (std::size_t i = 0; i < rec["files_abs"].Size(); i++)
        {
            std::string s;
            r.Text(rec["files_abs"][i], "records.files_abs", s);
            o.files.push_back(s);
        }
    if (!o.filesRel.empty() && o.filesRel.size() != o.files.size())
        r.Bad("records.files", "число файлов не совпадает с records.files_abs");
    if (o.folder.empty() && o.folderRel.empty() && o.files.empty())
        r.Bad("records", "нет папки записей");

    const json::Value& ship = v["ship"];
    r.Flag(ship["model"], "ship.model", o.model);
    r.Num(ship["D"], "ship.D", o.D);
    r.Num(ship["P"], "ship.P", o.P);
    r.Num(ship["L"], "ship.L", o.L);
    if (!ship["axis"].IsNull())
    {
        std::string a;
        r.Text(ship["axis"], "ship.axis", a);
        if (a == "X" || a == "Y")
            o.axis = a == "Y" ? 1 : 0;
        else
            r.Bad("ship.axis", "ожидается \"X\" или \"Y\"");
    }
    r.Num(ship["B"], "ship.B", o.B);
    if (!ship["arms_from"].IsNull())
    {
        std::string a;
        r.Text(ship["arms_from"], "ship.arms_from", a);
        if (a == "ДП" || a == "ЛБ" || a == "ПрБ")
            o.armFrom = a == "ПрБ" ? 2 : a == "ЛБ" ? 1 : 0;
        else
            r.Bad("ship.arms_from", "ожидается \"ДП\", \"ЛБ\" или \"ПрБ\"");
    }
    if (r.err.empty() && !(o.D > 0))
        r.Bad("ship.D", "водоизмещение должно быть больше нуля");
    const json::Value& det = v["detect"];
    r.Num(det["window_s"], "detect.window_s", o.windowS);
    r.Num(det["threshold_deg"], "detect.threshold_deg", o.thresholdDeg);
    r.Num(det["plateau_s"], "detect.plateau_s", o.plateauS);
    r.Num(det["step_deg"], "detect.step_deg", o.stepDeg);

    const json::Value& pr = v["processing"];
    r.Int(pr["method"], "processing.method", o.method, 0, 5);
    r.Int(pr["median_n"], "processing.median_n", o.medianN, 1, 99);
    r.Num(pr["fc_hz"], "processing.fc_hz", o.fcHz);
    r.Num(pr["tc_s"], "processing.tc_s", o.tcS);
    r.Num(pr["sg_window_s"], "processing.sg_window_s", o.sgWindowS);
    r.Flag(pr["whole_span"], "processing.whole_span", o.wholeSpan);
    r.Text(pr["user_method"], "processing.user_method", o.userMethod);
    r.Text(pr["user_method_text"], "processing.user_method_text", o.userMethodText);

    if (r.Arr(v["sensors"], "sensors"))
        for (std::size_t i = 0; i < v["sensors"].Size(); i++)
        {
            const json::Value& s = v["sensors"][i];
            Sensor q;
            r.Int(s["key"], "sensors.key", q.key, 0, 1000000);
            r.Text(s["name"], "sensors.name", q.name);
            r.Flag(s["inverted"], "sensors.inverted", q.inverted);
            r.Flag(s["use"], "sensors.use", q.use);
            o.sensors.push_back(q);
        }

    const json::Value& rs = v["rules"];
    r.Opt(rs["h"], "rules.h", o.rsH);
    r.Opt(rs["l_max"], "rules.l_max", o.rsLmax);
    r.Opt(rs["d0"], "rules.d0", o.rsD0);
    r.Num(rs["spread_deg"], "rules.spread_deg", o.rsSpread);
    r.Int(rs["main"], "rules.main", o.rsMain, 0, 1);
    r.Int(rs["main_groups"], "rules.main_groups", o.rsMainGroups, -1, 1);
    r.Int(rs["instrument"], "rules.instrument", o.rsInstrument, -1, heel::kMaxPosts - 1);
    r.Num(rs["dh"], "rules.dh", o.rsDh);
    r.Opt(rs["d_worst"], "rules.d_worst", o.rsDworst);
    r.Flag(rs["group_mode"], "rules.group_mode", o.rsGroupMode);
    if (r.Arr(rs["groups"], "rules.groups"))
        for (std::size_t i = 0; i < rs["groups"].Size(); i++)
        {
            heel::RsGroup g;
            r.Text(rs["groups"][i]["name"], "rules.groups.name", g.name);
            r.Num(rs["groups"][i]["mass"], "rules.groups.mass", g.mass);
            o.rsGroups.push_back(g);
        }
    if (r.Arr(rs["moves"], "rules.moves"))
        for (std::size_t i = 0; i < rs["moves"].Size(); i++)
        {
            std::vector<double> row;
            if (r.Arr(rs["moves"][i], "rules.moves"))
                for (std::size_t g = 0; g < rs["moves"][i].Size(); g++)
                {
                    double a = 0.0;
                    r.Num(rs["moves"][i][g], "rules.moves", a);
                    row.push_back(a);
                }
            o.rsMoves.push_back(row);
        }

    const json::Value& ex = v["experiment"];
    if (!ex["mode"].IsNull())
    {
        std::string m;
        r.Text(ex["mode"], "experiment.mode", m);
        if (m == "record" || m == "files")
            o.mode = m == "record" ? 1 : 0;
        else
            r.Bad("experiment.mode", "ожидается \"record\" или \"files\"");
    }
    r.Flag(ex["mode_manual"], "experiment.mode_manual", o.modeManual);
    r.Text(ex["record_file"], "experiment.record_file", o.recordFile);
    r.Flag(ex["positions_edited"], "experiment.positions_edited", o.positionsEdited);
    if (r.Arr(ex["positions"], "experiment.positions"))
        for (std::size_t i = 0; i < ex["positions"].Size(); i++)
        {
            const json::Value& q = ex["positions"][i];
            heel::Position pos;
            r.Num(q["t0"], "experiment.positions.t0", pos.t0);
            r.Num(q["t1"], "experiment.positions.t1", pos.t1);
            r.Flag(q["use"], "experiment.positions.use", pos.use);
            r.Num(q["arm"], "experiment.positions.arm", pos.arm, true);
            if (r.err.empty() && !(std::isfinite(pos.t0) && std::isfinite(pos.t1)))
                r.Bad("experiment.positions", "у положения нет границ t0 / t1");
            o.positions.push_back(pos);
        }
    if (r.Arr(ex["arms"], "experiment.arms"))
        for (std::size_t i = 0; i < ex["arms"].Size(); i++)
        {
            int n = 0;
            double a = heel::kNaN;
            r.Int(ex["arms"][i]["n"], "experiment.arms.n", n, -1000000, 1000000);
            r.Num(ex["arms"][i]["arm"], "experiment.arms.arm", a);
            if (std::isfinite(a))
                o.arms[n] = a;
        }
    if (r.Arr(ex["excluded"], "experiment.excluded"))
        for (std::size_t i = 0; i < ex["excluded"].Size(); i++)
        {
            int n = 0;
            r.Int(ex["excluded"][i], "experiment.excluded", n, -1000000, 1000000);
            o.excluded[n] = true;
        }
    if (r.Arr(ex["rs_manual"], "experiment.rs_manual"))
        for (std::size_t i = 0; i < ex["rs_manual"].Size(); i++)
        {
            int n = 0;
            bool use = true;
            r.Int(ex["rs_manual"][i]["n"], "experiment.rs_manual.n", n, -1000000, 1000000);
            r.Flag(ex["rs_manual"][i]["use"], "experiment.rs_manual.use", use);
            o.rsManual[n] = use ? 1 : 0;
        }
    const json::Value& res = v["result"];
    r.Num(res["h"], "result.h", o.h, true);
    r.Num(res["h_main"], "result.h_main", o.hMain, true);
    if (!r.err.empty())
        return fail(r.err);
    p = std::move(o);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Файл
// ---------------------------------------------------------------------------------------------------------------

// Сохранить: пути записей — относительно папки проекта, если рядом; запись — во временный файл и замена.
inline bool Save(const std::filesystem::path& file, Project p, std::string* err = nullptr)
{
    const std::filesystem::path dir = file.parent_path();
    p.folderRel = RelativeTo(dir, p.folder); // та же папка — «.»
    p.filesRel.clear();
    for (const auto& f : p.files)
        p.filesRel.push_back(RelativeTo(dir, f));
    const std::string body = ToJson(p, true);
    std::error_code ec;
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);
    std::filesystem::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (err)
                *err = "не удалось создать файл (папка только для чтения?)";
            return false;
        }
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out)
        {
            if (err)
                *err = "ошибка записи (диск полон?)";
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        if (err)
            *err = "не удалось заменить файл (открыт в другой программе?)";
        return false;
    }
    return true;
}

// Файл записи не нашёлся ни рядом с проектом, ни по полному пути: ищется в папке проекта, в папке записей (по
// относительному и полному пути) файл с тем же именем без приписок копии и регистра (heel::RecordNameKey). Один
// такой — он; ни одного или несколько — пусто.
inline std::string FindSimilar(const std::filesystem::path& projectDir, const std::string& rel, const std::string& abs,
                               std::string* ambiguous = nullptr)
{
    const std::string name = text::PathToUtf8(text::PathFromUtf8(!abs.empty() ? abs : rel).filename());
    if (name.empty())
        return {};
    const std::string key = heel::RecordNameKey(name);
    std::vector<std::filesystem::path> dirs = {projectDir};
    if (!rel.empty())
        dirs.push_back((projectDir / text::PathFromUtf8(rel)).parent_path().lexically_normal());
    if (!abs.empty())
        dirs.push_back(text::PathFromUtf8(abs).parent_path());
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    for (const auto& d : dirs)
        for (std::filesystem::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || heel::RecordNameKey(text::PathToUtf8(it->path().filename())) != key)
                continue;
            bool dup = false;
            for (const auto& f : found)
                dup |= std::filesystem::equivalent(f, it->path(), ec);
            if (!dup)
                found.push_back(it->path());
        }
    if (found.size() == 1)
        return text::PathToUtf8(found.front());
    if (ambiguous && found.size() > 1)
    {
        *ambiguous = name + " — похожих несколько:";
        for (const auto& f : found)
            *ambiguous += " " + text::PathToUtf8(f.filename()) + ";";
        ambiguous->pop_back();
    }
    return {};
}

// Прочитать и найти записи (folder / files — полные пути). Файлы, не найденные по пути, ищутся по похожему имени
// (FindSimilar: « (1)», « - копия», регистр) — найденные так — в p.notes. Не нашлись — false, err и p.missing
// (проект в p всё равно разобран: записи можно указать вручную).
inline bool Load(const std::filesystem::path& file, Project& p, std::string* err = nullptr)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        if (err)
            *err = "файл не открывается (нет такого файла?)";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() > (32u << 20))
    {
        if (err)
            *err = "файл слишком большой для проекта";
        return false;
    }
    Project o;
    if (!FromJson(text, o, err))
        return false;
    const std::filesystem::path dir = file.parent_path();
    if (!o.files.empty())
    {
        std::vector<std::string> found;
        std::string why;
        for (std::size_t i = 0; i < o.files.size(); i++)
        {
            const std::string rel = i < o.filesRel.size() ? o.filesRel[i] : std::string();
            std::string f = Resolve(dir, rel, o.files[i]);
            if (f.empty())
            {
                std::string amb;
                f = FindSimilar(dir, rel, o.files[i], &amb);
                const std::string want = text::PathToUtf8(text::PathFromUtf8(o.files[i].empty() ? rel : o.files[i]).filename());
                if (!f.empty())
                    o.notes.push_back(want + " не найден — взят " + text::PathToUtf8(text::PathFromUtf8(f).filename()) +
                                      " (то же имя с припиской копии или в другом регистре)");
                else
                {
                    o.missing.push_back(want);
                    why += (why.empty() ? "" : "; ") + (amb.empty() ? want : amb);
                }
            }
            found.push_back(f);
        }
        if (!o.missing.empty())
        {
            if (err)
                *err = "файлы записей не найдены ни рядом с проектом, ни по полному пути, ни по похожему имени: " + why;
            o.files = found;
            p = std::move(o);
            return false;
        }
        o.files = found;
        o.folder = text::PathToUtf8(text::PathFromUtf8(o.files.front()).parent_path());
    }
    else
    {
        const std::string f = Resolve(dir, o.folderRel, o.folder);
        if (f.empty())
        {
            if (err)
                *err = "папка записей не найдена: " +
                       (o.folderRel.empty() ? o.folder : o.folderRel + " рядом с проектом (или " + o.folder + ")");
            return false;
        }
        o.folder = f;
    }
    p = std::move(o);
    return true;
}

} // namespace proj
