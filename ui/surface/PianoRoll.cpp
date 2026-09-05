#include "PianoRoll.h"

namespace beatmaker::ui
{

PianoRoll::PianoRoll (model::Session& s, engine::Transport& t, engine::AudioGraph& g)
    : session (s), transport (t), graph (g)
{
    addAndMakeVisible (presetLabel);
    presetLabel.setColour (juce::Label::textColourId, theme::textDim);
    presetLabel.setFont (juce::FontOptions (12.0f));

    addAndMakeVisible (presetBox);
    int id = 1;
    for (const auto& p : engine::SynthParams::presets())
        presetBox.addItem (p.name, id++);
    presetBox.onChange = [this]
    {
        if (trackIndex >= 0 && presetBox.getSelectedId() > 0 && onPresetChanged)
            onPresetChanged (trackIndex, presetBox.getSelectedId() - 1);
    };

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void PianoRoll::setTarget (int newTrackIndex, int newClipIndex)
{
    trackIndex = newTrackIndex;
    clipIndex = newClipIndex;
    selectedNote = hoverNote = -1;
    refreshPresetBox();
    repaint();
}

void PianoRoll::refreshPresetBox()
{
    auto* track = getTrack();
    if (track == nullptr || track->synthParams == nullptr) return;
    const auto presets = engine::SynthParams::presets();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].name == track->synthParams->name)
            presetBox.setSelectedId (i + 1, juce::dontSendNotification);
}

//==============================================================================
// Model access

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

//==============================================================================
// Geometry

juce::Rectangle<int> PianoRoll::gridBounds() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth).withTrimmedTop (headerHeight + rulerHeight);
}

int PianoRoll::visibleRows() const { return juce::jmax (1, gridBounds().getHeight() / rowHeight); }

int PianoRoll::pitchAtY (int y) const
{
    const auto g = gridBounds();
    if (y < g.getY() || y >= g.getBottom()) return -1;
    const int rowFromBottom = (g.getBottom() - 1 - y) / rowHeight;
    return juce::jlimit (0, 127, lowestPitch + rowFromBottom);
}

int PianoRoll::yForPitch (int pitch) const
{
    return gridBounds().getBottom() - (pitch - lowestPitch + 1) * rowHeight;
}

double PianoRoll::beatAtX (int x) const
{
    auto* seq = getSequence();
    const auto g = gridBounds();
    const double length = seq != nullptr ? seq->lengthBeats : 8.0;
    return juce::jlimit (0.0, length, (x - g.getX()) * length / juce::jmax (1, g.getWidth()));
}

float PianoRoll::xForBeat (double beat) const
{
    auto* seq = getSequence();
    const auto g = gridBounds();
    const double length = seq != nullptr ? seq->lengthBeats : 8.0;
    return (float) (g.getX() + beat / length * g.getWidth());
}

juce::Rectangle<float> PianoRoll::noteRect (const engine::NoteEvent& n) const
{
    const float x1 = xForBeat (n.startBeat), x2 = xForBeat (n.getEndBeat());
    return { x1, (float) yForPitch (n.pitch) + 1.0f, juce::jmax (3.0f, x2 - x1 - 1.0f), (float) rowHeight - 2.0f };
}

int PianoRoll::noteAt (juce::Point<int> p) const
{
    auto* seq = getSequence();
    if (seq == nullptr) return -1;
    for (int i = (int) seq->notes.size() - 1; i >= 0; --i)
        if (noteRect (seq->notes[(size_t) i]).contains (p.toFloat())) return i;
    return -1;
}

//==============================================================================
// Painting

void PianoRoll::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 3);
    presetLabel.setBounds (header.removeFromLeft (44));
    presetBox.setBounds (header.removeFromLeft (160));
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
        g.drawText ("Select a Synth track to edit its notes", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto grid = gridBounds();
    const int rows = visibleRows();
    const int beatsPerBar = transport.getBeatsPerBar();

    // Header strip
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (track->name + "  -  " + clip->name, getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (220),
                juce::Justification::centredLeft, true);

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

    // Beat grid + ruler
    const int totalSteps = (int) std::round (seq->lengthBeats / gridBeats);
    g.setFont (juce::FontOptions (10.0f));
    for (int i = 0; i <= totalSteps; ++i)
    {
        const double beat = i * gridBeats;
        const float x = xForBeat (beat);
        const bool isBeat = std::abs (beat - std::round (beat)) < 1e-9;
        const bool isBar = isBeat && ((int) std::round (beat)) % beatsPerBar == 0;

        g.setColour (isBar ? theme::gridStrong.brighter (0.2f) : isBeat ? theme::gridStrong : theme::grid.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());

        if (isBeat && i < totalSteps)
        {
            g.setColour (isBar ? theme::text : theme::textDim);
            const int bar = (int) std::round (beat) / beatsPerBar + 1, b = (int) std::round (beat) % beatsPerBar + 1;
            g.drawText (isBar ? juce::String (bar) : juce::String (bar) + "." + juce::String (b),
                        (int) x + 3, headerHeight, 40, rulerHeight, juce::Justification::centredLeft);
        }
    }

    // Notes
    for (int i = 0; i < (int) seq->notes.size(); ++i)
    {
        const auto& n = seq->notes[(size_t) i];
        if (n.pitch < lowestPitch || n.pitch >= lowestPitch + rows) continue;
        auto r = noteRect (n);
        const float alpha = 0.45f + 0.55f * (float) n.velocity / 127.0f;
        g.setColour (i == selectedNote ? theme::accent : track->colour.withAlpha (alpha));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (i == hoverNote ? theme::text : track->colour.brighter (0.4f));
        g.drawRoundedRectangle (r, 2.5f, 1.0f);
    }

    // Playhead (position within the looping clip)
    const double rel = transport.getPositionSeconds() - clip->getStartSeconds();
    if (rel >= 0.0 && rel < clip->getLengthSeconds())
    {
        const double beat = std::fmod (transport.secondsToBeats (rel), seq->lengthBeats);
        const float x = xForBeat (beat);
        g.setColour (theme::playhead.withAlpha (0.8f));
        g.drawLine (x, (float) headerHeight, x, (float) getHeight(), 1.5f);
    }

    g.setColour (theme::gridStrong);
    g.drawVerticalLine (keyboardWidth - 1, (float) headerHeight, (float) getHeight());
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (headerHeight + rulerHeight - 1, (float) keyboardWidth, (float) getWidth());
}

//==============================================================================
// Editing

void PianoRoll::commit (engine::MidiSequence updated, const juce::String& action)
{
    updated.sortNotes();
    if (onSequenceChanged)
        onSequenceChanged (trackIndex, clipIndex, std::make_shared<const engine::MidiSequence> (std::move (updated)), action);
}

void PianoRoll::audition (int pitch, float velocity)
{
    if (auto* track = getTrack())
        graph.triggerNotePreview (track->id, pitch, velocity, 0.3);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto* seq = getSequence();
    if (seq == nullptr || e.y < headerHeight + rulerHeight) return;

    const int pitch = pitchAtY (e.y);
    if (pitch < 0) return;

    if (e.x < keyboardWidth) { audition (pitch); return; }

    const int hit = noteAt (e.getPosition());

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            auto updated = *seq;
            updated.notes.erase (updated.notes.begin() + hit);
            selectedNote = -1;
            commit (std::move (updated), "Delete Note");
        }
        return;
    }

    if (hit >= 0)
    {
        selectedNote = hit;
        dragOriginal = seq->notes[(size_t) hit];
        dragStart = e.getPosition();
        dragChanged = false;
        const float rightEdge = noteRect (dragOriginal).getRight();
        drag = (rightEdge - e.x) < 6.0f ? Drag::resize : Drag::move;
        audition (dragOriginal.pitch, (float) dragOriginal.velocity / 127.0f);
        repaint();
        return;
    }

    // Empty space: add a note
    engine::NoteEvent n;
    n.pitch = pitch;
    n.velocity = e.mods.isShiftDown() ? 70 : 100;
    n.startBeat = snap (beatAtX (e.x));
    n.lengthBeats = lastNoteLength;
    if (n.startBeat >= seq->lengthBeats) n.startBeat = seq->lengthBeats - gridBeats;
    n.lengthBeats = juce::jmin (n.lengthBeats, seq->lengthBeats - n.startBeat);

    auto updated = *seq;
    updated.notes.push_back (n);
    audition (pitch, (float) n.velocity / 127.0f);
    commit (std::move (updated), "Add Note");

    // Select the note we just added (it will be at its sorted position)
    if (auto* fresh = getSequence())
        for (int i = 0; i < (int) fresh->notes.size(); ++i)
            if (fresh->notes[(size_t) i].pitch == n.pitch && engine::NoteEvent::sameBeat (fresh->notes[(size_t) i].startBeat, n.startBeat))
            {
                selectedNote = i;
                dragOriginal = fresh->notes[(size_t) i];
                dragStart = e.getPosition();
                drag = Drag::resize;      // keep dragging to set the length
                dragChanged = false;
                break;
            }
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    auto* seq = getSequence();
    if (seq == nullptr || drag == Drag::none || selectedNote < 0 || selectedNote >= (int) seq->notes.size()) return;

    engine::NoteEvent n = dragOriginal;
    const double beatDelta = snap (beatAtX (e.x) - beatAtX (dragStart.x));

    if (drag == Drag::move)
    {
        const int pitchDelta = (dragStart.y - e.y) / rowHeight;
        n.pitch = juce::jlimit (0, 127, dragOriginal.pitch + pitchDelta);
        n.startBeat = juce::jlimit (0.0, seq->lengthBeats - gridBeats, snap (dragOriginal.startBeat + beatDelta));
        n.lengthBeats = juce::jmin (dragOriginal.lengthBeats, seq->lengthBeats - n.startBeat);
    }
    else
    {
        n.lengthBeats = juce::jlimit (gridBeats, seq->lengthBeats - n.startBeat, snap (dragOriginal.lengthBeats + beatDelta));
    }

    const auto& cur = seq->notes[(size_t) selectedNote];
    if (cur.samePlacement (n)) return;

    // Live update: replace the sequence but keep it as one undo step by
    // undoing our previous intermediate state first.
    if (dragChanged) session.undo();
    dragChanged = true;

    auto updated = *getSequence();
    // After the undo the note is back at its original position/index.
    for (int i = 0; i < (int) updated.notes.size(); ++i)
        if (updated.notes[(size_t) i].pitch == dragOriginal.pitch && engine::NoteEvent::sameBeat (updated.notes[(size_t) i].startBeat, dragOriginal.startBeat))
        {
            updated.notes[(size_t) i] = n;
            break;
        }
    if (n.pitch != dragOriginal.pitch) audition (n.pitch, (float) n.velocity / 127.0f);
    commit (std::move (updated), drag == Drag::move ? "Move Note" : "Resize Note");

    if (auto* fresh = getSequence())
        for (int i = 0; i < (int) fresh->notes.size(); ++i)
            if (fresh->notes[(size_t) i].pitch == n.pitch && engine::NoteEvent::sameBeat (fresh->notes[(size_t) i].startBeat, n.startBeat))
            { selectedNote = i; break; }
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::resize && selectedNote >= 0)
        if (auto* seq = getSequence(); seq != nullptr && selectedNote < (int) seq->notes.size())
            lastNoteLength = seq->notes[(size_t) selectedNote].lengthBeats;
    drag = Drag::none;
    dragChanged = false;
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const int hit = noteAt (e.getPosition());
    if (hit != hoverNote) { hoverNote = hit; repaint(); }
    if (hit >= 0)
        if (auto* seq = getSequence())
            setMouseCursor ((noteRect (seq->notes[(size_t) hit]).getRight() - e.x) < 6.0f ? juce::MouseCursor::LeftRightResizeCursor
                                                                                           : juce::MouseCursor::DraggingHandCursor);
    if (hit < 0) setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int step = w.deltaY > 0 ? 2 : w.deltaY < 0 ? -2 : 0;
    lowestPitch = juce::jlimit (0, 127 - visibleRows() + 1, lowestPitch + step);
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    auto* seq = getSequence();
    if (seq == nullptr) return false;

    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedNote >= 0
        && selectedNote < (int) seq->notes.size())
    {
        auto updated = *seq;
        updated.notes.erase (updated.notes.begin() + selectedNote);
        selectedNote = -1;
        commit (std::move (updated), "Delete Note");
        return true;
    }
    return false;
}

void PianoRoll::timerCallback()
{
    auto* clip = getClip();
    auto* seq = getSequence();
    if (clip == nullptr || seq == nullptr) return;
    const double rel = transport.getPositionSeconds() - clip->getStartSeconds();
    const int x = (rel >= 0.0 && rel < clip->getLengthSeconds())
                    ? (int) xForBeat (std::fmod (transport.secondsToBeats (rel), seq->lengthBeats)) : -1;
    if (x != lastPlayheadX) { lastPlayheadX = x; repaint(); }
}

} // namespace beatmaker::ui
