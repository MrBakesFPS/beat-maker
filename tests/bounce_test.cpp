#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <bounce/Bouncer.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> makeRamp (int length, float scale = 1.0f)
    {
        juce::AudioBuffer<float> b (1, length);
        for (int i = 0; i < length; ++i) b.setSample (0, i, (float) (i + 1) * scale);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    std::shared_ptr<const DrumKit> makeKit()
    {
        auto kit = std::make_shared<DrumKit>();
        juce::AudioBuffer<float> dc (1, 1000);
        juce::FloatVectorOperations::fill (dc.getWritePointer (0), 0.5f, 1000);
        kit->pads[0] = { "DC", std::make_shared<const juce::AudioBuffer<float>> (std::move (dc)), 1.0f };
        return kit;
    }

    BounceSettings settings (juce::int64 start, juce::int64 end, double tail = 0.0)
    {
        BounceSettings s;
        s.sampleRate = 48000.0;
        s.bpm = 120.0;
        s.startSample = start;
        s.endSample = end;
        s.tailSeconds = tail;
        s.numChannels = 2;
        s.blockSize = 64;
        return s;
    }

    std::unique_ptr<juce::AudioFormatReader> readFile (const juce::File& f)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        return std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (f));
    }
}

TEST_CASE ("Bounce of a clip range reproduces the clip exactly")
{
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (1000, 0.001f), 100, 0, 1000, 1.0f });

    juce::AudioBuffer<float> out;
    auto r = Bouncer::renderToBuffer (std::move (snap), settings (50, 350), out);
    REQUIRE (r.ok());
    CHECK (r.numSamples == 300);
    CHECK (out.getNumChannels() == 2);
    for (int i = 0; i < 50; ++i)  CHECK (out.getSample (0, i) == 0.0f);          // before the clip
    CHECK_THAT (out.getSample (0, 50),  WithinAbs (0.001f, 1e-7));                // clip sample 0 at timeline 100
    CHECK_THAT (out.getSample (1, 299), WithinAbs (0.250f, 1e-6));                // clip sample 249
    CHECK_THAT (r.peakBeforeNormalize, WithinAbs (0.25f, 1e-6));
    CHECK_FALSE (r.clipped);
}

TEST_CASE ("Offline bounce is identical to block-by-block live rendering")
{
    auto makeSnap = []
    {
        auto snap = std::make_unique<RenderSnapshot>();
        snap->clips.push_back ({ makeRamp (5000, 0.0001f), 0, 0, 5000, 0.8f });
        auto pattern = std::make_shared<StepPattern>();
        pattern->set (0, 0, 127);
        pattern->set (0, 3, 90);
        snap->patterns.push_back ({ pattern, makeKit(), 0, 48000, 1.0f });
        return snap;
    };

    // "Live": a graph driven in odd block sizes.
    Transport t; t.setSampleRate (48000.0); t.setBpm (120.0);
    AudioGraph live (t);
    live.setSnapshot (makeSnap());
    t.play();
    juce::AudioBuffer<float> liveOut (2, 24000);
    for (int pos = 0; pos < 24000; )
    {
        const int n = juce::jmin (373, 24000 - pos);
        float* ptrs[2] = { liveOut.getWritePointer (0, pos), liveOut.getWritePointer (1, pos) };
        live.renderBlock (ptrs, 2, n);
        pos += n;
    }

    juce::AudioBuffer<float> bounced;
    auto s = settings (0, 24000);
    s.blockSize = 512;
    REQUIRE (Bouncer::renderToBuffer (makeSnap(), s, bounced).ok());

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 24000; ++i)
            REQUIRE (bounced.getSample (ch, i) == liveOut.getSample (ch, i));
    live.collectGarbage();
}

TEST_CASE ("Tail lets voices ring out, starts nothing new, and is trimmed to the last sound")
{
    auto pattern = std::make_shared<StepPattern>();
    pattern->set (0, 0, 127);   // fires at sample 0 -> DC for 1000 samples
    pattern->set (0, 1, 127);   // would fire at 6000, but that is past the range end

    auto snap = std::make_unique<RenderSnapshot>();
    snap->patterns.push_back ({ pattern, makeKit(), 0, 48000, 1.0f });

    juce::AudioBuffer<float> out;
    auto r = Bouncer::renderToBuffer (std::move (snap), settings (0, 10, /*tail*/ 1.0), out);
    REQUIRE (r.ok());
    CHECK (r.numSamples == 1000);   // 10 in range + 990 of ring-out, then silence trimmed
    CHECK_THAT (out.getSample (0, 5),   WithinAbs (0.5f, 1e-6));
    CHECK_THAT (out.getSample (0, 999), WithinAbs (0.5f, 1e-6));
}

TEST_CASE ("Tail is kept when trimming is off and untrimmed silence is included")
{
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (10, 0.01f), 0, 0, 10, 1.0f });

    juce::AudioBuffer<float> out;
    auto s = settings (0, 10, 0.5);
    s.trimTail = false;
    auto r = Bouncer::renderToBuffer (std::move (snap), s, out);
    REQUIRE (r.ok());
    CHECK (r.numSamples == 10 + 24000);
}

TEST_CASE ("Normalise scales to the target and clipping is reported")
{
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (100, 0.02f), 0, 0, 100, 1.0f });   // peak 2.0

    juce::AudioBuffer<float> out;
    auto s = settings (0, 100);
    s.normalize = true;
    s.normalizeTargetDb = -0.3f;
    auto r = Bouncer::renderToBuffer (std::move (snap), s, out);
    REQUIRE (r.ok());
    CHECK (r.clipped);
    CHECK_THAT (r.peakBeforeNormalize, WithinAbs (2.0f, 1e-6));
    CHECK_THAT (out.getMagnitude (0, 0, 100), WithinAbs (juce::Decibels::decibelsToGain (-0.3f), 1e-5));
}

TEST_CASE ("Progress callback can cancel")
{
    auto snap = std::make_unique<RenderSnapshot>();
    juce::AudioBuffer<float> out;
    int calls = 0;
    auto r = Bouncer::renderToBuffer (std::move (snap), settings (0, 48000), out, [&] (double) { return ++calls < 3; });
    CHECK (r.cancelled);
    CHECK (calls == 3);
}

TEST_CASE ("Bounce writes readable WAV, AIFF and FLAC files with the right length")
{
    for (auto format : { BounceSettings::Format::wav, BounceSettings::Format::aiff, BounceSettings::Format::flac })
    {
        auto snap = std::make_unique<RenderSnapshot>();
        snap->clips.push_back ({ makeRamp (4800, 0.0001f), 0, 0, 4800, 1.0f });

        auto s = settings (0, 4800);
        s.format = format;
        s.bitDepth = 24;
        const auto file = juce::File::createTempFile (BounceSettings::extensionFor (format));

        auto r = Bouncer::renderToFile (std::move (snap), s, file);
        REQUIRE (r.ok());

        auto reader = readFile (file);
        REQUIRE (reader != nullptr);
        CHECK (reader->lengthInSamples == 4800);
        CHECK (reader->numChannels == 2);
        CHECK (reader->sampleRate == 48000.0);

        juce::AudioBuffer<float> data (2, 4800);
        reader->read (&data, 0, 4800, 0, true, true);
        CHECK_THAT (data.getSample (0, 4799), WithinAbs (0.48f, 1e-4));
        reader.reset();
        file.deleteFile();
    }
}

TEST_CASE ("32-bit float WAV round-trips exactly; unsupported depths are refused")
{
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeRamp (256, 0.001f), 0, 0, 256, 1.0f });
    auto s = settings (0, 256);
    s.bitDepth = 32;
    const auto file = juce::File::createTempFile (".wav");
    REQUIRE (Bouncer::renderToFile (std::move (snap), s, file).ok());

    auto reader = readFile (file);
    REQUIRE (reader != nullptr);
    juce::AudioBuffer<float> data (2, 256);
    reader->read (&data, 0, 256, 0, true, true);
    CHECK (data.getSample (0, 255) == 0.256f);
    reader.reset();
    file.deleteFile();

    juce::AudioBuffer<float> buffer (2, 16);
    auto bad = settings (0, 16);
    bad.format = BounceSettings::Format::flac;
    bad.bitDepth = 32;
    CHECK (Bouncer::writeFile (buffer, file, bad).isNotEmpty());
    CHECK_FALSE (BounceSettings::supportsBitDepth (BounceSettings::Format::aiff, 32));
}

TEST_CASE ("Empty range and missing snapshot are errors")
{
    juce::AudioBuffer<float> out;
    CHECK (Bouncer::renderToBuffer (nullptr, settings (0, 100), out).error.isNotEmpty());
    CHECK (Bouncer::renderToBuffer (std::make_unique<RenderSnapshot>(), settings (100, 100), out).error.isNotEmpty());
}
