#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <dsp/Effects.h>
#include <graph/AudioGraph.h>
#include <MixerCommands.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <thread>

using namespace beatmaker::engine;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    juce::AudioBuffer<float> sineBuffer (double freq, float amp, int length, int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));
        return b;
    }
    float dbOf (float v) { return juce::Decibels::gainToDecibels (v, -120.0f); }

    // Runs `fx` over `audio` in 512-sample blocks with `key` as sidechain (may be null).
    void run (Effect& fx, juce::AudioBuffer<float>& audio, const juce::AudioBuffer<float>* key, const InsertParams& p)
    {
        const int n = audio.getNumSamples();
        for (int pos = 0; pos < n; pos += 512)
        {
            const int count = juce::jmin (512, n - pos);
            juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), audio.getNumChannels(), pos, count);
            const float* k[2] = { nullptr, nullptr };
            if (key != nullptr) { k[0] = key->getReadPointer (0, pos); k[1] = key->getReadPointer (juce::jmin (1, key->getNumChannels() - 1), pos); fx.setSidechain (k, 2); }
            fx.process (block, count, p);
            fx.clearSidechain();
        }
    }
}

TEST_CASE ("Compressor and gate follow the key input instead of the audio when one is connected")
{
    auto comp = Effect::create (EffectType::compressor, sr);
    auto p = Effect::defaultParams (EffectType::compressor);
    p.values[CompressorEffect::threshold] = -30.0f;
    p.values[CompressorEffect::ratio] = 8.0f;
    p.values[CompressorEffect::attack] = 1.0f;
    p.values[CompressorEffect::release] = 50.0f;
    CHECK (comp->acceptsSidechain());

    // Quiet audio (-40 dB) never reaches the threshold on its own...
    auto quiet = sineBuffer (440.0, 0.01f, 48000);
    run (*comp, quiet, nullptr, p);
    CHECK_THAT (dbOf (quiet.getRMSLevel (0, 24000, 24000)), WithinAbs (dbOf (0.01f / juce::MathConstants<float>::sqrt2), 0.5f));
    CHECK (comp->getMeter() < 0.1f);

    // ...but a loud key makes it duck.
    comp->reset();
    auto ducked = sineBuffer (440.0, 0.01f, 48000);
    auto key = sineBuffer (100.0, 0.5f, 48000);
    run (*comp, ducked, &key, p);
    CHECK (dbOf (ducked.getRMSLevel (0, 24000, 24000)) < dbOf (0.01f / juce::MathConstants<float>::sqrt2) - 15.0f);
    CHECK (comp->getMeter() > 15.0f);

    // Gate: steady tone below threshold stays closed, a loud key opens it.
    auto gate = Effect::create (EffectType::gate, sr);
    auto gp = Effect::defaultParams (EffectType::gate);
    gp.values[GateEffect::threshold] = -20.0f;
    gp.values[GateEffect::range] = -60.0f;
    gp.values[GateEffect::ratio] = 10.0f;
    CHECK (gate->acceptsSidechain());
    auto closed = sineBuffer (440.0, 0.05f, 48000);   // -26 dB
    run (*gate, closed, nullptr, gp);
    CHECK (dbOf (closed.getRMSLevel (0, 24000, 24000)) < -45.0f);
    gate->reset();
    auto opened = sineBuffer (440.0, 0.05f, 48000);
    auto loudKey = sineBuffer (100.0, 0.5f, 48000);
    run (*gate, opened, &loudKey, gp);
    CHECK_THAT (dbOf (opened.getRMSLevel (0, 24000, 24000)), WithinAbs (dbOf (0.05f / juce::MathConstants<float>::sqrt2), 0.5f));

    // Non-dynamics effects ignore keys.
    CHECK_FALSE (Effect::create (EffectType::eq, sr)->acceptsSidechain());
    CHECK_FALSE (Effect::create (EffectType::delay, sr)->acceptsSidechain());
}

TEST_CASE ("De-esser keyed from an external signal reduces the audio when the key has highs")
{
    auto de = Effect::create (EffectType::deesser, sr);
    auto p = Effect::defaultParams (EffectType::deesser);
    p.values[DeEsserEffect::frequency] = 4000.0f;
    p.values[DeEsserEffect::threshold] = -40.0f;
    p.values[DeEsserEffect::range] = 12.0f;
    auto audio = sineBuffer (8000.0, 0.3f, 48000);
    auto silentKey = sineBuffer (100.0, 0.0f, 48000);
    run (*de, audio, &silentKey, p);
    CHECK (de->getMeter() < 0.1f);      // key is silent: no reduction even though the audio itself is bright
    de->reset();
    auto audio2 = sineBuffer (8000.0, 0.3f, 48000);
    auto brightKey = sineBuffer (8000.0, 0.5f, 48000);
    run (*de, audio2, &brightKey, p);
    CHECK (de->getMeter() > 6.0f);
}

namespace
{
    struct Renderer
    {
        Transport transport;
        AudioGraph graph { transport };
        juce::AudioBuffer<float> out { 2, 0 };
        Renderer() { transport.setSampleRate (sr); transport.setBpm (120.0); }
        void renderAll (int total, int blockSize = 512)
        {
            out.setSize (2, total, false, true, true);
            for (int done = 0; done < total; )
            {
                const int n = juce::jmin (blockSize, total - done);
                float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
                graph.renderBlock (ptrs, 2, n);
                done += n;
            }
        }
    };

    RenderStrip stripWithClip (std::shared_ptr<const juce::AudioBuffer<float>> audio, int stripIndex, RenderSnapshot& snap)
    {
        RenderClip clip;
        clip.audio = audio; clip.timelineStart = 0; clip.length = audio->getNumSamples(); clip.strip = stripIndex;
        snap.clips.push_back (clip);
        RenderStrip s;
        s.gain = 1.0f;
        return s;
    }
}

TEST_CASE ("Graph: a compressor keyed from a bus ducks its strip only when the sender is processed first")
{
    // Strip 0: the keyed (quiet) pad, listed FIRST. Strip 1: the loud kick sending to bus 0.
    auto pad = std::make_shared<const juce::AudioBuffer<float>> (sineBuffer (440.0, 0.05f, 96000));
    auto kick = std::make_shared<const juce::AudioBuffer<float>> ([]
    {
        auto b = sineBuffer (60.0, 0.0f, 96000);
        for (int ch = 0; ch < 2; ++ch) for (int i = 48000; i < 96000; ++i) b.setSample (ch, i, 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * 60.0 * i / sr));
        return b;   // silent first second, loud second second
    }());

    auto comp = std::shared_ptr<Effect> (Effect::create (EffectType::compressor, sr));
    auto params = std::make_shared<const InsertParams> ([]
    {
        auto p = Effect::defaultParams (EffectType::compressor);
        p.values[CompressorEffect::threshold] = -30.0f; p.values[CompressorEffect::ratio] = 10.0f;
        p.values[CompressorEffect::attack] = 0.5f; p.values[CompressorEffect::release] = 30.0f;
        return p;
    }());

    for (bool ordered : { true, false })
    {
        INFO ("ordered " << ordered);
        Renderer r;
        auto snap = std::make_unique<RenderSnapshot>();
        auto padStrip = stripWithClip (pad, 0, *snap);
        padStrip.inserts.push_back ({ comp, params, false, 0, /*keyBus*/ 0, false });
        auto kickStrip = stripWithClip (kick, 1, *snap);
        kickStrip.sends.push_back ({ 0, 1.0f, true, 0 });
        kickStrip.gain = 0.0f;                        // only heard through the key: the output is the pad alone
        snap->strips.push_back (padStrip);
        snap->strips.push_back (kickStrip);
        if (ordered) snap->stripOrder = computeStripOrder (snap->strips);
        CHECK ((! ordered || (snap->stripOrder.size() == 2 && snap->stripOrder[0] == 1 && snap->stripOrder[1] == 0)));
        r.graph.setSnapshot (std::move (snap));
        r.transport.play();
        r.renderAll (96000);

        const float before = dbOf (r.out.getRMSLevel (0, 24000, 20000));
        const float during = dbOf (r.out.getRMSLevel (0, 72000, 20000));
        CHECK_THAT (before, WithinAbs (dbOf (0.05f / juce::MathConstants<float>::sqrt2), 0.5f));
        if (ordered) CHECK (during < before - 10.0f);
        else         CHECK_THAT (during, WithinAbs (before, 0.5f));   // natural order: the bus is still empty when the pad reads it
        r.graph.collectGarbage();
    }
}

TEST_CASE ("Key listen replaces the strip audio with the key signal; the snapshot builder orders senders first")
{
    Session s;
    Track pad; pad.name = "Pad"; pad.type = Track::Type::audio;
    Track kick; kick.name = "Kick"; kick.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (pad));
    s.execute (std::make_unique<AddTrackCommand> (kick));
    auto tone = std::make_shared<const juce::AudioBuffer<float>> (sineBuffer (440.0, 0.1f, 48000));
    auto thump = std::make_shared<const juce::AudioBuffer<float>> (sineBuffer (55.0, 0.6f, 48000));
    for (int t = 0; t < 2; ++t)
    {
        AudioClip c; c.audio = t == 0 ? tone : thump; c.sampleRate = sr; c.length = 48000;
        s.execute (std::make_unique<AddClipCommand> (t, c));
    }
    s.execute (std::make_unique<SetInsertCommand> (0, 0, EffectType::compressor, sr));
    Send send; send.bus = 2; send.gain = 1.0f; send.preFader = true;
    s.execute (std::make_unique<SetSendCommand> (1, 0, send));
    s.execute (std::make_unique<SetInsertKeyCommand> (0, 0, 2, true));
    CHECK (s.getTracks()[0].inserts[0].keyBus == 2);
    CHECK (s.getTracks()[0].inserts[0].keyListen);
    CHECK (s.getHistory().getUndoName() == "Set Key Input");

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->strips.size() == 2);
    REQUIRE (snap->strips[0].inserts.size() == 1);
    CHECK (snap->strips[0].inserts[0].keyBus == 2);
    CHECK (snap->strips[0].inserts[0].keyListen);
    REQUIRE (snap->stripOrder.size() == 2);
    CHECK (snap->stripOrder[0] == 1);   // the kick (sender) first
    CHECK (snap->stripOrder[1] == 0);

    // Mute the kick so only the pad strip reaches the output; key listen still auditions the key (pre-fader send).
    s.execute (std::make_unique<SetTrackMixCommand> (1, 0.0f, 0.0f));
    Renderer r;
    r.graph.setSnapshot (buildRenderSnapshot (s));
    r.transport.play();
    r.renderAll (24000);
    // Output is the 55 Hz thump (0.6) rather than the 440 Hz tone (0.1)
    CHECK (r.out.getMagnitude (0, 12000, 12000) > 0.4f);
    const float* d = r.out.getReadPointer (0);
    int crossings = 0;
    for (int i = 12001; i < 24000; ++i) if ((d[i - 1] < 0.0f) != (d[i] < 0.0f)) ++crossings;
    CHECK_THAT (crossings * 0.5 * sr / 12000.0, WithinAbs (55.0, 3.0));

    s.undo();   // mix
    s.undo();   // key
    CHECK (s.getTracks()[0].inserts[0].keyBus == -1);
    CHECK_FALSE (s.getTracks()[0].inserts[0].keyListen);
    r.graph.collectGarbage();
}

//==============================================================================

namespace
{
    // The convolution builds responses on a background thread and swaps them
    // in during process(): give it time, run silence through to complete the
    // swap, then start clean.
    bool waitUntilLoaded (Effect& fx, const InsertParams& p)
    {
        if (! dynamic_cast<ConvolutionEffect&> (fx).isResponseLoaded()) return false;
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        juce::AudioBuffer<float> silence (2, 512);
        for (int i = 0; i < 16; ++i) { silence.clear(); fx.process (silence, 512, p); }
        fx.reset();
        return true;
    }

    // Energy of the tail in [from, to) after an impulse
    float tailEnergy (Effect& fx, const InsertParams& p, int from, int to)
    {
        juce::AudioBuffer<float> b (2, to);
        b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        for (int pos = 0; pos < to; pos += 512)
        {
            juce::AudioBuffer<float> block (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, to - pos));
            fx.process (block, juce::jmin (512, to - pos), p);
        }
        return b.getRMSLevel (0, from, to - from);
    }
}

TEST_CASE ("Convolution reverb: bundled spaces load, decay realistically, and pre-delay shifts the onset")
{
    auto fx = Effect::create (EffectType::convolution, sr);
    auto p = Effect::defaultParams (EffectType::convolution);
    p.values[ConvolutionEffect::mix] = 100.0f;
    p.values[ConvolutionEffect::predelay] = 0.0f;
    p.values[ConvolutionEffect::lowCut] = 20.0f;
    p.values[ConvolutionEffect::highCut] = 20000.0f;
    CHECK (juce::String (ConvolutionEffect::impulseName (ConvolutionEffect::hall)).isNotEmpty());
    CHECK (juce::String (Effect::typeName (EffectType::convolution)) == "Convolution Reverb");

    // Hall (default) rings for seconds
    p.values[ConvolutionEffect::impulse] = (float) ConvolutionEffect::hall;
    fx->paramsChanged (p);
    REQUIRE (waitUntilLoaded (*fx, p));
    const float hallEarly = tailEnergy (*fx, p, 2400, 12000);
    fx->reset();
    const float hallLate = tailEnergy (*fx, p, 96000, 120000);   // 2.0..2.5 s
    CHECK (hallEarly > 0.0005f);
    CHECK (hallLate > 0.0f);
    CHECK (hallLate < hallEarly);

    // Room dies far quicker
    p.values[ConvolutionEffect::impulse] = (float) ConvolutionEffect::room;
    fx->paramsChanged (p);
    REQUIRE (waitUntilLoaded (*fx, p));
    const float roomLate = tailEnergy (*fx, p, 48000, 60000);     // 1.0..1.25 s: beyond the room's response
    CHECK (roomLate < 1.0e-6f);   // beyond the response: only FFT rounding noise
    CHECK (fx->getDisplayName() == "Conv: Room");

    // Pre-delay of 100 ms: silence for the first ~4800 samples after the impulse
    p.values[ConvolutionEffect::predelay] = 100.0f;
    fx->reset();
    juce::AudioBuffer<float> b (2, 9600);
    b.clear(); b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
    for (int pos = 0; pos < 9600; pos += 512)
    {
        juce::AudioBuffer<float> block (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, 9600 - pos));
        fx->process (block, juce::jmin (512, 9600 - pos), p);
    }
    CHECK (b.getMagnitude (0, 1, 4700) < 1.0e-6f);
    CHECK (b.getMagnitude (0, 4800, 4800) > 0.001f);

    // Mix 0 is dry
    p.values[ConvolutionEffect::mix] = 0.0f;
    fx->reset();
    auto dry = sineBuffer (440.0, 0.5f, 4096);
    auto copy = dry;
    fx->process (dry, 4096, p);
    for (int i = 0; i < 4096; i += 64) CHECK (dry.getSample (0, i) == copy.getSample (0, i));
}

TEST_CASE ("Custom impulse responses load through the model command and undo back")
{
    Session s;
    Track t; t.name = "Vox"; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    s.execute (std::make_unique<SetInsertCommand> (0, 0, EffectType::convolution, sr));
    auto* conv = dynamic_cast<ConvolutionEffect*> (s.getTracks()[0].inserts[0].instance.get());
    REQUIRE (conv != nullptr);

    // A two-tap IR: an echo 1000 samples later at half level
    auto ir = std::make_shared<juce::AudioBuffer<float>> (2, 1001);
    ir->clear();
    for (int ch = 0; ch < 2; ++ch) { ir->setSample (ch, 0, 1.0f); ir->setSample (ch, 1000, 0.5f); }
    s.execute (std::make_unique<SetInsertImpulseCommand> (0, 0, ir, sr, "Slapback"));
    CHECK (s.getHistory().getUndoName() == "Load Impulse Response");
    CHECK (s.getTracks()[0].inserts[0].params->values[ConvolutionEffect::impulse] == (float) ConvolutionEffect::custom);
    CHECK (conv->getCustomImpulseName() == "Slapback");

    auto p = *s.getTracks()[0].inserts[0].params;
    p.values[ConvolutionEffect::mix] = 100.0f; p.values[ConvolutionEffect::predelay] = 0.0f;
    p.values[ConvolutionEffect::lowCut] = 20.0f; p.values[ConvolutionEffect::highCut] = 20000.0f; p.values[ConvolutionEffect::width] = 100.0f;
    REQUIRE (waitUntilLoaded (*conv, p));
    CHECK (conv->getDisplayName() == "Conv: Slapback");

    juce::AudioBuffer<float> b (2, 2048);
    b.clear(); b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
    for (int pos = 0; pos < 2048; pos += 512)
    {
        juce::AudioBuffer<float> block (b.getArrayOfWritePointers(), 2, pos, 512);
        conv->process (block, 512, p);
    }
    // Two taps, the second half the first (filters wide open; normalisation scales both alike)
    const int firstAt = [&] { for (int i = 0; i < 2048; ++i) if (std::abs (b.getSample (0, i)) > 1.0e-4f) return i; return -1; }();
    REQUIRE (firstAt >= 0);
    const float first = b.getSample (0, firstAt), second = b.getSample (0, firstAt + 1000);
    CHECK_THAT (second / first, WithinAbs (0.5, 0.05));
    CHECK (b.getMagnitude (0, firstAt + 2, 990) < std::abs (first) * 0.05f);

    s.undo();
    CHECK (conv->getCustomImpulse() == nullptr);
    CHECK (s.getTracks()[0].inserts[0].params->values[ConvolutionEffect::impulse] == (float) ConvolutionEffect::hall);
}
