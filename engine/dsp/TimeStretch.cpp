#include "TimeStretch.h"
#include "Resampler.h"

#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace beatmaker::engine
{

void StretchSpec::sortMarkers()
{
    std::sort (markers.begin(), markers.end(), [] (const WarpMarker& a, const WarpMarker& b) { return a.source < b.source; });
    markers.erase (std::unique (markers.begin(), markers.end(), [] (const WarpMarker& a, const WarpMarker& b) { return a.source == b.source; }),
                   markers.end());
}

const char* TimeStretch::modeName (StretchMode m)
{
    switch (m)
    {
        case StretchMode::off:        return "Off";
        case StretchMode::polyphonic: return "Polyphonic";
        case StretchMode::rhythmic:   return "Rhythmic";
        case StretchMode::monophonic: return "Monophonic";
        case StretchMode::varispeed:  return "Varispeed";
    }
    return "";
}

juce::int64 TimeStretch::outputLength (juce::int64 sourceLength, const StretchSpec& spec) noexcept
{
    if (! spec.isActive()) return sourceLength;
    return juce::jmax<juce::int64> (1, (juce::int64) std::llround ((double) sourceLength * spec.ratio));
}

namespace
{
    // Anchor points of the piecewise map: (0,0), valid markers, (srcLen, outLen).
    struct Anchors
    {
        std::vector<WarpMarker> points;
        Anchors (juce::int64 srcLen, const StretchSpec& spec)
        {
            const juce::int64 outLen = TimeStretch::outputLength (srcLen, spec);
            points.push_back ({ 0, 0 });
            if (spec.isActive())
                for (const auto& m : spec.markers)
                    if (m.source > 0 && m.source < srcLen && m.output > points.back().output && m.output < outLen && m.source > points.back().source)
                        points.push_back (m);
            points.push_back ({ srcLen, outLen });
        }
    };

    juce::int64 interpolate (juce::int64 x, juce::int64 x0, juce::int64 x1, juce::int64 y0, juce::int64 y1) noexcept
    {
        if (x1 <= x0) return y0;
        return y0 + (juce::int64) std::llround ((double) (x - x0) * (double) (y1 - y0) / (double) (x1 - x0));
    }
}

juce::int64 TimeStretch::sourceToOutput (juce::int64 source, juce::int64 sourceLength, const StretchSpec& spec) noexcept
{
    if (! spec.isActive() || sourceLength <= 0) return source;
    const Anchors a (sourceLength, spec);
    if (source <= 0) return (juce::int64) std::llround ((double) source * spec.ratio);
    for (size_t i = 1; i < a.points.size(); ++i)
        if (source <= a.points[i].source)
            return interpolate (source, a.points[i - 1].source, a.points[i].source, a.points[i - 1].output, a.points[i].output);
    const auto& last = a.points.back();
    return last.output + (juce::int64) std::llround ((double) (source - last.source) * spec.ratio);
}

juce::int64 TimeStretch::outputToSource (juce::int64 output, juce::int64 sourceLength, const StretchSpec& spec) noexcept
{
    if (! spec.isActive() || sourceLength <= 0) return output;
    const Anchors a (sourceLength, spec);
    if (output <= 0) return (juce::int64) std::llround ((double) output / spec.ratio);
    for (size_t i = 1; i < a.points.size(); ++i)
        if (output <= a.points[i].output)
            return interpolate (output, a.points[i - 1].output, a.points[i].output, a.points[i - 1].source, a.points[i].source);
    const auto& last = a.points.back();
    return last.source + (juce::int64) std::llround ((double) (output - last.output) / spec.ratio);
}

juce::String TimeStretch::libraryVersion()
{
    return "Rubber Band Library " + juce::String (RUBBERBAND_VERSION);
}

namespace
{
    juce::AudioBuffer<float> renderVarispeed (const juce::AudioBuffer<float>& source, const StretchSpec& spec)
    {
        const juce::int64 srcLen = source.getNumSamples();
        const juce::int64 outLen = TimeStretch::outputLength (srcLen, spec);
        const int channels = source.getNumChannels();
        juce::AudioBuffer<float> out (channels, (int) outLen);
        out.clear();

        // Each segment between anchors is resampled at its own ratio.
        const Anchors a (srcLen, spec);
        for (size_t i = 1; i < a.points.size(); ++i)
        {
            const auto& p0 = a.points[i - 1];
            const auto& p1 = a.points[i];
            const int inLen = (int) (p1.source - p0.source), segOut = (int) (p1.output - p0.output);
            if (inLen <= 0 || segOut <= 0) continue;

            juce::AudioBuffer<float> segment (channels, inLen);
            for (int ch = 0; ch < channels; ++ch) segment.copyFrom (ch, 0, source, ch, (int) p0.source, inLen);
            auto resampled = Resampler::resample (segment, (double) inLen / (double) segOut);
            const int n = juce::jmin (segOut, resampled.getNumSamples());
            for (int ch = 0; ch < channels; ++ch) out.copyFrom (ch, (int) p0.output, resampled, ch, 0, n);
        }
        return out;
    }
}

juce::AudioBuffer<float> TimeStretch::render (const juce::AudioBuffer<float>& source, double sampleRate,
                                              const StretchSpec& spec, const ProgressFn& progress)
{
    const int channels = source.getNumChannels();
    const juce::int64 srcLen = source.getNumSamples();
    if (channels <= 0 || srcLen <= 0) return {};

    if (spec.isIdentity())
    {
        juce::AudioBuffer<float> copy;
        copy.makeCopyOf (source);
        return copy;
    }
    if (spec.mode == StretchMode::varispeed)
        return renderVarispeed (source, spec);

    using RB = RubberBand::RubberBandStretcher;
    int options = RB::OptionProcessOffline | RB::OptionEngineFiner | RB::OptionPitchHighQuality | RB::OptionChannelsTogether;
    switch (spec.mode)
    {
        case StretchMode::rhythmic:   options |= RB::OptionWindowShort | RB::OptionTransientsCrisp | RB::OptionDetectorPercussive; break;
        case StretchMode::monophonic: options |= RB::OptionWindowLong | RB::OptionFormantPreserved | RB::OptionPitchHighConsistency; break;
        case StretchMode::polyphonic:
        case StretchMode::off:
        case StretchMode::varispeed:  options |= RB::OptionWindowStandard | RB::OptionTransientsMixed; break;
    }

    const juce::int64 outLen = outputLength (srcLen, spec);
    RB stretcher ((size_t) sampleRate, (size_t) channels, options, spec.ratio, std::pow (2.0, spec.pitchSemitones / 12.0));
    stretcher.setExpectedInputDuration ((size_t) srcLen);

    std::map<size_t, size_t> keyFrames;
    for (const auto& m : Anchors (srcLen, spec).points)
        if (m.source > 0 && m.source < srcLen) keyFrames[(size_t) m.source] = (size_t) m.output;
    if (! keyFrames.empty()) stretcher.setKeyFrameMap (keyFrames);

    const int block = 4096;
    std::vector<const float*> inPtrs ((size_t) channels);
    // Offline mode: study the whole input first, then process it.
    auto study = [&] () -> bool
    {
        for (juce::int64 pos = 0; pos < srcLen; pos += block)
        {
            const int n = (int) juce::jmin<juce::int64> (block, srcLen - pos);
            for (int ch = 0; ch < channels; ++ch) inPtrs[(size_t) ch] = source.getReadPointer (ch, (int) pos);
            stretcher.study (inPtrs.data(), (size_t) n, pos + n >= srcLen);
            if (progress && ! progress (0.5 * (double) (pos + n) / (double) srcLen)) return false;
        }
        return true;
    };

    juce::AudioBuffer<float> out (channels, (int) outLen);
    out.clear();
    std::vector<float*> outPtrs ((size_t) channels);
    juce::int64 written = 0;
    auto drain = [&]
    {
        for (int avail = stretcher.available(); avail > 0; avail = stretcher.available())
        {
            const int n = (int) juce::jmin<juce::int64> (avail, outLen - written);
            if (n <= 0) { // discard surplus
                juce::AudioBuffer<float> scratch (channels, avail);
                for (int ch = 0; ch < channels; ++ch) outPtrs[(size_t) ch] = scratch.getWritePointer (ch);
                stretcher.retrieve (outPtrs.data(), (size_t) avail);
                continue;
            }
            for (int ch = 0; ch < channels; ++ch) outPtrs[(size_t) ch] = out.getWritePointer (ch, (int) written);
            written += (juce::int64) stretcher.retrieve (outPtrs.data(), (size_t) n);
        }
    };

    if (! study()) return {};
    for (juce::int64 pos = 0; pos < srcLen; pos += block)
    {
        const int n = (int) juce::jmin<juce::int64> (block, srcLen - pos);
        for (int ch = 0; ch < channels; ++ch) inPtrs[(size_t) ch] = source.getReadPointer (ch, (int) pos);
        stretcher.process (inPtrs.data(), (size_t) n, pos + n >= srcLen);
        drain();
        if (progress && ! progress (0.5 + 0.5 * (double) (pos + n) / (double) srcLen)) return {};
    }
    drain();
    return out;
}

} // namespace beatmaker::engine
