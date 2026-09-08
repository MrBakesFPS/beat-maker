#include "NoteEdits.h"
#include <algorithm>

namespace beatmaker::model
{

NoteEdits::Indices NoteEdits::all (const Seq& s) { Indices v; for (int i = 0; i < (int) s.notes.size(); ++i) v.push_back (i); return v; }

NoteEdits::Indices NoteEdits::inRegion (const Seq& s, double b0, double b1, int p0, int p1)
{
    if (b0 > b1) std::swap (b0, b1);
    if (p0 > p1) std::swap (p0, p1);
    Indices v;
    for (int i = 0; i < (int) s.notes.size(); ++i)
    {
        const auto& n = s.notes[(size_t) i];
        if (n.pitch >= p0 && n.pitch <= p1 && n.startBeat < b1 && n.getEndBeat() > b0) v.push_back (i);
    }
    return v;
}

std::vector<NoteKey> NoteEdits::keysOf (const Seq& s, const Indices& idx)
{
    std::vector<NoteKey> k;
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) s.notes.size())) k.push_back ({ s.notes[(size_t) i].pitch, s.notes[(size_t) i].startBeat });
    return k;
}

NoteEdits::Indices NoteEdits::resolve (const Seq& s, const std::vector<NoteKey>& keys)
{
    // One note per key: notes stacked on the same pitch and beat (a note moved onto another) are not all
    // taken by one key, so dragging one of them away leaves the other in place
    Indices v;
    std::vector<bool> taken (s.notes.size(), false);
    for (const auto& k : keys)
        for (int i = 0; i < (int) s.notes.size(); ++i)
            if (! taken[(size_t) i] && k.matches (s.notes[(size_t) i])) { taken[(size_t) i] = true; v.push_back (i); break; }
    std::sort (v.begin(), v.end());
    return v;
}

static bool has (const NoteEdits::Indices& idx, int i) { return std::find (idx.begin(), idx.end(), i) != idx.end(); }
static void clampNote (engine::NoteEvent& n, double length)
{
    n.startBeat = juce::jlimit (0.0, juce::jmax (0.0, length - 1.0e-6), n.startBeat);
    n.lengthBeats = juce::jlimit (1.0 / 64.0, juce::jmax (1.0 / 64.0, length - n.startBeat), n.lengthBeats);
    n.pitch = juce::jlimit (0, 127, n.pitch);
    n.velocity = juce::jlimit (1, 127, n.velocity);
}

NoteEdits::Seq NoteEdits::quantize (const Seq& s, const Indices& idx, double grid, float strength, bool alsoLengths)
{
    Seq out = s;
    if (grid <= 0.0) return out;
    for (int i : idx)
    {
        if (! juce::isPositiveAndBelow (i, (int) out.notes.size())) continue;
        auto& n = out.notes[(size_t) i];
        n.startBeat += (snap (n.startBeat, grid) - n.startBeat) * strength;
        if (alsoLengths) n.lengthBeats = juce::jmax (grid, snap (n.lengthBeats, grid));
        clampNote (n, out.lengthBeats);
    }
    out.sortNotes();
    return out;
}

NoteEdits::Seq NoteEdits::transpose (const Seq& s, const Indices& idx, int semis)
{
    Seq out = s;
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) out.notes.size())) { out.notes[(size_t) i].pitch += semis; clampNote (out.notes[(size_t) i], out.lengthBeats); }
    out.sortNotes();
    return out;
}

NoteEdits::Seq NoteEdits::nudge (const Seq& s, const Indices& idx, double beats)
{
    Seq out = s;
    // Keep the group together: clamp the delta so no note leaves the sequence.
    double delta = beats;
    for (int i : idx)
        if (juce::isPositiveAndBelow (i, (int) out.notes.size()))
        {
            const auto& n = out.notes[(size_t) i];
            delta = juce::jlimit (-n.startBeat, juce::jmax (0.0, out.lengthBeats - n.getEndBeat()), delta);
        }
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) out.notes.size())) { out.notes[(size_t) i].startBeat += delta; clampNote (out.notes[(size_t) i], out.lengthBeats); }
    out.sortNotes();
    return out;
}

NoteEdits::Seq NoteEdits::changeVelocity (const Seq& s, const Indices& idx, int delta)
{
    Seq out = s;
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) out.notes.size())) out.notes[(size_t) i].velocity = juce::jlimit (1, 127, out.notes[(size_t) i].velocity + delta);
    return out;
}

NoteEdits::Seq NoteEdits::setVelocity (const Seq& s, const Indices& idx, int velocity)
{
    Seq out = s;
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) out.notes.size())) out.notes[(size_t) i].velocity = juce::jlimit (1, 127, velocity);
    return out;
}

NoteEdits::Seq NoteEdits::setLength (const Seq& s, const Indices& idx, double beats)
{
    Seq out = s;
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) out.notes.size())) { out.notes[(size_t) i].lengthBeats = beats; clampNote (out.notes[(size_t) i], out.lengthBeats); }
    return out;
}

NoteEdits::Seq NoteEdits::remove (const Seq& s, const Indices& idx)
{
    Seq out = s;
    out.notes.clear();
    for (int i = 0; i < (int) s.notes.size(); ++i) if (! has (idx, i)) out.notes.push_back (s.notes[(size_t) i]);
    return out;
}

std::vector<engine::NoteEvent> NoteEdits::copy (const Seq& s, const Indices& idx)
{
    std::vector<engine::NoteEvent> v;
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) s.notes.size())) v.push_back (s.notes[(size_t) i]);
    std::sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return v;
}

NoteEdits::Seq NoteEdits::paste (const Seq& s, const std::vector<engine::NoteEvent>& notes, double atBeat, std::vector<NoteKey>* newKeys)
{
    Seq out = s;
    if (notes.empty()) return out;
    double first = notes.front().startBeat;
    for (const auto& n : notes) first = juce::jmin (first, n.startBeat);
    for (auto n : notes)
    {
        n.startBeat = atBeat + (n.startBeat - first);
        if (n.startBeat >= out.lengthBeats) continue;
        clampNote (n, out.lengthBeats);
        out.notes.push_back (n);
        if (newKeys) newKeys->push_back ({ n.pitch, n.startBeat });
    }
    out.sortNotes();
    return out;
}

NoteEdits::Seq NoteEdits::duplicate (const Seq& s, const Indices& idx, std::vector<NoteKey>* newKeys)
{
    const auto notes = copy (s, idx);
    if (notes.empty()) return s;
    double first = notes.front().startBeat, end = 0.0;
    for (const auto& n : notes) { first = juce::jmin (first, n.startBeat); end = juce::jmax (end, n.getEndBeat()); }
    return paste (s, notes, first + (end - first), newKeys);
}

NoteEdits::Seq NoteEdits::legato (const Seq& s, const Indices& idx)
{
    Seq out = s;
    out.sortNotes();
    for (int i : idx)
    {
        if (! juce::isPositiveAndBelow (i, (int) out.notes.size())) continue;
        auto& n = out.notes[(size_t) i];
        double next = out.lengthBeats;
        for (const auto& o : out.notes) if (o.startBeat > n.startBeat + 1.0e-6 && o.startBeat < next) next = o.startBeat;
        n.lengthBeats = juce::jmax (1.0 / 64.0, next - n.startBeat);
    }
    return out;
}

} // namespace beatmaker::model
