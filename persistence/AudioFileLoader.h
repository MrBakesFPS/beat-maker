// Loads an audio file into memory and resamples it to the engine rate so the
// audio thread can play it with plain buffer reads.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <map>
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

    // Audio cache: the same file (path, size, modification time, target rate)
    // is decoded once and shared; entries stay resident up to the budget and
    // the least recently used unreferenced ones go first.
    struct CacheStats { long long hits = 0, misses = 0; int entries = 0; juce::int64 bytes = 0, budget = 0; };
    void setCacheBudgetBytes (juce::int64 bytes);
    CacheStats getCacheStats() const;
    void clearCache();

private:
    struct Entry { std::shared_ptr<const juce::AudioBuffer<float>> audio; LoadedAudio info; juce::int64 bytes = 0; juce::int64 lastUse = 0; };
    juce::String keyFor (const juce::File&, double rate) const;
    std::optional<LoadedAudio> decode (const juce::File&, double targetSampleRate, juce::String& error);
    void evictIfNeeded();
    juce::AudioFormatManager formatManager;
    std::map<juce::String, Entry> cache;
    juce::int64 cacheBudget = 2048LL * 1024 * 1024;
    juce::int64 useCounter = 0;
    long long hits = 0, misses = 0;
};

} // namespace beatmaker::persistence
