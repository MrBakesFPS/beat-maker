// ClipJoin: joins selected clips of one track into one clip. MIDI and
// pattern clips are merged by writing their notes or steps out at their
// timeline positions (loops included) into one sequence spanning the whole
// range, gaps kept as silence. Audio clips join only when they are adjacent
// pieces of the same audio (a "heal"); anything else needs a bounce.
#pragma once

#include "Session.h"
#include "ClipEdits.h"
#include <memory>
#include <vector>

namespace beatmaker::model
{

class JoinClipsCommand final : public Command
{
public:
    // All refs must be on one track and of one kind (checked by canJoin).
    JoinClipsCommand (const Session&, std::vector<ClipRef> refs);
    juce::String getName() const override { return "Join Clips"; }
    void execute (Session&) override;
    void undo (Session&) override;
    bool wasCancelled() const noexcept { return cancelled; }
    ClipRef getResult() const noexcept { return result; }

private:
    std::vector<ClipRef> refs;
    int track = -1; ClipRef::Kind kind = ClipRef::Kind::audio;
    bool cancelled = true, applied = false;
    ClipRef result;
    // Undo keeps the whole list of that kind
    std::vector<AudioClip> audioBefore; std::vector<PatternClip> patternBefore; std::vector<MidiClip> midiBefore;
    AudioClip joinedAudio; PatternClip joinedPattern; MidiClip joinedMidi;
};

class ClipJoin
{
public:
    // Whether `refs` can be joined; `reason` explains a refusal.
    static bool canJoin (const Session&, const std::vector<ClipRef>& refs, juce::String& reason);
    // Groups a mixed selection by track and kind, in timeline order.
    static std::vector<std::vector<ClipRef>> groups (const Session&, const std::vector<ClipRef>& selection);
};

} // namespace beatmaker::model
