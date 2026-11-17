// Deterministic scheduling checks; platform waits are covered separately.
#include "../src/ui/UiLoopPacer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Pacer = ui::UiLoopPacer;
using namespace std::chrono_literals;

static void Check(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    // Use an arbitrary long-running steady-clock origin, not wall clock/uptime 0.
    const Pacer::Time origin{std::chrono::duration_cast<Pacer::Clock::duration>(24h * 1000)};
    Pacer p(origin);
    Check(p.ServiceDue(origin) && p.FrameDue(origin, false), "startup is immediately due");
    Check(!p.FrameDue(origin, true), "minimized startup never draws");
    p.ServiceStarted(origin);
    p.FrameStarted(origin);
    Check(std::abs(p.WaitSeconds(origin, false) - 0.010) < 1e-9, "service wakes before next frame");
    Check(!p.ServiceDue(origin + 9ms) && !p.FrameDue(origin + 9ms, false), "early input cannot bypass deadlines");
    Check(p.ServiceDue(origin + 10ms), "services do not depend on redraw or input");
    p.ServiceStarted(origin + 10ms);
    Check(!p.FrameDue(origin + Pacer::kFrameInterval - Pacer::Clock::duration(1), false), "no frame before cap");
    Check(p.FrameDue(origin + Pacer::kFrameInterval, false), "frame due exactly at deadline");
    Check(p.WaitSeconds(origin + 1s, false) == 0.0, "overdue work never requests an invalid timeout");

    // No catch-up after a long draw/swap, oversleep, suspend, or delayed service.
    const auto late = origin + 5s;
    p.ServiceStarted(late);
    p.FrameStarted(late);
    Check(!p.ServiceDue(late) && !p.FrameDue(late, false), "late work rebases both deadlines");
    Check(!p.FrameDue(late + 1ms, false), "late frame cannot trigger catch-up burst");
    Check(std::abs(p.WaitSeconds(late, true) - 0.010) < 1e-9, "iconified wait ignores visible frame deadline");

    // Event storm: events arrive every 0.5 ms, with non-blocking (zero-cost)
    // swap. Mode changes do not reset deadlines; services remain independent.
    std::vector<Pacer::Time> frames, services;
    p = Pacer(origin);
    int minimizedFrames = 0;
    bool closeReceived = false;
    for (int us = 0; us <= 3'000'000; us += 500)
    {
        const auto now = origin + std::chrono::microseconds(us);
        const bool iconified = us >= 1'000'000 && us < 2'000'000;
        // This models the Renderer order: process event/close first, then due work.
        if (us == 3'000'000) closeReceived = true;
        if (closeReceived) break;
        if (p.ServiceDue(now)) { p.ServiceStarted(now); services.push_back(now); }
        if (p.FrameDue(now, iconified))
        {
            p.FrameStarted(now);
            frames.push_back(now);
            if (iconified) ++minimizedFrames;
        }
        const double wait = p.WaitSeconds(now, iconified);
        Check(std::isfinite(wait) && wait >= 0.0 && wait <= 0.010000001, "finite bounded event wait");
    }
    Check(closeReceived, "close processed even between deadlines");
    Check(minimizedFrames == 0, "minimized mode renders no frames");
    Check(services.size() == 300, "service cadence preserved throughout minimize/restore");
    for (std::size_t i = 1; i < services.size(); ++i)
        Check(services[i] - services[i - 1] == 10ms, "events cannot cause extra service work");
    for (std::size_t i = 1; i < frames.size(); ++i)
        Check(frames[i] - frames[i - 1] >= Pacer::kFrameInterval, "60 Hz cap holds without VSync");
    Check(frames.size() >= 116 && frames.size() <= 120, "active UI continues near 60 Hz with event quantization");

    // No events: advance fake time by precisely the requested timeout. This
    // must wake for both state machines without needing a mouse/keyboard event.
    p = Pacer(origin);
    auto now = origin;
    int idleServices = 0, idleFrames = 0;
    while (now < origin + 1s)
    {
        if (p.ServiceDue(now)) { p.ServiceStarted(now); ++idleServices; }
        if (p.FrameDue(now, false)) { p.FrameStarted(now); ++idleFrames; }
        const double wait = p.WaitSeconds(now, false);
        Check(wait > 0.0 && wait <= 0.010000001, "idle loop sleeps instead of spinning");
        now += std::chrono::ceil<Pacer::Clock::duration>(std::chrono::duration<double>(wait));
    }
    Check(idleServices == 100 && idleFrames == 60, "timer-only operation keeps service and plot cadence");
    std::printf("PASS: deadlines, zero-cost swap, event storm, late frames, minimize/restore, idle timers (%d services, %d frames/s)\n",
                idleServices, idleFrames);
}
