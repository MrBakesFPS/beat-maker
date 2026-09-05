#include "BeatDetective.h"
#include "Elastic.h"
#include <algorithm>
#include <map>

namespace beatmaker::model
{

namespace
{
    std::vector<ClipRef> audioOnly (const Session& s, const std::vector<ClipRef>& clips)
    {
        std::vector<ClipRef> out;
        for (const auto& r : clips)
            if (r.kind == ClipRef::Kind::audio)
                if (const auto* t = s.getTrack (r.track); t != nullptr && juce::isPositiveAndBelow (r.index, (int) t->clips.size()))
                    out.push_back (r);
        return out;
    }
}

juce::int64 BeatDetective::nearestGridSample (juce::int64 sample, const ConformSettings& c, double sampleRate) noexcept
{
    if (c.bpm <= 0.0 || c.gridBeats <= 0.0 || sampleRate <= 0.0) return sample;
    const double grid = c.gridBeats * 60.0 / c.bpm * sampleRate;
    const double swingShift = juce::jlimit (0.0f, 1.0f, c.swing) * grid / 3.0;   // odd lines delayed; 1.0 = triplet position

    // Candidate lines around the sample; odd ones carry the swing shift.
    const double k0 = std::floor ((double) sample / grid);
    juce::int64 best = sample;
    double bestDistance = 1.0e18;
    for (double k = k0 - 2.0; k <= k0 + 2.0; k += 1.0)
    {
        const bool odd = ((juce::int64) std::llround (k) % 2 + 2) % 2 == 1;
        const double line = k * grid + (odd ? swingShift : 0.0);
        const double distance = std::abs (line - (double) sample);
        if (distance < bestDistance) { bestDistance = distance; best = (juce::int64) std::llround (line); }
    }
    return juce::jmax<juce::int64> (0, best);
}

std::unique_ptr<Command> BeatDetective::separate (const Session& s, const std::vector<ClipRef>& clips, float sensitivity)
{
    auto compound = std::make_unique<CompoundCommand> ("Separate at Transients");
    // Splits leave the original clip at its index and append the remainders,
    // so every ref stays valid whatever the order.
    for (const auto& ref : audioOnly (s, clips))
        compound->add (Elastic::separateAtTransients (s, ref, sensitivity));
    return compound->isEmpty() ? nullptr : std::move (compound);
}

std::unique_ptr<Command> BeatDetective::conform (const Session& s, const std::vector<ClipRef>& clips, const ConformSettings& c)
{
    auto compound = std::make_unique<CompoundCommand> ("Clip Conform");
    for (const auto& ref : audioOnly (s, clips))
    {
        const auto& clip = s.getTrack (ref.track)->clips[(size_t) ref.index];
        const double grid = c.gridBeats * 60.0 / c.bpm * clip.sampleRate;
        const juce::int64 target = nearestGridSample (clip.timelineStart, c, clip.sampleRate);
        const juce::int64 delta = target - clip.timelineStart;
        if (delta == 0) continue;
        if (c.excludeWithin > 0.0f && (double) std::abs (delta) <= c.excludeWithin * grid * 0.5) continue;
        const juce::int64 newStart = clip.timelineStart + (juce::int64) std::llround ((double) delta * juce::jlimit (0.0f, 1.0f, c.strength));
        if (newStart != clip.timelineStart)
            compound->add (std::make_unique<MoveClipCommand> (ref, ref.track, newStart));
    }
    return compound->isEmpty() ? nullptr : std::move (compound);
}

std::unique_ptr<Command> BeatDetective::smooth (const Session& s, const std::vector<ClipRef>& clips, const SmoothingSettings& sm)
{
    if (! sm.fillGaps && ! sm.crossfade) return nullptr;
    auto compound = std::make_unique<CompoundCommand> (sm.crossfade ? "Edit Smoothing (fill and crossfade)" : "Edit Smoothing (fill gaps)");

    // Group by track, sort by start.
    std::map<int, std::vector<ClipRef>> byTrack;
    for (const auto& ref : audioOnly (s, clips)) byTrack[ref.track].push_back (ref);

    for (auto& [trackIndex, refs] : byTrack)
    {
        const auto& track = *s.getTrack (trackIndex);
        std::sort (refs.begin(), refs.end(), [&] (const ClipRef& a, const ClipRef& b)
                   { return track.clips[(size_t) a.index].timelineStart < track.clips[(size_t) b.index].timelineStart; });

        struct Fades { juce::int64 in, out; engine::FadeShape inShape, outShape; bool changed = false; };
        std::map<int, Fades> fades;
        for (const auto& r : refs)
        {
            const auto& c = track.clips[(size_t) r.index];
            fades[r.index] = { c.fadeIn, c.fadeOut, c.fadeInShape, c.fadeOutShape, false };
        }

        for (size_t i = 0; i + 1 < refs.size(); ++i)
        {
            const auto& a = track.clips[(size_t) refs[i].index];
            const auto& b = track.clips[(size_t) refs[i + 1].index];
            const juce::int64 xf = sm.crossfade ? (juce::int64) std::llround (sm.crossfadeMs * 0.001 * a.sampleRate) : 0;
            const auto timing = ClipEdits::timing (s, refs[i]);
            if (! timing) continue;

            // Desired end: the next clip's start, plus the crossfade overlap; limited by the audio that exists.
            juce::int64 desiredLength = (b.timelineStart + xf) - a.timelineStart;
            if (timing->maxLength > 0) desiredLength = juce::jmin (desiredLength, timing->maxLength);
            desiredLength = juce::jmax<juce::int64> (1, desiredLength);
            if (! sm.fillGaps && desiredLength > a.length) desiredLength = a.length;   // crossfade only: never extend
            if (desiredLength != a.length)
                compound->add (std::make_unique<TrimClipCommand> (refs[i], a.timelineStart, desiredLength));

            if (sm.crossfade)
            {
                const juce::int64 overlap = juce::jlimit<juce::int64> (0, xf, a.timelineStart + desiredLength - b.timelineStart);
                if (overlap > 0)
                {
                    auto& fa = fades[refs[i].index];     fa.out = overlap; fa.outShape = engine::FadeShape::equalPower; fa.changed = true;
                    auto& fb = fades[refs[i + 1].index]; fb.in = overlap;  fb.inShape = engine::FadeShape::equalPower;  fb.changed = true;
                }
            }
        }
        for (const auto& r : refs)
        {
            const auto& f = fades[r.index];
            if (f.changed) compound->add (std::make_unique<SetClipFadesCommand> (r, f.in, f.inShape, f.out, f.outShape));
        }
    }
    return compound->isEmpty() ? nullptr : std::move (compound);
}

} // namespace beatmaker::model
