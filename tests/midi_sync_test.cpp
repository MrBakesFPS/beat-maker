#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sync/MidiSync.h>
#include <sequencer/MidiSequence.h>
#include <graph/AudioGraph.h>
#include <dsp/Instrument.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

TEST_CASE ("MIDI clock generator sends Start, 24 ticks per beat at exact times, Stop, and SPP + Continue on relocate")
{
    MidiClockGenerator gen;
    TransportSnapshot t; t.bpm = 120.0; t.playing = false; t.positionSeconds = 0.0;
    CHECK (gen.update (t, 0.0).empty());

    t.playing = true;
    auto out = gen.update (t, 1.0);
    REQUIRE (out.size() >= 2);
    CHECK (out[0].message.isMidiStart());
    CHECK (out[1].message.isMidiClock());   // tick 0 at the start

    // 0.5 s later at 120 BPM = 1 beat = 24 more ticks, spaced 1/48 s
    t.positionSeconds = 0.5;
    out = gen.update (t, 1.5);
    int clocks = 0; double lastTime = -1.0;
    for (const auto& m : out) if (m.message.isMidiClock()) { ++clocks; if (lastTime >= 0.0) CHECK_THAT (m.timeSeconds - lastTime, WithinAbs (1.0 / 48.0, 1e-6)); lastTime = m.timeSeconds; }
    CHECK (clocks == 24);
    CHECK_THAT (lastTime, WithinAbs (1.5, 1e-6));   // the tick at exactly beat 1 is "now"

    t.playing = false;
    out = gen.update (t, 1.6);
    REQUIRE (out.size() == 1);
    CHECK (out[0].message.isMidiStop());

    // Locate while stopped: SPP; then play from there: SPP + Continue
    t.positionSeconds = 2.0;   // beat 4 = 16 sixteenths
    out = gen.update (t, 1.7);
    REQUIRE (out.size() == 1);
    CHECK (out[0].message.isSongPositionPointer());
    CHECK (out[0].message.getSongPositionPointerMidiBeat() == 16);
    t.playing = true;
    out = gen.update (t, 1.8);
    REQUIRE (out.size() >= 2);
    CHECK (out[0].message.isSongPositionPointer());
    CHECK (out[1].message.isMidiContinue());
}

TEST_CASE ("MTC generator emits full frames on locate and 8 quarter frames per two frames that decode back")
{
    MtcGenerator gen (MtcFrameRate::fps25, 3600.0);   // session starts at 01:00:00:00
    TransportSnapshot t; t.playing = false; t.positionSeconds = 2.0;
    auto out = gen.update (t, 0.0);
    CHECK (out.empty());   // first call only primes
    t.positionSeconds = 3.0;
    out = gen.update (t, 0.1);
    REQUIRE (out.size() == 1);
    CHECK (out[0].message.isFullFrame());
    int h, m, s, f; juce::MidiMessage::SmpteTimecodeType type;
    out[0].message.getFullFrameParameters (h, m, s, f, type);
    CHECK (h == 1); CHECK (m == 0); CHECK (s == 3); CHECK (f == 0); CHECK ((int) type == (int) MtcFrameRate::fps25);

    t.playing = true;
    out = gen.update (t, 0.2);          // full frame + quarter frame 0
    t.positionSeconds = 3.0 + 2.0 / 25.0;   // two frames later: quarters 1..8
    out = gen.update (t, 0.28);
    MtcChaser chaser (MtcFrameRate::fps30);
    std::optional<double> decoded;
    int quarters = 0;
    for (const auto& msg : out) if (msg.message.isQuarterFrame()) { ++quarters; auto a = chaser.handle (msg.message, msg.timeSeconds); if (a.positionSeconds) decoded = a.positionSeconds; }
    CHECK (quarters == 8);
    // The chaser needs pieces 0..7 in order: the earlier update sent piece 0, this one 1..7 and the next 0, so feed them all in order.
    MtcChaser chaser2 (MtcFrameRate::fps30);
    std::vector<juce::MidiMessage> all;
    MtcGenerator gen2 (MtcFrameRate::fps25, 3600.0);
    TransportSnapshot t2; t2.playing = true; t2.positionSeconds = 3.0;
    gen2.update (t2, 0.0);
    t2.positionSeconds = 3.0 + 4.0 / 25.0;
    for (const auto& msg : gen2.update (t2, 0.16)) if (msg.message.isQuarterFrame()) all.push_back (msg.message);
    std::optional<double> got;
    for (const auto& msg : all) { auto a = chaser2.handle (msg, 0.0); if (a.positionSeconds) { got = a.positionSeconds; break; } }
    REQUIRE (got.has_value());
    CHECK (chaser2.getFrameRate() == MtcFrameRate::fps25);
    // Quarter-frame sequences start on even frames: 3.0 s is frame 75 (odd), so the first full
    // sequence encodes frame 76 (3.04 s) and the receiver adds the two frames it spans.
    CHECK_THAT (*got, WithinAbs (3600.0 + 3.04 + 2.0 / 25.0, 1e-6));

    // Timecode maths
    auto tc = Timecode::fromSeconds (3661.5, MtcFrameRate::fps30);
    CHECK (tc.hours == 1); CHECK (tc.minutes == 1); CHECK (tc.seconds == 1); CHECK (tc.frames == 15);
    CHECK (tc.toString() == "01:01:01:15");
    CHECK_THAT (tc.toSeconds (MtcFrameRate::fps30), WithinAbs (3661.5, 1e-9));
}

TEST_CASE ("MIDI clock chaser estimates tempo from ticks and follows Start/Stop/SPP")
{
    MidiClockChaser chaser;
    auto a = chaser.handle (juce::MidiMessage::midiStart(), 0.0);
    CHECK (a.play); REQUIRE (a.locateBeats.has_value()); CHECK (*a.locateBeats == 0.0);
    std::optional<double> bpm;
    const double secondsPerTick = 60.0 / 100.0 / 24.0;   // 100 BPM
    for (int i = 0; i < 48; ++i) { auto r = chaser.handle (juce::MidiMessage::midiClock(), i * secondsPerTick); if (r.bpm) bpm = r.bpm; }
    REQUIRE (bpm.has_value());
    CHECK_THAT (*bpm, WithinAbs (100.0, 0.2));
    CHECK_THAT (chaser.getBeatPosition(), WithinAbs (2.0, 1e-9));
    a = chaser.handle (juce::MidiMessage::midiStop(), 2.0);
    CHECK (a.stop); CHECK_FALSE (chaser.isRunning());
    a = chaser.handle (juce::MidiMessage::songPositionPointer (32), 2.1);   // 32 sixteenths = 8 beats
    REQUIRE (a.locateBeats.has_value()); CHECK_THAT (*a.locateBeats, WithinAbs (8.0, 1e-9));
    a = chaser.handle (juce::MidiMessage::midiContinue(), 2.2);
    CHECK (a.cont); CHECK (a.play); CHECK_THAT (*a.locateBeats, WithinAbs (8.0, 1e-9));
}

TEST_CASE ("Real-time properties transpose, scale velocity, quantize, delay and stretch notes at playback only")
{
    MidiRealtimeProps p;
    CHECK (p.isIdentity());
    p.transpose = 12; p.velocityScale = 0.5f; p.velocityOffset = 10; p.quantize = true; p.quantizeBeats = 0.25; p.quantizeStrength = 0.5f; p.durationScale = 2.0f;
    CHECK_FALSE (p.isIdentity());
    NoteEvent n { 60, 100, 0.3, 0.5 };
    const auto applied = p.apply (n);
    CHECK (applied.pitch == 72);
    CHECK (applied.velocity == 60);
    CHECK_THAT (applied.startBeat, WithinAbs (0.275, 1e-9));   // halfway from 0.3 to the 0.25 grid line
    CHECK_THAT (applied.lengthBeats, WithinAbs (1.0, 1e-9));
    CHECK (n.pitch == 60);   // stored note untouched

    // Through the graph: a delayed, transposed note starts later and an octave up
    Transport transport; transport.setSampleRate (48000.0); transport.setBpm (120.0);
    AudioGraph graph (transport);
    auto inst = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::subtractive, 48000.0));
    auto params = std::make_shared<InstrumentParams> (Instrument::defaultParams (InstrumentType::subtractive));
    params->values[SubtractiveParams::wave] = 3.0f; params->values[SubtractiveParams::osc2] = 0.0f; params->values[SubtractiveParams::cutoff] = 20000.0f;
    params->values[SubtractiveParams::attack] = 0.0f; params->values[SubtractiveParams::sustain] = 1.0f; params->values[SubtractiveParams::level] = 1.0f; params->values[SubtractiveParams::filterEnv] = 0.0f;
    auto seq = std::make_shared<MidiSequence>(); seq->lengthBeats = 4.0; seq->notes.push_back ({ 69, 127, 0.0, 2.0 });
    auto snap = std::make_unique<RenderSnapshot>();
    snap->instruments.push_back ({ 1, inst, params });
    RenderMidiClip clip; clip.sequence = seq; clip.instrumentId = 1; clip.timelineStart = 0; clip.length = 96000;
    clip.props.delayMs = 100.0; clip.props.transpose = 12;
    snap->midiClips.push_back (clip);
    graph.setSnapshot (std::move (snap));
    transport.play();
    juce::AudioBuffer<float> out (1, 48000);
    out.setSize (1, 48000, false, true, true);
    for (int pos = 0; pos < 48000; pos += 512) { float* ptr = out.getWritePointer (0, pos); graph.renderBlock (&ptr, 1, juce::jmin (512, 48000 - pos)); }
    CHECK (out.getMagnitude (0, 0, 4700) == 0.0f);            // silent for the 100 ms delay
    CHECK (out.getMagnitude (0, 4900, 20000) > 0.5f);
    const float* d = out.getReadPointer (0);
    int crossings = 0;
    for (int i = 10001; i < 40000; ++i) if ((d[i - 1] < 0.0f) != (d[i] < 0.0f)) ++crossings;
    CHECK_THAT (crossings * 0.5 * 48000.0 / 30000.0, WithinAbs (880.0, 5.0));   // transposed an octave
    graph.collectGarbage();
}
