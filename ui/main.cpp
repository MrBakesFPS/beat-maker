// Beat Maker — application entry point.
//
// Wires the engine (audio device + transport + render graph), the model
// (undoable session document) and the surface UI (transport bar + track area)
// together. All edits flow: UI -> Command -> Session -> RenderSnapshot -> engine.

#include "shared/Theme.h"
#include "surface/StepSequencer.h"
#include "surface/TrackArea.h"
#include "surface/TransportBar.h"

#include <AudioFileLoader.h>
#include <dsp/DrumKitFactory.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <io/AudioEngine.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <csignal>

namespace beatmaker
{

// SIGTERM/SIGINT request a normal quit so any recording in progress is
// finalised. The handler only sets a flag; a timer on the message thread
// does the actual work.
static std::atomic<bool> terminationRequested { false };
static void onTerminationSignal (int) { terminationRequested.store (true); }

class MainComponent final : public juce::Component,
                            private model::Session::Listener,
                            private juce::Timer
{
public:
    MainComponent()
    {
        if (const auto error = engine.initialise (2, 2); error.isNotEmpty())
            statusMessage = "Audio device error: " + error;

        session.addListener (this);
        trackArea.setInputChannelNames (engine.getInputChannelNames());
        trackArea.getRecordStartSeconds = [this]
        {
            const auto start = engine.getRecorder().getRecordStartSample();
            return start < 0 ? -1.0 : (double) start / engine.getSampleRate();
        };

        addAndMakeVisible (transportBar);
        addAndMakeVisible (trackArea);
        addAndMakeVisible (sequencer);
        addAndMakeVisible (statusLabel);
        statusLabel.setColour (juce::Label::textColourId, ui::theme::textDim);
        statusLabel.setFont (juce::FontOptions (12.0f));
        updateStatus();

        transportBar.onOpenFile = [this] { openFileChooser(); };
        transportBar.onRecord = [this] { toggleRecord(); };
        transportBar.onEditorToggled = [this] (bool visible) { setEditorVisible (visible); };

        trackArea.onFilesDropped = [this] (const juce::StringArray& files, int trackIndex, double seconds)
        {
            for (const auto& path : files)
            {
                importAudioFile (juce::File (path), trackIndex, seconds);
                trackIndex = -1; // subsequent files each get their own track
            }
        };
        trackArea.onAddTrack = [this] (model::Track::Type type)
        {
            if (type == model::Track::Type::instrument) addDrumMachineTrack();
            else addTrack ("Audio " + juce::String (session.getNumTracks() + 1));
        };
        trackArea.onSelectionChanged = [this] (int) { updateSequencerTarget(); };

        sequencer.onStepChanged = [this] (int track, int clip, int pad, int step, std::uint8_t velocity)
        {
            session.execute (std::make_unique<model::SetStepCommand> (track, clip, pad, step, velocity));
        };
        sequencer.onPadSampleDropped = [this] (int track, int pad, const juce::File& file) { loadPadSample (track, pad, file); };
        trackArea.onMuteChanged = [this] (int i, bool on)
        {
            session.execute (std::make_unique<model::SetTrackFlagCommand> (i, model::SetTrackFlagCommand::Flag::mute, on));
        };
        trackArea.onSoloChanged = [this] (int i, bool on)
        {
            session.execute (std::make_unique<model::SetTrackFlagCommand> (i, model::SetTrackFlagCommand::Flag::solo, on));
        };
        trackArea.onArmChanged = [this] (int i, bool on)
        {
            session.execute (std::make_unique<model::SetTrackFlagCommand> (i, model::SetTrackFlagCommand::Flag::arm, on));
        };
        trackArea.onMonitorChanged = [this] (int i, bool on)
        {
            session.execute (std::make_unique<model::SetTrackFlagCommand> (i, model::SetTrackFlagCommand::Flag::monitor, on));
        };
        trackArea.onInputChanged = [this] (int i, int first, int count)
        {
            session.execute (std::make_unique<model::SetTrackInputCommand> (i, first, count));
        };

        setWantsKeyboardFocus (true);
        setSize (1200, 720);
        startTimerHz (20);
    }

    ~MainComponent() override
    {
        stopTimer();
        if (engine.getRecorder().isRecording())
            finishRecording();
        session.removeListener (this);
    }

    // Command-line entry points (also handy for smoke tests and demos).
    void addDrumMachineTrackFromCommandLine() { addDrumMachineTrack(); }
    void startPlayback() { engine.getTransport().play(); }
    void addArmedAudioTrackAndRecord()
    {
        const int index = addTrack ("Audio " + juce::String (countTracks (model::Track::Type::audio) + 1));
        session.execute (std::make_unique<model::SetTrackFlagCommand> (index, model::SetTrackFlagCommand::Flag::arm, true));
        startRecording();
    }
    void setCycleEnabled (bool on) { engine.getTransport().setLoopEnabled (on); }

    // Public so the application can open a file passed on the command line.
    void importAudioFile (const juce::File& file, int trackIndex = -1, double startSeconds = 0.0)
    {
        juce::String error;
        const auto loaded = loader.load (file, engine.getSampleRate(), error);

        if (! loaded)
        {
            statusMessage = error;
            updateStatus();
            return;
        }

        if (trackIndex < 0 || trackIndex >= session.getNumTracks())
            trackIndex = addTrack (file.getFileNameWithoutExtension());

        model::AudioClip clip;
        clip.name          = file.getFileNameWithoutExtension();
        clip.sourceFile    = file;
        clip.audio         = loaded->audio;
        clip.sampleRate    = loaded->sampleRate;
        clip.timelineStart = (juce::int64) std::llround (startSeconds * loaded->sampleRate);
        clip.length        = loaded->numSamples;

        session.execute (std::make_unique<model::AddClipCommand> (trackIndex, std::move (clip)));

        statusMessage = "Imported " + file.getFileName() + "  ("
                      + juce::String (loaded->numChannels) + " ch, "
                      + juce::String (loaded->sourceSampleRate / 1000.0, 1) + " kHz"
                      + (std::abs (loaded->sourceSampleRate - loaded->sampleRate) >= 1.0
                             ? " -> resampled to " + juce::String (loaded->sampleRate / 1000.0, 1) + " kHz"
                             : juce::String())
                      + ")";
        updateStatus();
    }

    void paint (juce::Graphics& g) override { g.fillAll (ui::theme::background); }

    void resized() override
    {
        auto area = getLocalBounds();
        transportBar.setBounds (area.removeFromTop (ui::theme::transportHeight));
        statusLabel.setBounds (area.removeFromBottom (22).reduced (8, 0));

        if (editorVisible)
        {
            sequencer.setBounds (area.removeFromBottom (juce::jmin (300, area.getHeight() / 2)));
            area.removeFromBottom (2);
        }
        trackArea.setBounds (area);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        auto& transport = engine.getTransport();

        if (key == juce::KeyPress::spaceKey)          { transport.togglePlay(); return true; }
        if (key == juce::KeyPress::returnKey
         || key == juce::KeyPress::homeKey)           { transport.returnToStart(); return true; }
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { session.undo(); return true; }
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
         || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) { session.redo(); return true; }
        if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0)) { openFileChooser(); return true; }
        if (key == juce::KeyPress ('c'))                      { transport.setLoopEnabled (! transport.isLoopEnabled()); return true; }
        if (key == juce::KeyPress ('r'))                      { toggleRecord(); return true; }
        if (key == juce::KeyPress ('e'))                      { setEditorVisible (! editorVisible); return true; }
        if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { addDrumMachineTrack(); return true; }

        return false;
    }

private:
    int addTrack (const juce::String& name)
    {
        model::Track track;
        track.name   = name;
        track.colour = model::Session::colourForTrackIndex (session.getNumTracks());

        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        return raw->getTrackIndex();
    }

    //==========================================================================
    // Recording

    void toggleRecord()
    {
        if (engine.getRecorder().isRecording()) finishRecording();   // punch out, keep playing
        else                                    startRecording();
    }

    void startRecording()
    {
        std::vector<engine::Recorder::Slot> slots;
        const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                             .getChildFile ("Beat Maker").getChildFile ("Audio Files");

        for (const auto& track : session.getTracks())
        {
            if (! (track.isAudio() && track.armed)) continue;

            engine::Recorder::Slot slot;
            slot.trackId    = track.id;
            slot.firstInput = track.firstInput;
            slot.numInputs  = track.numInputs;
            slot.file       = dir.getChildFile (juce::File::createLegalFileName (track.name) + "_01.wav").getNonexistentSibling (false);
            slot.receiver   = &trackArea.createLiveThumbnail (track.id);
            slots.push_back (std::move (slot));
        }

        if (slots.empty())
        {
            trackArea.clearLiveThumbnails();
            statusMessage = "Arm an audio track (R button) before recording";
            updateStatus();
            return;
        }

        if (engine.getNumInputChannels() == 0)
            statusMessage = "No audio inputs: recording silence";

        if (const auto error = engine.getRecorder().start (slots, engine.getSampleRate()); error.isNotEmpty())
        {
            trackArea.clearLiveThumbnails();
            statusMessage = error;
            updateStatus();
            return;
        }

        auto& transport = engine.getTransport();
        loopWasEnabled = transport.isLoopEnabled();
        transport.setLoopEnabled (false);          // linear takes only (loop recording comes later)
        transport.setRecordEnabled (true);
        if (! transport.isPlaying())
            transport.play();

        statusMessage = "Recording " + juce::String (slots.size()) + (slots.size() == 1 ? " track" : " tracks") + "...";
        updateStatus();
    }

    void finishRecording()
    {
        auto& recorder = engine.getRecorder();
        auto& transport = engine.getTransport();
        const int dropouts = recorder.getDropoutCount();
        const auto takes = recorder.stop();

        transport.setRecordEnabled (false);
        transport.setLoopEnabled (loopWasEnabled);
        trackArea.clearLiveThumbnails();

        int imported = 0;
        for (const auto& take : takes)
        {
            const int trackIndex = session.indexOfTrackId (take.trackId);
            if (trackIndex < 0) continue;

            juce::String error;
            const auto loaded = loader.load (take.file, engine.getSampleRate(), error);
            if (! loaded) { statusMessage = error; continue; }

            model::AudioClip clip;
            clip.name          = take.file.getFileNameWithoutExtension();
            clip.sourceFile    = take.file;
            clip.audio         = loaded->audio;
            clip.sampleRate    = loaded->sampleRate;
            clip.timelineStart = take.startSample;
            clip.length        = loaded->numSamples;
            session.execute (std::make_unique<model::AddClipCommand> (trackIndex, std::move (clip)));
            ++imported;
        }

        if (imported > 0)
            statusMessage = "Recorded " + juce::String (imported) + (imported == 1 ? " take" : " takes")
                          + (dropouts > 0 ? "  (WARNING: " + juce::String (dropouts) + " disk dropouts)" : juce::String());
        else if (takes.empty())
            statusMessage = "Nothing recorded";
        updateStatus();
    }

    void timerCallback() override
    {
        // Stopping the transport ends the take.
        if (engine.getRecorder().isRecording() && ! engine.getTransport().isPlaying())
            finishRecording();
    }

    void addDrumMachineTrack()
    {
        if (defaultKit == nullptr)
            defaultKit = engine::DrumKitFactory::createDefaultKit (engine.getSampleRate());

        model::Track track;
        track.name    = "Drums " + juce::String (countTracks (model::Track::Type::instrument) + 1);
        track.type    = model::Track::Type::instrument;
        track.colour  = model::Session::colourForTrackIndex (session.getNumTracks());
        track.drumKit = defaultKit;

        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        const int index = raw->getTrackIndex();

        // A 4-bar pattern clip with a starter beat so Play makes sound immediately.
        const auto& transport = engine.getTransport();
        model::PatternClip clip;
        clip.name       = "Beat";
        clip.pattern    = std::make_shared<const engine::StepPattern> (engine::StepPattern::createDefaultBeat());
        clip.sampleRate = engine.getSampleRate();
        clip.length     = (juce::int64) std::llround (transport.beatsToSeconds (4.0 * transport.getBeatsPerBar()) * clip.sampleRate);
        session.execute (std::make_unique<model::AddPatternClipCommand> (index, std::move (clip)));

        trackArea.setSelectedTrack (index);
        updateSequencerTarget();
        setEditorVisible (true);
    }

    void loadPadSample (int trackIndex, int pad, const juce::File& file)
    {
        juce::String error;
        const auto loaded = loader.load (file, engine.getSampleRate(), error);
        if (! loaded) { statusMessage = error; updateStatus(); return; }

        engine::DrumSample sample;
        sample.name  = file.getFileNameWithoutExtension();
        sample.audio = loaded->audio;
        session.execute (std::make_unique<model::SetPadSampleCommand> (trackIndex, pad, std::move (sample)));

        statusMessage = "Loaded " + file.getFileName() + " onto pad " + juce::String (pad + 1);
        updateStatus();
    }

    int countTracks (model::Track::Type type) const
    {
        int n = 0;
        for (const auto& t : session.getTracks()) n += t.type == type ? 1 : 0;
        return n;
    }

    void updateSequencerTarget()
    {
        const int sel = trackArea.getSelectedTrack();
        auto* track = session.getTrack (sel);
        if (track != nullptr && track->isInstrument() && ! track->patternClips.empty())
            sequencer.setTarget (sel, 0);
        else
            sequencer.setTarget (-1, -1);
    }

    void setEditorVisible (bool visible)
    {
        editorVisible = visible;
        sequencer.setVisible (visible);
        transportBar.setEditorVisible (visible);
        resized();
    }

    void openFileChooser()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open Audio File",
                                                           juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                           loader.getWildcard());

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      for (const auto& f : fc.getResults())
                                          importAudioFile (f);
                                      grabKeyboardFocus();
                                  });
    }

    void sessionChanged (model::Session& s) override
    {
        engine.setSnapshot (model::buildRenderSnapshot (s));

        // Cycle range follows the arrangement until a user-defined range exists.
        engine.getTransport().setLoopRange (0, (juce::int64) std::llround (s.getLengthSeconds() * engine.getSampleRate()));

        updateSequencerTarget();
        updateStatus();
    }

    void updateStatus()
    {
        juce::String text = statusMessage;
        const auto& history = session.getHistory();
        if (history.canUndo())
            text += "     Undo: " + history.getUndoName() + " (Ctrl+Z)";
        if (text.isEmpty())
            text = "Space: play/stop   R: record   Return: start   C: cycle   Ctrl+D: drum track   E: editor   Ctrl+O: open   Ctrl+wheel: zoom";
        statusLabel.setText (text, juce::dontSendNotification);
    }

    engine::AudioEngine engine;
    model::Session session;
    persistence::AudioFileLoader loader;

    ui::TransportBar transportBar { engine.getTransport() };
    ui::TrackArea trackArea { session, engine.getTransport(), loader.getFormatManager() };
    ui::StepSequencer sequencer { session, engine.getTransport(), engine.getGraph() };
    std::shared_ptr<const engine::DrumKit> defaultKit;
    bool editorVisible = true;
    bool loopWasEnabled = false;
    juce::Label statusLabel;
    juce::String statusMessage;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

//==============================================================================
class BeatMakerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        std::signal (SIGTERM, onTerminationSignal);
        std::signal (SIGINT,  onTerminationSignal);
        quitPoller.startTimer (100);

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // Command line: audio files are imported; --drums adds a Drum Machine
        // track with the starter beat; --record adds an armed audio track and
        // starts recording; --cycle enables looping; --play starts the transport.
        auto& main = mainWindow->getMainComponent();
        bool play = false;

        for (const auto& arg : juce::StringArray::fromTokens (commandLine, true))
        {
            if (arg == "--drums")       main.addDrumMachineTrackFromCommandLine();
            else if (arg == "--record") main.addArmedAudioTrackAndRecord();
            else if (arg == "--cycle")  main.setCycleEnabled (true);
            else if (arg == "--play")   play = true;
            else if (const juce::File f (arg.unquoted()); f.existsAsFile())
                main.importAudioFile (f);
        }

        if (play)
            main.startPlayback();
    }

    void shutdown() override { quitPoller.stopTimer(); mainWindow.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    struct QuitPoller final : juce::Timer
    {
        void timerCallback() override
        {
            if (terminationRequested.exchange (false))
                if (auto* app = juce::JUCEApplication::getInstance())
                    app->systemRequestedQuit();
        }
    } quitPoller;

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, ui::theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            getMainComponent().grabKeyboardFocus();
        }

        MainComponent& getMainComponent() { return *static_cast<MainComponent*> (getContentComponent()); }

        // Make sure keyboard shortcuts work as soon as the window is active,
        // even before the user has clicked anything.
        void activeWindowStatusChanged() override
        {
            DocumentWindow::activeWindowStatusChanged();
            if (isActiveWindow() && getCurrentlyFocusedComponent() == nullptr)
                getMainComponent().grabKeyboardFocus();
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace beatmaker

START_JUCE_APPLICATION (beatmaker::BeatMakerApplication)
