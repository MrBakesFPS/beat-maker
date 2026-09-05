// TrackArea: ruler, track headers, clip lanes with waveforms, and the
// playhead. Reads the Session; all edits go back through callbacks so the
// owner can issue Commands.
#pragma once

#include "../shared/EditSettings.h"
#include "../shared/Theme.h"
#include <ClipEdits.h>
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
    TrackArea (model::Session& session, engine::Transport& transport, juce::AudioFormatManager& formatManager,
               EditSettings& editSettings);
    ~TrackArea() override;

    // ---- Editing state ----
    struct TimeSelection
    {
        double start = 0.0, end = 0.0;     // seconds
        int firstTrack = -1, lastTrack = -1;
        bool isValid() const noexcept { return end > start; }
    };
    const TimeSelection& getTimeSelection() const noexcept { return timeSelection; }
    const std::vector<model::ClipRef>& getSelectedClips() const noexcept { return selectedClips; }
    void clearSelection();
    std::function<void()> onTimeSelectionChanged;

    // Edit operations on the current selection (bound to keys by the owner)
    void deleteSelection();
    void separateAtPlayhead();
    void duplicateSelectedClips();
    void nudgeSelectedClips (int direction);
    void nudgeClipGain (float deltaDb);
    void zoomToFit();

    // Fades window support
    struct FadeValues { double fadeInMs = 0.0, fadeOutMs = 0.0; engine::FadeShape inShape = engine::FadeShape::linear, outShape = engine::FadeShape::linear; float gainDb = 0.0f; };
    std::optional<FadeValues> currentFadeValues() const;           // from the first selected audio clip
    void applyFadesToSelection (const FadeValues&);
    int numSelectedAudioClips() const;

    bool keyPressed (const juce::KeyPress&) override;

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
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
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
    void paintEditOverlays (juce::Graphics&);

    // ---- Edit helpers ----
    enum class Drag { none, move, trimStart, trimEnd, select, zoomRange, fadeIn, fadeOut, clipGain };
    EditSettings::Tool effectiveTool (const juce::MouseEvent&, const std::optional<model::ClipRef>& hit, bool& nearStart, bool& nearEnd) const;
    std::optional<model::ClipRef> clipAtPoint (juce::Point<int>) const;
    juce::Rectangle<float> rectForClip (const model::ClipRef&) const;
    double snapSeconds (double seconds) const;
    double snapDelta (double deltaSeconds) const;
    double gridSeconds() const;
    juce::int64 toSamples (double seconds) const;
    bool isSelected (const model::ClipRef&) const;
    void selectClip (const model::ClipRef&, bool addToSelection);
    void commitDrag();
    void executeWithShuffle (std::unique_ptr<model::Command>, const juce::String& name, std::initializer_list<int> tracksToRepack);
    void showSpotDialog (const model::ClipRef&);
    void setTimeSelection (TimeSelection);
    void updateCursor (const juce::MouseEvent&);
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

    EditSettings& edit;
    int selectedTrack = -1;
    std::vector<model::ClipRef> selectedClips;
    TimeSelection timeSelection;

    Drag drag = Drag::none;
    model::ClipRef dragClip;
    model::ClipTiming dragOriginal;
    juce::Point<int> dragStartPoint;
    int dragTargetTrack = -1;
    double ghostStart = 0.0, ghostLength = 0.0;   // seconds
    double ghostFadeSeconds = 0.0;
    float ghostGainDb = 0.0f;
    bool dragMoved = false;
    double dragAnchorSeconds = 0.0;

    double pixelsPerSecond = 60.0;
    double viewStartSeconds = 0.0;
    bool dragHover = false;
};

} // namespace beatmaker::ui
