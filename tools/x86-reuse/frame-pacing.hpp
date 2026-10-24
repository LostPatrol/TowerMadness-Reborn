// Pace native presentation without changing the original simulation clock.
// Included after the host logger; uses the host's existing Windows/STL headers.
#pragma once

class HostFramePacer {
    // Report cadence and stall threshold are presentation diagnostics only.
    static constexpr double ReportSeconds = 5.0, LongFrameMs = 50.0;
    // This process-local timer flag is supported since Windows 10 version 1803.
    static constexpr DWORD HighResolutionTimer = 0x00000002;
    // Leave a short scheduler-yield tail to avoid rounding the timer past a deadline.
    static constexpr double YieldTailSeconds = 0.00025;
    HANDLE timer_ = nullptr;
    std::FILE* trace_ = nullptr;
    double frequency_ = 1.0, started_ = 0.0, previous_ = 0.0, windowStart_ = 0.0, deadline_ = 0.0;
    double recent_ = 0.0;
    unsigned long long totalFrames_ = 0;
    int target_ = 60;
    std::vector<double> samples_;

    /** Read the monotonic host counter without touching the original game clock. */
    static double counter() {
        LARGE_INTEGER value{}; QueryPerformanceCounter(&value);
        return static_cast<double>(value.QuadPart);
    }

    /** Wait only for remaining frame time, using a process-owned timer when available. */
    void waitUntil(double deadline) {
        for (;;) {
            const double remaining = (deadline - counter()) / frequency_;
            if (remaining <= 0.0) return;
            if (remaining > YieldTailSeconds && timer_) {
                LARGE_INTEGER due{};
                due.QuadPart = -static_cast<LONGLONG>((remaining - YieldTailSeconds) * 10000000.0);
                if (due.QuadPart < 0 && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                    WaitForSingleObject(timer_, INFINITE);
                    continue;
                }
            }
            // Ordinary Sleep is only a fallback; retain the final millisecond for yielding.
            if (remaining > 0.002) Sleep(static_cast<DWORD>(remaining * 1000.0 - 1.0));
            else if (!SwitchToThread()) YieldProcessor();
        }
    }

public:
    /** Read optional presentation settings and allocate a local high-resolution wait timer. */
    HostFramePacer() {
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        frequency_ = static_cast<double>(frequency.QuadPart);
        started_ = previous_ = windowStart_ = counter();
        // Match optional frame CSV times to resource and screenshot spans on the same monotonic clock.
        log("FRAME_CLOCK qpcStart=" + std::to_string(static_cast<LONGLONG>(started_))
            + " qpcFrequency=" + std::to_string(static_cast<LONGLONG>(frequency_)));
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, HighResolutionTimer, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        int requested = 60;
        if (const char* setting = std::getenv("TOWERMADNESS_FPS")) {
            char* end = nullptr;
            const long parsed = std::strtol(setting, &end, 10);
            if (end != setting && *end == '\0' && parsed >= 0 && parsed <= (std::numeric_limits<int>::max)())
                requested = static_cast<int>(parsed);
            else log("FRAME_PACING_WARNING Invalid TOWERMADNESS_FPS; using 60.");
        }
        if (const char* path = std::getenv("TOWERMADNESS_FRAME_TRACE")) {
            if (*path) {
                trace_ = std::fopen(path, "w");
                if (trace_) std::fprintf(trace_, "frame,hostSeconds,frameMs,targetFPS\n");
                else log("FRAME_PACING_WARNING Could not open TOWERMADNESS_FRAME_TRACE.");
            }
        }
        samples_.reserve(4096);
        configure(requested);
    }

    /** Flush the final partial statistics window and release only local resources. */
    ~HostFramePacer() {
        report();
        if (trace_) std::fclose(trace_);
        if (timer_) CloseHandle(timer_);
    }

    /** Timer and trace ownership must remain with one loop. */
    HostFramePacer(const HostFramePacer&) = delete;
    /** Timer and trace ownership must remain with one loop. */
    HostFramePacer& operator=(const HostFramePacer&) = delete;

    /** Set a presentation cap; zero means no host frame wait. */
    void configure(int fps) {
        report();
        target_ = (std::max)(fps, 0);
        deadline_ = counter() + (target_ ? frequency_ / target_ : 0.0);
        log("FRAME_PACING target=" + std::to_string(target_) + " timer=" + (timer_ ? "local" : "fallback"));
    }

    /** Complete presentation timing after SwapBuffers and message dispatch. */
    void finishFrame() {
        if (target_) waitUntil(deadline_);
        const double now = counter();
        const double frameMs = (now - previous_) * 1000.0 / frequency_;
        previous_ = now;
        ++totalFrames_;
        samples_.push_back(frameMs);
        recent_ = samples_.size() * frequency_ / (now - windowStart_);
        if (trace_) std::fprintf(trace_, "%llu,%.9f,%.6f,%d\n", totalFrames_, (now - started_) / frequency_, frameMs, target_);
        if (target_) {
            const double period = frequency_ / target_;
            deadline_ += period;
            // Skip missed slots; never issue a burst of catch-up presentations.
            if (deadline_ <= now) deadline_ += (std::floor((now - deadline_) / period) + 1.0) * period;
        }
        if ((now - windowStart_) / frequency_ >= ReportSeconds) report();
    }

    /** Return the current statistics-window average for a lightweight window title. */
    double recentFPS() const { return recent_; }

    /** Return the configured presentation cap; zero denotes unlimited. */
    int targetFPS() const { return target_; }

    /** Log exact host-loop counts and nearest-rank frame percentiles for this window. */
    void report() {
        if (samples_.empty()) return;
        const double now = previous_;
        std::vector<double> sorted(samples_);
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double fraction) {
            return sorted[static_cast<std::size_t>(std::ceil(fraction * sorted.size())) - 1];
        };
        const auto longFrames = std::count_if(sorted.begin(), sorted.end(), [](double ms) { return ms > LongFrameMs; });
        std::ostringstream message;
        message << "FRAME_STATS target=" << target_ << " frames=" << samples_.size()
            << " seconds=" << (now - windowStart_) / frequency_ << " avgFPS=" << recent_
            << " p95Ms=" << percentile(0.95) << " p99Ms=" << percentile(0.99)
            << " longFrames=" << longFrames << " maxMs=" << sorted.back()
            << " totalFrames=" << totalFrames_ << " totalAvgFPS=" << totalFrames_ * frequency_ / (now - started_);
        log(message.str());
        if (trace_) std::fflush(trace_);
        samples_.clear(); windowStart_ = now;
    }
};
