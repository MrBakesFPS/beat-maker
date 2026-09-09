// TrackArea: ruler, track headers, clip lanes with waveforms, and the
// playhead. Reads the Session; all edits go back through callbacks so the
// owner can issue Commands.
#pragma once

#include "../shared/EditSettings.h"
#include "../shared/Theme.h"
#include <ClipEdits.h>
#include <Session.h>
#include <graph/AudioGraph.h>
#include <transport/Transport.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <set>

namespace beatmaker::ui
{

class TrackArea final : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public juce::DragAndDropTarget,
                        private juce::Timer,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener,
                        private model::Session::Listener
{
public:
    TrackArea (model::Session& session, engine::Transport& transport, engine::AudioGraph& graph,
               juce::AudioFormatManager& formatManager, EditSettings& editSettings);
    ~TrackArea() override;

    // ---- Editing state ----
    struct TimeSelection
    {
        double start = 0.0, end = 0.0;     // seconds
        int firstTrack = -1, lastTrack = -1;
        int playlistTrack = -1, playlistIndex = -1;   // set when the selection was made on an alternate lane
        bool isValid() const noexcept { return end > start; }
        bool isOnAlternate() const noexcept { return playlistTrack >= 0 && playlistIndex >= 0; }
    };
    const TimeSelection& getTimeSelection() const noexcept { return timeSelection; }
    void setTimeSelectionSeconds (double start, double end, int trackIndex);   // e.g. punch range from the command line

    // Memory locations / arrangement sections (the strip above the ruler)
    void recallMarker (int markerId);
    void addMarkerAtPlayhead (bool asSectionFromSelection = false);
    void showMarkerMenu (int markerId, double seconds, juce::Point<int> screenPos);
    void getView (double& viewStart, double& pixelsPerSec) const { viewStart = viewStartSeconds; pixelsPerSec = pixelsPerSecond; }
    void setView (double viewStart, double pixelsPerSec) { viewStartSeconds = juce::jmax (0.0, viewStart); pixelsPerSecond = juce::jlimit (5.0, 400000.0, pixelsPerSec); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    const std::vector<model::ClipRef>& getSelectedClips() const noexcept { return selectedClips; }
    // Beat Detective target: the selected audio clips, else the selected track's clips inside the
    // time selection, else all of the selected track's audio clips.
    std::vector<model::ClipRef> clipsForRhythmEditing() const;
    std::function<void()> onOpenBeatDetective;
    std::function<void()> onOpenMemoryLocations;
    // Track header menu: action is one of freeze / unfreeze / commit / rename / delete
    std::function<void (int trackIndex, const juce::String& action)> onTrackAction;
    void clearSelection();
    std::function<void()> onTimeSelectionChanged;

    // Edit operations on the current selection (bound to keys by the owner)
    void deleteSelection();
    void separateAtPlayhead();
    void duplicateSelectedClips();
    void nudgeSelectedClips (int direction);
    void nudgeClipGain (float deltaDb);
    void zoomToFit();
    void zoomBy (double factor);          // around the playhead
    void zoomToSelection();               // the time selection, else everything
    void selectAllClips();

    // Commands Keyboard Focus edits
    void trimSelectionToPlayhead (bool start);                                     // A / S
    void fadeSelectionToPlayhead (bool fadeIn, engine::FadeShape);                  // D / G
    void applyDefaultFadesToSelection (double ms, engine::FadeShape);               // F
    void cutSelection();
    void copySelection();
    void pasteAtPlayhead (bool selectPasted);
    bool canPaste() const noexcept { return ! clipboard.empty(); }
    bool hasSelection() const noexcept { return ! selectedClips.empty(); }
    void quantizeSelectionPublic() { quantizeSelection(); }

    // Display preferences
    void setTrackHeight (int pixels);
    int getTrackHeight() const noexcept { return trackHeight; }
    // Vertical scrolling of the track list (the ruler stays put).
    static constexpr int scrollBarWidth = 12;
    int getLaneWidth() const noexcept { return getWidth() - theme::trackHeaderWidth - scrollBarWidth; }
    int getScrollY() const noexcept { return scrollY; }
    void setScrollY (int pixels);
    void ensureTrackVisible (int trackIndex);

    // Playlists: copy the time selection made on an alternate lane into the main playlist.
    void compSelectionToMain();
    void setPlaylistsShown (int trackIndex, bool shown);
    bool arePlaylistsShown (const model::Track&) const;

    // Fades window support
    struct FadeValues { double fadeInMs = 0.0, fadeOutMs = 0.0; engine::FadeShape inShape = engine::FadeShape::linear, outShape = engine::FadeShape::linear; float gainDb = 0.0f; };
    std::optional<FadeValues> currentFadeValues() const;           // from the first selected audio clip
    void applyFadesToSelection (const FadeValues&);
    int numSelectedAudioClips() const;

    bool keyPressed (const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    juce::String describeSelection() const;   // what a screen reader hears for the current selection
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void selectTrackByOffset (int delta);     // Up/Down
    void selectTrayByOffsetPublic (int delta) { selectTrackByOffset (delta); }
    void stepPlayhead (int direction, bool extendSelection);   // Left/Right by the grid

    // (files, trackIndex or -1 for "new track", timeline position in seconds)
    std::function<void (const juce::StringArray&, int, double)> onFilesDropped;
    // (loop file, trackIndex or -1 for new track, timeline seconds) from the Library
    std::function<void (const juce::File&, int, double)> onLoopDropped;
    std::function<void (int trackIndex, bool mute)> onMuteChanged;
    std::function<void (int trackIndex, bool solo)> onSoloChanged;
    std::function<void (int trackIndex, bool armed)> onArmChanged;
    std::function<void (int trackIndex, bool monitor)> onMonitorChanged;
    std::function<void (int trackIndex, int firstInput, int numInputs)> onInputChanged;
    std::function<void (int trackIndex, int inputPath)> onInputPathChanged;
    std::function<void (int trackIndex, model::AutomationMode)> onAutomationModeChanged;
    void showAutomationLane (int trackIndex, const engine::ParamId&);
    void showClipGainView (int trackIndex);
    std::function<double()> getPlayheadSeconds;   // for automation value readouts

    // Device inputs offered in each audio track's input selector.
    void setInputChannelNames (const juce::StringArray& names);

    // Live recording display: the recorder streams into the returned
    // thumbnail; the lane draws it from `recordStartSeconds` to the playhead.
    juce::AudioThumbnail& createLiveThumbnail (int trackId);
    void clearLiveThumbnails();
    std::function<double()> getRecordStartSeconds;   // -1 when not yet started
    std::function<void (model::Track::Type, model::Track::InstrumentKind, engine::InstrumentType)> onAddTrack;
    std::function<void()> onChooseInstrument;   // Instrument Track > Other...: open the instrument chooser
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
        std::unique_ptr<juce::TextButton> mute, solo, arm, monitor, playlists;
        std::unique_ptr<juce::ComboBox> input, autoMode, autoView;
        std::vector<std::unique_ptr<juce::TextButton>> laneMain, laneComp;   // per alternate lane
    };
    static constexpr int alternateLaneHeight = 56;
    std::set<int> playlistsShown;          // track ids with expanded playlist lanes
    int trackTop (int trackIndex) const;   // y of the track's header/lane
    int trackHeightFor (int trackIndex) const;
    int alternateAtY (int trackIndex, int y) const;   // -1 = the main lane
    juce::Rectangle<int> getAlternateLaneBounds (int trackIndex, int alternate) const;
    void paintAlternateLanes (juce::Graphics&, const model::Track&, int trackIndex);
    void showPlaylistMenu (int trackIndex);

    // Automation display state per track id: which parameter lane is shown (nullopt = clips)
    std::map<int, engine::ParamId> automationView;
    std::set<int> clipGainView;   // track ids showing clip gain lines
    bool showsClipGain (const model::Track& t) const { return clipGainView.count (t.id) > 0; }
    void paintClipGainLine (juce::Graphics&, const model::Track&, const model::AudioClip&, juce::Rectangle<float> clipRect);
    int clipGainPointAt (const model::ClipRef&, juce::Point<int>) const;
    juce::Rectangle<float> waveAreaFor (const model::ClipRef&) const;
    bool pencilZoomOk() const;    // enough pixels per sample to draw samples
    void paintSamples (juce::Graphics&, const model::AudioClip&, juce::Rectangle<float> waveArea);
    std::optional<engine::ParamId> shownLane (const model::Track&) const;
    void paintAutomationLane (juce::Graphics&, const model::Track&, int trackIndex, juce::Rectangle<int> lane);
    float valueToY (const engine::ParamId&, float value, juce::Rectangle<int> lane) const;
    float yToValue (const engine::ParamId&, int y, juce::Rectangle<int> lane) const;
    int automationPointAt (int trackIndex, juce::Point<int>) const;   // -1 = none
    void commitAutomationDrag();

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

    void paintBody (juce::Graphics&);
    void paintRuler (juce::Graphics&, juce::Rectangle<int>);
    void paintMarkerStrip (juce::Graphics&, juce::Rectangle<int>);
    int markerAtX (int x) const;   // marker id under x in the strip, -1 = none
    void promptMarkerName (int markerId);
    void paintHeader (juce::Graphics&, const model::Track&, int index, juce::Rectangle<int>);
    void paintLane (juce::Graphics&, const model::Track&, juce::Rectangle<int>);
    void paintAudioClip (juce::Graphics&, const model::Track&, const model::AudioClip&, juce::Rectangle<int> lane);
    void paintPatternClip (juce::Graphics&, const model::Track&, const model::PatternClip&, juce::Rectangle<int> lane);
    void paintMidiClip (juce::Graphics&, const model::Track&, const model::MidiClip&, juce::Rectangle<int> lane);
    void paintLiveRecording (juce::Graphics&, const model::Track&, juce::Rectangle<int> lane);
    void paintEditOverlays (juce::Graphics&);

    // ---- Edit helpers ----
    enum class Drag { none, move, trimStart, trimEnd, select, zoomRange, fadeIn, fadeOut, clipGain, automationPoint, clipGainPoint, scrub, pencil, warpMarker };

    // Elastic audio
    void showClipMenu (const model::ClipRef&, juce::Point<int> screenPos);
    void showLoopMenu (const model::ClipRef&, juce::Point<int> screenPos);
public:
    // A new (empty) pattern or MIDI clip covering the time selection on the selected instrument
    // track(s), or one bar at `seconds` when there is no selection. Returns how many were made.
    int addClipForSelection (double fallbackSeconds = -1.0);
    // Joins the selected clips (per track and kind) into one clip each.
    void joinSelectedClips();
private:
    int warpMarkerAt (const model::ClipRef&, juce::Point<int>) const;   // -1 = none
    void applyElastic (const model::ClipRef&, engine::StretchSpec, const juce::String& name);
    void quantizeSelection();
    void tabToTransient (bool forward);
    int dragMarkerIndex = -1;
    juce::int64 ghostMarkerSample = 0;   // rendered sample of the marker being dragged
public:
    std::function<void (const juce::String&)> onStatus;   // one-line status messages
private:
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
    engine::AudioGraph& graph;
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
    int trackHeight = theme::trackHeight;
    struct ClipboardItem { model::ClipRef::Kind kind; model::AudioClip audio; model::PatternClip pattern; model::MidiClip midi; double relativeStart = 0.0; int trackOffset = 0; };
    std::vector<ClipboardItem> clipboard;

    Drag drag = Drag::none;
    model::ClipRef dragClip;
    model::ClipTiming dragOriginal;
    juce::Point<int> dragStartPoint;
    int dragTargetTrack = -1;
    double ghostStart = 0.0, ghostLength = 0.0;   // seconds
    double ghostFadeSeconds = 0.0;
    float ghostGainDb = 0.0f;
    int dragPointIndex = -1;
    engine::AutomationPoint ghostPoint;
    // Pencil stroke
    std::shared_ptr<juce::AudioBuffer<float>> pencilBuffer;
    int pencilLastSample = -1;
    float pencilLastValue = 0.0f;
    bool dragMoved = false;
    double dragAnchorSeconds = 0.0;

    double pixelsPerSecond = 60.0;
    double viewStartSeconds = 0.0;
    int lastPlayheadPaintX = -1;
    int scrollY = 0;
    juce::ScrollBar vScroll { true };
    int contentHeight() const;
    void updateScrollRange();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    // Painted over the scrolled tracks so their controls slide under the ruler; forwards its mouse events.
    struct RulerOverlay final : juce::Component
    {
        explicit RulerOverlay (TrackArea& o) : owner (o) { setTitle ("Ruler"); }
        void paint (juce::Graphics& g) override { owner.paintRuler (g, getLocalBounds()); }
        void mouseDown (const juce::MouseEvent& e) override { owner.mouseDown (e.getEventRelativeTo (&owner)); }
        void mouseDrag (const juce::MouseEvent& e) override { owner.mouseDrag (e.getEventRelativeTo (&owner)); }
        void mouseUp (const juce::MouseEvent& e) override { owner.mouseUp (e.getEventRelativeTo (&owner)); }
        void mouseMove (const juce::MouseEvent& e) override { owner.mouseMove (e.getEventRelativeTo (&owner)); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { owner.mouseDoubleClick (e.getEventRelativeTo (&owner)); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { owner.mouseWheelMove (e.getEventRelativeTo (&owner), w); }
        TrackArea& owner;
    };
    RulerOverlay rulerOverlay { *this };
    bool dragHover = false;
};

} // namespace beatmaker::ui
