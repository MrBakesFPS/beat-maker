// NoteEdits: pure operations on a MidiSequence for the note editor (and
// scripts): selection by region, quantize, transpose, nudge, velocity,
// duplicate, legato, clipboard paste. Indices refer to `seq.notes`; the
// result is a new sequence, sorted, so callers re-resolve selections by key.
#pragma once

#include <sequencer/MidiSequence.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace beatmaker::model
{

struct NoteKey
{
    int pitch = 0; double startBeat = 0.0;
    bool matches (const engine::NoteEvent& n) const noexcept { return n.pitch == pitch && engine::NoteEvent::sameBeat (n.startBeat, startBeat); }
};

class NoteEdits
{
public:
    using Seq = engine::MidiSequence;
    using Indices = std::vector<int>;

    static Indices all (const Seq&);
    // Notes overlapping the beat range [b0, b1) and the pitch range [p0, p1] (either order).
    static Indices inRegion (const Seq&, double b0, double b1, int p0, int p1);
    static std::vector<NoteKey> keysOf (const Seq&, const Indices&);
    static Indices resolve (const Seq&, const std::vector<NoteKey>&);

    static Seq quantize (const Seq&, const Indices&, double gridBeats, float strength = 1.0f, bool alsoLengths = false);
    static Seq transpose (const Seq&, const Indices&, int semitones);
    static Seq nudge (const Seq&, const Indices&, double beats);                 // clamped inside the sequence
    static Seq changeVelocity (const Seq&, const Indices&, int delta);
    static Seq setVelocity (const Seq&, const Indices&, int velocity);
    static Seq setLength (const Seq&, const Indices&, double beats);
    static Seq remove (const Seq&, const Indices&);
    static Seq duplicate (const Seq&, const Indices&, std::vector<NoteKey>* newKeys = nullptr);   // copies placed right after the selection's end
    static Seq legato (const Seq&, const Indices&);                               // each note extends to the next note's start
    // Pastes `notes` (relative to their earliest start) at `atBeat`; notes past the end are dropped or shortened.
    static Seq paste (const Seq&, const std::vector<engine::NoteEvent>& notes, double atBeat, std::vector<NoteKey>* newKeys = nullptr);
    static std::vector<engine::NoteEvent> copy (const Seq&, const Indices&);
    static double snap (double beat, double grid) noexcept { return grid > 0.0 ? std::round (beat / grid) * grid : beat; }
};

} // namespace beatmaker::model
