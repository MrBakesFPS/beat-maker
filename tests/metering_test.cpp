#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <Session.h>
#include <graph/AudioGraph.h>
#include <metering/Loudness.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> sine (int length, double freq, double sr, float amp)
    {
        juce::AudioBuffer<float> b (1, length);
        for (int i = 0; i < length; ++i) b.setSample (0, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    // Run `seconds` of a stereo sine through the graph master, feeding the analyser.
    LoudnessAnalyser::Readings measure (AudioGraph& graph, Transport& t, double seconds, int block = 512)
    {
        LoudnessAnalyser analyser;
        analyser.setSampleRate (t.getSampleRate());
        std::vector<LoudnessBlock> scratch;
        juce::AudioBuffer<float> out (2, block);
        const int total = (int) (seconds * t.getSampleRate());
        for (int pos = 0; pos < total; pos += block)
        {
            graph.renderBlock (out.getArrayOfWritePointers(), 2, block);
            scratch.clear();
            graph.getLoudnessSource().drain (scratch);
            for (const auto& b : scratch) analyser.addBlock (b);
        }
        return analyser.getReadings();
    }
}

TEST_CASE ("RMS meters integrate over 300 ms and clip flags hold until read")
{
    Transport t;
    t.setSampleRate (48000.0);
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip c; c.audio = sine (96000, 1000.0, 48000.0, 1.2f); c.length = 96000; c.strip = 0;   // clips above 0 dBFS
    snap->clips.push_back (c);
    snap->strips.push_back ({});
    graph.setSnapshot (std::move (snap));
    t.play();

    juce::AudioBuffer<float> out (2, 512);
    for (int i = 0; i < 100; ++i) graph.renderBlock (out.getArrayOfWritePointers(), 2, 512);   // ~1.07 s

    CHECK_THAT (graph.getStripPeak (0, 0), WithinAbs (1.2f, 0.01f));
    CHECK_THAT (graph.getStripRms (0, 0), WithinAbs (1.2f / juce::MathConstants<float>::sqrt2, 0.02f));   // sine RMS
    CHECK_THAT (graph.getMasterRms (1), WithinAbs (1.2f / juce::MathConstants<float>::sqrt2, 0.02f));
    CHECK (graph.getAndClearStripClip (0));
    CHECK_FALSE (graph.getAndClearStripClip (0));   // cleared by the read
    CHECK (graph.getAndClearMasterClip());
    graph.collectGarbage();
}

TEST_CASE ("K-weighting matches BS.1770: +0.69 dB at 1 kHz (hence the -0.691 offset), boosted highs, cut lows")
{
    // Measured through the graph's loudness source: a full-scale sine's block energy tells the filter gain.
    Transport t;
    t.setSampleRate (48000.0);
    AudioGraph graph (t);
    auto gainAt = [&] (double freq)
    {
        auto snap = std::make_unique<RenderSnapshot>();
        RenderClip c; c.audio = sine (96000, freq, 48000.0, 1.0f); c.length = 96000; c.strip = 0;
        snap->clips.push_back (c);
        snap->strips.push_back ({});
        graph.setSnapshot (std::move (snap));
        t.setPositionSamples (0);
        t.play();
        std::vector<LoudnessBlock> blocks;
        juce::AudioBuffer<float> out (2, 480);
        for (int i = 0; i < 200; ++i) graph.renderBlock (out.getArrayOfWritePointers(), 2, 480);   // 2 s
        graph.getLoudnessSource().drain (blocks);
        double energy = 0.0; int n = 0;
        for (size_t i = blocks.size() / 2; i < blocks.size(); ++i) { energy += blocks[i].sumSquares[0]; n += blocks[i].numSamples; }
        const double meanSq = energy / n;                    // filtered sine: A^2 G^2 / 2 with A = 1
        return (float) (10.0 * std::log10 (meanSq * 2.0));  // = 20 log10 (G)
    };
    CHECK_THAT (gainAt (1000.0), WithinAbs (0.691f, 0.1f));
    CHECK (gainAt (10000.0) > 3.5f);
    CHECK (gainAt (30.0) < -3.0f);
    graph.collectGarbage();
}

TEST_CASE ("Loudness: a stereo 1 kHz sine at -20 dBFS reads -20 LUFS with true peak -20 dBTP")
{
    Transport t;
    t.setSampleRate (48000.0);
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip c; c.audio = sine (48000 * 6, 1000.0, 48000.0, 0.1f); c.length = 48000 * 6; c.strip = 0;   // mono source -> both channels
    snap->clips.push_back (c);
    snap->strips.push_back ({});
    graph.setSnapshot (std::move (snap));
    t.play();

    const auto r = measure (graph, t, 5.0);
    // BS.1770: a 0 dBFS 1 kHz sine in one channel reads -3.01 LKFS, so -20 dBFS in both channels reads -20.0.
    CHECK_THAT (r.momentary, WithinAbs (-20.0f, 0.2f));
    CHECK_THAT (r.shortTerm, WithinAbs (-20.0f, 0.2f));
    CHECK_THAT (r.integrated, WithinAbs (-20.0f, 0.2f));
    CHECK_THAT (r.truePeakDb, WithinAbs (-20.0f, 0.15f));
    CHECK (r.range < 1.0f);   // steady signal
    graph.collectGarbage();
}

TEST_CASE ("Integrated loudness gates out silence and the analyser resets")
{
    LoudnessAnalyser a;
    a.setSampleRate (48000.0);
    auto feed = [&] (double seconds, float energyPerChannel)
    {
        LoudnessBlock b; b.numSamples = 4800; b.sumSquares[0] = b.sumSquares[1] = energyPerChannel * 4800.0f; b.truePeak = std::sqrt (energyPerChannel);
        for (int i = 0; i < (int) (seconds * 10); ++i) a.addBlock (b);
    };
    feed (3.0, 0.005f);   // -20.69 LUFS for 3 s
    const float loud = a.getReadings().integrated;
    CHECK_THAT (loud, WithinAbs (-20.69f, 0.2f));

    feed (10.0, 0.0f);    // 10 s of silence: gated out. Only the three 400 ms windows straddling
                          // the transition dilute the result (by 0.22 LU, exactly as the standard behaves).
    CHECK_THAT (a.getReadings().integrated, WithinAbs (loud, 0.3f));
    CHECK (a.getReadings().integrated > -22.0f);
    CHECK (a.getReadings().momentary < -90.0f);   // but the momentary follows the input

    a.reset();
    CHECK (a.getReadings().integrated < -90.0f);
    CHECK (a.getReadings().truePeakDb < -90.0f);
}

TEST_CASE ("Meter type is per track and not undoable")
{
    using namespace beatmaker::model;
    Session s;
    Track t; s.execute (std::make_unique<AddTrackCommand> (t));
    CHECK (s.getTracks()[0].meterType == MeterType::samplePeak);
    s.execute (std::make_unique<SetMeterTypeCommand> (0, MeterType::k14));
    CHECK (s.getTracks()[0].meterType == MeterType::k14);
    CHECK (s.getHistory().getUndoName() == "Add Track");
    s.execute (std::make_unique<SetMeterTypeCommand> (-1, MeterType::rms));
    CHECK (s.getMaster().meterType == MeterType::rms);
    CHECK (juce::String (meterTypeName (MeterType::k20)) == "K-20");
}
