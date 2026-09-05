// Beat Maker — application entry point.
//
// Wires the engine (audio device + transport + render graph), the model
// (undoable session document) and the surface UI (transport bar + track area)
// together. All edits flow: UI -> Command -> Session -> RenderSnapshot -> engine.

#include "shared/Theme.h"
#include "surface/TrackArea.h"
#include "surface/TransportBar.h"

#include <AudioFileLoader.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <io/AudioEngine.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace beatmaker
{

class MainComponent final : public juce::Component,
                            private model::Session::Listener
{
public:
    MainComponent()
    {
        if (const auto error = engine.initialise (2); error.isNotEmpty())
            statusMessage = "Audio device error: " + error;

        session.addListener (this);

        addAndMakeVisible (transportBar);
        addAndMakeVisible (trackArea);
        addAndMakeVisible (statusLabel);
        statusLabel.setColour (juce::Label::textColourId, ui::theme::textDim);
        statusLabel.setFont (juce::FontOptions (12.0f));
        updateStatus();

        transportBar.onOpenFile = [this] { openFileChooser(); };

        trackArea.onFilesDropped = [this] (const juce::StringArray& files, int trackIndex, double seconds)
        {
            for (const auto& path : files)
            {
                importAudioFile (juce::File (path), trackIndex, seconds);
                trackIndex = -1; // subsequent files each get their own track
            }
        };
        trackArea.onAddTrack = [this] { addTrack ("Audio " + juce::String (session.getNumTracks() + 1)); };
        trackArea.onMuteChanged = [this] (int i, bool on)
        {
            session.execute (std::make_unique<model::SetTrackFlagCommand> (i, model::SetTrackFlagCommand::Flag::mute, on));
        };
        trackArea.onSoloChanged = [this] (int i, bool on)
        {
            session.execute (std::make_unique<model::SetTrackFlagCommand> (i, model::SetTrackFlagCommand::Flag::solo, on));
        };

        setWantsKeyboardFocus (true);
        setSize (1200, 720);
    }

    ~MainComponent() override
    {
        session.removeListener (this);
    }

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
        updateStatus();
    }

    void updateStatus()
    {
        juce::String text = statusMessage;
        const auto& history = session.getHistory();
        if (history.canUndo())
            text += "     Undo: " + history.getUndoName() + " (Ctrl+Z)";
        if (text.isEmpty())
            text = "Space: play/stop   Return: back to start   Ctrl+O: open   Ctrl+wheel: zoom";
        statusLabel.setText (text, juce::dontSendNotification);
    }

    engine::AudioEngine engine;
    model::Session session;
    persistence::AudioFileLoader loader;

    ui::TransportBar transportBar { engine.getTransport() };
    ui::TrackArea trackArea { session, engine.getTransport(), loader.getFormatManager() };
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
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // Any audio files passed on the command line are imported at start.
        for (const auto& arg : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File f (arg.unquoted());
            if (f.existsAsFile())
                mainWindow->getMainComponent().importAudioFile (f);
        }
    }

    void shutdown() override { mainWindow.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
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

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace beatmaker

START_JUCE_APPLICATION (beatmaker::BeatMakerApplication)
