#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <LoopLibrary.h>
#include <dsp/Resampler.h>

using namespace beatmaker::persistence;
using beatmaker::engine::Resampler;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::File writeWav (const juce::File& file, double sampleRate, int channels, int length)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, std::sin (i * 0.05f) * 0.5f);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                    .withNumChannels (channels)
                                                                                    .withBitsPerSample (16));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, length);
        return file;
    }
}

TEST_CASE ("Tempo is parsed from common file-name conventions")
{
    CHECK (LoopLibrary::parseTempoFromName ("Drum Loop 120") == 120.0);
    CHECK (LoopLibrary::parseTempoFromName ("bass_90bpm") == 90.0);
    CHECK (LoopLibrary::parseTempoFromName ("Pad-100-Cmin") == 100.0);
    CHECK (LoopLibrary::parseTempoFromName ("BPM128 house") == 128.0);
    CHECK (LoopLibrary::parseTempoFromName ("Vocal Chop") == 0.0);
    CHECK (LoopLibrary::parseTempoFromName ("Kick 808") == 0.0);           // 808 is out of range
    CHECK (LoopLibrary::parseTempoFromName ("Take 03") == 0.0);            // too low to be a tempo
    CHECK (LoopLibrary::parseTempoFromName ("Synth 2024 mix 95") == 95.0); // year is skipped
}

TEST_CASE ("Key is parsed only from unambiguous tokens")
{
    CHECK (LoopLibrary::parseKeyFromName ("Sub Bass Line 120 Am") == "Am");
    CHECK (LoopLibrary::parseKeyFromName ("Pad-100-Cmin") == "Cm");
    CHECK (LoopLibrary::parseKeyFromName ("lead_F#_140") == "F#");
    CHECK (LoopLibrary::parseKeyFromName ("Chords Bbmaj 90") == "Bb");
    CHECK (LoopLibrary::parseKeyFromName ("Guitar E minor") == "Em");
    CHECK (LoopLibrary::parseKeyFromName ("Drum Loop A") == "");          // bare letter ignored
    CHECK (LoopLibrary::parseKeyFromName ("Beat 120") == "");
}

TEST_CASE ("Categories are detected from name and folder")
{
    using C = LoopInfo::Category;
    CHECK (LoopLibrary::detectCategory ("loops Drum Loop 120") == C::drums);
    CHECK (LoopLibrary::detectCategory ("Sub Bass Line 120 Am") == C::bass);
    CHECK (LoopLibrary::detectCategory ("Synth Pad 120 Am") == C::synth);
    CHECK (LoopLibrary::detectCategory ("Acoustic Guitar Strum 100") == C::guitar);
    CHECK (LoopLibrary::detectCategory ("Vocal Chop 128") == C::vocals);
    CHECK (LoopLibrary::detectCategory ("Riser FX 120") == C::fx);
    CHECK (LoopLibrary::detectCategory ("Mystery") == C::other);
    CHECK (LoopLibrary::detectCategory ("Drums/take1") == C::drums);      // folder name counts
}

TEST_CASE ("Tempo estimated from length prefers whole bars near the session tempo")
{
    CHECK_THAT (LoopLibrary::estimateTempoFromLength (4.0),  WithinAbs (120.0, 1e-9));   // 2 bars @120
    CHECK_THAT (LoopLibrary::estimateTempoFromLength (2.0),  WithinAbs (120.0, 1e-9));   // 1 bar
    CHECK_THAT (LoopLibrary::estimateTempoFromLength (5.3333333), WithinAbs (90.0, 1e-4)); // 2 bars @90
    CHECK_THAT (LoopLibrary::estimateTempoFromLength (7.5, 128.0), WithinAbs (128.0, 1e-9)); // 4 bars @128
    CHECK (LoopLibrary::estimateTempoFromLength (0.37) == 0.0);          // one-shot, nothing fits
    CHECK (LoopLibrary::estimateTempoFromLength (0.0) == 0.0);
}

TEST_CASE ("Scanning a folder analyses files and sorts by category then name")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bm-loops-" + juce::Uuid().toString());
    writeWav (dir.getChildFile ("Synth Pad 100 Am.wav"), 44100.0, 2, 44100 * 2);
    writeWav (dir.getChildFile ("sub").getChildFile ("Drum Loop 120.wav"), 48000.0, 1, 96000);
    writeWav (dir.getChildFile ("unknown.wav"), 44100.0, 1, 44100 * 4);   // no tempo in the name: 2 bars @120 estimated
    dir.getChildFile ("notes.txt").replaceWithText ("ignore me");

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    LoopLibrary lib (fm);
    lib.setFolders ({ dir });
    REQUIRE (lib.getFolders().size() == 1);
    lib.rescanNow();

    const auto& loops = lib.getLoops();
    REQUIRE (loops.size() == 3);

    CHECK (loops[0].name == "Drum Loop 120");
    CHECK (loops[0].category == LoopInfo::Category::drums);
    CHECK (loops[0].bpm == 120.0);
    CHECK_FALSE (loops[0].bpmEstimated);
    CHECK (loops[0].numChannels == 1);
    CHECK (loops[0].sampleRate == 48000.0);
    CHECK_THAT (loops[0].lengthSeconds, WithinAbs (2.0, 1e-9));
    CHECK_THAT (loops[0].getBars(), WithinAbs (1.0, 1e-9));

    CHECK (loops[1].name == "Synth Pad 100 Am");
    CHECK (loops[1].category == LoopInfo::Category::synth);
    CHECK (loops[1].key == "Am");
    CHECK (loops[1].numChannels == 2);

    CHECK (loops[2].name == "unknown");
    CHECK (loops[2].category == LoopInfo::Category::other);
    CHECK (loops[2].bpmEstimated);
    CHECK_THAT (loops[2].bpm, WithinAbs (120.0, 1e-9));

    dir.deleteRecursively();
}

TEST_CASE ("Varispeed resampling changes length by the tempo ratio and keeps level")
{
    juce::AudioBuffer<float> src (1, 48000);
    for (int i = 0; i < 48000; ++i)
        src.setSample (0, i, std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / 48000.0) * 0.5);

    const double ratio = Resampler::ratioForTempo (90.0, 120.0);   // play faster
    CHECK_THAT (ratio, WithinAbs (120.0 / 90.0, 1e-12));

    auto out = Resampler::resample (src, ratio);
    CHECK (out.getNumSamples() == 36000);
    CHECK_THAT (out.getMagnitude (0, 100, 35000), WithinAbs (0.5, 0.01));

    auto same = Resampler::resample (src, Resampler::ratioForTempo (120.0, 120.0));
    CHECK (same.getNumSamples() == 48000);
    CHECK (same.getSample (0, 1234) == src.getSample (0, 1234));

    CHECK (Resampler::ratioForTempo (0.0, 120.0) == 1.0);
    CHECK (Resampler::resample (juce::AudioBuffer<float>(), 2.0).getNumSamples() == 0);
}
