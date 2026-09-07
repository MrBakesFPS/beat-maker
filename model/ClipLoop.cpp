#include "ClipLoop.h"

namespace beatmaker::model
{

bool ClipLoop::isLoopClip (const Session&, const ClipRef& ref) { return ref.kind != ClipRef::Kind::audio; }

bool ClipLoop::isLooping (const Session& s, const ClipRef& ref)
{
    const auto* t = s.getTrack (ref.track);
    if (t == nullptr) return false;
    if (ref.kind == ClipRef::Kind::midi) return juce::isPositiveAndBelow (ref.index, (int) t->midiClips.size()) && t->midiClips[(size_t) ref.index].loop;
    if (ref.kind == ClipRef::Kind::pattern) return juce::isPositiveAndBelow (ref.index, (int) t->patternClips.size()) && t->patternClips[(size_t) ref.index].loop;
    return false;
}

juce::int64 ClipLoop::contentLength (const Session& s, const ClipRef& ref)
{
    const auto* t = s.getTrack (ref.track);
    if (t == nullptr || s.getBpm() <= 0.0) return 0;
    const double secondsPerBeat = 60.0 / s.getBpm();
    if (ref.kind == ClipRef::Kind::midi && juce::isPositiveAndBelow (ref.index, (int) t->midiClips.size()))
    {
        const auto& c = t->midiClips[(size_t) ref.index];
        return c.sequence != nullptr ? (juce::int64) std::llround (c.sequence->lengthBeats * secondsPerBeat * c.sampleRate) : 0;
    }
    if (ref.kind == ClipRef::Kind::pattern && juce::isPositiveAndBelow (ref.index, (int) t->patternClips.size()))
    {
        const auto& c = t->patternClips[(size_t) ref.index];
        return c.pattern != nullptr ? (juce::int64) std::llround (c.pattern->getLengthBeats() * secondsPerBeat * c.sampleRate) : 0;
    }
    return 0;
}

juce::int64 ClipLoop::nextClipStart (const Session& s, const ClipRef& ref)
{
    const auto timing = ClipEdits::timing (s, ref);
    if (! timing) return -1;
    juce::int64 next = -1;
    for (const auto& other : ClipEdits::allClips (s, ref.track))
    {
        if (other.kind == ref.kind && other.index == ref.index) continue;
        const auto ot = ClipEdits::timing (s, other);
        if (ot && ot->start > timing->start && (next < 0 || ot->start < next)) next = ot->start;
    }
    return next;
}

std::unique_ptr<Command> ClipLoop::setLoop (const Session& s, const ClipRef& ref, bool on)
{
    if (! isLoopClip (s, ref)) return nullptr;
    const auto timing = ClipEdits::timing (s, ref);
    if (! timing) return nullptr;
    const auto content = contentLength (s, ref);
    auto compound = std::make_unique<CompoundCommand> (on ? "Loop Clip" : "Play Clip Once");
    compound->add (std::make_unique<SetClipLoopCommand> (ref.track, ref.kind == ClipRef::Kind::midi, ref.index, on));
    if (content > 0)
    {
        if (on)
        {
            // One extra pass, or as much as fits before the next clip
            juce::int64 wanted = juce::jmax (timing->length, content * 2);
            const auto next = nextClipStart (s, ref);
            if (next >= 0) wanted = juce::jmin (wanted, next - timing->start);
            wanted = juce::jmax (wanted, juce::jmin (timing->length, content));   // never shrink below what it was (or its content)
            if (wanted != timing->length) compound->add (std::make_unique<TrimClipCommand> (ref, timing->start, juce::jmax<juce::int64> (1, wanted)));
        }
        else if (timing->length > content)
            compound->add (std::make_unique<TrimClipCommand> (ref, timing->start, content));   // back to its original size
    }
    return compound;
}

std::unique_ptr<Command> ClipLoop::afterTrim (const Session& s, const ClipRef& ref)
{
    if (! isLoopClip (s, ref) || ! isLooping (s, ref)) return nullptr;
    const auto timing = ClipEdits::timing (s, ref);
    const auto content = contentLength (s, ref);
    if (! timing || content <= 0 || timing->length > content) return nullptr;
    return std::make_unique<SetClipLoopCommand> (ref.track, ref.kind == ClipRef::Kind::midi, ref.index, false);
}

} // namespace beatmaker::model
