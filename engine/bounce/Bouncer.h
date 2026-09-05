// Bouncer: offline render of a RenderSnapshot through the same AudioGraph
// code path the live device uses, so what you bounce is what you heard.
// Runs on whatever thread calls it (normally a background thread) using a
// private Transport and AudioGraph, so the live engine is never disturbed.
#pragma once

#include "../graph/RenderSnapshot.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>

namespace beatmaker::engine
{

struct BounceSettings
{
    enum class Format { wav, aiff, flac };

    // Timing context (copied from the live transport)
    double sampleRate  = 44100.0;
    double bpm         = 120.0;
    int beatsPerBar    = 4;

    // Range, in samples at `sampleRate`
    juce::int64 startSample = 0;
    juce::int64 endSample   = 0;

    // Rendering
    int numChannels     = 2;
    int blockSize       = 512;
    double tailSeconds  = 2.0;      // keep rendering after the range so reverbs/drums ring out
    bool trimTail       = true;     // cut trailing silence from the tail
    bool normalize      = false;
    float normalizeTargetDb = -0.3f;

    // File
    Format format = Format::wav;
    int bitDepth  = 24;             // 16, 24, or 32 (32 = float, WAV only)

    static juce::String extensionFor (Format f)
    {
        switch (f) { case Format::wav: return ".wav"; case Format::aiff: return ".aiff"; case Format::flac: return ".flac"; }
        return ".wav";
    }
    static bool supportsBitDepth (Format f, int bits)
    {
        if (bits == 32) return f == Format::wav;
        return bits == 16 || bits == 24;
    }
};

struct BounceResult
{
    juce::String error;          // empty on success
    bool cancelled = false;
    juce::int64 numSamples = 0;
    float peakBeforeNormalize = 0.0f;   // linear
    bool clipped = false;               // peak exceeded 0 dBFS before normalising
    float appliedGain = 1.0f;

    bool ok() const noexcept { return error.isEmpty() && ! cancelled; }
};

class Bouncer
{
public:
    // progress in 0..1; return false to cancel.
    using ProgressFn = std::function<bool (double)>;

    // Render to memory. On success `out` holds numChannels x numSamples.
    static BounceResult renderToBuffer (std::unique_ptr<RenderSnapshot> snapshot, const BounceSettings& settings,
                                        juce::AudioBuffer<float>& out, const ProgressFn& progress = {});

    // Write a buffer to disk in the requested format. Returns an error or empty.
    static juce::String writeFile (const juce::AudioBuffer<float>& buffer, const juce::File& file, const BounceSettings& settings);

    // Convenience: render then write.
    static BounceResult renderToFile (std::unique_ptr<RenderSnapshot> snapshot, const BounceSettings& settings,
                                      const juce::File& file, const ProgressFn& progress = {});

    static constexpr float silenceThreshold = 1.0e-4f;   // about -80 dBFS
};

} // namespace beatmaker::engine
