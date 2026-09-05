// PianoRoll: edits one MidiClip's sequence. Keyboard on the left (click to
// audition), beat grid on the right. Click empty space to add a note, drag a
// note to move it, drag its right edge to resize, right-click (or Delete) to
// remove. Every edit produces a new immutable sequence via onSequenceChanged.
#pragma once

#include "../shared/Theme.h"
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

    void setTarget (int trackIndex, int midiClipIndex);
    int getTrackIndex() const noexcept { return trackIndex; }

    std::function<void (int trackIndex, int clipIndex, std::shared_ptr<const engine::MidiSequence>, juce::String)> onSequenceChanged;
    std::function<void (int trackIndex, int presetIndex)> onPresetChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    static constexpr int keyboardWidth = 56;
    static constexpr int headerHeight  = 26;   // preset bar
    static constexpr int rulerHeight   = 18;
    static constexpr int rowHeight     = 14;
    static constexpr double gridBeats  = 0.25; // 16th

    enum class Drag { none, move, resize };

    void timerCallback() override;
    const model::Track* getTrack() const;
    const model::MidiClip* getClip() const;
    const engine::MidiSequence* getSequence() const;

    juce::Rectangle<int> gridBounds() const;
    int visibleRows() const;
    int pitchAtY (int y) const;
    double beatAtX (int x) const;
    float xForBeat (double beat) const;
    int yForPitch (int pitch) const;
    juce::Rectangle<float> noteRect (const engine::NoteEvent&) const;
    int noteAt (juce::Point<int>) const;
    static double snap (double beat) { return std::round (beat / gridBeats) * gridBeats; }
    static bool isBlackKey (int pitch) { const int n = pitch % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

    void commit (engine::MidiSequence updated, const juce::String& action);
    void audition (int pitch, float velocity = 0.8f);
    void refreshPresetBox();

    model::Session& session;
    engine::Transport& transport;
    engine::AudioGraph& graph;

    int trackIndex = -1, clipIndex = -1;
    int lowestPitch = 48;         // bottom row (C3)
    int selectedNote = -1;
    int hoverNote = -1;
    double lastNoteLength = 0.5;
    int lastPlayheadX = -1;

    Drag drag = Drag::none;
    engine::NoteEvent dragOriginal;
    juce::Point<int> dragStart;
    bool dragChanged = false;

    juce::ComboBox presetBox;
    juce::Label presetLabel { {}, "Sound" };
    engine::InstrumentType presetType = engine::InstrumentType::none;   // what presetBox currently lists
};

} // namespace beatmaker::ui
