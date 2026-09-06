#include "StepSequencer.h"
#include "../shared/UiProfiler.h"

namespace beatmaker::ui
{

using Tool = EditSettings::Tool;
using StepEdits = model::StepEdits;

StepSequencer::StepSequencer (model::Session& s, engine::Transport& t, engine::AudioGraph& g)
    : session (s), transport (t), graph (g)
{
    addAndMakeVisible (linkButton);
    linkButton.setClickingTogglesState (true);
    linkButton.setToggleState (true, juce::dontSendNotification);
    linkButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.45f));
    linkButton.setTooltip ("Link the time axis to the tracks: the steps sit under their clip and scrolling or zooming either view moves both");
    linkButton.onClick = [this] { setLinked (linkButton.getToggleState()); };
    addAndMakeVisible (loopButton);
    loopButton.setClickingTogglesState (true);
    loopButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.45f));
    loopButton.setTooltip ("Loop: repeat the pattern for the clip's length. Off: it plays once. Lengthen the clip in the tracks (drag its right edge) to loop further");
    loopButton.onClick = [this] { if (getClip() != nullptr && onClipLoopChanged) onClipLoopChanged (trackIndex, clipIndex, loopButton.getToggleState()); };
    addAndMakeVisible (unrollButton);
    unrollButton.setTooltip ("Write the loop out so every bar of the clip can be edited on its own (happens by itself when you edit a repeat)");
    unrollButton.onClick = [this] { unrollLoop(); };
    setWantsKeyboardFocus (true);
    setTitle ("Drum editor");
    startTimerHz (30);
}

void StepSequencer::setTarget (int newTrackIndex, int newClipIndex)
{
    const bool trackChanged = newTrackIndex != trackIndex;
    trackIndex = newTrackIndex;
    if (trackChanged) { clipIndex = newClipIndex >= 0 ? newClipIndex : 0; selection.clear(); ownPixelsPerSecond = 0.0; drag = Drag::none; }
    else if (newClipIndex >= 0 && newClipIndex != clipIndex) setActiveClip (newClipIndex);
    if (clipIndex >= numClips()) { clipIndex = juce::jmax (0, numClips() - 1); selection.clear(); }
    repaint();
}

void StepSequencer::setActiveClip (int ci)
{
    if (ci == clipIndex || ! juce::isPositiveAndBelow (ci, numClips())) return;
    clipIndex = ci; selection.clear(); drag = Drag::none; repaint();
}

int StepSequencer::numClips() const { auto* t = getTrack(); return t != nullptr ? (int) t->patternClips.size() : 0; }
int StepSequencer::clipIndexAtBeat (double beat) const
{
    for (int ci = 0; ci < numClips(); ++ci)
        if (beat >= clipStartBeats (ci) && beat < clipStartBeats (ci) + clipLengthBeats (ci)) return ci;
    return -1;
}

void StepSequencer::setLinked (bool on)
{
    if (linked == on) return;
    if (! on) { const auto v = view(); ownStartSeconds = v.startSeconds; ownPixelsPerSecond = v.pixelsPerSecond; }
    linked = on;
    linkButton.setToggleState (on, juce::dontSendNotification);
    status (on ? "Drum editor follows the tracks' view" : "Drum editor has its own view (Ctrl+wheel zoom, Shift+wheel scroll)");
    repaint();
}

//==============================================================================
// Model access

const model::Track* StepSequencer::getTrack() const { return session.getTrack (trackIndex); }
const model::PatternClip* StepSequencer::clipAt (int ci) const
{
    if (auto* track = getTrack())
        if (juce::isPositiveAndBelow (ci, (int) track->patternClips.size()))
            return &track->patternClips[(size_t) ci];
    return nullptr;
}
const engine::StepPattern* StepSequencer::patternOf (int ci) const { auto* c = clipAt (ci); return c != nullptr ? c->pattern.get() : nullptr; }

//==============================================================================
// Timeline geometry

TimelineView StepSequencer::view() const
{
    TimelineView v;
    if (linked && viewSource != nullptr) v = viewSource();
    else
    {
        v.originX = padColumnWidth + 8.0;
        v.laneWidth = juce::jmax (50.0, (double) getWidth() - v.originX);
        v.startSeconds = ownStartSeconds; v.pixelsPerSecond = ownPixelsPerSecond;
        if (auto* clip = getClip(); clip != nullptr && v.pixelsPerSecond <= 0.0) { v.startSeconds = clip->getStartSeconds(); v.pixelsPerSecond = v.laneWidth / juce::jmax (0.5, clip->getLengthSeconds()); }
        if (v.pixelsPerSecond <= 0.0) v.pixelsPerSecond = 60.0;
    }
    v.originX = juce::jlimit ((double) padColumnWidth, juce::jmax ((double) padColumnWidth, getWidth() - 50.0), v.originX);
    v.laneWidth = juce::jmax (50.0, juce::jmin (v.laneWidth, getWidth() - v.originX));
    return v;
}
int StepSequencer::originX() const { return juce::roundToInt (view().originX); }
double StepSequencer::secondsAtX (int x) const { const auto v = view(); return v.startSeconds + (x - v.originX) / juce::jmax (1.0e-6, v.pixelsPerSecond); }
float StepSequencer::xForSeconds (double sec) const { const auto v = view(); return (float) (v.originX + (sec - v.startSeconds) * v.pixelsPerSecond); }
double StepSequencer::beatAtX (int x) const { return transport.secondsToBeats (secondsAtX (x)); }
float StepSequencer::xForBeat (double beat) const { return xForSeconds (transport.beatsToSeconds (beat)); }
void StepSequencer::applyView (double startSeconds, double pps)
{
    pps = juce::jlimit (5.0, 400000.0, pps); startSeconds = juce::jmax (0.0, startSeconds);
    if (linked) { if (onViewChanged) onViewChanged (startSeconds, pps); }
    else { ownStartSeconds = startSeconds; ownPixelsPerSecond = pps; }
    repaint();
}
juce::Rectangle<int> StepSequencer::gridBounds() const { const int x = originX(); return { x, headerHeight + rulerHeight, getWidth() - x, getHeight() - headerHeight - rulerHeight - velocityHeight }; }
juce::Rectangle<int> StepSequencer::velocityBounds() const { const int x = originX(); return { x, getHeight() - velocityHeight, getWidth() - x, velocityHeight }; }
juce::Rectangle<int> StepSequencer::rulerBounds() const { const int x = originX(); return { x, headerHeight, getWidth() - x, rulerHeight }; }

double StepSequencer::clipStartBeats (int ci) const { auto* c = clipAt (ci); return c != nullptr ? transport.secondsToBeats (c->getStartSeconds()) : 0.0; }
double StepSequencer::clipLengthBeats (int ci) const { auto* c = clipAt (ci); return c != nullptr ? transport.secondsToBeats (c->getLengthSeconds()) : 4.0; }
double StepSequencer::loopOffsetBeats (int ci) const { auto* c = clipAt (ci); return c != nullptr && c->sampleRate > 0.0 ? transport.secondsToBeats ((double) c->loopOffset / c->sampleRate) : 0.0; }
double StepSequencer::stepBeats (int ci) const { auto* p = patternOf (ci); return p != nullptr && p->stepsPerBeat > 0 ? 1.0 / p->stepsPerBeat : 0.25; }
double StepSequencer::patternBeats (int ci) const { auto* p = patternOf (ci); return p != nullptr ? p->getLengthBeats() : 4.0; }
double StepSequencer::repeatStartBeats (int ci, int repeat) const { return clipStartBeats (ci) - loopOffsetBeats (ci) + repeat * patternBeats (ci); }
int StepSequencer::firstRepeat (int ci) const { return (int) std::floor (loopOffsetBeats (ci) / patternBeats (ci) + 1.0e-9); }
void StepSequencer::repeatsInView (int ci, int& first, int& last) const
{
    const double len = patternBeats (ci);
    const double clipEnd = clipStartBeats (ci) + clipLengthBeats (ci);
    const double viewStart = juce::jmax (clipStartBeats (ci), beatAtX (gridBounds().getX()));
    const double viewEnd = juce::jmin (clipEnd, beatAtX (gridBounds().getRight()));
    first = juce::jmax (firstRepeat (ci), (int) std::floor ((viewStart - clipStartBeats (ci) + loopOffsetBeats (ci)) / len));
    last = (int) std::floor ((viewEnd - clipStartBeats (ci) + loopOffsetBeats (ci) - 1.0e-9) / len);
    if (! clipLoopsAt (ci)) last = juce::jmin (last, firstRepeat (ci));   // plays once
    if (last < first) last = first - 1;
}
int StepSequencer::padAt (int y) const
{
    const auto g = gridBounds();
    if (y < g.getY() || y >= g.getBottom()) return -1;
    return juce::jlimit (0, engine::DrumKit::numPads - 1, (y - g.getY()) * engine::DrumKit::numPads / juce::jmax (1, g.getHeight()));
}
int StepSequencer::stepAtTimelineBeat (double beat, int& repeat) const
{
    auto* p = getPattern();
    if (p == nullptr) return -1;
    const double rel = beat - clipStartBeats() + loopOffsetBeats();
    const int absStep = (int) std::floor (rel / stepBeats() + 1.0e-9);
    repeat = (int) std::floor ((double) absStep / p->numSteps);
    return ((absStep % p->numSteps) + p->numSteps) % p->numSteps;
}
StepSequencer::Cell StepSequencer::cellAt (juce::Point<int> pt) const
{
    Cell c;
    if (! gridBounds().contains (pt)) return c;
    const double beat = beatAtX (pt.x);
    c.clip = clipIndexAtBeat (beat);
    if (c.clip < 0) return c;
    auto* p = patternOf (c.clip);
    if (p == nullptr) return c;
    c.pad = padAt (pt.y);
    const double rel = beat - clipStartBeats (c.clip) + loopOffsetBeats (c.clip);
    const int absStep = (int) std::floor (rel / stepBeats (c.clip) + 1.0e-9);
    c.repeat = (int) std::floor ((double) absStep / p->numSteps);
    c.step = ((absStep % p->numSteps) + p->numSteps) % p->numSteps;
    return c;
}
juce::Rectangle<float> StepSequencer::cellRect (int ci, int pad, int step, int repeat) const
{
    const auto g = gridBounds().toFloat();
    const float rh = g.getHeight() / (float) engine::DrumKit::numPads;
    const double t0 = repeatStartBeats (ci, repeat) + step * stepBeats (ci);
    return { xForBeat (t0), g.getY() + pad * rh, xForBeat (t0 + stepBeats (ci)) - xForBeat (t0), rh };
}
int StepSequencer::insertionStep() const
{
    auto* clip = getClip();
    if (clip == nullptr) return 0;
    const double rel = transport.getPositionSeconds() - clip->getStartSeconds();
    if (rel >= 0.0 && rel < clip->getLengthSeconds()) { int r; return stepAtTimelineBeat (transport.secondsToBeats (transport.getPositionSeconds()), r); }
    int first = -1;
    for (const auto& c : selection) first = first < 0 ? c.step : juce::jmin (first, c.step);
    return juce::jmax (0, first);
}
juce::String StepSequencer::barBeatTick (double beat) const
{
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const int whole = (int) std::floor (beat + 1.0e-9);
    return juce::String (whole / bpb + 1) + "|" + juce::String (whole % bpb + 1) + "|" + juce::String ((int) std::round ((beat - whole) * 960.0)).paddedLeft ('0', 3);
}

//==============================================================================
// Editing

void StepSequencer::commit (engine::StepPattern updated, const juce::String& action)
{
    if (onPatternChanged) onPatternChanged (trackIndex, clipIndex, std::make_shared<const engine::StepPattern> (std::move (updated)), action);
    repaint();
}
void StepSequencer::liveCommit (engine::StepPattern updated, const juce::String& action)
{
    if (dragChanged) session.undo();
    dragChanged = true;
    commit (std::move (updated), action);
}
bool StepSequencer::unrollIfGhost (int repeat)
{
    if (! isGhost (repeat)) return false;
    if (clipLoops()) { unrollLoop(); return true; }
    if (auto* p = getPattern())   // not looping: the pattern grows to cover the clip
    {
        const int steps = (int) std::ceil ((clipLengthBeats() + loopOffsetBeats()) / stepBeats() - 1.0e-6);
        if (steps > p->numSteps) commit (StepEdits::resize (*p, steps), "Extend Pattern");
    }
    return true;
}
void StepSequencer::unrollLoop()
{
    auto* p = getPattern();
    if (p == nullptr) return;
    const int totalSteps = (int) std::ceil ((clipLengthBeats() + loopOffsetBeats()) / stepBeats() - 1.0e-6);
    if (totalSteps <= p->numSteps) return;
    commit (StepEdits::unroll (*p, totalSteps), "Unroll Loop");
    status ("Loop unrolled: " + juce::String (clipLengthBeats() / juce::jmax (1, transport.getBeatsPerBar()), 1) + " bars of steps, each editable on its own"
            + (totalSteps > engine::StepPattern::maxSteps ? " (pattern limit reached)" : ""));
}
bool StepSequencer::extendClipTo (double timelineBeat)
{
    auto* clip = getClip(); auto* p = getPattern();
    if (clip == nullptr || p == nullptr) return false;
    const double clipEnd = clipStartBeats() + clipLengthBeats();
    if (timelineBeat < clipEnd) return true;
    if (clipLengthBeats() > p->getLengthBeats() + 1.0e-6) unrollIfGhost (firstRepeat() + 1);
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const double newEndBeat = std::ceil ((timelineBeat + stepBeats()) / bpb - 1.0e-9) * bpb;   // grow to the next bar
    const auto newLength = (juce::int64) std::llround (transport.beatsToSeconds (newEndBeat - clipStartBeats()) * clip->sampleRate);
    if (onClipExtend) onClipExtend (trackIndex, clipIndex, newLength);
    if (auto* p2 = getPattern())
    {
        const int steps = (int) std::ceil ((newEndBeat - clipStartBeats() + loopOffsetBeats()) / stepBeats() - 1.0e-6);
        if (steps > p2->numSteps) commit (StepEdits::resize (*p2, steps), "Extend Clip");
    }
    return true;
}

//==============================================================================
// Commands

void StepSequencer::selectAll() { if (auto* p = getPattern()) { selection = StepEdits::all (*p); repaint(); } }
void StepSequencer::clearSelection() { selection.clear(); repaint(); }
void StepSequencer::deleteSelection()
{
    auto* p = getPattern();
    if (p == nullptr || selection.empty()) return;
    auto cells = selection; selection.clear();
    commit (StepEdits::clear (*p, cells), "Clear Steps");
}
void StepSequencer::copySelection()
{
    auto* p = getPattern();
    if (p == nullptr || selection.empty()) return;
    clipboard = StepEdits::copy (*p, selection);
    status ("Copied " + juce::String (clipboard.size()) + " step(s)");
}
void StepSequencer::cutSelection() { copySelection(); deleteSelection(); }
bool StepSequencer::canPaste() const { return ! clipboard.empty() && getPattern() != nullptr; }
void StepSequencer::pasteAtInsertion()
{
    auto* p = getPattern();
    if (p == nullptr || clipboard.empty()) return;
    StepEdits::Cells pasted;
    auto updated = StepEdits::paste (*p, clipboard, insertionStep(), &pasted);
    selection = pasted;
    commit (std::move (updated), "Paste Steps");
}
void StepSequencer::duplicateSelection()
{
    auto* p = getPattern();
    if (p == nullptr || selection.empty()) return;
    StepEdits::Cells pasted;
    auto updated = StepEdits::duplicate (*p, selection, &pasted);
    selection = pasted;
    commit (std::move (updated), "Duplicate Steps");
}
void StepSequencer::nudgeSelection (int steps)
{
    auto* p = getPattern();
    if (p == nullptr || selection.empty()) return;
    StepEdits::Cells moved;
    auto updated = StepEdits::shift (*p, selection, steps, 0, &moved);
    selection = moved;
    commit (std::move (updated), "Nudge Steps");
}
void StepSequencer::transposeSelection (int pads)
{
    auto* p = getPattern();
    if (p == nullptr || selection.empty()) return;
    StepEdits::Cells moved;
    auto updated = StepEdits::shift (*p, selection, 0, pads, &moved);
    selection = moved;
    commit (std::move (updated), "Move Steps to Pad");
    if (! moved.empty()) if (auto* t = getTrack(); t != nullptr && t->drumKit != nullptr) graph.triggerPadPreview (t->drumKit.get(), moved.front().pad, 0.8f);
}
void StepSequencer::changeVelocity (int delta)
{
    auto* p = getPattern();
    if (p == nullptr || selection.empty()) return;
    commit (StepEdits::changeVelocity (*p, selection, delta), "Change Velocity");
}
void StepSequencer::zoomBy (double factor)
{
    const auto v = view();
    const double centre = v.startSeconds + v.laneWidth / v.pixelsPerSecond / 2.0, pps = v.pixelsPerSecond * factor;
    applyView (centre - v.laneWidth / pps / 2.0, pps);
}
void StepSequencer::zoomToFit()
{
    auto* clip = getClip();
    if (clip == nullptr) return;
    applyView (clip->getStartSeconds(), view().laneWidth / juce::jmax (0.25, clip->getLengthSeconds()));
}
juce::String StepSequencer::describeSelection() const
{
    auto* p = getPattern(); auto* t = getTrack();
    if (p == nullptr) return "No drum editor target";
    const auto lit = StepEdits::all (*p);
    if (selection.empty()) return juce::String (lit.size()) + " hits, none selected";
    int s0 = p->numSteps, s1 = 0, p0 = 16, p1 = 0;
    for (const auto& c : selection) { s0 = juce::jmin (s0, c.step); s1 = juce::jmax (s1, c.step); p0 = juce::jmin (p0, c.pad); p1 = juce::jmax (p1, c.pad); }
    const double base = repeatStartBeats (firstRepeat());
    juce::String pads = t != nullptr && t->drumKit != nullptr ? t->drumKit->pads[(size_t) p0].name + (p1 != p0 ? " to " + t->drumKit->pads[(size_t) p1].name : juce::String()) : juce::String();
    if (selection.size() == 1) return pads + "  velocity " + juce::String ((int) p->get (selection[0].pad, selection[0].step)) + "  at " + barBeatTick (base + s0 * stepBeats());
    return juce::String (selection.size()) + " hits  " + pads + "  " + barBeatTick (base + s0 * stepBeats()) + " to " + barBeatTick (base + (s1 + 1) * stepBeats());
}

//==============================================================================
// Painting

void StepSequencer::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 3);
    header.removeFromLeft (4);
    linkButton.setBounds (header.removeFromLeft (46));
    header.removeFromLeft (4);
    loopButton.setBounds (header.removeFromLeft (48));
    header.removeFromLeft (4);
    unrollButton.setBounds (header.removeFromLeft (56));
}

void StepSequencer::paint (juce::Graphics& g)
{
    ui::UiProfiler::Scope profile ("paint StepSequencer");
    g.fillAll (theme::panelDark);
    auto* track = getTrack();
    auto* clip = getClip();
    auto* pattern = getPattern();
    if (track == nullptr || clip == nullptr || pattern == nullptr || track->drumKit == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Select a Drum Machine or Synth track to edit it", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const auto& kit = *track->drumKit;
    const auto grid = gridBounds();
    const auto vel = velocityBounds();
    const int beatsPerBar = juce::jmax (1, transport.getBeatsPerBar());
    const float rh = (float) grid.getHeight() / (float) engine::DrumKit::numPads;
    const int padsX = grid.getX() - padColumnWidth;
    const double clipStart = clipStartBeats(), clipEnd = clipStart + clipLengthBeats();
    const bool longer = clipLengthBeats() > pattern->getLengthBeats() + 1.0e-6;
    const bool loops = longer && clipLoops();
    unrollButton.setEnabled (loops);
    loopButton.setToggleState (clipLoops(), juce::dontSendNotification);
    const int clips = numClips();

    // Header
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (track->name + "  -  " + clip->name + (clips > 1 ? "  (clip " + juce::String (clipIndex + 1) + " of " + juce::String (clips) + ")" : juce::String()),
                getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (200).withWidth (300), juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (describeSelection() + "    " + EditSettings::toolName (tool()) + "  |  1/" + juce::String (pattern->stepsPerBeat * 4) + " steps"
                + (loops ? "  |  loop of " + juce::String (pattern->getLengthBeats() / beatsPerBar, 1) + " bars" : longer ? juce::String ("  |  plays once") : juce::String()),
                getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (400).withTrimmedRight (8), juce::Justification::centredRight, true);

    // Pad column and row shading
    for (int pad = 0; pad < engine::DrumKit::numPads; ++pad)
    {
        auto row = juce::Rectangle<float> ((float) padsX, grid.getY() + pad * rh, (float) padColumnWidth, rh);
        g.setColour (pad == dragPad ? theme::accent.withAlpha (0.35f) : pad == focusPad ? theme::panel.brighter (0.08f) : (pad % 2 == 0 ? theme::panel : theme::panelDark));
        g.fillRect (row);
        g.setColour (track->colour);
        g.fillRect (row.removeFromLeft (4.0f));
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (kit.pads[(size_t) pad].name, row.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
        g.setColour (pad % 2 == 0 ? theme::background.brighter (0.03f) : theme::background);
        g.fillRect (juce::Rectangle<float> ((float) grid.getX(), grid.getY() + pad * rh, (float) grid.getWidth(), rh));
    }

    g.saveState();
    g.reduceClipRegion (grid.getUnion (rulerBounds()).getUnion (vel));

    // Outside every clip: dimmed; other clips a little dimmer; clip names on the ruler
    const float cx0 = xForBeat (clipStart), cx1 = xForBeat (clipEnd);
    {
        g.setColour (theme::panelDark.withAlpha (0.55f));
        std::vector<std::pair<float, float>> spans;
        for (int ci = 0; ci < clips; ++ci) spans.emplace_back (xForBeat (clipStartBeats (ci)), xForBeat (clipStartBeats (ci) + clipLengthBeats (ci)));
        std::sort (spans.begin(), spans.end());
        float cursor = (float) grid.getX();
        for (const auto& [a, b] : spans) { if (a > cursor) g.fillRect (juce::Rectangle<float> (cursor, (float) grid.getY(), a - cursor, (float) (vel.getBottom() - grid.getY()))); cursor = juce::jmax (cursor, b); }
        if (cursor < (float) grid.getRight()) g.fillRect (juce::Rectangle<float> (cursor, (float) grid.getY(), (float) grid.getRight() - cursor, (float) (vel.getBottom() - grid.getY())));
        g.setColour (theme::panelDark.withAlpha (0.25f));
        for (int ci = 0; ci < clips; ++ci)
            if (ci != clipIndex) g.fillRect (juce::Rectangle<float> (xForBeat (clipStartBeats (ci)), (float) grid.getY(), xForBeat (clipStartBeats (ci) + clipLengthBeats (ci)) - xForBeat (clipStartBeats (ci)), (float) (vel.getBottom() - grid.getY())));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (int ci = 0; ci < clips; ++ci)
        {
            const float x = xForBeat (clipStartBeats (ci)), w = xForBeat (clipStartBeats (ci) + clipLengthBeats (ci)) - x;
            g.setColour (ci == clipIndex ? track->colour : track->colour.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (x, (float) headerHeight, w, 3.0f));
            g.setColour (ci == clipIndex ? theme::text : theme::textDim);
            g.drawText (clipAt (ci)->name, juce::Rectangle<int> ((int) x + 26, headerHeight + 3, juce::jmax (10, (int) w - 30), 12), juce::Justification::centredLeft, true);   // after the bar number
        }
    }

    int firstK, lastK; repeatsInView (firstK, lastK);
    const float stepPx = xForBeat (stepBeats()) - xForBeat (0.0);
    const auto clipArea = g.getClipBounds();
    for (int ci = 0; ci < clips; ++ci)
    {
    auto* cpat = patternOf (ci);
    if (cpat == nullptr) continue;
    const bool active = ci == clipIndex;
    const double cStart = clipStartBeats (ci), cEnd = cStart + clipLengthBeats (ci);
    const float cStepPx = xForBeat (stepBeats (ci)) - xForBeat (0.0);
    int cFirst, cLast; repeatsInView (ci, cFirst, cLast);
    // The empty grid: one line per step column and pad row (not an outline per cell)
    if (cStepPx > 5.0f)
    {
        g.setColour (theme::grid.withAlpha (active ? 0.7f : 0.4f));
        for (int k = cFirst; k <= cLast; ++k)
            for (int st = 0; st <= cpat->numSteps; ++st)
            {
                const double t = repeatStartBeats (ci, k) + st * stepBeats (ci);
                if (t < cStart - 1.0e-9 || t > cEnd + 1.0e-9) continue;
                const int x = (int) xForBeat (t);
                if (x < clipArea.getX() - 1 || x > clipArea.getRight() + 1) continue;
                g.drawVerticalLine (x, (float) grid.getY(), (float) grid.getBottom());
            }
        for (int pad = 1; pad < engine::DrumKit::numPads; ++pad)
            g.drawHorizontalLine ((int) (grid.getY() + pad * rh), juce::jmax (xForBeat (cStart), (float) grid.getX()), juce::jmin (xForBeat (cEnd), (float) grid.getRight()));
    }
    // Lit cells inside the clip, per repeat; only what intersects the area being repainted
    for (int k = cFirst; k <= cLast; ++k)
    {
        const bool ghost = k != firstRepeat (ci);
        for (int st = 0; st < cpat->numSteps; ++st)
        {
            const double t0 = repeatStartBeats (ci, k) + st * stepBeats (ci);
            if (t0 < cStart - 1.0e-9 || t0 >= cEnd - 1.0e-9) continue;
            const float x0 = xForBeat (t0);
            if (x0 + cStepPx < (float) clipArea.getX() || x0 > (float) clipArea.getRight()) continue;
            for (int pad = 0; pad < engine::DrumKit::numPads; ++pad)
            {
                const auto v = cpat->get (pad, st);
                if (v == 0) continue;
                const auto cell = cellRect (ci, pad, st, k).reduced (cStepPx > 8.0f ? 1.5f : 0.5f, 1.5f);
                const bool sel = active && StepEdits::contains (selection, { pad, st });
                const float alpha = (0.35f + 0.65f * (float) v / 127.0f) * (ghost ? 0.4f : 1.0f) * (active ? 1.0f : 0.7f);
                g.setColour (sel ? theme::accent.withAlpha (ghost ? 0.5f : 1.0f) : track->colour.withAlpha (alpha));
                if (cStepPx > 8.0f) g.fillRoundedRectangle (cell, 3.0f); else g.fillRect (cell);
                if (sel) { g.setColour (theme::text.withAlpha (0.8f)); g.drawRoundedRectangle (cell, 3.0f, 1.0f); }
            }
        }
    }
    }   // clips

    // Grid lines and ruler: beats and bars
    const double pxPerBeat = juce::jmax (1.0e-6, (double) view().pixelsPerSecond * transport.beatsToSeconds (1.0));
    const double lineStep = pxPerBeat >= 6.0 ? 1.0 : (double) beatsPerBar;
    const double firstBeat = std::floor (juce::jmax (0.0, beatAtX (grid.getX())) / lineStep) * lineStep;
    g.setFont (juce::FontOptions (10.0f));
    for (double beat = firstBeat; beat <= beatAtX (grid.getRight()) + 1.0e-9; beat += lineStep)
    {
        const float x = xForBeat (beat);
        const bool isBar = ((int) std::round (beat)) % beatsPerBar == 0;
        g.setColour (isBar ? theme::gridStrong.brighter (0.2f) : theme::gridStrong);
        g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        g.drawVerticalLine ((int) x, (float) vel.getY(), (float) vel.getBottom());
        if (isBar || pxPerBeat >= 40.0)
        {
            g.setColour (isBar ? theme::text : theme::textDim);
            const int bar = (int) std::round (beat) / beatsPerBar + 1, b = (int) std::round (beat) % beatsPerBar + 1;
            g.drawText (isBar ? juce::String (bar) : juce::String (bar) + "." + juce::String (b), (int) x + 3, headerHeight, 40, rulerHeight, juce::Justification::centredLeft);
        }
    }
    if (loops)
    {
        g.setColour (theme::accent.withAlpha (0.35f));
        for (int k = firstK; k <= lastK + 1; ++k) { const double t = repeatStartBeats (k); if (t > clipStart && t < clipEnd) g.drawVerticalLine ((int) xForBeat (t), (float) grid.getY(), (float) grid.getBottom()); }
    }
    else if (longer)
    {
        const float xe = xForBeat (repeatStartBeats (firstRepeat()) + pattern->getLengthBeats());
        if (xe < cx1) { g.setColour (theme::panelDark.withAlpha (0.35f)); g.fillRect (juce::Rectangle<float> (juce::jmax (xe, (float) grid.getX()), (float) grid.getY(), cx1 - juce::jmax (xe, (float) grid.getX()), (float) grid.getHeight())); }
    }

    // Velocity lane: the hits of the focus pad
    g.setColour (theme::panel);
    g.fillRect (vel);
    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (vel.getY(), (float) vel.getX(), (float) vel.getRight());
    g.setColour (theme::panelDark.withAlpha (0.55f));
    if (cx0 > (float) vel.getX()) g.fillRect (juce::Rectangle<float> ((float) vel.getX(), (float) vel.getY(), cx0 - (float) vel.getX(), (float) vel.getHeight()));
    if (cx1 < (float) vel.getRight()) g.fillRect (juce::Rectangle<float> (cx1, (float) vel.getY(), (float) vel.getRight() - cx1, (float) vel.getHeight()));
    for (int k = firstK; k <= lastK; ++k)
        for (int s = 0; s < pattern->numSteps; ++s)
        {
            const auto v = pattern->get (focusPad, s);
            const double t = repeatStartBeats (k) + s * stepBeats();
            if (v == 0 || t < clipStart || t >= clipEnd) continue;
            const float x = xForBeat (t) + 1.0f, h = (float) (vel.getHeight() - 6) * (float) v / 127.0f;
            const bool sel = StepEdits::contains (selection, { focusPad, s }), ghost = isGhost (k);
            g.setColour ((sel ? theme::accent : track->colour).withAlpha (ghost ? 0.35f : 0.85f));
            g.fillRect (juce::Rectangle<float> (x, (float) vel.getBottom() - 2.0f - h, juce::jmax (3.0f, juce::jmin (6.0f, stepPx - 2.0f)), h));
        }
    g.restoreState();
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (kit.pads[(size_t) focusPad].name + " velocity", juce::Rectangle<int> (padsX, vel.getY(), padColumnWidth - 6, vel.getHeight()), juce::Justification::centredRight, true);

    if (drag == Drag::band || drag == Drag::zoomBand)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        g.setColour (theme::accent.withAlpha (0.15f)); g.fillRect (r);
        g.setColour (theme::accent.withAlpha (0.8f)); g.drawRect (r, 1);
    }

    // Playhead
    {
        const float x = xForSeconds (transport.getPositionSeconds());
        if (x >= (float) grid.getX() && x <= (float) grid.getRight()) { g.setColour (theme::playhead.withAlpha (0.8f)); g.drawLine (x, (float) headerHeight, x, (float) getHeight(), 1.5f); }
    }

    g.setColour (theme::gridStrong);
    g.drawVerticalLine (grid.getX() - 1, (float) headerHeight, (float) getHeight());
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (headerHeight + rulerHeight - 1, (float) padsX, (float) getWidth());
    if (hasKeyboardFocus (false)) { g.setColour (theme::accent.withAlpha (0.9f)); g.drawRect (getLocalBounds(), 2); }
}

//==============================================================================
// Mouse

void StepSequencer::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto* track = getTrack(); auto* pattern = getPattern();
    if (track == nullptr || pattern == nullptr || e.y < headerHeight) return;
    dragStart = dragCurrent = e.getPosition();
    dragChanged = false;

    if (rulerBounds().contains (e.getPosition())) { transport.setPositionSeconds (juce::jmax (0.0, secondsAtX (e.x))); return; }
    if (e.x < originX())
    {
        const int pad = padAt (e.y);
        if (pad >= 0) { focusPad = pad; if (track->drumKit != nullptr) graph.triggerPadPreview (track->drumKit.get(), pad, 1.0f); repaint(); }
        return;
    }
    if (velocityBounds().contains (e.getPosition()))
    {
        int repeat; const int step = stepAtTimelineBeat (beatAtX (e.x), repeat);
        if (step < 0 || pattern->get (focusPad, step) == 0) return;
        if (! StepEdits::contains (selection, { focusPad, step })) selection = { { focusPad, step } };
        dragBase = *pattern; dragCells = selection; drag = Drag::velocity;
        mouseDrag (e);
        return;
    }
    const auto t = tool();
    if (t == Tool::zoomer)
    {
        if (! e.mods.isAnyModifierKeyDown() && ! e.mods.isPopupMenu()) { drag = Drag::zoomBand; return; }
        const auto v = view(); const double at = secondsAtX (e.x), pps = v.pixelsPerSecond * (e.mods.isAltDown() ? 0.5 : 2.0);
        applyView (at - (e.x - v.originX) / pps, pps);
        return;
    }
    const auto beat = beatAtX (e.x);
    const int pad = padAt (e.y);
    if (pad < 0) return;
    focusPad = pad;
    if (const int under = clipIndexAtBeat (beat); under >= 0 && under != clipIndex) setActiveClip (under);
    if (clipIndexAtBeat (beat) < 0 && t != Tool::selector)
    {
        // No clip here: past the active clip's end it grows; anywhere else a new one-bar clip is made
        const bool justPast = beat >= clipStartBeats() + clipLengthBeats() && (clipIndexAtBeat (beat) < 0) && beat < clipStartBeats() + clipLengthBeats() + juce::jmax (1.0, (double) transport.getBeatsPerBar());
        bool nextClipInTheWay = false;
        for (int ci = 0; ci < numClips(); ++ci) if (clipStartBeats (ci) > clipStartBeats() && clipStartBeats (ci) <= beat) nextClipInTheWay = true;
        if (justPast && ! nextClipInTheWay) { if (! extendClipTo (beat)) return; }
        else if (onClipCreate != nullptr && getClip() != nullptr)
        {
            const int bpb = juce::jmax (1, transport.getBeatsPerBar());
            const double barStart = std::floor (beat / bpb) * bpb;
            double barEnd = barStart + bpb;
            for (int ci = 0; ci < numClips(); ++ci) if (clipStartBeats (ci) > beat) barEnd = juce::jmin (barEnd, clipStartBeats (ci));
            const double sr = getClip()->sampleRate;
            const int created = onClipCreate (trackIndex, (juce::int64) std::llround (transport.beatsToSeconds (barStart) * sr), (juce::int64) std::llround (transport.beatsToSeconds (barEnd - barStart) * sr));
            if (created < 0) return;
            clipIndex = created; selection.clear();
        }
        else return;
        pattern = getPattern();
        if (pattern == nullptr) return;
    }
    auto cell = cellAt (e.getPosition());
    if (cell.step < 0 || cell.clip != clipIndex) return;
    if (t == Tool::selector) { if (! e.mods.isShiftDown() && ! e.mods.isCtrlDown()) clearSelection(); drag = Drag::band; return; }
    if (unrollIfGhost (cell.repeat)) { cell = cellAt (e.getPosition()); pattern = getPattern(); if (cell.step < 0 || pattern == nullptr) return; }
    const bool lit = pattern->get (cell.pad, cell.step) > 0;
    if (e.mods.isPopupMenu()) { if (lit) commit (StepEdits::clear (*pattern, { { cell.pad, cell.step } }), "Clear Step"); return; }
    if (e.mods.isCtrlDown() && lit)
    {
        auto it = std::find (selection.begin(), selection.end(), model::StepCell { cell.pad, cell.step });
        if (it != selection.end()) selection.erase (it); else selection.push_back ({ cell.pad, cell.step });
        repaint();
        return;
    }
    if (t == Tool::grabber && lit)
    {
        if (! StepEdits::contains (selection, { cell.pad, cell.step })) selection = { { cell.pad, cell.step } };
        dragBase = *pattern; dragCells = selection; dragAnchor = cell; drag = Drag::move;
        return;
    }
    // Toggle and paint (Smart, Pencil, Grabber on empty, Trimmer)
    paintValue = lit ? (std::uint8_t) 0 : (e.mods.isShiftDown() ? softVelocity : fullVelocity);
    dragBase = *pattern; drag = Drag::paint; lastPaintPad = lastPaintStep = -1;
    selection.clear();
    mouseDrag (e);
}

void StepSequencer::mouseDrag (const juce::MouseEvent& e)
{
    dragCurrent = e.getPosition();
    if (drag == Drag::none) return;
    if (drag == Drag::band || drag == Drag::zoomBand) { repaint(); return; }
    auto* pattern = getPattern();
    if (pattern == nullptr) return;
    if (drag == Drag::paint)
    {
        auto cell = cellAt (e.getPosition());
        if (cell.step < 0 || cell.clip != clipIndex || (cell.pad == lastPaintPad && cell.step == lastPaintStep)) return;
        if (isGhost (cell.repeat)) return;   // a stroke stays within the first pass; click a repeat to unroll first
        lastPaintPad = cell.pad; lastPaintStep = cell.step;
        if (dragChanged) session.undo();   // the whole stroke is one undo step: rebuild it from the base
        dragChanged = true;
        strokeCells.push_back ({ cell.pad, cell.step });
        auto out = dragBase;
        for (const auto& c : strokeCells) out.set (c.pad, c.step, paintValue);
        commit (std::move (out), paintValue > 0 ? "Paint Steps" : "Clear Steps");
        if (paintValue > 0) if (auto* t = getTrack(); t != nullptr && t->drumKit != nullptr) graph.triggerPadPreview (t->drumKit.get(), cell.pad, (float) paintValue / 127.0f);
        return;
    }
    if (drag == Drag::velocity)
    {
        const auto vel = velocityBounds();
        const int v = juce::jlimit (1, 127, juce::roundToInt (127.0 * (double) (vel.getBottom() - 2 - e.y) / (double) (vel.getHeight() - 6)));
        liveCommit (StepEdits::setVelocity (dragBase, dragCells, v), "Change Velocity");
        return;
    }
    if (drag == Drag::move)
    {
        const auto cell = cellAt (e.getPosition());
        if (cell.step < 0 || cell.clip != clipIndex) return;
        const int stepDelta = (cell.step + cell.repeat * pattern->numSteps) - (dragAnchor.step + dragAnchor.repeat * pattern->numSteps);
        const int padDelta = cell.pad - dragAnchor.pad;
        if (stepDelta == 0 && padDelta == 0) { if (dragChanged) { session.undo(); dragChanged = false; selection = dragCells; repaint(); } return; }
        StepEdits::Cells moved;
        auto updated = StepEdits::shift (dragBase, dragCells, stepDelta, padDelta, &moved);
        selection = moved;
        liveCommit (std::move (updated), "Move Steps");
    }
}

void StepSequencer::mouseUp (const juce::MouseEvent& e)
{
    const auto d = drag;
    drag = Drag::none;
    strokeCells.clear();
    auto* pattern = getPattern();
    if (pattern == nullptr) return;
    if (d == Drag::band)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        const int p0 = padAt (juce::jlimit (gridBounds().getY(), gridBounds().getBottom() - 1, r.getY())), p1 = padAt (juce::jlimit (gridBounds().getY(), gridBounds().getBottom() - 1, r.getBottom()));
        int first, last; repeatsInView (first, last);
        StepEdits::Cells found = (e.mods.isShiftDown() || e.mods.isCtrlDown()) ? selection : StepEdits::Cells{};
        for (int k = first; k <= last; ++k)
        {
            const int s0 = (int) std::floor ((beatAtX (r.getX()) - repeatStartBeats (k)) / stepBeats() + 1.0e-9), s1 = (int) std::floor ((beatAtX (r.getRight()) - repeatStartBeats (k)) / stepBeats() - 1.0e-9);
            for (const auto& c : StepEdits::inRegion (*pattern, s0, s1, p0, p1)) if (! StepEdits::contains (found, c)) found.push_back (c);
        }
        selection = found;
        repaint();
        return;
    }
    if (d == Drag::zoomBand)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        if (r.getWidth() > 4) { const double s0 = secondsAtX (r.getX()), s1 = secondsAtX (r.getRight()); applyView (s0, view().laneWidth / juce::jmax (0.01, s1 - s0)); }
        repaint();
        return;
    }
    dragChanged = false;
}

void StepSequencer::mouseMove (const juce::MouseEvent& e)
{
    const auto t = tool();
    if (t == Tool::zoomer) { setMouseCursor (juce::MouseCursor::CrosshairCursor); return; }
    if (velocityBounds().contains (e.getPosition())) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); return; }
    setMouseCursor (t == Tool::selector ? juce::MouseCursor::IBeamCursor : t == Tool::grabber ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void StepSequencer::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto v = view();
    if (e.mods.isCtrlDown()) { const double at = secondsAtX (e.x), pps = v.pixelsPerSecond * (w.deltaY > 0 ? 1.25 : 0.8); applyView (at - (e.x - v.originX) / pps, pps); return; }
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    applyView (v.startSeconds - d * (v.laneWidth / v.pixelsPerSecond) * 0.5, v.pixelsPerSecond);
}

//==============================================================================
// Keys

bool StepSequencer::keyPressed (const juce::KeyPress& key)
{
    if (getPattern() == nullptr) return false;
    using M = juce::ModifierKeys;
    const auto k = [&key] (int code, int mods = 0) { return key == juce::KeyPress (code, mods, 0); };
    if (k (juce::KeyPress::deleteKey) || k (juce::KeyPress::backspaceKey)) { deleteSelection(); return true; }
    if (k (juce::KeyPress::escapeKey)) { clearSelection(); return true; }
    if (k ('a', M::commandModifier)) { selectAll(); return true; }
    if (k ('c', M::commandModifier)) { copySelection(); return true; }
    if (k ('x', M::commandModifier)) { cutSelection(); return true; }
    if (k ('v', M::commandModifier)) { pasteAtInsertion(); return true; }
    if (k ('d', M::commandModifier)) { duplicateSelection(); return true; }
    if (k (juce::KeyPress::upKey)) { transposeSelection (-1); return true; }
    if (k (juce::KeyPress::downKey)) { transposeSelection (1); return true; }
    if (k (juce::KeyPress::upKey, M::commandModifier)) { changeVelocity (10); return true; }
    if (k (juce::KeyPress::downKey, M::commandModifier)) { changeVelocity (-10); return true; }
    if (k (juce::KeyPress::upKey, M::commandModifier | M::shiftModifier)) { changeVelocity (1); return true; }
    if (k (juce::KeyPress::downKey, M::commandModifier | M::shiftModifier)) { changeVelocity (-1); return true; }
    if (k (juce::KeyPress::leftKey) || k (',')) { nudgeSelection (-1); return true; }
    if (k (juce::KeyPress::rightKey) || k ('.')) { nudgeSelection (1); return true; }
    if (k ('z', M::altModifier)) { zoomToFit(); return true; }
    if (settings != nullptr && settings->commandsFocus && ! key.getModifiers().isAnyModifierKeyDown())
    {
        const auto c = key.getTextCharacter();
        if (c == 'r') { zoomBy (0.8); return true; }
        if (c == 't') { zoomBy (1.25); return true; }
        if (c == 'e') { zoomToFit(); return true; }
        if (c == 'h') { duplicateSelection(); return true; }
        if (c == 'x') { cutSelection(); return true; }
        if (c == 'c') { copySelection(); return true; }
        if (c == 'v') { pasteAtInsertion(); return true; }
    }
    return false;
}

void StepSequencer::timerCallback()
{
    // Repaint only what changed: the playhead's two columns, or everything when the linked view moved.
    const auto v = view();
    if (linked && (std::abs (v.startSeconds - lastViewStart) > 1.0e-9 || std::abs (v.pixelsPerSecond - lastViewPps) > 1.0e-9 || std::abs (v.originX - lastViewOrigin) > 0.5))
    {
        lastViewStart = v.startSeconds; lastViewPps = v.pixelsPerSecond; lastViewOrigin = v.originX;
        repaint();
        return;
    }
    const int x = (int) xForSeconds (transport.getPositionSeconds());
    if (x != lastPlayheadX)
    {
        if (lastPlayheadX >= 0) repaint (lastPlayheadX - 2, headerHeight, 5, getHeight() - headerHeight);
        repaint (x - 2, headerHeight, 5, getHeight() - headerHeight);
        lastPlayheadX = x;
    }
}

bool StepSequencer::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (getTrack() == nullptr) return false;
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg" || ext == ".mp3") return true;
    }
    return false;
}

void StepSequencer::filesDropped (const juce::StringArray& files, int, int y)
{
    const int pad = padAt (y);
    dragPad = -1;
    repaint();
    if (pad >= 0 && files.size() > 0 && onPadSampleDropped) onPadSampleDropped (trackIndex, pad, juce::File (files[0]));
}

} // namespace beatmaker::ui
