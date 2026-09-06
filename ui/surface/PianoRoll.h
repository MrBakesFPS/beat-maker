// PianoRoll: the note editor for one MidiClip. It shares the edit window's
// settings (mode, tool, grid, relative grid, Commands Focus): Grid snaps,
// Slip is free, Spot types a position; the Zoomer zooms, the Trimmer drags
// either edge, the Selector rubber-bands, the Grabber moves, the Pencil adds
// and paints velocities, the Smart Tool does the right one by where you
// click. Multi-selection, a velocity lane, zoom and scroll, and the same
// keyboard vocabulary as the tracks. Every edit is one undo step through
// onSequenceChanged.
#pragma once

#include "../shared/EditSettings.h"
#include "../shared/Theme.h"
#include <NoteEdits.h>
#include <Session.h>
#include <graph/AudioGraph.h>
#include <transport/Transport.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class PianoRoll final : public juce::Component,
                        private juce::Timer
{
public:
    PianoRoll (model::Session& session, engine::Transport& transport, engine::AudioGraph& graph);

    void setEditSettings (EditSettings* s) { settings = s; }
    void setTarget (int trackIndex, int midiClipIndex);
    void setDefaultVelocities (int normal, int soft) { defaultVelocity = juce::jlimit (1, 127, normal); softVelocity = juce::jlimit (1, 127, soft); }
    int getTrackIndex() const noexcept { return trackIndex; }
    bool hasTarget() const { return getSequence() != nullptr; }

    // Editing commands (also reachable from the palette when the notes have focus)
    bool hasSelection() const noexcept { return ! selection.empty(); }
    int numSelected() const noexcept { return (int) selection.size(); }
    void selectAll();
    void clearSelection();
    void deleteSelection();
    void copySelection();
    void cutSelection();
    bool canPaste() const;
    void pasteAtInsertion();
    void duplicateSelection();
    void quantizeSelection (bool lengthsToo = false);
    void transposeSelection (int semitones);
    void nudgeSelection (double beats);
    void changeVelocity (int delta);
    void legatoSelection();
    void selectNextNote (bool forward);
    void zoomBy (double factor);
    void zoomToFit();
    juce::String describeSelection() const;

    std::function<void (int trackIndex, int clipIndex, std::shared_ptr<const engine::MidiSequence>, juce::String)> onSequenceChanged;
    std::function<void (int trackIndex, int presetIndex)> onPresetChanged;
    std::function<void (const juce::String&)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    static constexpr int keyboardWidth = 56;
    static constexpr int headerHeight  = 26;   // preset bar + readout
    static constexpr int rulerHeight   = 18;
    static constexpr int rowHeight     = 14;
    static constexpr int velocityHeight = 56;
    static constexpr int edgeGrab      = 6;

    enum class Drag { none, pending, move, resizeEnd, resizeStart, band, velocity, zoomBand };

    void timerCallback() override;
    const model::Track* getTrack() const;
    const model::MidiClip* getClip() const;
    const engine::MidiSequence* getSequence() const;

    // Settings
    double gridBeats() const noexcept;
    EditSettings::Tool tool() const noexcept { return settings != nullptr ? settings->tool : EditSettings::Tool::smart; }
    EditSettings::Mode mode() const noexcept { return settings != nullptr ? settings->mode : EditSettings::Mode::grid; }
    bool snapping() const noexcept { return mode() == EditSettings::Mode::grid || mode() == EditSettings::Mode::shuffle; }
    double snapBeat (double beat) const noexcept { return snapping() ? model::NoteEdits::snap (beat, gridBeats()) : beat; }
    double snapDelta (double anchorStart, double delta) const noexcept;   // Grid: absolute or relative per the setting

    // Geometry
    juce::Rectangle<int> gridBounds() const;
    juce::Rectangle<int> velocityBounds() const;
    juce::Rectangle<int> rulerBounds() const;
    int visibleRows() const;
    int pitchAtY (int y) const;
    double beatAtX (int x) const;
    float xForBeat (double beat) const;
    int yForPitch (int pitch) const;
    double visibleBeats() const;
    double sequenceLength() const;
    void clampView();
    juce::Rectangle<float> noteRect (const engine::NoteEvent&) const;
    int noteAt (juce::Point<int>) const;
    int velocityBarAt (int x) const;
    static bool isBlackKey (int pitch) { const int n = pitch % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
    static juce::String noteName (int pitch);
    juce::String barBeatTick (double beat) const;

    // Selection
    std::vector<int> selectedIndices() const;
    bool isSelected (int index) const;
    void select (const std::vector<int>& indices, bool add);
    void toggle (int index);
    double insertionBeat() const;

    // Editing
    void apply (engine::MidiSequence updated, const juce::String& action, const std::vector<model::NoteKey>* keepKeys = nullptr);
    void commit (engine::MidiSequence updated, const juce::String& action);
    void liveCommit (engine::MidiSequence updated, const juce::String& action);   // during a drag: one undo step in total
    void addNoteAt (int pitch, double beat, bool soft, bool startResize, juce::Point<int> at);
    void spotNote (int index);
    void audition (int pitch, float velocity = 0.8f);
    void refreshPresetBox();
    void status (const juce::String&);

    model::Session& session;
    engine::Transport& transport;
    engine::AudioGraph& graph;
    EditSettings* settings = nullptr;

    int trackIndex = -1, clipIndex = -1;
    int lowestPitch = 48;         // bottom row (C3)
    double scrollBeat = 0.0, pixelsPerBeat = 0.0;   // 0 = fit on next layout
    std::vector<model::NoteKey> selection;
    int hoverNote = -1;
    double lastNoteLength = 0.5;
    int lastPlayheadX = -1;

    Drag drag = Drag::none;
    juce::Point<int> dragStart, dragCurrent;
    engine::MidiSequence dragBase;              // the sequence when the drag began
    std::vector<int> dragIndices;               // indices into dragBase
    engine::NoteEvent dragAnchor;               // the note that was clicked
    bool dragChanged = false;
    int velocityTarget = -1;

    int defaultVelocity = 100, softVelocity = 70;
    juce::ComboBox presetBox;
    juce::Label presetLabel { {}, "Sound" };
    engine::InstrumentType presetType = engine::InstrumentType::none;
    static inline std::vector<engine::NoteEvent> clipboard;
};

} // namespace beatmaker::ui
