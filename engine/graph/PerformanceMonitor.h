// PerformanceMonitor: how much of each audio callback's time budget the
// engine used, smoothed and with a peak hold, plus overruns and per-strip
// shares. Written from the audio thread, read from the message thread.
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace beatmaker::engine
{

class PerformanceMonitor
{
public:
    static constexpr int maxStrips = 64;

    // Audio thread: call around each callback with the block's duration.
    void beginBlock() noexcept { blockStartTicks = juce::Time::getHighResolutionTicks(); }
    void endBlock (int numSamples, double sampleRate) noexcept
    {
        const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - blockStartTicks);
        const double budget = sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.0;
        record (elapsed, budget);
    }
    // The same maths without the clock (tests, offline analysis).
    void record (double elapsedSeconds, double blockSeconds) noexcept
    {
        const float load = blockSeconds > 0.0 ? (float) (elapsedSeconds / blockSeconds) : 0.0f;
        const float smoothed = smoothedLoad.load (std::memory_order_relaxed) * 0.9f + load * 0.1f;
        smoothedLoad.store (smoothed, std::memory_order_relaxed);
        lastLoad.store (load, std::memory_order_relaxed);
        if (load > peakLoad.load (std::memory_order_relaxed)) peakLoad.store (load, std::memory_order_relaxed);
        if (load > 1.0f) overruns.fetch_add (1, std::memory_order_relaxed);
        blocks.fetch_add (1, std::memory_order_relaxed);
    }

    // Per-strip share of the block budget (audio thread).
    void beginStrip() noexcept { stripStartTicks = juce::Time::getHighResolutionTicks(); }
    void endStrip (int stripIndex, int numSamples, double sampleRate) noexcept
    {
        if (! juce::isPositiveAndBelow (stripIndex, maxStrips) || sampleRate <= 0.0) return;
        const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - stripStartTicks);
        recordStrip (stripIndex, elapsed, (double) numSamples / sampleRate);
    }
    void recordStrip (int stripIndex, double elapsedSeconds, double blockSeconds) noexcept
    {
        if (! juce::isPositiveAndBelow (stripIndex, maxStrips) || blockSeconds <= 0.0) return;
        auto& s = stripLoad[(size_t) stripIndex];
        s.store (s.load (std::memory_order_relaxed) * 0.9f + (float) (elapsedSeconds / blockSeconds) * 0.1f, std::memory_order_relaxed);
    }

    // Message thread readouts (0..1+, 1 = the whole budget)
    float getLoad() const noexcept { return smoothedLoad.load (std::memory_order_relaxed); }
    float getLastLoad() const noexcept { return lastLoad.load (std::memory_order_relaxed); }
    float getPeakLoad() const noexcept { return peakLoad.load (std::memory_order_relaxed); }
    long long getOverruns() const noexcept { return overruns.load (std::memory_order_relaxed); }
    long long getBlocks() const noexcept { return blocks.load (std::memory_order_relaxed); }
    float getStripLoad (int i) const noexcept { return juce::isPositiveAndBelow (i, maxStrips) ? stripLoad[(size_t) i].load (std::memory_order_relaxed) : 0.0f; }
    void resetPeaks() noexcept { peakLoad.store (0.0f); overruns.store (0); }

private:
    juce::int64 blockStartTicks = 0, stripStartTicks = 0;
    std::atomic<float> smoothedLoad { 0.0f }, lastLoad { 0.0f }, peakLoad { 0.0f };
    std::atomic<long long> overruns { 0 }, blocks { 0 };
    std::array<std::atomic<float>, maxStrips> stripLoad {};
};

} // namespace beatmaker::engine
