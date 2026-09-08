#include "StepEdits.h"
#include <algorithm>

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
    for (const auto& c : cells) { out.set (c.pad, c.step, 0); out.setLength (c.pad, c.step, 1); }
    return out;
}

int StepEdits::maxLength (const Pattern& p, StepCell c) noexcept
{
    int room = juce::jmax (1, p.numSteps - c.step);
    for (int s = c.step + 1; s < p.numSteps; ++s) if (p.get (c.pad, s) > 0) { room = s - c.step; break; }
    return juce::jmax (1, room);
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

StepCell StepEdits::hitCovering (const Pattern& p, int pad, int step) noexcept
{
    if (! juce::isPositiveAndBelow (pad, Pattern::maxPads) || step < 0 || step >= p.numSteps) return { pad, -1 };
    for (int s = step; s >= 0; --s)
        if (p.get (pad, s) > 0) return { pad, s + p.getLength (pad, s) > step ? s : -1 };
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

StepEdits::Pattern StepEdits::shift (const Pattern& p, const Cells& cells, int stepDelta, int padDelta, Cells* moved)
{
    Pattern out = p;
    if (cells.empty()) return out;
    for (const auto& c : cells)
    {
        stepDelta = juce::jlimit (-c.step, juce::jmax (0, p.numSteps - 1 - c.step), stepDelta);
        padDelta = juce::jlimit (-c.pad, juce::jmax (0, Pattern::maxPads - 1 - c.pad), padDelta);
    }
    std::vector<StepHit> hits;
    for (const auto& c : cells) { hits.push_back ({ c.pad, c.step, p.get (c.pad, c.step), p.getLength (c.pad, c.step) }); out.set (c.pad, c.step, 0); out.setLength (c.pad, c.step, 1); }
    if (moved) moved->clear();
    for (const auto& h : hits)
    {
        out.set (h.pad + padDelta, h.step + stepDelta, h.velocity);
        out.setLength (h.pad + padDelta, h.step + stepDelta, juce::jmin (h.length, out.numSteps - (h.step + stepDelta)));
        if (moved) moved->push_back ({ h.pad + padDelta, h.step + stepDelta });
    }
    return out;
}

std::vector<StepHit> StepEdits::copy (const Pattern& p, const Cells& cells)
{
    std::vector<StepHit> hits;
    int first = -1;
    for (const auto& c : cells) if (p.get (c.pad, c.step) > 0) { hits.push_back ({ c.pad, c.step, p.get (c.pad, c.step), p.getLength (c.pad, c.step) }); first = first < 0 ? c.step : juce::jmin (first, c.step); }
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
        for (int s = 0; s < out.numSteps; ++s) { out.set (pad, s, p.get (pad, s % oldN)); out.setLength (pad, s, p.get (pad, s % oldN) > 0 ? juce::jmin (p.getLength (pad, s % oldN), out.numSteps - s) : 1); }
    return out;
}

StepEdits::Pattern StepEdits::resize (const Pattern& p, int numSteps)
{
    Pattern out = p;
    out.numSteps = juce::jlimit (1, Pattern::maxSteps, numSteps);
    for (int pad = 0; pad < Pattern::maxPads; ++pad)
    {
        for (int s = p.numSteps; s < out.numSteps; ++s) { out.set (pad, s, 0); out.setLength (pad, s, 1); }
        for (int s = 0; s < juce::jmin (p.numSteps, out.numSteps); ++s) if (out.get (pad, s) > 0) out.setLength (pad, s, juce::jmin (p.getLength (pad, s), out.numSteps - s));   // a shrink cuts held hits
    }
    return out;
}

} // namespace beatmaker::model
