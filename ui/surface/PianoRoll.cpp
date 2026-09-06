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

    setWantsKeyboardFocus (true);
    setTitle ("Note editor");
    startTimerHz (30);
}

void PianoRoll::setTarget (int newTrackIndex, int newClipIndex)
{
    const bool changed = newTrackIndex != trackIndex || newClipIndex != clipIndex;
    trackIndex = newTrackIndex;
    clipIndex = newClipIndex;
    if (changed) { selection.clear(); hoverNote = -1; pixelsPerBeat = 0.0; scrollBeat = 0.0; drag = Drag::none; }
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
// Geometry

juce::Rectangle<int> PianoRoll::gridBounds() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth).withTrimmedTop (headerHeight + rulerHeight).withTrimmedBottom (velocityHeight);
}
juce::Rectangle<int> PianoRoll::velocityBounds() const { return getLocalBounds().removeFromBottom (velocityHeight).withTrimmedLeft (keyboardWidth); }
juce::Rectangle<int> PianoRoll::rulerBounds() const { return { keyboardWidth, headerHeight, getWidth() - keyboardWidth, rulerHeight }; }

int PianoRoll::visibleRows() const { return juce::jmax (1, gridBounds().getHeight() / rowHeight); }
double PianoRoll::sequenceLength() const { auto* seq = getSequence(); return seq != nullptr ? seq->lengthBeats : 8.0; }
double PianoRoll::visibleBeats() const { return pixelsPerBeat > 0.0 ? gridBounds().getWidth() / pixelsPerBeat : sequenceLength(); }

void PianoRoll::clampView()
{
    const auto g = gridBounds();
    if (pixelsPerBeat <= 0.0) pixelsPerBeat = juce::jmax (1.0, (double) g.getWidth() / sequenceLength());
    pixelsPerBeat = juce::jlimit (juce::jmax (1.0, (double) g.getWidth() / sequenceLength()), 400.0, pixelsPerBeat);
    scrollBeat = juce::jlimit (0.0, juce::jmax (0.0, sequenceLength() - visibleBeats()), scrollBeat);
}

int PianoRoll::pitchAtY (int y) const
{
    const auto g = gridBounds();
    if (y < g.getY() || y >= g.getBottom()) return -1;
    return juce::jlimit (0, 127, lowestPitch + (g.getBottom() - 1 - y) / rowHeight);
}

int PianoRoll::yForPitch (int pitch) const { return gridBounds().getBottom() - (pitch - lowestPitch + 1) * rowHeight; }
double PianoRoll::beatAtX (int x) const { return scrollBeat + (x - gridBounds().getX()) / juce::jmax (1.0, pixelsPerBeat); }
float PianoRoll::xForBeat (double beat) const { return (float) (gridBounds().getX() + (beat - scrollBeat) * pixelsPerBeat); }

juce::Rectangle<float> PianoRoll::noteRect (const engine::NoteEvent& n) const
{
    const float x1 = xForBeat (n.startBeat), x2 = xForBeat (n.getEndBeat());
    return { x1, (float) yForPitch (n.pitch) + 1.0f, juce::jmax (3.0f, x2 - x1 - 1.0f), (float) rowHeight - 2.0f };
}

int PianoRoll::noteAt (juce::Point<int> p) const
{
    auto* seq = getSequence();
    if (seq == nullptr || ! gridBounds().contains (p)) return -1;
    for (int i = (int) seq->notes.size() - 1; i >= 0; --i)
        if (noteRect (seq->notes[(size_t) i]).contains (p.toFloat())) return i;
    return -1;
}

int PianoRoll::velocityBarAt (int x) const
{
    auto* seq = getSequence();
    if (seq == nullptr) return -1;
    int best = -1; float bestDist = 5.0f;
    for (int i = 0; i < (int) seq->notes.size(); ++i)
    {
        const float d = std::abs (xForBeat (seq->notes[(size_t) i].startBeat) + 2.0f - (float) x);
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
    if (rel >= 0.0 && rel < clip->getLengthSeconds()) return snapBeat (std::fmod (transport.secondsToBeats (rel), sequenceLength()));
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
        return noteName (n.pitch) + "  velocity " + juce::String (n.velocity) + "  start " + barBeatTick (n.startBeat) + "  length " + juce::String (n.lengthBeats, 2) + " beats";
    }
    int lo = 127, hi = 0; double first = 1.0e9, last = 0.0;
    for (int i : idx) { const auto& n = seq->notes[(size_t) i]; lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); first = juce::jmin (first, n.startBeat); last = juce::jmax (last, n.getEndBeat()); }
    return juce::String (idx.size()) + " notes  " + noteName (lo) + " to " + noteName (hi) + "  " + barBeatTick (first) + " to " + barBeatTick (last);
}

//==============================================================================
// Painting

void PianoRoll::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 3);
    presetLabel.setBounds (header.removeFromLeft (44));
    presetBox.setBounds (header.removeFromLeft (160));
    clampView();
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
    clampView();
    const auto grid = gridBounds();
    const auto vel = velocityBounds();
    const int rows = visibleRows();
    const int beatsPerBar = juce::jmax (1, transport.getBeatsPerBar());
    const auto idx = selectedIndices();
    auto selectedHas = [&idx] (int i) { return std::find (idx.begin(), idx.end(), i) != idx.end(); };

    // Header: name and readout
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (track->name + "  -  " + clip->name, getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (220).withWidth (260), juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (describeSelection() + "    " + juce::String (EditSettings::modeName (mode())) + " | " + EditSettings::toolName (tool()) + " | grid " + juce::String (gridBeats(), 2),
                getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (480).withTrimmedRight (8), juce::Justification::centredRight, true);

    // Rows: keyboard + lane shading
    for (int r = 0; r < rows; ++r)
    {
        const int pitch = lowestPitch + r;
        if (pitch > 127) break;
        const int y = yForPitch (pitch);
        const bool black = isBlackKey (pitch);
        g.setColour (black ? theme::background : theme::background.brighter (0.04f));
        g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);
        auto key = juce::Rectangle<int> (0, y, keyboardWidth - 1, rowHeight);
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

    // Grid lines at the edit grid, beats and bars stronger; ruler labels on beats
    g.saveState();
    g.reduceClipRegion (grid.getUnion (rulerBounds()).getUnion (vel));
    const double step = juce::jmin (gridBeats(), 1.0);
    const double firstBeat = std::floor (scrollBeat / step) * step;
    g.setFont (juce::FontOptions (10.0f));
    for (double beat = firstBeat; beat <= juce::jmin (sequenceLength(), scrollBeat + visibleBeats()) + 1.0e-9; beat += step)
    {
        const float x = xForBeat (beat);
        const bool isBeat = std::abs (beat - std::round (beat)) < 1e-9;
        const bool isBar = isBeat && ((int) std::round (beat)) % beatsPerBar == 0;
        g.setColour (isBar ? theme::gridStrong.brighter (0.2f) : isBeat ? theme::gridStrong : theme::grid.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        g.drawVerticalLine ((int) x, (float) vel.getY(), (float) vel.getBottom());
        if (isBeat && beat < sequenceLength() - 1.0e-9)
        {
            g.setColour (isBar ? theme::text : theme::textDim);
            const int bar = (int) std::round (beat) / beatsPerBar + 1, b = (int) std::round (beat) % beatsPerBar + 1;
            g.drawText (isBar ? juce::String (bar) : juce::String (bar) + "." + juce::String (b), (int) x + 3, headerHeight, 40, rulerHeight, juce::Justification::centredLeft);
        }
    }
    // Sequence end
    if (sequenceLength() < scrollBeat + visibleBeats())
    {
        const float xe = xForBeat (sequenceLength());
        g.setColour (theme::panelDark.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (xe, (float) grid.getY(), (float) grid.getRight() - xe, (float) grid.getHeight() + velocityHeight + 0.0f));
    }

    // Notes
    for (int i = 0; i < (int) seq->notes.size(); ++i)
    {
        const auto& n = seq->notes[(size_t) i];
        if (n.pitch < lowestPitch || n.pitch >= lowestPitch + rows) continue;
        auto r = noteRect (n);
        const float alpha = 0.45f + 0.55f * (float) n.velocity / 127.0f;
        const bool sel = selectedHas (i);
        g.setColour (sel ? theme::accent : track->colour.withAlpha (alpha));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (i == hoverNote ? theme::text : sel ? theme::text.withAlpha (0.7f) : track->colour.brighter (0.4f));
        g.drawRoundedRectangle (r, 2.5f, 1.0f);
        if (r.getWidth() > 28.0f && rowHeight >= 12)
        {
            g.setColour (sel ? theme::background : theme::text.withAlpha (0.8f));
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (noteName (n.pitch), r.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, false);
        }
    }

    // Velocity lane
    g.setColour (theme::panel);
    g.fillRect (vel);
    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (vel.getY(), (float) vel.getX(), (float) vel.getRight());
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText ("velocity", getLocalBounds().removeFromBottom (velocityHeight).removeFromLeft (keyboardWidth - 4), juce::Justification::centredRight);
    for (int i = 0; i < (int) seq->notes.size(); ++i)
    {
        const auto& n = seq->notes[(size_t) i];
        const float x = xForBeat (n.startBeat);
        if (x < (float) vel.getX() - 4.0f || x > (float) vel.getRight()) continue;
        const float h = (float) (vel.getHeight() - 6) * (float) n.velocity / 127.0f;
        const bool sel = selectedHas (i);
        g.setColour (sel ? theme::accent : track->colour.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (x, (float) vel.getBottom() - 2.0f - h, 4.0f, h));
        g.setColour (sel ? theme::text : track->colour.brighter (0.5f));
        g.fillEllipse (x - 1.0f, (float) vel.getBottom() - 2.0f - h - 3.0f, 6.0f, 6.0f);
    }
    g.restoreState();

    // Rubber band / zoom band
    if (drag == Drag::band || drag == Drag::zoomBand)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        g.setColour (theme::accent.withAlpha (0.15f)); g.fillRect (r);
        g.setColour (theme::accent.withAlpha (0.8f)); g.drawRect (r, 1);
    }

    // Playhead (position within the looping clip)
    const double rel = transport.getPositionSeconds() - clip->getStartSeconds();
    if (rel >= 0.0 && rel < clip->getLengthSeconds())
    {
        const double beat = std::fmod (transport.secondsToBeats (rel), seq->lengthBeats);
        const float x = xForBeat (beat);
        if (x >= (float) grid.getX() && x <= (float) grid.getRight())
        {
            g.setColour (theme::playhead.withAlpha (0.8f));
            g.drawLine (x, (float) headerHeight, x, (float) getHeight(), 1.5f);
        }
    }

    g.setColour (theme::gridStrong);
    g.drawVerticalLine (keyboardWidth - 1, (float) headerHeight, (float) getHeight());
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (headerHeight + rulerHeight - 1, (float) keyboardWidth, (float) getWidth());

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

void PianoRoll::addNoteAt (int pitch, double beat, bool soft, bool startResize, juce::Point<int> at)
{
    auto* seq = getSequence();
    if (seq == nullptr) return;
    engine::NoteEvent n;
    n.pitch = pitch;
    n.velocity = soft ? softVelocity : defaultVelocity;
    n.startBeat = juce::jlimit (0.0, juce::jmax (0.0, seq->lengthBeats - gridBeats()), snapBeat (beat));
    n.lengthBeats = juce::jmin (lastNoteLength, seq->lengthBeats - n.startBeat);
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
    w->addTextEditor ("start", barBeatTick (n.startBeat), "Start");
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
    if (keep[0].pitch < lowestPitch || keep[0].pitch >= lowestPitch + visibleRows()) { lowestPitch = juce::jlimit (0, 127 - visibleRows() + 1, keep[0].pitch - visibleRows() / 2); }
}
void PianoRoll::nudgeSelection (double beats)
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
    if (n.startBeat < scrollBeat || n.startBeat > scrollBeat + visibleBeats()) scrollBeat = n.startBeat;
    if (n.pitch < lowestPitch || n.pitch >= lowestPitch + visibleRows()) lowestPitch = juce::jlimit (0, 127 - visibleRows() + 1, n.pitch - visibleRows() / 2);
    clampView(); repaint();
}
void PianoRoll::zoomBy (double factor)
{
    const double centre = scrollBeat + visibleBeats() / 2.0;
    pixelsPerBeat = (pixelsPerBeat <= 0.0 ? gridBounds().getWidth() / sequenceLength() : pixelsPerBeat) * factor;
    clampView();
    scrollBeat = centre - visibleBeats() / 2.0;
    clampView(); repaint();
}
void PianoRoll::zoomToFit() { pixelsPerBeat = 0.0; scrollBeat = 0.0; clampView(); repaint(); }

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
        if (auto* clip = getClip())
            transport.setPositionSeconds (clip->getStartSeconds() + transport.beatsToSeconds (juce::jlimit (0.0, sequenceLength(), snapBeat (beatAtX (e.x)))));
        return;
    }
    if (e.x < keyboardWidth)
    {
        if (const int pitch = pitchAtY (e.y); pitch >= 0) audition (pitch);
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
    const int hit = noteAt (e.getPosition());
    const auto t = tool();

    if (t == Tool::zoomer)
    {
        if (! e.mods.isAnyModifierKeyDown() && ! e.mods.isPopupMenu()) { drag = Drag::zoomBand; return; }
        const double at = beatAtX (e.x);
        const double factor = e.mods.isAltDown() ? 0.5 : 2.0;
        pixelsPerBeat = (pixelsPerBeat <= 0.0 ? gridBounds().getWidth() / sequenceLength() : pixelsPerBeat) * factor;
        clampView();
        scrollBeat = at - (e.x - gridBounds().getX()) / pixelsPerBeat;
        clampView(); repaint();
        return;
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
        const auto r = noteRect (n);
        const bool nearEnd = (r.getRight() - e.x) < edgeGrab, nearStart = (e.x - r.getX()) < edgeGrab && r.getWidth() > 2 * edgeGrab;
        if (t == Tool::trimmer) drag = (e.x - r.getX()) < (r.getRight() - e.x) ? Drag::resizeStart : Drag::resizeEnd;
        else if (t == Tool::selector || t == Tool::scrubber) drag = Drag::band;
        else drag = nearEnd ? Drag::resizeEnd : nearStart ? Drag::resizeStart : Drag::move;
        dragBase = *seq; dragIndices = selectedIndices(); dragAnchor = n;
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
            select (NoteEdits::inRegion (*seq, beatAtX (r.getX()), beatAtX (r.getRight()), p0, p1), e.mods.isShiftDown() || e.mods.isCtrlDown());
        }
        repaint();
        return;
    }
    if (d == Drag::zoomBand)
    {
        const auto r = juce::Rectangle<int> (dragStart, dragCurrent);
        if (r.getWidth() > 4)
        {
            const double b0 = beatAtX (r.getX()), b1 = beatAtX (r.getRight());
            pixelsPerBeat = gridBounds().getWidth() / juce::jmax (1.0 / 16.0, b1 - b0);
            scrollBeat = b0;
            clampView();
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
        const auto r = noteRect (seq->notes[(size_t) hit]);
        const bool edge = (r.getRight() - e.x) < edgeGrab || ((e.x - r.getX()) < edgeGrab && r.getWidth() > 2 * edgeGrab);
        setMouseCursor (t == Tool::trimmer || (edge && (t == Tool::smart || t == Tool::grabber || t == Tool::pencil)) ? juce::MouseCursor::LeftRightResizeCursor
                        : t == Tool::selector ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::DraggingHandCursor);
        return;
    }
    setMouseCursor (t == Tool::selector ? juce::MouseCursor::IBeamCursor : t == Tool::pencil ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown()) { zoomBy (w.deltaY > 0 ? 1.25 : 0.8); return; }
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        scrollBeat -= d * visibleBeats() * 0.5;
        clampView(); repaint();
        return;
    }
    const int step = w.deltaY > 0 ? 2 : w.deltaY < 0 ? -2 : 0;
    lowestPitch = juce::jlimit (0, 127 - visibleRows() + 1, lowestPitch + step);
    repaint();
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
    if (k (juce::KeyPress::leftKey) || k (',')) { nudgeSelection (-grid); return true; }
    if (k (juce::KeyPress::rightKey) || k ('.')) { nudgeSelection (grid); return true; }
    if (k (juce::KeyPress::leftKey, M::shiftModifier)) { nudgeSelection (-grid / 4.0); return true; }
    if (k (juce::KeyPress::rightKey, M::shiftModifier)) { nudgeSelection (grid / 4.0); return true; }
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
    const double rel = transport.getPositionSeconds() - clip->getStartSeconds();
    const int x = (rel >= 0.0 && rel < clip->getLengthSeconds()) ? (int) xForBeat (std::fmod (transport.secondsToBeats (rel), seq->lengthBeats)) : -1;
    if (x != lastPlayheadX) { lastPlayheadX = x; repaint(); }
}

} // namespace beatmaker::ui
