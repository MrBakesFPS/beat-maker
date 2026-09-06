#include "SystemUsageWindow.h"
#include <set>

namespace beatmaker::ui
{

SystemUsageWindow::SystemUsageWindow (engine::PerformanceMonitor& p, persistence::AudioFileLoader& l, const model::Session& s, std::function<int()> bs)
    : perf (p), loader (l), session (s), blockSize (std::move (bs))
{
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { perf.resetPeaks(); };
    addAndMakeVisible (clearCacheButton);
    clearCacheButton.setTooltip ("Drops cached audio nothing references; audio in the session stays");
    clearCacheButton.onClick = [this] { loader.setCacheBudgetBytes (0); loader.setCacheBudgetBytes (loader.getCacheStats().budget); };
    setSize (preferredWidth, preferredHeight);
    startTimerHz (10);
}

juce::int64 SystemUsageWindow::sessionAudioBytes (const model::Session& s)
{
    std::set<const juce::AudioBuffer<float>*> seen;
    juce::int64 bytes = 0;
    auto count = [&] (const std::shared_ptr<const juce::AudioBuffer<float>>& b)
    {
        if (b == nullptr || ! seen.insert (b.get()).second) return;
        bytes += (juce::int64) b->getNumChannels() * b->getNumSamples() * (juce::int64) sizeof (float);
    };
    for (const auto& t : s.getTracks())
    {
        for (const auto& c : t.clips) { count (c.audio); count (c.sourceAudio); }
        for (const auto& alt : t.alternates) for (const auto& c : alt.clips) { count (c.audio); count (c.sourceAudio); }
        if (t.drumKit != nullptr) for (const auto& pad : t.drumKit->pads) count (pad.audio);
        if (t.instrumentParams != nullptr) count (t.instrumentParams->sample);
        count (t.freeze.audio);
    }
    return bytes;
}

void SystemUsageWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    auto area = getLocalBounds().reduced (16);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("System Usage", area.removeFromTop (20), juce::Justification::centredLeft);
    area.removeFromTop (8);

    auto bar = [&] (const juce::String& label, float value, float peak, const juce::String& readout)
    {
        auto row = area.removeFromTop (26);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, row.removeFromLeft (110), juce::Justification::centredLeft);
        g.drawText (readout, row.removeFromRight (150), juce::Justification::centredRight);
        auto track = row.reduced (4, 8).toFloat();
        g.setColour (theme::background);
        g.fillRoundedRectangle (track, 3.0f);
        const float v = juce::jlimit (0.0f, 1.0f, value);
        g.setColour (v > 0.9f ? theme::record : v > 0.7f ? juce::Colour (0xfff1c40f) : theme::accent);
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * v), 3.0f);
        if (peak > 0.0f)
        {
            const float px = track.getX() + track.getWidth() * juce::jlimit (0.0f, 1.0f, peak);
            g.setColour (theme::text);
            g.drawVerticalLine ((int) px, track.getY(), track.getBottom());
        }
    };
    const float load = perf.getLoad(), peak = perf.getPeakLoad();
    bar ("CPU (audio)", load, peak, juce::String (juce::roundToInt (load * 100.0f)) + " %   peak " + juce::String (juce::roundToInt (peak * 100.0f)) + " %");
    g.setColour (perf.getOverruns() > 0 ? theme::record : theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Overruns (callbacks over budget): " + juce::String (perf.getOverruns()) + "   blocks: " + juce::String (perf.getBlocks())
                    + "   block size: " + juce::String (blockSize ? blockSize() : 0) + " samples",
                area.removeFromTop (18), juce::Justification::centredLeft);
    area.removeFromTop (10);

    // Heaviest tracks
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("Tracks by CPU", area.removeFromTop (18), juce::Justification::centredLeft);
    std::vector<std::pair<float, int>> loads;
    for (int i = 0; i < session.getNumTracks() && i < engine::PerformanceMonitor::maxStrips; ++i) loads.emplace_back (perf.getStripLoad (i), i);
    std::sort (loads.begin(), loads.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    int shown = 0;
    for (const auto& [l, i] : loads)
    {
        if (shown++ >= 6) break;
        const auto& t = session.getTracks()[(size_t) i];
        bar (t.name.substring (0, 16), l, 0.0f, juce::String (l * 100.0f, 1) + " %" + (t.isFrozen() ? "  (frozen)" : juce::String()));
    }
    if (loads.empty()) { g.setColour (theme::textDim); g.setFont (juce::FontOptions (11.0f)); g.drawText ("No tracks", area.removeFromTop (18), juce::Justification::centredLeft); }
    area.removeFromTop (10);

    // Memory
    const auto stats = loader.getCacheStats();
    const auto mb = [] (juce::int64 b) { return juce::String (b / (1024.0 * 1024.0), 1) + " MB"; };
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("Memory", area.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("Session audio in RAM: " + mb (sessionAudioBytes (session)), area.removeFromTop (18), juce::Justification::centredLeft);
    g.drawText ("Audio cache: " + mb (stats.bytes) + " of " + mb (stats.budget) + " in " + juce::String (stats.entries) + " files   hits " + juce::String (stats.hits) + "  misses " + juce::String (stats.misses)
                    + (stats.hits + stats.misses > 0 ? "  (" + juce::String (juce::roundToInt (100.0 * stats.hits / (double) (stats.hits + stats.misses))) + " % hit rate)" : juce::String()),
                area.removeFromTop (18), juce::Justification::centredLeft, true);
}

void SystemUsageWindow::resized()
{
    auto buttons = getLocalBounds().reduced (16).removeFromBottom (26);
    resetButton.setBounds (buttons.removeFromLeft (110)); buttons.removeFromLeft (6);
    clearCacheButton.setBounds (buttons.removeFromLeft (150));
}

} // namespace beatmaker::ui
