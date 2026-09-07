#include "ClipLoop.h"
#include "StepEdits.h"

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

double ClipLoop::clipBeats (const Session& s, const ClipRef& ref)
{
    const auto timing = ClipEdits::timing (s, ref);
    if (! timing || s.getBpm() <= 0.0 || timing->sampleRate <= 0.0) return 0.0;
    const double beats = (double) timing->length / timing->sampleRate * s.getBpm() / 60.0;
    return std::round (beats * 1024.0) / 1024.0;   // sample rounding must not make "the same length" differ
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

double ClipLoop::addSetContent (const Session& s, const ClipRef& ref, double beats, CompoundCommand& compound, const juce::String& name)
{
    const auto* t = s.getTrack (ref.track);
    if (t == nullptr || beats <= 0.0) return contentBeats (s, ref);
    if (ref.kind == ClipRef::Kind::midi && juce::isPositiveAndBelow (ref.index, (int) t->midiClips.size()) && t->midiClips[(size_t) ref.index].sequence != nullptr)
    {
        auto seq = std::make_shared<engine::MidiSequence> (*t->midiClips[(size_t) ref.index].sequence);
        seq->lengthBeats = beats;
        compound.add (std::make_unique<ReplaceMidiSequenceCommand> (ref.track, ref.index, std::move (seq), name));
        return beats;
    }
    if (ref.kind == ClipRef::Kind::pattern && juce::isPositiveAndBelow (ref.index, (int) t->patternClips.size()) && t->patternClips[(size_t) ref.index].pattern != nullptr)
    {
        const auto& p = *t->patternClips[(size_t) ref.index].pattern;
        const int steps = juce::jlimit (1, engine::StepPattern::maxSteps, (int) std::round (beats * juce::jmax (1, p.stepsPerBeat)));
        if (steps != p.numSteps) compound.add (std::make_unique<ReplacePatternCommand> (ref.track, ref.index, std::make_shared<engine::StepPattern> (StepEdits::resize (p, steps)), name));
        return (double) steps / juce::jmax (1, p.stepsPerBeat);
    }
    return contentBeats (s, ref);
}

double ClipLoop::addFitContent (const Session& s, const ClipRef& ref, double beats, CompoundCommand& compound)
{
    const double content = contentBeats (s, ref);
    if (beats <= 0.0 || content <= beats + 1.0e-6) return content;     // nothing grew past it
    if (usedBeats (s, ref) > beats + 1.0e-6) return content;            // notes or hits live past it: keep them
    return addSetContent (s, ref, beats, compound, "Fit Notes to Clip");
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
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, s.getBpm());
    auto compound = std::make_unique<CompoundCommand> (on ? "Loop Clip" : "Play Clip Once");
    compound->add (std::make_unique<SetClipLoopCommand> (ref.track, midi, ref.index, on));
    if (on)
    {
        // "Normal" is the length the clip shows now: the content is fitted to it (grown content with nothing in it
        // shrinks, shorter content grows so the pass includes the silence), unless notes past the clip's end
        // would be lost, in which case the content is the pass and stays.
        const double visible = clipBeats (s, ref), content = contentBeats (s, ref);
        double base = content;
        if (content > visible + 1.0e-6) base = addFitContent (s, ref, visible, *compound);
        else if (content < visible - 1.0e-6) base = addSetContent (s, ref, visible, *compound, "Fit Notes to Clip");
        compound->add (std::make_unique<SetClipLoopBaseCommand> (ref.track, midi, ref.index, base));   // remember what "normal" is
        const auto pass = (juce::int64) std::llround (base * secondsPerBeat * timing->sampleRate);
        if (pass > 0)
        {
            // One extra pass, or as much as fits before the next clip; never shorter than it was
            juce::int64 wanted = juce::jmax (timing->length, pass * 2);
            const auto next = nextClipStart (s, ref);
            if (next >= 0) wanted = juce::jmin (wanted, next - timing->start);
            wanted = juce::jmax (wanted, timing->length);
            if (wanted != timing->length) compound->add (std::make_unique<TrimClipCommand> (ref, timing->start, juce::jmax<juce::int64> (1, wanted)));
        }
        return compound;
    }
    const double base = baseBeats (s, ref);
    addFitContent (s, ref, base, *compound);
    const auto baseLength = (juce::int64) std::llround (base * secondsPerBeat * timing->sampleRate);
    if (baseLength > 0 && timing->length != baseLength)
        compound->add (std::make_unique<TrimClipCommand> (ref, timing->start, baseLength));   // back to its original size
    compound->add (std::make_unique<SetClipLoopBaseCommand> (ref.track, midi, ref.index, 0.0));
    return compound;
}

std::unique_ptr<Command> ClipLoop::afterTrim (const Session& s, const ClipRef& ref)
{
    if (! isLoopClip (s, ref)) return nullptr;
    const auto timing = ClipEdits::timing (s, ref);
    if (! timing) return nullptr;
    const double visible = clipBeats (s, ref);
    const bool looping = isLooping (s, ref);
    auto compound = std::make_unique<CompoundCommand> (looping ? "Loop Off" : "Fit Notes to Clip");
    if (looping)
    {
        // Still longer than its base: the loop simply covers fewer or more passes
        if (visible > baseBeats (s, ref) + 1.0e-6) return nullptr;
        // Back to (or below) its base: the loop is off and the base is forgotten
        compound->add (std::make_unique<SetClipLoopCommand> (ref.track, ref.kind == ClipRef::Kind::midi, ref.index, false));
        compound->add (std::make_unique<SetClipLoopBaseCommand> (ref.track, ref.kind == ClipRef::Kind::midi, ref.index, 0.0));
    }
    // Content that grew past the clip's end with nothing in it folds back to the clip
    addFitContent (s, ref, visible, *compound);
    return compound->isEmpty() ? nullptr : std::move (compound);
}

} // namespace beatmaker::model
