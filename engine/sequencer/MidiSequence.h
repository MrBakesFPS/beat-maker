// MidiSequence: notes in beats. Immutable once shared with the engine; the
// model replaces the whole sequence copy-on-write on every edit.
#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace beatmaker::engine
{

struct NoteEvent
{
    int pitch = 60;          // MIDI note number
    int velocity = 100;      // 1..127
    double startBeat = 0.0;
    double lengthBeats = 1.0;

    double getEndBeat() const noexcept { return startBeat + lengthBeats; }

    static bool sameBeat (double a, double b) noexcept { return std::abs (a - b) < 1.0e-9; }
    bool samePlacement (const NoteEvent& o) const noexcept
    {
        return pitch == o.pitch && sameBeat (startBeat, o.startBeat) && sameBeat (lengthBeats, o.lengthBeats);
    }
};

// Pro Tools-style real-time properties: applied while playing, the stored
// notes are untouched.
struct MidiRealtimeProps
{
    bool quantize = false;
    double quantizeBeats = 0.25;      // grid
    float quantizeStrength = 1.0f;    // 0..1
    int transpose = 0;                // semitones
    float velocityScale = 1.0f;       // multiplier
    int velocityOffset = 0;           // added after scaling
    double delayMs = 0.0;             // +/- shift
    float durationScale = 1.0f;       // note length multiplier

    bool isIdentity() const noexcept
    {
        return ! quantize && transpose == 0 && std::abs (velocityScale - 1.0f) < 1e-6f && velocityOffset == 0
            && std::abs (delayMs) < 1e-9 && std::abs (durationScale - 1.0f) < 1e-6f;
    }

    // Beat-domain part (delay is applied in samples by the scheduler).
    NoteEvent apply (const NoteEvent& n) const noexcept
    {
        NoteEvent out = n;
        if (quantize && quantizeBeats > 0.0)
        {
            const double target = std::round (n.startBeat / quantizeBeats) * quantizeBeats;
            out.startBeat = n.startBeat + (target - n.startBeat) * (double) std::clamp (quantizeStrength, 0.0f, 1.0f);
        }
        out.pitch = n.pitch + transpose;
        out.velocity = std::clamp ((int) std::lround (n.velocity * velocityScale) + velocityOffset, 1, 127);
        out.lengthBeats = std::max (0.001, n.lengthBeats * (double) std::max (0.01f, durationScale));
        return out;
    }
};

struct MidiSequence
{
    std::vector<NoteEvent> notes;
    double lengthBeats = 8.0;   // loop length

    void sortNotes()
    {
        std::sort (notes.begin(), notes.end(), [] (const NoteEvent& a, const NoteEvent& b)
        {
            return a.startBeat < b.startBeat || (NoteEvent::sameBeat (a.startBeat, b.startBeat) && a.pitch < b.pitch);
        });
    }

    // A two-bar arpeggio (Am / F) so a new synth track sounds immediately.
    static MidiSequence createDefaultArpeggio()
    {
        MidiSequence s;
        s.lengthBeats = 8.0;
        const int pitches[] = { 57, 60, 64, 67, 57, 60, 64, 67, 53, 57, 60, 65, 53, 57, 60, 65 };
        for (int i = 0; i < 16; ++i)
            s.notes.push_back ({ pitches[i], i % 4 == 0 ? 110 : 85, i * 0.5, 0.45 });
        return s;
    }
};

} // namespace beatmaker::engine
