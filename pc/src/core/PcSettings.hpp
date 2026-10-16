#pragma once
// Настройки программы на ПК (не прибора): папка для скачивания, окно графика, период потока, тема, последний образ
// прошивки, выбранный порт. Файл %APPDATA%\Krenomer\krenomer.ini, строки «ключ=значение» в UTF-8.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "TextUtil.hpp"

#ifdef _WIN32
#include <shlobj.h>
#endif

struct PcSettings
{
    std::string downloadDir;  // "" — Документы\Регистратор крена
    int chartWindowS = 60;    // окно графиков, с
    int streamMs = 100;       // период потока stream, мс
    bool darkTheme = false;
    std::string firmwarePath; // последний выбранный .bin
    std::string port;         // "" — автоматически (0483:5740)
    bool overwriteFiles = false;
    bool termShowStream = false;
    bool termShowPolls = false;
    std::string recordDir;    // «Запись на ПК»: "" — та же папка, что для файлов с карты
    bool recordXlsx = true;   // «Запись на ПК»: также книга .xlsx по остановке
    // «Обработка»: папка ("" — папка файлов с карты) и исходные данные опыта кренования
    std::string procDir;
    double heelD = 1500.0, heelP = 15.0, heelL = 4.5, heelWinS = 30.0, heelThr = 0.1; // т, т, м, с, °
    int heelAxis = 0;         // ось угла: 0 — X (датчики поперёк судна), 1 — Y
    double heelPlateauS = 10.0, heelStep = 0.5; // непрерывная запись: положение не короче, с; перепад не меньше, °
    bool heelSmall = false;   // малое судно (модель): D и P — кг, плечи — мм (heelD/heelP/heelL — в этих единицах)
    // Знаков после запятой в полях: D, P, плечи (не меньше 3), порог, шаг
    int digD = 1, digP = 2, digArm = 3, digThr = 2, digStep = 2;
    // Обработка сигнала положения (core/Filters.hpp)
    int fltMethod = 3, fltMedianN = 3; // метод: 0…4 встроенные (3 — Калман, по умолчанию), 5 — свой (fltUser)
    std::string fltUser;      // имя выбранного своего метода (методы — файлы в Dir()/methods)
    std::string sensorNames;  // названия датчиков: «ключ=название;…» (ключ — heel::SensorKey)
    std::string sensorInvert; // датчики с обратным знаком (ось +X — к левому борту): «ключ,ключ,…»
    double fltFc = 0.5, fltTc = 0.0, fltSgWin = 4.0;
    bool fltWhole = false;
    // Правила РС: h и l_max по Информации об остойчивости (плечевые единицы), Δ0 порожнем (единицы массы); 0 — не
    // задано. Порог сверки постов, °. Основной расчёт h: 0 — от начального + МНК, 1 — по приращениям (табл. 6.5).
    double rsH = 0.0, rsLmax = 0.0, rsD0 = 0.0;
    double rsSpread = 0.1;   // сверка приборов: расхождение приращений Δθ на замере, ° (rs_spread_step)
    int rsMain = 0;
    int rsMainGroups = -1;   // основной расчёт в режиме групп: -1 — не выбран (табл. 6.5)
    double procSplit = 0.56; // «Обработка»: доля высоты графиков над карточкой с вкладками (делитель)
    int uiDensity = 0;       // интерфейс: 0 — авто (компактный, если окно ниже 800 точек), 1 — обычный, 2 — компактный
    // Проект опыта (*.krenomer): автосохранение рядом с записями и недавние проекты (последний — первым)
    bool projectAutosave = true;
    std::vector<std::string> recentProjects;
    static constexpr std::size_t kRecentMax = 8;
    int rsInstrument = -1;   // табл. 6.5: Δθ — среднее приборов (-1) или пост 0…3
    double rsDh = 0.0;       // поправка на свободные поверхности (плечевые единицы)
    double rsDworst = 0.0;   // Δ наихудшего варианта загрузки (единицы массы); 0 — D
    bool rsGroupMode = false; // крен-балласт группами (таблица перемещений)
    std::string rsGroups;    // группы: «название=масса;…» (масса — в единицах массы)

    static std::filesystem::path Dir()
    {
#ifdef _WIN32
        wchar_t buf[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf)))
            return std::filesystem::path(buf) / L"Krenomer";
#endif
        return std::filesystem::path(".");
    }

    static std::string DefaultDownloadDir()
    {
#ifdef _WIN32
        wchar_t buf[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buf)))
            return text::PathToUtf8(std::filesystem::path(buf) / L"Регистратор крена");
#endif
        return "downloads";
    }

    std::string DownloadDirOrDefault() const { return downloadDir.empty() ? DefaultDownloadDir() : downloadDir; }
    std::string RecordDirOrDefault() const { return recordDir.empty() ? DownloadDirOrDefault() : recordDir; }

    void Load(const std::filesystem::path& file = Dir() / L"krenomer.ini")
    {
        std::ifstream in(file);
        std::string line;
        std::map<std::string, std::string> kv;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto eq = line.find('=');
            if (eq != std::string::npos)
                kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
        auto num = [&](const char* k, int def, int lo, int hi) {
            const auto it = kv.find(k);
            if (it == kv.end())
                return def;
            const int v = std::atoi(it->second.c_str());
            return v < lo || v > hi ? def : v;
        };
        auto flag = [&](const char* k, bool def) {
            const auto it = kv.find(k);
            return it == kv.end() ? def : it->second == "1";
        };
        if (kv.count("download_dir"))
            downloadDir = kv["download_dir"];
        if (kv.count("firmware"))
            firmwarePath = kv["firmware"];
        if (kv.count("port"))
            port = kv["port"];
        chartWindowS = num("chart_window_s", chartWindowS, 10, 600);
        streamMs = num("stream_ms", streamMs, 50, 1000);
        darkTheme = flag("dark_theme", darkTheme);
        overwriteFiles = flag("overwrite_files", overwriteFiles);
        termShowStream = flag("term_show_stream", termShowStream);
        termShowPolls = flag("term_show_polls", termShowPolls);
        if (kv.count("record_dir"))
            recordDir = kv["record_dir"];
        recordXlsx = flag("record_xlsx", recordXlsx);
        auto real = [&](const char* k, double def, double lo, double hi) {
            const auto it = kv.find(k);
            if (it == kv.end())
                return def;
            const double v = std::strtod(it->second.c_str(), nullptr);
            return v < lo || v > hi ? def : v;
        };
        if (kv.count("proc_dir"))
            procDir = kv["proc_dir"];
        heelD = real("heel_d", heelD, 0.001, 1e7);
        heelP = real("heel_p", heelP, 0.0, 1e6);
        heelL = real("heel_l", heelL, 0.0, 1000.0);
        heelWinS = real("heel_window_s", heelWinS, 1.0, 3600.0);
        heelThr = real("heel_threshold", heelThr, 0.0, 10.0);
        // Ось: только выбор, сохранённый программой 1.3+ (heel_axis_v=2). Программа 1.2 сохраняла ось лишь при
        // закрытии окна и без отметки версии; такое значение не берётся — по умолчанию X.
        if (num("heel_axis_v", 0, 0, 100) >= 2)
            heelAxis = num("heel_axis", heelAxis, 0, 1);
        heelPlateauS = real("heel_plateau_s", heelPlateauS, 2.0, 600.0);
        heelStep = real("heel_step", heelStep, 0.05, 20.0);
        heelSmall = flag("heel_small", heelSmall);
        digD = num("dig_d", digD, 0, 6);
        digP = num("dig_p", digP, 0, 6);
        digArm = num("dig_arm", digArm, 3, 6);
        digThr = num("dig_thr", digThr, 0, 6);
        digStep = num("dig_step", digStep, 0, 6);
        if (num("flt_v", 0, 0, 100) >= 2) // до 1.4 по умолчанию был метод статьи — старый выбор не берётся
            fltMethod = num("flt_method", fltMethod, 0, 5);
        if (kv.count("flt_user"))
            fltUser = kv["flt_user"];
        if (kv.count("sensor_names"))
            sensorNames = kv["sensor_names"];
        if (kv.count("sensor_invert"))
            sensorInvert = kv["sensor_invert"];
        fltMedianN = num("flt_median", fltMedianN, 1, 99);
        fltFc = real("flt_fc", fltFc, 0.001, 100.0);
        fltTc = real("flt_tc", fltTc, 0.0, 120.0);
        fltSgWin = real("flt_sg_win", fltSgWin, 0.2, 60.0);
        fltWhole = flag("flt_whole", fltWhole);
        rsH = real("rs_h", rsH, 0.0, 1e6);
        rsLmax = real("rs_lmax", rsLmax, 0.0, 1e7);
        rsD0 = real("rs_d0", rsD0, 0.0, 1e9);
        rsSpread = real("rs_spread_step", rsSpread, 0.001, 10.0); // до 1.4 — по уровням (rs_spread), не берётся
        rsMain = num("rs_main", rsMain, 0, 1);
        rsMainGroups = num("rs_main_groups", rsMainGroups, -1, 1);
        procSplit = real("proc_split", procSplit, 0.25, 0.85);
        uiDensity = num("ui_density", uiDensity, 0, 2);
        rsInstrument = num("rs_instrument", rsInstrument, -1, 3);
        rsDh = real("rs_dh", rsDh, -1e6, 1e6);
        rsDworst = real("rs_dworst", rsDworst, 0.0, 1e9);
        rsGroupMode = flag("rs_group_mode", rsGroupMode);
        if (kv.count("rs_groups"))
            rsGroups = kv["rs_groups"];
        projectAutosave = flag("project_autosave", projectAutosave);
        if (kv.count("recent_projects"))
        {
            recentProjects.clear();
            const std::string& r = kv["recent_projects"];
            std::size_t a = 0;
            while (a < r.size() && recentProjects.size() < kRecentMax)
            {
                std::size_t b = r.find('|', a);
                if (b == std::string::npos)
                    b = r.size();
                if (b > a)
                    recentProjects.push_back(r.substr(a, b - a));
                a = b + 1;
            }
        }
    }

    // Проект — первым в списке недавних (без повторов, не больше kRecentMax).
    void AddRecentProject(const std::string& path)
    {
        if (path.empty())
            return;
        for (std::size_t i = 0; i < recentProjects.size(); i++)
            if (recentProjects[i] == path)
            {
                recentProjects.erase(recentProjects.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        recentProjects.insert(recentProjects.begin(), path);
        if (recentProjects.size() > kRecentMax)
            recentProjects.resize(kRecentMax);
    }

    void Save(const std::filesystem::path& file = Dir() / L"krenomer.ini") const
    {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        // Во временный файл рядом и замена: оборванная запись не портит прежние настройки.
        std::filesystem::path tmp = file;
        tmp += L".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out)
                return;
            out << "download_dir=" << downloadDir << "\n";
            out << "firmware=" << firmwarePath << "\n";
            out << "port=" << port << "\n";
            out << "chart_window_s=" << chartWindowS << "\n";
            out << "stream_ms=" << streamMs << "\n";
            out << "dark_theme=" << (darkTheme ? 1 : 0) << "\n";
            out << "overwrite_files=" << (overwriteFiles ? 1 : 0) << "\n";
            out << "term_show_stream=" << (termShowStream ? 1 : 0) << "\n";
            out << "term_show_polls=" << (termShowPolls ? 1 : 0) << "\n";
            out << "record_dir=" << recordDir << "\n";
            out << "record_xlsx=" << (recordXlsx ? 1 : 0) << "\n";
            out << "proc_dir=" << procDir << "\n";
            out.precision(12);
            out << "heel_d=" << heelD << "\nheel_p=" << heelP << "\nheel_l=" << heelL << "\n";
            out << "heel_window_s=" << heelWinS << "\nheel_threshold=" << heelThr << "\nheel_axis=" << heelAxis << "\n";
            out << "heel_axis_v=2\nheel_plateau_s=" << heelPlateauS << "\nheel_step=" << heelStep << "\n";
            out << "heel_small=" << (heelSmall ? 1 : 0) << "\ndig_d=" << digD << "\ndig_p=" << digP << "\ndig_arm=" << digArm
                << "\ndig_thr=" << digThr << "\ndig_step=" << digStep << "\n";
            out << "flt_v=2\nflt_user=" << fltUser << "\nsensor_names=" << sensorNames << "\nsensor_invert=" << sensorInvert << "\n";
            out << "flt_method=" << fltMethod << "\nflt_median=" << fltMedianN << "\nflt_fc=" << fltFc << "\nflt_tc=" << fltTc
                << "\nflt_sg_win=" << fltSgWin << "\nflt_whole=" << (fltWhole ? 1 : 0) << "\n";
            out << "rs_h=" << rsH << "\nrs_lmax=" << rsLmax << "\nrs_d0=" << rsD0 << "\nrs_spread_step=" << rsSpread
                << "\nrs_main=" << rsMain << "\nrs_main_groups=" << rsMainGroups << "\nproc_split=" << procSplit << "\nui_density=" << uiDensity << "\nrs_instrument=" << rsInstrument << "\nrs_dh=" << rsDh
                << "\nrs_dworst=" << rsDworst << "\nrs_group_mode=" << (rsGroupMode ? 1 : 0) << "\nrs_groups=" << rsGroups
                << "\n";
            out << "project_autosave=" << (projectAutosave ? 1 : 0) << "\nrecent_projects=";
            for (std::size_t i = 0; i < recentProjects.size(); i++)
                out << (i ? "|" : "") << recentProjects[i];
            out << "\n";
            if (!out)
                return;
        }
        std::filesystem::rename(tmp, file, ec);
    }
};
