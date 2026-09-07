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

double ClipLoop::contentBeats (const Session& s, const ClipRef& ref)
{
    const auto* t = s.getTrack (ref.track);
    if (t == nullptr) return 0.0;
    if (ref.kind == ClipRef::Kind::midi && juce::isPositiveAndBelow (ref.index, (int) t->midiClips.size()))
        return t->midiClips[(size_t) ref.index].sequence != nullptr ? t->midiClips[(size_t) ref.index].sequence->lengthBeats : 0.0;
    if (ref.kind == ClipRef::Kind::pattern && juce::isPositiveAndBelow (ref.index, (int) t->patternClips.size()))
        return t->patternClips[(size_t) ref.index].pattern != nullptr ? t->patternClips[(size_t) ref.index].pattern->getLengthBeats() : 0.0;
    return 0.0;
}

double ClipLoop::baseBeats (const Session& s, const ClipRef& ref)
{
    const auto* t = s.getTrack (ref.track);
    double base = 0.0;
    if (t != nullptr && ref.kind == ClipRef::Kind::midi && juce::isPositiveAndBelow (ref.index, (int) t->midiClips.size())) base = t->midiClips[(size_t) ref.index].loopBaseBeats;
    if (t != nullptr && ref.kind == ClipRef::Kind::pattern && juce::isPositiveAndBelow (ref.index, (int) t->patternClips.size())) base = t->patternClips[(size_t) ref.index].loopBaseBeats;
    return base > 0.0 ? base : contentBeats (s, ref);
}

double ClipLoop::usedBeats (const Session& s, const ClipRef& ref)
{
    const auto* t = s.getTrack (ref.track);
    if (t == nullptr) return 0.0;
    double used = 0.0;
    if (ref.kind == ClipRef::Kind::midi && juce::isPositiveAndBelow (ref.index, (int) t->midiClips.size()))
    {
        if (const auto& seq = t->midiClips[(size_t) ref.index].sequence) for (const auto& n : seq->notes) used = juce::jmax (used, n.getEndBeat());
    }
    else if (ref.kind == ClipRef::Kind::pattern && juce::isPositiveAndBelow (ref.index, (int) t->patternClips.size()))
    {
        if (const auto& p = t->patternClips[(size_t) ref.index].pattern)
            for (int pad = 0; pad < engine::StepPattern::maxPads; ++pad)
                for (int st = p->numSteps - 1; st >= 0; --st)
                    if (p->get (pad, st) > 0) { used = juce::jmax (used, (double) (st + 1) / juce::jmax (1, p->stepsPerBeat)); break; }
    }
    return used;
}

void ClipLoop::addRestoreContent (const Session& s, const ClipRef& ref, CompoundCommand& compound)
{
    const double base = baseBeats (s, ref), content = contentBeats (s, ref);
    if (base <= 0.0 || content <= base + 1.0e-9) return;                 // nothing grew
    if (usedBeats (s, ref) > base + 1.0e-9) return;                       // notes or hits live past the base: keep them
    const auto* t = s.getTrack (ref.track);
    if (ref.kind == ClipRef::Kind::midi)
    {
        auto seq = std::make_shared<engine::MidiSequence> (*t->midiClips[(size_t) ref.index].sequence);
        seq->lengthBeats = base;
        compound.add (std::make_unique<ReplaceMidiSequenceCommand> (ref.track, ref.index, std::move (seq), "Restore Clip"));
    }
    else
    {
        auto pat = std::make_shared<engine::StepPattern> (*t->patternClips[(size_t) ref.index].pattern);
        pat->numSteps = juce::jlimit (1, engine::StepPattern::maxSteps, (int) std::round (base * pat->stepsPerBeat));
        compound.add (std::make_unique<ReplacePatternCommand> (ref.track, ref.index, std::move (pat), "Restore Clip"));
    }
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
    const bool midi = ref.kind == ClipRef::Kind::midi;
    auto compound = std::make_unique<CompoundCommand> (on ? "Loop Clip" : "Play Clip Once");
    compound->add (std::make_unique<SetClipLoopCommand> (ref.track, midi, ref.index, on));
    if (on) compound->add (std::make_unique<SetClipLoopBaseCommand> (ref.track, midi, ref.index, contentBeats (s, ref)));   // remember what "normal" is
    else addRestoreContent (s, ref, *compound);
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, s.getBpm());
    const auto content = on ? contentLength (s, ref) : (juce::int64) std::llround (baseBeats (s, ref) * secondsPerBeat * timing->sampleRate);
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
        else if (timing->length != content)
            compound->add (std::make_unique<TrimClipCommand> (ref, timing->start, content));   // back to its original size
    }
    if (! on) compound->add (std::make_unique<SetClipLoopBaseCommand> (ref.track, midi, ref.index, 0.0));
    return compound;
}

std::unique_ptr<Command> ClipLoop::afterTrim (const Session& s, const ClipRef& ref)
{
    if (! isLoopClip (s, ref) || ! isLooping (s, ref)) return nullptr;
    const auto timing = ClipEdits::timing (s, ref);
    if (! timing) return nullptr;
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, s.getBpm());
    const auto base = (juce::int64) std::llround (baseBeats (s, ref) * secondsPerBeat * timing->sampleRate);
    if (base <= 0 || timing->length > base) return nullptr;
    // Back to (or below) its base: the loop is off, grown content shrinks to the base, the base is forgotten
    auto compound = std::make_unique<CompoundCommand> ("Loop Off");
    compound->add (std::make_unique<SetClipLoopCommand> (ref.track, ref.kind == ClipRef::Kind::midi, ref.index, false));
    addRestoreContent (s, ref, *compound);
    compound->add (std::make_unique<SetClipLoopBaseCommand> (ref.track, ref.kind == ClipRef::Kind::midi, ref.index, 0.0));
    return compound;
}

} // namespace beatmaker::model
