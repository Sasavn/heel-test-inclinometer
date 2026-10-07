#pragma once
// Запуск внешней программы (dfu-util) без окна консоли, вывод stdout+stderr построчно (\r и \n — концы строк:
// dfu-util рисует прогресс через \r). Чтение трубы — в своём потоке, интерфейс забирает строки TakeLines().
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../core/TextUtil.hpp"

namespace fw
{

class IProcess
{
public:
    virtual ~IProcess() = default;
    virtual bool Running() = 0;
    virtual int ExitCode() = 0;              // после завершения
    virtual std::vector<std::string> TakeLines() = 0;
    virtual void Kill() = 0;
};

#ifdef _WIN32

class WinProcess final : public IProcess
{
public:
    // exe и аргументы — UTF-8; аргументы с пробелами берутся в кавычки.
    static std::unique_ptr<WinProcess> Start(const std::string& exe, const std::vector<std::string>& args, std::string* err)
    {
        std::wstring cmd = Quote(text::Widen(exe));
        for (const auto& a : args)
            cmd += L" " + Quote(text::Widen(a));

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE rd = nullptr, wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0))
        {
            if (err)
                *err = "CreatePipe: ошибка " + std::to_string(GetLastError());
            return nullptr;
        }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nul;
        si.hStdOutput = wr;
        si.hStdError = wr;
        PROCESS_INFORMATION pi{};
        std::wstring mutableCmd = cmd;
        const BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                       nullptr, &si, &pi);
        const DWORD e = GetLastError();
        CloseHandle(wr);
        if (nul != INVALID_HANDLE_VALUE)
            CloseHandle(nul);
        if (!ok)
        {
            CloseHandle(rd);
            if (err)
                *err = "не удалось запустить " + exe + " (ошибка " + std::to_string(e) + ")";
            return nullptr;
        }
        CloseHandle(pi.hThread);
        auto p = std::unique_ptr<WinProcess>(new WinProcess());
        p->proc_ = pi.hProcess;
        p->pipe_ = rd;
        p->reader_ = std::thread(&WinProcess::Read, p.get());
        return p;
    }

    ~WinProcess() override
    {
        if (Running())
            Kill();
        if (reader_.joinable())
            reader_.join();
        if (pipe_)
            CloseHandle(pipe_);
        if (proc_)
            CloseHandle(proc_);
    }

    bool Running() override { return proc_ && WaitForSingleObject(proc_, 0) == WAIT_TIMEOUT; }

    int ExitCode() override
    {
        DWORD code = 0;
        if (proc_ && GetExitCodeProcess(proc_, &code))
            return static_cast<int>(code);
        return -1;
    }

    std::vector<std::string> TakeLines() override
    {
        std::lock_guard lock(m_);
        std::vector<std::string> out;
        out.swap(lines_);
        // Процесс завершился и труба дочитана — хвост без перевода строки тоже отдать.
        if (eof_ && !partial_.empty())
        {
            out.push_back(partial_);
            partial_.clear();
        }
        return out;
    }

    void Kill() override
    {
        if (proc_)
            TerminateProcess(proc_, 1);
    }

private:
    WinProcess() = default;

    static std::wstring Quote(const std::wstring& a)
    {
        if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos)
            return a;
        std::wstring q = L"\"";
        for (wchar_t c : a)
        {
            if (c == L'"')
                q += L'\\';
            q += c;
        }
        return q + L"\"";
    }

    void Read()
    {
        char buf[512];
        DWORD n = 0;
        while (ReadFile(pipe_, buf, sizeof(buf), &n, nullptr) && n > 0)
        {
            std::lock_guard lock(m_);
            for (DWORD i = 0; i < n; i++)
            {
                const char c = buf[i];
                if (c == '\r' || c == '\n')
                {
                    if (!partial_.empty())
                        lines_.push_back(text::AcpToUtf8(partial_));
                    partial_.clear();
                }
                else
                    partial_ += c;
            }
        }
        std::lock_guard lock(m_);
        eof_ = true;
    }

    HANDLE proc_ = nullptr;
    HANDLE pipe_ = nullptr;
    std::thread reader_;
    std::mutex m_;
    std::vector<std::string> lines_;
    std::string partial_;
    bool eof_ = false;
};

#endif

} // namespace fw
