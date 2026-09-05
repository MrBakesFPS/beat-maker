#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> makeRamp (int channels, int length, float scale = 1.0f)
    {
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, (float) (i + 1) * scale * (float) (ch + 1));
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    struct Renderer
    {
        Transport transport;
        AudioGraph graph { transport };
        juce::AudioBuffer<float> out { 2, 64 };

        void render (int numSamples)
        {
            out.setSize (2, numSamples, false, false, true);
            graph.renderBlock (out.getArrayOfWritePointers(), 2, numSamples);
        }
    };
}

TEST_CASE ("Graph is silent with no snapshot and when stopped")
{
    Renderer r;
    r.render (32);
    for (int ch = 0; ch < 2; ++ch)
        CHECK (r.out.getMagnitude (ch, 0, 32) == 0.0f);

    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (1, 100), 0, 0, 100, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.render (32); // transport still stopped
    CHECK (r.out.getMagnitude (0, 0, 32) == 0.0f);
    CHECK (r.transport.getPositionSamples() == 0);
}

TEST_CASE ("Mono clip at timeline zero plays to both outputs and advances transport")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (1, 100), 0, 0, 100, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();

    r.render (16);
    CHECK (r.transport.getPositionSamples() == 16);
    for (int i = 0; i < 16; ++i)
    {
        CHECK_THAT (r.out.getSample (0, i), WithinAbs ((float) (i + 1), 1e-6));
        CHECK_THAT (r.out.getSample (1, i), WithinAbs ((float) (i + 1), 1e-6));
    }

    r.render (16); // second block continues from sample 16
    CHECK_THAT (r.out.getSample (0, 0), WithinAbs (17.0f, 1e-6));
}

TEST_CASE ("Clip starting mid-block is offset correctly and stops at its end")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (1, 10), /*start*/ 5, 0, /*length*/ 10, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();

    r.render (32);
    for (int i = 0; i < 5; ++i)   CHECK (r.out.getSample (0, i) == 0.0f);
    for (int i = 5; i < 15; ++i)  CHECK_THAT (r.out.getSample (0, i), WithinAbs ((float) (i - 5 + 1), 1e-6));
    for (int i = 15; i < 32; ++i) CHECK (r.out.getSample (0, i) == 0.0f);
}

TEST_CASE ("Stereo clips map channels 1:1 and overlapping clips sum with gain")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (2, 100), 0, 0, 100, 0.5f });   // L = i+1, R = 2(i+1), gain 0.5
    snap->clips.push_back ({ makeRamp (1, 100), 0, 0, 100, 1.0f });   // both = i+1
    snap->masterGain = 2.0f;
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();

    r.render (8);
    // L: (i+1)*0.5*2 + (i+1)*1*2 = 3(i+1);  R: 2(i+1)*0.5*2 + (i+1)*2 = 4(i+1)
    for (int i = 0; i < 8; ++i)
    {
        CHECK_THAT (r.out.getSample (0, i), WithinAbs (3.0f * (i + 1), 1e-5));
        CHECK_THAT (r.out.getSample (1, i), WithinAbs (4.0f * (i + 1), 1e-5));
    }
}

TEST_CASE ("Source offset skips into the buffer")
{
    Renderer r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (1, 100), 0, /*sourceOffset*/ 40, 20, 1.0f });
    r.graph.setSnapshot (std::move (snap));
    r.transport.play();

    r.render (8);
    CHECK_THAT (r.out.getSample (0, 0), WithinAbs (41.0f, 1e-6));
}

TEST_CASE ("Replacing snapshots retires the old one without leaking or crashing")
{
    Renderer r;
    r.transport.play();

    for (int n = 0; n < 200; ++n)
    {
        auto snap = std::make_unique<RenderSnapshot>();
        snap->clips.push_back ({ makeRamp (1, 4096), 0, 0, 4096, 1.0f });
        r.graph.setSnapshot (std::move (snap));
        r.render (64);
        if (n % 10 == 0)
            r.graph.collectGarbage();
    }
    r.graph.collectGarbage();
    SUCCEED ("no crash; leak detector runs at shutdown in debug builds");
}
