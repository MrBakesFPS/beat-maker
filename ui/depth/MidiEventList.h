// MidiEventList: Pro Tools-style MIDI Event List for the selected instrument
// track (every note of its MIDI clips in time order, editable in place) with
// the track's Real-Time Properties above it.
#pragma once

#include "../shared/Theme.h"
#include <Session.h>
#include <transport/Transport.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class MidiEventList final : public juce::Component,
                            private juce::TableListBoxModel,
                            private model::Session::Listener
{
public:
    MidiEventList (model::Session&, engine::Transport&);
    ~MidiEventList() override;

    void setTrack (int trackIndex);
    int getTrack() const noexcept { return trackIndex; }
    std::function<void()> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 640, preferredHeight = 480;

private:
    struct Row { int clipIndex = 0, noteIndex = 0; double absoluteBeat = 0.0; engine::NoteEvent note; juce::String clipName; };
    enum Columns { colIndex = 1, colStart, colNote, colVelocity, colLength, colClip };

    void rebuild();
    void rebuildPropsControls();
    int getNumRows() override { return (int) rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int columnId, bool selected, juce::Component* existing) override;
    void cellClicked (int row, int columnId, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int columnId, const juce::MouseEvent&) override;
    void deleteKeyPressed (int row) override;
    void sessionChanged (model::Session&) override { rebuild(); }
    void applyEdit (int row, int columnId, const juce::String& text);
    void insertNoteAtPlayhead();
    void deleteSelectedNotes();
    void applyProps();
    juce::String barBeatTick (double absoluteBeat) const;

    model::Session& session;
    engine::Transport& transport;
    int trackIndex = -1;
    std::vector<Row> rows;
    juce::TableListBox table;
    juce::TextButton insertButton { "Insert Note at Playhead" }, deleteButton { "Delete" };
    juce::Label title;

    // Real-time properties
    juce::ToggleButton quantizeToggle { "Quantize" };
    juce::ComboBox quantizeGrid;
    juce::Slider strength, transpose, velocityScale, velocityOffset, delay, duration;
    juce::Label strengthLabel { {}, "Strength" }, transposeLabel { {}, "Transpose" }, velScaleLabel { {}, "Vel %" }, velOffsetLabel { {}, "Vel +" }, delayLabel { {}, "Delay ms" }, durationLabel { {}, "Duration %" };
    bool syncingProps = false;
};

} // namespace beatmaker::ui
