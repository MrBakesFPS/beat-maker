#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <AudioFileLoader.h>

using namespace beatmaker::persistence;

namespace
{
    // Write a short WAV with a known sine so we can verify what comes back.
    juce::File writeTestWav (double sampleRate, int channels, int length, double freq)
    {
        auto file = juce::File::createTempFile (".wav");
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate) * 0.5f);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        const auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                             .withNumChannels (channels)
                                                             .withBitsPerSample (24);
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (b, 0, length));
        return file;
    }
}

TEST_CASE ("Loads a WAV at matching sample rate without resampling")
{
    auto file = writeTestWav (44100.0, 2, 4410, 440.0);
    AudioFileLoader loader;
    juce::String error;
    auto loaded = loader.load (file, 44100.0, error);

    REQUIRE (loaded.has_value());
    CHECK (error.isEmpty());
    CHECK (loaded->numChannels == 2);
    CHECK (loaded->numSamples == 4410);
    CHECK (loaded->sourceSampleRate == 44100.0);
    CHECK (loaded->audio->getNumSamples() == 4410);
    CHECK_THAT (loaded->audio->getMagnitude (0, 0, 4410), Catch::Matchers::WithinAbs (0.5, 0.01));
    file.deleteFile();
}

TEST_CASE ("Resamples 48k file to a 96k engine")
{
    auto file = writeTestWav (48000.0, 1, 4800, 1000.0); // 0.1 s
    AudioFileLoader loader;
    juce::String error;
    auto loaded = loader.load (file, 96000.0, error);

    REQUIRE (loaded.has_value());
    CHECK (loaded->sampleRate == 96000.0);
    CHECK (loaded->numSamples == 9600);
    // Same peak level after resampling (a 1 kHz sine is far below Nyquist).
    CHECK_THAT (loaded->audio->getMagnitude (0, 100, 9000), Catch::Matchers::WithinAbs (0.5, 0.02));
    file.deleteFile();
}

TEST_CASE ("Unreadable file reports an error")
{
    AudioFileLoader loader;
    juce::String error;
    auto loaded = loader.load (juce::File ("/definitely/not/here.wav"), 44100.0, error);
    CHECK_FALSE (loaded.has_value());
    CHECK (error.isNotEmpty());
}
