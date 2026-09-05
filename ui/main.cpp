// Beat Maker — application entry point.
//
// Wires the engine (audio device + transport + render graph), the model
// (undoable session document) and the surface UI (transport bar + track area)
// together. All edits flow: UI -> Command -> Session -> RenderSnapshot -> engine.

#include "depth/BounceDialog.h"
#include "depth/EditToolbar.h"
#include "depth/FadesDialog.h"
#include "depth/MixerView.h"
#include "shared/EditSettings.h"
#include "shared/Theme.h"
#include "surface/LoopBrowser.h"
#include "surface/PianoRoll.h"
#include "surface/SmartControls.h"
#include "surface/StepSequencer.h"
#include "surface/TrackArea.h"
#include "surface/TransportBar.h"

#include <AudioFileLoader.h>
#include <LoopLibrary.h>
#include <bounce/Bouncer.h>
#include <dsp/DrumKitFactory.h>
#include <dsp/Resampler.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <io/AudioEngine.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <csignal>
#include <iostream>

namespace beatmaker
{

// SIGTERM/SIGINT request a normal quit so any recording in progress is
// finalised. The handler only sets a flag; a timer on the message thread
// does the actual work.
static std::atomic<bool> terminationRequested { false };
static void onTerminationSignal (int) { terminationRequested.store (true); }

class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
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
        addAndMakeVisible (editToolbar);
        addAndMakeVisible (trackArea);
        editSettings.onChanged = [this] { editToolbar.refresh(); trackArea.repaint(); };
        trackArea.onTimeSelectionChanged = [this] { updateLoopRange(); };
        addAndMakeVisible (sequencer);
        addAndMakeVisible (pianoRoll);
        pianoRoll.setVisible (false);
        addAndMakeVisible (mixerView);
        mixerView.setVisible (false);
        addAndMakeVisible (smartControls);

        mixerView.onCommand = [this] (std::unique_ptr<model::Command> cmd, bool replacePrevious)
        {
            if (replacePrevious) session.undo();
            session.execute (std::move (cmd));
        };
        mixerView.onSelectTrack = [this] (int i) { trackArea.setSelectedTrack (i); };
        addAndMakeVisible (loopBrowser);
        addAndMakeVisible (statusLabel);

        setupLoopLibrary();
        statusLabel.setColour (juce::Label::textColourId, ui::theme::textDim);
        statusLabel.setFont (juce::FontOptions (12.0f));
        updateStatus();

        transportBar.onOpenFile = [this] { openFileChooser(); };
        transportBar.onRecord = [this] { toggleRecord(); };
        transportBar.onBounce = [this] { showBounceDialogImpl(); };
        transportBar.onEditorToggled = [this] (bool visible) { setEditorVisible (visible); };
        transportBar.onLibraryToggled = [this] (bool visible) { setLibraryVisible (visible); };
        transportBar.onControlsToggled = [this] (bool visible) { setControlsVisible (visible); };
        transportBar.onMixerToggled = [this] (bool visible) { setMixerVisible (visible); };

        smartControls.onCommand = [this] (std::unique_ptr<model::Command> cmd, bool replacePrevious)
        {
            if (replacePrevious) session.undo();
            session.execute (std::move (cmd));
        };

        loopBrowser.onPreview = [this] (const persistence::LoopInfo* loop) { previewLoop (loop); };
        loopBrowser.onAddAtPlayhead = [this] (const persistence::LoopInfo& loop)
        {
            importLoop (loop.file, -1, engine.getTransport().getPositionSeconds());
        };
        loopBrowser.onAddFolder = [this] { chooseLoopFolder(); };
        trackArea.onLoopDropped = [this] (const juce::File& file, int trackIndex, double seconds)
        {
            loopBrowser.stopPreview();
            importLoop (file, trackIndex, seconds);
        };

        trackArea.onFilesDropped = [this] (const juce::StringArray& files, int trackIndex, double seconds)
        {
            for (const auto& path : files)
            {
                importAudioFile (juce::File (path), trackIndex, seconds);
                trackIndex = -1; // subsequent files each get their own track
            }
        };
        trackArea.onAddTrack = [this] (model::Track::Type type, model::Track::InstrumentKind kind)
        {
            if (type == model::Track::Type::aux)                              addAuxTrack();
            else if (type != model::Track::Type::instrument)                  addTrack ("Audio " + juce::String (session.getNumTracks() + 1));
            else if (kind == model::Track::InstrumentKind::synth)             addSynthTrack();
            else                                                              addDrumMachineTrack();
        };
        trackArea.onSelectionChanged = [this] (int) { updateSequencerTarget(); };

        sequencer.onStepChanged = [this] (int track, int clip, int pad, int step, std::uint8_t velocity)
        {
            session.execute (std::make_unique<model::SetStepCommand> (track, clip, pad, step, velocity));
        };
        sequencer.onPadSampleDropped = [this] (int track, int pad, const juce::File& file) { loadPadSample (track, pad, file); };

        pianoRoll.onSequenceChanged = [this] (int track, int clip, std::shared_ptr<const engine::MidiSequence> seq, juce::String name)
        {
            session.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (track, clip, std::move (seq), std::move (name)));
        };
        pianoRoll.onPresetChanged = [this] (int track, int preset)
        {
            session.execute (std::make_unique<model::SetSynthParamsCommand> (track, engine::SynthParams::preset (preset)));
        };
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
    void addSynthTrackFromCommandLine() { addSynthTrack(); }
    void startPlayback() { engine.getTransport().play(); }
    void addArmedAudioTrackAndRecord()
    {
        const int index = addTrack ("Audio " + juce::String (countTracks (model::Track::Type::audio) + 1));
        session.execute (std::make_unique<model::SetTrackFlagCommand> (index, model::SetTrackFlagCommand::Flag::arm, true));
        startRecording();
    }
    void setCycleEnabled (bool on) { engine.getTransport().setLoopEnabled (on); }
    void showBounceDialog();
    void setMixerVisibleFromCommandLine (bool v) { setMixerVisible (v); }
    void importLoopFromCommandLine (const juce::File& file) { importLoop (file, -1, engine.getTransport().getPositionSeconds()); }

    // --fades=<in ms>,<out ms>[,<gain dB>]: apply to every audio clip (smoke tests).
    void applyFadesFromCommandLine (const juce::String& spec)
    {
        const auto parts = juce::StringArray::fromTokens (spec, ",", {});
        ui::TrackArea::FadeValues v;
        v.fadeInMs = parts.size() > 0 ? parts[0].getDoubleValue() : 0.0;
        v.fadeOutMs = parts.size() > 1 ? parts[1].getDoubleValue() : 0.0;
        v.gainDb = parts.size() > 2 ? parts[2].getFloatValue() : 0.0f;
        v.inShape = engine::FadeShape::sCurve;
        v.outShape = engine::FadeShape::equalPower;
        trackArea.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));   // select all
        trackArea.applyFadesToSelection (v);
    }

    // Synchronous whole-arrangement bounce for command-line use. Returns an
    // exit message for stdout.
    juce::String bounceArrangementToFile (const juce::File& file)
    {
        engine::BounceSettings s = defaultBounceSettings();
        s.format = file.hasFileExtension ("aiff;aif") ? engine::BounceSettings::Format::aiff
                 : file.hasFileExtension ("flac")     ? engine::BounceSettings::Format::flac
                                                      : engine::BounceSettings::Format::wav;
        s.startSample = 0;
        s.endSample   = (juce::int64) std::llround (session.getLengthSeconds() * s.sampleRate);

        const auto result = engine::Bouncer::renderToFile (model::buildRenderSnapshot (session), s, file);
        return describeBounce (result, file);
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
        editToolbar.setBounds (area.removeFromTop (ui::EditToolbar::preferredHeight));
        statusLabel.setBounds (area.removeFromBottom (22).reduced (8, 0));

        if (mixerVisible)
        {
            mixerView.setBounds (area.removeFromBottom (juce::jmin (ui::MixerView::preferredHeight, area.getHeight() * 2 / 3)));
            area.removeFromBottom (2);
        }
        else if (editorVisible)
        {
            auto editor = area.removeFromBottom (juce::jmin (300, area.getHeight() / 2));
            sequencer.setBounds (editor);
            pianoRoll.setBounds (editor);
            area.removeFromBottom (2);
        }
        if (controlsVisible)
            smartControls.setBounds (area.removeFromBottom (ui::SmartControls::preferredHeight));
        if (libraryVisible)
            loopBrowser.setBounds (area.removeFromLeft (juce::jmin (280, area.getWidth() / 3)));
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
        if (key == juce::KeyPress ('l'))                      { setLibraryVisible (! libraryVisible); return true; }
        if (key == juce::KeyPress ('b'))                      { setControlsVisible (! controlsVisible); return true; }
        if (key == juce::KeyPress ('x'))                      { setMixerVisible (! mixerVisible); return true; }
        if (key == juce::KeyPress::escapeKey)                 { loopBrowser.stopPreview(); return true; }
        if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { addDrumMachineTrack(); return true; }
        if (key == juce::KeyPress ('i', juce::ModifierKeys::commandModifier, 0)) { addSynthTrack(); return true; }

        // Edit modes F1-F4, tools F5-F9 (Pro Tools layout)
        using Mode = ui::EditSettings::Mode; using Tool = ui::EditSettings::Tool;
        if (key == juce::KeyPress::F1Key) { editSettings.mode = Mode::shuffle; editSettings.notify(); return true; }
        if (key == juce::KeyPress::F2Key) { editSettings.mode = Mode::slip;    editSettings.notify(); return true; }
        if (key == juce::KeyPress::F3Key) { editSettings.mode = Mode::spot;    editSettings.notify(); return true; }
        if (key == juce::KeyPress::F4Key) { editSettings.mode = Mode::grid;    editSettings.notify(); return true; }
        if (key == juce::KeyPress::F5Key) { editSettings.tool = Tool::zoomer;   editSettings.notify(); return true; }
        if (key == juce::KeyPress::F6Key) { editSettings.tool = Tool::trimmer;  editSettings.notify(); return true; }
        if (key == juce::KeyPress::F7Key) { editSettings.tool = Tool::selector; editSettings.notify(); return true; }
        if (key == juce::KeyPress::F8Key) { editSettings.tool = Tool::grabber;  editSettings.notify(); return true; }
        if (key == juce::KeyPress::F9Key) { editSettings.tool = Tool::smart;    editSettings.notify(); return true; }
        if (key == juce::KeyPress ('z', juce::ModifierKeys::altModifier, 0))   { trackArea.zoomToFit(); return true; }
        if (key == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0)) { showFadesDialog(); return true; }

        // Clip edits reach the track area even when it doesn't have focus
        if (trackArea.keyPressed (key)) return true;
        if (key == juce::KeyPress ('b', juce::ModifierKeys::commandModifier, 0)) { showBounceDialogImpl(); return true; }

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
    // Loop library

    static juce::File bundledLoopsFolder()
    {
        // Development builds point straight at the source tree; installed
        // builds look next to the executable.
        const juce::File exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (const auto& candidate : {
       #ifdef BEATMAKER_ASSETS_DIR
                 juce::File (BEATMAKER_ASSETS_DIR).getChildFile ("loops"),
       #endif
                 exe.getSiblingFile ("assets").getChildFile ("loops"),
                 exe.getParentDirectory().getParentDirectory().getChildFile ("assets").getChildFile ("loops") })
            if (candidate.isDirectory()) return candidate;
        return {};
    }

    static juce::File userLoopsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Loops");
    }

    static juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Beat Maker";
        o.filenameSuffix = "settings";
        o.folderName = "Beat Maker";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    void setupLoopLibrary()
    {
        appSettings = std::make_unique<juce::PropertiesFile> (settingsOptions());

        juce::Array<juce::File> folders;
        if (const auto bundled = bundledLoopsFolder(); bundled.isDirectory()) folders.add (bundled);
        userLoopsFolder().createDirectory();
        folders.add (userLoopsFolder());
        for (const auto& path : juce::StringArray::fromLines (appSettings->getValue ("loopFolders")))
            if (juce::File (path).isDirectory()) folders.add (juce::File (path));

        loopLibrary.setFolders (folders);
        loopLibrary.rescanAsync();
        loopBrowser.setSessionBpm (engine.getTransport().getBpm());
    }

    void chooseLoopFolder()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Add Loop Folder", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto folder = fc.getResult();
                                      if (! folder.isDirectory()) return;
                                      loopLibrary.addFolder (folder);

                                      juce::StringArray saved = juce::StringArray::fromLines (appSettings->getValue ("loopFolders"));
                                      saved.addIfNotAlreadyThere (folder.getFullPathName());
                                      saved.removeEmptyStrings();
                                      appSettings->setValue ("loopFolders", saved.joinIntoString ("\n"));
                                      appSettings->saveIfNeeded();

                                      loopLibrary.rescanAsync();
                                  });
    }

    void previewLoop (const persistence::LoopInfo* loop)
    {
        previewAudio.reset();
        if (loop != nullptr)
        {
            juce::String error;
            if (const auto loaded = loader.load (loop->file, engine.getSampleRate(), error))
                previewAudio = loaded->audio;
            else
                statusMessage = error;
        }
        pushSnapshot();
        updateStatus();
    }

    double snapToBeat (double seconds) const
    {
        const double beat = engine.getTransport().beatsToSeconds (1.0);
        return juce::jmax (0.0, std::round (seconds / beat) * beat);
    }

    // Place a library loop on a track, conformed to the session tempo by
    // varispeed resampling (pitch follows tempo until Phase 3 adds
    // polyphonic time-stretching) and snapped to the beat grid.
    void importLoop (const juce::File& file, int trackIndex, double seconds)
    {
        juce::String error;
        const auto loaded = loader.load (file, engine.getSampleRate(), error);
        if (! loaded) { statusMessage = error; updateStatus(); return; }

        const auto info = persistence::LoopLibrary::analyse (file, loader.getFormatManager());
        const double sessionBpm = engine.getTransport().getBpm();
        auto audio = loaded->audio;
        juce::String conformNote;

        if (info.bpm > 0.0 && std::abs (info.bpm - sessionBpm) > 0.01)
        {
            const double ratio = engine::Resampler::ratioForTempo (info.bpm, sessionBpm);
            audio = std::make_shared<const juce::AudioBuffer<float>> (engine::Resampler::resample (*audio, ratio));
            conformNote = "  (" + juce::String (juce::roundToInt (info.bpm)) + " -> " + juce::String (juce::roundToInt (sessionBpm))
                        + " BPM, varispeed)";
        }

        if (trackIndex < 0 || trackIndex >= session.getNumTracks())
            trackIndex = addTrack (file.getFileNameWithoutExtension());

        const double start = snapToBeat (seconds);
        model::AudioClip clip;
        clip.name          = info.name;
        clip.sourceFile    = file;
        clip.audio         = audio;
        clip.sampleRate    = loaded->sampleRate;
        clip.timelineStart = (juce::int64) std::llround (start * loaded->sampleRate);
        clip.length        = audio->getNumSamples();
        session.execute (std::make_unique<model::AddClipCommand> (trackIndex, std::move (clip)));

        statusMessage = "Added " + info.name + " at bar " + juce::String (engine.getTransport().barBeatForSeconds (start).bar) + conformNote;
        updateStatus();
    }

    void setMixerVisible (bool visible)
    {
        mixerVisible = visible;
        mixerView.setVisible (visible);
        transportBar.setMixerVisible (visible);
        updateSequencerTarget();
        resized();
    }

    void addAuxTrack()
    {
        model::Track track;
        track.name   = "Aux " + juce::String (countTracks (model::Track::Type::aux) + 1);
        track.type   = model::Track::Type::aux;
        track.colour = model::Session::colourForTrackIndex (session.getNumTracks());
        track.inputBus = 0;   // Bus 1-2 by default; change it in the mixer
        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        trackArea.setSelectedTrack (raw->getTrackIndex());
        setMixerVisible (true);
    }

    void setControlsVisible (bool visible)
    {
        controlsVisible = visible;
        smartControls.setVisible (visible);
        transportBar.setControlsVisible (visible);
        resized();
    }

    void setLibraryVisible (bool visible)
    {
        libraryVisible = visible;
        loopBrowser.setVisible (visible);
        transportBar.setLibraryVisible (visible);
        if (! visible) loopBrowser.stopPreview();
        resized();
    }

    //==========================================================================
    // Fades window

    void showFadesDialog()
    {
        const auto values = trackArea.currentFadeValues();
        if (! values)
        {
            statusMessage = "Select one or more audio clips first (Fades applies to the selection)";
            updateStatus();
            return;
        }

        ui::FadesDialog::Values initial;
        initial.fadeInMs = values->fadeInMs; initial.fadeOutMs = values->fadeOutMs;
        initial.inShape = values->inShape; initial.outShape = values->outShape; initial.gainDb = values->gainDb;

        auto* dialog = new ui::FadesDialog (initial, trackArea.numSelectedAudioClips());
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "Fades";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();

        dialog->onCancel = [window] { window->setVisible (false); };
        dialog->onApply = [this, window] (const ui::FadesDialog::Values& v)
        {
            ui::TrackArea::FadeValues fv;
            fv.fadeInMs = v.fadeInMs; fv.fadeOutMs = v.fadeOutMs; fv.inShape = v.inShape; fv.outShape = v.outShape; fv.gainDb = v.gainDb;
            trackArea.applyFadesToSelection (fv);
            window->setVisible (false);
        };
    }

    //==========================================================================
    // Bounce

    engine::BounceSettings defaultBounceSettings()
    {
        engine::BounceSettings s;
        s.sampleRate  = engine.getSampleRate();
        s.bpm         = engine.getTransport().getBpm();
        s.beatsPerBar = engine.getTransport().getBeatsPerBar();
        s.numChannels = 2;
        return s;
    }

    static juce::String describeBounce (const engine::BounceResult& r, const juce::File& file)
    {
        if (r.cancelled)         return "Bounce cancelled";
        if (r.error.isNotEmpty()) return "Bounce failed: " + r.error;

        juce::String text = "Bounced " + file.getFileName() + "  (" + juce::String (r.numSamples) + " samples, peak "
                          + juce::String (juce::Decibels::gainToDecibels (r.peakBeforeNormalize), 1) + " dBFS";
        if (std::abs (r.appliedGain - 1.0f) > 1.0e-6f) text += ", normalised " + juce::String (juce::Decibels::gainToDecibels (r.appliedGain), 1) + " dB";
        text += ")";
        if (r.clipped) text += "  WARNING: output clipped, lower the mix or enable Normalize";
        return text;
    }

    void showBounceDialogImpl()
    {
        if (session.getLengthSeconds() <= 0.0)
        {
            statusMessage = "Nothing to bounce: the arrangement is empty";
            updateStatus();
            return;
        }

        const auto& transport = engine.getTransport();
        ui::BounceDialog::Context ctx;
        ctx.sampleRate         = engine.getSampleRate();
        ctx.arrangementSeconds = session.getLengthSeconds();
        ctx.numOutputs         = 2;
        if (transport.hasValidLoop())
        {
            ctx.cycleStartSeconds = (double) transport.getLoopStart() / ctx.sampleRate;
            ctx.cycleEndSeconds   = (double) transport.getLoopEnd() / ctx.sampleRate;
        }

        auto* dialog = new ui::BounceDialog (ctx);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "Bounce to Disk";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();

        dialog->onCancel = [window] { window->exitModalState (0); window->setVisible (false); };
        dialog->onBounce = [this, window] (engine::BounceSettings settings)
        {
            settings.bpm         = engine.getTransport().getBpm();
            settings.beatsPerBar = engine.getTransport().getBeatsPerBar();
            window->setVisible (false);
            chooseBounceDestination (settings);
        };
    }

    void chooseBounceDestination (engine::BounceSettings settings)
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                             .getChildFile ("Beat Maker").getChildFile ("Bounces");
        dir.createDirectory();
        const auto ext = engine::BounceSettings::extensionFor (settings.format);
        const auto suggested = dir.getChildFile ("Bounce" + ext).getNonexistentSibling (false);

        fileChooser = std::make_unique<juce::FileChooser> ("Bounce to Disk", suggested, "*" + ext);
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, settings, ext] (const juce::FileChooser& fc)
                                  {
                                      auto file = fc.getResult();
                                      if (file == juce::File()) return;
                                      if (! file.hasFileExtension (ext.substring (1)))
                                          file = file.withFileExtension (ext);
                                      runBounce (settings, file);
                                  });
    }

    // Background render with a progress window.
    class BounceJob final : public juce::ThreadWithProgressWindow
    {
    public:
        BounceJob (std::unique_ptr<engine::RenderSnapshot> snap, engine::BounceSettings s, juce::File f,
                   std::function<void (const engine::BounceResult&, const juce::File&)> done)
            : ThreadWithProgressWindow ("Bouncing to " + f.getFileName() + "...", true, true),
              snapshot (std::move (snap)), settings (s), file (f), onDone (std::move (done)) {}

        void run() override
        {
            result = engine::Bouncer::renderToFile (std::move (snapshot), settings, file,
                                                    [this] (double p) { setProgress (p); return ! threadShouldExit(); });
        }

        void threadComplete (bool userCancelled) override
        {
            if (userCancelled) result.cancelled = true;
            if (onDone) onDone (result, file);
        }

    private:
        std::unique_ptr<engine::RenderSnapshot> snapshot;
        engine::BounceSettings settings;
        juce::File file;
        engine::BounceResult result;
        std::function<void (const engine::BounceResult&, const juce::File&)> onDone;
    };

    void runBounce (const engine::BounceSettings& settings, const juce::File& file)
    {
        bounceJob = std::make_unique<BounceJob> (model::buildRenderSnapshot (session), settings, file,
            [this] (const engine::BounceResult& r, const juce::File& f)
            {
                statusMessage = describeBounce (r, f);
                updateStatus();
                if (r.ok()) f.revealToUser();
                juce::MessageManager::callAsync ([this] { bounceJob.reset(); });
            });
        bounceJob->launchThread();
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
        track.instrumentKind = model::Track::InstrumentKind::drumMachine;
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

    void addSynthTrack()
    {
        model::Track track;
        track.name           = "Synth " + juce::String (countTracks (model::Track::InstrumentKind::synth) + 1);
        track.type           = model::Track::Type::instrument;
        track.instrumentKind = model::Track::InstrumentKind::synth;
        track.colour         = model::Session::colourForTrackIndex (session.getNumTracks());
        track.synthParams    = engine::SynthParams::preset (1);   // Pluck

        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        const int index = raw->getTrackIndex();

        // A 4-bar clip looping a 2-bar arpeggio so Play makes sound immediately.
        const auto& transport = engine.getTransport();
        model::MidiClip clip;
        clip.name       = "Arp";
        clip.sequence   = std::make_shared<const engine::MidiSequence> (engine::MidiSequence::createDefaultArpeggio());
        clip.sampleRate = engine.getSampleRate();
        clip.length     = (juce::int64) std::llround (transport.beatsToSeconds (4.0 * transport.getBeatsPerBar()) * clip.sampleRate);
        session.execute (std::make_unique<model::AddMidiClipCommand> (index, std::move (clip)));

        trackArea.setSelectedTrack (index);
        updateSequencerTarget();
        setEditorVisible (true);
    }

    int countTracks (model::Track::InstrumentKind kind) const
    {
        int n = 0;
        for (const auto& t : session.getTracks()) n += t.instrumentKind == kind ? 1 : 0;
        return n;
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

    // The editor panel shows whichever editor fits the selected track.
    void updateSequencerTarget()
    {
        const int sel = trackArea.getSelectedTrack();
        auto* track = session.getTrack (sel);
        const bool drums = track != nullptr && track->isDrumMachine() && ! track->patternClips.empty();
        const bool synth = track != nullptr && track->isSynth() && ! track->midiClips.empty();

        sequencer.setTarget (drums ? sel : -1, drums ? 0 : -1);
        pianoRoll.setTarget (synth ? sel : -1, synth ? 0 : -1);
        if (smartControls.getTrackIndex() != sel) smartControls.setTrack (sel);
        pianoRoll.setVisible (editorVisible && ! mixerVisible && synth);
        sequencer.setVisible (editorVisible && ! mixerVisible && ! synth);
    }

    void setEditorVisible (bool visible)
    {
        editorVisible = visible;
        transportBar.setEditorVisible (visible);
        updateSequencerTarget();
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

    void pushSnapshot()
    {
        auto snapshot = model::buildRenderSnapshot (session);
        snapshot->preview = previewAudio;
        engine.setSnapshot (std::move (snapshot));
    }

    // Cycle range: the time selection when there is one, else the arrangement.
    void updateLoopRange()
    {
        const auto& sel = trackArea.getTimeSelection();
        const double sr = engine.getSampleRate();
        if (sel.isValid())
            engine.getTransport().setLoopRange ((juce::int64) std::llround (sel.start * sr), (juce::int64) std::llround (sel.end * sr));
        else
            engine.getTransport().setLoopRange (0, (juce::int64) std::llround (session.getLengthSeconds() * sr));
    }

    void sessionChanged (model::Session&) override
    {
        pushSnapshot();
        updateLoopRange();

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
            text = "Space: play/stop   R: record   Return: start   C: cycle   L: library   B: controls   X: mixer   E: editor   Ctrl+Shift+D: drums   Ctrl+I: synth   Ctrl+O: open   Ctrl+B: bounce";
        statusLabel.setText (text, juce::dontSendNotification);
    }

    engine::AudioEngine engine;
    model::Session session;
    persistence::AudioFileLoader loader;

    ui::TransportBar transportBar { engine.getTransport() };
    ui::EditSettings editSettings;
    ui::EditToolbar editToolbar { editSettings };
    ui::TrackArea trackArea { session, engine.getTransport(), loader.getFormatManager(), editSettings };
    ui::StepSequencer sequencer { session, engine.getTransport(), engine.getGraph() };
    ui::PianoRoll pianoRoll { session, engine.getTransport(), engine.getGraph() };
    ui::SmartControls smartControls { session };
    ui::MixerView mixerView { session, engine.getGraph(), [this] { return engine.getSampleRate(); } };
    bool controlsVisible = true;
    bool mixerVisible = false;
    persistence::LoopLibrary loopLibrary { loader.getFormatManager() };
    ui::LoopBrowser loopBrowser { loopLibrary };
    std::unique_ptr<juce::PropertiesFile> appSettings;
    std::shared_ptr<const juce::AudioBuffer<float>> previewAudio;
    bool libraryVisible = true;
    std::shared_ptr<const engine::DrumKit> defaultKit;
    bool editorVisible = true;
    bool loopWasEnabled = false;
    juce::Label statusLabel;
    juce::String statusMessage;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<BounceJob> bounceJob;
};

void MainComponent::showBounceDialog() { showBounceDialogImpl(); }

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
        // track with the starter beat; --synth adds a Synth track with an arpeggio; --record adds an armed audio track and
        // starts recording; --cycle enables looping; --play starts the
        // transport; --loop=<file> adds a library loop tempo-conformed at the
        // playhead; --bounce=<file> renders the arrangement and quits.
        auto& main = mainWindow->getMainComponent();
        bool play = false;
        juce::File bounceFile;

        // Use the raw argument array: paths with spaces arrive intact.
        juce::ignoreUnused (commandLine);
        for (const auto& arg : getCommandLineParameterArray())
        {
            if (arg == "--drums")       main.addDrumMachineTrackFromCommandLine();
            else if (arg == "--synth")  main.addSynthTrackFromCommandLine();
            else if (arg == "--record") main.addArmedAudioTrackAndRecord();
            else if (arg == "--cycle")  main.setCycleEnabled (true);
            else if (arg == "--play")   play = true;
            else if (arg.startsWith ("--bounce=")) bounceFile = juce::File::getCurrentWorkingDirectory()
                                                                    .getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--bounce-dialog") main.showBounceDialog();
            else if (arg == "--mixer")  main.setMixerVisibleFromCommandLine (true);
            else if (arg.startsWith ("--fades=")) main.applyFadesFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--loop=")) main.importLoopFromCommandLine (juce::File::getCurrentWorkingDirectory()
                                                                                    .getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (arg); f.existsAsFile())
                main.importAudioFile (f);
        }

        // --bounce=<file>: render the arrangement and quit (batch mode).
        if (bounceFile != juce::File())
        {
            std::cout << main.bounceArrangementToFile (bounceFile) << std::endl;
            quit();
            return;
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
