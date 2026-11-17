#pragma once
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace ui
{
namespace detail
{
// Injectable API table for failure-path tests; normal callers use System().
struct WindowsWaitApi
{
    using CreateEx = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
    CreateEx createEx = nullptr;
    decltype(&CreateWaitableTimerW) create = &CreateWaitableTimerW;
    decltype(&SetWaitableTimer) arm = &SetWaitableTimer;
    decltype(&CancelWaitableTimer) cancel = &CancelWaitableTimer;
    decltype(&MsgWaitForMultipleObjectsEx) wait = &MsgWaitForMultipleObjectsEx;
    decltype(&CloseHandle) close = &CloseHandle;
    decltype(&Sleep) sleep = &Sleep;

    static WindowsWaitApi System()
    {
        WindowsWaitApi api;
        if (HMODULE kernel = GetModuleHandleW(L"kernel32.dll"))
        {
#ifdef _MSC_VER
#pragma warning(suppress : 4191)
#endif
            api.createEx = reinterpret_cast<CreateEx>(GetProcAddress(kernel, "CreateWaitableTimerExW"));
        }
        return api;
    }
};
} // namespace detail

// UI-thread-only wait. It observes only the calling thread's message queue;
// the caller must dispatch callbacks with glfwPollEvents() after each return.
// No message is removed here, no APC callback and no system timer-resolution change.
class WindowsEventWait
{
public:
    enum class Mode { HighResolution, StandardTimer, MessagesOnly };
    explicit WindowsEventWait(detail::WindowsWaitApi api = detail::WindowsWaitApi::System()) : api_(api)
    {
        if (api_.createEx)
        {
            // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION: supported since Win10 1803.
            timer_ = api_.createEx(nullptr, nullptr, 0x2, TIMER_MODIFY_STATE | SYNCHRONIZE);
            if (timer_) mode_ = Mode::HighResolution;
            else timer_ = api_.createEx(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
        if (!timer_ && api_.create) timer_ = api_.create(nullptr, FALSE, nullptr);
        if (timer_ && mode_ != Mode::HighResolution) mode_ = Mode::StandardTimer;
    }
    ~WindowsEventWait() { CloseTimer(); }
    WindowsEventWait(const WindowsEventWait&) = delete;
    WindowsEventWait& operator=(const WindowsEventWait&) = delete;

    Mode TimerMode() const { return mode_; }

    void Wait(double seconds)
    {
        if (!(seconds > 0.0) || !std::isfinite(seconds)) return;
        // Renderer waits at most 10 ms; also keep malformed requests and API
        // failure paths finite. Ceil prevents the old sub-ms -> DWORD(0) spin.
        seconds = (std::min)(seconds, 1.0);
        const DWORD timeout = (std::max)(DWORD(1), static_cast<DWORD>(std::ceil(seconds * 1000.0)));
        if (timer_)
        {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>((std::max)(1.0, std::ceil(seconds * 10000000.0)));
            if (api_.arm(timer_, &due, 0, nullptr, nullptr, FALSE))
            {
                const DWORD result = api_.wait(1, &timer_, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                api_.cancel(timer_);
                if (result != WAIT_FAILED) return;
            }
            // Disable a broken timer once, instead of retrying in a hot loop.
            CloseTimer();
        }
        if (api_.wait(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_FAILED)
        {
            // Last resort only if the message-wait API itself fails: a bounded
            // deadline-derived delay, then the caller polls again. Never spin.
            api_.sleep(timeout);
        }
    }

private:
    void CloseTimer()
    {
        if (timer_) { api_.close(timer_); timer_ = nullptr; }
        mode_ = Mode::MessagesOnly;
    }
    detail::WindowsWaitApi api_;
    HANDLE timer_ = nullptr;
    Mode mode_ = Mode::MessagesOnly;
};
} // namespace ui
#endif
