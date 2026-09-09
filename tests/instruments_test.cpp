#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <dsp/Instrument.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    // Subtractive synth set up for exact timing tests: instant attack, open
    // filter, sine, 5 ms release (240 samples at 48k).
    std::shared_ptr<const InstrumentParams> testParams()
    {
        auto p = Instrument::defaultParams (InstrumentType::subtractive);
        p.presetName = "Test";
        p.values[SubtractiveParams::wave] = 3.0f;      // sine
        p.values[SubtractiveParams::osc2] = 0.0f;
        p.values[SubtractiveParams::cutoff] = 20000.0f;
        p.values[SubtractiveParams::resonance] = 0.0f;
        p.values[SubtractiveParams::filterEnv] = 0.0f;
        p.values[SubtractiveParams::attack] = 0.0f;
        p.values[SubtractiveParams::decay] = 0.01f;
        p.values[SubtractiveParams::sustain] = 1.0f;
        p.values[SubtractiveParams::release] = 0.005f;
        p.values[SubtractiveParams::level] = 1.0f;
        return std::make_shared<const InstrumentParams> (p);
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

        bool silentBetween (int from, int to) const { return out.getMagnitude (0, from, to - from) == 0.0f; }
        bool loudBetween (int from, int to) const   { return out.getMagnitude (0, from, to - from) > 0.05f; }
    };

    // Renders `seconds` of one held note and returns the mono output.
    juce::AudioBuffer<float> renderNote (Instrument& inst, const InstrumentParams& p, int pitch, double seconds, int gate = -1)
    {
        const int n = (int) (seconds * inst.getSampleRate());
        juce::AudioBuffer<float> out (1, n);
        out.clear();
        float* ptr = out.getWritePointer (0);
        inst.noteOn (pitch, 1.0f, 1.0f, 0, gate, p);
        for (int done = 0; done < n; done += 512)
        {
            float* block = ptr + done;
            inst.render (&block, 1, juce::jmin (512, n - done), p);
        }
        return out;
    }

    // Dominant frequency by zero-crossing count over [from, to).
    double zeroCrossingHz (const juce::AudioBuffer<float>& buf, int from, int to, double sr)
    {
        const float* d = buf.getReadPointer (0);
        int crossings = 0;
        for (int i = from + 1; i < to; ++i) if ((d[i - 1] < 0.0f) != (d[i] < 0.0f)) ++crossings;
        return crossings * 0.5 * sr / (to - from);
    }
}

TEST_CASE ("Voice starts after its delay, holds for the gate, then releases")
{
    auto synth = Instrument::create (InstrumentType::subtractive, 48000.0);
    auto params = testParams();

    juce::AudioBuffer<float> out (1, 2000);
    out.clear();
    float* ptr = out.getWritePointer (0);

    synth->noteOn (69, 1.0f, 1.0f, /*delay*/ 100, /*gate*/ 1000, *params);
    synth->render (&ptr, 1, 2000, *params);

    CHECK (out.getMagnitude (0, 0, 100) == 0.0f);                 // before the delay
    CHECK (out.getMagnitude (0, 100, 1000) > 0.5f);               // sounding (sine peak ~1)
    CHECK (out.getMagnitude (0, 1100 + 300, 500) == 0.0f);        // released (240-sample release) and off
    CHECK (synth->getNumActiveVoices() == 0);
}

TEST_CASE ("Held notes stay until noteOff or allNotesOff")
{
    auto synth = Instrument::create (InstrumentType::subtractive, 48000.0);
    auto params = testParams();
    juce::AudioBuffer<float> out (1, 512);
    auto renderBlock = [&] { out.clear(); float* ptr = out.getWritePointer (0); synth->render (&ptr, 1, 512, *params); };

    synth->noteOn (60, 1.0f, 1.0f, 0, -1, *params);
    synth->noteOn (64, 1.0f, 1.0f, 0, -1, *params);
    for (int i = 0; i < 20; ++i) renderBlock();
    CHECK (synth->getNumActiveVoices() == 2);
    CHECK (out.getMagnitude (0, 0, 512) > 0.5f);

    synth->noteOff (60);
    for (int i = 0; i < 4; ++i) renderBlock();
    CHECK (synth->getNumActiveVoices() == 1);

    synth->allNotesOff (true);
    CHECK (synth->getNumActiveVoices() == 0);
}

TEST_CASE ("Voice stealing keeps polyphony bounded")
{
    auto synth = Instrument::create (InstrumentType::subtractive, 48000.0);
    auto params = testParams();
    for (int i = 0; i < 16 + 5; ++i)
        synth->noteOn (40 + i, 1.0f, 1.0f, 0, -1, *params);
    CHECK (synth->getNumActiveVoices() == 16);
}

TEST_CASE ("Graph schedules MIDI clip notes in beats, loops the sequence and cuts at the clip end")
{
    Renderer r;
    auto seq = std::make_shared<MidiSequence>();
    seq->lengthBeats = 2.0;                        // 48000 samples at 120 BPM / 48k
    seq->notes.push_back ({ 69, 127, 0.0, 0.25 });  // 0..6000
    seq->notes.push_back ({ 72, 127, 1.0, 0.25 });  // 24000..30000

    auto inst = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::subtractive, 48000.0));
    auto snap = std::make_unique<RenderSnapshot>();
    snap->instruments.push_back ({ 5, inst, testParams() });
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
    auto inst = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::subtractive, 48000.0));
    auto snap = std::make_unique<RenderSnapshot>();
    snap->instruments.push_back ({ 3, inst, testParams() });
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
    snap2->instruments.push_back ({ 3, inst, testParams() });
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

TEST_CASE ("Instruments leave the graph with their voices killed when they leave the snapshot")
{
    Renderer r;
    auto inst = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::subtractive, 48000.0));
    auto snap = std::make_unique<RenderSnapshot>();
    snap->instruments.push_back ({ 1, inst, testParams() });
    r.graph.setSnapshot (std::move (snap));
    r.graph.triggerNotePreview (1, 60, 1.0f, 1.0);
    r.renderAll (512, 512);
    CHECK (r.graph.getNumSynthVoices() == 1);
    CHECK (inst->getNumActiveVoices() == 1);

    r.graph.setSnapshot (std::make_unique<RenderSnapshot>());
    r.renderAll (512, 512);
    CHECK (r.graph.getNumSynthVoices() == 0);
    CHECK (inst->getNumActiveVoices() == 0);       // the model-owned instance was silenced, not leaked
    CHECK (r.silentBetween (0, 512));
    r.graph.collectGarbage();
}

TEST_CASE ("Swapping a track's instrument instance rebinds the slot without touching other tracks")
{
    Renderer r;
    auto a = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::subtractive, 48000.0));
    auto b = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::fm, 48000.0));
    auto fmParams = std::make_shared<const InstrumentParams> (Instrument::defaultParams (InstrumentType::fm));

    auto snap = std::make_unique<RenderSnapshot>();
    snap->instruments.push_back ({ 1, a, testParams() });
    r.graph.setSnapshot (std::move (snap));
    r.graph.triggerNotePreview (1, 60, 1.0f, 2.0);
    r.renderAll (512, 512);
    CHECK (a->getNumActiveVoices() == 1);

    auto snap2 = std::make_unique<RenderSnapshot>();
    snap2->instruments.push_back ({ 1, b, fmParams });
    r.graph.setSnapshot (std::move (snap2));
    r.graph.triggerNotePreview (1, 60, 1.0f, 2.0);
    r.renderAll (512, 512);
    CHECK (a->getNumActiveVoices() == 0);           // old instance silenced
    CHECK (b->getNumActiveVoices() == 1);           // new one plays the preview
    CHECK (r.loudBetween (1, 512));
    r.graph.collectGarbage();
}

TEST_CASE ("Every instrument type sounds, decays after release and exposes metadata")
{
    for (auto type : Instrument::availableTypes())
    {
        INFO ("type " << Instrument::typeName (type));
        auto inst = Instrument::create (type, 48000.0);
        REQUIRE (inst != nullptr);
        CHECK (inst->getType() == type);
        CHECK (! Instrument::paramInfo (type).empty());
        CHECK (Instrument::paramInfo (type).size() <= 24);
        CHECK (! Instrument::presets (type).empty());
        CHECK (juce::String (Instrument::typeName (type)).isNotEmpty());
        CHECK (juce::String (Instrument::typeCategory (type)).isNotEmpty());      // the chooser files it under a category
        CHECK (juce::String (Instrument::typeDescription (type)).length() > 30);  // and says what it is for
        for (const auto& preset : Instrument::presets (type)) CHECK (preset.presetName.isNotEmpty());

        auto p = Instrument::defaultParams (type);
        CHECK (p.type == type);
        if (type == InstrumentType::sampler)
        {
            // Sampler needs a sample: a 1-second 440 Hz sine at C4
            auto sample = std::make_shared<juce::AudioBuffer<float>> (1, 48000);
            for (int i = 0; i < 48000; ++i) sample->setSample (0, i, std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));
            p.sample = sample; p.sampleRate = 48000.0; p.rootNote = 69;
            p.values[SamplerParams::loop] = 1.0f;
        }

        // Half a second held, then released; must be audible while held and silent well after release.
        const int n = 48000;
        auto out = renderNote (*inst, p, 60, 1.0, /*gate*/ n / 2);
        CHECK (out.getMagnitude (0, 2000, n / 2 - 2000) > 0.02f);
        CHECK (out.getMagnitude (0, 0, n) < 1.5f);                              // bounded
        for (int i = 0; i < 8; ++i) { float* z = out.getWritePointer (0); inst->render (&z, 1, 512, p); }   // drain
        CHECK (inst->getNumActiveVoices() == 0);

        inst->allNotesOff (true);
        CHECK (inst->getNumActiveVoices() == 0);
    }
}

TEST_CASE ("Sampler plays the sample at its root pitch and transposes by semitones")
{
    auto sample = std::make_shared<juce::AudioBuffer<float>> (1, 96000);
    for (int i = 0; i < 96000; ++i) sample->setSample (0, i, std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / 48000.0));

    auto p = Instrument::defaultParams (InstrumentType::sampler);
    p.sample = sample; p.sampleRate = 48000.0; p.rootNote = 57;   // A3 = 220 Hz
    p.values[SamplerParams::attack] = 0.0f;
    p.values[SamplerParams::cutoff] = 20000.0f;
    auto inst = Instrument::create (InstrumentType::sampler, 48000.0);

    auto root = renderNote (*inst, p, 57, 0.5);
    CHECK_THAT (zeroCrossingHz (root, 1000, 24000, 48000.0), WithinAbs (220.0, 3.0));
    inst->allNotesOff (true);

    auto octave = renderNote (*inst, p, 69, 0.5);
    CHECK_THAT (zeroCrossingHz (octave, 1000, 24000, 48000.0), WithinAbs (440.0, 5.0));
    inst->allNotesOff (true);

    // A one-shot stops at the end of the sample: 96000 samples at 2x rate = 48000 output samples
    p.values[SamplerParams::loop] = 0.0f;
    auto shot = renderNote (*inst, p, 69, 1.5);
    CHECK (shot.getMagnitude (0, 1000, 40000) > 0.5f);
    CHECK (shot.getMagnitude (0, 50000, 20000) == 0.0f);
    CHECK (inst->getNumActiveVoices() == 0);

    // With no sample loaded, notes are ignored instead of crashing.
    p.sample = nullptr;
    inst->noteOn (60, 1.0f, 1.0f, 0, -1, p);
    CHECK (inst->getNumActiveVoices() == 0);

    // A sample recorded at 96k plays at the right pitch on a 48k engine: the
    // same buffer tagged 96k describes a 440 Hz tone, so root pitch reads 440 Hz.
    p.sample = sample; p.sampleRate = 96000.0;
    auto fast = renderNote (*inst, p, 57, 0.5);
    CHECK_THAT (zeroCrossingHz (fast, 1000, 24000, 48000.0), WithinAbs (440.0, 5.0));
}

TEST_CASE ("Bass synth is monophonic with last-note priority and glides between pitches")
{
    auto p = Instrument::defaultParams (InstrumentType::bass);
    p.values[BassParams::sub] = 0.0f;
    p.values[BassParams::cutoff] = 20000.0f;
    p.values[BassParams::filterEnv] = 0.0f;
    p.values[BassParams::drive] = 0.0f;
    p.values[BassParams::resonance] = 0.0f;
    p.values[BassParams::wave] = 1.0f;             // square: clean zero crossings
    p.values[BassParams::glide] = 0.0f;
    auto inst = Instrument::create (InstrumentType::bass, 48000.0);

    inst->noteOn (45, 1.0f, 1.0f, 0, -1, p);       // A2 110 Hz
    inst->noteOn (57, 1.0f, 1.0f, 0, -1, p);       // A3 220 Hz, legato
    CHECK (inst->getNumActiveVoices() == 1);

    juce::AudioBuffer<float> out (1, 24000);
    out.clear();
    float* ptr = out.getWritePointer (0);
    inst->render (&ptr, 1, 24000, p);
    CHECK_THAT (zeroCrossingHz (out, 4000, 24000, 48000.0), WithinAbs (220.0, 4.0));

    inst->noteOff (57);                            // back to the still-held A2
    out.clear(); ptr = out.getWritePointer (0);
    inst->render (&ptr, 1, 24000, p);
    CHECK (inst->getNumActiveVoices() == 1);
    CHECK_THAT (zeroCrossingHz (out, 4000, 24000, 48000.0), WithinAbs (110.0, 4.0));

    inst->noteOff (45);
    out.clear(); ptr = out.getWritePointer (0);
    inst->render (&ptr, 1, 24000, p);
    CHECK (inst->getNumActiveVoices() == 0);

    // Glide: with a 100 ms glide the pitch is still between the notes shortly after the change.
    p.values[BassParams::glide] = 0.1f;
    inst->noteOn (45, 1.0f, 1.0f, 0, -1, p);
    out.clear(); ptr = out.getWritePointer (0);
    inst->render (&ptr, 1, 24000, p);
    inst->noteOn (57, 1.0f, 1.0f, 0, -1, p);
    out.clear(); ptr = out.getWritePointer (0);
    inst->render (&ptr, 1, 2400, p);               // first 50 ms after the change
    const double early = zeroCrossingHz (out, 0, 2400, 48000.0);
    CHECK (early > 120.0);
    CHECK (early < 210.0);
}

TEST_CASE ("Presets are distinct and PolyBLEP waveforms stay bounded")
{
    const auto presets = Instrument::presets (InstrumentType::subtractive);
    REQUIRE (presets.size() >= 4);
    for (size_t i = 1; i < presets.size(); ++i)
        CHECK (presets[i].presetName != presets[0].presetName);

    for (float wave : { 0.0f, 1.0f, 2.0f, 3.0f })
    {
        auto p = *testParams();
        p.values[SubtractiveParams::wave] = wave;
        auto synth = Instrument::create (InstrumentType::subtractive, 48000.0);
        auto out = renderNote (*synth, p, 100, 4096.0 / 48000.0);   // high note: aliasing-prone
        CHECK (out.getMagnitude (0, 0, 4096) > 0.3f);
        CHECK (out.getMagnitude (0, 0, 4096) < 1.6f);
    }
}

TEST_CASE ("Instrument params apply to already-sounding voices (copy-on-write swap)")
{
    auto synth = Instrument::create (InstrumentType::subtractive, 48000.0);
    auto loud = *testParams();
    auto quiet = loud; quiet.values[SubtractiveParams::level] = 0.1f;

    juce::AudioBuffer<float> out (1, 4800);
    out.clear(); float* ptr = out.getWritePointer (0);
    synth->noteOn (69, 1.0f, 1.0f, 0, -1, loud);
    synth->render (&ptr, 1, 4800, loud);
    CHECK (out.getMagnitude (0, 2400, 2400) > 0.9f);

    out.clear(); ptr = out.getWritePointer (0);
    synth->render (&ptr, 1, 4800, quiet);
    CHECK (out.getMagnitude (0, 0, 4800) < 0.12f);
    CHECK (out.getMagnitude (0, 0, 4800) > 0.05f);
}

TEST_CASE ("Instrument categories are listed in display order and each holds an instrument")
{
    const auto cats = Instrument::categories();
    REQUIRE (! cats.empty());
    CHECK (cats.front() == "Synths");
    for (const auto& c : cats)
    {
        int n = 0;
        for (auto type : Instrument::availableTypes()) if (c == Instrument::typeCategory (type)) ++n;
        CHECK (n >= 1);
    }
    for (size_t i = 0; i < cats.size(); ++i) for (size_t j = i + 1; j < cats.size(); ++j) CHECK (cats[i] != cats[j]);
    CHECK (juce::String (Instrument::typeCategory (InstrumentType::none)).isEmpty());
}
