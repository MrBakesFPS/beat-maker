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
