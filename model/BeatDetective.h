// Beat Detective-style rhythm editing: separate clips at their transients,
// conform the clip starts to the grid (with strength, exclusion and swing),
// then smooth the result by filling gaps and crossfading the joins. Each
// step is one undoable compound command.
#pragma once

#include "Session.h"
#include "ClipEdits.h"

namespace beatmaker::model
{

struct ConformSettings
{
    double bpm = 120.0;
    double gridBeats = 0.25;     // 1/16 at the default
    float strength = 1.0f;       // 0..1: how far toward the grid line
    float excludeWithin = 0.0f;  // 0..1: clips already within this fraction of half a grid step stay put
    float swing = 0.0f;          // 0..1: every second grid line is delayed; 1 = triplet feel (2/3 of the pair)
};

struct SmoothingSettings
{
    bool fillGaps = true;        // extend each clip to the start of the next one
    bool crossfade = false;      // ...and overlap by crossfadeMs with equal-power fades
    double crossfadeMs = 5.0;
};

class BeatDetective
{
public:
    // Grid line nearest to `sample` (timeline samples), including swing.
    static juce::int64 nearestGridSample (juce::int64 sample, const ConformSettings&, double sampleRate) noexcept;

    // Separate every audio clip in `clips` at its transients (one undo step).
    static std::unique_ptr<Command> separate (const Session&, const std::vector<ClipRef>& clips, float sensitivity = 0.5f);

    // Move the start of every audio clip in `clips` toward the nearest grid line.
    static std::unique_ptr<Command> conform (const Session&, const std::vector<ClipRef>& clips, const ConformSettings&);

    // Fill gaps / crossfade between consecutive audio clips (per track, in start order).
    static std::unique_ptr<Command> smooth (const Session&, const std::vector<ClipRef>& clips, const SmoothingSettings&);
};

} // namespace beatmaker::model
