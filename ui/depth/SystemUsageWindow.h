// SystemUsageWindow: CPU load with peak and overruns, the heaviest tracks,
// the audio cache and session memory (Pro Tools' System Usage window).
#pragma once

#include "../shared/Theme.h"
#include <graph/PerformanceMonitor.h>
#include <AudioFileLoader.h>
#include <Session.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace beatmaker::ui
{

class SystemUsageWindow final : public juce::Component,
                                private juce::Timer
{
public:
    SystemUsageWindow (engine::PerformanceMonitor&, persistence::AudioFileLoader&, const model::Session&, std::function<int()> deviceBlockSize);
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 560, preferredHeight = 560;

private:
    void timerCallback() override { repaint(); }
    static juce::int64 sessionAudioBytes (const model::Session&);
    engine::PerformanceMonitor& perf;
    persistence::AudioFileLoader& loader;
    const model::Session& session;
    std::function<int()> blockSize;
    juce::TextButton resetButton { "Reset peaks" }, clearCacheButton { "Clear unused cache" };
};

} // namespace beatmaker::ui
