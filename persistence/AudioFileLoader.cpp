#include "AudioFileLoader.h"
#include <dsp/Resampler.h>

namespace beatmaker::persistence
{

AudioFileLoader::AudioFileLoader()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, OGG, (MP3 where available)
}

juce::String AudioFileLoader::keyFor (const juce::File& file, double rate) const
{
    return file.getFullPathName() + "|" + juce::String (file.getSize()) + "|" + juce::String (file.getLastModificationTime().toMilliseconds()) + "|" + juce::String (rate, 1);
}

void AudioFileLoader::setCacheBudgetBytes (juce::int64 bytes) { cacheBudget = juce::jmax<juce::int64> (0, bytes); evictIfNeeded(); }

AudioFileLoader::CacheStats AudioFileLoader::getCacheStats() const
{
    CacheStats s; s.hits = hits; s.misses = misses; s.entries = (int) cache.size(); s.budget = cacheBudget;
    for (const auto& [k, e] : cache) s.bytes += e.bytes;
    return s;
}

void AudioFileLoader::clearCache() { cache.clear(); }

void AudioFileLoader::evictIfNeeded()
{
    // Drop least-recently-used entries nobody else holds until under budget.
    for (;;)
    {
        juce::int64 total = 0;
        for (const auto& [k, e] : cache) total += e.bytes;
        if (total <= cacheBudget) return;
        juce::String victim; juce::int64 oldest = std::numeric_limits<juce::int64>::max();
        for (const auto& [k, e] : cache)
            if (e.audio.use_count() == 1 && e.lastUse < oldest) { oldest = e.lastUse; victim = k; }
        if (victim.isEmpty()) return;   // everything is in use: over budget but nothing to drop
        cache.erase (victim);
    }
}

std::optional<LoadedAudio> AudioFileLoader::load (const juce::File& file, double targetSampleRate, juce::String& error)
{
    const auto key = keyFor (file, targetSampleRate);
    if (auto it = cache.find (key); it != cache.end())
    {
        ++hits;
        it->second.lastUse = ++useCounter;
        auto info = it->second.info;
        info.audio = it->second.audio;
        return info;
    }
    ++misses;
    auto loaded = decode (file, targetSampleRate, error);
    if (loaded)
    {
        Entry e; e.audio = loaded->audio; e.info = *loaded; e.info.audio.reset(); e.lastUse = ++useCounter;   // one reference in the cache
        e.bytes = (juce::int64) loaded->audio->getNumChannels() * loaded->audio->getNumSamples() * (juce::int64) sizeof (float);
        cache[key] = std::move (e);
        evictIfNeeded();
    }
    return loaded;
}

std::optional<LoadedAudio> AudioFileLoader::decode (const juce::File& file, double targetSampleRate, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file: " + file.getFileName();
        return std::nullopt;
    }

    if (reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        error = "Audio file is empty: " + file.getFileName();
        return std::nullopt;
    }

    const int numChannels = (int) reader->numChannels;
    const int sourceLength = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> source (numChannels, sourceLength);
    if (! reader->read (&source, 0, sourceLength, 0, true, true))
    {
        error = "Failed to read audio data from " + file.getFileName();
        return std::nullopt;
    }

    LoadedAudio result;
    result.sourceSampleRate = reader->sampleRate;
    result.sampleRate = targetSampleRate;
    result.numChannels = numChannels;

    if (std::abs (reader->sampleRate - targetSampleRate) < 1.0)
    {
        result.numSamples = sourceLength;
        result.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (source));
        return result;
    }

    // Resample to the engine rate.
    auto resampled = engine::Resampler::resample (source, reader->sampleRate / targetSampleRate);
    result.numSamples = resampled.getNumSamples();
    result.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (resampled));
    return result;
}

} // namespace beatmaker::persistence
