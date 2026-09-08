#include "StepEdits.h"
#include <algorithm>
#include <cmath>

namespace beatmaker::model
{

StepEdits::Cells StepEdits::all (const Pattern& p)
{
    Cells c;
    for (int pad = 0; pad < Pattern::maxPads; ++pad) for (int s = 0; s < p.numSteps; ++s) if (p.get (pad, s) > 0) c.push_back ({ pad, s });
    return c;
}

StepEdits::Cells StepEdits::inRegion (const Pattern& p, int s0, int s1, int p0, int p1)
{
    if (s0 > s1) std::swap (s0, s1);
    if (p0 > p1) std::swap (p0, p1);
    Cells c;
    for (int pad = juce::jmax (0, p0); pad <= juce::jmin (Pattern::maxPads - 1, p1); ++pad)
        for (int s = juce::jmax (0, s0); s <= juce::jmin (p.numSteps - 1, s1); ++s)
            if (p.get (pad, s) > 0) c.push_back ({ pad, s });
    return c;
}

bool StepEdits::contains (const Cells& cells, StepCell cell) noexcept { return std::find (cells.begin(), cells.end(), cell) != cells.end(); }

StepEdits::Pattern StepEdits::clear (const Pattern& p, const Cells& cells)
{
    Pattern out = p;
    for (const auto& c : cells) { out.set (c.pad, c.step, 0); out.setLength (c.pad, c.step, 1); out.setOffset (c.pad, c.step, 0); }
    return out;
}

int StepEdits::maxLength (const Pattern& p, StepCell c) noexcept
{
    const double start = p.getPosition (c.pad, c.step);
    double room = p.numSteps - start;
    for (int s = c.step + 1; s < p.numSteps; ++s) if (p.get (c.pad, s) > 0) { room = p.getPosition (c.pad, s) - start; break; }
    return juce::jmax (1, (int) std::floor (room + 1.0e-9));
}

StepEdits::Pattern StepEdits::setLength (const Pattern& p, const Cells& cells, int steps)
{
    Pattern out = p;
    for (const auto& c : cells) if (p.get (c.pad, c.step) > 0) out.setLength (c.pad, c.step, juce::jlimit (1, maxLength (p, c), steps));
    return out;
}

StepEdits::Pattern StepEdits::stretch (const Pattern& p, const Cells& cells, int delta)
{
    Pattern out = p;
    for (const auto& c : cells) if (p.get (c.pad, c.step) > 0) out.setLength (c.pad, c.step, juce::jlimit (1, maxLength (p, c), p.getLength (c.pad, c.step) + delta));
    return out;
}

StepCell StepEdits::hitCovering (const Pattern& p, int pad, double pos) noexcept
{
    if (! juce::isPositiveAndBelow (pad, Pattern::maxPads) || pos < 0.0 || pos >= (double) p.numSteps) return { pad, -1 };
    for (int s = juce::jmin (p.numSteps - 1, (int) std::floor (pos)); s >= 0; --s)
        if (p.get (pad, s) > 0)
        {
            const double start = p.getPosition (pad, s);
            if (pos < start) continue;   // the hit in this step plays later than the position asked about
            return { pad, start + p.getLength (pad, s) > pos ? s : -1 };
        }
    return { pad, -1 };
}

StepEdits::Pattern StepEdits::setVelocity (const Pattern& p, const Cells& cells, int v)
{
    Pattern out = p;
    for (const auto& c : cells) out.set (c.pad, c.step, (std::uint8_t) juce::jlimit (1, 127, v));
    return out;
}

StepEdits::Pattern StepEdits::changeVelocity (const Pattern& p, const Cells& cells, int delta)
{
    Pattern out = p;
    for (const auto& c : cells) if (p.get (c.pad, c.step) > 0) out.set (c.pad, c.step, (std::uint8_t) juce::jlimit (1, 127, (int) p.get (c.pad, c.step) + delta));
    return out;
}

StepEdits::Pattern StepEdits::shift (const Pattern& p, const Cells& cells, int stepDelta, int padDelta, Cells* moved, Collide collide)
{
    return shiftFine (p, cells, stepDelta * Pattern::quarters, padDelta, moved, collide);
}

StepEdits::Pattern StepEdits::shiftFine (const Pattern& p, const Cells& cellsIn, int quarterDelta, int padDelta, Cells* moved, Collide collide)
{
    Pattern out = p;
    const Cells cells = cellsIn;   // `moved` may be the caller's selection, i.e. the same vector as `cellsIn`
    const auto stay = [&] { if (moved) *moved = cells; return out; };
    if (cells.empty()) { if (moved) moved->clear(); return out; }
    const int q = Pattern::quarters, lastQ = p.numSteps * q - 1;
    for (const auto& c : cells)
    {
        const int at = c.step * q + p.getOffset (c.pad, c.step);
        quarterDelta = juce::jlimit (-at, juce::jmax (0, lastQ - at), quarterDelta);
        padDelta = juce::jlimit (-c.pad, juce::jmax (0, Pattern::maxPads - 1 - c.pad), padDelta);
    }
    if (quarterDelta == 0 && padDelta == 0) return stay();
    std::vector<StepHit> group;
    for (const auto& c : cells) if (p.get (c.pad, c.step) > 0) group.push_back ({ c.pad, c.step, p.get (c.pad, c.step), p.getLength (c.pad, c.step), p.getOffset (c.pad, c.step) });
    // The hits already there are never touched: the group lands only where every cell is free (or its own).
    // With `skip` it keeps going in the same direction until that is so; with `block` it stays put.
    for (int mult = 1; ; ++mult)
    {
        const int dq = quarterDelta * mult, dp = padDelta * mult;
        bool inRange = true, free = true;
        Cells dests;
        for (const auto& h : group)
        {
            const int at = h.step * q + h.offset + dq, pad = h.pad + dp;
            if (at < 0 || at > lastQ || pad < 0 || pad >= Pattern::maxPads) { inRange = false; break; }
            const StepCell d { pad, at / q };
            if ((! contains (cells, d) && p.get (d.pad, d.step) > 0) || contains (dests, d)) free = false;   // taken by a hit outside the group, or by two of ours
            dests.push_back (d);
        }
        if (! inRange) return stay();
        if (free)
        {
            for (const auto& c : cells) { out.set (c.pad, c.step, 0); out.setLength (c.pad, c.step, 1); out.setOffset (c.pad, c.step, 0); }
            for (size_t i = 0; i < group.size(); ++i)
            {
                const auto& h = group[i]; const auto& d = dests[i];
                out.set (d.pad, d.step, h.velocity);
                out.setOffset (d.pad, d.step, (h.step * q + h.offset + dq) % q);
                out.setLength (d.pad, d.step, juce::jmin (h.length, out.numSteps - d.step));
            }
            if (moved) *moved = dests;
            break;
        }
        if (collide == Collide::block) return stay();
    }
    // Held hits never run into the next hit on their pad
    for (int pad = 0; pad < Pattern::maxPads; ++pad)
        for (int st = 0; st < out.numSteps; ++st)
            if (out.get (pad, st) > 0) out.setLength (pad, st, juce::jmin (out.getLength (pad, st), maxLength (out, { pad, st })));
    return out;
}

std::vector<StepHit> StepEdits::copy (const Pattern& p, const Cells& cells)
{
    std::vector<StepHit> hits;
    int first = -1;
    for (const auto& c : cells) if (p.get (c.pad, c.step) > 0) { hits.push_back ({ c.pad, c.step, p.get (c.pad, c.step), p.getLength (c.pad, c.step), p.getOffset (c.pad, c.step) }); first = first < 0 ? c.step : juce::jmin (first, c.step); }
    for (auto& h : hits) h.step -= juce::jmax (0, first);
    std::sort (hits.begin(), hits.end(), [] (const StepHit& a, const StepHit& b) { return a.step < b.step || (a.step == b.step && a.pad < b.pad); });
    return hits;
}

StepEdits::Pattern StepEdits::paste (const Pattern& p, const std::vector<StepHit>& hits, int atStep, Cells* pasted)
{
    Pattern out = p;
    if (pasted) pasted->clear();
    for (const auto& h : hits)
    {
        const int s = atStep + h.step;
        if (s < 0 || s >= out.numSteps) continue;
        out.set (h.pad, s, h.velocity);
        out.setLength (h.pad, s, juce::jmin (juce::jmax (1, h.length), out.numSteps - s));
        out.setOffset (h.pad, s, h.offset);
        if (pasted) pasted->push_back ({ h.pad, s });
    }
    return out;
}

StepEdits::Pattern StepEdits::duplicate (const Pattern& p, const Cells& cells, Cells* pasted)
{
    const auto hits = copy (p, cells);
    if (hits.empty()) return p;
    int first = -1, last = 0;
    for (const auto& c : cells) { first = first < 0 ? c.step : juce::jmin (first, c.step); last = juce::jmax (last, c.step); }
    return paste (p, hits, last + 1, pasted);
}

StepEdits::Pattern StepEdits::unroll (const Pattern& p, int totalSteps)
{
    Pattern out = p;
    const int oldN = juce::jmax (1, p.numSteps);
    out.numSteps = juce::jlimit (1, Pattern::maxSteps, totalSteps);
    for (int pad = 0; pad < Pattern::maxPads; ++pad)
        for (int s = 0; s < out.numSteps; ++s) { out.set (pad, s, p.get (pad, s % oldN)); out.setLength (pad, s, p.get (pad, s % oldN) > 0 ? juce::jmin (p.getLength (pad, s % oldN), out.numSteps - s) : 1); out.setOffset (pad, s, p.getOffset (pad, s % oldN)); }
    return out;
}

StepEdits::Pattern StepEdits::resize (const Pattern& p, int numSteps)
{
    Pattern out = p;
    out.numSteps = juce::jlimit (1, Pattern::maxSteps, numSteps);
    for (int pad = 0; pad < Pattern::maxPads; ++pad)
    {
        for (int s = p.numSteps; s < out.numSteps; ++s) { out.set (pad, s, 0); out.setLength (pad, s, 1); out.setOffset (pad, s, 0); }
        for (int s = 0; s < juce::jmin (p.numSteps, out.numSteps); ++s) if (out.get (pad, s) > 0) out.setLength (pad, s, juce::jmin (p.getLength (pad, s), out.numSteps - s));   // a shrink cuts held hits
    }
    return out;
}

} // namespace beatmaker::model
