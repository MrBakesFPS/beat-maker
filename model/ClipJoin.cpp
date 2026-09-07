#include "ClipJoin.h"
#include <algorithm>
#include <map>

namespace beatmaker::model
{

static double beatsPerSample (const Session& s, double sampleRate) { return s.getBpm() / 60.0 / juce::jmax (1.0, sampleRate); }

std::vector<std::vector<ClipRef>> ClipJoin::groups (const Session& s, const std::vector<ClipRef>& selection)
{
    std::map<std::pair<int, int>, std::vector<ClipRef>> byTrackKind;
    for (const auto& r : selection) if (ClipEdits::timing (s, r)) byTrackKind[{ r.track, (int) r.kind }].push_back (r);
    std::vector<std::vector<ClipRef>> out;
    for (auto& [key, refs] : byTrackKind)
    {
        std::sort (refs.begin(), refs.end(), [&s] (const ClipRef& a, const ClipRef& b) { return ClipEdits::timing (s, a)->start < ClipEdits::timing (s, b)->start; });
        out.push_back (refs);
    }
    return out;
}

bool ClipJoin::canJoin (const Session& s, const std::vector<ClipRef>& refs, juce::String& reason)
{
    if (refs.size() < 2) { reason = "Select two or more clips on one track to join"; return false; }
    for (const auto& r : refs)
        if (r.track != refs.front().track || r.kind != refs.front().kind || ! ClipEdits::timing (s, r)) { reason = "Join works on clips of one track and one kind"; return false; }
    if (refs.front().kind == ClipRef::Kind::audio)
    {
        const auto& t = s.getTracks()[(size_t) refs.front().track];
        auto sorted = refs;
        std::sort (sorted.begin(), sorted.end(), [&t] (const ClipRef& a, const ClipRef& b) { return t.clips[(size_t) a.index].timelineStart < t.clips[(size_t) b.index].timelineStart; });
        for (size_t i = 1; i < sorted.size(); ++i)
        {
            const auto& a = t.clips[(size_t) sorted[i - 1].index];
            const auto& b = t.clips[(size_t) sorted[i].index];
            if (a.audio != b.audio || std::abs ((a.timelineStart + a.length) - b.timelineStart) > 1 || a.sourceOffset + a.length != b.sourceOffset)
            { reason = "Audio clips join only when they are adjacent pieces of the same audio (use Bounce or Commit for anything else)"; return false; }
        }
    }
    return true;
}

JoinClipsCommand::JoinClipsCommand (const Session& s, std::vector<ClipRef> r) : refs (std::move (r))
{
    juce::String reason;
    if (! ClipJoin::canJoin (s, refs, reason)) return;
    track = refs.front().track; kind = refs.front().kind;
    std::sort (refs.begin(), refs.end(), [&s] (const ClipRef& a, const ClipRef& b) { return ClipEdits::timing (s, a)->start < ClipEdits::timing (s, b)->start; });
    const auto& t = s.getTracks()[(size_t) track];
    juce::int64 start = ClipEdits::timing (s, refs.front())->start, end = start;
    for (const auto& ref : refs) { const auto ti = ClipEdits::timing (s, ref); end = juce::jmax (end, ti->start + ti->length); }

    if (kind == ClipRef::Kind::audio)
    {
        joinedAudio = t.clips[(size_t) refs.front().index];
        joinedAudio.length = end - start;
        joinedAudio.fadeOut = t.clips[(size_t) refs.back().index].fadeOut;
        joinedAudio.fadeOutShape = t.clips[(size_t) refs.back().index].fadeOutShape;
        joinedAudio.clampFades();
    }
    else if (kind == ClipRef::Kind::midi)
    {
        const auto& first = t.midiClips[(size_t) refs.front().index];
        joinedMidi = first;
        joinedMidi.timelineStart = start; joinedMidi.length = end - start; joinedMidi.loopOffset = 0; joinedMidi.loop = false;
        const double bps = beatsPerSample (s, first.sampleRate);
        auto seq = std::make_shared<engine::MidiSequence>();
        seq->lengthBeats = juce::jmax (0.25, (double) (end - start) * bps);
        for (const auto& ref : refs)
        {
            const auto& c = t.midiClips[(size_t) ref.index];
            if (c.sequence == nullptr || c.sequence->lengthBeats <= 0.0) continue;
            const double cStart = (double) c.timelineStart * bps, cEnd = (double) (c.timelineStart + c.length) * bps, offset = (double) c.loopOffset * bps;
            const double period = c.sequence->lengthBeats;
            const int firstK = (int) std::floor (offset / period + 1.0e-9);
            for (int k = firstK; ; ++k)
            {
                const double kStart = cStart - offset + k * period;
                if (kStart >= cEnd - 1.0e-9) break;
                if (! c.loop && k > firstK) break;
                for (auto n : c.sequence->notes)
                {
                    const double tBeat = kStart + n.startBeat;
                    if (tBeat < cStart - 1.0e-9 || tBeat >= cEnd - 1.0e-9) continue;
                    n.startBeat = tBeat - (double) start * bps;
                    n.lengthBeats = juce::jmin (n.lengthBeats, cEnd - tBeat);
                    seq->notes.push_back (n);
                }
            }
        }
        seq->sortNotes();
        joinedMidi.sequence = seq;
    }
    else
    {
        const auto& first = t.patternClips[(size_t) refs.front().index];
        joinedPattern = first;
        joinedPattern.timelineStart = start; joinedPattern.length = end - start; joinedPattern.loopOffset = 0; joinedPattern.loop = false;
        const double bps = beatsPerSample (s, first.sampleRate);
        auto pat = std::make_shared<engine::StepPattern>();
        pat->stepsPerBeat = first.pattern != nullptr ? first.pattern->stepsPerBeat : 4;
        pat->numSteps = juce::jlimit (1, engine::StepPattern::maxSteps, (int) std::round ((double) (end - start) * bps * pat->stepsPerBeat));
        for (const auto& ref : refs)
        {
            const auto& c = t.patternClips[(size_t) ref.index];
            if (c.pattern == nullptr || c.pattern->numSteps <= 0) continue;
            const double stepBeats = 1.0 / juce::jmax (1, c.pattern->stepsPerBeat);
            const double cStart = (double) c.timelineStart * bps, cEnd = (double) (c.timelineStart + c.length) * bps, offset = (double) c.loopOffset * bps;
            const int firstStep = (int) std::floor (offset / stepBeats + 1.0e-9);
            for (int abs = firstStep; ; ++abs)
            {
                const double tBeat = cStart - offset + abs * stepBeats;
                if (tBeat >= cEnd - 1.0e-9) break;
                if (! c.loop && abs >= c.pattern->numSteps) break;
                const int step = ((abs % c.pattern->numSteps) + c.pattern->numSteps) % c.pattern->numSteps;
                const int target = (int) std::round ((tBeat - (double) start * bps) * pat->stepsPerBeat);
                if (! juce::isPositiveAndBelow (target, pat->numSteps)) continue;
                for (int pad = 0; pad < engine::StepPattern::maxPads; ++pad)
                    if (const auto v = c.pattern->get (pad, step); v > 0) pat->set (pad, target, v);
            }
        }
        joinedPattern.pattern = pat;
    }
    cancelled = false;
}

void JoinClipsCommand::execute (Session& s)
{
    if (cancelled) return;
    auto& t = EditAccess::tracks (s)[(size_t) track];
    auto removeOthers = [this] (auto& list, auto& joined)
    {
        std::vector<int> indices; for (const auto& r : refs) indices.push_back (r.index);
        std::sort (indices.begin(), indices.end(), std::greater<>());
        for (int i : indices) if (juce::isPositiveAndBelow (i, (int) list.size())) list.erase (list.begin() + i);
        list.push_back (joined);
        result = { track, kind, (int) list.size() - 1 };
    };
    if (kind == ClipRef::Kind::audio) { if (! applied) audioBefore = t.clips; removeOthers (t.clips, joinedAudio); }
    else if (kind == ClipRef::Kind::midi) { if (! applied) midiBefore = t.midiClips; removeOthers (t.midiClips, joinedMidi); }
    else { if (! applied) patternBefore = t.patternClips; removeOthers (t.patternClips, joinedPattern); }
    applied = true;
}

void JoinClipsCommand::undo (Session& s)
{
    if (cancelled || ! applied) return;
    auto& t = EditAccess::tracks (s)[(size_t) track];
    if (kind == ClipRef::Kind::audio) t.clips = audioBefore;
    else if (kind == ClipRef::Kind::midi) t.midiClips = midiBefore;
    else t.patternClips = patternBefore;
}

} // namespace beatmaker::model
