// Elastic-style time stretching and pitch shifting (offline, message or
// background thread only). Polyphonic/Rhythmic/Monophonic run through the
// Rubber Band Library (GPL); Varispeed resamples, so pitch follows speed.
// Warp markers pin source positions to output positions; between markers the
// stretch ratio is whatever it takes to hit the next one.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <vector>

namespace beatmaker::engine
{

enum class StretchMode { off, polyphonic, rhythmic, monophonic, varispeed };

struct WarpMarker
{
    juce::int64 source = 0;   // sample in the unstretched audio
    juce::int64 output = 0;   // sample in the rendered audio
};

struct StretchSpec
{
    StretchMode mode = StretchMode::off;
    double ratio = 1.0;              // output length / source length
    double pitchSemitones = 0.0;     // ignored by Varispeed (pitch follows speed)
    std::vector<WarpMarker> markers; // sorted by source

    bool isActive() const noexcept { return mode != StretchMode::off; }
    bool isIdentity() const noexcept
    {
        return ! isActive() || (std::abs (ratio - 1.0) < 1.0e-9 && std::abs (pitchSemitones) < 1.0e-9 && markers.empty());
    }
    void sortMarkers();
};

class TimeStretch
{
public:
    static const char* modeName (StretchMode);

    static juce::int64 outputLength (juce::int64 sourceLength, const StretchSpec&) noexcept;

    // Piecewise-linear maps through (0,0), the markers and (sourceLength, outputLength).
    static juce::int64 sourceToOutput (juce::int64 source, juce::int64 sourceLength, const StretchSpec&) noexcept;
    static juce::int64 outputToSource (juce::int64 output, juce::int64 sourceLength, const StretchSpec&) noexcept;

    // Renders `source` (at `sampleRate`) through `spec`. Progress returns false
    // to cancel (result is then empty). The result has exactly outputLength() samples.
    using ProgressFn = std::function<bool (double)>;
    static juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& source, double sampleRate,
                                            const StretchSpec& spec, const ProgressFn& progress = {});

    // Rubber Band's own version string, for the About/credits text.
    static juce::String libraryVersion();
};

} // namespace beatmaker::engine
