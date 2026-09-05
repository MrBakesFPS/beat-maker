// GroupLogic: turns a control change or edit on one track into the commands
// that apply it to every member of the track's active groups.
#pragma once

#include "ClipEdits.h"
#include "Session.h"
#include <vector>

namespace beatmaker::model
{

class GroupLogic
{
public:
    // Track indices (including `trackIndex`) that follow it for Mix or Edit purposes.
    static std::vector<int> mixMembers (const Session&, int trackIndex);
    static std::vector<int> editMembers (const Session&, int trackIndex);

    // Mix: relative gain in dB across the group (when the group follows volume),
    // absolute pan when it follows pan. Returns a single (compound) command.
    static std::unique_ptr<Command> mixCommand (const Session&, int trackIndex, float newGain, float newPan);
    static std::unique_ptr<Command> flagCommand (const Session&, int trackIndex, SetTrackFlagCommand::Flag, bool value);

    // Edit: the same move/trim delta applied to clips on member tracks that
    // start at the same time as the edited clip.
    static std::unique_ptr<Command> moveCommand (const Session&, const ClipRef&, int newTrack, juce::int64 newStart);
    static std::unique_ptr<Command> trimCommand (const Session&, const ClipRef&, juce::int64 newStart, juce::int64 newLength);
    static std::vector<ClipRef> siblingsOf (const Session&, const ClipRef&);   // same-start clips on edit members

    // Group whose badge should show for a track (first active group), if any.
    static std::vector<const Group*> groupsOf (const Session&, int trackId);
};

} // namespace beatmaker::model
