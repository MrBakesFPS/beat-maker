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
    // Shows every pattern clip of the track; `patternClipIndex` picks the active one (-1 keeps the current). Track -1 shows nothing.
    void setTarget (int trackIndex, int patternClipIndex = -1);
    int getActiveClip() const noexcept { return clipIndex; }
    std::function<int (int trackIndex, juce::int64 startSamples, juce::int64 lengthSamples)> onClipCreate;
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
    void nudgeFine (int quarterSteps);                // Shift+Left/Right: a quarter step, as micro-timing
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
        return "DRUMS: click toggles (Shift soft), drag empty paints, drag a hit moves it, its right edge holds it (a gate)   Selector: band   Up/Down other pad   Ctrl+Up/Down velocity   Left/Right nudge (Shift: a quarter step)   Ctrl+D duplicate   Del clear";
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

    static constexpr int edgeGrab       = 6;

    enum class Drag { none, paint, band, pendingMove, move, resize, velocity, zoomBand };
    struct Cell { int pad = -1, step = -1, repeat = 0, clip = -1; double pos = 0.0; };   // a drawn cell: clip, pattern step, loop repeat, and the exact position in steps

    void timerCallback() override;
    const model::Track* getTrack() const;
    const model::PatternClip* clipAt (int ci) const;
    const model::PatternClip* getClip() const { return clipAt (clipIndex); }
    const engine::StepPattern* patternOf (int ci) const;
    const engine::StepPattern* getPattern() const { return patternOf (clipIndex); }
    int numClips() const;
    int clipIndexAtBeat (double timelineBeat) const;
    void setActiveClip (int ci);
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
    double clipStartBeats (int ci) const;
    double clipLengthBeats (int ci) const;
    double loopOffsetBeats (int ci) const;
    double stepBeats (int ci) const;
    double patternBeats (int ci) const;
    double repeatStartBeats (int ci, int repeat) const;
    int firstRepeat (int ci) const;
    bool clipLoopsAt (int ci) const { auto* c = clipAt (ci); return c != nullptr && c->loop; }
    void repeatsInView (int ci, int& first, int& last) const;
    double clipStartBeats() const { return clipStartBeats (clipIndex); }
    double clipLengthBeats() const { return clipLengthBeats (clipIndex); }
    double loopOffsetBeats() const { return loopOffsetBeats (clipIndex); }
    double stepBeats() const { return stepBeats (clipIndex); }
    double repeatStartBeats (int repeat) const { return repeatStartBeats (clipIndex, repeat); }
    int firstRepeat() const { return firstRepeat (clipIndex); }
    bool isGhost (int repeat) const { return repeat != firstRepeat(); }
    void repeatsInView (int& first, int& last) const { repeatsInView (clipIndex, first, last); }
    int padAt (int y) const;
    Cell cellAt (juce::Point<int>) const;                    // step within the clip, even where no hit
    juce::Rectangle<float> cellRect (int ci, int pad, int step, int repeat) const;
    juce::Rectangle<float> cellRect (int pad, int step, int repeat) const { return cellRect (clipIndex, pad, step, repeat); }
    juce::Rectangle<float> hitRect (int ci, int pad, int step, int repeat) const;   // the hit's held span, cut at the clip's end
    Cell hitAt (juce::Point<int>) const;                     // the start cell of the hit whose span covers the point (step -1 when silent)
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
    int dragAnchorLength = 1;
    bool dragChanged = false;
    std::uint8_t paintValue = 0;
    int lastPaintPad = -1, lastPaintStep = -1;
    model::StepEdits::Cells strokeCells;

    juce::TextButton linkButton { "Link" }, loopButton { "Loop" }, unrollButton { "Unroll" };
};

} // namespace beatmaker::ui
