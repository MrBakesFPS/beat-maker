#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <graph/PerformanceMonitor.h>
#include <AudioFileLoader.h>

using namespace beatmaker;
using Catch::Matchers::WithinAbs;

TEST_CASE ("PerformanceMonitor smooths load, holds peaks, counts overruns and tracks strips")
{
    engine::PerformanceMonitor pm;
    CHECK (pm.getLoad() == 0.0f);
    for (int i = 0; i < 200; ++i) pm.record (0.0025, 0.01);   // 25% for a while
    CHECK_THAT (pm.getLoad(), WithinAbs (0.25f, 0.01f));
    CHECK_THAT (pm.getLastLoad(), WithinAbs (0.25f, 1e-6f));
    CHECK_THAT (pm.getPeakLoad(), WithinAbs (0.25f, 1e-6f));
    CHECK (pm.getOverruns() == 0);
    pm.record (0.012, 0.01);   // an overrun
    CHECK (pm.getOverruns() == 1);
    CHECK_THAT (pm.getPeakLoad(), WithinAbs (1.2f, 1e-6f));
    CHECK (pm.getLoad() < 0.4f);   // smoothed, not spiking
    CHECK (pm.getBlocks() == 201);
    pm.resetPeaks();
    CHECK (pm.getPeakLoad() == 0.0f);
    CHECK (pm.getOverruns() == 0);
    for (int i = 0; i < 100; ++i) { pm.recordStrip (3, 0.001, 0.01); pm.recordStrip (5, 0.004, 0.01); }
    CHECK_THAT (pm.getStripLoad (3), WithinAbs (0.1f, 0.01f));
    CHECK_THAT (pm.getStripLoad (5), WithinAbs (0.4f, 0.01f));
    CHECK (pm.getStripLoad (99) == 0.0f);
    pm.recordStrip (99, 1.0, 1.0);   // out of range: ignored
}

TEST_CASE ("The audio cache shares decoded files, counts hits and evicts unreferenced entries under budget")
{
    persistence::AudioFileLoader loader;
    auto write = [] (const juce::String& name, int seconds)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        juce::AudioBuffer<float> b (1, 48000 * seconds); b.clear();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16)));
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    };
    const auto a = write ("beatmaker_cache_a.wav", 1), b = write ("beatmaker_cache_b.wav", 1);
    juce::String error;
    auto first = loader.load (a, 48000.0, error);
    auto again = loader.load (a, 48000.0, error);
    REQUIRE (first); REQUIRE (again);
    CHECK (first->audio == again->audio);              // one decode, shared buffer
    auto other = loader.load (a, 44100.0, error);      // a different target rate is a different entry
    REQUIRE (other);
    CHECK (other->audio != first->audio);
    auto stats = loader.getCacheStats();
    CHECK (stats.hits == 1); CHECK (stats.misses == 2); CHECK (stats.entries == 2);
    CHECK (stats.bytes == 48000 * 4 + other->audio->getNumSamples() * 4);

    // Budget below the cached size: entries still referenced survive, released ones go
    loader.setCacheBudgetBytes (48000 * 4);   // room for exactly one 1-second mono file
    CHECK (loader.getCacheStats().entries == 2);       // both still held by first/again/other
    other.reset();
    loader.load (b, 48000.0, error);                   // loading b triggers eviction of the unreferenced 44.1k entry
    stats = loader.getCacheStats();
    CHECK (stats.entries == 2);                        // a@48k (held) + b
    first.reset(); again.reset();
    loader.load (b, 48000.0, error);                   // hit; now a@48k is unreferenced and over budget -> evicted next time
    loader.setCacheBudgetBytes (48000 * 4);
    stats = loader.getCacheStats();
    CHECK (stats.entries == 1);
    CHECK (stats.hits == 2);
    loader.clearCache();
    CHECK (loader.getCacheStats().entries == 0);
    a.deleteFile(); b.deleteFile();
}
