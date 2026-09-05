// Arrangement sections: markers with a range that can be reordered or
// duplicated, taking every track's clips with them (one undo step).
#pragma once

#include "Session.h"
#include "ClipEdits.h"

namespace beatmaker::model
{

class Arrangement
{
public:
    // Every clip (all kinds, all tracks) starting inside [from, to) moves by `deltaSeconds`.
    static void shiftClips (const Session&, double from, double to, double deltaSeconds, CompoundCommand&);

    // Swap a section with its earlier/later neighbour in the arrangement strip,
    // moving both sections' contents. Returns nullptr when there is no neighbour.
    static std::unique_ptr<Command> moveSection (const Session&, int markerId, bool later);

    // Insert a copy of the section right after itself: later material shifts
    // right by the section length, the section's clips are duplicated.
    static std::unique_ptr<Command> duplicateSection (const Session&, int markerId);

    // Remove the section and its time: later material shifts left.
    static std::unique_ptr<Command> deleteSectionTime (const Session&, int markerId);
};

} // namespace beatmaker::model
