// PianoRoll: the note editor for one MidiClip, drawn on the session
// timeline. By default its time axis is linked to the track area, so the
// notes sit under their clip and scrolling or zooming either view moves
// both; Link off gives it a view of its own. A looping clip shows every
// repeat of its sequence; the repeats after the first are ghosts, and the
// first edit inside a ghost unrolls the loop so each bar becomes
// independent. It shares the edit window's settings (mode, tool, grid,
// relative grid, Commands Focus): Grid snaps, Slip is free, Spot types a
// position; the Zoomer zooms, the Trimmer drags either edge, the Selector
// rubber-bands, the Grabber moves, the Pencil adds and paints velocities,
// the Smart Tool does the right one by where you click. Multi-selection, a
// velocity lane, and the same keyboard vocabulary as the tracks. Every edit
// is one undo step through onSequenceChanged.
#pragma once

#include "../shared/EditSettings.h"
#include "../shared/EditorPanel.h"
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
                        public EditorPanel,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
public:
    PianoRoll (model::Session& session, engine::Transport& transport, engine::AudioGraph& graph);

    void setEditSettings (EditSettings* s) { settings = s; }
    void setTarget (int trackIndex, int midiClipIndex);
    void setDefaultVelocities (int normal, int soft) { defaultVelocity = juce::jlimit (1, 127, normal); softVelocity = juce::jlimit (1, 127, soft); }
    // Vertical zoom (pixels per semitone), independent of the tracks' zoom.
    void setRowHeight (int pixels);
    int getRowHeight() const noexcept { return rowHeight; }
    std::function<void (int rowHeight)> onRowHeightChanged;
    int getTrackIndex() const noexcept { return trackIndex; }
    bool hasTarget() const override { return getSequence() != nullptr; }

    // Editing commands (also reachable from the palette when the notes have focus)
    bool hasSelection() const override { return ! selection.empty(); }
    int numSelected() const noexcept { return (int) selection.size(); }
    void selectAll() override;
    void clearSelection() override;
    void deleteSelection() override;
    void copySelection() override;
    void cutSelection() override;
    bool canPaste() const override;
    void pasteAtInsertion() override;
    void duplicateSelection() override;
    void quantizeSelection (bool lengthsToo) override;
    void transposeSelection (int semitones) override;
    void nudgeBeats (double beats);
    void nudgeSelection (int gridSteps) override { nudgeBeats (gridSteps * gridBeats()); }
    void changeVelocity (int delta) override;
    void legatoSelection() override;
    void selectNextNote (bool forward);
    void zoomBy (double factor) override;
    void zoomToFit() override;
    juce::String describeSelection() const override;
    juce::String keyHint() const override
    {
        return "NOTES: Up/Down transpose (Shift octave)   Ctrl+Up/Down velocity   Left/Right nudge   Alt+Q quantize   Ctrl+L legato   Ctrl+D duplicate   Tab next note   double-click: spot";
    }

    std::function<void (int trackIndex, int clipIndex, std::shared_ptr<const engine::MidiSequence>, juce::String)> onSequenceChanged;
    std::function<void (int trackIndex, int presetIndex)> onPresetChanged;
    using View = TimelineView;   // viewSource / onViewChanged / onStatus come from EditorPanel
    // A note added past the clip's end: the app extends the clip (samples at the clip's rate).
    std::function<void (int trackIndex, int clipIndex, juce::int64 newLengthSamples)> onClipExtend;
    std::function<void (int trackIndex, int clipIndex, bool loop)> onClipLoopChanged;
    bool clipLoops() const;   // the clip repeats its sequence
    bool isLinked() const override { return linked; }
    void setLinked (bool) override;
    void unrollLoop() override;   // one sequence as long as the clip, every repeat written out

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
    int rowHeight = 14;                             // pixels per semitone (vertical zoom)
    static constexpr int velocityHeight = 56;
    static constexpr int edgeGrab      = 6;

    enum class Drag { none, pending, move, resizeEnd, resizeStart, band, velocity, zoomBand };
    struct Instance { int note = -1; int repeat = 0; };   // a drawn note: sequence note index and loop repeat

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

    // Timeline geometry: x <-> session seconds <-> session beats
    View view() const;
    int originX() const;
    double secondsAtX (int x) const;
    float xForSeconds (double s) const;
    double clipStartBeats() const;
    double clipLengthBeats() const;
    double loopOffsetBeats() const;
    int firstRepeat() const;                       // index of the repeat that starts the clip
    double repeatStartBeats (int repeat) const;    // timeline beats
    double sequenceBeatAt (double timelineBeat) const;   // wrapped into the sequence
    bool isGhost (int repeat) const { return repeat != firstRepeat(); }
    juce::Rectangle<float> instanceRect (const engine::NoteEvent&, int repeat) const;
    Instance instanceAt (juce::Point<int>) const;
    void repeatsInView (int& first, int& last) const;
    juce::Rectangle<int> gridBounds() const;
    juce::Rectangle<int> velocityBounds() const;
    juce::Rectangle<int> rulerBounds() const;
    int visibleRows() const;
    int pitchAtY (int y) const;
    double beatAtX (int x) const;                  // timeline beats
    float xForBeat (double beat) const;
    int yForPitch (int pitch) const;
    double sequenceLength() const;
    void applyView (double startSeconds, double pixelsPerSecond);
    int noteAt (juce::Point<int> p) const { return instanceAt (p).note; }
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
    void addNoteAt (int pitch, double timelineBeat, bool soft, bool startResize, juce::Point<int> at);
    bool unrollIfGhost (int repeat);               // returns true when the loop was unrolled
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
    bool linked = true;
    double ownStartSeconds = 0.0, ownPixelsPerSecond = 0.0;   // when not linked (0 = fit the clip)
    std::vector<model::NoteKey> selection;
    int hoverNote = -1;
    double lastNoteLength = 0.5;
    int lastPlayheadX = -1;
    double lastViewStart = -1.0, lastViewPps = -1.0, lastViewOrigin = -1.0;

    Drag drag = Drag::none;
    juce::Point<int> dragStart, dragCurrent;
    engine::MidiSequence dragBase;              // the sequence when the drag began
    std::vector<int> dragIndices;               // indices into dragBase
    engine::NoteEvent dragAnchor;               // the note that was clicked
    int dragRepeat = 0;
    bool dragChanged = false;
    int velocityTarget = -1;

    int defaultVelocity = 100, softVelocity = 70;
    juce::ComboBox presetBox;
    juce::Label presetLabel { {}, "Sound" };
    juce::TextButton linkButton { "Link" }, loopButton { "Loop" }, unrollButton { "Unroll" }, rowsSmaller { "-" }, rowsBigger { "+" };
    juce::ScrollBar pitchScroll { true };
    bool draggingKeyboard = false; int keyboardDragStartPitch = 0;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void syncScrollBar();
    void scrollToPitch (int lowest);
    engine::InstrumentType presetType = engine::InstrumentType::none;
    static inline std::vector<engine::NoteEvent> clipboard;
};

} // namespace beatmaker::ui
