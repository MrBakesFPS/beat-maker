#include "Elastic.h"
#include <dsp/Transients.h>
#include <algorithm>

namespace beatmaker::model
{

namespace
{
    const AudioClip* clipFor (const Session& s, const ClipRef& ref)
    {
        if (ref.kind != ClipRef::Kind::audio) return nullptr;
        const auto* t = s.getTrack (ref.track);
        if (t == nullptr || ! juce::isPositiveAndBelow (ref.index, (int) t->clips.size())) return nullptr;
        return &t->clips[(size_t) ref.index];
    }

    juce::int64 sourceLengthOf (const AudioClip& c) { return c.originalAudio() != nullptr ? c.originalAudio()->getNumSamples() : 0; }

    // rendered (current spec) -> source
    juce::int64 toSource (const AudioClip& c, juce::int64 rendered) { return engine::TimeStretch::outputToSource (rendered, sourceLengthOf (c), c.elastic); }
    // source -> rendered (given spec)
    juce::int64 toRendered (const AudioClip& c, juce::int64 source, const engine::StretchSpec& spec) { return engine::TimeStretch::sourceToOutput (source, sourceLengthOf (c), spec); }
}

//==============================================================================

SetClipElasticCommand::SetClipElasticCommand (const Session& s, ClipRef r, engine::StretchSpec spec, juce::String commandName,
                                              engine::TimeStretch::ProgressFn progress)
    : ref (r), name (std::move (commandName))
{
    const auto* clip = clipFor (s, ref);
    if (clip == nullptr || clip->originalAudio() == nullptr) { cancelled = true; return; }
    before = *clip;
    after = *clip;

    spec.sortMarkers();
    const auto& source = *clip->originalAudio();
    const juce::int64 srcLen = source.getNumSamples();

    const auto original = clip->sourceAudio != nullptr ? clip->sourceAudio : clip->audio;
    after.elastic = spec;
    if (spec.isActive())
    {
        auto rendered = engine::TimeStretch::render (source, clip->sampleRate, spec, progress);
        if (rendered.getNumSamples() == 0) { cancelled = true; return; }
        after.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (rendered));
        after.sourceAudio = original;
    }
    else
    {
        after.audio = original;        // Elastic off: back to the untouched original buffer
        after.sourceAudio = nullptr;
    }
    after.audioModified = false;

    // Remap the visible region and everything anchored to it.
    const juce::int64 srcStart = toSource (*clip, clip->sourceOffset);
    const juce::int64 srcEnd   = toSource (*clip, clip->sourceOffset + clip->length);
    const juce::int64 newStart = juce::jlimit<juce::int64> (0, after.audio->getNumSamples(), toRendered (*clip, srcStart, spec));
    const juce::int64 newEnd   = juce::jlimit<juce::int64> (newStart, after.audio->getNumSamples(), toRendered (*clip, srcEnd, spec));
    after.sourceOffset = newStart;
    after.length = juce::jmax<juce::int64> (1, newEnd - newStart);

    const double lengthScale = clip->length > 0 ? (double) after.length / (double) clip->length : 1.0;
    after.fadeIn  = (juce::int64) std::llround ((double) clip->fadeIn * lengthScale);
    after.fadeOut = (juce::int64) std::llround ((double) clip->fadeOut * lengthScale);
    after.clampFades();

    if (clip->gainLane != nullptr)
    {
        auto lane = std::make_shared<engine::AutomationLane> (*clip->gainLane);
        for (auto& p : lane->points) p.time = toRendered (*clip, toSource (*clip, p.time), spec);
        lane->sortPoints();
        after.gainLane = lane;
    }
    juce::ignoreUnused (srcLen);
}

void SetClipElasticCommand::execute (Session& s)
{
    if (cancelled) return;
    auto& tracks = EditAccess::tracks (s);
    auto& clip = tracks[(size_t) ref.track].clips[(size_t) ref.index];
    after.name = clip.name; after.timelineStart = clip.timelineStart; after.gain = clip.gain;   // untouched fields stay live
    clip = after;
}

void SetClipElasticCommand::undo (Session& s)
{
    if (cancelled) return;
    auto& tracks = EditAccess::tracks (s);
    auto& clip = tracks[(size_t) ref.track].clips[(size_t) ref.index];
    before.name = clip.name; before.timelineStart = clip.timelineStart; before.gain = clip.gain;
    clip = before;
}

//==============================================================================

engine::StretchSpec Elastic::withMode (const AudioClip& c, engine::StretchMode mode)
{
    auto spec = c.elastic;
    if (mode == engine::StretchMode::off) { spec = {}; return spec; }
    if (! spec.isActive()) { spec.ratio = 1.0; spec.pitchSemitones = 0.0; spec.markers.clear(); }
    if (mode == engine::StretchMode::varispeed) spec.pitchSemitones = 0.0;
    spec.mode = mode;
    return spec;
}

engine::StretchSpec Elastic::withPitch (const AudioClip& c, double semitones)
{
    auto spec = c.elastic;
    if (! spec.isActive() || spec.mode == engine::StretchMode::varispeed) spec.mode = engine::StretchMode::polyphonic;
    spec.pitchSemitones = juce::jlimit (-24.0, 24.0, semitones);
    return spec;
}

engine::StretchSpec Elastic::forVisibleLength (const AudioClip& c, juce::int64 newLength)
{
    auto spec = c.elastic;
    if (! spec.isActive()) spec.mode = engine::StretchMode::polyphonic;
    const juce::int64 srcLen = sourceLengthOf (c);
    const juce::int64 srcVisible = juce::jmax<juce::int64> (1, toSource (c, c.sourceOffset + c.length) - toSource (c, c.sourceOffset));
    const double newRatio = (double) juce::jmax<juce::int64> (1, newLength) / (double) srcVisible;
    // Markers keep their source positions and scale their output positions with the ratio change.
    for (auto& m : spec.markers) m.output = (juce::int64) std::llround ((double) m.output * newRatio / spec.ratio);
    spec.ratio = juce::jlimit (0.05, 20.0, newRatio);
    juce::ignoreUnused (srcLen);
    return spec;
}

engine::StretchSpec Elastic::forTempo (const AudioClip& c, double sessionBpm, engine::StretchMode fallbackMode)
{
    auto spec = c.elastic;
    if (! spec.isActive()) spec.mode = fallbackMode;
    if (c.sourceBpm > 0.0 && sessionBpm > 0.0)
    {
        const double newRatio = c.sourceBpm / sessionBpm;
        for (auto& m : spec.markers) m.output = (juce::int64) std::llround ((double) m.output * newRatio / spec.ratio);
        spec.ratio = juce::jlimit (0.05, 20.0, newRatio);
    }
    return spec;
}

engine::StretchSpec Elastic::withMarkerAt (const AudioClip& c, juce::int64 renderedSample)
{
    auto spec = c.elastic;
    if (! spec.isActive()) spec.mode = engine::StretchMode::polyphonic;
    spec.markers.push_back ({ toSource (c, renderedSample), renderedSample });
    spec.sortMarkers();
    return spec;
}

engine::StretchSpec Elastic::withMarkerMoved (const AudioClip& c, int index, juce::int64 newRenderedSample)
{
    auto spec = c.elastic;
    if (! juce::isPositiveAndBelow (index, (int) spec.markers.size())) return spec;
    const juce::int64 outLen = engine::TimeStretch::outputLength (sourceLengthOf (c), spec);
    juce::int64 lo = 1, hi = outLen - 1;
    if (index > 0) lo = spec.markers[(size_t) index - 1].output + 1;
    if (index + 1 < (int) spec.markers.size()) hi = spec.markers[(size_t) index + 1].output - 1;
    spec.markers[(size_t) index].output = juce::jlimit (lo, juce::jmax (lo, hi), newRenderedSample);
    return spec;
}

engine::StretchSpec Elastic::withoutMarker (const AudioClip& c, int index)
{
    auto spec = c.elastic;
    if (juce::isPositiveAndBelow (index, (int) spec.markers.size())) spec.markers.erase (spec.markers.begin() + index);
    return spec;
}

engine::StretchSpec Elastic::withoutMarkers (const AudioClip& c)
{
    auto spec = c.elastic;
    spec.markers.clear();
    return spec;
}

std::vector<juce::int64> Elastic::transients (const AudioClip& c, float sensitivity)
{
    std::vector<juce::int64> out;
    if (c.audio == nullptr) return out;
    const int start = (int) juce::jlimit<juce::int64> (0, c.audio->getNumSamples(), c.sourceOffset);
    const int len = (int) juce::jlimit<juce::int64> (0, c.audio->getNumSamples() - start, c.length);
    if (len <= 0) return out;
    juce::AudioBuffer<float> region (c.audio->getNumChannels(), len);
    for (int ch = 0; ch < region.getNumChannels(); ++ch) region.copyFrom (ch, 0, *c.audio, ch, start, len);
    for (auto t : engine::Transients::detect (region, c.sampleRate, sensitivity)) out.push_back (t + start);
    return out;
}

std::optional<engine::StretchSpec> Elastic::quantizeToGrid (const AudioClip& c, double bpm, double gridBeats, float strength, float sensitivity)
{
    if (bpm <= 0.0 || gridBeats <= 0.0) return std::nullopt;
    const auto hits = transients (c, sensitivity);
    if (hits.empty()) return std::nullopt;

    auto spec = c.elastic;
    if (! spec.isActive()) spec.mode = engine::StretchMode::rhythmic;
    spec.markers.clear();

    // Grid lines are on the timeline; the clip's rendered sample r sits at timelineStart + (r - sourceOffset).
    const double gridSamples = gridBeats * 60.0 / bpm * c.sampleRate;
    const juce::int64 outLen = engine::TimeStretch::outputLength (sourceLengthOf (c), spec);
    juce::int64 lastOutput = 0;
    for (auto r : hits)
    {
        const double timeline = (double) (c.timelineStart + (r - c.sourceOffset));
        const double snapped = std::round (timeline / gridSamples) * gridSamples;
        const juce::int64 target = r + (juce::int64) std::llround ((snapped - timeline) * juce::jlimit (0.0f, 1.0f, strength));
        if (target <= lastOutput || target >= outLen - 1) continue;
        spec.markers.push_back ({ toSource (c, r), target });
        lastOutput = target;
    }
    if (spec.markers.empty()) return std::nullopt;
    return spec;
}

std::unique_ptr<Command> Elastic::separateAtTransients (const Session& s, const ClipRef& ref, float sensitivity)
{
    const auto* clip = clipFor (s, ref);
    if (clip == nullptr) return nullptr;
    const auto hits = transients (*clip, sensitivity);

    // Split from the end so earlier indices stay valid; each split leaves the
    // original (now shorter) clip at `ref`.
    std::vector<std::unique_ptr<Command>> steps;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
    {
        const juce::int64 timeline = clip->timelineStart + (*it - clip->sourceOffset);
        if (timeline <= clip->timelineStart + 64 || timeline >= clip->timelineStart + clip->length - 64) continue;
        steps.push_back (std::make_unique<SplitClipCommand> (ref, timeline));
    }
    if (steps.empty()) return nullptr;
    auto compound = std::make_unique<CompoundCommand> ("Separate at Transients");
    for (auto& step : steps) compound->add (std::move (step));
    return compound;
}

std::optional<juce::int64> Elastic::nextTransient (const Session& s, int trackIndex, juce::int64 sample, bool forward, float sensitivity)
{
    std::optional<juce::int64> best;
    for (int t = 0; t < s.getNumTracks(); ++t)
    {
        if (trackIndex >= 0 && t != trackIndex) continue;
        const auto& track = s.getTracks()[(size_t) t];
        if (! track.isAudio()) continue;
        for (const auto& clip : track.clips)
        {
            if (forward ? clip.timelineStart + clip.length <= sample : clip.timelineStart >= sample) continue;
            for (auto r : transients (clip, sensitivity))
            {
                const juce::int64 timeline = clip.timelineStart + (r - clip.sourceOffset);
                if (forward ? timeline > sample : timeline < sample)
                    if (! best || (forward ? timeline < *best : timeline > *best)) best = timeline;
            }
        }
    }
    return best;
}

} // namespace beatmaker::model
