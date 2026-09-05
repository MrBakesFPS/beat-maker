#include "Bouncer.h"
#include "../graph/AudioGraph.h"
#include "../transport/Transport.h"

namespace beatmaker::engine
{

BounceResult Bouncer::renderToBuffer (std::unique_ptr<RenderSnapshot> snapshot, const BounceSettings& s,
                                      juce::AudioBuffer<float>& out, const ProgressFn& progress)
{
    BounceResult result;

    if (snapshot == nullptr)            { result.error = "Nothing to render"; return result; }
    if (s.endSample <= s.startSample)   { result.error = "Bounce range is empty"; return result; }
    if (s.numChannels <= 0 || s.numChannels > 32) { result.error = "Invalid channel count"; return result; }
    if (s.sampleRate <= 0.0)            { result.error = "Invalid sample rate"; return result; }

    Transport transport;
    transport.setSampleRate (s.sampleRate);
    transport.setBpm (s.bpm);
    transport.setBeatsPerBar (s.beatsPerBar);
    transport.setPositionSamples (s.startSample);

    // The offline render must not share stateful DSP with the live audio
    // thread, which keeps running (silently) while we bounce: give every
    // instrument and built-in effect a fresh instance at the bounce rate.
    // Hosted plugins can't be duplicated and keep the shared instance.
    for (auto& ri : snapshot->instruments)
        if (ri.instance != nullptr)
            ri.instance = Instrument::create (ri.instance->getType(), s.sampleRate);
    auto freshInserts = [&] (std::vector<RenderInsert>& inserts)
    {
        for (auto& ins : inserts)
            if (ins.fx != nullptr && ins.fx->getType() != EffectType::plugin)
                if (auto fresh = Effect::create (ins.fx->getType(), s.sampleRate, AudioGraph::maxBlock))
                    ins.fx = std::move (fresh);
    };
    for (auto& strip : snapshot->strips) freshInserts (strip.inserts);
    freshInserts (snapshot->master.inserts);

    AudioGraph graph (transport);
    graph.setSnapshot (std::move (snapshot));

    const juce::int64 mainLength = s.endSample - s.startSample;
    const juce::int64 tailLength = (juce::int64) std::llround (juce::jmax (0.0, s.tailSeconds) * s.sampleRate);
    const juce::int64 total = mainLength + tailLength;
    const int block = juce::jmax (16, s.blockSize);

    out.setSize (s.numChannels, (int) total, false, true, true);

    auto renderRange = [&] (juce::int64 from, juce::int64 to) -> bool
    {
        for (juce::int64 pos = from; pos < to; )
        {
            const int n = (int) juce::jmin<juce::int64> (block, to - pos);
            float* ptrs[32];
            for (int ch = 0; ch < s.numChannels; ++ch)
                ptrs[ch] = out.getWritePointer (ch, (int) pos);

            graph.renderBlock (ptrs, s.numChannels, n);
            pos += n;

            if (progress && ! progress ((double) pos / (double) total))
                return false;
        }
        return true;
    };

    // Main range: transport running, clips and patterns play.
    transport.play();
    if (! renderRange (0, mainLength)) { result.cancelled = true; return result; }

    // Tail: transport stopped so nothing new starts, but voices ring out.
    transport.stop();
    if (! renderRange (mainLength, total)) { result.cancelled = true; return result; }

    // Trim trailing silence from the tail.
    juce::int64 length = total;
    if (s.trimTail && tailLength > 0)
    {
        juce::int64 last = mainLength - 1;
        for (juce::int64 i = total - 1; i >= mainLength; --i)
        {
            bool loud = false;
            for (int ch = 0; ch < s.numChannels && ! loud; ++ch)
                loud = std::abs (out.getSample (ch, (int) i)) > silenceThreshold;
            if (loud) { last = i; break; }
        }
        length = last + 1;
    }
    out.setSize (s.numChannels, (int) length, true, false, true);
    result.numSamples = length;

    // Peak / normalise
    float peak = 0.0f;
    for (int ch = 0; ch < s.numChannels; ++ch)
        peak = juce::jmax (peak, out.getMagnitude (ch, 0, (int) length));
    result.peakBeforeNormalize = peak;
    result.clipped = peak > 1.0f;

    if (s.normalize && peak > 0.0f)
    {
        result.appliedGain = juce::Decibels::decibelsToGain (s.normalizeTargetDb) / peak;
        out.applyGain (result.appliedGain);
    }

    graph.collectGarbage();
    return result;
}

juce::String Bouncer::writeFile (const juce::AudioBuffer<float>& buffer, const juce::File& file, const BounceSettings& s)
{
    if (! BounceSettings::supportsBitDepth (s.format, s.bitDepth))
        return "Unsupported bit depth for this format";

    std::unique_ptr<juce::AudioFormat> format;
    switch (s.format)
    {
        case BounceSettings::Format::wav:  format = std::make_unique<juce::WavAudioFormat>(); break;
        case BounceSettings::Format::aiff: format = std::make_unique<juce::AiffAudioFormat>(); break;
        case BounceSettings::Format::flac: format = std::make_unique<juce::FlacAudioFormat>(); break;
    }

    if (! file.getParentDirectory().createDirectory())
        return "Cannot create " + file.getParentDirectory().getFullPathName();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
        return "Cannot write " + file.getFullPathName();

    const auto options = juce::AudioFormatWriterOptions().withSampleRate (s.sampleRate)
                                                         .withNumChannels (buffer.getNumChannels())
                                                         .withBitsPerSample (s.bitDepth);
    auto writer = format->createWriterFor (stream, options);
    if (writer == nullptr)
        return "Cannot create " + format->getFormatName() + " writer";

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        return "Write failed for " + file.getFileName();

    writer.reset(); // flush
    return {};
}

BounceResult Bouncer::renderToFile (std::unique_ptr<RenderSnapshot> snapshot, const BounceSettings& settings,
                                    const juce::File& file, const ProgressFn& progress)
{
    juce::AudioBuffer<float> buffer;
    auto result = renderToBuffer (std::move (snapshot), settings, buffer, progress);
    if (! result.ok())
        return result;

    result.error = writeFile (buffer, file, settings);
    return result;
}

} // namespace beatmaker::engine
