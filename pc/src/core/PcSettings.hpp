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
            if (!out)
                return;
        }
        std::filesystem::rename(tmp, file, ec);
    }
};
