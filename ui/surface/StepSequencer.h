// StepSequencer: 16 pads x N steps grid editing one PatternClip. Click to
// toggle a step, Shift-click for a soft hit, drag to paint. Click a pad name
// to audition it. Drop an audio file on a pad row to replace its sample.
#pragma once

#include "../shared/Theme.h"
#include <Session.h>
#include <graph/AudioGraph.h>
#include <transport/Transport.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class StepSequencer final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    StepSequencer (model::Session& session, engine::Transport& transport, engine::AudioGraph& graph);

    // Which pattern clip to edit; pass -1 to show nothing.
    void setTarget (int trackIndex, int patternClipIndex);
    int getTrackIndex() const noexcept { return trackIndex; }

    std::function<void (int trackIndex, int clipIndex, int pad, int step, std::uint8_t velocity)> onStepChanged;
    std::function<void (int trackIndex, int pad, const juce::File&)> onPadSampleDropped;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int, int y) override { dragPad = padAt (y); repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragPad = -1; repaint(); }

private:
    static constexpr int padColumnWidth = 130;
    static constexpr int headerHeight   = 22;
    static constexpr std::uint8_t fullVelocity = 100;
    static constexpr std::uint8_t softVelocity = 55;

    void timerCallback() override;
    const model::PatternClip* getClip() const;
    const model::Track* getTrack() const;
    int getNumSteps() const;
    int currentStep() const;

    juce::Rectangle<int> gridBounds() const;
    int padAt (int y) const;
    int stepAt (int x) const;
    juce::Rectangle<float> cellBounds (int pad, int step) const;
    void applyPaint (int pad, int step);

    model::Session& session;
    engine::Transport& transport;
    engine::AudioGraph& graph;

    int trackIndex = -1, clipIndex = -1;
    int lastPlayheadStep = -1;
    int dragPad = -1;

    // Paint state during a drag
    bool painting = false;
    std::uint8_t paintValue = 0;
    int lastPaintPad = -1, lastPaintStep = -1;
};

} // namespace beatmaker::ui
