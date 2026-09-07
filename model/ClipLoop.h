// ClipLoop: the per-clip loop rule for pattern and MIDI clips. Switching a
// loop on extends the clip by its own content length (or up to the next
// clip on the track); switching it off, or trimming the clip back to its
// content, returns it to its original size and Loop must be chosen again.
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
    // The length Loop restores to: the content length when Loop was switched on (else the content now).
    static double baseBeats (const Session&, const ClipRef&);
    // The last note or hit's end, in beats (0 when empty).
    static double usedBeats (const Session&, const ClipRef&);
    // Commands that put the content back to its base length when everything past the base is empty.
    static void addRestoreContent (const Session&, const ClipRef&, CompoundCommand&);
    static juce::int64 nextClipStart (const Session&, const ClipRef&);   // -1 when none follows
    // Loop on: extend by one content length, stopping short of the next clip. Loop off: back to the content length.
    static std::unique_ptr<Command> setLoop (const Session&, const ClipRef&, bool on);
    // After a trim: a looping clip shrunk to its content (or less) stops looping. Null when nothing to do.
    static std::unique_ptr<Command> afterTrim (const Session&, const ClipRef&);
};

} // namespace beatmaker::model
