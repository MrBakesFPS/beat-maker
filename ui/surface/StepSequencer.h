// StepSequencer: the drum editor for one PatternClip, drawn on the session
// timeline like the note editor. Linked to the tracks' view by default (the
// cells sit under their clip; scrolling or zooming either view moves both),
// a looping clip shows every repeat with the later ones as ghosts, and the
// first edit in a ghost, or Unroll, writes the loop out so each bar is its
// own. Click to toggle a step (Shift for a soft hit), drag to paint; the
// Selector rubber-bands cells, the Grabber moves them, the Zoomer zooms; a
// velocity lane edits the hits of the pad you last touched. The keyboard
// vocabulary matches the tracks and the note editor. Click a pad name to
// audition it; drop an audio file on a pad row to replace its sample.
#pragma once

#include "../shared/EditSettings.h"
#include "../shared/EditorPanel.h"
#include "../shared/Theme.h"
#include <StepEdits.h>
#include <Session.h>
#include <graph/AudioGraph.h>
#include <transport/Transport.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class StepSequencer final : public juce::Component,
                            public EditorPanel,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    StepSequencer (model::Session& session, engine::Transport& transport, engine::AudioGraph& graph);

    void setEditSettings (EditSettings* s) { settings = s; }
    void setTarget (int trackIndex, int patternClipIndex);   // -1 shows nothing
    int getTrackIndex() const noexcept { return trackIndex; }

    std::function<void (int trackIndex, int clipIndex, std::shared_ptr<const engine::StepPattern>, juce::String)> onPatternChanged;
    std::function<void (int trackIndex, int pad, const juce::File&)> onPadSampleDropped;
    std::function<void (int trackIndex, int clipIndex, juce::int64 newLengthSamples)> onClipExtend;
    std::function<void (int trackIndex, int clipIndex, bool loop)> onClipLoopChanged;
    bool clipLoops() const { auto* c = getClip(); return c != nullptr && c->loop; }

    // EditorPanel
    bool hasTarget() const override { return getPattern() != nullptr; }
    bool hasSelection() const override { return ! selection.empty(); }
    void selectAll() override;
    void clearSelection() override;
    void deleteSelection() override;
    void copySelection() override;
    void cutSelection() override;
    bool canPaste() const override;
    void pasteAtInsertion() override;
    void duplicateSelection() override;
    void nudgeSelection (int steps) override;
    void transposeSelection (int pads) override;      // moves the cells to other pads
    void changeVelocity (int delta) override;
    void quantizeSelection (bool) override {}
    void legatoSelection() override {}
    void zoomBy (double factor) override;
    void zoomToFit() override;
    void unrollLoop() override;
    bool isLinked() const override { return linked; }
    void setLinked (bool) override;
    juce::String describeSelection() const override;
    juce::String keyHint() const override
    {
        return "DRUMS: click toggles (Shift soft), drag paints   Selector: band   Up/Down other pad   Ctrl+Up/Down velocity   Left/Right nudge   Ctrl+D duplicate   Del clear";
    }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int, int y) override { dragPad = padAt (y); repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragPad = -1; repaint(); }

private:
    static constexpr int padColumnWidth = 120;
    static constexpr int headerHeight   = 26;
    static constexpr int rulerHeight    = 18;
    static constexpr int velocityHeight = 48;
    static constexpr std::uint8_t fullVelocity = 100;
    static constexpr std::uint8_t softVelocity = 55;

    enum class Drag { none, paint, band, move, velocity, zoomBand };
    struct Cell { int pad = -1, step = -1, repeat = 0; };   // a drawn cell: pattern step and loop repeat

    void timerCallback() override;
    const model::PatternClip* getClip() const;
    const model::Track* getTrack() const;
    const engine::StepPattern* getPattern() const;
    EditSettings::Tool tool() const noexcept { return settings != nullptr ? settings->tool : EditSettings::Tool::smart; }

    // Timeline geometry
    TimelineView view() const;
    int originX() const;
    double secondsAtX (int x) const;
    float xForSeconds (double s) const;
    double beatAtX (int x) const;
    float xForBeat (double beat) const;
    void applyView (double startSeconds, double pixelsPerSecond);
    juce::Rectangle<int> gridBounds() const;
    juce::Rectangle<int> velocityBounds() const;
    juce::Rectangle<int> rulerBounds() const;
    double clipStartBeats() const;
    double clipLengthBeats() const;
    double loopOffsetBeats() const;
    double stepBeats() const;                                // beats per step
    double repeatStartBeats (int repeat) const;
    int firstRepeat() const;
    bool isGhost (int repeat) const { return repeat != firstRepeat(); }
    void repeatsInView (int& first, int& last) const;
    int padAt (int y) const;
    Cell cellAt (juce::Point<int>) const;                    // step within the clip, even where no hit
    juce::Rectangle<float> cellRect (int pad, int step, int repeat) const;
    int stepAtTimelineBeat (double beat, int& repeat) const;
    int insertionStep() const;

    // Editing
    void commit (engine::StepPattern updated, const juce::String& action);
    void liveCommit (engine::StepPattern updated, const juce::String& action);
    bool unrollIfGhost (int repeat);
    bool extendClipTo (double timelineBeat);                 // grows the clip and pattern to cover the beat
    void status (const juce::String& s) { if (onStatus) onStatus (s); }
    juce::String barBeatTick (double beat) const;

    model::Session& session;
    engine::Transport& transport;
    engine::AudioGraph& graph;
    EditSettings* settings = nullptr;

    int trackIndex = -1, clipIndex = -1;
    bool linked = true;
    double ownStartSeconds = 0.0, ownPixelsPerSecond = 0.0;
    int lastPlayheadX = -1;
    double lastViewStart = -1.0, lastViewPps = -1.0, lastViewOrigin = -1.0;
    int dragPad = -1;
    int focusPad = 0;                                        // whose hits the velocity lane shows
    model::StepEdits::Cells selection;
    static inline std::vector<model::StepHit> clipboard;

    Drag drag = Drag::none;
    juce::Point<int> dragStart, dragCurrent;
    engine::StepPattern dragBase;
    model::StepEdits::Cells dragCells;
    Cell dragAnchor;
    bool dragChanged = false;
    std::uint8_t paintValue = 0;
    int lastPaintPad = -1, lastPaintStep = -1;
    model::StepEdits::Cells strokeCells;

    juce::TextButton linkButton { "Link" }, loopButton { "Loop" }, unrollButton { "Unroll" };
};

} // namespace beatmaker::ui
