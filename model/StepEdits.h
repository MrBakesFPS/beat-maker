// StepEdits: pure operations on a StepPattern for the step sequencer:
// cell selection by region, clear, velocity, shift, copy, paste, duplicate,
// unroll a looping clip, extend.
#pragma once

#include <sequencer/StepPattern.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace beatmaker::model
{

struct StepCell { int pad = 0, step = 0; bool operator== (const StepCell& o) const noexcept { return pad == o.pad && step == o.step; } };
struct StepHit { int pad = 0, step = 0; std::uint8_t velocity = 0; int length = 1; int offset = 0; };

class StepEdits
{
public:
    using Pattern = engine::StepPattern;
    using Cells = std::vector<StepCell>;

    static Cells all (const Pattern&);                                            // every lit cell
    static Cells inRegion (const Pattern&, int step0, int step1, int pad0, int pad1);   // lit cells, inclusive ranges either order
    static bool contains (const Cells&, StepCell) noexcept;

    static Pattern clear (const Pattern&, const Cells&);
    static Pattern setVelocity (const Pattern&, const Cells&, int velocity);
    static Pattern changeVelocity (const Pattern&, const Cells&, int delta);
    // Holds each hit for `steps` (1 = a one-shot), capped at the pattern's end and at the next hit on the pad.
    static Pattern setLength (const Pattern&, const Cells&, int steps);
    static Pattern stretch (const Pattern&, const Cells&, int deltaSteps);       // each hit's length + delta, capped the same way
    static int maxLength (const Pattern&, StepCell) noexcept;                     // room before the next hit on the pad (or the end)
    // The hit whose span covers (pad, step): its start cell, or step -1 when the step is silent. The double form
    // takes a position in steps (offsets included), the int form the middle of the step.
    static StepCell hitCovering (const Pattern&, int pad, double stepPosition) noexcept;
    static StepCell hitCovering (const Pattern& p, int pad, int step) noexcept { return hitCovering (p, pad, step + 0.5); }
    // What a move does when a hit lands on a hit outside the group. A moving hit never touches the hits already
    // there: `skip` carries on in the same direction to the next free place (arrow keys pass over a neighbour),
    // `block` leaves the group where it is (a drag hovering an occupied cell).
    enum class Collide { skip, block };
    // Moves the cells by steps and pads (clamped so the group stays inside); returns the moved cells.
    static Pattern shift (const Pattern&, const Cells&, int stepDelta, int padDelta, Cells* moved = nullptr, Collide = Collide::skip);
    // The same in quarter steps: a hit's micro-timing offset carries the fraction (Shift+arrow in the editor).
    static Pattern shiftFine (const Pattern&, const Cells&, int quarterDelta, int padDelta, Cells* moved = nullptr, Collide = Collide::skip);
    static std::vector<StepHit> copy (const Pattern&, const Cells&);             // steps relative to the earliest
    static Pattern paste (const Pattern&, const std::vector<StepHit>&, int atStep, Cells* pasted = nullptr);
    static Pattern duplicate (const Pattern&, const Cells&, Cells* pasted = nullptr);   // right after the selection's last step
    // The pattern written out over `totalSteps` (the clip's steps plus the loop offset), repeating itself; capped at maxSteps.
    static Pattern unroll (const Pattern&, int totalSteps);
    static Pattern resize (const Pattern&, int numSteps);                        // new steps are silent
};

} // namespace beatmaker::model
