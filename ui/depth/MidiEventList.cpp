#include "MidiEventList.h"
#include <ClipEdits.h>
#include <map>

namespace beatmaker::ui
{

namespace
{
    class EditableCell final : public juce::Label
    {
    public:
        EditableCell (std::function<void (int, int, const juce::String&)> onEdit) : commit (std::move (onEdit))
        {
            setEditable (false, true, false);
            setColour (juce::Label::textColourId, theme::text);
            setColour (juce::Label::textWhenEditingColourId, theme::text);
            onTextChange = [this] { if (commit) commit (row, column, getText()); };
        }
        int row = 0, column = 0;
    private:
        std::function<void (int, int, const juce::String&)> commit;
    };
}

MidiEventList::MidiEventList (model::Session& s, engine::Transport& t) : session (s), transport (t)
{
    session.addListener (this);
    addAndMakeVisible (title);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, theme::text);

    addAndMakeVisible (table);
    table.setModel (this);
    table.setColour (juce::ListBox::backgroundColourId, theme::background);
    table.setRowHeight (22);
    auto& header = table.getHeader();
    header.addColumn ("#", colIndex, 40);
    header.addColumn ("Start (bar|beat|tick)", colStart, 150);
    header.addColumn ("Note", colNote, 70);
    header.addColumn ("Velocity", colVelocity, 70);
    header.addColumn ("Length (beats)", colLength, 110);
    header.addColumn ("Clip", colClip, 140);
    table.setMultipleSelectionEnabled (true);

    addAndMakeVisible (insertButton);
    insertButton.onClick = [this] { insertNoteAtPlayhead(); };
    addAndMakeVisible (deleteButton);
    deleteButton.onClick = [this] { deleteSelectedNotes(); };

    // Real-time properties strip
    addAndMakeVisible (quantizeToggle);
    quantizeToggle.onClick = [this] { applyProps(); };
    addAndMakeVisible (quantizeGrid);
    quantizeGrid.addItem ("1/4", 1); quantizeGrid.addItem ("1/8", 2); quantizeGrid.addItem ("1/16", 3); quantizeGrid.addItem ("1/32", 4); quantizeGrid.addItem ("1/8 triplet", 5); quantizeGrid.addItem ("1/16 triplet", 6);
    quantizeGrid.onChange = [this] { applyProps(); };
    auto slider = [this] (juce::Slider& sl, juce::Label& l, double min, double max, double step, const juce::String& suffix)
    {
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        sl.setRange (min, max, step);
        sl.setTextValueSuffix (suffix);
        sl.setColour (juce::Slider::trackColourId, theme::accent);
        sl.onValueChange = [this] { applyProps(); };
        addAndMakeVisible (sl);
        l.setColour (juce::Label::textColourId, theme::textDim);
        l.setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (l);
    };
    slider (strength, strengthLabel, 0.0, 100.0, 1.0, "%");
    slider (transpose, transposeLabel, -24.0, 24.0, 1.0, " st");
    slider (velocityScale, velScaleLabel, 0.0, 200.0, 1.0, "%");
    slider (velocityOffset, velOffsetLabel, -64.0, 64.0, 1.0, "");
    slider (delay, delayLabel, -500.0, 500.0, 1.0, " ms");
    slider (duration, durationLabel, 10.0, 400.0, 1.0, "%");
    setSize (preferredWidth, preferredHeight);
    rebuild();
}

MidiEventList::~MidiEventList() { session.removeListener (this); }

void MidiEventList::setTrack (int index) { trackIndex = index; rebuild(); }

juce::String MidiEventList::barBeatTick (double absoluteBeat) const
{
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const int bar = (int) std::floor (absoluteBeat / bpb);
    const double inBar = absoluteBeat - bar * bpb;
    const int beat = (int) std::floor (inBar);
    const int tick = (int) std::lround ((inBar - beat) * engine::Transport::ticksPerBeat);
    return juce::String (bar + 1) + " | " + juce::String (beat + 1) + " | " + juce::String (tick).paddedLeft ('0', 3);
}

void MidiEventList::rebuild()
{
    rows.clear();
    const auto* t = session.getTrack (trackIndex);
    if (t != nullptr && t->isSynth())
    {
        for (int c = 0; c < (int) t->midiClips.size(); ++c)
        {
            const auto& clip = t->midiClips[(size_t) c];
            if (clip.sequence == nullptr) continue;
            const double clipStartBeats = transport.secondsToBeats (clip.getStartSeconds());
            const double loopOffsetBeats = transport.secondsToBeats ((double) clip.loopOffset / clip.sampleRate);
            for (int n = 0; n < (int) clip.sequence->notes.size(); ++n)
            {
                Row r; r.clipIndex = c; r.noteIndex = n; r.note = clip.sequence->notes[(size_t) n]; r.clipName = clip.name;
                r.absoluteBeat = clipStartBeats + r.note.startBeat - loopOffsetBeats;
                rows.push_back (r);
            }
        }
        std::stable_sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.absoluteBeat < b.absoluteBeat; });
        title.setText (t->name + "  -  " + juce::String (rows.size()) + " events", juce::dontSendNotification);
    }
    else title.setText ("Select an instrument track to list its MIDI events", juce::dontSendNotification);
    rebuildPropsControls();
    table.updateContent();
    table.repaint();
}

void MidiEventList::rebuildPropsControls()
{
    const auto* t = session.getTrack (trackIndex);
    const bool enabled = t != nullptr && t->isSynth();
    for (auto* c : std::initializer_list<juce::Component*> { &quantizeToggle, &quantizeGrid, &strength, &transpose, &velocityScale, &velocityOffset, &delay, &duration, &insertButton, &deleteButton })
        c->setEnabled (enabled);
    if (! enabled) return;
    syncingProps = true;
    const auto& p = t->midiProps;
    quantizeToggle.setToggleState (p.quantize, juce::dontSendNotification);
    const double grids[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    int gridId = 3;
    for (int i = 0; i < 6; ++i) if (std::abs (grids[i] - p.quantizeBeats) < 1e-6) gridId = i + 1;
    quantizeGrid.setSelectedId (gridId, juce::dontSendNotification);
    strength.setValue (p.quantizeStrength * 100.0, juce::dontSendNotification);
    transpose.setValue (p.transpose, juce::dontSendNotification);
    velocityScale.setValue (p.velocityScale * 100.0, juce::dontSendNotification);
    velocityOffset.setValue (p.velocityOffset, juce::dontSendNotification);
    delay.setValue (p.delayMs, juce::dontSendNotification);
    duration.setValue (p.durationScale * 100.0, juce::dontSendNotification);
    syncingProps = false;
}

void MidiEventList::applyProps()
{
    if (syncingProps) return;
    const auto* t = session.getTrack (trackIndex);
    if (t == nullptr || ! t->isSynth()) return;
    engine::MidiRealtimeProps p;
    p.quantize = quantizeToggle.getToggleState();
    const double grids[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    p.quantizeBeats = grids[juce::jlimit (0, 5, quantizeGrid.getSelectedId() - 1)];
    p.quantizeStrength = (float) strength.getValue() * 0.01f;
    p.transpose = (int) transpose.getValue();
    p.velocityScale = (float) velocityScale.getValue() * 0.01f;
    p.velocityOffset = (int) velocityOffset.getValue();
    p.delayMs = delay.getValue();
    p.durationScale = (float) duration.getValue() * 0.01f;
    session.execute (std::make_unique<model::SetTrackMidiPropsCommand> (trackIndex, p));
}

void MidiEventList::paintRowBackground (juce::Graphics& g, int, int width, int height, bool selected)
{
    if (selected) { g.setColour (theme::accent.withAlpha (0.3f)); g.fillRect (0, 0, width, height); }
}

void MidiEventList::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
    const auto& r = rows[(size_t) row];
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f));
    juce::String text;
    switch (columnId)
    {
        case colIndex: text = juce::String (row + 1); g.setColour (theme::textDim); break;
        case colClip:  text = r.clipName; g.setColour (theme::textDim); break;
        default: return;   // editable columns are components
    }
    g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
}

juce::Component* MidiEventList::refreshComponentForCell (int row, int columnId, bool, juce::Component* existing)
{
    if (columnId == colIndex || columnId == colClip || ! juce::isPositiveAndBelow (row, (int) rows.size())) { delete existing; return nullptr; }
    auto* cell = dynamic_cast<EditableCell*> (existing);
    if (cell == nullptr) { delete existing; cell = new EditableCell ([this] (int r, int c, const juce::String& text) { applyEdit (r, c, text); }); }
    cell->row = row; cell->column = columnId;
    const auto& r = rows[(size_t) row];
    juce::String text;
    switch (columnId)
    {
        case colStart:    text = barBeatTick (r.absoluteBeat); break;
        case colNote:     text = juce::MidiMessage::getMidiNoteName (r.note.pitch, true, true, 3); break;
        case colVelocity: text = juce::String (r.note.velocity); break;
        case colLength:   text = juce::String (r.note.lengthBeats, 3); break;
        default: break;
    }
    cell->setText (text, juce::dontSendNotification);
    return cell;
}

void MidiEventList::applyEdit (int row, int columnId, const juce::String& text)
{
    const auto* t = session.getTrack (trackIndex);
    if (t == nullptr || ! juce::isPositiveAndBelow (row, (int) rows.size())) return;
    const auto& r = rows[(size_t) row];
    if (r.clipIndex >= (int) t->midiClips.size()) return;
    const auto& clip = t->midiClips[(size_t) r.clipIndex];
    if (clip.sequence == nullptr || r.noteIndex >= (int) clip.sequence->notes.size()) return;
    auto seq = std::make_shared<engine::MidiSequence> (*clip.sequence);
    auto& n = seq->notes[(size_t) r.noteIndex];
    juce::String name = "Edit MIDI Event";
    if (columnId == colNote)
    {
        // "C4", "F#3", or a number
        const auto s = text.trim().toUpperCase();
        int pitch = -1;
        if (s.containsOnly ("0123456789")) pitch = s.getIntValue();
        else
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            for (int i = 11; i >= 0; --i)
                if (s.startsWith (names[i]) && s.substring (juce::String (names[i]).length()).containsOnly ("-0123456789")) { pitch = i + (s.substring (juce::String (names[i]).length()).getIntValue() + 1) * 12; break; }
        }
        if (! juce::isPositiveAndBelow (pitch, 128)) { rebuild(); return; }
        n.pitch = pitch; name = "Change Note";
    }
    else if (columnId == colVelocity) { n.velocity = juce::jlimit (1, 127, text.getIntValue()); name = "Change Velocity"; }
    else if (columnId == colLength)   { n.lengthBeats = juce::jmax (0.01, text.getDoubleValue()); name = "Change Length"; }
    else if (columnId == colStart)
    {
        const auto parts = juce::StringArray::fromTokens (text, "|", {});
        if (parts.size() < 2) { rebuild(); return; }
        const int bpb = juce::jmax (1, transport.getBeatsPerBar());
        const double absolute = (parts[0].getIntValue() - 1) * bpb + (parts[1].getIntValue() - 1) + (parts.size() > 2 ? parts[2].getIntValue() / (double) engine::Transport::ticksPerBeat : 0.0);
        const double clipStartBeats = transport.secondsToBeats (clip.getStartSeconds()) - transport.secondsToBeats ((double) clip.loopOffset / clip.sampleRate);
        n.startBeat = juce::jmax (0.0, absolute - clipStartBeats);
        name = "Move Note";
    }
    seq->sortNotes();
    session.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (trackIndex, r.clipIndex, seq, name));
}

void MidiEventList::cellClicked (int row, int, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) rows.size()))
        transport.setPositionSeconds (transport.beatsToSeconds (rows[(size_t) row].absoluteBeat));
}

void MidiEventList::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) rows.size())) transport.setPositionSeconds (transport.beatsToSeconds (rows[(size_t) row].absoluteBeat));
}

void MidiEventList::deleteKeyPressed (int) { deleteSelectedNotes(); }

void MidiEventList::deleteSelectedNotes()
{
    const auto* t = session.getTrack (trackIndex);
    if (t == nullptr) return;
    // Group by clip, delete from the highest note index down
    std::map<int, std::vector<int>> byClip;
    for (int i = 0; i < table.getNumSelectedRows(); ++i)
    {
        const int row = table.getSelectedRow (i);
        if (juce::isPositiveAndBelow (row, (int) rows.size())) byClip[rows[(size_t) row].clipIndex].push_back (rows[(size_t) row].noteIndex);
    }
    if (byClip.empty()) return;
    auto compound = std::make_unique<model::CompoundCommand> ("Delete MIDI Events");
    for (auto& [clipIndex, notes] : byClip)
    {
        if (clipIndex >= (int) t->midiClips.size() || t->midiClips[(size_t) clipIndex].sequence == nullptr) continue;
        auto seq = std::make_shared<engine::MidiSequence> (*t->midiClips[(size_t) clipIndex].sequence);
        std::sort (notes.rbegin(), notes.rend());
        for (int n : notes) if (n < (int) seq->notes.size()) seq->notes.erase (seq->notes.begin() + n);
        compound->add (std::make_unique<model::ReplaceMidiSequenceCommand> (trackIndex, clipIndex, seq, "Delete MIDI Events"));
    }
    session.execute (std::move (compound));
}

void MidiEventList::insertNoteAtPlayhead()
{
    const auto* t = session.getTrack (trackIndex);
    if (t == nullptr || t->midiClips.empty()) { if (onStatus) onStatus(); return; }
    const double here = transport.getPositionSeconds();
    int clipIndex = 0;
    for (int c = 0; c < (int) t->midiClips.size(); ++c)
        if (here >= t->midiClips[(size_t) c].getStartSeconds() && here < t->midiClips[(size_t) c].getEndSeconds()) clipIndex = c;
    const auto& clip = t->midiClips[(size_t) clipIndex];
    auto seq = std::make_shared<engine::MidiSequence> (clip.sequence != nullptr ? *clip.sequence : engine::MidiSequence());
    engine::NoteEvent n;
    n.pitch = 60; n.velocity = 100; n.lengthBeats = 1.0;
    n.startBeat = juce::jmax (0.0, transport.secondsToBeats (here - clip.getStartSeconds()) + transport.secondsToBeats ((double) clip.loopOffset / clip.sampleRate));
    seq->notes.push_back (n);
    seq->sortNotes();
    session.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (trackIndex, clipIndex, seq, "Insert Note"));
}

void MidiEventList::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Real-Time Properties (applied at playback; the stored notes are untouched)", 16, 32, getWidth() - 32, 14, juce::Justification::centredLeft);
    g.drawText ("Click a row to locate; edit Start, Note (e.g. F#3), Velocity and Length in place; Delete removes selected events.", 16, getHeight() - 20, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void MidiEventList::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedBottom (18);
    title.setBounds (area.removeFromTop (22));
    area.removeFromTop (24);
    auto props = area.removeFromTop (58);
    auto rowA = props.removeFromTop (26), rowB = props.removeFromTop (26);
    quantizeToggle.setBounds (rowA.removeFromLeft (90)); quantizeGrid.setBounds (rowA.removeFromLeft (110)); rowA.removeFromLeft (8);
    strengthLabel.setBounds (rowA.removeFromLeft (52)); strength.setBounds (rowA.removeFromLeft (150)); rowA.removeFromLeft (8);
    transposeLabel.setBounds (rowA.removeFromLeft (60)); transpose.setBounds (rowA);
    velScaleLabel.setBounds (rowB.removeFromLeft (40)); velocityScale.setBounds (rowB.removeFromLeft (140)); rowB.removeFromLeft (8);
    velOffsetLabel.setBounds (rowB.removeFromLeft (36)); velocityOffset.setBounds (rowB.removeFromLeft (120)); rowB.removeFromLeft (8);
    delayLabel.setBounds (rowB.removeFromLeft (54)); delay.setBounds (rowB.removeFromLeft (130)); rowB.removeFromLeft (8);
    durationLabel.setBounds (rowB.removeFromLeft (64)); duration.setBounds (rowB);
    area.removeFromTop (8);
    auto buttons = area.removeFromBottom (26);
    insertButton.setBounds (buttons.removeFromLeft (170)); buttons.removeFromLeft (6);
    deleteButton.setBounds (buttons.removeFromLeft (80));
    area.removeFromBottom (6);
    table.setBounds (area);
}

} // namespace beatmaker::ui
