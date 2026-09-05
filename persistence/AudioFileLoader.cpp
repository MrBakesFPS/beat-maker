#include "AudioFileLoader.h"

namespace beatmaker::persistence
{

AudioFileLoader::AudioFileLoader()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, OGG, (MP3 where available)
}

std::optional<LoadedAudio> AudioFileLoader::load (const juce::File& file, double targetSampleRate, juce::String& error)
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
    const double ratio = reader->sampleRate / targetSampleRate; // input samples per output sample
    const int outLength = (int) std::ceil (sourceLength / ratio);

    juce::AudioBuffer<float> resampled (numChannels, outLength);
    resampled.clear();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.reset();
        interpolator.process (ratio, source.getReadPointer (ch), resampled.getWritePointer (ch), outLength,
                              sourceLength, 0);
    }

    result.numSamples = outLength;
    result.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (resampled));
    return result;
}

} // namespace beatmaker::persistence
