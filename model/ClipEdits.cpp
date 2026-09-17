#include "ClipEdits.h"
#include <algorithm>

namespace beatmaker::model
{

namespace
{
    template <typename Fn>
    void withClip (Session& s, const ClipRef& ref, Fn&& fn)
    {
        auto& tracks = EditAccess::tracks (s);
        if (! juce::isPositiveAndBelow (ref.track, (int) tracks.size())) return;
        auto& t = tracks[(size_t) ref.track];
        switch (ref.kind)
        {
            case ClipRef::Kind::audio:   if (juce::isPositiveAndBelow (ref.index, (int) t.clips.size()))        fn (t.clips[(size_t) ref.index]); break;
            case ClipRef::Kind::pattern: if (juce::isPositiveAndBelow (ref.index, (int) t.patternClips.size())) fn (t.patternClips[(size_t) ref.index]); break;
            case ClipRef::Kind::midi:    if (juce::isPositiveAndBelow (ref.index, (int) t.midiClips.size()))    fn (t.midiClips[(size_t) ref.index]); break;
        }
    }

    juce::int64& offsetOf (AudioClip& c)   { return c.sourceOffset; }
    juce::int64& offsetOf (PatternClip& c) { return c.loopOffset; }
    juce::int64& offsetOf (MidiClip& c)    { return c.loopOffset; }

    juce::int64 maxLengthOf (const AudioClip& c)   { return c.audio != nullptr ? c.audio->getNumSamples() - c.sourceOffset : c.length; }
    juce::int64 maxLengthOf (const PatternClip&)   { return 0; }
    juce::int64 maxLengthOf (const MidiClip&)      { return 0; }
}

//==============================================================================
// Queries

ClipRef::Kind ClipEdits::kindForTrack (const Track& t)
{
    if (t.isDrumMachine()) return ClipRef::Kind::pattern;
    if (t.isSynth())       return ClipRef::Kind::midi;
    return ClipRef::Kind::audio;
}

int ClipEdits::numClips (const Track& t, ClipRef::Kind kind)
{
    switch (kind)
    {
        case ClipRef::Kind::audio:   return (int) t.clips.size();
        case ClipRef::Kind::pattern: return (int) t.patternClips.size();
        case ClipRef::Kind::midi:    return (int) t.midiClips.size();
    }
    return 0;
}

std::optional<ClipTiming> ClipEdits::timing (const Session& s, const ClipRef& ref)
{
    std::optional<ClipTiming> result;
    withClip (const_cast<Session&> (s), ref, [&] (auto& c)
    {
        ClipTiming t;
        t.start = c.timelineStart; t.length = c.length; t.offset = offsetOf (c);
        t.maxLength = maxLengthOf (c); t.sampleRate = c.sampleRate; t.name = c.name;
        result = t;
    });
    return result;
}

std::vector<ClipRef> ClipEdits::allClips (const Session& s, int trackIndex)
{
    std::vector<ClipRef> refs;
    if (auto* t = s.getTrack (trackIndex))
    {
        for (int i = 0; i < (int) t->clips.size(); ++i)        refs.push_back ({ trackIndex, ClipRef::Kind::audio, i });
        for (int i = 0; i < (int) t->patternClips.size(); ++i) refs.push_back ({ trackIndex, ClipRef::Kind::pattern, i });
        for (int i = 0; i < (int) t->midiClips.size(); ++i)    refs.push_back ({ trackIndex, ClipRef::Kind::midi, i });
    }
    return refs;
}

std::optional<ClipRef> ClipEdits::clipAt (const Session& s, int trackIndex, juce::int64 sample)
{
    for (const auto& ref : allClips (s, trackIndex))
        if (auto t = timing (s, ref))
            if (sample >= t->start && sample < t->start + t->length) return ref;
    return std::nullopt;
}

bool ClipEdits::canPlaceOn (const Session& s, const ClipRef& ref, int trackIndex)
{
    auto* t = s.getTrack (trackIndex);
    return t != nullptr && kindForTrack (*t) == ref.kind;
}

//==============================================================================
// Move

MoveClipCommand::MoveClipCommand (ClipRef r, int track, juce::int64 start)
    : ref (r), result (r), newTrack (track), newStart (juce::jmax<juce::int64> (0, start)) {}

void MoveClipCommand::execute (Session& s)
{
    auto& tracks = EditAccess::tracks (s);
    result = ref;

    if (newTrack == ref.track || ! juce::isPositiveAndBelow (newTrack, (int) tracks.size()))
    {
        withClip (s, ref, [&] (auto& c) { oldStart = c.timelineStart; c.timelineStart = newStart; });
        return;
    }

    // Cross-track: pop from the source list, push onto the destination.
    auto& src = tracks[(size_t) ref.track];
    auto& dst = tracks[(size_t) newTrack];
    switch (ref.kind)
    {
        case ClipRef::Kind::audio:
        {
            auto c = src.clips[(size_t) ref.index]; oldStart = c.timelineStart; c.timelineStart = newStart;
            src.clips.erase (src.clips.begin() + ref.index); dst.clips.push_back (c);
            result = { newTrack, ref.kind, (int) dst.clips.size() - 1 }; break;
        }
        case ClipRef::Kind::pattern:
        {
            auto c = src.patternClips[(size_t) ref.index]; oldStart = c.timelineStart; c.timelineStart = newStart;
            src.patternClips.erase (src.patternClips.begin() + ref.index); dst.patternClips.push_back (c);
            result = { newTrack, ref.kind, (int) dst.patternClips.size() - 1 }; break;
        }
        case ClipRef::Kind::midi:
        {
            auto c = src.midiClips[(size_t) ref.index]; oldStart = c.timelineStart; c.timelineStart = newStart;
            src.midiClips.erase (src.midiClips.begin() + ref.index); dst.midiClips.push_back (c);
            result = { newTrack, ref.kind, (int) dst.midiClips.size() - 1 }; break;
        }
    }
}

void MoveClipCommand::undo (Session& s)
{
    auto& tracks = EditAccess::tracks (s);
    if (result.track == ref.track)
    {
        withClip (s, ref, [&] (auto& c) { c.timelineStart = oldStart; });
        return;
    }

    auto& src = tracks[(size_t) ref.track];
    auto& dst = tracks[(size_t) result.track];
    switch (ref.kind)
    {
        case ClipRef::Kind::audio:
        {
            auto c = dst.clips.back(); dst.clips.pop_back(); c.timelineStart = oldStart;
            src.clips.insert (src.clips.begin() + ref.index, c); break;
        }
        case ClipRef::Kind::pattern:
        {
            auto c = dst.patternClips.back(); dst.patternClips.pop_back(); c.timelineStart = oldStart;
            src.patternClips.insert (src.patternClips.begin() + ref.index, c); break;
        }
        case ClipRef::Kind::midi:
        {
            auto c = dst.midiClips.back(); dst.midiClips.pop_back(); c.timelineStart = oldStart;
            src.midiClips.insert (src.midiClips.begin() + ref.index, c); break;
        }
    }
}

//==============================================================================
// Trim

TrimClipCommand::TrimClipCommand (ClipRef r, juce::int64 start, juce::int64 length)
    : ref (r), newStart (juce::jmax<juce::int64> (0, start)), newLength (juce::jmax<juce::int64> (1, length)) {}

void TrimClipCommand::execute (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        old.start = c.timelineStart; old.length = c.length; old.offset = offsetOf (c);

        juce::int64 start = newStart, length = newLength;
        juce::int64 delta = start - c.timelineStart;      // positive = start moved right

        // Audio: the start can't move before the beginning of the source file.
        if constexpr (std::is_same_v<std::decay_t<decltype (c)>, AudioClip>)
        {
            if (offsetOf (c) + delta < 0) { delta = -offsetOf (c); start = c.timelineStart + delta; }
        }

        offsetOf (c) += delta;
        c.timelineStart = start;

        const juce::int64 maxLen = maxLengthOf (c);
        if (maxLen > 0) length = juce::jmin (length, maxLen);
        c.length = juce::jmax<juce::int64> (1, length);

        oldFadeIn = c.fadeIn; oldFadeOut = c.fadeOut;
        c.clampFades();
    });
}

void TrimClipCommand::undo (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        c.timelineStart = old.start; c.length = old.length; offsetOf (c) = old.offset;
        c.fadeIn = oldFadeIn; c.fadeOut = oldFadeOut;
    });
}

//==============================================================================
// Remove

void RemoveAnyClipCommand::execute (Session& s)
{
    auto& tracks = EditAccess::tracks (s);
    if (! juce::isPositiveAndBelow (ref.track, (int) tracks.size())) return;
    auto& t = tracks[(size_t) ref.track];
    switch (ref.kind)
    {
        case ClipRef::Kind::audio:   audio = t.clips[(size_t) ref.index];          t.clips.erase (t.clips.begin() + ref.index); break;
        case ClipRef::Kind::pattern: pattern = t.patternClips[(size_t) ref.index]; t.patternClips.erase (t.patternClips.begin() + ref.index); break;
        case ClipRef::Kind::midi:    midi = t.midiClips[(size_t) ref.index];       t.midiClips.erase (t.midiClips.begin() + ref.index); break;
    }
}

void RemoveAnyClipCommand::undo (Session& s)
{
    auto& t = EditAccess::tracks (s)[(size_t) ref.track];
    if (audio)   t.clips.insert (t.clips.begin() + ref.index, *audio);
    if (pattern) t.patternClips.insert (t.patternClips.begin() + ref.index, *pattern);
    if (midi)    t.midiClips.insert (t.midiClips.begin() + ref.index, *midi);
}

//==============================================================================
// Split

void SplitClipCommand::execute (Session& s)
{
    auto& tracks = EditAccess::tracks (s);
    didSplit = false;
    if (! juce::isPositiveAndBelow (ref.track, (int) tracks.size())) return;
    auto& t = tracks[(size_t) ref.track];

    auto split = [&] (auto& list)
    {
        auto& c = list[(size_t) ref.index];
        if (at <= c.timelineStart || at >= c.timelineStart + c.length) return;
        oldLength = c.length;
        auto tail = c;
        const juce::int64 head = at - c.timelineStart;
        tail.timelineStart = at;
        tail.length = c.length - head;
        offsetOf (tail) += head;
        c.length = head;
        oldFadeOut = c.fadeOut;
        c.fadeOut = 0;  c.clampFades();      // the join is now a hard cut
        tail.fadeIn = 0; tail.clampFades();
        list.push_back (tail);
        second = { ref.track, ref.kind, (int) list.size() - 1 };
        didSplit = true;
    };

    switch (ref.kind)
    {
        case ClipRef::Kind::audio:   split (t.clips); break;
        case ClipRef::Kind::pattern: split (t.patternClips); break;
        case ClipRef::Kind::midi:    split (t.midiClips); break;
    }
}

void SplitClipCommand::undo (Session& s)
{
    if (! didSplit) return;
    auto& t = EditAccess::tracks (s)[(size_t) ref.track];
    switch (ref.kind)
    {
        case ClipRef::Kind::audio:   t.clips.pop_back();        t.clips[(size_t) ref.index].length = oldLength; t.clips[(size_t) ref.index].fadeOut = oldFadeOut; break;
        case ClipRef::Kind::pattern: t.patternClips.pop_back(); t.patternClips[(size_t) ref.index].length = oldLength; t.patternClips[(size_t) ref.index].fadeOut = oldFadeOut; break;
        case ClipRef::Kind::midi:    t.midiClips.pop_back();    t.midiClips[(size_t) ref.index].length = oldLength; t.midiClips[(size_t) ref.index].fadeOut = oldFadeOut; break;
    }
}

//==============================================================================
// Fades & clip gain

void SetClipFadesCommand::execute (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        oldIn = c.fadeIn; oldOut = c.fadeOut; oldInShape = c.fadeInShape; oldOutShape = c.fadeOutShape;
        c.fadeIn = juce::jmax<juce::int64> (0, in); c.fadeOut = juce::jmax<juce::int64> (0, out);
        c.fadeInShape = inShape; c.fadeOutShape = outShape;
        c.clampFades();
    });
}

void SetClipFadesCommand::undo (Session& s)
{
    withClip (s, ref, [&] (auto& c) { c.fadeIn = oldIn; c.fadeOut = oldOut; c.fadeInShape = oldInShape; c.fadeOutShape = oldOutShape; });
}

void SetClipGainCommand::execute (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        oldGain = c.gain; c.gain = gain;
    });
}

void SetClipGainCommand::undo (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        if constexpr (std::is_same_v<std::decay_t<decltype (c)>, AudioClip>) c.gain = oldGain;
    });
}

//==============================================================================
// Duplicate

void DuplicateClipCommand::execute (Session& s)
{
    auto& tracks = EditAccess::tracks (s);
    if (! juce::isPositiveAndBelow (ref.track, (int) tracks.size())) return;
    auto& t = tracks[(size_t) ref.track];

    auto dup = [&] (auto& list)
    {
        auto c = list[(size_t) ref.index];
        c.timelineStart += c.length;
        list.push_back (c);
        copy = { ref.track, ref.kind, (int) list.size() - 1 };
    };
    switch (ref.kind)
    {
        case ClipRef::Kind::audio:   dup (t.clips); break;
        case ClipRef::Kind::pattern: dup (t.patternClips); break;
        case ClipRef::Kind::midi:    dup (t.midiClips); break;
    }
}

void DuplicateClipCommand::undo (Session& s)
{
    auto& t = EditAccess::tracks (s)[(size_t) ref.track];
    switch (ref.kind)
    {
        case ClipRef::Kind::audio:   t.clips.pop_back(); break;
        case ClipRef::Kind::pattern: t.patternClips.pop_back(); break;
        case ClipRef::Kind::midi:    t.midiClips.pop_back(); break;
    }
}

void SetClipGainLaneCommand::execute (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        if constexpr (std::is_same_v<std::decay_t<decltype (c)>, AudioClip>)
        {
            oldLane = c.gainLane;
            c.gainLane = (newLane != nullptr && ! newLane->isEmpty()) ? newLane : nullptr;
        }
    });
}

void SetClipGainLaneCommand::undo (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        if constexpr (std::is_same_v<std::decay_t<decltype (c)>, AudioClip>) c.gainLane = oldLane;
    });
}

void ReplaceClipAudioCommand::execute (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        if constexpr (std::is_same_v<std::decay_t<decltype (c)>, AudioClip>)
        {
            if (newAudio == nullptr) return;
            oldAudio = c.audio; oldModified = c.audioModified;
            c.audio = newAudio; c.audioModified = true;
        }
    });
}

void ReplaceClipAudioCommand::undo (Session& s)
{
    withClip (s, ref, [&] (auto& c)
    {
        if constexpr (std::is_same_v<std::decay_t<decltype (c)>, AudioClip>) { c.audio = oldAudio; c.audioModified = oldModified; }
    });
}

//==============================================================================
// Repack (Shuffle)

void RepackTrackCommand::execute (Session& s)
{
    oldStarts.clear();
    auto refs = ClipEdits::allClips (s, track);
    std::vector<std::pair<juce::int64, ClipRef>> order;
    for (const auto& r : refs)
        if (auto t = ClipEdits::timing (s, r)) { order.emplace_back (t->start, r); oldStarts.emplace_back (r, t->start); }
    std::sort (order.begin(), order.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    juce::int64 cursor = -1;
    for (const auto& [start, r] : order)
    {
        if (cursor < 0) { cursor = start; }
        withClip (s, r, [&] (auto& c) { c.timelineStart = cursor; cursor += c.length; });
    }
}

void RepackTrackCommand::undo (Session& s)
{
    for (const auto& [r, start] : oldStarts)
        withClip (s, r, [&] (auto& c) { c.timelineStart = start; });
}

} // namespace beatmaker::model
