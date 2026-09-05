#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <graph/AudioGraph.h>
#include <io/Recorder.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    std::unique_ptr<juce::AudioFormatReader> readWav (const juce::File& f)
    {
        juce::WavAudioFormat wav;
        return std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (new juce::FileInputStream (f), true));
    }
}

TEST_CASE ("Recorder captures input only while playing and reports the start position")
{
    Transport transport;
    transport.setSampleRate (48000.0);
    Recorder recorder (transport);

    const auto file = juce::File::createTempFile (".wav");
    Recorder::Slot slot;
    slot.trackId = 7;
    slot.firstInput = 0;
    slot.numInputs = 1;
    slot.file = file;

    REQUIRE (recorder.start ({ slot }, 48000.0).isEmpty());
    CHECK (recorder.isRecording());
    CHECK (recorder.getRecordStartSample() == -1);

    const int block = 480;
    std::vector<float> ramp ((size_t) block);
    const float* inputs[1] = { ramp.data() };

    // Stopped: nothing captured.
    recorder.processInput (inputs, 1, block);
    CHECK (recorder.getRecordStartSample() == -1);

    // Playing from sample 1000: capture ten blocks of a known ramp.
    transport.setPositionSamples (1000);
    transport.play();
    for (int b = 0; b < 10; ++b)
    {
        for (int i = 0; i < block; ++i)
            ramp[(size_t) i] = (float) (b * block + i) / 4800.0f;
        recorder.processInput (inputs, 1, block);
        transport.advance (block);
    }
    CHECK (recorder.getRecordStartSample() == 1000);

    auto takes = recorder.stop();
    CHECK_FALSE (recorder.isRecording());
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].trackId == 7);
    CHECK (takes[0].startSample == 1000);
    CHECK (takes[0].numSamples == 4800);
    CHECK (takes[0].numChannels == 1);
    CHECK (recorder.getDropoutCount() == 0);

    auto reader = readWav (file);
    REQUIRE (reader != nullptr);
    CHECK (reader->lengthInSamples == 4800);
    CHECK (reader->numChannels == 1);
    CHECK (reader->sampleRate == 48000.0);

    juce::AudioBuffer<float> data (1, 4800);
    reader->read (&data, 0, 4800, 0, true, false);
    CHECK_THAT (data.getSample (0, 0),    WithinAbs (0.0f, 1e-4));
    CHECK_THAT (data.getSample (0, 2400), WithinAbs (0.5f, 1e-4));
    CHECK_THAT (data.getSample (0, 4799), WithinAbs (4799.0f / 4800.0f, 1e-4));
    reader.reset();
    file.deleteFile();
}

TEST_CASE ("Recorder writes silence for missing inputs and drops empty takes")
{
    Transport transport;
    Recorder recorder (transport);

    const auto fileA = juce::File::createTempFile (".wav");
    const auto fileB = juce::File::createTempFile (".wav");
    Recorder::Slot a; a.trackId = 1; a.firstInput = 5; a.numInputs = 2; a.file = fileA;   // channels that don't exist
    Recorder::Slot b; b.trackId = 2; b.firstInput = 0; b.numInputs = 1; b.file = fileB;

    REQUIRE (recorder.start ({ a, b }, 44100.0).isEmpty());

    // Never played: stop() must delete both files and return no takes.
    auto takes = recorder.stop();
    CHECK (takes.empty());
    CHECK_FALSE (fileA.existsAsFile());
    CHECK_FALSE (fileB.existsAsFile());

    // Now record from missing channels: valid stereo file of zeros.
    REQUIRE (recorder.start ({ a }, 44100.0).isEmpty());
    transport.play();
    std::vector<float> noise (256, 0.9f);
    const float* inputs[1] = { noise.data() };
    recorder.processInput (inputs, 1, 256);
    takes = recorder.stop();
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].numChannels == 2);
    CHECK (takes[0].numSamples == 256);

    auto reader = readWav (fileA);
    REQUIRE (reader != nullptr);
    juce::AudioBuffer<float> data (2, 256);
    reader->read (&data, 0, 256, 0, true, true);
    CHECK (data.getMagnitude (0, 256) == 0.0f);
    reader.reset();
    fileA.deleteFile();
}

TEST_CASE ("Recorder refuses to start twice or with no slots")
{
    Transport transport;
    Recorder recorder (transport);
    CHECK (recorder.start ({}, 44100.0).isNotEmpty());

    const auto file = juce::File::createTempFile (".wav");
    Recorder::Slot s; s.file = file;
    REQUIRE (recorder.start ({ s }, 44100.0).isEmpty());
    CHECK (recorder.start ({ s }, 44100.0).isNotEmpty());
    recorder.stop();
    file.deleteFile();
}

TEST_CASE ("Graph mixes monitored inputs to the outputs and feeds the recorder")
{
    Transport transport;
    transport.setSampleRate (48000.0);
    AudioGraph graph (transport);
    Recorder recorder (transport);
    graph.setRecorder (&recorder);

    auto snap = std::make_unique<RenderSnapshot>();
    snap->monitors.push_back ({ /*firstInput*/ 1, /*numInputs*/ 1, 0.5f });
    graph.setSnapshot (std::move (snap));

    std::vector<float> in0 (64, 1.0f), in1 (64, 2.0f);
    const float* inputs[2] = { in0.data(), in1.data() };
    juce::AudioBuffer<float> out (2, 64);

    // Stopped transport still monitors.
    graph.renderBlock (inputs, 2, out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (1.0f, 1e-6));   // 2.0 * 0.5 on both outputs (mono monitor)
    CHECK_THAT (out.getSample (1, 10), WithinAbs (1.0f, 1e-6));

    // No monitors: silence.
    graph.setSnapshot (std::make_unique<RenderSnapshot>());
    graph.renderBlock (inputs, 2, out.getArrayOfWritePointers(), 2, 64);
    CHECK (out.getMagnitude (0, 64) == 0.0f);

    // The recorder sees the same inputs via the graph.
    const auto file = juce::File::createTempFile (".wav");
    Recorder::Slot slot; slot.firstInput = 1; slot.file = file;
    REQUIRE (recorder.start ({ slot }, 48000.0).isEmpty());
    transport.play();
    graph.renderBlock (inputs, 2, out.getArrayOfWritePointers(), 2, 64);
    auto takes = recorder.stop();
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].startSample == 0);
    CHECK (takes[0].numSamples == 64);
    graph.collectGarbage();
    file.deleteFile();
}

namespace
{
    struct PunchHarness
    {
        Transport transport;
        Recorder recorder { transport };
        juce::File file = juce::File::createTempFile (".wav");
        static constexpr int block = 480;
        std::vector<float> ones = std::vector<float> ((size_t) block, 1.0f);

        PunchHarness() { transport.setSampleRate (48000.0); }
        Recorder::Slot slot (int trackId) { Recorder::Slot s; s.trackId = trackId; s.firstInput = 0; s.numInputs = 1; s.file = trackId == 7 ? file : juce::File::createTempFile (".wav"); return s; }
        void run (int blocks) { const float* in[1] = { ones.data() }; for (int b = 0; b < blocks; ++b) { recorder.processInput (in, 1, block); transport.advance (block); } }
    };
}

TEST_CASE ("Whole-take recording reports one range covering the take; the auto punch range limits it sample-accurately")
{
    PunchHarness h;
    REQUIRE (h.recorder.start ({ h.slot (7) }, 48000.0).isEmpty());
    h.transport.setPositionSamples (48000);
    h.transport.play();
    h.run (10);
    auto takes = h.recorder.stop();
    REQUIRE (takes.size() == 1);
    REQUIRE (takes[0].ranges.size() == 1);
    CHECK (takes[0].ranges[0].timelineStart == 48000);
    CHECK (takes[0].ranges[0].fileOffset == 0);
    CHECK (takes[0].ranges[0].length == 4800);

    // Auto punch: rolling from 48000, record only [49000, 51000)
    REQUIRE (h.recorder.start ({ h.slot (7) }, 48000.0).isEmpty());
    h.recorder.setAutoPunch (49000, 51000);
    h.transport.setPositionSamples (48000);
    h.run (10);
    takes = h.recorder.stop();
    REQUIRE (takes.size() == 1);
    CHECK (takes[0].numSamples == 4800);              // the file holds the whole pass
    REQUIRE (takes[0].ranges.size() == 1);
    CHECK (takes[0].ranges[0].timelineStart == 49000);
    CHECK (takes[0].ranges[0].fileOffset == 1000);
    CHECK (takes[0].ranges[0].length == 2000);

    // A range that is never reached leaves nothing (the file is deleted)
    REQUIRE (h.recorder.start ({ h.slot (7) }, 48000.0).isEmpty());
    h.recorder.setAutoPunch (900000, 950000);
    h.run (4);
    CHECK (h.recorder.stop().empty());
    CHECK_FALSE (h.file.existsAsFile());
    h.file.deleteFile();
}

TEST_CASE ("Manual punch (QuickPunch / TrackPunch) creates one range per punch, per track")
{
    PunchHarness h;
    REQUIRE (h.recorder.start ({ h.slot (7), h.slot (8) }, 48000.0, 24, Recorder::PunchMode::manual).isEmpty());
    h.transport.setPositionSamples (0);
    h.transport.play();
    h.run (2);                                   // 0..960: nothing punched
    CHECK_FALSE (h.recorder.isPunched (-1));
    h.recorder.setPunch (-1, true);              // both tracks in at 960
    h.run (3);                                   // ..2400
    CHECK (h.recorder.isPunched (7));
    CHECK (h.recorder.isPunched (8));
    h.recorder.setPunch (8, false);              // track 8 out at 2400
    h.run (2);                                   // ..3360
    CHECK (h.recorder.isPunched (7));
    CHECK_FALSE (h.recorder.isPunched (8));
    h.recorder.setPunch (-1, false);             // track 7 out at 3360
    h.run (1);
    h.recorder.setPunch (7, true);               // track 7 back in at 3840
    h.run (2);                                   // ..4800, still punched at stop
    auto takes = h.recorder.stop();
    REQUIRE (takes.size() == 2);

    const auto* t7 = takes[0].trackId == 7 ? &takes[0] : &takes[1];
    const auto* t8 = takes[0].trackId == 8 ? &takes[0] : &takes[1];
    REQUIRE (t7->ranges.size() == 2);
    CHECK (t7->ranges[0].timelineStart == 960);
    CHECK (t7->ranges[0].fileOffset == 960);
    CHECK (t7->ranges[0].length == 2400);
    CHECK (t7->ranges[1].timelineStart == 3840);
    CHECK (t7->ranges[1].length == 960);     // closed by stop()
    REQUIRE (t8->ranges.size() == 1);
    CHECK (t8->ranges[0].timelineStart == 960);
    CHECK (t8->ranges[0].length == 1440);
    CHECK (t7->numSamples == 4800);
    for (const auto& t : takes) t.file.deleteFile();

    // Manual mode with no punch at all records nothing
    REQUIRE (h.recorder.start ({ h.slot (7) }, 48000.0, 24, Recorder::PunchMode::manual).isEmpty());
    h.run (3);
    CHECK (h.recorder.stop().empty());
}
