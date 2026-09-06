#include "PianoRoll.h"

namespace beatmaker::ui
{

using Tool = EditSettings::Tool;
using Mode = EditSettings::Mode;
using NoteEdits = model::NoteEdits;

PianoRoll::PianoRoll (model::Session& s, engine::Transport& t, engine::AudioGraph& g)
    : session (s), transport (t), graph (g)
{
    addAndMakeVisible (presetLabel);
    presetLabel.setColour (juce::Label::textColourId, theme::textDim);
    presetLabel.setFont (juce::FontOptions (12.0f));

    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        if (trackIndex >= 0 && presetBox.getSelectedId() > 0 && onPresetChanged)
            onPresetChanged (trackIndex, presetBox.getSelectedId() - 1);
    };

    addAndMakeVisible (linkButton);
    linkButton.setClickingTogglesState (true);
    linkButton.setToggleState (true, juce::dontSendNotification);
    linkButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.45f));
    linkButton.setTooltip ("Link the time axis to the tracks: the notes sit under their clip and scrolling or zooming either view moves both");
    linkButton.onClick = [this] { setLinked (linkButton.getToggleState()); };
    addAndMakeVisible (unrollButton);
    unrollButton.setTooltip ("Write the loop out so every bar of the clip can be edited on its own (happens by itself when you edit a repeat)");
    unrollButton.onClick = [this] { unrollLoop(); };

    addAndMakeVisible (rowsSmaller); addAndMakeVisible (rowsBigger);
    rowsSmaller.setTooltip ("Smaller rows: zoom out vertically (Alt+wheel)");
    rowsBigger.setTooltip ("Bigger rows: zoom in vertically (Alt+wheel)");
    rowsSmaller.onClick = [this] { setRowHeight (rowHeight - 2); };
    rowsBigger.onClick = [this] { setRowHeight (rowHeight + 2); };
    addAndMakeVisible (pitchScroll);
    pitchScroll.setAutoHide (false);
    pitchScroll.addListener (this);
    pitchScroll.setTitle ("Pitch range");

    setWantsKeyboardFocus (true);
    setTitle ("Note editor");
    startTimerHz (30);
}

void PianoRoll::setRowHeight (int pixels)
{
    pixels = juce::jlimit (6, 32, pixels);
    if (pixels == rowHeight) return;
    // Keep the middle of the visible range where it is
    const int middle = lowestPitch + visibleRows() / 2;
    rowHeight = pixels;
    scrollToPitch (middle - visibleRows() / 2);
    if (onRowHeightChanged) onRowHeightChanged (rowHeight);
    repaint();
}

void PianoRoll::scrollToPitch (int lowest)
{
    lowestPitch = juce::jlimit (0, juce::jmax (0, 128 - visibleRows()), lowest);
    syncScrollBar();
    repaint();
}

void PianoRoll::syncScrollBar()
{
    // The bar runs top (high pitches) to bottom (low pitches)
    pitchScroll.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
    pitchScroll.setCurrentRange (128.0 - lowestPitch - visibleRows(), visibleRows(), juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    lowestPitch = juce::jlimit (0, juce::jmax (0, 128 - visibleRows()), 128 - visibleRows() - juce::roundToInt (newRangeStart));
    repaint();
}

void PianoRoll::setLinked (bool on)
{
    if (linked == on) return;
    if (! on) { const auto v = view(); ownStartSeconds = v.startSeconds; ownPixelsPerSecond = v.pixelsPerSecond; }
    linked = on;
    linkButton.setToggleState (on, juce::dontSendNotification);
    status (on ? "Note editor follows the tracks' view" : "Note editor has its own view (Ctrl+wheel zoom, Shift+wheel scroll)");
    repaint();
}

void PianoRoll::setTarget (int newTrackIndex, int newClipIndex)
{
    const bool changed = newTrackIndex != trackIndex || newClipIndex != clipIndex;
    trackIndex = newTrackIndex;
    clipIndex = newClipIndex;
    if (changed) { selection.clear(); hoverNote = -1; ownPixelsPerSecond = 0.0; drag = Drag::none; }
    refreshPresetBox();
    repaint();
}

void PianoRoll::refreshPresetBox()
{
    auto* track = getTrack();
    if (track == nullptr || ! track->hasInstrument()) return;
    const auto type = track->instrumentType();
    const auto presets = engine::Instrument::presets (type);
    if (type != presetType)
    {
        presetType = type;
        presetBox.clear (juce::dontSendNotification);
        int id = 1;
        for (const auto& p : presets) presetBox.addItem (p.presetName, id++);
    }
    presetBox.setSelectedId (0, juce::dontSendNotification);
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].presetName == track->instrumentParams->presetName)
            presetBox.setSelectedId (i + 1, juce::dontSendNotification);
}

void PianoRoll::status (const juce::String& s) { if (onStatus) onStatus (s); }

//==============================================================================
// Model access and settings

const model::Track* PianoRoll::getTrack() const { return session.getTrack (trackIndex); }

const model::MidiClip* PianoRoll::getClip() const
{
    if (auto* track = getTrack())
        if (juce::isPositiveAndBelow (clipIndex, (int) track->midiClips.size()))
            return &track->midiClips[(size_t) clipIndex];
    return nullptr;
}

const engine::MidiSequence* PianoRoll::getSequence() const
{
    auto* clip = getClip();
    return clip != nullptr ? clip->sequence.get() : nullptr;
}

double PianoRoll::gridBeats() const noexcept { return settings != nullptr && settings->gridBeats > 0.0 ? settings->gridBeats : 0.25; }

double PianoRoll::snapDelta (double anchorStart, double delta) const noexcept
{
    if (! snapping()) return delta;
    if (settings != nullptr && settings->relativeGrid) return NoteEdits::snap (delta, gridBeats());
    return NoteEdits::snap (anchorStart + delta, gridBeats()) - anchorStart;
}

//==============================================================================
// Geometry: the time axis is the session timeline

PianoRoll::View PianoRoll::view() const
{
    View v;
    if (linked && viewSource != nullptr) v = viewSource();
    else
    {
        v.originX = juce::jmax (keyboardWidth + 8.0, linked ? keyboardWidth + 8.0 : 240.0);
        v.laneWidth = juce::jmax (50.0, (double) getWidth() - v.originX);
        v.startSeconds = ownStartSeconds; v.pixelsPerSecond = ownPixelsPerSecond;
        if (auto* clip = getClip(); clip != nullptr && v.pixelsPerSecond <= 0.0)
        {
            v.startSeconds = clip->getStartSeconds();
            v.pixelsPerSecond = v.laneWidth / juce::jmax (0.5, clip->getLengthSeconds());
        }
        if (v.pixelsPerSecond <= 0.0) v.pixelsPerSecond = 60.0;
    }
    v.originX = juce::jlimit ((double) keyboardWidth, juce::jmax ((double) keyboardWidth, getWidth() - 50.0), v.originX);
    v.laneWidth = juce::jmax (50.0, juce::jmin (v.laneWidth, getWidth() - v.originX));
    return v;
}

int PianoRoll::originX() const { return juce::roundToInt (view().originX); }
double PianoRoll::secondsAtX (int x) const { const auto v = view(); return v.startSeconds + (x - v.originX) / juce::jmax (1.0e-6, v.pixelsPerSecond); }
float PianoRoll::xForSeconds (double sec) const { const auto v = view(); return (float) (v.originX + (sec - v.startSeconds) * v.pixelsPerSecond); }
double PianoRoll::beatAtX (int x) const { return transport.secondsToBeats (secondsAtX (x)); }
float PianoRoll::xForBeat (double beat) const { return xForSeconds (transport.beatsToSeconds (beat)); }

void PianoRoll::applyView (double startSeconds, double pps)
{
    pps = juce::jlimit (5.0, 400000.0, pps);
    startSeconds = juce::jmax (0.0, startSeconds);
    if (linked) { if (onViewChanged) onViewChanged (startSeconds, pps); }
    else { ownStartSeconds = startSeconds; ownPixelsPerSecond = pps; }
    repaint();
}

juce::Rectangle<int> PianoRoll::gridBounds() const
{
    const int x = originX();
    return { x, headerHeight + rulerHeight, getWidth() - x - 12, getHeight() - headerHeight - rulerHeight - velocityHeight };   // 12: the pitch scrollbar
}
juce::Rectangle<int> PianoRoll::velocityBounds() const { const int x = originX(); return { x, getHeight() - velocityHeight, getWidth() - x, velocityHeight }; }
juce::Rectangle<int> PianoRoll::rulerBounds() const { const int x = originX(); return { x, headerHeight, getWidth() - x, rulerHeight }; }

int PianoRoll::visibleRows() const { return juce::jmax (1, gridBounds().getHeight() / rowHeight); }
double PianoRoll::sequenceLength() const { auto* seq = getSequence(); return seq != nullptr && seq->lengthBeats > 0.0 ? seq->lengthBeats : 8.0; }

int PianoRoll::pitchAtY (int y) const
{
    const auto g = gridBounds();
    if (y < g.getY() || y >= g.getBottom()) return -1;
    return juce::jlimit (0, 127, lowestPitch + (g.getBottom() - 1 - y) / rowHeight);
}
int PianoRoll::yForPitch (int pitch) const { return gridBounds().getBottom() - (pitch - lowestPitch + 1) * rowHeight; }

double PianoRoll::clipStartBeats() const { auto* c = getClip(); return c != nullptr ? transport.secondsToBeats (c->getStartSeconds()) : 0.0; }
double PianoRoll::clipLengthBeats() const { auto* c = getClip(); return c != nullptr ? transport.secondsToBeats (c->getLengthSeconds()) : sequenceLength(); }
double PianoRoll::loopOffsetBeats() const { auto* c = getClip(); return c != nullptr && c->sampleRate > 0.0 ? transport.secondsToBeats ((double) c->loopOffset / c->sampleRate) : 0.0; }
double PianoRoll::repeatStartBeats (int repeat) const { return clipStartBeats() - loopOffsetBeats() + repeat * sequenceLength(); }
int PianoRoll::firstRepeat() const { return (int) std::floor (loopOffsetBeats() / sequenceLength() + 1.0e-9); }
double PianoRoll::sequenceBeatAt (double timelineBeat) const
{
    const double len = sequenceLength();
    double s = std::fmod (timelineBeat - clipStartBeats() + loopOffsetBeats(), len);
    if (s < 0.0) s += len;
    return s;
}

void PianoRoll::repeatsInView (int& first, int& last) const
{
    const double clipEnd = clipStartBeats() + clipLengthBeats();
    const double viewStart = juce::jmax (clipStartBeats(), beatAtX (gridBounds().getX()));
    const double viewEnd = juce::jmin (clipEnd, beatAtX (gridBounds().getRight()));
    first = (int) std::floor ((viewStart - clipStartBeats() + loopOffsetBeats()) / sequenceLength());
    last = (int) std::floor ((viewEnd - clipStartBeats() + loopOffsetBeats() - 1.0e-9) / sequenceLength());
    first = juce::jmax (first, firstRepeat());
    if (last < first) last = first - 1;
}

juce::Rectangle<float> PianoRoll::instanceRect (const engine::NoteEvent& n, int repeat) const
{
    const double clipStart = clipStartBeats(), clipEnd = clipStart + clipLengthBeats();
    const double t0 = juce::jmax (clipStart, repeatStartBeats (repeat) + n.startBeat);
    const double t1 = juce::jmin (clipEnd, repeatStartBeats (repeat) + n.getEndBeat());
    const float x1 = xForBeat (t0), x2 = xForBeat (t1);
    return { x1, (float) yForPitch (n.pitch) + 1.0f, juce::jmax (3.0f, x2 - x1 - 1.0f), (float) rowHeight - 2.0f };
}

PianoRoll::Instance PianoRoll::instanceAt (juce::Point<int> p) const
{
    auto* seq = getSequence();
    if (seq == nullptr || ! gridBounds().contains (p)) return {};
    int first, last; repeatsInView (first, last);
    for (int k = first; k <= last; ++k)
        for (int i = (int) seq->notes.size() - 1; i >= 0; --i)
        {
            const auto& n = seq->notes[(size_t) i];
            if (repeatStartBeats (k) + n.startBeat >= clipStartBeats() + clipLengthBeats()) continue;
            if (repeatStartBeats (k) + n.getEndBeat() <= clipStartBeats()) continue;
            if (instanceRect (n, k).contains (p.toFloat())) return { i, k };
        }
    return {};
}

int PianoRoll::velocityBarAt (int x) const
{
    auto* seq = getSequence();
    if (seq == nullptr) return -1;
    int first, last; repeatsInView (first, last);
    int best = -1; float bestDist = 5.0f;
    for (int k = first; k <= last; ++k)
        for (int i = 0; i < (int) seq->notes.size(); ++i)
        {
            const double t = repeatStartBeats (k) + seq->notes[(size_t) i].startBeat;
            if (t < clipStartBeats() || t >= clipStartBeats() + clipLengthBeats()) continue;
            const float d = std::abs (xForBeat (t) + 2.0f - (float) x);
            if (d < bestDist || (d <= bestDist && isSelected (i))) { bestDist = d; best = i; }
        }
    return best;
}

juce::String PianoRoll::noteName (int pitch)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[pitch % 12]) + juce::String (pitch / 12 - 1);
}

juce::String PianoRoll::barBeatTick (double beat) const
{
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const int whole = (int) std::floor (beat + 1.0e-9);
    const int tick = (int) std::round ((beat - whole) * 960.0);
    return juce::String (whole / bpb + 1) + "|" + juce::String (whole % bpb + 1) + "|" + juce::String (tick).paddedLeft ('0', 3);
}

//==============================================================================
// Selection

std::vector<int> PianoRoll::selectedIndices() const
{
    auto* seq = getSequence();
    return seq != nullptr ? NoteEdits::resolve (*seq, selection) : std::vector<int>{};
}

bool PianoRoll::isSelected (int index) const
{
    auto* seq = getSequence();
    if (seq == nullptr || ! juce::isPositiveAndBelow (index, (int) seq->notes.size())) return false;
    for (const auto& k : selection) if (k.matches (seq->notes[(size_t) index])) return true;
    return false;
}

void PianoRoll::select (const std::vector<int>& indices, bool add)
{
    auto* seq = getSequence();
    if (seq == nullptr) return;
    if (! add) selection.clear();
    for (int i : indices) if (! isSelected (i)) selection.push_back ({ seq->notes[(size_t) i].pitch, seq->notes[(size_t) i].startBeat });
    repaint();
}

void PianoRoll::toggle (int index)
{
    auto* seq = getSequence();
    if (seq == nullptr || ! juce::isPositiveAndBelow (index, (int) seq->notes.size())) return;
    const auto& n = seq->notes[(size_t) index];
    for (size_t k = 0; k < selection.size(); ++k)
        if (selection[k].matches (n)) { selection.erase (selection.begin() + (long) k); repaint(); return; }
    selection.push_back ({ n.pitch, n.startBeat });
    repaint();
}

void PianoRoll::selectAll() { if (auto* seq = getSequence()) select (NoteEdits::all (*seq), false); }
void PianoRoll::clearSelection() { selection.clear(); repaint(); }

double PianoRoll::insertionBeat() const
{
    auto* clip = getClip();
    if (clip == nullptr) return 0.0;
    const double rel = transport.getPositionSeconds() - clip->getStartSeconds();
    if (rel >= 0.0 && rel < clip->getLengthSeconds()) return snapBeat (sequenceBeatAt (transport.secondsToBeats (transport.getPositionSeconds())));
    double first = -1.0;
    for (int i : selectedIndices()) { const double s = getSequence()->notes[(size_t) i].startBeat; if (first < 0.0 || s < first) first = s; }
    return first >= 0.0 ? first : 0.0;
}

juce::String PianoRoll::describeSelection() const
{
    auto* seq = getSequence();
    if (seq == nullptr) return "No note editor target";
    const auto idx = selectedIndices();
    if (idx.empty()) return juce::String (seq->notes.size()) + " notes, none selected";
    if (idx.size() == 1)
    {
        const auto& n = seq->notes[(size_t) idx[0]];
        return noteName (n.pitch) + "  velocity " + juce::String (n.velocity) + "  start " + barBeatTick (repeatStartBeats (firstRepeat()) + n.startBeat) + "  length " + juce::String (n.lengthBeats, 2) + " beats";
    }
    int lo = 127, hi = 0; double first = 1.0e9, last = 0.0;
    for (int i : idx) { const auto& n = seq->notes[(size_t) i]; lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); first = juce::jmin (first, n.startBeat); last = juce::jmax (last, n.getEndBeat()); }
    const double base = repeatStartBeats (firstRepeat());
    return juce::String (idx.size()) + " notes  " + noteName (lo) + " to " + noteName (hi) + "  " + barBeatTick (base + first) + " to " + barBeatTick (base + last);
}

//==============================================================================
// Painting

void PianoRoll::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 3);
    presetLabel.setBounds (header.removeFromLeft (44));
    presetBox.setBounds (header.removeFromLeft (160));
    header.removeFromLeft (8);
    linkButton.setBounds (header.removeFromLeft (46));
    header.removeFromLeft (4);
    unrollButton.setBounds (header.removeFromLeft (56));
    header.removeFromLeft (8);
    rowsSmaller.setBounds (header.removeFromLeft (22));
    header.removeFromLeft (2);
    rowsBigger.setBounds (header.removeFromLeft (22));
    const auto g = gridBounds();
    pitchScroll.setBounds (getWidth() - 12, g.getY(), 12, g.getHeight());
    scrollToPitch (lowestPitch);
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelDark);
    auto* track = getTrack();
    auto* clip = getClip();
    auto* seq = getSequence();
    if (track == nullptr || clip == nullptr || seq == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Select an instrument track to edit its notes", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const auto grid = gridBounds();
    const auto vel = velocityBounds();
    const int rows = visibleRows();
    const int beatsPerBar = juce::jmax (1, transport.getBeatsPerBar());
    const auto idx = selectedIndices();
    auto selectedHas = [&idx] (int i) { return std::find (idx.begin(), idx.end(), i) != idx.end(); };
    const int kb = grid.getX() - keyboardWidth;   // keyboard sits just left of the lanes
    const double clipStart = clipStartBeats(), clipEnd = clipStart + clipLengthBeats();
    const bool loops = clipLengthBeats() > sequenceLength() + 1.0e-6;
    unrollButton.setEnabled (loops);

    // Header: name and readout
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (track->name + "  -  " + clip->name, getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (400).withWidth (260), juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (describeSelection() + "    " + juce::String (EditSettings::modeName (mode())) + " | " + EditSettings::toolName (tool()) + " | grid " + juce::String (gridBeats(), 2)
                + (loops ? "  |  loop of " + juce::String (sequenceLength() / beatsPerBar, 1) + " bars" : juce::String()),
                getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (660).withTrimmedRight (8), juce::Justification::centredRight, true);

    // Left panel (under the track headers when linked)
    g.setColour (theme::panelDark);
    g.fillRect (0, headerHeight, kb, getHeight() - headerHeight);

    // Rows: keyboard + lane shading
    for (int r = 0; r < rows; ++r)
    {
        const int pitch = lowestPitch + r;
        if (pitch > 127) break;
        const int y = yForPitch (pitch);
        const bool black = isBlackKey (pitch);
        g.setColour (black ? theme::background : theme::background.brighter (0.04f));
        g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);
        auto key = juce::Rectangle<int> (kb, y, keyboardWidth - 1, rowHeight);
        g.setColour (black ? juce::Colour (0xff2a2d33) : juce::Colour (0xffd8dbe0));
        g.fillRect (key);
        if (pitch % 12 == 0)
        {
            g.setColour (black ? theme::text : theme::background);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("C" + juce::String (pitch / 12 - 1), key.reduced (4, 0), juce::Justification::centredRight);
            g.setColour (theme::gridStrong);
            g.drawHorizontalLine (y + rowHeight - 1, (float) grid.getX(), (float) grid.getRight());
        }
    }

    g.saveState();
    g.reduceClipRegion (grid.getUnion (rulerBounds()).getUnion (vel));

    // Outside the clip: dimmed
    g.setColour (theme::panelDark.withAlpha (0.55f));
    const float cx0 = xForBeat (clipStart), cx1 = xForBeat (clipEnd);
    if (cx0 > (float) grid.getX()) g.fillRect (juce::Rectangle<float> ((float) grid.getX(), (float) grid.getY(), cx0 - (float) grid.getX(), (float) (vel.getBottom() - grid.getY())));
    if (cx1 < (float) grid.getRight()) g.fillRect (juce::Rectangle<float> (cx1, (float) grid.getY(), (float) grid.getRight() - cx1, (float) (vel.getBottom() - grid.getY())));

    // Grid lines at the edit grid, beats and bars stronger; bar numbers on the ruler
    const double step = juce::jmin (gridBeats(), 1.0);
    const double pxPerBeat = juce::jmax (1.0e-6, (double) view().pixelsPerSecond * transport.beatsToSeconds (1.0));
    const double lineStep = pxPerBeat * step >= 6.0 ? step : pxPerBeat >= 6.0 ? 1.0 : (double) beatsPerBar;
    const double firstBeat = std::floor (juce::jmax (0.0, beatAtX (grid.getX())) / lineStep) * lineStep;
    const double lastBeat = beatAtX (grid.getRight());
    g.setFont (juce::FontOptions (10.0f));
    for (double beat = firstBeat; beat <= lastBeat + 1.0e-9; beat += lineStep)
    {
        const float x = xForBeat (beat);
        const bool isBeat = std::abs (beat - std::round (beat)) < 1e-9;
        const bool isBar = isBeat && ((int) std::round (beat)) % beatsPerBar == 0;
        g.setColour (isBar ? theme::gridStrong.brighter (0.2f) : isBeat ? theme::gridStrong : theme::grid.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        g.drawVerticalLine ((int) x, (float) vel.getY(), (float) vel.getBottom());
        if (isBeat && (isBar || pxPerBeat >= 40.0))
        {
            g.setColour (isBar ? theme::text : theme::textDim);
            const int bar = (int) std::round (beat) / beatsPerBar + 1, b = (int) std::round (beat) % beatsPerBar + 1;
            g.drawText (isBar ? juce::String (bar) : juce::String (bar) + "." + juce::String (b), (int) x + 3, headerHeight, 40, rulerHeight, juce::Justification::centredLeft);
        }
    }
    // Loop boundaries
    int firstK, lastK; repeatsInView (firstK, lastK);
    if (loops)
    {
        g.setColour (theme::accent.withAlpha (0.35f));
        for (int k = firstK; k <= lastK + 1; ++k)
        {
            const double t = repeatStartBeats (k);
            if (t > clipStart && t < clipEnd) { const float x = xForBeat (t); g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom()); }
        }
    }

    // Notes: every repeat inside the clip; repeats after the first are ghosts
    for (int k = firstK; k <= lastK; ++k)
    {
        const bool ghost = isGhost (k);
        for (int i = 0; i < (int) seq->notes.size(); ++i)
        {
            const auto& n = seq->notes[(size_t) i];
            if (n.pitch < lowestPitch || n.pitch >= lowestPitch + rows) continue;
            const double t0 = repeatStartBeats (k) + n.startBeat;
            if (t0 >= clipEnd || repeatStartBeats (k) + n.getEndBeat() <= clipStart) continue;
            auto r = instanceRect (n, k);
            const float alpha = (0.45f + 0.55f * (float) n.velocity / 127.0f) * (ghost ? 0.45f : 1.0f);
            const bool sel = selectedHas (i);
            g.setColour (sel ? theme::accent.withAlpha (ghost ? 0.5f : 1.0f) : track->colour.withAlpha (alpha));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (i == hoverNote ? theme::text : sel ? theme::text.withAlpha (0.7f) : track->colour.brighter (0.4f).withAlpha (ghost ? 0.4f : 1.0f));
            g.drawRoundedRectangle (r, 2.5f, 1.0f);
            if (r.getWidth() > 28.0f && rowHeight >= 12 && ! ghost)
            {
                g.setColour (sel ? theme::background : theme::text.withAlpha (0.8f));
                g.setFont (juce::FontOptions (9.5f));
                g.drawText (noteName (n.pitch), r.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, false);
            }
        }
    }

    // Velocity lane
    g.setColour (theme::panel);
    g.fillRect (vel);
    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (vel.getY(), (float) vel.getX(), (float) vel.getRight());
    g.setColour (theme::panelDark.withAlpha (0.55f));
    if (cx0 > (float) vel.getX()) g.fillRect (juce::Rectangle<float> ((float) vel.getX(), (float) vel.getY(), cx0 - (float) vel.getX(), (float) vel.getHeight()));
    if (cx1 < (float) vel.getRight()) g.fillRect (juce::Rectangle<float> (cx1, (float) vel.getY(), (float) vel.getRight() - cx1, (float) vel.getHeight()));
    for (int k = firstK; k <= lastK; ++k)
        for (int i = 0; i < (int) seq->notes.size(); ++i)
        {
            const auto& n = seq->notes[(size_t) i];
            const double t = repeatStartBeats (k) + n.startBeat;
            if (t < clipStart || t >= clipEnd) continue;
            const float x = xForBeat (t);
            if (x < (float) vel.getX() - 4.0f || x > (float) vel.getRight()) continue;
            const float h = (float) (vel.getHeight() - 6) * (float) n.velocity / 127.0f;
            const bool sel = selectedHas (i), ghost = isGhost (k);
            g.setColour ((sel ? theme::accent : track->colour).withAlpha (ghost ? 0.35f : 0.85f));
            g.fillRect (juce::Rectangle<float> (x, (float) vel.getBottom() - 2.0f - h, 4.0f, h));
            g.setColour ((sel ? theme::text : track->colour.brighter (0.5f)).withAlpha (ghost ? 0.35f : 1.0f));
            g.fillEllipse (x - 1.0f, (float) vel.getBottom() - 2.0f - h - 3.0f, 6.0f, 6.0f);
        }
    g.restoreState();
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText ("velocity", juce::Rectangle<int> (kb, vel.getY(), keyboardWidth - 4, vel.getHeight()), juce::Justification::centredRight);

    // Rubber band / zoom band
    if (drag == Drag::band || drag == Drag::zoomBand)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        g.setColour (theme::accent.withAlpha (0.15f)); g.fillRect (r);
        g.setColour (theme::accent.withAlpha (0.8f)); g.drawRect (r, 1);
    }

    // Playhead: the session position
    {
        const float x = xForSeconds (transport.getPositionSeconds());
        if (x >= (float) grid.getX() && x <= (float) grid.getRight())
        {
            g.setColour (theme::playhead.withAlpha (0.8f));
            g.drawLine (x, (float) headerHeight, x, (float) getHeight(), 1.5f);
        }
    }

    g.setColour (theme::gridStrong);
    g.drawVerticalLine (grid.getX() - 1, (float) headerHeight, (float) getHeight());
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (headerHeight + rulerHeight - 1, (float) kb, (float) getWidth());

    if (hasKeyboardFocus (false))
    {
        g.setColour (theme::accent.withAlpha (0.9f));
        g.drawRect (getLocalBounds(), 2);
    }
}

//==============================================================================
// Editing

void PianoRoll::commit (engine::MidiSequence updated, const juce::String& action)
{
    updated.sortNotes();
    if (onSequenceChanged)
        onSequenceChanged (trackIndex, clipIndex, std::make_shared<const engine::MidiSequence> (std::move (updated)), action);
}

void PianoRoll::apply (engine::MidiSequence updated, const juce::String& action, const std::vector<model::NoteKey>* keepKeys)
{
    if (keepKeys != nullptr) selection = *keepKeys;
    commit (std::move (updated), action);
    repaint();
}

void PianoRoll::liveCommit (engine::MidiSequence updated, const juce::String& action)
{
    if (dragChanged) session.undo();   // back to dragBase, then re-apply: one undo step for the whole drag
    dragChanged = true;
    commit (std::move (updated), action);
}

void PianoRoll::audition (int pitch, float velocity)
{
    if (auto* track = getTrack()) graph.triggerNotePreview (track->id, pitch, velocity, 0.3);
}

bool PianoRoll::unrollIfGhost (int repeat)
{
    if (! isGhost (repeat)) return false;
    unrollLoop();
    return true;
}

void PianoRoll::unrollLoop()
{
    auto* seq = getSequence(); auto* clip = getClip();
    if (seq == nullptr || clip == nullptr) return;
    const double clipStart = clipStartBeats(), clipLen = clipLengthBeats(), offset = loopOffsetBeats(), len = sequenceLength();
    if (clipLen <= len + 1.0e-6) return;
    engine::MidiSequence out;
    out.lengthBeats = clipLen + offset;   // repeat 0 still starts `offset` beats before the clip
    const int lastK = (int) std::floor ((clipLen + offset - 1.0e-9) / len);
    for (int k = firstRepeat(); k <= lastK; ++k)
        for (auto n : seq->notes)
        {
            const double t = repeatStartBeats (k) + n.startBeat;   // timeline
            if (t < clipStart || t >= clipStart + clipLen) continue;
            n.startBeat = t - clipStart + offset;
            n.lengthBeats = juce::jmin (n.lengthBeats, out.lengthBeats - n.startBeat);
            out.notes.push_back (n);
        }
    const auto keys = NoteEdits::keysOf (out, NoteEdits::resolve (out, selection));   // the first repeat keeps its selection
    selection = keys;
    commit (std::move (out), "Unroll Loop");
    status ("Loop unrolled: " + juce::String (clipLen / juce::jmax (1, transport.getBeatsPerBar()), 1) + " bars of notes, each editable on its own");
}

void PianoRoll::addNoteAt (int pitch, double timelineBeat, bool soft, bool startResize, juce::Point<int> at)
{
    auto* clip = getClip();
    if (getSequence() == nullptr || clip == nullptr) return;
    const double snappedT = snapBeat (timelineBeat);
    if (snappedT < clipStartBeats()) return;
    const double clipEnd = clipStartBeats() + clipLengthBeats();
    if (snappedT + juce::jmin (lastNoteLength, gridBeats()) > clipEnd + 1.0e-6)
    {
        // Past the clip: grow it to the next grid line after the note (unrolling first so the new bar is independent)
        if (clipLengthBeats() > sequenceLength() + 1.0e-6) unrollLoop();
        const double newEndBeat = NoteEdits::snap (snappedT + lastNoteLength + gridBeats() * 0.999, gridBeats());
        const auto newLength = (juce::int64) std::llround (transport.beatsToSeconds (newEndBeat - clipStartBeats()) * clip->sampleRate);
        if (onClipExtend) onClipExtend (trackIndex, clipIndex, newLength);
        if (auto* s2 = getSequence())
        {
            auto grown = *s2;
            grown.lengthBeats = juce::jmax (grown.lengthBeats, newEndBeat - clipStartBeats() + loopOffsetBeats());
            commit (std::move (grown), "Extend Clip");
        }
    }
    else
    {
        const int k = (int) std::floor ((snappedT - clipStartBeats() + loopOffsetBeats()) / sequenceLength());
        unrollIfGhost (k);
    }
    auto* seq = getSequence();
    if (seq == nullptr) return;
    engine::NoteEvent n;
    n.pitch = pitch;
    n.velocity = soft ? softVelocity : defaultVelocity;
    n.startBeat = juce::jlimit (0.0, juce::jmax (0.0, seq->lengthBeats - 1.0 / 64.0), sequenceBeatAt (snappedT));
    n.lengthBeats = juce::jmax (1.0 / 64.0, juce::jmin (lastNoteLength, seq->lengthBeats - n.startBeat));
    auto updated = *seq;
    updated.notes.push_back (n);
    audition (pitch, (float) n.velocity / 127.0f);
    selection = { { n.pitch, n.startBeat } };
    commit (std::move (updated), "Add Note");
    if (startResize)
    {
        if (auto* fresh = getSequence())
        {
            dragBase = *fresh;
            dragIndices = NoteEdits::resolve (*fresh, selection);
            dragAnchor = n; dragStart = at; drag = Drag::resizeEnd; dragChanged = false;
        }
    }
}

void PianoRoll::spotNote (int index)
{
    auto* seq = getSequence();
    if (seq == nullptr || ! juce::isPositiveAndBelow (index, (int) seq->notes.size())) return;
    const auto n = seq->notes[(size_t) index];
    auto* w = new juce::AlertWindow ("Spot Note", "Start as bar|beat|tick (960 ticks per beat) or beats, and length in beats.", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("start", barBeatTick (repeatStartBeats (firstRepeat()) + n.startBeat), "Start");
    w->addTextEditor ("length", juce::String (n.lengthBeats, 3), "Length (beats)");
    w->addButton ("Spot", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    const model::NoteKey key { n.pitch, n.startBeat };
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, key] (int result)
    {
        if (result != 1) return;
        auto* s = getSequence();
        if (s == nullptr) return;
        const auto idx = NoteEdits::resolve (*s, { key });
        if (idx.empty()) return;
        const auto text = w->getTextEditorContents ("start");
        double start;
        const auto parts = juce::StringArray::fromTokens (text, "|", {});
        if (parts.size() >= 2)
        {
            const int bpb = juce::jmax (1, transport.getBeatsPerBar());
            start = (parts[0].getIntValue() - 1) * bpb + (parts[1].getIntValue() - 1) + (parts.size() >= 3 ? parts[2].getIntValue() / 960.0 : 0.0);
        }
        else start = text.getDoubleValue();
        start -= repeatStartBeats (firstRepeat());   // timeline -> sequence
        auto updated = *s;
        auto& note = updated.notes[(size_t) idx[0]];
        note.startBeat = juce::jlimit (0.0, juce::jmax (0.0, updated.lengthBeats - 1.0 / 64.0), start);
        const double len = w->getTextEditorContents ("length").getDoubleValue();
        if (len > 0.0) note.lengthBeats = len;
        note.lengthBeats = juce::jlimit (1.0 / 64.0, updated.lengthBeats - note.startBeat, note.lengthBeats);
        selection = { { note.pitch, note.startBeat } };
        apply (std::move (updated), "Spot Note");
    }), true);
}

//==============================================================================
// Commands

void PianoRoll::deleteSelection()
{
    auto* seq = getSequence(); const auto idx = selectedIndices();
    if (seq == nullptr || idx.empty()) return;
    selection.clear();
    apply (NoteEdits::remove (*seq, idx), idx.size() == 1 ? "Delete Note" : "Delete Notes");
}
void PianoRoll::copySelection()
{
    auto* seq = getSequence(); const auto idx = selectedIndices();
    if (seq == nullptr || idx.empty()) return;
    clipboard = NoteEdits::copy (*seq, idx);
    status ("Copied " + juce::String (clipboard.size()) + " note(s)");
}
void PianoRoll::cutSelection() { copySelection(); deleteSelection(); }
bool PianoRoll::canPaste() const { return ! clipboard.empty() && getSequence() != nullptr; }
void PianoRoll::pasteAtInsertion()
{
    auto* seq = getSequence();
    if (seq == nullptr || clipboard.empty()) return;
    std::vector<model::NoteKey> keys;
    auto updated = NoteEdits::paste (*seq, clipboard, insertionBeat(), &keys);
    apply (std::move (updated), "Paste Notes", &keys);
}
void PianoRoll::duplicateSelection()
{
    auto* seq = getSequence(); const auto idx = selectedIndices();
    if (seq == nullptr || idx.empty()) return;
    std::vector<model::NoteKey> keys;
    auto updated = NoteEdits::duplicate (*seq, idx, &keys);
    apply (std::move (updated), "Duplicate Notes", &keys);
}
void PianoRoll::quantizeSelection (bool lengthsToo)
{
    auto* seq = getSequence(); auto idx = selectedIndices();
    if (seq == nullptr) return;
    if (idx.empty()) idx = NoteEdits::all (*seq);
    auto updated = NoteEdits::quantize (*seq, idx, gridBeats(), 1.0f, lengthsToo);
    const auto keys = NoteEdits::keysOf (updated, NoteEdits::all (updated));   // re-resolve by the quantized positions
    std::vector<model::NoteKey> keep;
    for (int i : idx) { auto n = seq->notes[(size_t) i]; n.startBeat = NoteEdits::snap (n.startBeat, gridBeats()); keep.push_back ({ n.pitch, juce::jlimit (0.0, updated.lengthBeats, n.startBeat) }); }
    apply (std::move (updated), lengthsToo ? "Quantize Notes and Lengths" : "Quantize Notes", &keep);
    status ("Quantized " + juce::String (idx.size()) + " note(s) to " + juce::String (gridBeats(), 2) + " beats");
    juce::ignoreUnused (keys);
}
void PianoRoll::transposeSelection (int semis)
{
    auto* seq = getSequence(); const auto idx = selectedIndices();
    if (seq == nullptr || idx.empty()) return;
    std::vector<model::NoteKey> keep;
    for (int i : idx) keep.push_back ({ juce::jlimit (0, 127, seq->notes[(size_t) i].pitch + semis), seq->notes[(size_t) i].startBeat });
    apply (NoteEdits::transpose (*seq, idx, semis), "Transpose Notes", &keep);
    if (idx.size() == 1) audition (keep[0].pitch, (float) seq->notes[(size_t) idx[0]].velocity / 127.0f);
    if (keep[0].pitch < lowestPitch || keep[0].pitch >= lowestPitch + visibleRows()) scrollToPitch (keep[0].pitch - visibleRows() / 2);
}
void PianoRoll::nudgeBeats (double beats)
{
    auto* seq = getSequence(); const auto idx = selectedIndices();
    if (seq == nullptr || idx.empty()) return;
    auto updated = NoteEdits::nudge (*seq, idx, beats);
    // The group moved by one clamped delta: find it from the first note.
    const double delta = updated.notes.empty() ? 0.0 : [&]
    {
        const auto& before = seq->notes[(size_t) idx[0]];
        for (const auto& n : updated.notes) if (n.pitch == before.pitch && ! engine::NoteEvent::sameBeat (n.startBeat, before.startBeat) && std::abs ((n.startBeat - before.startBeat) - beats) < 1.0e-6 + std::abs (beats)) return n.startBeat - before.startBeat;
        return 0.0;
    }();
    std::vector<model::NoteKey> keep;
    for (int i : idx) keep.push_back ({ seq->notes[(size_t) i].pitch, seq->notes[(size_t) i].startBeat + delta });
    apply (std::move (updated), "Nudge Notes", &keep);
}
void PianoRoll::changeVelocity (int delta)
{
    auto* seq = getSequence(); const auto idx = selectedIndices();
    if (seq == nullptr || idx.empty()) return;
    apply (NoteEdits::changeVelocity (*seq, idx, delta), "Change Velocity");
}
void PianoRoll::legatoSelection()
{
    auto* seq = getSequence(); auto idx = selectedIndices();
    if (seq == nullptr) return;
    if (idx.empty()) idx = NoteEdits::all (*seq);
    apply (NoteEdits::legato (*seq, idx), "Legato");
}
void PianoRoll::selectNextNote (bool forward)
{
    auto* seq = getSequence();
    if (seq == nullptr || seq->notes.empty()) return;
    const auto idx = selectedIndices();
    int next = forward ? 0 : (int) seq->notes.size() - 1;
    if (! idx.empty()) next = juce::jlimit (0, (int) seq->notes.size() - 1, (forward ? *std::max_element (idx.begin(), idx.end()) + 1 : *std::min_element (idx.begin(), idx.end()) - 1));
    select ({ next }, false);
    const auto& n = seq->notes[(size_t) next];
    audition (n.pitch, (float) n.velocity / 127.0f);
    const double t = transport.beatsToSeconds (repeatStartBeats (firstRepeat()) + n.startBeat);
    const auto v = view();
    if (t < v.startSeconds || t > v.startSeconds + v.laneWidth / v.pixelsPerSecond) applyView (t, v.pixelsPerSecond);
    if (n.pitch < lowestPitch || n.pitch >= lowestPitch + visibleRows()) scrollToPitch (n.pitch - visibleRows() / 2);
    repaint();
}
void PianoRoll::zoomBy (double factor)
{
    const auto v = view();
    const double centre = v.startSeconds + v.laneWidth / v.pixelsPerSecond / 2.0;
    const double pps = v.pixelsPerSecond * factor;
    applyView (centre - v.laneWidth / pps / 2.0, pps);
}
void PianoRoll::zoomToFit()
{
    auto* clip = getClip();
    if (clip == nullptr) return;
    const auto v = view();
    applyView (clip->getStartSeconds(), v.laneWidth / juce::jmax (0.25, clip->getLengthSeconds()));
}

//==============================================================================
// Mouse

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto* seq = getSequence();
    if (seq == nullptr || e.y < headerHeight) return;
    dragStart = dragCurrent = e.getPosition();
    dragChanged = false;

    if (rulerBounds().contains (e.getPosition()))
    {
        transport.setPositionSeconds (juce::jmax (0.0, transport.beatsToSeconds (snapBeat (beatAtX (e.x)))));
        return;
    }
    if (e.x < originX())
    {
        if (const int pitch = pitchAtY (e.y); pitch >= 0) { audition (pitch); draggingKeyboard = true; keyboardDragStartPitch = lowestPitch; }
        return;
    }
    if (velocityBounds().contains (e.getPosition()))
    {
        velocityTarget = velocityBarAt (e.x);
        if (velocityTarget < 0) return;
        if (! isSelected (velocityTarget)) select ({ velocityTarget }, e.mods.isShiftDown());
        dragBase = *seq; dragIndices = selectedIndices(); drag = Drag::velocity;
        mouseDrag (e);
        return;
    }
    const int pitch = pitchAtY (e.y);
    if (pitch < 0) return;
    const auto inst = instanceAt (e.getPosition());
    int hit = inst.note;
    const auto t = tool();

    if (t == Tool::zoomer)
    {
        if (! e.mods.isAnyModifierKeyDown() && ! e.mods.isPopupMenu()) { drag = Drag::zoomBand; return; }
        const auto v = view();
        const double at = secondsAtX (e.x);
        const double pps = v.pixelsPerSecond * (e.mods.isAltDown() ? 0.5 : 2.0);
        applyView (at - (e.x - v.originX) / pps, pps);
        return;
    }
    if (hit >= 0 && mode() != Mode::spot && unrollIfGhost (inst.repeat))
    {
        // The loop was written out; the note under the mouse is now its own
        const auto again = instanceAt (e.getPosition());
        hit = again.note;
        if (hit < 0) return;
        seq = getSequence();
    }
    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            if (! isSelected (hit)) select ({ hit }, false);
            deleteSelection();
        }
        return;
    }
    if (hit >= 0)
    {
        if (mode() == Mode::spot) { select ({ hit }, false); spotNote (hit); return; }
        if (e.mods.isCtrlDown() && ! e.mods.isShiftDown()) { toggle (hit); return; }
        if (! isSelected (hit)) select ({ hit }, e.mods.isShiftDown());
        const auto& n = seq->notes[(size_t) hit];
        const auto r = instanceRect (n, inst.repeat);
        const bool nearEnd = (r.getRight() - e.x) < edgeGrab, nearStart = (e.x - r.getX()) < edgeGrab && r.getWidth() > 2 * edgeGrab;
        if (t == Tool::trimmer) drag = (e.x - r.getX()) < (r.getRight() - e.x) ? Drag::resizeStart : Drag::resizeEnd;
        else if (t == Tool::selector || t == Tool::scrubber) drag = Drag::band;
        else drag = nearEnd ? Drag::resizeEnd : nearStart ? Drag::resizeStart : Drag::move;
        dragBase = *seq; dragIndices = selectedIndices(); dragAnchor = n; dragRepeat = inst.repeat;
        if (drag != Drag::band) audition (n.pitch, (float) n.velocity / 127.0f);
        repaint();
        return;
    }
    // Empty space
    if (t == Tool::pencil) { addNoteAt (pitch, beatAtX (e.x), e.mods.isShiftDown(), true, e.getPosition()); return; }
    if (t == Tool::smart) { drag = Drag::pending; if (! e.mods.isShiftDown()) clearSelection(); return; }
    if (! e.mods.isShiftDown()) clearSelection();
    drag = Drag::band;
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    dragCurrent = e.getPosition();
    if (draggingKeyboard) { scrollToPitch (keyboardDragStartPitch + e.getDistanceFromDragStartY() / juce::jmax (1, rowHeight)); return; }   // drag the keyboard to scroll
    if (drag == Drag::none) return;
    if (drag == Drag::pending)
    {
        if (e.getDistanceFromDragStart() < 4) return;
        drag = Drag::band;
    }
    if (drag == Drag::band || drag == Drag::zoomBand) { repaint(); return; }

    if (drag == Drag::velocity)
    {
        const auto vel = velocityBounds();
        const int v = juce::jlimit (1, 127, juce::roundToInt (127.0 * (double) (vel.getBottom() - 2 - e.y) / (double) (vel.getHeight() - 6)));
        auto indices = dragIndices;
        if (tool() == Tool::pencil) { const int under = velocityBarAt (e.x); if (under >= 0 && std::find (indices.begin(), indices.end(), under) == indices.end()) indices.push_back (under); }
        liveCommit (NoteEdits::setVelocity (dragBase, indices, v), "Change Velocity");
        return;
    }

    auto* seq = getSequence();
    if (seq == nullptr || dragIndices.empty()) return;
    const double rawDelta = beatAtX (e.x) - beatAtX (dragStart.x);
    engine::MidiSequence updated = dragBase;
    const double length = dragBase.lengthBeats;
    std::vector<model::NoteKey> keys;

    if (drag == Drag::move)
    {
        double delta = snapDelta (dragAnchor.startBeat, rawDelta);
        int pitchDelta = (dragStart.y - e.y) / rowHeight;
        for (int i : dragIndices) { const auto& n = dragBase.notes[(size_t) i]; delta = juce::jlimit (-n.startBeat, juce::jmax (0.0, length - n.getEndBeat()), delta); pitchDelta = juce::jlimit (-n.pitch, 127 - n.pitch, pitchDelta); }
        for (int i : dragIndices) { auto& n = updated.notes[(size_t) i]; n.startBeat += delta; n.pitch += pitchDelta; keys.push_back ({ n.pitch, n.startBeat }); }
        if (pitchDelta != 0 && dragAnchor.pitch + pitchDelta != (dragChanged ? seq->notes[(size_t) juce::jmin ((int) seq->notes.size() - 1, dragIndices[0])].pitch : dragAnchor.pitch))
            audition (dragAnchor.pitch + pitchDelta, (float) dragAnchor.velocity / 127.0f);
    }
    else if (drag == Drag::resizeEnd)
    {
        const double delta = snapDelta (dragAnchor.getEndBeat(), rawDelta);
        for (int i : dragIndices) { auto& n = updated.notes[(size_t) i]; n.lengthBeats = juce::jlimit (snapping() ? gridBeats() : 1.0 / 64.0, length - n.startBeat, n.lengthBeats + delta); keys.push_back ({ n.pitch, n.startBeat }); }
    }
    else if (drag == Drag::resizeStart)
    {
        const double delta = snapDelta (dragAnchor.startBeat, rawDelta);
        for (int i : dragIndices)
        {
            auto& n = updated.notes[(size_t) i];
            const double end = n.getEndBeat();
            n.startBeat = juce::jlimit (0.0, end - (snapping() ? gridBeats() : 1.0 / 64.0), n.startBeat + delta);
            n.lengthBeats = end - n.startBeat;
            keys.push_back ({ n.pitch, n.startBeat });
        }
    }
    bool same = true;
    for (int i : dragIndices) if (! updated.notes[(size_t) i].samePlacement (dragBase.notes[(size_t) i])) { same = false; break; }
    if (same && dragChanged) { session.undo(); dragChanged = false; selection = NoteEdits::keysOf (dragBase, dragIndices); repaint(); return; }
    if (same) return;
    selection = keys;
    liveCommit (std::move (updated), drag == Drag::move ? "Move Notes" : "Resize Notes");
}

void PianoRoll::mouseUp (const juce::MouseEvent& e)
{
    draggingKeyboard = false;
    auto* seq = getSequence();
    const auto d = drag;
    drag = Drag::none;
    if (seq == nullptr) return;
    if (d == Drag::pending)
    {
        if (const int pitch = pitchAtY (dragStart.y); pitch >= 0) addNoteAt (pitch, beatAtX (dragStart.x), e.mods.isShiftDown(), false, dragStart);
        drag = Drag::none;
        return;
    }
    if (d == Drag::band)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        if (r.getWidth() > 2 || r.getHeight() > 2)
        {
            const int p0 = pitchAtY (juce::jlimit (gridBounds().getY(), gridBounds().getBottom() - 1, r.getBottom())), p1 = pitchAtY (juce::jlimit (gridBounds().getY(), gridBounds().getBottom() - 1, r.getY()));
            // Timeline range -> sequence beats, per repeat it touches
            int first, last; repeatsInView (first, last);
            std::vector<int> found;
            const double b0 = beatAtX (r.getX()), b1 = beatAtX (r.getRight());
            for (int k = first; k <= last; ++k)
                for (int i : NoteEdits::inRegion (*seq, b0 - repeatStartBeats (k), b1 - repeatStartBeats (k), p0, p1))
                    if (std::find (found.begin(), found.end(), i) == found.end()) found.push_back (i);
            select (found, e.mods.isShiftDown() || e.mods.isCtrlDown());
        }
        repaint();
        return;
    }
    if (d == Drag::zoomBand)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        if (r.getWidth() > 4)
        {
            const double s0 = secondsAtX (r.getX()), s1 = secondsAtX (r.getRight());
            applyView (s0, view().laneWidth / juce::jmax (0.01, s1 - s0));
        }
        repaint();
        return;
    }
    if (d == Drag::resizeEnd && ! selection.empty())
        for (int i : selectedIndices()) { lastNoteLength = seq->notes[(size_t) i].lengthBeats; break; }
    dragChanged = false;
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = noteAt (e.getPosition());
    if (hit >= 0) { select ({ hit }, false); spotNote (hit); }
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const int hit = noteAt (e.getPosition());
    if (hit != hoverNote) { hoverNote = hit; repaint(); }
    const auto t = tool();
    if (t == Tool::zoomer) { setMouseCursor (juce::MouseCursor::CrosshairCursor); return; }
    if (velocityBounds().contains (e.getPosition())) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); return; }
    if (hit >= 0)
    {
        auto* seq = getSequence();
        const auto r = instanceRect (seq->notes[(size_t) hit], instanceAt (e.getPosition()).repeat);
        const bool edge = (r.getRight() - e.x) < edgeGrab || ((e.x - r.getX()) < edgeGrab && r.getWidth() > 2 * edgeGrab);
        setMouseCursor (t == Tool::trimmer || (edge && (t == Tool::smart || t == Tool::grabber || t == Tool::pencil)) ? juce::MouseCursor::LeftRightResizeCursor
                        : t == Tool::selector ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::DraggingHandCursor);
        return;
    }
    setMouseCursor (t == Tool::selector ? juce::MouseCursor::IBeamCursor : t == Tool::pencil ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isAltDown())
    {
        // Vertical zoom around the pitch under the mouse
        const int pitchUnderMouse = juce::jmax (0, pitchAtY (e.y));
        const int rowsAbove = pitchAtY (e.y) >= 0 ? pitchUnderMouse - lowestPitch : visibleRows() / 2;
        rowHeight = juce::jlimit (6, 32, rowHeight + (w.deltaY > 0 ? 2 : -2));
        scrollToPitch (pitchUnderMouse - rowsAbove);
        if (onRowHeightChanged) onRowHeightChanged (rowHeight);
        return;
    }
    if (e.mods.isCtrlDown())
    {
        const auto v = view();
        const double at = secondsAtX (e.x), pps = v.pixelsPerSecond * (w.deltaY > 0 ? 1.25 : 0.8);
        applyView (at - (e.x - v.originX) / pps, pps);
        return;
    }
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        const auto v = view();
        applyView (v.startSeconds - d * (v.laneWidth / v.pixelsPerSecond) * 0.5, v.pixelsPerSecond);
        return;
    }
    const int step = w.deltaY > 0 ? 2 : w.deltaY < 0 ? -2 : 0;
    scrollToPitch (lowestPitch + step);
}

//==============================================================================
// Keys: the track editor's vocabulary, applied to notes

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    auto* seq = getSequence();
    if (seq == nullptr) return false;
    using M = juce::ModifierKeys;
    const auto k = [&key] (int code, int mods = 0) { return key == juce::KeyPress (code, mods, 0); };
    const double grid = gridBeats();

    if (k (juce::KeyPress::deleteKey) || k (juce::KeyPress::backspaceKey)) { deleteSelection(); return true; }
    if (k (juce::KeyPress::escapeKey)) { clearSelection(); return true; }
    if (k ('a', M::commandModifier)) { selectAll(); return true; }
    if (k ('c', M::commandModifier)) { copySelection(); return true; }
    if (k ('x', M::commandModifier)) { cutSelection(); return true; }
    if (k ('v', M::commandModifier)) { pasteAtInsertion(); return true; }
    if (k ('d', M::commandModifier)) { duplicateSelection(); return true; }
    if (k ('l', M::commandModifier)) { legatoSelection(); return true; }
    if (k ('q', M::altModifier)) { quantizeSelection (false); return true; }
    if (k ('q', M::altModifier | M::shiftModifier)) { quantizeSelection (true); return true; }
    if (k (juce::KeyPress::upKey)) { transposeSelection (1); return true; }
    if (k (juce::KeyPress::downKey)) { transposeSelection (-1); return true; }
    if (k (juce::KeyPress::upKey, M::shiftModifier)) { transposeSelection (12); return true; }
    if (k (juce::KeyPress::downKey, M::shiftModifier)) { transposeSelection (-12); return true; }
    if (k (juce::KeyPress::upKey, M::commandModifier)) { changeVelocity (10); return true; }
    if (k (juce::KeyPress::downKey, M::commandModifier)) { changeVelocity (-10); return true; }
    if (k (juce::KeyPress::upKey, M::commandModifier | M::shiftModifier)) { changeVelocity (1); return true; }
    if (k (juce::KeyPress::downKey, M::commandModifier | M::shiftModifier)) { changeVelocity (-1); return true; }
    if (k (juce::KeyPress::leftKey) || k (',')) { nudgeBeats (-grid); return true; }
    if (k (juce::KeyPress::rightKey) || k ('.')) { nudgeBeats (grid); return true; }
    if (k (juce::KeyPress::leftKey, M::shiftModifier)) { nudgeBeats (-grid / 4.0); return true; }
    if (k (juce::KeyPress::rightKey, M::shiftModifier)) { nudgeBeats (grid / 4.0); return true; }
    if (k (juce::KeyPress::tabKey)) { selectNextNote (true); return true; }
    if (k (juce::KeyPress::tabKey, M::shiftModifier)) { selectNextNote (false); return true; }
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
        if (c == 'q') { quantizeSelection (false); return true; }
    }
    return false;
}

void PianoRoll::timerCallback()
{
    auto* clip = getClip();
    auto* seq = getSequence();
    if (clip == nullptr || seq == nullptr) return;
    juce::ignoreUnused (clip, seq);
    const int x = (int) xForSeconds (transport.getPositionSeconds());
    if (x != lastPlayheadX) { lastPlayheadX = x; repaint(); }
    if (linked) repaint();   // the tracks may have scrolled
}

} // namespace beatmaker::ui
