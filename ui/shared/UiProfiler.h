// UiProfiler: where the message thread's time goes. Scoped timers record
// named operations (paints, edits, snapshots, saves); a stall detector notes
// when the message loop was blocked for longer than a frame. Readable from
// the System Usage window, the log, and --profile-ui.
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <map>
#include <mutex>

namespace beatmaker::ui
{

class UiProfiler
{
public:
    static UiProfiler& get() { static UiProfiler p; return p; }

    struct Stat { long long count = 0; double totalMs = 0.0, maxMs = 0.0; };
    struct Event { juce::String name; double ms = 0.0; juce::int64 at = 0; };

    void record (const juce::String& name, double ms)
    {
        const std::lock_guard<std::mutex> l (lock);
        auto& s = stats[name]; ++s.count; s.totalMs += ms; s.maxMs = juce::jmax (s.maxMs, ms);
        lastName = name;
        if (ms >= slowThresholdMs) push ({ name, ms, juce::Time::currentTimeMillis() });
    }
    void recordStall (double ms)
    {
        const std::lock_guard<std::mutex> l (lock);
        ++stalls; maxStallMs = juce::jmax (maxStallMs, ms);
        push ({ "stall (last op: " + lastName + ")", ms, juce::Time::currentTimeMillis() });
    }
    std::map<juce::String, Stat> getStats() const { const std::lock_guard<std::mutex> l (lock); return stats; }
    std::vector<Event> getRecentSlow() const
    {
        const std::lock_guard<std::mutex> l (lock);
        std::vector<Event> out;
        for (int i = 0; i < juce::jmin (eventCount, (int) events.size()); ++i) out.push_back (events[(size_t) ((eventCount - 1 - i) % (int) events.size())]);
        return out;   // newest first
    }
    long long getStalls() const { const std::lock_guard<std::mutex> l (lock); return stalls; }
    double getMaxStallMs() const { const std::lock_guard<std::mutex> l (lock); return maxStallMs; }
    void reset() { const std::lock_guard<std::mutex> l (lock); stats.clear(); eventCount = 0; stalls = 0; maxStallMs = 0.0; }

    juce::String summary() const
    {
        auto s = getStats();
        std::vector<std::pair<juce::String, Stat>> rows (s.begin(), s.end());
        std::sort (rows.begin(), rows.end(), [] (const auto& a, const auto& b) { return a.second.totalMs > b.second.totalMs; });
        juce::String t = "Message thread profile (total ms, count, avg, max)\n";
        for (const auto& [name, st] : rows)
            t << juce::String (st.totalMs, 1).paddedLeft (' ', 9) << "  " << juce::String (st.count).paddedLeft (' ', 6) << "  " << juce::String (st.totalMs / (double) juce::jmax (1LL, st.count), 2).paddedLeft (' ', 7)
              << "  " << juce::String (st.maxMs, 1).paddedLeft (' ', 7) << "  " << name << "\n";
        t << "Stalls over " << juce::String (stallThresholdMs, 0) << " ms: " << juce::String (getStalls()) << " (longest " << juce::String (getMaxStallMs(), 0) << " ms)\n";
        for (const auto& e : getRecentSlow()) t << "  " << juce::String (e.ms, 1) << " ms  " << e.name << "\n";
        return t;
    }

    static constexpr double slowThresholdMs = 8.0, stallThresholdMs = 100.0;

    // RAII timer for one operation.
    struct Scope
    {
        explicit Scope (juce::String n) : name (std::move (n)), start (juce::Time::getHighResolutionTicks()) {}
        ~Scope() { get().record (name, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1000.0); }
        juce::String name; juce::int64 start;
    };

    // Message-thread stall detector: measures how late its own ticks arrive.
    class StallDetector final : private juce::Timer
    {
    public:
        StallDetector() { startTimer (25); last = juce::Time::getMillisecondCounterHiRes(); }
        ~StallDetector() override { stopTimer(); }
    private:
        void timerCallback() override
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double gap = now - last;
            if (gap > stallThresholdMs + 25.0) get().recordStall (gap - 25.0);
            last = now;
        }
        double last = 0.0;
    };

private:
    void push (Event e) { events[(size_t) (eventCount % (int) events.size())] = std::move (e); ++eventCount; }
    mutable std::mutex lock;
    std::map<juce::String, Stat> stats;
    std::array<Event, 24> events;
    int eventCount = 0;
    long long stalls = 0; double maxStallMs = 0.0;
    juce::String lastName;
};

} // namespace beatmaker::ui
