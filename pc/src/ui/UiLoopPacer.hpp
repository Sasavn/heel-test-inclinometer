#pragma once
// Bound rendering independently of VSync and keep servicing
// requests/recording even when no frame is due or the window is minimized.
#include <algorithm>
#include <chrono>

namespace ui
{

class UiLoopPacer
{
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    static constexpr auto kFrameInterval = std::chrono::ceil<Clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
    static constexpr auto kServiceInterval = std::chrono::milliseconds(10);

    explicit UiLoopPacer(Time now) : serviceAt_(now), frameAt_(now) {}

    bool ServiceDue(Time now) const { return now >= serviceAt_; }
    bool FrameDue(Time now, bool iconified) const { return !iconified && now >= frameAt_; }

    // Start-to-start deadlines: slow work consumes the budget, without a burst
    // of catch-up iterations afterwards. Input never resets these deadlines.
    void ServiceStarted(Time now) { serviceAt_ = now + kServiceInterval; }
    void FrameStarted(Time now) { frameAt_ = now + kFrameInterval; }

    double WaitSeconds(Time now, bool iconified) const
    {
        const auto deadline = iconified ? serviceAt_ : std::min(serviceAt_, frameAt_);
        return deadline > now ? std::chrono::duration<double>(deadline - now).count() : 0.0;
    }

private:
    Time serviceAt_, frameAt_;
};

} // namespace ui
