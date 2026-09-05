// Loads an audio file into memory and resamples it to the engine rate so the
// audio thread can play it with plain buffer reads.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <optional>

namespace beatmaker::persistence
{

struct LoadedAudio
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double sampleRate = 0.0;        // rate of `audio` (== target rate)
    double sourceSampleRate = 0.0;  // rate of the file on disk
    int numChannels = 0;
    juce::int64 numSamples = 0;
};

class AudioFileLoader
{
public:
    AudioFileLoader();

    // Blocking. Returns nullopt and sets `error` on failure.
    std::optional<LoadedAudio> load (const juce::File& file, double targetSampleRate, juce::String& error);

    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::String getWildcard() const { return formatManager.getWildcardForAllFormats(); }

private:
    juce::AudioFormatManager formatManager;
};

} // namespace beatmaker::persistence
