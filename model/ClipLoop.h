// ClipLoop: the per-clip loop rule for pattern and MIDI clips. Switching a
// loop on extends the clip by the length it shows (or up to the next clip
// on the track) and remembers that length as its base; switching it off,
// or trimming the clip back to the base, returns it to its original size
// and Loop must be chosen again. Notes or steps that grew to fill an
// extension are folded back whenever nothing was left in them.
#pragma once

#include "Session.h"
#include "ClipEdits.h"
#include <memory>

namespace beatmaker::model
{

class ClipLoop
{
public:
    static bool isLoopClip (const Session&, const ClipRef&);            // a pattern or MIDI clip
    static bool isLooping (const Session&, const ClipRef&);
    static juce::int64 contentLength (const Session&, const ClipRef&);   // samples of one pass at the session tempo
    static double contentBeats (const Session&, const ClipRef&);
    static double clipBeats (const Session&, const ClipRef&);            // the length the clip shows on the timeline
    // The length Loop restores to: the clip's length when Loop was switched on (else the content now).
    static double baseBeats (const Session&, const ClipRef&);
    // The last note or hit's end, in beats (0 when empty).
    static double usedBeats (const Session&, const ClipRef&);
    // Adds a command that sets the content length (a pattern is capped at its step limit); returns the length set.
    static double addSetContent (const Session&, const ClipRef&, double beats, CompoundCommand&, const juce::String& name);
    // Adds a command that shrinks content longer than `beats` when nothing lives past it; returns the content length afterwards.
    static double addFitContent (const Session&, const ClipRef&, double beats, CompoundCommand&);
    static juce::int64 nextClipStart (const Session&, const ClipRef&);   // -1 when none follows
    // Loop on: fit the content to the clip, remember its length, extend by one pass, stopping short of the next clip.
    // Loop off: back to the base length.
    static std::unique_ptr<Command> setLoop (const Session&, const ClipRef&, bool on);
    // After a trim: a looping clip shrunk to its base (or less) stops looping, and any clip shrunk below empty
    // content folds the content to its length. Null when nothing to do.
    static std::unique_ptr<Command> afterTrim (const Session&, const ClipRef&);
};

} // namespace beatmaker::model
