#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <dsp/DrumKitFactory.h>
#include <dsp/DrumMachine.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    // A kit whose pad 0 is a single-sample impulse and pad 1 is a long DC
    // block, so onsets and voice counts can be checked exactly.
    std::shared_ptr<const DrumKit> makeTestKit()
    {
        auto kit = std::make_shared<DrumKit>();
        juce::AudioBuffer<float> impulse (1, 4);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        kit->pads[0] = { "Impulse", std::make_shared<const juce::AudioBuffer<float>> (std::move (impulse)), 1.0f };

        juce::AudioBuffer<float> dc (1, 1000);
        juce::FloatVectorOperations::fill (dc.getWritePointer (0), 1.0f, 1000);
        kit->pads[1] = { "DC", std::make_shared<const juce::AudioBuffer<float>> (std::move (dc)), 1.0f };
        return kit;
    }

    std::shared_ptr<const juce::AudioBuffer<float>> makeRamp (int length)
    {
        juce::AudioBuffer<float> b (1, length);
        for (int i = 0; i < length; ++i) b.setSample (0, i, (float) (i + 1));
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    struct Renderer
    {
        Transport transport;
        AudioGraph graph { transport };
        juce::AudioBuffer<float> out { 1, 0 };

        // Render `total` samples in blocks, appending to `out`.
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

        std::vector<int> nonZeroIndices() const
        {
            std::vector<int> v;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (out.getSample (0, i) != 0.0f) v.push_back (i);
            return v;
        }
    };
}

TEST_CASE ("Sequencer fires steps at sample-accurate positions")
{
    Renderer r;
    r.transport.setSampleRate (48000.0);
    r.transport.setBpm (120.0);   // step = 48000 * 60 / (120 * 4) = 6000 samples

    auto pattern = std::make_shared<StepPattern>();
    pattern->set (0, 0, 127);
    pattern->set (0, 2, 127);

    auto snap = std::make_unique<RenderSnapshot>();
    snap->patterns.push_back ({ pattern, makeTestKit(), 0, 48000 * 4, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();

    r.renderAll (16384, 512);   // covers steps 0 (0) and 2 (12000) only
    CHECK (r.nonZeroIndices() == std::vector<int> { 0, 12000 });
    CHECK_THAT (r.out.getSample (0, 12000), WithinAbs (1.0f, 1e-6));
}

TEST_CASE ("Pattern loops for the clip length and honours the clip start")
{
    Renderer r;
    r.transport.setSampleRate (48000.0);
    r.transport.setBpm (120.0);

    auto pattern = std::make_shared<StepPattern>();
    pattern->numSteps = 4;      // one beat long: 4 * 6000 = 24000 samples
    pattern->set (0, 0, 127);

    auto snap = std::make_unique<RenderSnapshot>();
    snap->patterns.push_back ({ pattern, makeTestKit(), /*start*/ 1000, /*length*/ 50000, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();

    r.renderAll (80000, 700);
    // Iterations at 1000, 25000, 49000; the next (73000) is past the clip end (51000).
    CHECK (r.nonZeroIndices() == std::vector<int> { 1000, 25000, 49000 });
}

TEST_CASE ("A pattern with Loop off plays once for the clip's length")
{
    Renderer r;
    r.transport.setSampleRate (48000.0);
    r.transport.setBpm (120.0);
    auto pattern = std::make_shared<StepPattern>();
    pattern->numSteps = 4;
    pattern->set (0, 0, 127);
    auto snap = std::make_unique<RenderSnapshot>();
    beatmaker::engine::RenderPattern rp { pattern, makeTestKit(), 1000, 50000, 1.0f };
    rp.loop = false;
    snap->patterns.push_back (rp);
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();
    r.renderAll (80000, 700);
    CHECK (r.nonZeroIndices() == std::vector<int> { 1000 });   // no repeats at 25000 and 49000
}

TEST_CASE ("Velocity scales the hit and pattern gain applies")
{
    Renderer r;
    auto pattern = std::make_shared<StepPattern>();
    pattern->set (0, 0, 64);

    auto snap = std::make_unique<RenderSnapshot>();
    snap->patterns.push_back ({ pattern, makeTestKit(), 0, 100000, 0.5f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();
    r.renderAll (64, 64);
    CHECK_THAT (r.out.getSample (0, 0), WithinAbs (64.0f / 127.0f * 0.5f, 1e-6));
}

TEST_CASE ("Cycle wraps sample-accurately at the loop end")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (1000), 0, 0, 1000, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.setLoopRange (0, 100);
    r.transport.setLoopEnabled (true);
    r.transport.play();

    r.renderAll (250, 64);
    CHECK_THAT (r.out.getSample (0, 99),  WithinAbs (100.0f, 1e-6));
    CHECK_THAT (r.out.getSample (0, 100), WithinAbs (1.0f, 1e-6));    // wrapped
    CHECK_THAT (r.out.getSample (0, 249), WithinAbs (50.0f, 1e-6));
    CHECK (r.transport.getPositionSamples() == 50);
}

TEST_CASE ("Cycle is ignored when disabled or when located past the loop end")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (1000), 0, 0, 1000, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.setLoopRange (0, 100);
    r.transport.play();

    r.renderAll (150, 64);
    CHECK_THAT (r.out.getSample (0, 149), WithinAbs (150.0f, 1e-6));

    r.transport.setLoopEnabled (true);
    r.transport.setPositionSamples (500);
    r.renderAll (64, 64);
    CHECK (r.transport.getPositionSamples() == 564);
}

TEST_CASE ("Re-triggering a pad chokes the previous voice")
{
    DrumMachine dm;
    auto kit = makeTestKit();
    juce::AudioBuffer<float> out (1, 600);
    out.clear();
    float* ptr = out.getWritePointer (0);

    dm.trigger (kit.get(), 1, 1.0f, 1.0f, 0);
    dm.trigger (kit.get(), 1, 1.0f, 1.0f, 200);
    dm.render (&ptr, 1, 600);

    CHECK_THAT (out.getSample (0, 100), WithinAbs (1.0f, 1e-6));            // one voice
    CHECK (out.getSample (0, 210) > 1.0f);                                    // overlap during fade
    CHECK_THAT (out.getSample (0, 500), WithinAbs (1.0f, 1e-6));            // old voice gone
    CHECK (dm.getNumActiveVoices() == 1);
}

TEST_CASE ("A held step gates the voice at its end; a one-step hit rings out")
{
    auto kit = makeTestKit();
    DrumMachine dm;
    juce::AudioBuffer<float> out (1, 600); out.clear();
    float* ptr = out.getWritePointer (0);
    dm.trigger (kit.get(), 1, 1.0f, 1.0f, 0, 0, 200);   // the 1000-sample DC pad, gated after 200 samples
    dm.render (&ptr, 1, 600);
    CHECK_THAT (out.getSample (0, 100), WithinAbs (1.0, 1e-6));
    CHECK_THAT (out.getSample (0, 199), WithinAbs (1.0, 1e-6));
    CHECK (out.getSample (0, 201) < 1.0f);                                             // fading (the fade starts at full scale)
    CHECK_THAT (out.getSample (0, 200 + DrumMachine::retriggerFadeSamples + 1), WithinAbs (0.0, 1e-6));
    CHECK (dm.getNumActiveVoices() == 0);
    out.clear();
    dm.trigger (kit.get(), 1, 1.0f, 1.0f, 0, 0, 0);     // no gate: still sounding after 600 samples
    dm.render (&ptr, 1, 600);
    CHECK_THAT (out.getSample (0, 599), WithinAbs (1.0, 1e-6));
    CHECK (dm.getNumActiveVoices() == 1);

    // Through the graph: the pad-1 hit at step 0 is held for two sixteenths (12000 samples at 120 BPM) and gated
    // there; the one at step 8 rings for the sample's full 1000 samples
    Renderer r;
    r.transport.setSampleRate (48000.0);
    r.transport.setBpm (120.0);
    auto pattern = std::make_shared<StepPattern>();
    pattern->numSteps = 16; pattern->stepsPerBeat = 4;
    pattern->set (1, 0, 127); pattern->setLength (1, 0, 2);
    pattern->set (1, 8, 127);
    auto snap = std::make_unique<RenderSnapshot>();
    snap->patterns.push_back (RenderPattern { pattern, kit, 0, 96000, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();
    r.renderAll (60000, 512);
    CHECK_THAT (r.out.getSample (0, 500), WithinAbs (1.0, 1e-6));
    CHECK_THAT (r.out.getSample (0, 48000 + 999), WithinAbs (1.0, 1e-6));   // step 8: the whole sample
    CHECK_THAT (r.out.getSample (0, 48000 + 1000), WithinAbs (0.0, 1e-6));

    // A held step longer than the sample changes nothing: the sample simply ends
    Renderer r2;
    r2.transport.setSampleRate (48000.0);
    r2.transport.setBpm (120.0);
    auto pattern2 = std::make_shared<StepPattern>();
    pattern2->numSteps = 16; pattern2->stepsPerBeat = 4;
    pattern2->set (1, 0, 127); pattern2->setLength (1, 0, 4);       // 24000 samples, sample is 1000
    auto snap2 = std::make_unique<RenderSnapshot>();
    snap2->patterns.push_back (RenderPattern { pattern2, kit, 0, 96000, 1.0f });
    r2.graph.setSnapshot (std::move (snap2));
    r2.transport.play();
    r2.renderAll (30000, 512);
    CHECK (r2.nonZeroIndices().size() == 1000);
}

TEST_CASE ("A hit's micro-timing offset delays it by quarter steps, across block boundaries")
{
    Renderer r;
    r.transport.setSampleRate (48000.0);
    r.transport.setBpm (120.0);   // a sixteenth is 6000 samples
    auto pattern = std::make_shared<StepPattern>();
    pattern->numSteps = 4;
    pattern->set (0, 0, 127); pattern->setOffset (0, 0, 2);   // half a step late: 3000
    pattern->set (0, 2, 127); pattern->setOffset (0, 2, 3);   // 12000 + 4500 = 16500
    auto snap = std::make_unique<RenderSnapshot>();
    RenderPattern rp { pattern, makeTestKit(), 0, 24000, 1.0f };
    rp.loop = false;
    snap->patterns.push_back (rp);
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();
    r.renderAll (24000, 1000);   // block edges at multiples of 1000: 3000 is an edge, 16500 is not
    CHECK (r.nonZeroIndices() == std::vector<int> { 3000, 16500 });
}

TEST_CASE ("Delayed trigger starts mid-block and voices finish")
{
    DrumMachine dm;
    auto kit = makeTestKit();
    juce::AudioBuffer<float> out (1, 64);
    out.clear();
    float* ptr = out.getWritePointer (0);

    dm.trigger (kit.get(), 0, 1.0f, 1.0f, 10);
    dm.render (&ptr, 1, 64);
    CHECK (out.getSample (0, 9) == 0.0f);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (1.0f, 1e-6));
    CHECK (dm.getNumActiveVoices() == 0);   // 4-sample impulse is done
}

TEST_CASE ("Pad previews only fire for kits in the active snapshot, and die when the kit leaves")
{
    Renderer r;
    auto kitA = makeTestKit();
    auto kitB = makeTestKit();

    auto snap = std::make_unique<RenderSnapshot>();
    snap->patterns.push_back ({ std::make_shared<StepPattern>(), kitA, 0, 0, 1.0f });
    r.graph.setSnapshot (std::move (snap));

    r.graph.triggerPadPreview (kitB.get(), 1, 1.0f);   // not in snapshot: ignored
    r.renderAll (64, 64);
    CHECK (r.nonZeroIndices().empty());

    r.graph.triggerPadPreview (kitA.get(), 1, 1.0f);   // stopped transport still previews
    r.renderAll (64, 64);
    CHECK_THAT (r.out.getSample (0, 63), WithinAbs (1.0f, 1e-6));
    CHECK (r.graph.getNumActiveVoices() == 1);

    // Replace the snapshot with one that no longer references kitA.
    auto snap2 = std::make_unique<RenderSnapshot>();
    snap2->patterns.push_back ({ std::make_shared<StepPattern>(), kitB, 0, 0, 1.0f });
    r.graph.setSnapshot (std::move (snap2));
    r.renderAll (64, 64);
    CHECK (r.graph.getNumActiveVoices() == 0);
    CHECK (r.nonZeroIndices().empty());
    r.graph.collectGarbage();
}

TEST_CASE ("Every bundled kit has 16 named, non-silent pads, a category and a description; unknown names give the default")
{
    const auto& kits = DrumKitFactory::availableKits();
    REQUIRE (kits.size() >= 25);
    CHECK (kits.front().name == DrumKitFactory::defaultKitName());
    for (const auto& cat : { "Acoustic", "Electronic", "Hip-Hop", "World" })
    {
        int n = 0; for (const auto& k : kits) if (k.category == cat) ++n;
        INFO ("category " << cat); CHECK (n >= 6);
    }
    for (const auto& info : kits)
    {
        INFO ("kit " << info.name);
        CHECK (juce::String (info.category).isNotEmpty());
        CHECK (juce::String (info.description).length() > 40);
        auto kit = DrumKitFactory::createKit (info.name, 44100.0);
        REQUIRE (kit != nullptr);
        CHECK (kit->name == info.name);
        for (const auto& pad : kit->pads)
        {
            INFO ("pad " << pad.name);
            CHECK (pad.name.isNotEmpty());
            REQUIRE (pad.audio != nullptr);
            CHECK (pad.audio->getNumSamples() > 1000);
            CHECK (pad.audio->getMagnitude (0, 0, pad.audio->getNumSamples()) > 0.2f);
            CHECK (pad.audio->getMagnitude (0, 0, pad.audio->getNumSamples()) <= 1.0f);
        }
    }
    for (size_t i = 0; i < kits.size(); ++i) for (size_t j = i + 1; j < kits.size(); ++j) CHECK (kits[i].name != kits[j].name);
    CHECK (DrumKitFactory::createKit ("No Such Kit", 44100.0)->name == DrumKitFactory::defaultKitName());
    CHECK (DrumKitFactory::info ("808") != nullptr);
    CHECK (DrumKitFactory::info ("nope") == nullptr);
    CHECK (! DrumKitFactory::categories().empty());
    // Kits sound different: the 808 kick rings far longer than the 909's
    auto k808 = DrumKitFactory::createKit ("808", 48000.0), k909 = DrumKitFactory::createKit ("909", 48000.0);
    CHECK (k808->pads[DrumKitFactory::kick].audio->getMagnitude (0, 24000, 12000) > k909->pads[DrumKitFactory::kick].audio->getMagnitude (0, 24000, juce::jmin (12000, k909->pads[DrumKitFactory::kick].audio->getNumSamples() - 24000)) * 2.0f);
}

TEST_CASE ("A custom kit takes its pads from other kits, lists under My Kits, and never refers to itself")
{
    DrumKitFactory::CustomKit mine;
    mine.name = "My Mix";
    for (int i = 0; i < DrumKit::numPads; ++i) mine.pads[(size_t) i] = { "Studio Kit", i };
    mine.pads[DrumKitFactory::kick] = { "808", DrumKitFactory::kick };
    mine.pads[DrumKitFactory::snare] = { "909", DrumKitFactory::snare };
    mine.pads[DrumKitFactory::closedHat] = { "Latin", DrumKitFactory::closedHat };   // "Cabasa"
    auto kit = DrumKitFactory::createCustomKit (mine, 48000.0);
    CHECK (kit->name == "My Mix");
    CHECK (kit->pads[DrumKitFactory::kick].name == "808 Kick");
    CHECK (kit->pads[DrumKitFactory::snare].name == "909 Snare");
    CHECK (kit->pads[DrumKitFactory::closedHat].name == "Cabasa");
    CHECK (kit->pads[DrumKitFactory::clap].name == "Clap");
    auto ref808 = DrumKitFactory::createKit ("808", 48000.0);
    CHECK (kit->pads[DrumKitFactory::kick].audio->getNumSamples() == ref808->pads[DrumKitFactory::kick].audio->getNumSamples());

    // Registered: it lists, resolves by name and cannot shadow a bundled kit
    DrumKitFactory::CustomKit shadow; shadow.name = "808"; for (int i = 0; i < DrumKit::numPads; ++i) shadow.pads[(size_t) i] = { "909", i };
    DrumKitFactory::CustomKit self; self.name = "Loop"; for (int i = 0; i < DrumKit::numPads; ++i) self.pads[(size_t) i] = { "Loop", i };
    DrumKitFactory::setCustomKits ({ mine, shadow, self });
    CHECK (DrumKitFactory::customKits().size() == 2);
    REQUIRE (DrumKitFactory::info ("My Mix") != nullptr);
    CHECK (DrumKitFactory::info ("My Mix")->custom);
    CHECK (DrumKitFactory::info ("My Mix")->category == DrumKitFactory::customCategory());
    CHECK (DrumKitFactory::info ("My Mix")->description.contains ("808"));
    CHECK (DrumKitFactory::createKit ("My Mix", 48000.0)->pads[DrumKitFactory::snare].name == "909 Snare");
    CHECK (DrumKitFactory::createKit ("808", 48000.0)->pads[DrumKitFactory::snare].name == "808 Snare");   // the bundled one, not the shadow
    CHECK (DrumKitFactory::createKit ("Loop", 48000.0)->pads[0].name == "Kick");                          // self-reference falls back to the default kit
    CHECK (std::find (DrumKitFactory::categories().begin(), DrumKitFactory::categories().end(), juce::String (DrumKitFactory::customCategory())) != DrumKitFactory::categories().end());
    DrumKitFactory::setCustomKits ({});
    CHECK (DrumKitFactory::info ("My Mix") == nullptr);
}

TEST_CASE ("The audition kit plays pads without a snapshot, is choked on stop, and is retired when cleared")
{
    Renderer r;
    r.transport.setSampleRate (48000.0);
    r.transport.setBpm (120.0);
    r.graph.triggerAuditionPad (1, 1.0f);      // nothing loaded: ignored
    r.renderAll (64, 64);
    CHECK (r.nonZeroIndices().empty());

    r.graph.setAuditionKit (makeTestKit());
    CHECK (r.graph.hasAuditionInstrument());
    r.graph.triggerAuditionPad (1, 1.0f);      // the 1000-sample DC pad, no snapshot at all
    r.renderAll (64, 64);
    CHECK_THAT (r.out.getSample (0, 63), WithinAbs (1.0f, 1e-6));
    CHECK (r.graph.getNumActiveVoices() == 1);
    // A track's strip never renders it
    r.graph.stopAuditionNotes();               // choke: gone within the fade
    r.renderAll (200, 200);
    CHECK (r.graph.getNumActiveVoices() == 0);

    r.graph.triggerAuditionPad (1, 1.0f);
    r.renderAll (64, 64);
    CHECK (r.graph.getNumActiveVoices() == 1);
    r.graph.setAuditionKit (nullptr);          // cleared: the voice is choked and the kit retired
    r.renderAll (200, 200);
    CHECK (r.graph.getNumActiveVoices() == 0);
    r.graph.triggerAuditionPad (1, 1.0f);
    r.renderAll (64, 64);
    CHECK (r.nonZeroIndices().empty());
    r.graph.collectGarbage();
}

TEST_CASE ("Default kit has 16 named, non-silent pads")
{
    auto kit = DrumKitFactory::createDefaultKit (44100.0);
    REQUIRE (kit != nullptr);
    for (const auto& pad : kit->pads)
    {
        CHECK (pad.name.isNotEmpty());
        REQUIRE (pad.audio != nullptr);
        CHECK (pad.audio->getNumSamples() > 1000);
        CHECK (pad.audio->getMagnitude (0, 0, pad.audio->getNumSamples()) > 0.2f);
        CHECK (pad.audio->getMagnitude (0, 0, pad.audio->getNumSamples()) <= 1.0f);
    }
}
