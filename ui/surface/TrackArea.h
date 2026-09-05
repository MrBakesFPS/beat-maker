// TrackArea: ruler, track headers, clip lanes with waveforms, and the
// playhead. Reads the Session; all edits go back through callbacks so the
// owner can issue Commands.
#pragma once

#include "../shared/Theme.h"
#include <Session.h>
#include <transport/Transport.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>

namespace beatmaker::ui
{

class TrackArea final : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public juce::DragAndDropTarget,
                        private juce::Timer,
                        private juce::ChangeListener,
                        private model::Session::Listener
{
public:
    TrackArea (model::Session& session, engine::Transport& transport, juce::AudioFormatManager& formatManager);
    ~TrackArea() override;

    // (files, trackIndex or -1 for "new track", timeline position in seconds)
    std::function<void (const juce::StringArray&, int, double)> onFilesDropped;
    // (loop file, trackIndex or -1 for new track, timeline seconds) from the Library
    std::function<void (const juce::File&, int, double)> onLoopDropped;
    std::function<void (int trackIndex, bool mute)> onMuteChanged;
    std::function<void (int trackIndex, bool solo)> onSoloChanged;
    std::function<void (int trackIndex, bool armed)> onArmChanged;
    std::function<void (int trackIndex, bool monitor)> onMonitorChanged;
    std::function<void (int trackIndex, int firstInput, int numInputs)> onInputChanged;

    // Device inputs offered in each audio track's input selector.
    void setInputChannelNames (const juce::StringArray& names);

    // Live recording display: the recorder streams into the returned
    // thumbnail; the lane draws it from `recordStartSeconds` to the playhead.
    juce::AudioThumbnail& createLiveThumbnail (int trackId);
    void clearLiveThumbnails();
    std::function<double()> getRecordStartSeconds;   // -1 when not yet started
    std::function<void (model::Track::Type, model::Track::InstrumentKind)> onAddTrack;
    std::function<void (int trackIndex)> onSelectionChanged;

    int getSelectedTrack() const noexcept { return selectedTrack; }
    void setSelectedTrack (int index);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    struct TrackControls
    {
        std::unique_ptr<juce::TextButton> mute, solo, arm, monitor;
        std::unique_ptr<juce::ComboBox> input;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void sessionChanged (model::Session&) override;

    void rebuildTrackControls();
    juce::AudioThumbnail& thumbnailFor (const model::AudioClip& clip);

    // Geometry
    juce::Rectangle<int> getRulerBounds() const;
    juce::Rectangle<int> getLaneBounds (int trackIndex) const;
    juce::Rectangle<int> getHeaderBounds (int trackIndex) const;
    int trackIndexAtY (int y) const;
    float secondsToX (double seconds) const;
    double xToSeconds (float x) const;
    void ensurePlayheadVisible();

    void paintRuler (juce::Graphics&, juce::Rectangle<int>);
    void paintHeader (juce::Graphics&, const model::Track&, int index, juce::Rectangle<int>);
    void paintLane (juce::Graphics&, const model::Track&, juce::Rectangle<int>);
    void paintAudioClip (juce::Graphics&, const model::Track&, const model::AudioClip&, juce::Rectangle<int> lane);
    void paintPatternClip (juce::Graphics&, const model::Track&, const model::PatternClip&, juce::Rectangle<int> lane);
    void paintMidiClip (juce::Graphics&, const model::Track&, const model::MidiClip&, juce::Rectangle<int> lane);
    void paintLiveRecording (juce::Graphics&, const model::Track&, juce::Rectangle<int> lane);
    juce::Rectangle<float> clipRectFor (double startSeconds, double endSeconds, juce::Rectangle<int> lane) const;
    void paintClipFrame (juce::Graphics&, juce::Rectangle<float>, const model::Track&, const juce::String& name);

    model::Session& session;
    engine::Transport& transport;
    juce::AudioFormatManager& formatManager;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    std::vector<TrackControls> trackControls;
    juce::StringArray inputNames;
    std::map<int, std::unique_ptr<juce::AudioThumbnail>> liveThumbnails;
    juce::TextButton addTrackButton { "+ Track" };

    int selectedTrack = -1;
    double pixelsPerSecond = 60.0;
    double viewStartSeconds = 0.0;
    bool dragHover = false;
};

} // namespace beatmaker::ui
