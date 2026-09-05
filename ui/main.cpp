// Beat Maker — application entry point.
//
// Wires the engine (audio device + transport + render graph), the model
// (undoable session document) and the surface UI (transport bar + track area)
// together. All edits flow: UI -> Command -> Session -> RenderSnapshot -> engine.

#include "depth/BounceDialog.h"
#include "depth/BeatDetectiveDialog.h"
#include "depth/MemoryLocationsWindow.h"
#include "depth/NewSessionDialog.h"
#include <SessionFile.h>
#include "shared/ElasticJob.h"
#include "depth/EditToolbar.h"
#include "depth/FadesDialog.h"
#include "depth/GroupDialog.h"
#include "depth/IOSetupDialog.h"
#include "depth/MixerView.h"
#include "depth/PluginWindow.h"
#include "shared/EditSettings.h"
#include "shared/Theme.h"
#include "surface/LoopBrowser.h"
#include "surface/PianoRoll.h"
#include "surface/SmartControls.h"
#include "surface/StepSequencer.h"
#include "surface/TrackArea.h"
#include "surface/TransportBar.h"

#include <AudioFileLoader.h>
#include <AutomationRecorder.h>
#include <GroupLogic.h>
#include <LoopLibrary.h>
#include <Playlists.h>
#include <PluginHost.h>
#include <bounce/Bouncer.h>
#include <dsp/DrumKitFactory.h>
#include <dsp/Resampler.h>
#include <dsp/PitchCorrection.h>
#include <Elastic.h>
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
        // I/O paths default to whatever the device offers
        session.execute (std::make_unique<model::SetIOSetupCommand> (model::IOSetup::createDefault (engine.getNumInputChannels(), 2)));
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
        mixerView.onParameterChanged = [this] (int t, const engine::ParamId& p, float v, bool g) { automation.parameterChanged (t, p, v, g); };
        mixerView.onGestureEnded = [this] (int t, const engine::ParamId& p) { automation.gestureEnded (t, p); };
        mixerView.automatedValue = [this] (int t, const engine::ParamId& p) { return automation.displayedValue (t, p); };
        mixerView.onTrimChanged = [this] (int t, float db, bool g) { automation.trimChanged (t, db, g); };

        mixerView.knownPlugins = [this] { return pluginManager.getKnownPlugins().getTypes(); };
        mixerView.onScanPlugins = [this] { scanPlugins(); };
        mixerView.onInsertPlugin = [this] (int track, int slot, const juce::PluginDescription& desc) { insertPlugin (track, slot, desc); };
        mixerView.onOpenPluginEditor = [this] (int track, int slot) { openPluginEditor (track, slot); };
        mixerView.onLoadImpulse = [this] (int track, int slot) { chooseImpulseResponse (track, slot); };
        smartControls.onParameterChanged = [this] (int t, const engine::ParamId& p, float v, bool g) { automation.parameterChanged (t, p, v, g); };
        smartControls.onGestureEnded = [this] (int t, const engine::ParamId& p) { automation.gestureEnded (t, p); };
        trackArea.onAutomationModeChanged = [this] (int t, model::AutomationMode m)
        {
            session.execute (std::make_unique<model::SetAutomationModeCommand> (t, m));
        };
        addAndMakeVisible (loopBrowser);
        addAndMakeVisible (statusLabel);

        setupLoopLibrary();
        statusLabel.setColour (juce::Label::textColourId, ui::theme::textDim);
        statusLabel.setFont (juce::FontOptions (12.0f));
        updateStatus();

        transportBar.onOpenFile = [this] { showFileMenu(); };
        trackArea.onOpenMemoryLocations = [this] { showMemoryLocations(); };
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
            // Dropping onto a Sampler track loads the file as its sound.
            if (auto* t = session.getTrack (trackIndex); t != nullptr && t->instrumentType() == engine::InstrumentType::sampler && ! files.isEmpty())
            {
                loadSamplerSample (trackIndex, juce::File (files[0]));
                return;
            }
            for (const auto& path : files)
            {
                importAudioFile (juce::File (path), trackIndex, seconds);
                trackIndex = -1; // subsequent files each get their own track
            }
        };
        trackArea.onAddTrack = [this] (model::Track::Type type, model::Track::InstrumentKind kind, engine::InstrumentType instrument)
        {
            if (type == model::Track::Type::vca)                              addVcaTrack();
            else if (type == model::Track::Type::aux)                         addAuxTrack();
            else if (type != model::Track::Type::instrument)                  addTrack ("Audio " + juce::String (session.getNumTracks() + 1));
            else if (kind == model::Track::InstrumentKind::synth)             addInstrumentTrack (instrument);
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
            auto* t = session.getTrack (track);
            if (t == nullptr || ! t->hasInstrument()) return;
            const auto presets = engine::Instrument::presets (t->instrumentType());
            if (! juce::isPositiveAndBelow (preset, (int) presets.size())) return;
            auto p = std::make_shared<engine::InstrumentParams> (presets[(size_t) preset]);
            // Presets change the sound, not the sampler's loaded sample.
            p->sample = t->instrumentParams->sample; p->sampleRate = t->instrumentParams->sampleRate;
            p->rootNote = t->instrumentParams->rootNote; p->sampleName = t->instrumentParams->sampleName;
            session.execute (std::make_unique<model::SetInstrumentParamsCommand> (track, std::move (p), "Change Preset"));
        };
        trackArea.onStatus = [this] (const juce::String& text) { statusMessage = text; updateStatus(); };
        trackArea.onOpenBeatDetective = [this] { showBeatDetective(); };
        trackArea.onMuteChanged = [this] (int i, bool on) { session.execute (model::GroupLogic::flagCommand (session, i, model::SetTrackFlagCommand::Flag::mute, on)); };
        trackArea.onSoloChanged = [this] (int i, bool on) { session.execute (model::GroupLogic::flagCommand (session, i, model::SetTrackFlagCommand::Flag::solo, on)); };
        trackArea.onArmChanged  = [this] (int i, bool on)
        {
            // TrackPunch while rolling: the R button punches the track in/out instead of disarming it.
            auto* t = session.getTrack (i);
            if (engine.getRecorder().isRecording() && session.getRecordSettings().mode == model::RecordMode::trackPunch && t != nullptr && t->armed)
            {
                setTrackPunched (i, ! t->punched);
                return;
            }
            session.execute (model::GroupLogic::flagCommand (session, i, model::SetTrackFlagCommand::Flag::arm, on));
        };
        transportBar.onRecordModeClicked = [this] (juce::TextButton& b) { showRecordModeMenu (b); };
        transportBar.onRollClicked = [this] (juce::TextButton& b) { showRollMenu (b); };
        refreshRecordSettingsDisplay();
        mixerView.onEditGroup = [this] (int groupId) { showGroupDialog (groupId); };
        trackArea.onMonitorChanged = [this] (int i, bool on)
        {
            session.execute (std::make_unique<model::SetTrackFlagCommand> (i, model::SetTrackFlagCommand::Flag::monitor, on));
        };
        trackArea.onInputChanged = [this] (int i, int first, int count)
        {
            session.execute (std::make_unique<model::SetTrackInputCommand> (i, first, count));
        };
        trackArea.onInputPathChanged = [this] (int i, int path)
        {
            if (auto* t = session.getTrack (i))
                session.execute (std::make_unique<model::SetTrackPathsCommand> (i, path, t->outputPath));
        };
        editToolbar.onIOSetup = [this] { showIOSetupDialog(); };

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
    void addSynthTrackFromCommandLine() { addInstrumentTrack (engine::InstrumentType::subtractive); }
    void setRecordModeFromCommandLine (const juce::String& name)
    {
        auto rs = session.getRecordSettings();
        for (auto m : { model::RecordMode::normal, model::RecordMode::quickPunch, model::RecordMode::trackPunch, model::RecordMode::loop })
            if (juce::String (model::recordModeName (m)).equalsIgnoreCase (name)) rs.mode = m;
        session.execute (std::make_unique<model::SetRecordSettingsCommand> (rs));
        refreshRecordSettingsDisplay();
    }
    void setRollFromCommandLine (bool pre, double seconds)
    {
        auto rs = session.getRecordSettings();
        if (pre) { rs.preRoll = seconds > 0.0; rs.preRollSeconds = seconds; } else { rs.postRoll = seconds > 0.0; rs.postRollSeconds = seconds; }
        session.execute (std::make_unique<model::SetRecordSettingsCommand> (rs));
        refreshRecordSettingsDisplay();
    }
    void setPunchRangeFromCommandLine (double start, double end) { trackArea.setTimeSelectionSeconds (start, end, 0); }
    void punchFromCommandLine() { if (engine.getRecorder().isRecording()) punchAll (! engine.getRecorder().isPunched (-1)); }
    void stopTransportFromCommandLine() { engine.getTransport().stop(); }
    bool openSessionFromCommandLine (const juce::File& f) { return openSession (f); }
    bool saveSessionFromCommandLine (const juce::File& f) { return writeSession (f, f.hasFileExtension ("bmkt")); }
    void templateFromCommandLine (const juce::String& name)
    {
        for (const auto& c : templateChoices()) if (c.name.equalsIgnoreCase (name)) { applyTemplate (c, name); return; }
        statusMessage = "Unknown template: " + name; updateStatus();
    }
    // --marker-demo: memory locations and arrangement sections over the beat-making template.
    void markerDemoFromCommandLine()
    {
        templateFromCommandLine ("Beat Making");
        const auto& tr = engine.getTransport();
        const double bar = tr.beatsToSeconds (tr.getBeatsPerBar());
        auto section = [&] (const char* name, int fromBar, int toBar, juce::uint32 colour)
        {
            model::Marker m; m.name = name; m.isSection = true; m.seconds = fromBar * bar; m.endSeconds = toBar * bar; m.colour = juce::Colour (colour);
            session.execute (std::make_unique<model::AddMarkerCommand> (m));
        };
        section ("Intro", 0, 2, 0xff3498db); section ("Verse", 2, 4, 0xff2ecc71); section ("Chorus", 4, 6, 0xffe67e22);
        model::Marker drop; drop.name = "Drop"; drop.seconds = 4 * bar; drop.recallSelection = true; drop.selectionStart = 4 * bar; drop.selectionEnd = 6 * bar;
        session.execute (std::make_unique<model::AddMarkerCommand> (drop));
        model::Marker outro; outro.name = "Outro"; outro.seconds = 6 * bar; outro.colour = juce::Colour (0xff9b59b6);
        session.execute (std::make_unique<model::AddMarkerCommand> (outro));
        trackArea.recallMarker (drop.id > 0 ? drop.id : 4);
        statusMessage = "Marker demo: 3 sections and 2 memory locations; Alt+4 recalls Drop with its selection";
        updateStatus();
    }
    void beatDetectiveDemoFromCommandLine() { beatDetectiveDemo(); }
    void addInstrumentTrackFromCommandLine (const juce::String& name)
    {
        for (auto type : engine::Instrument::availableTypes())
            if (juce::String (engine::Instrument::typeName (type)).removeCharacters (" ").equalsIgnoreCase (name.removeCharacters (" -_")))
            { addInstrumentTrack (type); return; }
        statusMessage = "Unknown instrument: " + name;
        updateStatus();
    }
    // Loads a file into the most recently added Sampler track (creating one if needed).
    void loadSampleFromCommandLine (const juce::File& file)
    {
        int index = -1;
        for (int i = session.getNumTracks(); --i >= 0;)
            if (session.getTracks()[(size_t) i].instrumentType() == engine::InstrumentType::sampler) { index = i; break; }
        if (index < 0) { addInstrumentTrack (engine::InstrumentType::sampler); index = trackArea.getSelectedTrack(); }
        loadSamplerSample (index, file);
    }
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
    void showIOSetupDialogFromCommandLine() { showIOSetupDialog(); }
    void addVcaTrackFromCommandLine() { addVcaTrack(); }
    void scanPluginsFromCommandLine() { scanPlugins(); }
    // --insert-plugin=<name>: put the first known plugin whose name contains <name> on track 1, slot 1.
    void insertPluginFromCommandLine (const juce::String& name)
    {
        for (const auto& d : pluginManager.getKnownPlugins().getTypes())
            if (d.name.containsIgnoreCase (name))
            {
                if (session.getNumTracks() == 0) addTrack ("Audio 1");
                insertPlugin (0, 0, d);
                return;
            }
        statusMessage = "No known plugin matches \"" + name + "\" (scan first)";
        updateStatus();
    }
    // Waits for a running scan, then runs `then` (smoke tests chain --scan-plugins with --insert-plugin).
    void afterScan (std::function<void()> then)
    {
        if (! pluginManager.isScanning()) { then(); return; }
        juce::Timer::callAfterDelay (200, [this, then] { afterScan (then); });
    }

    // Convolution Reverb: pick an impulse response file for an insert.
    void chooseImpulseResponse (int trackIndex, int slot)
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load Impulse Response", juce::File::getSpecialLocation (juce::File::userHomeDirectory), loader.getWildcard());
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, trackIndex, slot] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            loadImpulseResponse (trackIndex, slot, file);
        });
    }

    void loadImpulseResponse (int trackIndex, int slot, const juce::File& file)
    {
        juce::String error;
        const auto loaded = loader.load (file, engine.getSampleRate(), error);
        if (! loaded) { statusMessage = error; updateStatus(); return; }
        session.execute (std::make_unique<model::SetInsertImpulseCommand> (trackIndex, slot, loaded->audio, loaded->sampleRate, file.getFileNameWithoutExtension()));
        statusMessage = "Loaded impulse response " + file.getFileName() + "  (" + juce::String (loaded->numSamples / loaded->sampleRate, 2) + " s)";
        updateStatus();
    }

    // --sidechain-demo: a drum track sends pre-fader to Bus 1-2; a pad's
    // compressor is keyed from that bus so it pumps with the kick.
    void sidechainDemoFromCommandLine()
    {
        const double sr = engine.getSampleRate();
        addDrumMachineTrack();
        addInstrumentTrack (engine::InstrumentType::wavetable);
        const int drums = session.getNumTracks() - 2, pad = session.getNumTracks() - 1;
        if (drums < 0) return;
        const auto presets = engine::Instrument::presets (engine::InstrumentType::wavetable);
        for (const auto& p : presets) if (p.presetName == "WT Pad") session.execute (std::make_unique<model::SetInstrumentParamsCommand> (pad, std::make_shared<const engine::InstrumentParams> (p)));

        model::Send send; send.bus = 0; send.gain = 1.0f; send.preFader = true;
        session.execute (std::make_unique<model::SetSendCommand> (drums, 0, send));
        session.execute (std::make_unique<model::SetInsertCommand> (pad, 0, engine::EffectType::compressor, sr));
        auto p = std::make_shared<engine::InsertParams> (engine::Effect::defaultParams (engine::EffectType::compressor));
        p->values[engine::CompressorEffect::threshold] = -35.0f;
        p->values[engine::CompressorEffect::ratio] = 10.0f;
        p->values[engine::CompressorEffect::attack] = 1.0f;
        p->values[engine::CompressorEffect::release] = 120.0f;
        session.execute (std::make_unique<model::SetInsertParamsCommand> (pad, 0, std::move (p)));
        session.execute (std::make_unique<model::SetInsertKeyCommand> (pad, 0, 0, false));
        statusMessage = "Sidechain demo: the pad's compressor is keyed from Bus 1-2, fed pre-fader by the drums";
        updateStatus();
    }

    // --pitch-demo: the bundled bass line pushed 40 cents sharp with Elastic
    // pitch shift, then pulled back to A minor by a Pitch Correction insert.
    void pitchDemoFromCommandLine()
    {
        const double sr = engine.getSampleRate();
        importLoop (bundledLoopsFolder().getChildFile ("Sub Bass Line 120 Am.wav"), -1, 0.0);
        const int track = session.getNumTracks() - 1;
        if (track < 0 || session.getTracks()[(size_t) track].clips.empty()) return;
        const model::ClipRef ref { track, model::ClipRef::Kind::audio, 0 };
        session.execute (std::make_unique<model::SetClipElasticCommand> (session, ref, model::Elastic::withPitch (session.getTracks()[(size_t) track].clips[0], 0.4), "Pitch Shift"));
        session.execute (std::make_unique<model::SetInsertCommand> (track, 0, engine::EffectType::pitchCorrection, sr));
        auto p = std::make_shared<engine::InsertParams> (engine::Effect::defaultParams (engine::EffectType::pitchCorrection));
        p->values[engine::PitchCorrectionEffect::key] = 9.0f;   // A
        p->values[engine::PitchCorrectionEffect::scale] = (float) engine::PitchCorrectionEffect::minor;
        p->values[engine::PitchCorrectionEffect::speed] = 10.0f;
        session.execute (std::make_unique<model::SetInsertParamsCommand> (track, 0, std::move (p)));
        statusMessage = "Pitch demo: bass line shifted +40 cents, corrected back to A minor by the insert";
        updateStatus();
    }

    // --convolution-demo: an electric piano through a Cathedral convolution reverb.
    void convolutionDemoFromCommandLine()
    {
        const double sr = engine.getSampleRate();
        addInstrumentTrack (engine::InstrumentType::electricPiano);
        const int track = session.getNumTracks() - 1;
        session.execute (std::make_unique<model::SetInsertCommand> (track, 0, engine::EffectType::convolution, sr));
        auto p = std::make_shared<engine::InsertParams> (engine::Effect::defaultParams (engine::EffectType::convolution));
        p->values[engine::ConvolutionEffect::impulse] = (float) engine::ConvolutionEffect::cathedral;
        p->values[engine::ConvolutionEffect::mix] = 45.0f;
        session.execute (std::make_unique<model::SetInsertParamsCommand> (track, 0, std::move (p)));
        statusMessage = "Convolution demo: Electric Piano into a Cathedral impulse response";
        updateStatus();
    }

    void insertDemoFromCommandLine()
    {
        const double sr = engine.getSampleRate();
        if (session.getNumTracks() > 0)
        {
            session.execute (std::make_unique<model::SetInsertCommand> (0, 0, engine::EffectType::eq, sr));
            session.execute (std::make_unique<model::SetInsertCommand> (0, 1, engine::EffectType::saturation, sr));
            session.execute (std::make_unique<model::SetInsertCommand> (0, 2, engine::EffectType::chorus, sr));
        }
        if (session.getNumTracks() > 1)
        {
            session.execute (std::make_unique<model::SetInsertCommand> (1, 0, engine::EffectType::gate, sr));
            session.execute (std::make_unique<model::SetInsertCommand> (1, 1, engine::EffectType::deesser, sr));
            session.execute (std::make_unique<model::SetInsertCommand> (1, 2, engine::EffectType::phaser, sr));
        }
        session.execute (std::make_unique<model::SetInsertCommand> (-1, 0, engine::EffectType::limiter, sr));
        session.execute (std::make_unique<model::SetInsertCommand> (-1, 1, engine::EffectType::utility, sr));
    }

    // --clip-gain-demo: a gain line on the first audio clip, Clip Gain view on, zoomed to fit
    void clipGainDemoFromCommandLine()
    {
        for (int i = 0; i < session.getNumTracks(); ++i)
        {
            const auto& t = session.getTracks()[(size_t) i];
            if (! t.isAudio() || t.clips.empty()) continue;
            const auto& c = t.clips[0];
            auto lane = std::make_shared<engine::AutomationLane>();
            lane->param = engine::ParamId::volume();
            lane->points = { { c.sourceOffset, 1.0f }, { c.sourceOffset + c.length / 4, 0.3f }, { c.sourceOffset + c.length / 2, 1.5f }, { c.sourceOffset + c.length, 0.6f } };
            session.execute (std::make_unique<model::SetClipGainLaneCommand> (model::ClipRef { i, model::ClipRef::Kind::audio, 0 }, lane, "Demo Clip Gain"));
            trackArea.showClipGainView (i);
            break;
        }
    }

    void playlistDemoFromCommandLine()
    {
        for (int i = 0; i < session.getNumTracks(); ++i)
        {
            const auto& t = session.getTracks()[(size_t) i];
            if (! t.isAudio() || t.clips.empty()) continue;
            session.execute (std::make_unique<model::DuplicatePlaylistCommand> (i));
            session.execute (std::make_unique<model::DuplicatePlaylistCommand> (i));
            // Offset the second alternate so the lanes visibly differ
            auto shifted = t.clips;
            for (auto& c : shifted) { c.timelineStart += (juce::int64) (0.5 * c.sampleRate); c.name += " (late)"; }
            session.execute (std::make_unique<model::ReplaceMainClipsCommand> (i, shifted, "Demo Take"));
            trackArea.setPlaylistsShown (i, true);
            break;
        }
    }

    // --group-demo: group all existing audio-carrying tracks (Edit+Mix), add a VCA and assign them to it.
    void groupDemoFromCommandLine()
    {
        model::Group g;
        g.name = "Rhythm";
        g.colour = juce::Colour (0xffe67e22);
        for (const auto& t : session.getTracks()) if (t.carriesAudio()) g.trackIds.push_back (t.id);
        session.execute (std::make_unique<model::CreateGroupCommand> (g));

        addVcaTrack();
        const int vcaId = session.getTracks().back().id;
        for (int i = 0; i < session.getNumTracks(); ++i)
            if (session.getTracks()[(size_t) i].carriesAudio())
                session.execute (std::make_unique<model::SetTrackVcaCommand> (i, vcaId));
    }

    // --automation-demo: put a volume swell + pan sweep on track 1 and show the lane (smoke tests).
    void automationDemoFromCommandLine()
    {
        if (session.getNumTracks() == 0) return;
        const double sr = engine.getSampleRate();
        auto vol = std::make_shared<engine::AutomationLane>();
        vol->param = engine::ParamId::volume();
        vol->points = { { 0, 0.1f }, { (juce::int64) (2.0 * sr), 1.0f }, { (juce::int64) (4.0 * sr), 0.3f }, { (juce::int64) (6.0 * sr), 1.2f } };
        session.execute (std::make_unique<model::ReplaceAutomationLaneCommand> (0, vol, "Demo Automation"));
        auto pan = std::make_shared<engine::AutomationLane>();
        pan->param = engine::ParamId::pan();
        pan->points = { { 0, -1.0f }, { (juce::int64) (8.0 * sr), 1.0f } };
        session.execute (std::make_unique<model::ReplaceAutomationLaneCommand> (0, pan, "Demo Automation"));
        trackArea.showAutomationLane (0, engine::ParamId::volume());
    }
    void importLoopFromCommandLine (const juce::File& file) { importLoop (file, -1, engine.getTransport().getPositionSeconds()); }

    // --elastic-demo: session at 100 BPM; a 120 BPM drum loop conformed
    // (Rhythmic) and quantized to 1/16, a 120 BPM pad conformed (Polyphonic)
    // and pitched up a fourth, and a copy of the drums TCE'd to 3 beats.
    void elasticDemoFromCommandLine()
    {
        engine.getTransport().setBpm (100.0);
        const auto loops = bundledLoopsFolder();
        importLoop (loops.getChildFile ("Drum Loop 120.wav"), -1, 0.0);
        importLoop (loops.getChildFile ("Synth Pad 120 Am.wav"), -1, 0.0);
        if (session.getNumTracks() < 2) return;

        const model::ClipRef drums { 0, model::ClipRef::Kind::audio, 0 }, pad { 1, model::ClipRef::Kind::audio, 0 };
        if (auto spec = model::Elastic::quantizeToGrid (session.getTracks()[0].clips[0], 100.0, 0.25))
            session.execute (std::make_unique<model::SetClipElasticCommand> (session, drums, *spec, "Quantize Audio"));
        session.execute (std::make_unique<model::SetClipElasticCommand> (session, pad, model::Elastic::withPitch (session.getTracks()[1].clips[0], 5.0), "Pitch Shift"));
        session.execute (std::make_unique<model::SetTrackMixCommand> (0, 0.7f, 0.0f));   // headroom for the sum
        session.execute (std::make_unique<model::SetTrackMixCommand> (1, 0.5f, 0.0f));

        session.execute (std::make_unique<model::DuplicateClipCommand> (drums));
        const model::ClipRef copy { 0, model::ClipRef::Kind::audio, 1 };
        const auto& c = session.getTracks()[0].clips[1];
        trackArea.setSelectedTrack (0);
        statusMessage = "Elastic demo: drums Rhythmic + quantized, pad Polyphonic +5 st, copy TCE'd to 3 beats  (" + engine::TimeStretch::libraryVersion() + ")";
        updateStatus();
        // The TCE goes through the job so --elastic-async=0 exercises the background path.
        auto tce = std::make_unique<model::SetClipElasticCommand> (session, copy,
                       model::Elastic::forVisibleLength (c, (juce::int64) std::llround (3.0 * 60.0 / 100.0 * c.sampleRate)), "TCE Trim", true);
        const bool background = (double) tce->getSourceLength() / tce->getSampleRate() > ui::ElasticJob::asyncThresholdSeconds;
        if (! background) tce->render();
        ui::ElasticJob::run (session, std::move (tce), [this, background] (bool applied)
        {
            statusMessage += applied ? (background ? "  [TCE rendered in the background]" : "  [TCE rendered inline]") : "  [TCE cancelled]";
            updateStatus();
        });
    }

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
        if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0)) { openSessionChooser(); return true; }
        if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { saveSession (false); return true; }
        if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { saveSession (true); return true; }
        if (key == juce::KeyPress ('n', juce::ModifierKeys::commandModifier, 0)) { showNewSessionDialog(); return true; }
        if (key == juce::KeyPress ('5', juce::ModifierKeys::commandModifier, 0)) { showMemoryLocations(); return true; }
        if (key == juce::KeyPress ('c'))                      { transport.setLoopEnabled (! transport.isLoopEnabled()); return true; }
        if (key == juce::KeyPress ('r'))                      { toggleRecord(); return true; }
        if (key == juce::KeyPress ('e'))                      { setEditorVisible (! editorVisible); return true; }
        if (key == juce::KeyPress ('l'))                      { setLibraryVisible (! libraryVisible); return true; }
        if (key == juce::KeyPress ('b'))                      { setControlsVisible (! controlsVisible); return true; }
        if (key == juce::KeyPress ('x'))                      { setMixerVisible (! mixerVisible); return true; }
        if (key == juce::KeyPress::escapeKey)                 { loopBrowser.stopPreview(); return true; }
        if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { addDrumMachineTrack(); return true; }
        if (key == juce::KeyPress ('i', juce::ModifierKeys::commandModifier, 0)) { addInstrumentTrack (engine::InstrumentType::subtractive); return true; }

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
        if (key == juce::KeyPress::F9Key) { editSettings.tool = Tool::scrubber; editSettings.notify(); return true; }
        if (key == juce::KeyPress::F10Key) { editSettings.tool = Tool::pencil;  editSettings.notify(); return true; }
        if (key == juce::KeyPress::F11Key) { editSettings.tool = Tool::smart;   editSettings.notify(); return true; }
        if (key == juce::KeyPress ('z', juce::ModifierKeys::altModifier, 0))   { trackArea.zoomToFit(); return true; }
        if (key == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0)) { showFadesDialog(); return true; }
        if (key == juce::KeyPress ('g', juce::ModifierKeys::commandModifier, 0)) { showGroupDialog (-1); return true; }
        if (key == juce::KeyPress ('8', juce::ModifierKeys::commandModifier, 0)) { showBeatDetective(); return true; }
        if (key == juce::KeyPress ('i', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0)) { showIOSetupDialog(); return true; }

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

    // Place a library loop on a track, conformed to the session tempo with
    // Elastic Audio (Rhythmic for drums, Polyphonic otherwise: pitch stays
    // put) and snapped to the beat grid. The clip remembers its source
    // tempo so it can be re-conformed after a tempo change.
    void importLoop (const juce::File& file, int trackIndex, double seconds)
    {
        juce::String error;
        const auto loaded = loader.load (file, engine.getSampleRate(), error);
        if (! loaded) { statusMessage = error; updateStatus(); return; }

        const auto info = persistence::LoopLibrary::analyse (file, loader.getFormatManager());
        const double sessionBpm = engine.getTransport().getBpm();

        if (trackIndex < 0 || trackIndex >= session.getNumTracks())
            trackIndex = addTrack (file.getFileNameWithoutExtension());

        const double start = snapToBeat (seconds);
        model::AudioClip clip;
        clip.name          = info.name;
        clip.sourceFile    = file;
        clip.audio         = loaded->audio;
        clip.sampleRate    = loaded->sampleRate;
        clip.timelineStart = (juce::int64) std::llround (start * loaded->sampleRate);
        clip.length        = loaded->audio->getNumSamples();
        clip.sourceBpm     = info.bpm;
        auto add = std::make_unique<model::AddClipCommand> (trackIndex, std::move (clip));
        session.execute (std::move (add));
        const model::ClipRef ref { trackIndex, model::ClipRef::Kind::audio, (int) session.getTracks()[(size_t) trackIndex].clips.size() - 1 };

        juce::String conformNote;
        if (info.bpm > 0.0 && std::abs (info.bpm - sessionBpm) > 0.01)
        {
            const auto mode = info.category == persistence::LoopInfo::Category::drums ? engine::StretchMode::rhythmic : engine::StretchMode::polyphonic;
            const auto& placed = session.getTracks()[(size_t) trackIndex].clips[(size_t) ref.index];
            auto stretch = std::make_unique<model::SetClipElasticCommand> (session, ref, model::Elastic::forTempo (placed, sessionBpm, mode), "Conform to Tempo");
            if (! stretch->wasCancelled())
            {
                session.execute (std::move (stretch));
                conformNote = "  (" + juce::String (juce::roundToInt (info.bpm)) + " -> " + juce::String (juce::roundToInt (sessionBpm))
                            + " BPM, " + engine::TimeStretch::modeName (mode) + ")";
            }
        }

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

    void addVcaTrack()
    {
        model::Track track;
        track.name   = "VCA " + juce::String (countTracks (model::Track::Type::vca) + 1);
        track.type   = model::Track::Type::vca;
        track.colour = juce::Colour (0xffb0b8c4);
        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        trackArea.setSelectedTrack (raw->getTrackIndex());
        setMixerVisible (true);
    }

    // groupId < 0 creates a new group seeded with the selected track
    // Beat Detective window (non-modal; stays open while you work)
    void showBeatDetective()
    {
        if (beatDetectiveWindow != nullptr) { beatDetectiveWindow->setVisible (true); beatDetectiveWindow->toFront (true); return; }
        auto* dialog = new ui::BeatDetectiveDialog (editSettings);
        beatDetectiveDialog = dialog;
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "Beat Detective";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        beatDetectiveWindow = options.launchAsync();

        auto target = [this] (const char* what) -> std::vector<model::ClipRef>
        {
            auto clips = trackArea.clipsForRhythmEditing();
            if (clips.empty() && beatDetectiveDialog != nullptr) beatDetectiveDialog->setStatus (juce::String (what) + ": select audio clips or an audio track first");
            return clips;
        };
        auto report = [this] (const juce::String& text) { if (beatDetectiveDialog != nullptr) beatDetectiveDialog->setStatus (text); statusMessage = text; updateStatus(); };

        dialog->onSeparate = [this, target, report, dialog]
        {
            const auto clips = target ("Separate");
            if (clips.empty()) return;
            if (auto cmd = model::BeatDetective::separate (session, clips, dialog->sensitivity()))
            {
                const int before = (int) session.getTracks()[(size_t) clips[0].track].clips.size();
                session.execute (std::move (cmd));
                report ("Separated: " + juce::String ((int) session.getTracks()[(size_t) clips[0].track].clips.size() - before) + " new clips");
            }
            else report ("Separate: no transients found");
        };
        dialog->onConform = [this, target, report, dialog]
        {
            const auto clips = target ("Conform");
            if (clips.empty()) return;
            if (auto cmd = model::BeatDetective::conform (session, clips, dialog->conformSettings (engine.getTransport().getBpm())))
            { session.execute (std::move (cmd)); report ("Conformed " + juce::String (clips.size()) + " clips to the grid"); }
            else report ("Conform: every clip is already on the grid");
        };
        dialog->onSmooth = [this, target, report, dialog]
        {
            const auto clips = target ("Smooth");
            if (clips.empty()) return;
            if (auto cmd = model::BeatDetective::smooth (session, clips, dialog->smoothingSettings()))
            { session.execute (std::move (cmd)); report ("Smoothed " + juce::String (clips.size()) + " clips"); }
            else report ("Smooth: nothing to fill");
        };
        dialog->onAll = [this, dialog]
        {
            dialog->onSeparate();
            // After separating, operate on every clip of the same track(s)
            auto clips = trackArea.clipsForRhythmEditing();
            if (clips.empty()) return;
            std::vector<model::ClipRef> all;
            for (const auto& r : clips)
                if (std::none_of (all.begin(), all.end(), [&] (const model::ClipRef& o) { return o.track == r.track; }))
                    for (int i = 0; i < (int) session.getTracks()[(size_t) r.track].clips.size(); ++i) all.push_back ({ r.track, model::ClipRef::Kind::audio, i });
            if (auto cmd = model::BeatDetective::conform (session, all, dialog->conformSettings (engine.getTransport().getBpm()))) session.execute (std::move (cmd));
            if (auto cmd = model::BeatDetective::smooth (session, all, dialog->smoothingSettings())) session.execute (std::move (cmd));
            statusMessage = "Beat Detective: separated, conformed and smoothed (3 undo steps)";
            updateStatus();
            if (beatDetectiveDialog != nullptr) beatDetectiveDialog->setStatus (statusMessage);
        };
    }

    // --beat-detective-demo: the 90 BPM hip hop loop conformed to 120, separated at its
    // transients, conformed to 1/16 with 40% swing and smoothed with 5 ms crossfades.
    void beatDetectiveDemo()
    {
        importLoop (bundledLoopsFolder().getChildFile ("Hip Hop Beat 90.wav"), -1, 0.0);
        const int track = session.getNumTracks() - 1;
        if (track < 0) return;
        trackArea.setSelectedTrack (track);
        std::vector<model::ClipRef> clips { { track, model::ClipRef::Kind::audio, 0 } };
        if (auto cmd = model::BeatDetective::separate (session, clips, 0.5f)) session.execute (std::move (cmd));
        std::vector<model::ClipRef> all;
        for (int i = 0; i < (int) session.getTracks()[(size_t) track].clips.size(); ++i) all.push_back ({ track, model::ClipRef::Kind::audio, i });
        model::ConformSettings c; c.bpm = engine.getTransport().getBpm(); c.gridBeats = 0.25; c.swing = 0.4f;
        if (auto cmd = model::BeatDetective::conform (session, all, c)) session.execute (std::move (cmd));
        model::SmoothingSettings sm; sm.fillGaps = true; sm.crossfade = true; sm.crossfadeMs = 5.0;
        if (auto cmd = model::BeatDetective::smooth (session, all, sm)) session.execute (std::move (cmd));
        statusMessage = "Beat Detective demo: " + juce::String (all.size()) + " slices, conformed to 1/16 with 40% swing, crossfaded";
        updateStatus();
        showBeatDetective();
    }

    void showGroupDialog (int groupId)
    {
        model::Group initial;
        if (auto* existing = session.getGroup (groupId)) initial = *existing;
        else
        {
            initial.name = "Group " + juce::String (session.getGroups().size() + 1);
            static const juce::Colour palette[] = { juce::Colour (0xffe67e22), juce::Colour (0xff9b59b6), juce::Colour (0xff1abc9c), juce::Colour (0xffe84393), juce::Colour (0xff3498db) };
            initial.colour = palette[session.getGroups().size() % 5];
            if (auto* t = session.getTrack (trackArea.getSelectedTrack())) initial.trackIds.push_back (t->id);
        }

        auto* dialog = new ui::GroupDialog (session, initial);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = groupId < 0 ? "New Group" : "Edit Group";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();

        dialog->onCancel = [window] { window->setVisible (false); };
        dialog->onApply = [this, window, isNew = groupId < 0] (const model::Group& g)
        {
            if (isNew) session.execute (std::make_unique<model::CreateGroupCommand> (g));
            else       session.execute (std::make_unique<model::ReplaceGroupCommand> (g));
            window->setVisible (false);
        };
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
    // Plugins

    void scanPlugins()
    {
        if (pluginManager.isScanning()) { statusMessage = "Plugin scan already running"; updateStatus(); return; }
        statusMessage = "Scanning for plugins (" + pluginManager.getFormatNames().joinIntoString (", ") + ")...";
        updateStatus();
        pluginManager.scanAsync (
            [this] (float progress, const juce::String& file)
            {
                statusMessage = "Scanning plugins " + juce::String (juce::roundToInt (progress * 100.0f)) + "%   " + juce::File (file).getFileName();
                updateStatus();
            },
            [this]
            {
                const int n = pluginManager.getKnownPlugins().getNumTypes();
                const int bad = pluginManager.getKnownPlugins().getBlacklistedFiles().size();
                statusMessage = "Plugin scan finished: " + juce::String (n) + (n == 1 ? " plugin" : " plugins")
                              + (bad > 0 ? ", " + juce::String (bad) + " blacklisted (crashed or failed validation)" : juce::String());
                updateStatus();
            });
    }

    void insertPlugin (int trackIndex, int slot, const juce::PluginDescription& desc)
    {
        juce::String error;
        auto fx = pluginManager.instantiate (desc, engine.getSampleRate(), engine::AudioGraph::maxBlock, error);
        if (fx == nullptr) { statusMessage = "Plugin failed to load: " + error; updateStatus(); return; }
        session.execute (std::make_unique<model::SetPluginInsertCommand> (trackIndex, slot, fx, desc.createIdentifierString()));
        statusMessage = "Inserted " + desc.name + " (" + desc.pluginFormatName + ", latency " + juce::String (fx->getLatencySamples ({})) + " samples)";
        updateStatus();
    }

    void openPluginEditor (int trackIndex, int slot)
    {
        auto* t = session.getTrackOrMaster (trackIndex);
        if (t == nullptr || ! juce::isPositiveAndBelow (slot, model::Track::numInsertSlots)) return;
        auto* fx = dynamic_cast<plugins::PluginEffect*> (t->inserts[(size_t) slot].instance.get());
        if (fx == nullptr) return;

        // One window per instance
        for (auto& w : pluginWindows)
            if (w.effect == fx) { w.window->toFront (true); return; }

        auto window = std::make_unique<ui::PluginWindow> (*fx, [this, fx]
        {
            juce::MessageManager::callAsync ([this, fx]
            {
                std::erase_if (pluginWindows, [fx] (const OpenPluginWindow& w) { return w.effect == fx; });
            });
        });
        pluginWindows.push_back ({ fx, std::move (window) });
    }

    //==========================================================================
    // I/O Setup

    void showIOSetupDialog()
    {
        auto* dialog = new ui::IOSetupDialog (session.getIO(), engine.getNumInputChannels(), 2, session.isDelayCompensationEnabled());
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "I/O Setup";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();

        dialog->onCancel = [window] { window->setVisible (false); };
        dialog->onApply = [this, window] (const model::IOSetup& io, bool adc)
        {
            auto compound = std::make_unique<model::CompoundCommand> ("I/O Setup");
            compound->add (std::make_unique<model::SetIOSetupCommand> (io));
            if (adc != session.isDelayCompensationEnabled())
                compound->add (std::make_unique<model::SetDelayCompensationCommand> (adc));
            session.execute (std::move (compound));
            trackArea.setInputChannelNames (engine.getInputChannelNames());   // rebuilds the header combos
            window->setVisible (false);
        };
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
        const auto mode = session.getRecordSettings().mode;
        if (! engine.getRecorder().isRecording()) { startRecording(); return; }
        if (mode == model::RecordMode::quickPunch || mode == model::RecordMode::trackPunch)
            punchAll (! engine.getRecorder().isPunched (-1));   // the Record button punches in/out while rolling
        else
            finishRecording();                                  // punch out, keep playing
    }

    // QuickPunch: every recording track; TrackPunch: the master Record button drives all of them.
    void punchAll (bool in)
    {
        auto& recorder = engine.getRecorder();
        recorder.setPunch (-1, in);
        for (int i = 0; i < session.getNumTracks(); ++i)
            if (session.getTracks()[(size_t) i].armed) session.execute (std::make_unique<model::SetTrackPunchCommand> (i, in));
        engine.getTransport().setRecordEnabled (in);
        transportBar.setWaitingForPunch (! in);
        statusMessage = in ? "Punched in" : "Punched out (still rolling: Record punches in again, Stop finishes)";
        updateStatus();
    }

    void setTrackPunched (int trackIndex, bool in)
    {
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr) return;
        engine.getRecorder().setPunch (t->id, in);
        session.execute (std::make_unique<model::SetTrackPunchCommand> (trackIndex, in));
        const bool any = engine.getRecorder().isPunched (-1) || in;
        engine.getTransport().setRecordEnabled (any);
        transportBar.setWaitingForPunch (! any);
        statusMessage = t->name + (in ? ": punched in" : ": punched out");
        updateStatus();
    }

    void refreshRecordSettingsDisplay()
    {
        const auto& rs = session.getRecordSettings();
        transportBar.setRecordModeText ("Rec: " + juce::String (model::recordModeName (rs.mode)));
        auto roll = [] (bool on, double seconds) { return on ? juce::String (seconds, 1) + "s" : juce::String ("off"); };
        transportBar.setRollText ("Pre " + roll (rs.preRoll, rs.preRollSeconds) + " / Post " + roll (rs.postRoll, rs.postRollSeconds));
    }

    void showRecordModeMenu (juce::TextButton& button)
    {
        juce::PopupMenu menu;
        const auto current = session.getRecordSettings().mode;
        for (auto m : { model::RecordMode::normal, model::RecordMode::quickPunch, model::RecordMode::trackPunch, model::RecordMode::loop })
            menu.addItem ((int) m + 1, model::recordModeName (m), ! engine.getRecorder().isRecording(), current == m);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button), [this] (int result)
        {
            if (result == 0) return;
            auto rs = session.getRecordSettings();
            rs.mode = (model::RecordMode) (result - 1);
            session.execute (std::make_unique<model::SetRecordSettingsCommand> (rs));
            refreshRecordSettingsDisplay();
            statusMessage = juce::String ("Record mode: ") + model::recordModeName (rs.mode)
                          + (rs.mode == model::RecordMode::quickPunch ? "  (Record starts rolling; Record again punches in/out)"
                             : rs.mode == model::RecordMode::trackPunch ? "  (arm tracks, Record starts rolling, each track's R punches it in/out)"
                             : rs.mode == model::RecordMode::loop ? "  (Cycle range: every pass becomes a take)"
                             : "  (a time selection sets the punch-in/out points)");
            updateStatus();
        });
    }

    void showRollMenu (juce::TextButton& button)
    {
        juce::PopupMenu menu, pre, post;
        const auto& rs = session.getRecordSettings();
        const double bar = engine.getTransport().beatsToSeconds (engine.getTransport().getBeatsPerBar());
        struct Choice { int id; const char* name; double seconds; };
        const Choice choices[] = { { 1, "Off", 0.0 }, { 2, "1 bar", bar }, { 3, "2 bars", 2 * bar }, { 4, "4 bars", 4 * bar }, { 5, "1 second", 1.0 }, { 6, "2 seconds", 2.0 } };
        for (const auto& c : choices)
        {
            pre.addItem (100 + c.id, c.name, true, c.id == 1 ? ! rs.preRoll : rs.preRoll && std::abs (rs.preRollSeconds - c.seconds) < 0.01);
            post.addItem (200 + c.id, c.name, true, c.id == 1 ? ! rs.postRoll : rs.postRoll && std::abs (rs.postRollSeconds - c.seconds) < 0.01);
        }
        menu.addSubMenu ("Pre-roll", pre);
        menu.addSubMenu ("Post-roll", post);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button), [this, choices] (int result)
        {
            if (result == 0) return;
            auto settings = session.getRecordSettings();
            const bool isPre = result < 200;
            const auto& c = choices[(result % 100) - 1];
            if (isPre) { settings.preRoll = c.seconds > 0.0; if (c.seconds > 0.0) settings.preRollSeconds = c.seconds; }
            else       { settings.postRoll = c.seconds > 0.0; if (c.seconds > 0.0) settings.postRollSeconds = c.seconds; }
            session.execute (std::make_unique<model::SetRecordSettingsCommand> (settings));
            refreshRecordSettingsDisplay();
        });
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
            const auto [first, num] = session.resolveInput (track);
            slot.firstInput = first;
            slot.numInputs  = num;
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

        auto& transport = engine.getTransport();
        const auto rs = session.getRecordSettings();
        const bool manual = rs.mode == model::RecordMode::quickPunch || rs.mode == model::RecordMode::trackPunch;

        // Punch points: the time selection (main lanes only).
        const auto& sel = trackArea.getTimeSelection();
        const bool hasRange = sel.isValid() && ! sel.isOnAlternate();
        const juce::int64 punchIn = hasRange ? (juce::int64) std::llround (sel.start * engine.getSampleRate()) : -1;
        const juce::int64 punchOut = hasRange ? (juce::int64) std::llround (sel.end * engine.getSampleRate()) : -1;

        if (rs.mode == model::RecordMode::loop && hasRange) { transport.setLoopRange (punchIn, punchOut); transport.setLoopEnabled (true); }

        if (const auto error = engine.getRecorder().start (slots, engine.getSampleRate(), 24,
                                                           manual ? engine::Recorder::PunchMode::manual : engine::Recorder::PunchMode::whole);
            error.isNotEmpty())
        {
            trackArea.clearLiveThumbnails();
            statusMessage = error;
            updateStatus();
            return;
        }

        // Loop recording: with Cycle on, every pass becomes a take (playlist).
        loopRecording = transport.hasValidLoop() && ! manual;
        loopRecordStart = transport.getLoopStart();
        loopRecordEnd = transport.getLoopEnd();
        autoStopSample = -1;
        for (int i = 0; i < session.getNumTracks(); ++i)
            if (session.getTracks()[(size_t) i].punched) session.execute (std::make_unique<model::SetTrackPunchCommand> (i, false));

        juce::String how;
        if (rs.mode == model::RecordMode::normal && hasRange && ! loopRecording)
        {
            engine.getRecorder().setAutoPunch (punchIn, punchOut);
            if (rs.postRoll) autoStopSample = punchOut + (juce::int64) std::llround (rs.postRollSeconds * engine.getSampleRate());
            how = "punching " + juce::String (sel.start, 2) + " - " + juce::String (sel.end, 2) + " s";
        }
        // Pre-roll: start rolling before the punch-in point (or the cycle start).
        if (! transport.isPlaying())
        {
            const juce::int64 anchor = hasRange ? punchIn : loopRecording ? loopRecordStart : -1;
            if (anchor >= 0 && rs.preRoll)
                transport.setPositionSamples (juce::jmax<juce::int64> (0, anchor - (juce::int64) std::llround (rs.preRollSeconds * engine.getSampleRate())));
            else if (anchor >= 0 && ! manual)
                transport.setPositionSamples (anchor);
        }

        transport.setRecordEnabled (! manual);
        transportBar.setWaitingForPunch (manual);
        if (! transport.isPlaying())
            transport.play();

        statusMessage = (loopRecording ? "Loop recording " : manual ? juce::String (model::recordModeName (rs.mode)) + ": rolling, " : "Recording ")
                      + juce::String (slots.size()) + (slots.size() == 1 ? " track" : " tracks")
                      + (manual ? (rs.mode == model::RecordMode::quickPunch ? " - press Record to punch in" : " - click a track's R to punch it in") : "...")
                      + (how.isNotEmpty() ? "  (" + how + ")" : juce::String());
        updateStatus();
    }

    void finishRecording()
    {
        auto& recorder = engine.getRecorder();
        auto& transport = engine.getTransport();
        const int dropouts = recorder.getDropoutCount();
        const auto takes = recorder.stop();

        transport.setRecordEnabled (false);
        transportBar.setWaitingForPunch (false);
        autoStopSample = -1;
        trackArea.clearLiveThumbnails();
        for (int i = 0; i < session.getNumTracks(); ++i)
            if (session.getTracks()[(size_t) i].punched) session.execute (std::make_unique<model::SetTrackPunchCommand> (i, false));
        const bool punchMode = session.getRecordSettings().mode == model::RecordMode::quickPunch || session.getRecordSettings().mode == model::RecordMode::trackPunch;

        int imported = 0, passesTotal = 0;
        for (const auto& take : takes)
        {
            const int trackIndex = session.indexOfTrackId (take.trackId);
            if (trackIndex < 0) continue;

            juce::String error;
            const auto loaded = loader.load (take.file, engine.getSampleRate(), error);
            if (! loaded) { statusMessage = error; continue; }

            // Loop record splits the whole take into passes; otherwise each punched range is a clip.
            std::vector<model::Playlists::Pass> passes;
            if (loopRecording)
                passes = model::Playlists::loopPasses (take.startSample, loaded->numSamples, loopRecordStart, loopRecordEnd, (juce::int64) (0.1 * engine.getSampleRate()));
            else
                for (const auto& r : take.ranges) passes.push_back ({ r.fileOffset, r.length, r.timelineStart });
            if (passes.empty()) continue;

            auto compound = std::make_unique<model::CompoundCommand> (loopRecording && passes.size() > 1 ? "Loop Record" : punchMode ? "Punch Record" : "Record");
            const auto* track = session.getTrack (trackIndex);
            const int existingTakes = (int) track->alternates.size();

            for (size_t p = 0; p < passes.size(); ++p)
            {
                model::AudioClip clip;
                clip.name          = take.file.getFileNameWithoutExtension() + (passes.size() > 1 ? "-" + juce::String ((int) p + 1) : juce::String());
                clip.sourceFile    = take.file;
                clip.audio         = loaded->audio;
                clip.sampleRate    = loaded->sampleRate;
                clip.timelineStart = passes[p].timelineStart;
                clip.sourceOffset  = passes[p].fileOffset;
                clip.length        = passes[p].length;

                const bool last = p + 1 == passes.size();
                if (last || ! loopRecording)
                {
                    // Punch ranges all land on the main playlist as separate clips.
                    // The final pass lands on the main playlist (Pro Tools behaviour); earlier passes are alternates.
                    if (loopRecording && passes.size() > 1) compound->add (std::make_unique<model::NewPlaylistCommand> (trackIndex, model::defaultPlaylistName (*track, existingTakes + (int) passes.size())));
                    compound->add (std::make_unique<model::AddClipCommand> (trackIndex, std::move (clip)));
                }
                else
                {
                    model::Playlist alt;
                    alt.name = model::defaultPlaylistName (*track, existingTakes + (int) p + 1);
                    alt.clips.push_back (std::move (clip));
                    compound->add (std::make_unique<model::AddAlternatePlaylistCommand> (trackIndex, std::move (alt)));
                }
            }
            session.execute (std::move (compound));
            if (loopRecording && passes.size() > 1) trackArea.setPlaylistsShown (trackIndex, true);
            passesTotal += (int) passes.size();
            ++imported;
        }

        if (imported > 0)
            statusMessage = "Recorded " + juce::String (passesTotal) + (passesTotal == 1 ? " take" : " takes")
                          + (passesTotal > imported ? " across " + juce::String (imported) + (imported == 1 ? " track" : " tracks") : juce::String())
                          + (dropouts > 0 ? "  (WARNING: " + juce::String (dropouts) + " disk dropouts)" : juce::String());
        else if (takes.empty())
            statusMessage = "Nothing recorded";
        updateStatus();
    }

    void timerCallback() override
    {
        if (++autosaveCounter >= 20 * 60 * 3) { autosaveCounter = 0; autosaveTick(); }   // every 3 minutes at 20 Hz
        // Post-roll reached: stop (the take is finished below).
        if (engine.getRecorder().isRecording() && autoStopSample >= 0 && engine.getTransport().getPositionSamples() >= autoStopSample)
            engine.getTransport().stop();
        // Stopping the transport ends the take.
        if (engine.getRecorder().isRecording() && ! engine.getTransport().isPlaying())
            finishRecording();

        automation.tick();
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

    void addInstrumentTrack (engine::InstrumentType type)
    {
        if (type == engine::InstrumentType::none) type = engine::InstrumentType::subtractive;
        const auto presets = engine::Instrument::presets (type);
        const size_t startPreset = type == engine::InstrumentType::subtractive && presets.size() > 1 ? 1 : 0;   // Pluck

        int count = 0;
        for (const auto& t : session.getTracks()) count += t.instrumentType() == type ? 1 : 0;

        model::Track track;
        track.name             = juce::String (engine::Instrument::typeName (type)) + " " + juce::String (count + 1);
        track.type             = model::Track::Type::instrument;
        track.instrumentKind   = model::Track::InstrumentKind::synth;
        track.colour           = model::Session::colourForTrackIndex (session.getNumTracks());
        track.instrument       = engine::Instrument::create (type, engine.getSampleRate());
        track.instrumentParams = std::make_shared<const engine::InstrumentParams> (presets[startPreset]);

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

        if (type == engine::InstrumentType::sampler)
        {
            statusMessage = "Sampler: drop an audio file onto the track to load it";
            updateStatus();
        }
    }

    void loadSamplerSample (int trackIndex, const juce::File& file)
    {
        auto* track = session.getTrack (trackIndex);
        if (track == nullptr || ! track->hasInstrument() || track->instrumentType() != engine::InstrumentType::sampler) return;

        juce::String error;
        const auto loaded = loader.load (file, engine.getSampleRate(), error);
        if (! loaded) { statusMessage = error; updateStatus(); return; }

        auto p = std::make_shared<engine::InstrumentParams> (*track->instrumentParams);
        p->sample     = loaded->audio;
        p->sampleRate = loaded->sampleRate;
        p->sampleName = file.getFileNameWithoutExtension();
        p->rootNote   = 60;
        session.execute (std::make_unique<model::SetInstrumentParamsCommand> (trackIndex, std::move (p), "Load Sample"));
        statusMessage = "Loaded " + file.getFileName() + " into " + track->name;
        updateStatus();
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

    //==========================================================================
    // Session files, templates, memory locations

    static juce::File sessionsFolder()  { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Sessions"); }
    static juce::File templatesFolder() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Templates"); }

    persistence::TransportState transportState() const
    {
        persistence::TransportState t;
        const auto& tr = engine.getTransport();
        t.bpm = tr.getBpm(); t.beatsPerBar = tr.getBeatsPerBar();
        t.loopStart = tr.getLoopStart(); t.loopEnd = tr.getLoopEnd(); t.loopEnabled = tr.isLoopEnabled();
        return t;
    }

    persistence::LoadContext loadContext()
    {
        persistence::LoadContext ctx;
        ctx.sampleRate = engine.getSampleRate();
        ctx.loadAudio = [this] (const juce::File& f) -> std::shared_ptr<const juce::AudioBuffer<float>>
        {
            juce::String error;
            auto loaded = loader.load (f, engine.getSampleRate(), error);
            return loaded ? loaded->audio : nullptr;
        };
        ctx.defaultKit = [this]
        {
            if (defaultKit == nullptr) defaultKit = engine::DrumKitFactory::createDefaultKit (engine.getSampleRate());
            return defaultKit;
        };
        ctx.instantiatePlugin = [this] (const juce::String& id, juce::String& error) -> std::shared_ptr<engine::Effect>
        {
            auto desc = pluginManager.getKnownPlugins().getTypeForIdentifierString (id);
            if (desc == nullptr) { error = "not in the plugin list"; return nullptr; }
            return pluginManager.instantiate (*desc, engine.getSampleRate(), engine::AudioGraph::maxBlock, error);
        };
        return ctx;
    }

    void updateWindowTitle()
    {
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setName ((sessionFile != juce::File() ? persistence::SessionFile::sessionName (sessionFile) : juce::String ("Untitled")) + " - Beat Maker");
    }

    // Writes the session to `bundle` (a .bmk or .bmkt directory).
    bool writeSession (const juce::File& bundle, bool asTemplate)
    {
        if (const auto error = persistence::SessionFile::save (session, transportState(), bundle); error.isNotEmpty())
        {
            statusMessage = "Save failed: " + error; updateStatus(); return false;
        }
        if (! asTemplate) { sessionFile = bundle; savedHistorySize = session.getHistory().getUndoName().hashCode(); updateWindowTitle(); }
        statusMessage = (asTemplate ? "Saved template " : "Saved ") + bundle.getFileName();
        updateStatus();
        return true;
    }

    void saveSession (bool forceChooser)
    {
        if (sessionFile != juce::File() && ! forceChooser) { writeSession (sessionFile, false); return; }
        sessionsFolder().createDirectory();
        const auto suggested = sessionsFolder().getChildFile ((sessionFile != juce::File() ? persistence::SessionFile::sessionName (sessionFile) : juce::String ("Untitled")) + ".bmk");
        fileChooser = std::make_unique<juce::FileChooser> ("Save Session As", suggested, "*.bmk");
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (f == juce::File()) return;
                                      if (! f.hasFileExtension ("bmk")) f = f.withFileExtension ("bmk");
                                      writeSession (f, false);
                                      grabKeyboardFocus();
                                  });
    }

    void saveAsTemplate()
    {
        templatesFolder().createDirectory();
        auto* window = new juce::AlertWindow ("Save As Template", "Template name:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", sessionFile != juce::File() ? persistence::SessionFile::sessionName (sessionFile) : "My Template");
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
                writeSession (templatesFolder().getChildFile (juce::File::createLegalFileName (name) + ".bmkt"), true);
            grabKeyboardFocus();
        }), true);
    }

    bool openSession (const juce::File& bundle)
    {
        if (engine.getRecorder().isRecording()) finishRecording();
        engine.getTransport().stop();
        persistence::TransportState ts;
        juce::StringArray warnings;
        if (const auto error = persistence::SessionFile::load (session, ts, bundle, loadContext(), warnings); error.isNotEmpty())
        {
            statusMessage = "Open failed: " + error; updateStatus(); return false;
        }
        auto& tr = engine.getTransport();
        tr.setBpm (ts.bpm); tr.setBeatsPerBar (ts.beatsPerBar);
        tr.setLoopRange (ts.loopStart, ts.loopEnd); tr.setLoopEnabled (ts.loopEnabled);
        tr.setPositionSamples (0);
        trackArea.clearSelection();
        trackArea.setSelectedTrack (session.getNumTracks() > 0 ? 0 : -1);
        trackArea.zoomToFit();
        sessionFile = bundle.hasFileExtension ("bmk") ? bundle : juce::File();   // templates open as Untitled
        updateWindowTitle();
        refreshRecordSettingsDisplay();
        statusMessage = "Opened " + bundle.getFileNameWithoutExtension() + "  (" + juce::String (session.getNumTracks()) + " tracks)"
                      + (warnings.isEmpty() ? juce::String() : "  WARNING: " + juce::String (warnings.size()) + " item(s) missing: " + warnings[0]);
        updateStatus();
        return true;
    }

    void openSessionChooser()
    {
        sessionsFolder().createDirectory();
        fileChooser = std::make_unique<juce::FileChooser> ("Open Session or Audio File", sessionsFolder(), "*.bmk;*.bmkt;" + loader.getWildcard());
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (f == juce::File()) return;
                                      if (f.getFileName() == "session.json") f = f.getParentDirectory();
                                      if (persistence::SessionFile::isSessionBundle (f)) openSession (f);
                                      else if (f.existsAsFile()) importAudioFile (f);
                                      grabKeyboardFocus();
                                  });
    }

    void newEmptySession (const juce::String& name)
    {
        if (engine.getRecorder().isRecording()) finishRecording();
        engine.getTransport().stop();
        session.execute (std::make_unique<model::LoadSessionCommand> (model::LoadSessionCommand::Contents {}));
        session.clearHistory();
        engine.getTransport().setLoopEnabled (false);
        engine.getTransport().setPositionSamples (0);
        trackArea.clearSelection();
        sessionFile = juce::File();
        untitledName = name;
        updateWindowTitle();
        refreshRecordSettingsDisplay();
    }

    // Built-in templates are recipes applied to an empty session.
    std::vector<ui::NewSessionDialog::Choice> templateChoices()
    {
        std::vector<ui::NewSessionDialog::Choice> out {
            { "Empty", "No tracks. Add what you need.", {} },
            { "Beat Making", "Drum Machine, Synth, Bass and two audio tracks, 90 BPM, Cycle over 4 bars.", {} },
            { "Songwriter", "Vocal and guitar tracks with a Concert Hall reverb bus and a master limiter, 120 BPM.", {} },
            { "Podcast", "Two mono voice tracks with EQ and compression, a limiter on the master.", {} } };
        for (const auto& f : templatesFolder().findChildFiles (juce::File::findDirectories, false, "*.bmkt"))
            if (persistence::SessionFile::isSessionBundle (f))
                out.push_back ({ f.getFileNameWithoutExtension(), "Saved " + f.getLastModificationTime().toString (true, true, false), f });
        return out;
    }

    void applyTemplate (const ui::NewSessionDialog::Choice& choice, const juce::String& name)
    {
        if (choice.templateFile != juce::File()) { openSession (choice.templateFile); untitledName = name; updateWindowTitle(); return; }
        newEmptySession (name);
        const double sr = engine.getSampleRate();
        auto& tr = engine.getTransport();
        if (choice.name == "Beat Making")
        {
            tr.setBpm (90.0);
            addDrumMachineTrack();
            addInstrumentTrack (engine::InstrumentType::subtractive);
            addInstrumentTrack (engine::InstrumentType::bass);
            addTrack ("Vocal");
            addTrack ("Sample");
            tr.setLoopRange (0, (juce::int64) std::llround (tr.beatsToSeconds (16.0) * sr));
            tr.setLoopEnabled (true);
        }
        else if (choice.name == "Songwriter")
        {
            tr.setBpm (120.0);
            const int vox = addTrack ("Vocal"), gtr = addTrack ("Guitar");
            session.execute (std::make_unique<model::SetInsertCommand> (vox, 0, engine::EffectType::eq, sr));
            session.execute (std::make_unique<model::SetInsertCommand> (vox, 1, engine::EffectType::compressor, sr));
            addAuxTrack();
            const int verb = session.getNumTracks() - 1;
            session.execute (std::make_unique<model::SetTrackRoutingCommand> (verb, 0, -1));
            session.execute (std::make_unique<model::SetInsertCommand> (verb, 0, engine::EffectType::convolution, sr));
            model::Send send; send.bus = 0; send.gain = 0.5f;
            session.execute (std::make_unique<model::SetSendCommand> (vox, 0, send));
            session.execute (std::make_unique<model::SetSendCommand> (gtr, 0, send));
            session.execute (std::make_unique<model::SetInsertCommand> (-1, 0, engine::EffectType::limiter, sr));
        }
        else if (choice.name == "Podcast")
        {
            for (const char* n : { "Host", "Guest" })
            {
                const int t = addTrack (n);
                session.execute (std::make_unique<model::SetInsertCommand> (t, 0, engine::EffectType::eq, sr));
                session.execute (std::make_unique<model::SetInsertCommand> (t, 1, engine::EffectType::compressor, sr));
            }
            session.execute (std::make_unique<model::SetInsertCommand> (-1, 0, engine::EffectType::limiter, sr));
        }
        session.clearHistory();
        trackArea.setSelectedTrack (session.getNumTracks() > 0 ? 0 : -1);
        statusMessage = "New session from template: " + choice.name;
        updateStatus();
    }

    void showNewSessionDialog()
    {
        auto* dialog = new ui::NewSessionDialog (templateChoices(), "Untitled");
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "New Session";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        dialog->onCancel = [window] { window->setVisible (false); };
        dialog->onCreate = [this, window] (const ui::NewSessionDialog::Choice& c, const juce::String& name)
        {
            window->setVisible (false);
            applyTemplate (c, name);
            grabKeyboardFocus();
        };
    }

    void showFileMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "New Session...  (Ctrl+N)");
        menu.addItem (2, "Open Session or Audio...  (Ctrl+O)");
        menu.addSeparator();
        menu.addItem (3, "Save  (Ctrl+S)");
        menu.addItem (4, "Save As...  (Ctrl+Shift+S)");
        menu.addItem (5, "Save As Template...");
        menu.addSeparator();
        menu.addItem (6, "Import Audio Files...");
        menu.addItem (7, "Memory Locations...  (Ctrl+5)");
        menu.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            switch (r)
            {
                case 1: showNewSessionDialog(); break;
                case 2: openSessionChooser(); break;
                case 3: saveSession (false); break;
                case 4: saveSession (true); break;
                case 5: saveAsTemplate(); break;
                case 6: openFileChooser(); break;
                case 7: showMemoryLocations(); break;
                default: break;
            }
        });
    }

    void showMemoryLocations()
    {
        if (memoryWindow != nullptr) { memoryWindow->setVisible (true); memoryWindow->toFront (true); return; }
        auto* content = new ui::MemoryLocationsWindow (session);
        content->onRecall = [this] (int id) { trackArea.recallMarker (id); };
        content->onRename = [this] (int id) { trackArea.showMarkerMenu (id, 0.0, juce::Desktop::getMousePosition()); };
        content->onAdd = [this] { trackArea.addMarkerAtPlayhead (false); };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Memory Locations";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        memoryWindow = options.launchAsync();
    }

    // Autosave every few minutes into the bundle's backups folder.
    void autosaveTick()
    {
        if (sessionFile == juce::File() || ! session.getHistory().canUndo()) return;
        const auto backups = sessionFile.getChildFile ("Session File Backups");
        backups.createDirectory();
        const auto target = backups.getChildFile ("autosave-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".bmkt");
        persistence::SessionFile::save (session, transportState(), target);
        // keep the five newest
        auto old = backups.findChildFiles (juce::File::findDirectories, false, "autosave-*.bmkt");
        old.sort();
        while (old.size() > 5) { old.getReference (0).deleteRecursively(); old.remove (0); }
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
    ui::TrackArea trackArea { session, engine.getTransport(), engine.getGraph(), loader.getFormatManager(), editSettings };
    ui::StepSequencer sequencer { session, engine.getTransport(), engine.getGraph() };
    ui::PianoRoll pianoRoll { session, engine.getTransport(), engine.getGraph() };
    ui::SmartControls smartControls { session };
    ui::MixerView mixerView { session, engine.getGraph(), [this] { return engine.getSampleRate(); } };
    model::AutomationRecorder automation { session, engine.getTransport() };
    bool controlsVisible = true;
    bool mixerVisible = false;
    persistence::LoopLibrary loopLibrary { loader.getFormatManager() };
    ui::LoopBrowser loopBrowser { loopLibrary };
    std::unique_ptr<juce::PropertiesFile> appSettings;
    std::shared_ptr<const juce::AudioBuffer<float>> previewAudio;
    bool libraryVisible = true;
    std::shared_ptr<const engine::DrumKit> defaultKit;
    bool editorVisible = true;
    bool loopRecording = false;
    juce::int64 loopRecordStart = 0, loopRecordEnd = 0;
    juce::int64 autoStopSample = -1;   // post-roll end while punch recording
    juce::File sessionFile;            // the open .bmk bundle (empty = Untitled)
    juce::String untitledName = "Untitled";
    int savedHistorySize = 0, autosaveCounter = 0;
    juce::Component::SafePointer<juce::DialogWindow> memoryWindow;
    juce::Label statusLabel;
    juce::String statusMessage;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Component::SafePointer<juce::DialogWindow> beatDetectiveWindow;
    juce::Component::SafePointer<ui::BeatDetectiveDialog> beatDetectiveDialog;
    std::unique_ptr<BounceJob> bounceJob;

    plugins::PluginManager pluginManager {
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Beat Maker").getChildFile ("plugins.xml"),
        juce::File::getSpecialLocation (juce::File::currentExecutableFile) };
    struct OpenPluginWindow { plugins::PluginEffect* effect; std::unique_ptr<ui::PluginWindow> window; };
    std::vector<OpenPluginWindow> pluginWindows;
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
        // Out-of-process plugin validation: scan one plugin, print its descriptions, exit.
        for (const auto& arg : getCommandLineParameterArray())
            if (arg.startsWith ("--scan-plugin="))
            {
                const auto spec = arg.fromFirstOccurrenceOf ("=", false, false);
                const int code = plugins::PluginManager::runScanChild (spec.upToFirstOccurrenceOf ("|", false, false),
                                                                       spec.fromFirstOccurrenceOf ("|", false, false));
                setApplicationReturnValue (code);
                quit();
                return;
            }

        std::signal (SIGTERM, onTerminationSignal);
        std::signal (SIGINT,  onTerminationSignal);
        quitPoller.startTimer (100);

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // Command line: audio files are imported; --drums adds a Drum Machine
        // track with the starter beat; --synth adds a Synth track with an arpeggio; --instrument=<name> adds any bundled
        // instrument (fmsynth, wavetable, sampler, electricpiano, bass); --sample=<file> loads a file into the last Sampler
        // track; --record adds an armed audio track and
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
            else if (arg.startsWith ("--instrument=")) main.addInstrumentTrackFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--sample=")) main.loadSampleFromCommandLine (juce::File::getCurrentWorkingDirectory()
                                                                                       .getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg == "--record") main.addArmedAudioTrackAndRecord();
            else if (arg.startsWith ("--record-mode=")) main.setRecordModeFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--pre-roll=")) main.setRollFromCommandLine (true, arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue());
            else if (arg.startsWith ("--post-roll=")) main.setRollFromCommandLine (false, arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue());
            else if (arg.startsWith ("--punch="))
            {
                const auto parts = juce::StringArray::fromTokens (arg.fromFirstOccurrenceOf ("=", false, false), ",", {});
                if (parts.size() == 2) main.setPunchRangeFromCommandLine (parts[0].getDoubleValue(), parts[1].getDoubleValue());
            }
            else if (arg.startsWith ("--session=")) main.openSessionFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg.startsWith ("--save=")) main.saveSessionFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg.startsWith ("--template=")) main.templateFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--marker-demo") main.markerDemoFromCommandLine();
            else if (arg.startsWith ("--stop-at="))    // seconds after launch: stop the transport (smoke tests)
                juce::Timer::callAfterDelay (juce::roundToInt (arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue() * 1000.0), [&main] { main.stopTransportFromCommandLine(); });
            else if (arg.startsWith ("--punch-at="))   // seconds after launch: toggle punch (QuickPunch/TrackPunch smoke tests)
                juce::Timer::callAfterDelay (juce::roundToInt (arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue() * 1000.0), [&main] { main.punchFromCommandLine(); });
            else if (arg == "--cycle")  main.setCycleEnabled (true);
            else if (arg == "--play")   play = true;
            else if (arg.startsWith ("--bounce=")) bounceFile = juce::File::getCurrentWorkingDirectory()
                                                                    .getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--bounce-dialog") main.showBounceDialog();
            else if (arg == "--mixer")  main.setMixerVisibleFromCommandLine (true);
            else if (arg == "--automation-demo") main.automationDemoFromCommandLine();
            else if (arg == "--io-setup") main.showIOSetupDialogFromCommandLine();
            else if (arg == "--vca") main.addVcaTrackFromCommandLine();
            else if (arg == "--playlist-demo") main.playlistDemoFromCommandLine();
            else if (arg == "--clip-gain-demo") main.clipGainDemoFromCommandLine();
            else if (arg == "--scan-plugins") main.scanPluginsFromCommandLine();
            else if (arg.startsWith ("--insert-plugin=")) main.afterScan ([&main, name = arg.fromFirstOccurrenceOf ("=", false, false)] { main.insertPluginFromCommandLine (name); });
            else if (arg == "--insert-demo") main.insertDemoFromCommandLine();
            else if (arg == "--elastic-demo") main.elasticDemoFromCommandLine();
            else if (arg == "--sidechain-demo") main.sidechainDemoFromCommandLine();
            else if (arg == "--convolution-demo") main.convolutionDemoFromCommandLine();
            else if (arg == "--pitch-demo") main.pitchDemoFromCommandLine();
            else if (arg == "--beat-detective-demo") main.beatDetectiveDemoFromCommandLine();
            else if (arg.startsWith ("--elastic-async=")) ui::ElasticJob::asyncThresholdSeconds = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            else if (arg == "--group-demo") main.groupDemoFromCommandLine();
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
