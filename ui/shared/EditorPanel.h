// EditorPanel: what the editor panel's editors (note editor, step sequencer)
// have in common, so keyboard focus, the Edit and View commands and the
// timeline link treat them alike.
#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace beatmaker::ui
{

// The track area's time axis, mirrored by an editor: the lane origin in the
// editor's own coordinates, the view start, the zoom and the lane width.
struct TimelineView { double originX = 0.0, startSeconds = 0.0, pixelsPerSecond = 60.0, laneWidth = 800.0; };

class EditorPanel
{
public:
    virtual ~EditorPanel() = default;
    virtual bool hasTarget() const = 0;
    virtual bool hasSelection() const = 0;
    virtual void selectAll() = 0;
    virtual void clearSelection() = 0;
    virtual void deleteSelection() = 0;
    virtual void copySelection() = 0;
    virtual void cutSelection() = 0;
    virtual bool canPaste() const = 0;
    virtual void pasteAtInsertion() = 0;
    virtual void duplicateSelection() = 0;
    virtual void nudgeSelection (int gridSteps) = 0;      // +/- one grid (notes) or one step (drums)
    virtual void transposeSelection (int amount) = 0;     // semitones, or pad rows for drums
    virtual void changeVelocity (int delta) = 0;
    virtual void quantizeSelection (bool lengthsToo) = 0; // no-op for drums
    virtual void legatoSelection() = 0;                   // no-op for drums
    virtual void zoomBy (double factor) = 0;
    virtual void zoomToFit() = 0;
    virtual void unrollLoop() = 0;
    virtual bool isLinked() const = 0;
    virtual void setLinked (bool) = 0;
    virtual juce::String describeSelection() const = 0;
    virtual juce::String keyHint() const = 0;             // for the toolbar

    std::function<TimelineView()> viewSource;
    std::function<void (double startSeconds, double pixelsPerSecond)> onViewChanged;
    std::function<void (const juce::String&)> onStatus;
};

} // namespace beatmaker::ui
