#pragma once
// Настройки программы на ПК (не прибора): папка для скачивания, окно графика, период потока, тема, последний образ
// прошивки, выбранный порт. Файл %APPDATA%\Krenomer\krenomer.ini, строки «ключ=значение» в UTF-8.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>

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
    double fltFc = 0.5, fltTc = 0.0, fltSgWin = 4.0;
    bool fltWhole = false;

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
        fltMedianN = num("flt_median", fltMedianN, 1, 99);
        fltFc = real("flt_fc", fltFc, 0.001, 100.0);
        fltTc = real("flt_tc", fltTc, 0.0, 120.0);
        fltSgWin = real("flt_sg_win", fltSgWin, 0.2, 60.0);
        fltWhole = flag("flt_whole", fltWhole);
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
            out << "flt_v=2\nflt_user=" << fltUser << "\nsensor_names=" << sensorNames << "\n";
            out << "flt_method=" << fltMethod << "\nflt_median=" << fltMedianN << "\nflt_fc=" << fltFc << "\nflt_tc=" << fltTc
                << "\nflt_sg_win=" << fltSgWin << "\nflt_whole=" << (fltWhole ? 1 : 0) << "\n";
            if (!out)
                return;
        }
        std::filesystem::rename(tmp, file, ec);
    }
};
