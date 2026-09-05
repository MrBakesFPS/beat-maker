#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <dsp/Synth.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    // Instant attack, fully open filter, sine: onsets are visible at exact samples.
    std::shared_ptr<const SynthParams> testParams()
    {
        SynthParams p;
        p.name = "Test";
        p.wave = SynthParams::Wave::sine;
        p.secondOscillator = false;
        p.cutoffHz = 20000.0f;
        p.resonance = 0.0f;
        p.attackSeconds = 0.0f;
        p.decaySeconds = 0.01f;
        p.sustainLevel = 1.0f;
        p.releaseSeconds = 0.005f;   // 240 samples at 48k
        p.gain = 1.0f;
        return std::make_shared<const SynthParams> (p);
    }

    struct Renderer
    {
        Transport transport;
        AudioGraph graph { transport };
        juce::AudioBuffer<float> out { 1, 0 };

        Renderer() { transport.setSampleRate (48000.0); transport.setBpm (120.0); }

        void renderAll (int total, int blockSize)
        {
            out.setSize (1, total, false, true, true);
            for (int done = 0; done < total; )
            {
                const int n = juce::jmin (blockSize, total - done);
                float* ptr = out.getWritePointer (0, done);
                graph.renderBlock (&ptr, 1, n);
                done += n;
            }
        }

        bool silentBetween (int from, int to) const
        {
            return out.getMagnitude (0, from, to - from) == 0.0f;
        }
        bool loudBetween (int from, int to) const
        {
            return out.getMagnitude (0, from, to - from) > 0.05f;
        }
    };
}

TEST_CASE ("Synth voice starts after its delay, holds for the gate, then releases")
{
    Synth synth;
    synth.prepare (48000.0);
    auto params = testParams();
    synth.setParams (params.get());

    juce::AudioBuffer<float> out (1, 2000);
    out.clear();
    float* ptr = out.getWritePointer (0);

    synth.noteOn (69, 1.0f, /*delay*/ 100, /*gate*/ 1000);
    synth.render (&ptr, 1, 2000);

    CHECK (out.getMagnitude (0, 0, 100) == 0.0f);                 // before the delay
    CHECK (out.getMagnitude (0, 100, 1000) > 0.5f);               // sounding (sine peak ~1)
    CHECK (out.getMagnitude (0, 1100 + 300, 500) == 0.0f);        // released (240-sample release) and off
    CHECK (synth.getNumActiveVoices() == 0);
}

TEST_CASE ("Held notes stay until noteOff or allNotesOff")
{
    Synth synth;
    synth.prepare (48000.0);
    auto params = testParams();
    synth.setParams (params.get());
    juce::AudioBuffer<float> out (1, 512);
    // Note: fetch the pointer after clear() so the buffer's "is clear" flag
    // is reset, otherwise getMagnitude() reports 0 regardless of contents.
    auto renderBlock = [&] { out.clear(); float* ptr = out.getWritePointer (0); synth.render (&ptr, 1, 512); };

    synth.noteOn (60, 1.0f, 0, -1);
    synth.noteOn (64, 1.0f, 0, -1);
    for (int i = 0; i < 20; ++i) renderBlock();
    CHECK (synth.getNumActiveVoices() == 2);
    CHECK (out.getMagnitude (0, 0, 512) > 0.5f);

    synth.noteOff (60);
    for (int i = 0; i < 4; ++i) renderBlock();
    CHECK (synth.getNumActiveVoices() == 1);

    synth.allNotesOff (true);
    CHECK (synth.getNumActiveVoices() == 0);
}

TEST_CASE ("Voice stealing keeps polyphony bounded")
{
    Synth synth;
    synth.prepare (48000.0);
    auto params = testParams();
    synth.setParams (params.get());
    for (int i = 0; i < Synth::maxVoices + 5; ++i)
        synth.noteOn (40 + i, 1.0f, 0, -1);
    CHECK (synth.getNumActiveVoices() == Synth::maxVoices);
}

TEST_CASE ("Graph schedules MIDI clip notes in beats, loops the sequence and cuts at the clip end")
{
    Renderer r;
    auto seq = std::make_shared<MidiSequence>();
    seq->lengthBeats = 2.0;                        // 48000 samples at 120 BPM / 48k
    seq->notes.push_back ({ 69, 127, 0.0, 0.25 });  // 0..6000
    seq->notes.push_back ({ 72, 127, 1.0, 0.25 });  // 24000..30000

    auto snap = std::make_unique<RenderSnapshot>();
    snap->synths.push_back ({ 5, testParams() });
    snap->midiClips.push_back ({ seq, 5, /*start*/ 0, /*length*/ 60000, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();
    r.renderAll (80000, 512);

    CHECK (r.loudBetween (10, 5000));
    CHECK (r.silentBetween (7000, 23990));         // note off + 240-sample release finished
    CHECK (r.loudBetween (24010, 29000));
    CHECK (r.silentBetween (31000, 47990));
    CHECK (r.loudBetween (48010, 53000));          // loop iteration 2
    // The second iteration's second note (72000) is past the clip end (60000): silence.
    CHECK (r.silentBetween (62000, 80000));
}

TEST_CASE ("Note previews only reach instruments in the snapshot, and stopping releases notes")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->synths.push_back ({ 3, testParams() });
    r.graph.setSnapshot (std::move (snap));

    r.graph.triggerNotePreview (99, 60, 1.0f, 0.1);   // unknown instrument: ignored
    r.renderAll (512, 512);
    CHECK (r.silentBetween (0, 512));
    CHECK (r.graph.getNumSynthVoices() == 0);

    r.graph.triggerNotePreview (3, 60, 1.0f, 0.1);
    r.renderAll (512, 512);
    CHECK (r.loudBetween (1, 512));
    CHECK (r.graph.getNumSynthVoices() == 1);

    // A held sequencer note is released when the transport stops.
    auto seq = std::make_shared<MidiSequence>();
    seq->lengthBeats = 4.0;
    seq->notes.push_back ({ 60, 127, 0.0, 4.0 });   // whole loop
    auto snap2 = std::make_unique<RenderSnapshot>();
    snap2->synths.push_back ({ 3, testParams() });
    snap2->midiClips.push_back ({ seq, 3, 0, 480000, 1.0f });
    r.graph.setSnapshot (std::move (snap2));
    r.transport.play();
    r.renderAll (4096, 512);
    CHECK (r.graph.getNumSynthVoices() >= 1);
    r.transport.stop();
    r.renderAll (4096, 512);                         // release (240 samples) completes
    CHECK (r.graph.getNumSynthVoices() == 0);
    r.graph.collectGarbage();
}

TEST_CASE ("Synths vanish from slots when they leave the snapshot")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->synths.push_back ({ 1, testParams() });
    r.graph.setSnapshot (std::move (snap));
    r.graph.triggerNotePreview (1, 60, 1.0f, 1.0);
    r.renderAll (512, 512);
    CHECK (r.graph.getNumSynthVoices() == 1);

    r.graph.setSnapshot (std::make_unique<RenderSnapshot>());
    r.renderAll (512, 512);
    CHECK (r.graph.getNumSynthVoices() == 0);
    CHECK (r.silentBetween (0, 512));
    r.graph.collectGarbage();
}

TEST_CASE ("Presets are distinct and PolyBLEP waveforms stay bounded")
{
    const auto presets = SynthParams::presets();
    REQUIRE (presets.size() >= 4);
    for (size_t i = 1; i < presets.size(); ++i)
        CHECK (presets[i].name != presets[0].name);

    for (auto wave : { SynthParams::Wave::saw, SynthParams::Wave::square, SynthParams::Wave::triangle, SynthParams::Wave::sine })
    {
        SynthParams p;
        p.wave = wave; p.secondOscillator = false; p.cutoffHz = 20000.0f; p.attackSeconds = 0.0f; p.sustainLevel = 1.0f; p.gain = 1.0f;
        Synth synth;
        synth.prepare (48000.0);
        synth.setParams (&p);
        synth.noteOn (100, 1.0f, 0, -1);   // high note: aliasing-prone
        juce::AudioBuffer<float> out (1, 4096);
        out.clear();
        float* ptr = out.getWritePointer (0);
        synth.render (&ptr, 1, 4096);
        CHECK (out.getMagnitude (0, 0, 4096) > 0.3f);
        CHECK (out.getMagnitude (0, 0, 4096) < 1.6f);
    }
}
