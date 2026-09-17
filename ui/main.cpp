// Beat Maker — application entry point.
//
// Wires the engine (audio device + transport + render graph), the model
// (undoable session document) and the surface UI (transport bar + track area)
// together. All edits flow: UI -> Command -> Session -> RenderSnapshot -> engine.

#include "depth/BounceDialog.h"
#include "depth/BeatDetectiveDialog.h"
#include "depth/MemoryLocationsWindow.h"
#include "depth/CommandPalette.h"
#include "depth/MidiEventList.h"
#include "depth/ScriptConsole.h"
#include "depth/StemExportDialog.h"
#include "depth/ImportSessionDialog.h"
#include "depth/WelcomeWindow.h"
#include "depth/TourOverlay.h"
#include "depth/TutorialWindow.h"
#include "depth/ShortcutsWindow.h"
#include "depth/SystemUsageWindow.h"
#include "depth/AafExportDialog.h"
#include "depth/CrashReportWindow.h"
#include "shared/CrashReporter.h"
#include "shared/SessionChooser.h"
#include "depth/InstrumentChooser.h"
#include "depth/DrumKitChooser.h"
#include "depth/KitBuilder.h"
#include "depth/PresetBuilder.h"
#include "depth/EffectEditor.h"
#include "../persistence/UserPresets.h"
#include "../persistence/UserKits.h"
#include <map>
#include "shared/UiProfiler.h"
#include <cstdlib>
#include <AafExport.h>
#include <ClipLoop.h>
#include <SampleProjects.h>
#include <SessionImport.h>
#include <Freeze.h>
#include <LuaEngine.h>
#include "depth/SyncDialog.h"
#include <sync/MidiSyncController.h>
#include "depth/PreferencesWindow.h"
#include "shared/CommandRegistry.h"
#include "shared/Preferences.h"
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
        ui::theme::applyPalette (ui::theme::palette (juce::jlimit (0, 2, ui::Preferences (preferencesFile()).getInt ("display.theme"))));
        ui::theme::applyLookAndFeel (lookAndFeel);
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
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
        editSettings.onChanged = [this] { editToolbar.refresh(); trackArea.repaint(); pianoRoll.repaint(); sequencer.repaint(); };
        editToolbar.editorHint = [this] { auto* ed = activeEditor(); return ed != nullptr ? ed->keyHint() : juce::String(); };
        pianoRoll.setEditSettings (&editSettings);
        pianoRoll.onRowHeightChanged = [this] (int h) { if (prefs.getInt ("display.pianoRollRowHeight") != h) prefs.set ("display.pianoRollRowHeight", h); };
        addChildComponent (editorResizer);
        editorResizer.onDrag = [this] (int deltaFromStart, int startHeight)
        {
            prefs.set ("display.editorHeight", (double) juce::jlimit (120, 1200, startHeight - deltaFromStart));
        };
        editorResizer.currentHeight = [this] { return pianoRoll.isVisible() ? pianoRoll.getHeight() : sequencer.getHeight(); };
        pianoRoll.onStatus = [this] (const juce::String& s) { statusMessage = s; updateStatus(); };
        pianoRoll.viewSource = [this]
        {
            ui::TimelineView v;
            trackArea.getView (v.startSeconds, v.pixelsPerSecond);
            return v;   // the editor keeps its own left edge and width: it spans the panel whether linked or not
        };
        pianoRoll.onViewChanged = [this] (double start, double pps) { trackArea.setView (start, pps); };
        pianoRoll.onClipCreate = [this] (int track, juce::int64 start, juce::int64 length) -> int
        {
            auto* t = session.getTrack (track);
            if (t == nullptr) return -1;
            model::MidiClip c; c.name = "Clip " + juce::String (t->midiClips.size() + 1); c.sampleRate = engine.getSampleRate(); c.timelineStart = start; c.length = length;
            auto seq = std::make_shared<engine::MidiSequence>(); seq->lengthBeats = juce::jmax (0.25, engine.getTransport().secondsToBeats ((double) length / c.sampleRate));
            c.sequence = seq; c.loop = false;
            session.execute (std::make_unique<model::AddMidiClipCommand> (track, std::move (c)));
            return (int) session.getTracks()[(size_t) track].midiClips.size() - 1;
        };
        sequencer.onClipCreate = [this] (int track, juce::int64 start, juce::int64 length) -> int
        {
            auto* t = session.getTrack (track);
            if (t == nullptr) return -1;
            model::PatternClip c; c.name = "Beat " + juce::String (t->patternClips.size() + 1); c.sampleRate = engine.getSampleRate(); c.timelineStart = start; c.length = length;
            auto p = std::make_shared<engine::StepPattern>(); p->numSteps = juce::jlimit (1, engine::StepPattern::maxSteps, (int) std::round (engine.getTransport().secondsToBeats ((double) length / c.sampleRate) * p->stepsPerBeat));
            c.pattern = p; c.loop = false;
            session.execute (std::make_unique<model::AddPatternClipCommand> (track, std::move (c)));
            return (int) session.getTracks()[(size_t) track].patternClips.size() - 1;
        };
        pianoRoll.onClipLoopChanged = [this] (int track, int clip, bool on)
        {
            if (auto cmd = model::ClipLoop::setLoop (session, { track, model::ClipRef::Kind::midi, clip }, on)) session.execute (std::move (cmd));
            statusMessage = on ? "Looping: one extra pass added (drag the clip's right edge for more)" : "Clip back to its own length"; updateStatus();
        };
        sequencer.onClipLoopChanged = [this] (int track, int clip, bool on)
        {
            if (auto cmd = model::ClipLoop::setLoop (session, { track, model::ClipRef::Kind::pattern, clip }, on)) session.execute (std::move (cmd));
            statusMessage = on ? "Looping: one extra pass added (drag the clip's right edge for more)" : "Clip back to its own length"; updateStatus();
        };
        pianoRoll.onClipExtend = [this] (int track, int clip, juce::int64 newLength)
        {
            auto* t = session.getTrack (track);
            if (t == nullptr || ! juce::isPositiveAndBelow (clip, (int) t->midiClips.size())) return;
            session.execute (std::make_unique<model::TrimClipCommand> (model::ClipRef { track, model::ClipRef::Kind::midi, clip }, t->midiClips[(size_t) clip].timelineStart, newLength));
        };
        editToolbar.notesFocused = [this] { return notesFocused(); };
        editToolbar.onFocusNotes = [this] (bool notes) { focusNotes (notes); };
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
        addAndMakeVisible (statusLabel);

        setupLoopLibrary();
        statusLabel.setColour (juce::Label::textColourId, ui::theme::textDim);
        statusLabel.setFont (juce::FontOptions (12.0f));
        updateStatus();

        transportBar.onOpenFile = [this] { showFileMenu(); };
        trackArea.onOpenMemoryLocations = [this] { showMemoryLocations(); };
        trackArea.onTrackAction = [this] (int i, const juce::String& action)
        {
            if (action == "freeze") freezeTrack (i);
            else if (action == "unfreeze") unfreezeTrack (i);
            else if (action == "commit") commitTrack (i);
            else if (action == "delete") { session.execute (std::make_unique<model::RemoveTrackCommand> (i)); trackArea.setSelectedTrack (-1); }
            else if (action == "rename") promptRenameTrack (i);
        };
        trackArea.onTrackColour = [this] (int i, juce::Colour c) { setTrackColour (i, c); };
        trackArea.onOutputChanged = [this] (int i, int bus, int path)
        {
            auto* t = session.getTrack (i);
            if (t == nullptr) return;
            session.execute (std::make_unique<model::SetTrackRoutingCommand> (i, t->inputBus, bus));
            if (bus < 0) session.execute (std::make_unique<model::SetTrackPathsCommand> (i, t->inputPath, path));
        };
        trackArea.onOpenIOSetup = [this] { showIOSetupDialog(); };
        trackArea.onInputBusChanged = [this] (int i, int bus) { if (auto* t = session.getTrack (i)) session.execute (std::make_unique<model::SetTrackRoutingCommand> (i, bus, t->outputBus)); };
        trackArea.onSendChanged = [this] (int i, int slot, model::Send send) { session.execute (std::make_unique<model::SetSendCommand> (i, slot, send)); };
        mixerView.onLaneViewChanged = [this] (int i, int view) { trackArea.setLaneView (i, view); };
        trackArea.onFadesDialog = [this] { showFadesDialog(); };
        trackArea.onPanChanged = [this] (int i, float pan) { if (auto* t = session.getTrack (i)) session.execute (std::make_unique<model::SetTrackMixCommand> (i, t->gain, pan)); };
        trackArea.onRenameBus = [this] (int bus) { promptRenameBus (bus); };
        trackArea.onNewAuxForBus = [this] (int bus) { addAuxTrack (bus); };
        trackArea.onTrackEffect = [this] (int i, const juce::String& action, int slot, engine::EffectType type, juce::Rectangle<int> anchor) { trackEffect (i, action, slot, type, anchor); };
        trackArea.onTrackCustomColour = [this] (int i) { promptTrackColour (i); };
        transportBar.onRecord = [this] { toggleRecord(); };
        transportBar.onBounce = [this] { showBounceDialogImpl(); };
        transportBar.onEditorToggled = [this] (bool visible) { setEditorVisible (visible); };
        transportBar.onMetronomeToggled = [this] (bool on) { setMetronome (on); };
        transportBar.onPlay = [this] { startTransport (false); };
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
            if (auto* t = session.getTrack (trackIndex); t != nullptr && engine::Instrument::usesSample (t->instrumentType()) && ! files.isEmpty())
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
        trackArea.onAddFromLibrary = [this] { showLibraryWindow(); };
        trackArea.onAddTrack = [this] (model::Track::Type type, model::Track::InstrumentKind kind, engine::InstrumentType instrument)
        {
            if (type == model::Track::Type::vca)                              addVcaTrack();
            else if (type == model::Track::Type::aux)                         addAuxTrack();
            else if (type != model::Track::Type::instrument)                  addTrack ("Audio " + juce::String (session.getNumTracks() + 1));
            else if (kind == model::Track::InstrumentKind::synth)             addInstrumentTrack (instrument);
            else                                                              addDrumMachineTrack();
        };
        trackArea.onChooseInstrument = [this] { showInstrumentChooser(); };
        trackArea.onAddDrumTrack = [this] (const juce::String& kit) { addDrumMachineTrack (kit); };
        trackArea.onChooseKit = [this] { showDrumKitChooser(); };
        trackArea.onBuildKit = [this] { showKitBuilder ({}); };
        engine::DrumKitFactory::setCustomKits (persistence::UserKits::load (persistence::UserKits::defaultFolder()));   // the user's kits, from ~/Music/Beat Maker/Kits
        engine::Instrument::setUserPresets (persistence::UserPresets::load (persistence::UserPresets::defaultFolder()));   // and presets, from ~/Music/Beat Maker/Presets
        pianoRoll.onSavePreset = [this] (int track) { savePresetFromTrack (track); };
        trackArea.onSelectionChanged = [this] (int i) { updateSequencerTarget(); if (eventListContent != nullptr) eventListContent->setTrack (i); };
        midiSync.onTransportChanged = [this] { updateStatus(); };

        sequencer.onPatternChanged = [this] (int track, int clip, std::shared_ptr<const engine::StepPattern> pattern, juce::String name)
        {
            session.execute (std::make_unique<model::ReplacePatternCommand> (track, clip, std::move (pattern), std::move (name)));
        };
        sequencer.setEditSettings (&editSettings);
        sequencer.onStatus = [this] (const juce::String& s) { statusMessage = s; updateStatus(); };
        sequencer.viewSource = [this]
        {
            ui::TimelineView v;
            trackArea.getView (v.startSeconds, v.pixelsPerSecond);
            return v;
        };
        sequencer.onViewChanged = [this] (double start, double pps) { trackArea.setView (start, pps); };
        sequencer.onClipExtend = [this] (int track, int clip, juce::int64 newLength)
        {
            auto* t = session.getTrack (track);
            if (t == nullptr || ! juce::isPositiveAndBelow (clip, (int) t->patternClips.size())) return;
            session.execute (std::make_unique<model::TrimClipCommand> (model::ClipRef { track, model::ClipRef::Kind::pattern, clip }, t->patternClips[(size_t) clip].timelineStart, newLength));
        };
        sequencer.onPadSampleDropped = [this] (int track, int pad, const juce::File& file) { loadPadSample (track, pad, file); };
        sequencer.onKitChanged = [this] (int track, const juce::String& kitName)
        {
            auto* t = session.getTrack (track);
            if (t == nullptr || t->drumKit == nullptr || t->drumKit->name == kitName) return;
            auto kit = std::make_shared<engine::DrumKit> (*kitNamed (kitName));
            for (size_t i = 0; i < kit->pads.size(); ++i) kit->pads[i].gain = t->drumKit->pads[i].gain;   // the Smart Controls levels stay
            session.execute (std::make_unique<model::ReplaceDrumKitCommand> (track, std::move (kit), "Change Kit"));
            statusMessage = "Kit: " + kitName; updateStatus();
        };

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
        addChildComponent (tour);
        tour.onFinished = [this] { statusMessage = "Tour finished. Ctrl+Shift+P opens the command palette whenever you need something."; updateStatus(); grabKeyboardFocus(); };
        registerCommands();
        prefsListener = prefs.addListener ([this] (const juce::String& id) { applyPreferences (id); });
        applyPreferences ({});
        trackArea.onOpenBeatDetective = [this] { showBeatDetective(); };
        trackArea.onMuteChanged = [this] (int i, bool on) { session.execute (model::GroupLogic::flagCommand (session, i, model::SetTrackFlagCommand::Flag::mute, on)); };
        trackArea.onSoloChanged = [this] (int i, bool on)
        {
            if (on && ! prefs.getBool ("mixing.soloLatch"))
                for (int k = 0; k < session.getNumTracks(); ++k)
                    if (k != i && session.getTracks()[(size_t) k].solo) session.execute (std::make_unique<model::SetTrackFlagCommand> (k, model::SetTrackFlagCommand::Flag::solo, false));
            session.execute (model::GroupLogic::flagCommand (session, i, model::SetTrackFlagCommand::Flag::solo, on));
        };
        trackArea.onArmChanged  = [this] (int i, bool on)
        {
            // TrackPunch while rolling: the R button punches the track in/out instead of disarming it.
            auto* t = session.getTrack (i);
            if (engine.getRecorder().isRecording() && session.getRecordSettings().mode == model::RecordMode::trackPunch && t != nullptr && t->armed)
            {
                setTrackPunched (i, ! t->punched);
                return;
            }
            if (on && ! prefs.getBool ("operation.latchRecordEnable"))
                for (int k = 0; k < session.getNumTracks(); ++k)
                    if (k != i && session.getTracks()[(size_t) k].armed) session.execute (std::make_unique<model::SetTrackFlagCommand> (k, model::SetTrackFlagCommand::Flag::arm, false));
            session.execute (model::GroupLogic::flagCommand (session, i, model::SetTrackFlagCommand::Flag::arm, on));
        };
        transportBar.onRecordModeClicked = [this] (juce::TextButton& b) { showRecordModeMenu (b); };
        transportBar.onRollClicked = [this] (juce::TextButton& b) { showRollMenu (b); };
        transportBar.onCpuClicked = [this] { showSystemUsage(); };
        transportBar.onTempoEdited = [this] (double bpm, int bpb) { setTempo (bpm, bpb); };
        transportBar.onTempoPreview = [this] (double bpm) { engine.getTransport().setBpm (bpm); };
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
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        if (engine.getRecorder().isRecording())
            finishRecording();
        session.removeListener (this);
    }

    // Command-line entry points (also handy for smoke tests and demos).
    void addDrumMachineTrackFromCommandLine (const juce::String& kitName = {}) { addDrumMachineTrack (kitName); }
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
    void showPreferencesFromCommandLine() { showPreferences ({}); }
    void showMidiEventListFromCommandLine() { showMidiEventList(); }
    void showSyncDialogFromCommandLine() { showSyncDialog(); }
    // --sync=<clock-out|mtc-out|clock-in|mtc-in>[,<device>]: virtual "Beat Maker Sync" port unless a device is named
    void setSyncFromCommandLine (const juce::String& spec)
    {
        using Mode = engine::MidiSyncController::Mode;
        const auto parts = juce::StringArray::fromTokens (spec, ",", {});
        const auto modeName = parts[0].toLowerCase();
        const auto device = parts.size() > 1 ? parts[1] : juce::String (engine::MidiSyncController::virtualPortName);
        Mode mode = Mode::off;
        if (modeName == "clock-out") mode = Mode::sendClock; else if (modeName == "mtc-out") mode = Mode::sendMtc;
        else if (modeName == "clock-in") mode = Mode::chaseClock; else if (modeName == "mtc-in") mode = Mode::chaseMtc;
        if (mode == Mode::sendClock || mode == Mode::sendMtc) midiSync.setOutputDevice (device); else if (mode != Mode::off) midiSync.setInputDevice (device);
        midiSync.setMode (mode);
        statusMessage = midiSync.getStatus(); updateStatus();
    }
    void showPaletteFromCommandLine (const juce::String& query) { showCommandPalette (query); }
    void setFocusModeFromCommandLine (bool on) { editSettings.commandsFocus = on; editSettings.notify(); }
    void runCommandFromCommandLine (const juce::String& id) { if (! commands.run (id)) { statusMessage = "Unknown command: " + id; updateStatus(); } }
    void setPreferenceFromCommandLine (const juce::String& id, const juce::String& value)
    {
        const auto* d = prefs.def (id);
        if (d == nullptr) { statusMessage = "Unknown preference: " + id; updateStatus(); return; }
        switch (d->type)
        {
            case ui::PrefDef::Type::toggle: prefs.set (id, value == "1" || value.equalsIgnoreCase ("true") || value.equalsIgnoreCase ("on")); break;
            case ui::PrefDef::Type::number: prefs.set (id, value.getDoubleValue()); break;
            case ui::PrefDef::Type::choice: prefs.set (id, value.getIntValue()); break;
            case ui::PrefDef::Type::text:
            case ui::PrefDef::Type::folder:
            default:                        prefs.set (id, value); break;
        }
    }
    bool openSessionFromCommandLine (const juce::File& f) { return openSessionBundle (f); }
    void setSendFromCommandLine (const juce::String& spec)   // --send=<track>,<slot>,<bus>[,<dB>[,pre]]
    {
        const auto p = juce::StringArray::fromTokens (spec, ",", {});
        if (p.size() < 3) return;
        model::Send send; send.bus = p[2].getIntValue() - 1;
        if (p.size() > 3) send.gain = p[3].getFloatValue() <= -99.0f ? 0.0f : juce::Decibels::decibelsToGain (p[3].getFloatValue());
        send.preFader = p.size() > 4 && p[4].trim() == "pre";
        session.execute (std::make_unique<model::SetSendCommand> (p[0].getIntValue() - 1, juce::jlimit (0, model::Track::numSendSlots - 1, p[1].getIntValue() - 1), send));
    }
    void setPanFromCommandLine (const juce::String& spec)   // --pan=<track>,<-100..100>: negative is left
    {
        const int track = spec.upToFirstOccurrenceOf (",", false, false).getIntValue() - 1;
        if (auto* t = session.getTrack (track))
            session.execute (std::make_unique<model::SetTrackMixCommand> (track, t->gain, juce::jlimit (-1.0f, 1.0f, spec.fromFirstOccurrenceOf (",", false, false).getFloatValue() / 100.0f)));
    }
    void importToTrackFromCommandLine (const juce::String& spec)   // --import=<track>,<file>: onto an existing track at the playhead
    {
        const int track = spec.upToFirstOccurrenceOf (",", false, false).getIntValue() - 1;
        importAudioFile (juce::File::getCurrentWorkingDirectory().getChildFile (spec.fromFirstOccurrenceOf (",", false, false)), track, engine.getTransport().getPositionSeconds());
    }
    void addTrackEffectFromCommandLine (const juce::String& spec)   // --add-effect=<track>,<effect name>
    {
        const int track = spec.upToFirstOccurrenceOf (",", false, false).getIntValue() - 1;
        const auto name = spec.fromFirstOccurrenceOf (",", false, false).removeCharacters (" -_");
        for (auto type : engine::Effect::availableTypes())
            if (juce::String (engine::Effect::typeName (type)).removeCharacters (" -_/").equalsIgnoreCase (name))
                { trackEffect (track, "add", -1, type, trackArea.getScreenBounds()); return; }
        statusMessage = "Unknown effect: " + name; updateStatus();
    }
    void setTrackColourFromCommandLine (const juce::String& spec)   // --track-colour=<track>,<#rrggbb>
    {
        setTrackColour (spec.upToFirstOccurrenceOf (",", false, false).getIntValue() - 1, juce::Colour::fromString (spec.fromFirstOccurrenceOf (",", false, false).replace ("#", "ff")));
    }
    void showInstrumentChooserFromCommandLine() { showInstrumentChooser(); }   // --instrument-chooser
    void showDrumKitChooserFromCommandLine() { showDrumKitChooser(); }         // --kit-chooser
    void showKitBuilderFromCommandLine (const juce::String& startFrom) { showKitBuilder (startFrom); }   // --kit-builder[=<kit>]
    void showPresetBuilderFromCommandLine (const juce::String& spec)   // --preset-builder[=<instrument>[,<preset>]]
    {
        showPresetBuilder (engine::Instrument::typeNamed (spec.upToFirstOccurrenceOf (",", false, false)), spec.fromFirstOccurrenceOf (",", false, false), nullptr);
    }
    bool renameSessionFromCommandLine (const juce::String& name) { return renameSession (name); }   // --rename=<name>
    bool deleteSessionFromCommandLine (const juce::String& path)   // --delete-session=<bundle>: no confirmation
    {
        return deleteSessionBundle (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    }
    // --open-dialog[=<bundle>]: shows the Open Session browser; with a bundle, selects it and after a moment
    // takes the double-click/Return path on it (smoke tests)
    void showOpenDialogFromCommandLine (const juce::File& bundle)
    {
        auto* browser = openSessionChooser();
        if (bundle == juce::File() || browser == nullptr) return;
        browser->showBundle (bundle);
        juce::Component::SafePointer<ui::SessionBrowser> safe (browser);
        juce::Timer::callAfterDelay (800, [safe, bundle] { if (auto* b = safe.getComponent()) b->fileDoubleClicked (bundle); });
    }
    void runScriptFromCommandLine (const juce::File& f) { runScriptFile (f); }
    void runLuaFromCommandLine (const juce::String& code) { const auto r = lua.run (code, "command-line"); statusMessage = r.ok ? (r.output.isNotEmpty() ? r.output.trimEnd() : "Lua ok") : "Lua error: " + r.error; updateStatus(); std::cout << (r.ok ? r.output : "error: " + r.error + "\n"); }
    void showScriptConsoleFromCommandLine() { showScriptConsole(); }
    void freezeFromCommandLine (int oneBased) { freezeTrack (oneBased - 1); }
    void showImportDialogFromCommandLine (const juce::File& bundle) { showImportSessionDialog (bundle); }
    void showWelcomeFromCommandLine() { showWelcome(); }
    void startTourFromCommandLine() { startTour(); }
    void showTutorialsFromCommandLine() { showTutorials(); }
    void showShortcutsFromCommandLine() { showShortcuts(); }
    void showSystemUsageFromCommandLine() { showSystemUsage(); }
    void printUiProfile() { std::cout << ui::UiProfiler::get().summary() << std::endl; }
    // After the window regains activation: focus goes back to the component that had it (note or drum editor, tracks).
    void restoreFocus()
    {
        if (lastFocused != nullptr && lastFocused->isShowing()) lastFocused->grabKeyboardFocus();
        else grabKeyboardFocus();
    }
    void showPendingCrashReportFromCommandLine() { showPendingCrashReport(); }
    void dumpDocsFromCommandLine (const juce::File& dir) { dumpDocs (dir); }
    void setTempoFromCommandLine (double bpm) { setTempo (bpm, session.getBeatsPerBar()); }
    void focusNotesFromCommandLine() { focusNotes (true); }
    void scrollTracksFromCommandLine (int pixels) { trackArea.setScrollY (pixels); }
    // --click-button=<text>: presses the first button with that text (smoke tests; goes through the same click path as the mouse).
    bool clickButtonFromCommandLine (const juce::String& text)
    {
        std::function<juce::Button* (juce::Component&)> find = [&] (juce::Component& c) -> juce::Button*
        {
            if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->getButtonText() == text && b->isShowing()) return b;
            for (auto* child : c.getChildren()) if (auto* found = find (*child)) return found;
            return nullptr;
        };
        if (auto* b = find (*this)) { b->triggerClick(); return true; }
        std::cout << "No button named " << text << std::endl;
        return false;
    }
    void reportProblemFromCommandLine() { reportProblem (false); }
    void exportAafFromCommandLine (const juce::String& spec) { exportAafFromCommandLineImpl (spec); }
    void showAafExportFromCommandLine() { showAafExport(); }
    void openSampleFromCommandLine (const juce::String& name) { openSampleProject (name); }
    bool wantsWelcomeAtStartup() const { return prefs.getBool ("display.showWelcome"); }
    // --import-session=<bundle>[,<seconds>]: every track, markers and tempo; placed at the offset (default absolute).
    void importSessionFromCommandLine (const juce::String& spec)
    {
        const auto parts = juce::StringArray::fromTokens (spec, ",", {});
        const auto bundle = juce::File::getCurrentWorkingDirectory().getChildFile (parts[0]);
        importSource = std::make_unique<model::Session>();
        persistence::TransportState ts; juce::StringArray warnings;
        if (const auto error = persistence::SessionImport::open (*importSource, ts, bundle, loadContext(), warnings); error.isNotEmpty())
        { statusMessage = "Import failed: " + error; updateStatus(); importSource.reset(); return; }
        persistence::ImportOptions o;
        for (int i = 0; i < importSource->getNumTracks(); ++i) o.tracks.push_back (i);
        o.offsetSeconds = parts.size() > 1 ? parts[1].getDoubleValue() : 0.0;
        o.importMarkers = true; o.importTempo = true;
        applyImport (o, ts, warnings);
        importSource.reset();
    }
    void unfreezeFromCommandLine (int oneBased) { unfreezeTrack (oneBased - 1); }
    void commitFromCommandLine (int oneBased) { commitTrack (oneBased - 1); }
    // --stems=<dir>: every renderable track, WAV 24-bit, whole arrangement; synchronous for smoke tests.
    void stemsFromCommandLine (const juce::File& dir)
    {
        ui::StemExportDialog::Request r;
        r.tracks = model::Freeze::renderableTracks (session);
        r.settings.format = engine::BounceSettings::Format::wav; r.settings.bitDepth = 24; r.settings.tailSeconds = 1.0;
        dir.createDirectory();
        auto settings = defaultBounceSettings();
        settings.startSample = 0; settings.endSample = juce::jmax<juce::int64> (1, (juce::int64) std::llround (session.getLengthSeconds() * settings.sampleRate));
        settings.tailSeconds = 1.0; settings.trimTail = false;
        int written = 0;
        for (int i : r.tracks)
        {
            auto snap = model::Freeze::stemSnapshot (session, i, false, false);
            snap->panDepthDb = panDepthDb();
            const auto file = dir.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (session.getTracks()[(size_t) i].name) + ".wav");
            const auto res = engine::Bouncer::renderToFile (std::move (snap), settings, file);
            if (res.ok()) { ++written; std::cout << "Stem " << file.getFileName() << " " << res.numSamples << " samples" << std::endl; }
            else std::cout << "Stem failed: " << res.error << std::endl;
        }
        statusMessage = "Exported " + juce::String (written) + " stems to " + dir.getFullPathName(); updateStatus();
    }
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
    void addInstrumentTrackFromCommandLine (const juce::String& spec)   // <name>[,<preset>]
    {
        const auto name = spec.upToFirstOccurrenceOf (",", false, false), preset = spec.fromFirstOccurrenceOf (",", false, false);
        for (auto type : engine::Instrument::availableTypes())
            if (juce::String (engine::Instrument::typeName (type)).removeCharacters (" ").equalsIgnoreCase (name.removeCharacters (" -_")))
            { addInstrumentTrack (type, preset); return; }
        statusMessage = "Unknown instrument: " + name;
        updateStatus();
    }
    // Loads a file into the most recently added Sampler track (creating one if needed).
    void loadSampleFromCommandLine (const juce::File& file)
    {
        int index = -1;
        for (int i = session.getNumTracks(); --i >= 0;)
            if (engine::Instrument::usesSample (session.getTracks()[(size_t) i].instrumentType())) { index = i; break; }
        if (index < 0) { addInstrumentTrack (engine::InstrumentType::sampler); index = trackArea.getSelectedTrack(); }
        loadSamplerSample (index, file);
    }
    void startPlayback() { startTransport (false); }

    // Play or record with the count-in Preferences > Metronome asks for: the transport clicks through the bars
    // before its position moves (Transport::playWithCountIn), and the recorder waits with it.
    void startTransport (bool forRecording)
    {
        auto& transport = engine.getTransport();
        static const int barsFor[] = { 0, 1, 2, 4 };
        const int bars = barsFor[juce::jlimit (0, 3, prefs.getInt ("metronome.countIn"))];
        const bool wanted = bars > 0 && (forRecording || prefs.getInt ("metronome.countInFor") == 1);
        if (! wanted || transport.isPlaying()) { if (! transport.isPlaying()) transport.play(); return; }
        const auto samples = (juce::int64) std::llround (bars * transport.getBeatsPerBar() * 60.0 / transport.getBpm() * transport.getSampleRate());
        transport.playWithCountIn (samples);
        statusMessage = "Count-in: " + juce::String (bars) + (bars == 1 ? " bar" : " bars"); updateStatus();
    }
    void togglePlayback() { auto& t = engine.getTransport(); if (t.isPlaying()) t.stop(); else startTransport (false); }
    void addArmedAudioTrackAndRecord()
    {
        const int index = addTrack ("Audio " + juce::String (countTracks (model::Track::Type::audio) + 1));
        session.execute (std::make_unique<model::SetTrackFlagCommand> (index, model::SetTrackFlagCommand::Flag::arm, true));
        startRecording();
    }
    void setCycleEnabled (bool on) { engine.getTransport().setLoopEnabled (on); }
    void setMetronomeFromCommandLine (bool on) { setMetronome (on); }   // --metronome

    // The metronome: on or off is remembered across sessions (an app setting, like the loop folders)
    void setMetronome (bool on)
    {
        metronomeOn = on;
        engine.getGraph().getMetronome().setEnabled (on);
        transportBar.setMetronome (on);
        if (appSettings != nullptr) { appSettings->setValue ("metronome", on); appSettings->saveIfNeeded(); }   // the file is only written on request
        statusMessage = on ? "Metronome on: a click on every beat (K)" : "Metronome off"; updateStatus();
    }
    void showBounceDialog();
    void setMixerVisibleFromCommandLine (bool v) { setMixerVisible (v); }
    void showLibraryWindowFromCommandLine() { showLibraryWindow(); }   // --library
    void showIOSetupDialogFromCommandLine() { showIOSetupDialog(); }
    void openSendsPanelFromCommandLine (int track)   // --sends-panel=<track>: the Mix window with that strip's sends panel open
    {
        setMixerVisible (true);
        juce::Timer::callAfterDelay (300, [this, track] { mixerView.openSendsPanel (track); });
    }
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
        setTempo (100.0, 4, false);
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

        auto snapshot = model::buildRenderSnapshot (session);
        snapshot->panDepthDb = panDepthDb();
        const auto result = engine::Bouncer::renderToFile (std::move (snapshot), s, file);
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
        tour.setBounds (getLocalBounds());
        tour.toFront (false);
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
            const int wanted = juce::roundToInt (prefs.getDouble ("display.editorHeight"));
            auto editor = area.removeFromBottom (juce::jlimit (120, juce::jmax (120, area.getHeight() - 160), wanted));
            sequencer.setBounds (editor);
            pianoRoll.setBounds (editor);
            editorResizer.setBounds (area.removeFromBottom (6));
            editorResizer.setVisible (true);
        }
        else editorResizer.setVisible (false);
        if (controlsVisible)
        {
            smartControls.setBounds (area.removeFromTop (ui::SmartControls::preferredHeight));   // above the tracks
            area.removeFromTop (2);
        }
        trackArea.setBounds (area);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        ui::UiProfiler::Scope scope ("keyPressed");
        if (commands.handleKey (key, editSettings.commandsFocus)) return true;
        // Clip edits reach the track area even when it doesn't have focus
        if (trackArea.keyPressed (key)) return true;
        return false;
    }

    // Every shortcut lives in the registry so the command palette can list and run it.
    void registerCommands()
    {
        using M = juce::ModifierKeys;
        auto add = [this] (const char* id, const char* category, const char* name, juce::KeyPress key, juce::juce_wchar focusKey, std::function<void()> run, std::function<bool()> enabled = nullptr)
        {
            commands.add ({ id, name, category, key, focusKey, std::move (run), std::move (enabled) });
        };
        auto& transport = engine.getTransport();
        auto notifyEdit = [this] { editSettings.notify(); };
        using Mode = ui::EditSettings::Mode; using Tool = ui::EditSettings::Tool;

        // Transport
        add ("transport.setTempo", "Transport", "Set Tempo...", juce::KeyPress ('t', M::commandModifier | M::shiftModifier, 0), 0, [this] { showTempoDialog(); });
        add ("transport.tapTempo", "Transport", "Tap Tempo", juce::KeyPress ('t', M::commandModifier | M::altModifier, 0), 0, [this] { tapTempo(); });
        add ("transport.playStop", "Transport", "Play / Stop", juce::KeyPress (juce::KeyPress::spaceKey), 0, [this] { togglePlayback(); });
        add ("transport.returnToStart", "Transport", "Return to Start", juce::KeyPress (juce::KeyPress::returnKey), 0, [&transport] { transport.returnToStart(); });
        add ("transport.returnToStartHome", "Transport", "Return to Start (Home)", juce::KeyPress (juce::KeyPress::homeKey), 0, [&transport] { transport.returnToStart(); });
        add ("transport.record", "Transport", "Record / Punch", juce::KeyPress ('r'), 0, [this] { toggleRecord(); });
        add ("transport.recordCtrl", "Transport", "Record / Punch (Ctrl+Space, works in focus mode)", juce::KeyPress (juce::KeyPress::spaceKey, M::commandModifier, 0), 0, [this] { toggleRecord(); });
        add ("transport.cycle", "Transport", "Cycle on/off", juce::KeyPress ('c'), 0, [&transport] { transport.setLoopEnabled (! transport.isLoopEnabled()); });
        add ("transport.cycleCtrl", "Transport", "Cycle on/off (Ctrl+Shift+C)", juce::KeyPress ('c', M::commandModifier | M::shiftModifier, 0), 0, [&transport] { transport.setLoopEnabled (! transport.isLoopEnabled()); });
        add ("transport.stopPreview", "Transport", "Stop loop preview", juce::KeyPress (juce::KeyPress::escapeKey), 0, [this] { loopBrowser.stopPreview(); });

        // File
        add ("file.new", "File", "New Session...", juce::KeyPress ('n', M::commandModifier, 0), 0, [this] { showNewSessionDialog(); });
        add ("file.open", "File", "Open Session or Audio...", juce::KeyPress ('o', M::commandModifier, 0), 0, [this] { openSessionChooser(); });
        add ("file.save", "File", "Save", juce::KeyPress ('s', M::commandModifier, 0), 0, [this] { saveSession (false); });
        add ("file.saveAs", "File", "Save As...", juce::KeyPress ('s', M::commandModifier | M::shiftModifier, 0), 0, [this] { saveSession (true); });
        add ("file.rename", "File", "Rename Session...", {}, 0, [this] { showRenameSession(); });
        add ("file.delete", "File", "Delete Session...", {}, 0, [this] { showDeleteSession(); });
        add ("file.saveTemplate", "File", "Save As Template...", {}, 0, [this] { saveAsTemplate(); });
        add ("file.import", "File", "Import Audio Files...", {}, 0, [this] { openFileChooser(); });
        add ("file.bounce", "File", "Bounce to Disk...", juce::KeyPress ('b', M::commandModifier, 0), 0, [this] { showBounceDialogImpl(); });

        // Edit
        add ("edit.undo", "Edit", "Undo", juce::KeyPress ('z', M::commandModifier, 0), 0, [this] { session.undo(); }, [this] { return session.getHistory().canUndo(); });
        add ("edit.redo", "Edit", "Redo", juce::KeyPress ('z', M::commandModifier | M::shiftModifier, 0), 0, [this] { session.redo(); }, [this] { return session.getHistory().canRedo(); });
        add ("edit.redoY", "Edit", "Redo (Ctrl+Y)", juce::KeyPress ('y', M::commandModifier, 0), 0, [this] { session.redo(); }, [this] { return session.getHistory().canRedo(); });
        add ("edit.join", "Edit", "Join Selected Clips", juce::KeyPress ('j', M::commandModifier, 0), 0, [this] { trackArea.joinSelectedClips(); }, [this] { return trackArea.hasSelection(); });
        add ("edit.addClip", "Edit", "Add Clip for Selection (instrument tracks)", juce::KeyPress ('m', M::commandModifier | M::altModifier, 0), 0, [this] { trackArea.addClipForSelection(); });
        add ("edit.notesFocus", "Edit", "Keyboard Focus: Editor (notes or drums) / Tracks", juce::KeyPress ('n', M::commandModifier | M::altModifier, 0), 0, [this] { focusNotes (! notesFocused()); });
        add ("edit.selectAll", "Edit", "Select All (clips, or notes when the note editor has focus)", juce::KeyPress ('a', M::commandModifier, 0), 0, [this] { if (auto* ed = focusedEditor()) ed->selectAll(); else trackArea.selectAllClips(); });
        add ("edit.cut", "Edit", "Cut", juce::KeyPress ('x', M::commandModifier, 0), 'x', [this] { if (auto* ed = focusedEditor()) ed->cutSelection(); else trackArea.cutSelection(); }, [this] { return focusedEditor() != nullptr ? focusedEditor()->hasSelection() : trackArea.hasSelection(); });
        add ("edit.copy", "Edit", "Copy", juce::KeyPress ('c', M::commandModifier, 0), 'c', [this] { if (auto* ed = focusedEditor()) ed->copySelection(); else trackArea.copySelection(); }, [this] { return focusedEditor() != nullptr ? focusedEditor()->hasSelection() : trackArea.hasSelection(); });
        add ("edit.paste", "Edit", "Paste at Insertion", juce::KeyPress ('v', M::commandModifier, 0), 'v', [this] { if (auto* ed = focusedEditor()) ed->pasteAtInsertion(); else trackArea.pasteAtPlayhead (prefs.getBool ("editing.autoSelectAfterPaste")); }, [this] { return focusedEditor() != nullptr ? focusedEditor()->canPaste() : trackArea.canPaste(); });
        add ("edit.delete", "Edit", "Delete Selection", {}, 0, [this] { if (auto* ed = focusedEditor()) ed->deleteSelection(); else trackArea.deleteSelection(); });
        add ("edit.separate", "Edit", "Separate Clip at Insertion", juce::KeyPress ('e', M::commandModifier, 0), 'b', [this] { trackArea.separateAtPlayhead(); });
        add ("edit.duplicate", "Edit", "Duplicate Clips", juce::KeyPress ('d', M::commandModifier, 0), 'h', [this] { if (auto* ed = focusedEditor()) ed->duplicateSelection(); else trackArea.duplicateSelectedClips(); }, [this] { return focusedEditor() != nullptr ? focusedEditor()->hasSelection() : trackArea.hasSelection(); });
        add ("edit.trimStart", "Edit", "Trim Start to Insertion", {}, 'a', [this] { trackArea.trimSelectionToPlayhead (true); }, [this] { return trackArea.hasSelection(); });
        add ("edit.trimEnd", "Edit", "Trim End to Insertion", {}, 's', [this] { trackArea.trimSelectionToPlayhead (false); }, [this] { return trackArea.hasSelection(); });
        add ("edit.fadeIn", "Edit", "Fade In to Insertion", {}, 'd', [this] { trackArea.fadeSelectionToPlayhead (true, defaultFadeShape()); }, [this] { return trackArea.hasSelection(); });
        add ("edit.fadeOut", "Edit", "Fade Out from Insertion", {}, 'g', [this] { trackArea.fadeSelectionToPlayhead (false, defaultFadeShape()); }, [this] { return trackArea.hasSelection(); });
        add ("edit.fades", "Edit", "Apply Default Fades", {}, 'f', [this] { trackArea.applyDefaultFadesToSelection (prefs.getDouble ("editing.defaultFadeMs"), defaultFadeShape()); }, [this] { return trackArea.hasSelection(); });
        add ("edit.fadesDialog", "Edit", "Fades...", juce::KeyPress ('f', M::commandModifier, 0), 0, [this] { showFadesDialog(); });
        add ("edit.nudgeLeft", "Edit", "Nudge Earlier", {}, 0, [this] { if (auto* ed = focusedEditor()) ed->nudgeSelection (-1); else trackArea.nudgeSelectedClips (-1); });
        add ("edit.nudgeRight", "Edit", "Nudge Later", {}, 0, [this] { if (auto* ed = focusedEditor()) ed->nudgeSelection (1); else trackArea.nudgeSelectedClips (1); });
        add ("notes.quantize", "Notes", "Quantize Notes to Grid", {}, 0, [this] { focusNotes (true); if (auto* ed = activeEditor()) ed->quantizeSelection (false); }, [this] { return pianoRoll.isVisible() && pianoRoll.hasTarget(); });
        add ("notes.quantizeLengths", "Notes", "Quantize Notes and Lengths", {}, 0, [this] { focusNotes (true); if (auto* ed = activeEditor()) ed->quantizeSelection (true); }, [this] { return pianoRoll.isVisible() && pianoRoll.hasTarget(); });
        add ("notes.legato", "Notes", "Legato (extend notes to the next)", {}, 0, [this] { focusNotes (true); if (auto* ed = activeEditor()) ed->legatoSelection(); }, [this] { return pianoRoll.isVisible() && pianoRoll.hasTarget(); });
        add ("notes.transposeUp", "Notes", "Transpose Up a Semitone", {}, 0, [this] { if (auto* ed = activeEditor()) ed->transposeSelection (1); }, [this] { return activeEditor() != nullptr && activeEditor()->hasSelection(); });
        add ("notes.transposeDown", "Notes", "Transpose Down a Semitone", {}, 0, [this] { if (auto* ed = activeEditor()) ed->transposeSelection (-1); }, [this] { return activeEditor() != nullptr && activeEditor()->hasSelection(); });
        add ("notes.octaveUp", "Notes", "Transpose Up an Octave", {}, 0, [this] { if (auto* ed = activeEditor()) ed->transposeSelection (12); }, [this] { return pianoRoll.isVisible() && pianoRoll.hasSelection(); });
        add ("notes.octaveDown", "Notes", "Transpose Down an Octave", {}, 0, [this] { if (auto* ed = activeEditor()) ed->transposeSelection (-12); }, [this] { return pianoRoll.isVisible() && pianoRoll.hasSelection(); });
        add ("notes.louder", "Notes", "Velocity +10", {}, 0, [this] { if (auto* ed = activeEditor()) ed->changeVelocity (10); }, [this] { return activeEditor() != nullptr && activeEditor()->hasSelection(); });
        add ("notes.softer", "Notes", "Velocity -10", {}, 0, [this] { if (auto* ed = activeEditor()) ed->changeVelocity (-10); }, [this] { return activeEditor() != nullptr && activeEditor()->hasSelection(); });
        add ("notes.unroll", "Notes", "Unroll Loop (write every repeat out; notes or drum steps)", {}, 0, [this] { focusNotes (true); if (auto* ed = activeEditor()) ed->unrollLoop(); }, [this] { return activeEditor() != nullptr && activeEditor()->hasTarget(); });
        add ("notes.linkView", "Notes", "Editor Follows the Tracks' View on/off", {}, 0, [this] { if (auto* ed = activeEditor()) ed->setLinked (! ed->isLinked()); }, [this] { return activeEditor() != nullptr && activeEditor()->hasTarget(); });
        add ("notes.describe", "Notes", "Announce Note Selection", {}, 0, [this] { statusMessage = activeEditor() != nullptr ? activeEditor()->describeSelection() : juce::String ("No editor"); updateStatus(); juce::AccessibilityHandler::postAnnouncement (statusMessage, juce::AccessibilityHandler::AnnouncementPriority::high); });
        add ("edit.quantize", "Edit", "Quantize Audio to Grid", {}, 0, [this] { trackArea.quantizeSelectionPublic(); });
        add ("edit.addMarker", "Edit", "Add Memory Location at Insertion", {}, 0, [this] { trackArea.addMarkerAtPlayhead (false); });
        add ("edit.addSection", "Edit", "Add Section from Selection", {}, 0, [this] { trackArea.addMarkerAtPlayhead (true); });

        // Edit modes and tools
        add ("mode.shuffle", "Edit Mode", "Shuffle", juce::KeyPress (juce::KeyPress::F1Key), 0, [this, notifyEdit] { editSettings.mode = Mode::shuffle; notifyEdit(); });
        add ("mode.slip", "Edit Mode", "Slip", juce::KeyPress (juce::KeyPress::F2Key), 0, [this, notifyEdit] { editSettings.mode = Mode::slip; notifyEdit(); });
        add ("mode.spot", "Edit Mode", "Spot", juce::KeyPress (juce::KeyPress::F3Key), 0, [this, notifyEdit] { editSettings.mode = Mode::spot; notifyEdit(); });
        add ("mode.grid", "Edit Mode", "Grid", juce::KeyPress (juce::KeyPress::F4Key), 0, [this, notifyEdit] { editSettings.mode = Mode::grid; notifyEdit(); });
        add ("tool.zoomer", "Tool", "Zoomer", juce::KeyPress (juce::KeyPress::F5Key), 0, [this, notifyEdit] { editSettings.tool = Tool::zoomer; notifyEdit(); });
        add ("tool.trimmer", "Tool", "Trimmer", juce::KeyPress (juce::KeyPress::F6Key), 0, [this, notifyEdit] { editSettings.tool = Tool::trimmer; notifyEdit(); });
        add ("tool.selector", "Tool", "Selector", juce::KeyPress (juce::KeyPress::F7Key), 0, [this, notifyEdit] { editSettings.tool = Tool::selector; notifyEdit(); });
        add ("tool.grabber", "Tool", "Grabber", juce::KeyPress (juce::KeyPress::F8Key), 0, [this, notifyEdit] { editSettings.tool = Tool::grabber; notifyEdit(); });
        add ("tool.scrubber", "Tool", "Scrubber", juce::KeyPress (juce::KeyPress::F9Key), 0, [this, notifyEdit] { editSettings.tool = Tool::scrubber; notifyEdit(); });
        add ("tool.pencil", "Tool", "Pencil", juce::KeyPress (juce::KeyPress::F10Key), 0, [this, notifyEdit] { editSettings.tool = Tool::pencil; notifyEdit(); });
        add ("tool.smart", "Tool", "Smart Tool", juce::KeyPress (juce::KeyPress::F11Key), 0, [this, notifyEdit] { editSettings.tool = Tool::smart; notifyEdit(); });
        add ("tool.tce", "Tool", "TCE Trimmer on/off", {}, 0, [this, notifyEdit] { editSettings.tceTrim = ! editSettings.tceTrim; notifyEdit(); });
        add ("tool.focus", "Tool", "Commands Keyboard Focus on/off", juce::KeyPress ('k', M::commandModifier | M::altModifier, 0), 0, [this, notifyEdit] { editSettings.commandsFocus = ! editSettings.commandsFocus; notifyEdit(); statusMessage = editSettings.commandsFocus ? "Commands Keyboard Focus ON: A/S trim, D/G fades, F fades, B separate, H duplicate, X/C/V clipboard, R/T zoom, E zoom to fit" : "Commands Keyboard Focus off"; updateStatus(); });

        // View
        add ("transport.metronome", "Transport", "Metronome (click)", juce::KeyPress ('k'), 0, [this] { setMetronome (! metronomeOn); });
        add ("view.zoomIn", "View", "Zoom In", juce::KeyPress ('t', M::commandModifier, 0), 't', [this] { if (auto* ed = focusedEditor()) ed->zoomBy (prefs.getDouble ("display.zoomSensitivity")); else trackArea.zoomBy (prefs.getDouble ("display.zoomSensitivity")); });
        add ("view.zoomOut", "View", "Zoom Out", juce::KeyPress ('r', M::commandModifier, 0), 'r', [this] { if (auto* ed = focusedEditor()) ed->zoomBy (1.0 / prefs.getDouble ("display.zoomSensitivity")); else trackArea.zoomBy (1.0 / prefs.getDouble ("display.zoomSensitivity")); });
        add ("view.zoomToFit", "View", "Zoom to Fit / Selection", juce::KeyPress ('z', M::altModifier, 0), 'e', [this] { if (auto* ed = focusedEditor()) ed->zoomToFit(); else trackArea.zoomToSelection(); });
        add ("view.editor", "View", "Editor panel", juce::KeyPress ('e'), 0, [this] { setEditorVisible (! editorVisible); });
        add ("track.addFromLibrary", "Track", "Add from Sample Library...", juce::KeyPress ('l'), 0, [this] { showLibraryWindow (true); });
        add ("view.controls", "View", "Smart Controls", juce::KeyPress ('b'), 0, [this] { setControlsVisible (! controlsVisible); });
        add ("view.mixer", "View", "Mix window", juce::KeyPress ('x'), 0, [this] { setMixerVisible (! mixerVisible); });
        add ("view.editorCtrl", "View", "Editor panel (Ctrl+Shift+E)", juce::KeyPress ('e', M::commandModifier | M::shiftModifier, 0), 0, [this] { setEditorVisible (! editorVisible); });
        add ("track.addFromLibraryCtrl", "Track", "Add from Sample Library... (Ctrl+Shift+L)", juce::KeyPress ('l', M::commandModifier | M::shiftModifier, 0), 0, [this] { showLibraryWindow (true); });
        add ("view.controlsCtrl", "View", "Smart Controls (Ctrl+Shift+B)", juce::KeyPress ('b', M::commandModifier | M::shiftModifier, 0), 0, [this] { setControlsVisible (! controlsVisible); });
        add ("view.mixerCtrl", "View", "Mix window (Ctrl+Shift+X)", juce::KeyPress ('x', M::commandModifier | M::shiftModifier, 0), 0, [this] { setMixerVisible (! mixerVisible); });

        // Track
        add ("track.addAudio", "Track", "New Audio Track", juce::KeyPress ('n', M::commandModifier | M::shiftModifier, 0), 0, [this] { addTrack ("Audio " + juce::String (countTracks (model::Track::Type::audio) + 1)); });
        add ("track.addDrums", "Track", "New Drum Machine Track", juce::KeyPress ('d', M::commandModifier | M::shiftModifier, 0), 0, [this] { addDrumMachineTrack(); });
        add ("track.addSynth", "Track", "New Synth Track", juce::KeyPress ('i', M::commandModifier, 0), 0, [this] { addInstrumentTrack (engine::InstrumentType::subtractive); });
        add ("track.addDrumKit", "Track", "New Drum Machine Track (choose kit)...", {}, 0, [this] { showDrumKitChooser(); });
        add ("track.buildKit", "Track", "Build Your Own Drum Kit...", {}, 0, [this] { showKitBuilder ({}); });
        add ("track.buildPreset", "Track", "Build Your Own Preset...", {}, 0, [this]
        {
            const auto* t = session.getTrack (trackArea.getSelectedTrack());
            if (t != nullptr && t->hasInstrument()) showPresetBuilder (t->instrumentType(), t->instrumentParams->presetName, t->instrumentParams.get());
            else showPresetBuilder (engine::InstrumentType::subtractive, {}, nullptr);
        });
        add ("track.savePreset", "Track", "Save Sound as Preset...", {}, 0, [this] { savePresetFromTrack (trackArea.getSelectedTrack()); },
             [this] { const auto* t = session.getTrack (trackArea.getSelectedTrack()); return t != nullptr && t->hasInstrument(); });
        add ("track.addInstrument", "Track", "New Instrument Track (choose)...", juce::KeyPress ('i', M::commandModifier | M::shiftModifier, 0), 0, [this] { showInstrumentChooser(); });
        for (auto type : engine::Instrument::availableTypes())
            add (("track.add." + juce::String (engine::Instrument::typeName (type)).removeCharacters (" ")).toRawUTF8(), "Track", ("New " + juce::String (engine::Instrument::typeName (type)) + " Track").toRawUTF8(), {}, 0, [this, type] { addInstrumentTrack (type); });
        add ("track.addAux", "Track", "New Aux Input", {}, 0, [this] { addAuxTrack(); });
        add ("track.addVca", "Track", "New VCA Master", {}, 0, [this] { addVcaTrack(); });
        add ("track.group", "Track", "New Group...", juce::KeyPress ('g', M::commandModifier, 0), 0, [this] { showGroupDialog (-1); });
        add ("track.mute", "Track", "Mute Selected Track", juce::KeyPress ('m', M::shiftModifier, 0), 0, [this] { toggleSelectedTrackFlag (model::SetTrackFlagCommand::Flag::mute); }, [this] { return session.getTrack (trackArea.getSelectedTrack()) != nullptr; });
        add ("track.solo", "Track", "Solo Selected Track", juce::KeyPress ('s', M::shiftModifier, 0), 0, [this] { toggleSelectedTrackFlag (model::SetTrackFlagCommand::Flag::solo); }, [this] { return session.getTrack (trackArea.getSelectedTrack()) != nullptr; });
        add ("track.arm", "Track", "Record-arm Selected Track", juce::KeyPress ('r', M::shiftModifier, 0), 0, [this] { toggleSelectedTrackFlag (model::SetTrackFlagCommand::Flag::arm); }, [this] { auto* t = session.getTrack (trackArea.getSelectedTrack()); return t != nullptr && t->isAudio(); });
        add ("track.effects", "Track", "Effects on Selected Track...", juce::KeyPress ('f', M::commandModifier | M::altModifier, 0), 0, [this] { trackArea.showTrackEffectsMenu (trackArea.getSelectedTrack()); }, [this] { return session.getTrack (trackArea.getSelectedTrack()) != nullptr; });
        add ("track.fadesAll", "Track", "Fades for All Clips on Selected Track...", {}, 0, [this] { showTrackFadesDialog (trackArea.getSelectedTrack()); },
             [this] { const auto* t = session.getTrack (trackArea.getSelectedTrack()); return t != nullptr && ! t->clips.empty(); });
        add ("track.colour", "Track", "Colour Selected Track...", {}, 0, [this] { promptTrackColour (trackArea.getSelectedTrack()); }, [this] { return session.getTrack (trackArea.getSelectedTrack()) != nullptr; });
        add ("track.rename", "Track", "Rename Selected Track...", {}, 0, [this] { promptRenameTrack (trackArea.getSelectedTrack()); }, [this] { return session.getTrack (trackArea.getSelectedTrack()) != nullptr; });
        add ("track.delete", "Track", "Delete Selected Track", {}, 0, [this] { const int i = trackArea.getSelectedTrack(); if (session.getTrack (i)) { session.execute (std::make_unique<model::RemoveTrackCommand> (i)); trackArea.setSelectedTrack (juce::jmin (i, session.getNumTracks() - 1)); } }, [this] { return session.getTrack (trackArea.getSelectedTrack()) != nullptr; });
        add ("track.next", "Track", "Select Next Track", {}, 0, [this] { trackArea.selectTrayByOffsetPublic (1); });
        add ("track.previous", "Track", "Select Previous Track", {}, 0, [this] { trackArea.selectTrayByOffsetPublic (-1); });
        add ("transport.stepRight", "Transport", "Move Playhead Right by the Grid", {}, 0, [this] { trackArea.stepPlayhead (1, false); });
        add ("transport.stepLeft", "Transport", "Move Playhead Left by the Grid", {}, 0, [this] { trackArea.stepPlayhead (-1, false); });
        add ("window.systemUsage", "Window", "System Usage (CPU, memory, cache)", juce::KeyPress ('u', M::commandModifier | M::shiftModifier, 0), 0, [this] { showSystemUsage(); });
        add ("help.whereAmI", "Help", "Announce Selection", juce::KeyPress ('/', M::commandModifier | M::shiftModifier, 0), 0, [this] { const auto text = trackArea.describeSelection(); statusMessage = text; updateStatus(); juce::AccessibilityHandler::postAnnouncement (text, juce::AccessibilityHandler::AnnouncementPriority::high); });
        add ("track.freeze", "Track", "Freeze Selected Track", {}, 0, [this] { freezeTrack (trackArea.getSelectedTrack()); }, [this] { auto* t = session.getTrack (trackArea.getSelectedTrack()); return t != nullptr && model::Freeze::canFreeze (*t) && ! t->isFrozen(); });
        add ("track.unfreeze", "Track", "Unfreeze Selected Track", {}, 0, [this] { unfreezeTrack (trackArea.getSelectedTrack()); }, [this] { auto* t = session.getTrack (trackArea.getSelectedTrack()); return t != nullptr && t->isFrozen(); });
        add ("track.commit", "Track", "Commit Selected Track", {}, 0, [this] { commitTrack (trackArea.getSelectedTrack()); }, [this] { auto* t = session.getTrack (trackArea.getSelectedTrack()); return t != nullptr && model::Freeze::canFreeze (*t); });
        add ("file.stems", "File", "Export Stems...", juce::KeyPress ('b', M::commandModifier | M::altModifier, 0), 0, [this] { showStemExport(); });
        add ("file.exportAaf", "File", "Export AAF...", juce::KeyPress ('a', M::commandModifier | M::altModifier, 0), 0, [this] { showAafExport(); });
        add ("file.importSession", "File", "Import Session Data...", juce::KeyPress ('i', M::shiftModifier | M::altModifier, 0), 0, [this] { chooseSessionToImport(); });
        add ("help.welcome", "Help", "Welcome Window", {}, 0, [this] { showWelcome(); });
        add ("help.tour", "Help", "Take the Tour", {}, 0, [this] { startTour(); });
        add ("help.tutorials", "Help", "Tutorials", {}, 0, [this] { showTutorials(); });
        add ("help.shortcuts", "Help", "Keyboard Shortcuts", juce::KeyPress ('/', M::commandModifier, 0), 0, [this] { showShortcuts(); });
        add ("help.reportProblem", "Help", "Report a Problem (diagnostics file)", {}, 0, [this] { reportProblem(); });
        add ("help.userGuide", "Help", "User Guide", {}, 0, [this] { openUserGuide(); });
        for (const auto& sample : persistence::SampleProjects::list())
            add (("file.sample." + juce::File::createLegalFileName (sample.name).replaceCharacter (' ', '-')).toRawUTF8(), "File", ("Open Sample Project: " + sample.name).toRawUTF8(), {}, 0, [this, name = sample.name] { openSampleProject (name); });

        // Windows
        add ("window.preferences", "Window", "Preferences...", juce::KeyPress (',', M::commandModifier, 0), 0, [this] { showPreferences ({}); });
        add ("window.palette", "Window", "Command Palette", juce::KeyPress ('p', M::commandModifier | M::shiftModifier, 0), 0, [this] { showCommandPalette ({}); });
        add ("window.paletteK", "Window", "Command Palette (Ctrl+K)", juce::KeyPress ('k', M::commandModifier, 0), 0, [this] { showCommandPalette ({}); });
        add ("window.memoryLocations", "Window", "Memory Locations", juce::KeyPress ('5', M::commandModifier, 0), 0, [this] { showMemoryLocations(); });
        add ("window.beatDetective", "Window", "Beat Detective", juce::KeyPress ('8', M::commandModifier, 0), 0, [this] { showBeatDetective(); });
        add ("window.ioSetup", "Window", "I/O Setup", juce::KeyPress ('i', M::commandModifier | M::altModifier, 0), 0, [this] { showIOSetupDialog(); });
        add ("window.scanPlugins", "Window", "Scan for Plugins", {}, 0, [this] { scanPlugins(); });
        add ("window.midiEventList", "Window", "MIDI Event List", juce::KeyPress ('e', M::commandModifier | M::altModifier, 0), 0, [this] { showMidiEventList(); });
        add ("window.scriptConsole", "Window", "Script Console (Lua)", juce::KeyPress ('l', M::commandModifier | M::altModifier, 0), 0, [this] { showScriptConsole(); });
        add ("script.reload", "Script", "Reload Scripts Folder", {}, 0, [this] { registerScripts(); });
        add ("script.openFolder", "Script", "Open Scripts Folder", {}, 0, [this] { scriptsFolder().createDirectory(); scriptsFolder().revealToUser(); });
        registerScripts();
        add ("window.sync", "Window", "Synchronization (Session Setup)", juce::KeyPress ('2', M::commandModifier, 0), 0, [this] { showSyncDialog(); });
    }

    engine::FadeShape defaultFadeShape() const { return (engine::FadeShape) juce::jlimit (0, 2, prefs.getInt ("editing.defaultFadeShape")); }


private:
    int addTrack (const juce::String& name)
    {
        model::Track track;
        track.name   = name;
        track.colour = model::Session::colourForTrackIndex (session.getNumTracks());

        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        applyNewTrackDefaults (raw->getTrackIndex());
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

    // App settings (loop folders, sync, metronome): next to the preferences in ~/.config/Beat Maker. On Linux JUCE
    // builds the default path as ~/<folderName>, so the folder name carries the .config prefix; earlier builds wrote
    // ~/Beat Maker/Beat Maker.settings, which is moved across once.
    static juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Beat Maker";
        o.filenameSuffix = "settings";
       #if JUCE_LINUX || JUCE_BSD
        o.folderName = ".config/Beat Maker";
       #else
        o.folderName = "Beat Maker";
       #endif
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }
    static void migrateOldSettingsFile()
    {
        const auto wanted = settingsOptions().getDefaultFile();
        const auto old = juce::File ("~").getChildFile ("Beat Maker").getChildFile ("Beat Maker.settings");
        if (old.existsAsFile() && ! wanted.existsAsFile() && wanted.getParentDirectory().createDirectory())
            old.moveFileTo (wanted);
    }

    void setupLoopLibrary()
    {
        migrateOldSettingsFile();
        appSettings = std::make_unique<juce::PropertiesFile> (settingsOptions());

        juce::Array<juce::File> folders;
        if (const auto bundled = bundledLoopsFolder(); bundled.isDirectory()) folders.add (bundled);
        userLoopsFolder().createDirectory();
        folders.add (userLoopsFolder());
        setMetronome (appSettings->getBoolValue ("metronome", false));
        for (const auto& path : juce::StringArray::fromLines (appSettings->getValue ("loopFolders")))
            if (juce::File (path).isDirectory()) folders.add (juce::File (path));

        loopLibrary.setFolders (folders);
        loopLibrary.rescanAsync();
        loadSyncSettings();
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
            const engine::StretchMode modes[] = { engine::StretchMode::polyphonic, engine::StretchMode::polyphonic, engine::StretchMode::rhythmic, engine::StretchMode::monophonic, engine::StretchMode::varispeed };
            const int conformPref = prefs.getInt ("processing.loopConformMode");
            const auto mode = conformPref == 0 ? (info.category == persistence::LoopInfo::Category::drums ? engine::StretchMode::rhythmic : engine::StretchMode::polyphonic)
                                               : modes[juce::jlimit (0, 4, conformPref)];
            const auto& placed = session.getTracks()[(size_t) trackIndex].clips[(size_t) ref.index];
            auto stretch = std::make_unique<model::SetClipElasticCommand> (session, ref, model::Elastic::forTempo (placed, sessionBpm, mode), "Conform to Tempo", true);
            if (! stretch->wasCancelled())
            {
                conformNote = "  (" + juce::String (juce::roundToInt (info.bpm)) + " -> " + juce::String (juce::roundToInt (sessionBpm))
                            + " BPM, " + engine::TimeStretch::modeName (mode) + ")";
                ui::ElasticJob::run (session, std::move (stretch));   // background with a progress window when it would take a moment
            }
        }

        statusMessage = "Added " + info.name + " at bar " + juce::String (engine.getTransport().barBeatForSeconds (start).bar) + conformNote;
        updateStatus();
    }

    void setMixerVisible (bool visible)
    {
        mixerVisible = visible;
        if (visible && editorVisible) { editorVisible = false; transportBar.setEditorVisible (false); }   // one or the other
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
        applyNewTrackDefaults (raw->getTrackIndex());
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

    void addAuxTrack (int inputBus = 0)
    {
        model::Track track;
        track.name   = (inputBus > 0 ? session.busName (inputBus) + " Aux " : juce::String ("Aux ")) + juce::String (countTracks (model::Track::Type::aux) + 1);
        track.type   = model::Track::Type::aux;
        track.colour = model::Session::colourForTrackIndex (session.getNumTracks());
        track.inputBus = juce::jlimit (0, model::Track::numBuses - 1, inputBus);   // the bus it reads; the track menu or mixer can change it
        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        applyNewTrackDefaults (raw->getTrackIndex());
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

    // The Sample Library: a window over the loop browser, opened from + Track (or L, which also closes it). Its Close
    // button, Escape and the title bar's close button hide it. Choosing a loop adds a track.
    void showLibraryWindow (bool toggle = false)
    {
        if (libraryWindow != nullptr && libraryWindow->isVisible() && toggle) { closeLibraryWindow(); return; }
        if (libraryWindow == nullptr)
        {
            libraryWindow = std::make_unique<LibraryWindow> (loopBrowser, [this] { closeLibraryWindow(); });
            loopBrowser.onClose = [this] { closeLibraryWindow(); };
        }
        libraryWindow->setVisible (true);
        libraryWindow->toFront (true);
        loopBrowser.grabKeyboardFocus();
    }
    void closeLibraryWindow()
    {
        loopBrowser.stopPreview();
        if (libraryWindow != nullptr) libraryWindow->setVisible (false);
        grabKeyboardFocus();
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

    // Fades for every audio clip on a track, applied as one undo step (with the dialog's clip gain)
    void showTrackFadesDialog (int trackIndex)
    {
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr) return;
        const auto refs = model::ClipEdits::allClips (session, trackIndex);
        if (refs.empty()) return;
        ui::FadesDialog::Values initial;
        if (const auto first = trackArea.fadeValuesFor (refs.front()))
        { initial.fadeInMs = first->fadeInMs; initial.fadeOutMs = first->fadeOutMs; initial.inShape = first->inShape; initial.outShape = first->outShape; initial.gainDb = first->gainDb; }
        auto* dialog = new ui::FadesDialog (initial, (int) refs.size());
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "Fades: every clip on " + t->name;
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        dialog->onCancel = [window] { window->setVisible (false); };
        dialog->onApply = [this, window, trackIndex] (const ui::FadesDialog::Values& v)
        {
            window->setVisible (false);
            const auto* tr = session.getTrack (trackIndex);
            if (tr == nullptr) return;
            ui::TrackArea::FadeValues fv;
            fv.fadeInMs = v.fadeInMs; fv.fadeOutMs = v.fadeOutMs; fv.inShape = v.inShape; fv.outShape = v.outShape; fv.gainDb = v.gainDb;
            const int n = trackArea.applyFadesTo (model::ClipEdits::allClips (session, trackIndex), fv, "Fades on Track");
            statusMessage = "Fades set on " + juce::String (n) + " clip(s) of " + tr->name; updateStatus();
            grabKeyboardFocus();
        };
    }

    void showFadesDialog()
    {
        const auto values = trackArea.currentFadeValues();
        if (! values)
        {
            statusMessage = "Select one or more clips first (Fades applies to the selection)";
            updateStatus();
            return;
        }

        ui::FadesDialog::Values initial;
        initial.fadeInMs = values->fadeInMs; initial.fadeOutMs = values->fadeOutMs;
        initial.inShape = values->inShape; initial.outShape = values->outShape; initial.gainDb = values->gainDb;

        auto* dialog = new ui::FadesDialog (initial, trackArea.numSelectedClips());
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
        auto snapshot = model::buildRenderSnapshot (session);
        snapshot->panDepthDb = panDepthDb();
        bounceJob = std::make_unique<BounceJob> (std::move (snapshot), settings, file,
            [this] (const engine::BounceResult& r, const juce::File& f)
            {
                statusMessage = describeBounce (r, f);
                bouncedOnce = bouncedOnce || r.ok();
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
        const auto dir = juce::File (prefs.getString ("operation.audioFilesFolder"));

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

        const int bitDepths[] = { 16, 24, 32 };
        if (const auto error = engine.getRecorder().start (slots, engine.getSampleRate(), bitDepths[juce::jlimit (0, 2, prefs.getInt ("operation.recordBitDepth"))],
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
            startTransport (true);

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
        const int autosaveMinutes = prefs.getInt ("operation.autosaveMinutes");
        if (autosaveMinutes > 0 && ++autosaveCounter >= 20 * 60 * autosaveMinutes) { autosaveCounter = 0; autosaveTick(); }
        // Timeline insertion follows playback: off = the playhead returns to where play started
        const bool playingNow = engine.getTransport().isPlaying();
        if (playingNow) playedOnce = true;
        transportBar.setCpuLoad (engine.getGraph().getPerformance().getLoad(), engine.getGraph().getPerformance().getOverruns());
        if (auto* f = getCurrentlyFocusedComponent(); f != nullptr && f != this && (f == &pianoRoll || f == &sequencer || f == &trackArea)) lastFocused = f;
        // Window activation hands focus to the window itself; give it back to the editor that had it.
        if (auto* f = getCurrentlyFocusedComponent(); f != nullptr && f == getTopLevelComponent() && lastFocused != nullptr && lastFocused->isShowing()) lastFocused->grabKeyboardFocus();
        if (std::getenv ("BEATMAKER_DEBUG_FOCUS")) { auto* f = getCurrentlyFocusedComponent(); const juce::String n = f == nullptr ? "none" : f == this ? "main" : f == &sequencer ? "sequencer" : f == &pianoRoll ? "pianoRoll" : f == &trackArea ? "trackArea" : f->getTitle(); if (n != debugFocusName) { debugFocusName = n; std::cout << "focus -> " << n << std::endl; } }
        if (auto* d = engine.getDeviceManager().getCurrentAudioDevice())
        {
            const auto info = d->getName() + " " + juce::String (d->getCurrentSampleRate()) + " Hz " + juce::String (d->getCurrentBufferSizeSamples());
            if (info != lastDeviceInfo) { lastDeviceInfo = info; ui::CrashReporter::get().setAudioDevice (info); juce::Logger::writeToLog ("Audio device: " + info); }
        }
        if (mixerVisible) mixedOnce = true;
        if (playingNow && ! wasPlayingLastTick) playStartSample = engine.getTransport().getPositionSamples();
        if (! playingNow && wasPlayingLastTick && ! prefs.getBool ("operation.timelineFollowsPlayback") && ! engine.getRecorder().isRecording())
            engine.getTransport().setPositionSamples (playStartSample);
        wasPlayingLastTick = playingNow;
        // Post-roll reached: stop (the take is finished below).
        if (engine.getRecorder().isRecording() && autoStopSample >= 0 && engine.getTransport().getPositionSamples() >= autoStopSample)
            engine.getTransport().stop();
        // Stopping the transport ends the take.
        if (engine.getRecorder().isRecording() && ! engine.getTransport().isPlaying())
            finishRecording();

        automation.tick();
    }

    // A bundled kit, synthesised once per name at the engine rate and shared by every track that uses it
    std::shared_ptr<const engine::DrumKit> kitNamed (const juce::String& name)
    {
        const auto* info = engine::DrumKitFactory::info (name);
        const juce::String key = info != nullptr ? juce::String (info->name) : juce::String (engine::DrumKitFactory::defaultKitName());
        auto& slot = kitCache[key];
        if (slot == nullptr) slot = engine::DrumKitFactory::createKit (key, engine.getSampleRate());
        if (key == engine::DrumKitFactory::defaultKitName()) defaultKit = slot;
        return slot;
    }

    void addDrumMachineTrack (const juce::String& kitName = {})
    {
        const auto kit = kitNamed (kitName);
        model::Track track;
        track.name    = (kitName.isEmpty() || kitName == engine::DrumKitFactory::defaultKitName() ? juce::String ("Drums ") : kitName + " ") + juce::String (countTracks (model::Track::Type::instrument) + 1);
        track.type    = model::Track::Type::instrument;
        track.instrumentKind = model::Track::InstrumentKind::drumMachine;
        track.colour  = model::Session::colourForTrackIndex (session.getNumTracks());
        track.drumKit = kit;

        auto cmd = std::make_unique<model::AddTrackCommand> (std::move (track));
        auto* raw = cmd.get();
        session.execute (std::move (cmd));
        const int index = raw->getTrackIndex();
        applyNewTrackDefaults (index);

        // A one-bar pattern clip with a starter beat so Play makes sound immediately;
        // it plays once until Loop is switched on and the clip is lengthened.
        const auto& transport = engine.getTransport();
        model::PatternClip clip;
        clip.name       = "Beat";
        clip.pattern    = std::make_shared<const engine::StepPattern> (engine::StepPattern::createDefaultBeat());
        clip.sampleRate = engine.getSampleRate();
        clip.length     = (juce::int64) std::llround (transport.beatsToSeconds (clip.pattern->getLengthBeats()) * clip.sampleRate);
        clip.loop       = false;
        session.execute (std::make_unique<model::AddPatternClipCommand> (index, std::move (clip)));

        trackArea.setSelectedTrack (index);
        updateSequencerTarget();
        setEditorVisible (true);
    }

    void addInstrumentTrack (engine::InstrumentType type, const juce::String& presetName = {})
    {
        if (type == engine::InstrumentType::none) type = engine::InstrumentType::subtractive;
        const auto presets = engine::Instrument::presets (type);
        size_t startPreset = type == engine::InstrumentType::subtractive && presets.size() > 1 ? 1 : 0;   // Pluck
        for (size_t i = 0; i < presets.size(); ++i) if (presetName.isNotEmpty() && presets[i].presetName == presetName) startPreset = i;

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
        applyNewTrackDefaults (index);

        // A two-bar arpeggio clip so Play makes sound immediately; it plays once
        // until Loop is switched on and the clip is lengthened.
        const auto& transport = engine.getTransport();
        model::MidiClip clip;
        clip.name       = "Arp";
        clip.sequence   = std::make_shared<const engine::MidiSequence> (engine::MidiSequence::createDefaultArpeggio());
        clip.sampleRate = engine.getSampleRate();
        clip.length     = (juce::int64) std::llround (transport.beatsToSeconds (clip.sequence->lengthBeats) * clip.sampleRate);
        clip.loop       = false;
        session.execute (std::make_unique<model::AddMidiClipCommand> (index, std::move (clip)));

        trackArea.setSelectedTrack (index);
        updateSequencerTarget();
        setEditorVisible (true);

        if (engine::Instrument::usesSample (type))
        {
            statusMessage = juce::String (engine::Instrument::typeName (type)) + ": drop an audio file onto the track to load it";
            updateStatus();
        }
    }

    void loadSamplerSample (int trackIndex, const juce::File& file)
    {
        auto* track = session.getTrack (trackIndex);
        if (track == nullptr || ! track->hasInstrument() || ! engine::Instrument::usesSample (track->instrumentType())) return;

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

        sequencer.setTarget (drums ? sel : -1);
        pianoRoll.setTarget (synth ? sel : -1);
        if (smartControls.getTrackIndex() != sel) smartControls.setTrack (sel);
        pianoRoll.setVisible (editorVisible && ! mixerVisible && synth);
        sequencer.setVisible (editorVisible && ! mixerVisible && ! synth);
    }

    // The editor panel's editor for the selected track: the note editor or the drum editor.
    ui::EditorPanel* activeEditor() { return pianoRoll.isVisible() ? (ui::EditorPanel*) &pianoRoll : sequencer.isVisible() ? (ui::EditorPanel*) &sequencer : nullptr; }
    juce::Component* activeEditorComponent() { return pianoRoll.isVisible() ? (juce::Component*) &pianoRoll : sequencer.isVisible() ? (juce::Component*) &sequencer : nullptr; }
    bool notesFocused() const
    {
        return (pianoRoll.isVisible() && pianoRoll.hasKeyboardFocus (true)) || (sequencer.isVisible() && sequencer.hasKeyboardFocus (true));
    }
    ui::EditorPanel* focusedEditor() { return notesFocused() ? activeEditor() : nullptr; }

    // Keyboard focus between the editor panel (notes or drums) and the tracks (Ctrl+Alt+N, the Editor button).
    void focusNotes (bool notes)
    {
        if (notes)
        {
            if (! editorVisible) setEditorVisible (true);
            auto* ed = activeEditor(); auto* comp = activeEditorComponent();
            if (ed == nullptr || comp == nullptr || ! ed->hasTarget()) { statusMessage = "Editor: select an instrument or drum track with a clip first"; updateStatus(); return; }
            comp->grabKeyboardFocus();
            statusMessage = "Keyboard focus: editor (" + ed->describeSelection() + ")";
        }
        else
        {
            trackArea.grabKeyboardFocus();
            statusMessage = "Keyboard focus: tracks";
        }
        updateStatus();
        editToolbar.refresh();
    }

    void setEditorVisible (bool visible)
    {
        editorVisible = visible;
        if (visible && mixerVisible) { mixerVisible = false; mixerView.setVisible (false); transportBar.setMixerVisible (false); }   // one or the other
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
        t.bpm = session.getBpm(); t.beatsPerBar = session.getBeatsPerBar();
        t.loopStart = tr.getLoopStart(); t.loopEnd = tr.getLoopEnd(); t.loopEnabled = tr.isLoopEnabled();
        return t;
    }

    persistence::LoadContext loadContext()
    {
        persistence::LoadContext ctx;
        ctx.sampleRate = engine.getSampleRate();
        ctx.deferElasticRenders = true;   // rendered by renderPendingElastic() after the load
        ctx.loadAudio = [this] (const juce::File& f) -> std::shared_ptr<const juce::AudioBuffer<float>>
        {
            juce::String error;
            auto loaded = loader.load (f, engine.getSampleRate(), error);
            return loaded ? loaded->audio : nullptr;
        };
        ctx.defaultKit = [this]
        {
            return kitNamed (engine::DrumKitFactory::defaultKitName());
        };
        ctx.kitNamed = [this] (const juce::String& name) { return kitNamed (name); };
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
        ui::CrashReporter::get().setSessionPath (sessionFile.getFullPathName());
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setName (currentSessionName() + " - Beat Maker");
    }

    // Writes the session to `bundle` (a .bmk or .bmkt directory).
    bool writeSession (const juce::File& bundle, bool asTemplate)
    {
        ui::UiProfiler::Scope scope ("saveSession");
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
        const auto suggested = sessionsFolder().getChildFile (currentSessionName() + ".bmk");
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

    // Rename Session: the name in the title and, for a saved session, the bundle folder on disk. A saved session is
    // saved first and reopened from its new folder, so every media reference points into the renamed bundle.
    void showRenameSession()
    {
        auto* w = new juce::AlertWindow ("Rename Session",
                                         sessionFile != juce::File() ? "The session is saved and its folder on disk renamed too:\n" + sessionFile.getParentDirectory().getFullPathName()
                                                                     : "The name is used in the title and when the session is first saved.",
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", currentSessionName(), "Name");
        w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1) renameSession (w->getTextEditorContents ("name"));
            grabKeyboardFocus();
        }), true);
    }

    // Delete Session: pick a session bundle (the open one is offered first), confirm, and move it to the trash
    // (deleted outright only where there is no trash). Deleting the open session leaves an empty Untitled one.
    void showDeleteSession()
    {
        sessionsFolder().createDirectory();
        auto* browser = ui::SessionChooser::launch ("Delete Session: choose a session", sessionsFolder(), "*.bmk", [this] (juce::File f)
        {
            if (f == juce::File()) { grabKeyboardFocus(); return; }
            if (f.getFileName() == "session.json") f = f.getParentDirectory();
            if (! persistence::SessionFile::isSessionBundle (f)) { statusMessage = "Not a session: " + f.getFileName(); updateStatus(); grabKeyboardFocus(); return; }
            const bool open = f == sessionFile;
            auto* w = new juce::AlertWindow ("Delete Session",
                                             "Move \"" + persistence::SessionFile::sessionName (f) + "\" to the trash?\n" + f.getFullPathName()
                                                 + "\n\nThe whole bundle goes, recordings and backups included. It can be brought back from the trash."
                                                 + (open ? "\n\nThis is the open session: an empty Untitled session takes its place." : juce::String()),
                                             juce::MessageBoxIconType::WarningIcon);
            w->addButton ("Delete", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, f] (int result)
            {
                if (result == 1) deleteSessionBundle (f);
                grabKeyboardFocus();
            }), true);
        });
        if (browser != nullptr && sessionFile != juce::File()) browser->showBundle (sessionFile);
    }

    bool deleteSessionBundle (const juce::File& bundle)
    {
        if (! persistence::SessionFile::isSessionBundle (bundle)) { statusMessage = "Not a session: " + bundle.getFileName(); updateStatus(); return false; }
        const auto name = persistence::SessionFile::sessionName (bundle);
        const bool wasOpen = bundle == sessionFile;
        if (wasOpen) newEmptySession ("Untitled");   // let go of it first: the recorder, backups and the title all point into it
        ui::CrashReporter::get().addBreadcrumb ("delete session " + bundle.getFileName());
        bool trashed = bundle.moveToTrash();
        if (! trashed && bundle.exists() && ! bundle.deleteRecursively()) { statusMessage = "Could not delete " + name; updateStatus(); return false; }
        statusMessage = "Session " + name + (trashed ? " moved to the trash" : " deleted") + (wasOpen ? " (now on a new Untitled session)" : ""); updateStatus();
        return true;
    }

    bool renameSession (const juce::String& newName)
    {
        const auto name = persistence::SessionFile::legalSessionName (newName);
        if (name.isEmpty()) { statusMessage = "A session needs a name"; updateStatus(); return false; }
        if (sessionFile == juce::File())
        {
            untitledName = name; updateWindowTitle();
            statusMessage = "Session named " + name + " (it is saved under that name)"; updateStatus();
            return true;
        }
        if (name == currentSessionName()) return true;
        if (! writeSession (sessionFile, false)) return false;
        juce::String error;
        const auto target = persistence::SessionFile::renameBundle (sessionFile, name, error);
        if (target == juce::File()) { statusMessage = "Rename failed: " + error; updateStatus(); return false; }
        ui::CrashReporter::get().addBreadcrumb ("rename " + sessionFile.getFileName() + " -> " + target.getFileName());
        if (! openSessionBundle (target)) return false;
        statusMessage = "Session renamed to " + name; updateStatus();
        return true;
    }

    bool openSessionBundle (const juce::File& bundle)
    {
        ui::CrashReporter::get().addBreadcrumb ("open " + bundle.getFileName());
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
        renderPendingElastic (true);
        return true;
    }

    // Elastic clips whose render was deferred at load: render them on a background
    // thread behind a progress window (inline when quick) and apply as one step.
    void renderPendingElastic (bool clearHistoryAfter)
    {
        const auto pending = model::Elastic::pendingRenders (session);
        if (pending.empty()) return;
        std::vector<std::unique_ptr<model::SetClipElasticCommand>> cmds;
        for (const auto& ref : pending)
        {
            const auto& clip = session.getTracks()[(size_t) ref.track].clips[(size_t) ref.index];
            auto cmd = std::make_unique<model::SetClipElasticCommand> (session, ref, clip.elastic, "Restore Elastic Audio", true);
            if (! cmd->wasCancelled()) cmds.push_back (std::move (cmd));
        }
        const int count = (int) cmds.size();
        ui::ElasticJob::runMany (session, std::move (cmds), "Restoring Elastic Audio (" + juce::String (count) + " clips)...", "Restore Elastic Audio", [this, clearHistoryAfter] (int applied)
        {
            if (clearHistoryAfter) session.clearHistory();
            if (applied > 0) { statusMessage = "Elastic Audio restored on " + juce::String (applied) + " clip(s)"; updateStatus(); }
        });
    }

    // The Open Session browser: a session bundle opens on double-click, Return or Open (it is a folder, but not one to step into)
    ui::SessionBrowser* openSessionChooser()
    {
        sessionsFolder().createDirectory();
        return ui::SessionChooser::launch ("Open Session or Audio File", sessionsFolder(), "*.bmk;*.bmkt;" + loader.getWildcard(), [this] (juce::File f)
        {
            if (f == juce::File()) return;
            if (f.getFileName() == "session.json") f = f.getParentDirectory();
            if (persistence::SessionFile::isSessionBundle (f)) openSessionBundle (f);
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
        if (choice.templateFile != juce::File()) { openSessionBundle (choice.templateFile); untitledName = name; updateWindowTitle(); return; }
        newEmptySession (name);
        const double sr = engine.getSampleRate();
        auto& tr = engine.getTransport();
        if (choice.name == "Beat Making")
        {
            setTempo (90.0, 4, false);
            addDrumMachineTrack();
            addInstrumentTrack (engine::InstrumentType::subtractive);
            addInstrumentTrack (engine::InstrumentType::bass);
            addTrack ("Vocal");
            addTrack ("Sample");
            for (int i = 0; i < 3; ++i) session.execute (std::make_unique<model::SetTrackMixCommand> (i, 0.6f, 0.0f));   // headroom for the sum
            tr.setLoopRange (0, (juce::int64) std::llround (tr.beatsToSeconds (16.0) * sr));
            tr.setLoopEnabled (true);
        }
        else if (choice.name == "Songwriter")
        {
            setTempo (120.0, 4, false);
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

    //==========================================================================
    // Preferences and the command palette

    static juce::File preferencesFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Beat Maker").getChildFile ("Beat Maker.preferences");
    }

    void applyPreferences (const juce::String& id)
    {
        auto is = [&] (const char* key) { return id.isEmpty() || id == key; };
        if (is ("display.theme"))
        {
            ui::theme::applyPalette (ui::theme::palette (prefs.getInt ("display.theme")));
            ui::theme::applyLookAndFeel (lookAndFeel);
            sendLookAndFeelChange();
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
                if (auto* c = juce::Desktop::getInstance().getComponent (i)) { c->sendLookAndFeelChange(); c->repaint(); }
            repaint();
        }
        if (is ("display.editorHeight")) resized();
        if (is ("display.pianoRollRowHeight")) pianoRoll.setRowHeight (prefs.getInt ("display.pianoRollRowHeight"));
        if (is ("display.uiScale"))
        {
            const float scales[] = { 1.0f, 1.25f, 1.5f, 1.75f };
            juce::Desktop::getInstance().setGlobalScaleFactor (scales[juce::jlimit (0, 3, prefs.getInt ("display.uiScale"))]);
        }
        if (is ("display.trackHeight"))
        {
            const int heights[] = { 88, 124, 168, 220 };
            trackArea.setTrackHeight (heights[juce::jlimit (0, 3, prefs.getInt ("display.trackHeight"))]);
        }
        if (is ("processing.backgroundRenderSeconds")) ui::ElasticJob::asyncThresholdSeconds = prefs.getDouble ("processing.backgroundRenderSeconds");
        if (is ("metronome.level"))      engine.getGraph().getMetronome().setLevel (juce::Decibels::decibelsToGain ((float) prefs.getDouble ("metronome.level"), -60.0f));
        if (is ("metronome.sound"))      engine.getGraph().getMetronome().setSound (prefs.getInt ("metronome.sound"));
        if (is ("metronome.accent"))     engine.getGraph().getMetronome().setAccent (prefs.getBool ("metronome.accent"));
        if (is ("metronome.recordOnly")) engine.getGraph().getMetronome().setRecordOnly (prefs.getBool ("metronome.recordOnly"));
        if (is ("operation.crashReports")) ui::CrashReporter::get().setEnabled (prefs.getBool ("operation.crashReports"));
        if (is ("processing.audioCacheMb")) loader.setCacheBudgetBytes ((juce::int64) (prefs.getDouble ("processing.audioCacheMb") * 1024.0 * 1024.0));
        if (is ("midi.defaultVelocity") || is ("midi.softVelocity")) pianoRoll.setDefaultVelocities (prefs.getInt ("midi.defaultVelocity"), prefs.getInt ("midi.softVelocity"));
        if (is ("mixing.panDepth")) pushSnapshot();
    }

    float panDepthDb() const
    {
        const float depths[] = { 2.5f, 3.0f, 4.5f, 6.0f };
        return depths[juce::jlimit (0, 3, prefs.getInt ("mixing.panDepth"))];
    }

    void applyNewTrackDefaults (int trackIndex)
    {
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr) return;
        const auto meter = (model::MeterType) juce::jlimit (0, 6, prefs.getInt ("mixing.defaultMeterType"));
        const auto mode = (model::AutomationMode) juce::jlimit (0, 5, prefs.getInt ("mixing.defaultAutomationMode"));
        if (t->meterType != meter) session.execute (std::make_unique<model::SetMeterTypeCommand> (trackIndex, meter));
        if (t->automationMode != mode) session.execute (std::make_unique<model::SetAutomationModeCommand> (trackIndex, mode));
    }

    void showPreferences (const juce::String& settingId)
    {
        if (preferencesWindow != nullptr)
        {
            preferencesWindow->setVisible (true); preferencesWindow->toFront (true);
            if (preferencesContent != nullptr && settingId.isNotEmpty()) preferencesContent->showSetting (settingId);
            return;
        }
        auto* content = new ui::PreferencesWindow (prefs);
        preferencesContent = content;
        content->chooseFolder = [this] (const juce::File& current)
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Audio Files Folder", current.isDirectory() ? current : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) prefs.set ("operation.audioFilesFolder", fc.getResult().getFullPathName());
            });
        };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Preferences";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        preferencesWindow = options.launchAsync();
        if (settingId.isNotEmpty()) content->showSetting (settingId);
    }

    void showCommandPalette (const juce::String& query)
    {
        if (paletteWindow != nullptr) { paletteWindow->setVisible (false); }
        auto* content = new ui::CommandPalette (commands, prefs);
        content->onDismiss = [this] { if (paletteWindow != nullptr) paletteWindow->setVisible (false); grabKeyboardFocus(); };
        content->onOpenPreference = [this] (const juce::String& id) { showPreferences (id); };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Command Palette";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        paletteWindow = options.launchAsync();
        paletteOpenedOnce = true;
        if (query.isNotEmpty()) content->setQuery (query);
        content->grabKeyboardFocus();
    }

    //==========================================================================
    // Onboarding: welcome, tour, tutorials, shortcuts, sample projects

    static juce::File samplesFolder() { return sessionsFolder().getChildFile ("Sample Projects"); }

    void openSampleProject (const juce::String& name)
    {
        samplesFolder().createDirectory();
        const auto bundle = samplesFolder().getChildFile (juce::File::createLegalFileName (name) + ".bmk");
        if (! persistence::SessionFile::isSessionBundle (bundle))
        {
            model::Session scratch; persistence::TransportState ts; juce::String error;
            const auto ctx = loadContext();
            if (! persistence::SampleProjects::create (name, scratch, ts, engine.getSampleRate(), bundledLoopsFolder(), ctx.loadAudio, error)
                || ! persistence::SessionFile::save (scratch, ts, bundle).isEmpty())
            { statusMessage = "Sample project failed: " + error; updateStatus(); return; }
        }
        if (openSessionBundle (bundle)) { statusMessage = "Opened sample project " + name + "  (saved in " + samplesFolder().getFullPathName() + ")"; updateStatus(); }
    }

    void showWelcome()
    {
        if (welcomeWindow != nullptr) { welcomeWindow->setVisible (true); welcomeWindow->toFront (true); return; }
        juce::StringArray names, descs;
        for (const auto& s : persistence::SampleProjects::list()) { names.add (s.name); descs.add (s.description); }
        auto* content = new ui::WelcomeWindow (names, descs, prefs.getBool ("display.showWelcome"));
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Welcome";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        welcomeWindow = options.launchAsync();
        auto close = [this] { if (welcomeWindow != nullptr) welcomeWindow->setVisible (false); };
        content->onClose = [close] { close(); };
        content->onNewSession = [this, close] { close(); showNewSessionDialog(); };
        content->onOpenSession = [this, close] { close(); openSessionChooser(); };
        content->onOpenSample = [this, close] (const juce::String& n) { close(); openSampleProject (n); };
        content->onTour = [this, close] { close(); startTour(); };
        content->onTutorials = [this, close] { close(); showTutorials(); };
        content->onShortcuts = [this] { showShortcuts(); };
        content->onShowAtStartupChanged = [this] (bool on) { prefs.set ("display.showWelcome", on); };
    }

    void startTour()
    {
        std::vector<ui::TourOverlay::Step> steps;
        steps.push_back ({ "Transport", "Space plays and stops, R records (arm a track first), C turns Cycle on. The Rec menu picks Normal, QuickPunch, TrackPunch or Loop record; Pre/Post sets pre- and post-roll.", [this] { return (juce::Component*) &transportBar; }, {} });
        steps.push_back ({ "Tracks", "+ Track adds audio, Drum Machine, any instrument, an aux input, a VCA, or a loop from the Sample Library (L): click a loop there to audition it, double-click to add it at the playhead, conformed to the session tempo. Right-click a header to rename, freeze or commit. The strip above the ruler holds memory locations and arrangement sections (M adds a marker).", [this] { return (juce::Component*) &trackArea; }, {} });
        steps.push_back ({ "Edit modes and tools", "Shuffle / Slip / Spot / Grid and the Zoomer, Trimmer, Selector, Grabber, Scrubber, Pencil and Smart Tool (F1-F11). TCE makes the Trimmer stretch; a-z turns on single-key edit commands.", [this] { return (juce::Component*) &editToolbar; }, {} });
        steps.push_back ({ "Editor panel", "The step sequencer for drum tracks and the piano roll for instruments, with presets. E hides and shows it.", [this] { return (juce::Component*) (sequencer.isVisible() ? (juce::Component*) &sequencer : (juce::Component*) &pianoRoll); }, [this] { setEditorVisible (true); } });
        steps.push_back ({ "Smart Controls", "Macro knobs for the selected track: volume and pan, every instrument parameter, drum pad levels. A drag is one undo step.", [this] { return (juce::Component*) &smartControls; }, [this] { setControlsVisible (true); } });
        steps.push_back ({ "Mix window", "One strip per track: ten inserts (built-in effects or plugins), five sends, fader, pan, meters, automation mode. Right-click an insert to change it; click to edit.", [this] { return (juce::Component*) &mixerView; }, [this] { setMixerVisible (true); } });
        steps.push_back ({ "Command palette and Preferences", "Ctrl+Shift+P finds any command or setting by name; Ctrl+, opens Preferences; Ctrl+/ lists every shortcut. The File... button has sessions, templates, stems and import.", [this] { return (juce::Component*) &transportBar; }, [this] { setMixerVisible (false); } });
        tour.start (std::move (steps));
    }

    std::vector<ui::Tutorial> makeTutorials()
    {
        auto hasTrack = [this] (std::function<bool (const model::Track&)> pred) { return [this, pred] { for (const auto& t : session.getTracks()) if (pred (t)) return true; return false; }; };
        std::vector<ui::Tutorial> out;
        ui::Tutorial beat;
        beat.title = "Make your first beat";
        beat.summary = "A drum track, an instrument, a loop, then play it back and bounce it.";
        beat.steps = {
            { "Add a Drum Machine track", "+ Track > Drum Machine Track. It comes with a starter beat; click steps in the sequencer to change it.", "track.addDrums", hasTrack ([] (const model::Track& t) { return t.isDrumMachine(); }) },
            { "Add an instrument track", "+ Track > Instrument Track > Synth (or any other). Draw notes in the piano roll.", "track.addSynth", hasTrack ([] (const model::Track& t) { return t.isSynth(); }) },
            { "Turn on Cycle", "Press C or click Cycle so the arrangement loops.", "transport.cycle", [this] { return engine.getTransport().isLoopEnabled(); } },
            { "Press Play", "Space starts and stops the transport.", "transport.playStop", [this] { return engine.getTransport().isPlaying() || playedOnce; } },
            { "Bounce to Disk", "Ctrl+B renders the arrangement to WAV, AIFF or FLAC.", "file.bounce", [this] { return bouncedOnce; } } };
        out.push_back (beat);
        ui::Tutorial rec;
        rec.title = "Record audio";
        rec.summary = "Arm an audio track, pick a record mode and punch in.";
        rec.steps = {
            { "Add an audio track", "+ Track > Audio Track (Ctrl+Shift+N).", "track.addAudio", hasTrack ([] (const model::Track& t) { return t.isAudio(); }) },
            { "Arm it", "Click the track's R button and choose its input.", "", hasTrack ([] (const model::Track& t) { return t.isAudio() && t.armed; }) },
            { "Record", "Press R (or Ctrl+Space). Stop ends the take. The Rec menu offers QuickPunch, TrackPunch and Loop record.", "transport.record", hasTrack ([] (const model::Track& t) { return t.isAudio() && ! t.clips.empty(); }) } };
        out.push_back (rec);
        ui::Tutorial mix;
        mix.title = "Mix with inserts and sends";
        mix.summary = "Open the mixer, add an effect, route a send to an aux return.";
        mix.steps = {
            { "Open the Mix window", "Press X.", "view.mixer", [this] { return mixerVisible || mixedOnce; } },
            { "Add an insert", "Click an empty insert slot and pick an effect, or a plugin.", "", hasTrack ([] (const model::Track& t) { for (const auto& i : t.inserts) if (! i.isEmpty()) return true; return false; }) },
            { "Add an aux input", "+ Track > Aux Input. It listens to a bus.", "track.addAux", hasTrack ([] (const model::Track& t) { return t.isAux(); }) },
            { "Send a track to the bus", "Click a send slot on a track and choose the aux's bus.", "", hasTrack ([] (const model::Track& t) { for (const auto& s : t.sends) if (s.isActive()) return true; return false; }) } };
        out.push_back (mix);
        ui::Tutorial pro;
        pro.title = "Pro Tools-depth tools";
        pro.summary = "Elastic Audio, memory locations, automation and the command palette.";
        pro.steps = {
            { "Add a memory location", "Press M at the playhead; Alt+1..9 recalls; Ctrl+5 lists them.", "edit.addMarker", [this] { return ! session.getMarkers().empty(); } },
            { "Stretch a clip", "Right-click an audio clip > Elastic Audio, or turn on TCE and trim its edge.", "", hasTrack ([] (const model::Track& t) { for (const auto& c : t.clips) if (c.isElastic()) return true; return false; }) },
            { "Write some automation", "Set a track's automation mode to Write or Latch, play, and move its fader.", "", hasTrack ([] (const model::Track& t) { for (const auto& l : t.automation) if (l != nullptr && ! l->isEmpty()) return true; return false; }) },
            { "Open the command palette", "Ctrl+Shift+P finds any command or setting.", "window.palette", [this] { return paletteOpenedOnce; } } };
        out.push_back (pro);
        return out;
    }

    void showTutorials()
    {
        if (tutorialsWindow != nullptr) { tutorialsWindow->setVisible (true); tutorialsWindow->toFront (true); return; }
        auto* content = new ui::TutorialWindow (makeTutorials());
        content->runCommand = [this] (const juce::String& id) { commands.run (id); };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Tutorials";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        tutorialsWindow = options.launchAsync();
    }

    // Diagnostics for bug reports: system, prefs of note, plugins, session summary, breadcrumbs, last crash, log tail.
    void reportProblem (bool revealFolder = true)
    {
        juce::String extra;
        extra << "Sample rate: " << engine.getSampleRate() << "\n";
        if (auto* d = engine.getDeviceManager().getCurrentAudioDevice()) extra << "Device: " << d->getName() << " (" << d->getTypeName() << "), buffer " << d->getCurrentBufferSizeSamples() << "\n";
        extra << "Tracks: " << session.getNumTracks() << ", length " << juce::String (session.getLengthSeconds(), 1) << " s, tempo " << engine.getTransport().getBpm() << "\n";
        extra << "Theme: " << prefs.getInt ("display.theme") << ", scale " << prefs.getDouble ("display.uiScale") << ", cache " << prefs.getDouble ("processing.audioCacheMb") << " MB\n";
        extra << "Plugins known: " << pluginManager.getKnownPlugins().getNumTypes() << "\n";
        const auto file = ui::CrashReporter::get().writeDiagnostics (extra);
        statusMessage = "Diagnostics written to " + file.getFullPathName() + " (attach it to an issue)";
        updateStatus();
        std::cout << "Diagnostics: " << file.getFullPathName() << std::endl;
        if (revealFolder) file.getParentDirectory().revealToUser();
    }

    // The launch after a crash: show the report, offer the newest auto-backup.
    void showPendingCrashReport()
    {
        auto& reporter = ui::CrashReporter::get();
        const auto report = reporter.pendingReport();
        if (report == juce::File()) return;
        const auto original = juce::File (reporter.sessionPathIn (report));
        juce::File autosave;
        if (original.isDirectory())
        {
            auto backups = original.getChildFile ("Session File Backups").findChildFiles (juce::File::findDirectories, false, "autosave-*.bmkt");
            backups.sort();
            if (! backups.isEmpty() && backups[backups.size() - 1].getLastModificationTime() > original.getChildFile ("session.json").getLastModificationTime())
                autosave = backups[backups.size() - 1];
        }
        auto* content = new ui::CrashReportWindow (report, autosave, original);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Beat Maker quit unexpectedly";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        auto* window = options.launchAsync();
        reporter.acknowledge (report);
        content->onClose = [window] { window->setVisible (false); };
        content->onRecover = [this, window] (const juce::File& backup, const juce::File& orig)
        {
            window->setVisible (false);
            if (openSessionBundle (backup)) { sessionFile = orig; updateWindowTitle(); statusMessage = "Recovered " + backup.getFileName() + "; Save writes to " + orig.getFileName(); updateStatus(); }
        };
    }

    // The built docs site next to the sources, or the markdown sources, or the project page.
    void openUserGuide()
    {
        const juce::File source (BEATMAKER_SOURCE_DIR);
        const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto site = exeDir.getChildFile ("docs").getChildFile ("index.html");                 // packaged: docs/ beside the binary
        if (! site.existsAsFile()) site = source.getChildFile ("site").getChildFile ("index.html");
        const auto md = source.getChildFile ("docs").getChildFile ("index.md");
        if (site.existsAsFile()) { juce::URL (site).launchInDefaultBrowser(); statusMessage = "User Guide opened in the browser"; }
        else if (md.existsAsFile()) { md.revealToUser(); statusMessage = "User Guide sources in " + md.getParentDirectory().getFullPathName() + " (run tools/build_docs.py for the site)"; }
        else statusMessage = "User Guide not found: build the docs site with tools/build_docs.py";
        updateStatus();
    }

    // --dump-docs=<dir>: the shortcut and preference tables for the user guide, from the live registries.
    void dumpDocs (const juce::File& dir)
    {
        dir.createDirectory();
        juce::String s = "# Keyboard shortcuts\n\nEvery shortcut is a registered command; the command palette (Ctrl+Shift+P) lists the same names. "
                         "Focus keys apply in Commands Keyboard Focus mode (the a-z button, Ctrl+Alt+K). Generated from the app by `--dump-docs`.\n";
        juce::StringArray categories;
        for (const auto& c : commands.all()) categories.addIfNotAlreadyThere (c.category);
        for (const auto& cat : categories)
        {
            s << "\n## " << cat << "\n\n| Command | Shortcut | Focus key |\n|---|---|---|\n";
            for (const auto& c : commands.all())
                if (c.category == cat)
                    s << "| " << c.name.replace ("|", "\\|") << " | " << (c.shortcut.isValid() ? "`" + c.shortcut.getTextDescription() + "`" : juce::String()) << " | "
                      << (c.focusKey != 0 ? "`" + juce::String::charToString (juce::CharacterFunctions::toUpperCase (c.focusKey)) + "`" : juce::String()) << " |\n";
        }
        dir.getChildFile ("shortcuts.md").replaceWithText (s);
        juce::String p = "# Preferences\n\nPreferences (Ctrl+,) apply immediately and live in `~/.config/Beat Maker/Beat Maker.preferences`. "
                         "The search box finds a setting by any word in its name or description; changed settings show a mark and can be reset one at a time or per category. Generated from the app by `--dump-docs`.\n";
        for (const auto& cat : prefs.categories())
        {
            p << "\n## " << cat << "\n\n| Setting | Default | What it does |\n|---|---|---|\n";
            for (const auto& d : prefs.all())
            {
                if (d.category != cat) continue;
                juce::String def;
                switch (d.type)
                {
                    case ui::PrefDef::Type::toggle: def = (bool) d.defaultValue ? "on" : "off"; break;
                    case ui::PrefDef::Type::choice: def = d.choices[(int) d.defaultValue]; break;
                    case ui::PrefDef::Type::number: def = juce::String ((double) d.defaultValue) + (d.unit.isNotEmpty() ? " " + d.unit : juce::String()); break;
                    default: def = d.defaultValue.toString().isEmpty() ? juce::String ("(empty)") : d.defaultValue.toString(); break;
                }
                juce::String desc = d.description;
                if (d.type == ui::PrefDef::Type::choice) desc << " Choices: " << d.choices.joinIntoString (", ") << ".";
                if (d.type == ui::PrefDef::Type::number) desc << " Range " << d.min << " to " << d.max << ".";
                p << "| " << d.name << " | " << def << " | " << desc.replace ("|", "\\|") << " |\n";
            }
        }
        dir.getChildFile ("preferences.md").replaceWithText (p);
        std::cout << "Docs tables written to " << dir.getFullPathName() << std::endl;
    }

    void showSystemUsage()
    {
        if (usageWindow != nullptr) { usageWindow->setVisible (true); usageWindow->toFront (true); return; }
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new ui::SystemUsageWindow (engine.getGraph().getPerformance(), loader, session,
                                                             [this] { auto* d = engine.getDeviceManager().getCurrentAudioDevice(); return d != nullptr ? d->getCurrentBufferSizeSamples() : 0; }));
        options.dialogTitle = "System Usage";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        usageWindow = options.launchAsync();
    }

    void showShortcuts()
    {
        if (shortcutsWindow != nullptr) { shortcutsWindow->setVisible (true); shortcutsWindow->toFront (true); return; }
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (new ui::ShortcutsWindow (commands));
        options.dialogTitle = "Keyboard Shortcuts";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        shortcutsWindow = options.launchAsync();
    }

    //==========================================================================
    // Import Session Data

    void chooseSessionToImport()
    {
        sessionsFolder().createDirectory();
        ui::SessionChooser::launch ("Import Session Data: choose a session", sessionsFolder(), "*.bmk;*.bmkt", [this] (juce::File f)
        {
            if (f.getFileName() == "session.json") f = f.getParentDirectory();
            if (persistence::SessionFile::isSessionBundle (f)) showImportSessionDialog (f);
            else if (f != juce::File()) { statusMessage = "Not a Beat Maker session: " + f.getFileName(); updateStatus(); }
        });
    }

    void showImportSessionDialog (const juce::File& bundle)
    {
        importSource = std::make_unique<model::Session>();
        persistence::TransportState ts; juce::StringArray warnings;
        if (const auto error = persistence::SessionImport::open (*importSource, ts, bundle, loadContext(), warnings); error.isNotEmpty())
        { statusMessage = "Import failed: " + error; updateStatus(); importSource.reset(); return; }
        auto* dialog = new ui::ImportSessionDialog (*importSource, ts, persistence::SessionFile::sessionName (bundle), engine.getTransport().getPositionSeconds());
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "Import Session Data";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        dialog->onCancel = [this, window] { window->setVisible (false); importSource.reset(); };
        dialog->onImport = [this, window, ts, warnings] (const persistence::ImportOptions& o)
        {
            window->setVisible (false);
            applyImport (o, ts, warnings);
            importSource.reset();
        };
    }

    void applyImport (const persistence::ImportOptions& o, const persistence::TransportState& ts, const juce::StringArray& loadWarnings)
    {
        if (importSource == nullptr) return;
        persistence::ImportSummary summary;
        auto cmd = persistence::SessionImport::build (session, *importSource, engine.getSampleRate(), o, summary);
        if (cmd == nullptr) { statusMessage = "Import Session Data: nothing selected"; updateStatus(); return; }
        session.execute (std::move (cmd));
        renderPendingElastic (false);
        if (o.importTempo) setTempo (ts.bpm, ts.beatsPerBar, false);
        trackArea.setSelectedTrack (session.getNumTracks() - 1);
        statusMessage = "Imported " + juce::String (summary.tracksAdded) + " new track(s)" + (summary.tracksMerged > 0 ? ", merged " + juce::String (summary.tracksMerged) : juce::String())
                      + ", " + juce::String (summary.clipsAdded) + " clips" + (summary.markersAdded > 0 ? ", " + juce::String (summary.markersAdded) + " memory locations" : juce::String())
                      + (o.importTempo ? ", tempo " + juce::String (ts.bpm, 1) : juce::String())
                      + (loadWarnings.isEmpty() && summary.warnings.isEmpty() ? juce::String() : "  WARNING: " + (loadWarnings.isEmpty() ? summary.warnings[0] : loadWarnings[0]));
        updateStatus();
    }

    //==========================================================================
    // Freeze / commit / stems

    static juce::File freezeFolder() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Freeze"); }

    // Renders a track's post-insert output from timeline 0 to the end of the session (+2 s tail).
    std::shared_ptr<const juce::AudioBuffer<float>> renderTrackOutput (int trackIndex, juce::File& writtenFile, juce::String& error)
    {
        int trim = 0;
        auto snapshot = model::Freeze::renderSnapshotForTrack (session, trackIndex, trim);
        snapshot->panDepthDb = panDepthDb();
        auto settings = defaultBounceSettings();
        settings.startSample = 0;
        settings.endSample = juce::jmax<juce::int64> ((juce::int64) settings.sampleRate, (juce::int64) std::llround (session.getLengthSeconds() * settings.sampleRate));
        settings.tailSeconds = 2.0; settings.trimTail = false; settings.normalize = false;   // keep the whole tail: the freeze must ring out like the live track
        juce::AudioBuffer<float> out;
        const auto result = engine::Bouncer::renderToBuffer (std::move (snapshot), settings, out);
        if (! result.ok()) { error = result.error.isNotEmpty() ? result.error : "cancelled"; return nullptr; }
        if (trim > 0 && trim < out.getNumSamples())
        {
            // The inserts delayed the audio by their latency: drop it so the render sits on the grid.
            juce::AudioBuffer<float> aligned (out.getNumChannels(), out.getNumSamples() - trim);
            for (int ch = 0; ch < out.getNumChannels(); ++ch) aligned.copyFrom (ch, 0, out, ch, trim, aligned.getNumSamples());
            out = std::move (aligned);
        }
        const auto dir = sessionFile != juce::File() ? sessionFile.getChildFile ("Audio Files") : freezeFolder();
        dir.createDirectory();
        writtenFile = dir.getChildFile (juce::File::createLegalFileName (session.getTracks()[(size_t) trackIndex].name) + " (frozen).wav").getNonexistentSibling (false);
        if (const auto err = engine::Bouncer::writeFile (out, writtenFile, settings); err.isNotEmpty()) error = err;
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (out));
    }

    void freezeTrack (int trackIndex)
    {
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr || ! model::Freeze::canFreeze (*t) || t->isFrozen()) return;
        juce::File file; juce::String error;
        auto audio = renderTrackOutput (trackIndex, file, error);
        if (audio == nullptr) { statusMessage = "Freeze failed: " + error; updateStatus(); return; }
        model::Track::FreezeState state;
        state.audio = audio; state.sampleRate = engine.getSampleRate(); state.file = file;
        session.execute (std::make_unique<model::FreezeTrackCommand> (trackIndex, state));
        statusMessage = "Froze " + t->name + "  (" + juce::String (audio->getNumSamples() / engine.getSampleRate(), 1) + " s rendered; fader, pan and sends stay live)";
        updateStatus();
    }

    void unfreezeTrack (int trackIndex)
    {
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr || ! t->isFrozen()) return;
        session.execute (std::make_unique<model::UnfreezeTrackCommand> (trackIndex));
        statusMessage = "Unfroze " + session.getTracks()[(size_t) trackIndex].name; updateStatus();
    }

    void commitTrack (int trackIndex)
    {
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr || ! model::Freeze::canFreeze (*t)) return;
        juce::File file; juce::String error;
        auto audio = renderTrackOutput (trackIndex, file, error);
        if (audio == nullptr) { statusMessage = "Commit failed: " + error; updateStatus(); return; }
        if (auto cmd = model::Freeze::commitCommand (session, trackIndex, audio, engine.getSampleRate(), file))
        {
            session.execute (std::move (cmd));
            trackArea.setSelectedTrack (trackIndex + 1);
            statusMessage = "Committed " + t->name + " to a new audio track (source muted)"; updateStatus();
        }
    }

    void toggleSelectedTrackFlag (model::SetTrackFlagCommand::Flag flag)
    {
        const int i = trackArea.getSelectedTrack();
        const auto* t = session.getTrack (i);
        if (t == nullptr) return;
        const bool current = flag == model::SetTrackFlagCommand::Flag::mute ? t->mute : flag == model::SetTrackFlagCommand::Flag::solo ? t->solo : t->armed;
        session.execute (model::GroupLogic::flagCommand (session, i, flag, ! current));
        statusMessage = t->name + (flag == model::SetTrackFlagCommand::Flag::mute ? (current ? ": unmuted" : ": muted") : flag == model::SetTrackFlagCommand::Flag::solo ? (current ? ": solo off" : ": soloed") : (current ? ": disarmed" : ": armed"));
        updateStatus();
    }

    // Effects from a track header: the same insert slots the Mix window shows
    void trackEffect (int trackIndex, const juce::String& action, int slot, engine::EffectType type, juce::Rectangle<int> anchor)
    {
        auto* t = session.getTrack (trackIndex);
        if (t == nullptr) return;
        if (action == "add")
        {
            const int empty = t->firstEmptyInsert();
            if (empty < 0) { statusMessage = "All " + juce::String (model::Track::numInsertSlots) + " insert slots are in use"; updateStatus(); return; }
            session.execute (std::make_unique<model::SetInsertCommand> (trackIndex, empty, type, engine.getSampleRate()));
            statusMessage = juce::String (engine::Effect::typeName (type)) + " added to " + t->name + " (slot " + juce::String (empty + 1) + ")"; updateStatus();
            // The Mix window shows the new insert in its slot, with its knobs open there
            setMixerVisible (true);
            juce::Timer::callAfterDelay (120, [this, trackIndex, empty] { mixerView.openInsertEditor (trackIndex, empty); });
            return;
        }
        if (action == "toggle")   // the header's FX button: all inserts off if any is on, else all on
        {
            bool anyOn = false;
            for (const auto& ins : t->inserts) anyOn = anyOn || (! ins.isEmpty() && ! ins.bypass);
            auto compound = std::make_unique<model::CompoundCommand> (anyOn ? "Bypass Effects" : "Enable Effects");
            for (int i = 0; i < (int) t->inserts.size(); ++i)
                if (! t->inserts[(size_t) i].isEmpty() && t->inserts[(size_t) i].bypass != anyOn)
                    compound->add (std::make_unique<model::SetInsertBypassCommand> (trackIndex, i, anyOn));
            if (compound->isEmpty()) { statusMessage = "No effects on " + t->name + " (right-click FX to add one)"; updateStatus(); return; }
            session.execute (std::move (compound));
            statusMessage = (anyOn ? "Effects off on " : "Effects on for ") + t->name; updateStatus();
            return;
        }
        if (! juce::isPositiveAndBelow (slot, model::Track::numInsertSlots) || t->inserts[(size_t) slot].isEmpty()) return;
        if (action == "bypass") session.execute (std::make_unique<model::SetInsertBypassCommand> (trackIndex, slot, ! t->inserts[(size_t) slot].bypass));
        else if (action == "remove") session.execute (std::make_unique<model::SetInsertCommand> (trackIndex, slot, engine::EffectType::none, engine.getSampleRate()));
        else if (action == "edit") editTrackEffect (trackIndex, slot, anchor);
    }

    void editTrackEffect (int trackIndex, int slot, juce::Rectangle<int> anchor)
    {
        // Called from a menu: the click that chose the item is still in flight and would dismiss a callout opened
        // right now, so the callout opens a moment later
        juce::Timer::callAfterDelay (60, [this, trackIndex, slot, anchor] { editTrackEffectNow (trackIndex, slot, anchor); });
    }

    void editTrackEffectNow (int trackIndex, int slot, juce::Rectangle<int> anchor)
    {
        auto* t = session.getTrack (trackIndex);
        if (t == nullptr || ! juce::isPositiveAndBelow (slot, model::Track::numInsertSlots) || t->inserts[(size_t) slot].isEmpty()) return;
        if (t->inserts[(size_t) slot].isPlugin()) { openPluginEditor (trackIndex, slot); return; }
        auto editor = std::make_unique<ui::EffectEditor> (t->inserts[(size_t) slot], engine.getSampleRate(), session,
            [this, trackIndex, slot] (std::shared_ptr<const engine::InsertParams> p, bool replace)
            {
                if (replace) session.undo();
                session.execute (std::make_unique<model::SetInsertParamsCommand> (trackIndex, slot, std::move (p)));
            },
            [this, trackIndex, slot] (int keyBus, bool listen) { session.execute (std::make_unique<model::SetInsertKeyCommand> (trackIndex, slot, keyBus, listen)); },
            [this, trackIndex, slot] { chooseImpulseResponse (trackIndex, slot); });
        juce::CallOutBox::launchAsynchronously (std::move (editor), anchor, nullptr);
    }

    void setTrackColour (int trackIndex, juce::Colour colour)
    {
        const auto* t = session.getTrackOrMaster (trackIndex);
        if (t == nullptr || t->colour == colour) return;
        session.execute (std::make_unique<model::SetTrackColourCommand> (trackIndex, colour));
    }

    // Colour > Custom...: a colour picker; the colour is applied once, on OK, as one undo step
    void promptTrackColour (int trackIndex)
    {
        const auto* t = session.getTrackOrMaster (trackIndex);
        if (t == nullptr) return;
        auto* window = new juce::AlertWindow ("Track Colour", "The colour of " + t->name + " in its header, clips and mixer strip.", juce::MessageBoxIconType::NoIcon);
        auto selector = std::make_unique<juce::ColourSelector> (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders | juce::ColourSelector::editableColour);
        selector->setCurrentColour (t->colour, juce::dontSendNotification);
        selector->setSize (300, 280);
        auto* sel = selector.get();
        window->addCustomComponent (sel);
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, trackIndex, sel, keep = std::move (selector)] (int result) mutable
        {
            if (result == 1) setTrackColour (trackIndex, sel->getCurrentColour().withAlpha (1.0f));
            keep.reset();
            grabKeyboardFocus();
        }), true);
    }

    void promptRenameBus (int bus)
    {
        if (! juce::isPositiveAndBelow (bus, model::Track::numBuses)) return;
        auto* window = new juce::AlertWindow ("Rename Bus", "Name of " + session.busName (bus) + ":", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", session.busName (bus));
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, bus] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
            {
                auto io = session.getIO();
                io.busNames.resize ((size_t) model::Track::numBuses);
                io.busNames[(size_t) bus] = name;
                session.execute (std::make_unique<model::SetIOSetupCommand> (io));
            }
            grabKeyboardFocus();
        }), true);
    }

    void promptRenameTrack (int trackIndex)
    {
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr) return;
        auto* window = new juce::AlertWindow ("Rename Track", "Name:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", t->name);
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, trackIndex] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty()) session.execute (std::make_unique<model::RenameTrackCommand> (trackIndex, name));
            grabKeyboardFocus();
        }), true);
    }

    // Stems: one file per track, rendered sequentially behind a progress window.
    class StemJob final : public juce::ThreadWithProgressWindow
    {
    public:
        struct Item { juce::String name; std::unique_ptr<engine::RenderSnapshot> snapshot; };
        StemJob (std::vector<Item> items, engine::BounceSettings s, juce::File dir, std::function<void (juce::String)> done)
            : ThreadWithProgressWindow ("Exporting stems...", true, true), stems (std::move (items)), settings (s), folder (std::move (dir)), onDone (std::move (done)) {}
        void run() override
        {
            const int n = (int) stems.size();
            for (int i = 0; i < n && ! threadShouldExit(); ++i)
            {
                setStatusMessage ("Rendering " + stems[(size_t) i].name + " (" + juce::String (i + 1) + "/" + juce::String (n) + ")");
                const auto file = folder.getChildFile (juce::File::createLegalFileName (stems[(size_t) i].name) + engine::BounceSettings::extensionFor (settings.format));
                const auto r = engine::Bouncer::renderToFile (std::move (stems[(size_t) i].snapshot), settings, file,
                                                              [this, i, n] (double p) { setProgress ((i + p) / n); return ! threadShouldExit(); });
                if (! r.ok()) { summary = "Stem " + stems[(size_t) i].name + ": " + (r.cancelled ? "cancelled" : r.error); return; }
                if (r.clipped) clipped.add (stems[(size_t) i].name);
                ++written;
            }
            summary = "Exported " + juce::String (written) + " stems to " + folder.getFullPathName() + (clipped.isEmpty() ? juce::String() : "  (clipped: " + clipped.joinIntoString (", ") + ")");
        }
        void threadComplete (bool) override { if (onDone) onDone (summary); }
    private:
        std::vector<Item> stems;
        engine::BounceSettings settings;
        juce::File folder;
        std::function<void (juce::String)> onDone;
        juce::String summary;
        juce::StringArray clipped;
        int written = 0;
    };

    void exportStems (const ui::StemExportDialog::Request& request, const juce::File& folder)
    {
        folder.createDirectory();
        auto settings = defaultBounceSettings();
        settings.format = request.settings.format; settings.bitDepth = request.settings.bitDepth;
        settings.tailSeconds = request.settings.tailSeconds; settings.trimTail = false; settings.normalize = false;
        const auto& tr = engine.getTransport();
        if (request.cycleRange && tr.hasValidLoop()) { settings.startSample = tr.getLoopStart(); settings.endSample = tr.getLoopEnd(); }
        else { settings.startSample = 0; settings.endSample = juce::jmax<juce::int64> (1, (juce::int64) std::llround (session.getLengthSeconds() * settings.sampleRate)); }
        std::vector<StemJob::Item> items;
        for (int i : request.tracks)
        {
            if (session.getTrack (i) == nullptr) continue;
            auto snap = model::Freeze::stemSnapshot (session, i, request.includeAuxReturns, request.throughMasterInserts);
            snap->panDepthDb = panDepthDb();
            items.push_back ({ juce::String (i + 1).paddedLeft ('0', 2) + " " + session.getTracks()[(size_t) i].name, std::move (snap) });
        }
        if (items.empty()) { statusMessage = "Export Stems: no tracks selected"; updateStatus(); return; }
        stemJob = std::make_unique<StemJob> (std::move (items), settings, folder, [this] (juce::String summary)
        {
            statusMessage = summary; updateStatus();
            juce::MessageManager::callAsync ([this] { stemJob.reset(); });
        });
        stemJob->launchThread();
    }

    juce::String currentSessionName() const { return sessionFile != juce::File() ? persistence::SessionFile::sessionName (sessionFile) : untitledName; }

    void exportAaf (const juce::File& file, const persistence::AafExportOptions& options)
    {
        const auto summary = persistence::AafExport::write (session, engine.getSampleRate(), currentSessionName(), file, options);
        if (! summary.ok()) { statusMessage = "Export AAF failed: " + summary.error; updateStatus(); std::cout << "AAF export failed: " << summary.error << std::endl; return; }
        statusMessage = "Exported AAF " + file.getFileName() + ": " + juce::String (summary.tracks) + " tracks, " + juce::String (summary.clips) + " clips, "
                        + juce::String (summary.essences) + (options.embedAudio ? " embedded" : " linked") + " audio files, " + juce::String (summary.markers) + " markers ("
                        + juce::File::descriptionOfSizeInBytes (summary.fileBytes) + ")";
        updateStatus();
        std::cout << "AAF " << file.getFullPathName() << " tracks=" << summary.tracks << " slots=" << summary.slots << " clips=" << summary.clips
                  << " essences=" << summary.essences << " markers=" << summary.markers << " bytes=" << summary.fileBytes << std::endl;
    }

    // --export-aaf=<file>[,link][,whole][,16]: synchronous export for smoke tests.
    void exportAafFromCommandLineImpl (const juce::String& spec)
    {
        const auto parts = juce::StringArray::fromTokens (spec, ",", {});
        persistence::AafExportOptions o;
        for (int i = 1; i < parts.size(); ++i)
        {
            if (parts[i] == "link") o.embedAudio = false;
            else if (parts[i] == "whole") o.consolidateClips = false;
            else if (parts[i] == "16") o.bitDepth = 16;
        }
        exportAaf (juce::File::getCurrentWorkingDirectory().getChildFile (parts[0]), o);
    }

    void showAafExport()
    {
        if (persistence::AafExport::exportableTracks (session).empty()) { statusMessage = "Export AAF: no audio tracks with clips (frozen instrument tracks count too)"; updateStatus(); return; }
        auto* dialog = new ui::AafExportDialog (session);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "Export AAF";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        dialog->onCancel = [window] { window->setVisible (false); };
        dialog->onExport = [this, window] (const persistence::AafExportOptions& o)
        {
            window->setVisible (false);
            const auto base = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Bounces");
            base.createDirectory();
            fileChooser = std::make_unique<juce::FileChooser> ("Export AAF", base.getChildFile (currentSessionName() + ".aaf"), "*.aaf");
            fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, o] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File()) exportAaf (fc.getResult().withFileExtension ("aaf"), o);
            });
        };
    }

    void showStemExport()
    {
        if (session.getNumTracks() == 0) { statusMessage = "Export Stems: the session is empty"; updateStatus(); return; }
        auto* dialog = new ui::StemExportDialog (session, engine.getTransport().hasValidLoop());
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (dialog);
        options.dialogTitle = "Export Stems";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        dialog->onCancel = [window] { window->setVisible (false); };
        dialog->onExport = [this, window] (const ui::StemExportDialog::Request& request)
        {
            window->setVisible (false);
            const auto base = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Bounces");
            base.createDirectory();
            fileChooser = std::make_unique<juce::FileChooser> ("Stems Folder", base);
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, request] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) exportStems (request, fc.getResult().getChildFile ("Stems").getNonexistentSibling (false));
            });
        };
    }

    //==========================================================================
    // Lua scripting (ScriptHost)

    static juce::File scriptsFolder() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Scripts"); }

    // Adapter so the Lua engine sees a ScriptHost without the member/method name clash on `session`.
    struct HostAdapter final : scripting::ScriptHost
    {
        explicit HostAdapter (MainComponent& m) : main (m) {}
        model::Session& session() override { return main.session; }
        engine::Transport& transport() override { return main.engine.getTransport(); }
        double sampleRate() const override { return main.engine.getSampleRate(); }
        void setTempo (double bpm) override { main.setTempo (bpm, main.session.getBeatsPerBar()); }
        int addTrack (const juce::String& kind, const juce::String& name) override { return main.scriptAddTrack (kind, name); }
        void status (const juce::String& m) override { main.statusMessage = m; main.updateStatus(); }
        void log (const juce::String& m) override { main.scriptLog (m); }
        bool runCommand (const juce::String& id) override { return main.commands.run (id); }
        juce::String bounce (const juce::File& f) override { const auto msg = main.bounceArrangementToFile (f); return msg.startsWith ("Bounced") ? juce::String() : msg; }
        bool saveSession (const juce::File& f) override { return main.writeSession (f, f.hasFileExtension ("bmkt")); }
        bool openSession (const juce::File& f) override { return main.openSessionBundle (f); }
        juce::var getPreference (const juce::String& id) override { return main.prefs.get (id); }
        void setPreference (const juce::String& id, const juce::var& v) override { main.prefs.set (id, v); }
        void registerCommand (const juce::String& id, const juce::String& name, std::function<void()> run) override { main.scriptRegisterCommand (id, name, std::move (run)); }
        MainComponent& main;
    };

    int scriptAddTrack (const juce::String& kind, const juce::String& name)
    {
        const auto k = kind.toLowerCase().removeCharacters (" -_");
        int index = -1;
        if (k == "audio")            index = addTrack (name.isNotEmpty() ? name : "Audio " + juce::String (countTracks (model::Track::Type::audio) + 1));
        else if (k == "drums" || k == "drummachine") { addDrumMachineTrack(); index = trackArea.getSelectedTrack(); }
        else if (k == "aux")         { addAuxTrack(); index = session.getNumTracks() - 1; }
        else if (k == "vca")         { addVcaTrack(); index = session.getNumTracks() - 1; }
        else
        {
            for (auto type : engine::Instrument::availableTypes())
                if (juce::String (engine::Instrument::typeName (type)).removeCharacters (" ").equalsIgnoreCase (k) || (k == "synth" && type == engine::InstrumentType::subtractive))
                { addInstrumentTrack (type); index = trackArea.getSelectedTrack(); break; }
        }
        if (index >= 0 && name.isNotEmpty() && session.getTracks()[(size_t) index].name != name)
            session.execute (std::make_unique<model::RenameTrackCommand> (index, name));
        return index;
    }
    void scriptLog (const juce::String& m) { juce::Logger::writeToLog ("[lua] " + m); if (scriptConsoleContent != nullptr) scriptConsoleContent->append (m); }
    void scriptRegisterCommand (const juce::String& id, const juce::String& name, std::function<void()> run)
    {
        commands.add ({ id, name, "Script", {}, 0, [this, run, id] { run(); if (scriptConsoleContent != nullptr) scriptConsoleContent->append ("-- ran " + id); }, nullptr });
    }

    // Every .lua in the Scripts folder becomes a "Script: <name>" command (palette / File menu).
    void registerScripts()
    {
        scriptsFolder().createDirectory();
        scriptCommands.clear();
        for (const auto& f : scriptsFolder().findChildFiles (juce::File::findFiles, false, "*.lua"))
        {
            const auto id = "script.file." + f.getFileNameWithoutExtension();
            scriptCommands.add (id);
            if (commands.find (id) == nullptr)
                commands.add ({ id, f.getFileNameWithoutExtension(), "Script", {}, 0, [this, f] { runScriptFile (f); }, nullptr });
        }
    }

    void runScriptFile (const juce::File& f)
    {
        const auto r = lua.runFile (f);
        if (scriptConsoleContent != nullptr) { if (r.output.isNotEmpty()) scriptConsoleContent->append (r.output.trimEnd()); scriptConsoleContent->append (r.ok ? "-- ran " + f.getFileName() : "-- error: " + r.error); }
        statusMessage = r.ok ? "Ran script " + f.getFileName() : "Script error: " + r.error;
        updateStatus();
    }

    void showScriptConsole()
    {
        if (scriptWindow != nullptr) { scriptWindow->setVisible (true); scriptWindow->toFront (true); return; }
        auto* content = new ui::ScriptConsole (lua);
        scriptConsoleContent = content;
        content->onSaveScript = [this] (const juce::String& code)
        {
            auto* window = new juce::AlertWindow ("Save Script", "Script name:", juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", "My Script");
            window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, code] (int result)
            {
                const auto name = window->getTextEditorContents ("name").trim();
                if (result == 1 && name.isNotEmpty())
                {
                    scriptsFolder().createDirectory();
                    scriptsFolder().getChildFile (juce::File::createLegalFileName (name) + ".lua").replaceWithText (code);
                    registerScripts();
                    statusMessage = "Saved script " + name + " (run it from the command palette)"; updateStatus();
                }
            }), true);
        };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Script Console";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        scriptWindow = options.launchAsync();
    }

    void showMidiEventList()
    {
        if (eventListWindow != nullptr) { eventListWindow->setVisible (true); eventListWindow->toFront (true); if (eventListContent) eventListContent->setTrack (trackArea.getSelectedTrack()); return; }
        auto* content = new ui::MidiEventList (session, engine.getTransport());
        eventListContent = content;
        content->setTrack (trackArea.getSelectedTrack());
        content->onStatus = [this] { statusMessage = "Insert Note: the track needs a MIDI clip first"; updateStatus(); };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "MIDI Event List";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        eventListWindow = options.launchAsync();
    }

    void loadSyncSettings()
    {
        if (appSettings == nullptr) return;
        midiSync.setFrameRate ((engine::MtcFrameRate) juce::jlimit (0, 3, appSettings->getIntValue ("sync.frameRate", 3)));
        midiSync.setStartOffsetSeconds (appSettings->getDoubleValue ("sync.startSeconds", 0.0));
        if (const auto out = appSettings->getValue ("sync.output"); out.isNotEmpty()) midiSync.setOutputDevice (out);
        if (const auto in = appSettings->getValue ("sync.input"); in.isNotEmpty()) midiSync.setInputDevice (in);
        midiSync.setMode ((engine::MidiSyncController::Mode) juce::jlimit (0, 4, appSettings->getIntValue ("sync.mode", 0)));
    }

    void saveSyncSettings()
    {
        if (appSettings == nullptr) return;
        appSettings->setValue ("sync.mode", (int) midiSync.getMode());
        appSettings->setValue ("sync.frameRate", (int) midiSync.getFrameRate());
        appSettings->setValue ("sync.startSeconds", midiSync.getStartOffsetSeconds());
        appSettings->setValue ("sync.output", midiSync.getOutputDeviceName());
        appSettings->setValue ("sync.input", midiSync.getInputDeviceName());
        appSettings->saveIfNeeded();
    }

    void showSyncDialog()
    {
        if (syncWindow != nullptr) { syncWindow->setVisible (true); syncWindow->toFront (true); return; }
        auto* content = new ui::SyncDialog (midiSync);
        content->onChanged = [this] { saveSyncSettings(); statusMessage = midiSync.getStatus(); updateStatus(); };
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (content);
        options.dialogTitle = "Synchronization";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        syncWindow = options.launchAsync();
    }

    // Instrument Track > Other...: the chooser, with every bundled instrument by category
    void showInstrumentChooser (engine::InstrumentType preselect = engine::InstrumentType::none)
    {
        auto* chooser = new ui::InstrumentChooser (&engine.getGraph());
        if (preselect != engine::InstrumentType::none) chooser->selectType (preselect);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (chooser);
        options.dialogTitle = "Add Instrument Track";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        instrumentChooser = chooser;
        chooser->onCancel = [window] { window->setVisible (false); };
        chooser->onAdd = [this, window] (engine::InstrumentType type, const juce::String& preset)
        {
            window->setVisible (false);
            addInstrumentTrack (type, preset);
            grabKeyboardFocus();
        };
        chooser->onBuild = [this] (engine::InstrumentType type, const juce::String& preset) { showPresetBuilder (type, preset, nullptr); };
        chooser->onRemove = [this] (engine::InstrumentType type, const juce::String& preset)
        {
            if (! engine::Instrument::isUserPreset (type, preset)) return;
            persistence::UserPresets::remove (persistence::UserPresets::defaultFolder(), type, preset);
            userPresetsChanged();
            statusMessage = "Removed preset " + preset + " (tracks using it keep their sound)"; updateStatus();
        };
    }

    void userPresetsChanged()
    {
        engine::Instrument::setUserPresets (persistence::UserPresets::load (persistence::UserPresets::defaultFolder()));
        if (auto* c = instrumentChooser.getComponent()) c->refreshPresets();
        pianoRoll.refreshPresetBox();
    }

    // Build Your Own preset: an instrument's knobs, a phrase to hear it, saved to ~/Music/Beat Maker/Presets
    void showPresetBuilder (engine::InstrumentType type, const juce::String& startFrom, const engine::InstrumentParams* initial)
    {
        auto* builder = new ui::PresetBuilder (&engine.getGraph(), type, startFrom, initial);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (builder);
        options.dialogTitle = engine::Instrument::isUserPreset (type, startFrom) ? "Edit Preset" : "Build Your Own Preset";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        builder->onCancel = [window] { window->setVisible (false); };
        builder->onSave = [this, window] (engine::InstrumentType t, const engine::InstrumentParams& p, bool addTrack)
        {
            if (const auto error = persistence::UserPresets::save (persistence::UserPresets::defaultFolder(), t, p); error.isNotEmpty()) { statusMessage = error; updateStatus(); return; }
            window->setVisible (false);
            userPresetsChanged();
            statusMessage = "Saved preset " + p.presetName + " to " + persistence::UserPresets::defaultFolder().getFullPathName(); updateStatus();
            if (addTrack) addInstrumentTrack (t, p.presetName);
            else
                for (int i = 0; i < session.getNumTracks(); ++i)   // tracks on this preset take the new sound
                    if (const auto* tr = session.getTrack (i); tr != nullptr && tr->hasInstrument() && tr->instrumentType() == t && tr->instrumentParams->presetName == p.presetName)
                    {
                        auto np = std::make_shared<engine::InstrumentParams> (p);
                        np->sample = tr->instrumentParams->sample; np->sampleRate = tr->instrumentParams->sampleRate; np->rootNote = tr->instrumentParams->rootNote; np->sampleName = tr->instrumentParams->sampleName;
                        session.execute (std::make_unique<model::SetInstrumentParamsCommand> (i, std::move (np), "Update Preset"));
                    }
            grabKeyboardFocus();
        };
    }

    // "Save..." next to the Sound menu: the track's sound, as it is, under a name of the user's own
    void savePresetFromTrack (int track)
    {
        auto* t = session.getTrack (track);
        if (t == nullptr || ! t->hasInstrument()) return;
        auto* w = new juce::AlertWindow ("Save Sound as Preset", "The knobs of " + t->name + " as they are now, under a name of your own. It lists under My Presets in every Sound menu.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", engine::Instrument::isUserPreset (t->instrumentType(), t->instrumentParams->presetName) ? t->instrumentParams->presetName : t->instrumentParams->presetName + " 2", "Name");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, track] (int result)
        {
            if (result != 1) return;
            auto* tr = session.getTrack (track);
            if (tr == nullptr || ! tr->hasInstrument()) return;
            const auto type = tr->instrumentType();
            auto p = *tr->instrumentParams;
            p.presetName = w->getTextEditorContents ("name").trim();
            for (const auto& b : engine::Instrument::bundledPresets (type)) if (b.presetName == p.presetName) { statusMessage = "That is a bundled preset's name; pick another"; updateStatus(); return; }
            if (const auto error = persistence::UserPresets::save (persistence::UserPresets::defaultFolder(), type, p); error.isNotEmpty()) { statusMessage = error; updateStatus(); return; }
            userPresetsChanged();
            session.execute (std::make_unique<model::SetInstrumentParamsCommand> (track, std::make_shared<engine::InstrumentParams> (p), "Save Preset"));   // the track now shows the name
            statusMessage = "Saved preset " + p.presetName; updateStatus();
            grabKeyboardFocus();
        }), true);
    }

    // Drum Machine Track > Other...: the kit chooser
    void showDrumKitChooser (const juce::String& preselect = {})
    {
        auto* chooser = new ui::DrumKitChooser (&engine.getGraph(), engine.getSampleRate());
        if (preselect.isNotEmpty()) chooser->selectKit (preselect);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (chooser);
        options.dialogTitle = "Add Drum Machine Track";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        kitChooser = chooser;
        chooser->onCancel = [window] { window->setVisible (false); };
        chooser->onAdd = [this, window] (const juce::String& kit)
        {
            window->setVisible (false);
            addDrumMachineTrack (kit);
            grabKeyboardFocus();
        };
        chooser->onBuild = [this] (const juce::String& startFrom) { showKitBuilder (startFrom); };
        chooser->onRemove = [this] (const juce::String& name)
        {
            if (engine::DrumKitFactory::customKit (name) == nullptr) return;
            persistence::UserKits::remove (persistence::UserKits::defaultFolder(), name);
            userKitsChanged();
            statusMessage = "Removed kit " + name + " (tracks using it keep their sounds)"; updateStatus();
        };
    }

    // The user's kits changed on disk: re-register them, forget cached renders, refresh what shows them
    void userKitsChanged()
    {
        engine::DrumKitFactory::setCustomKits (persistence::UserKits::load (persistence::UserKits::defaultFolder()));
        for (auto it = kitCache.begin(); it != kitCache.end();)
        {
            const auto* info = engine::DrumKitFactory::info (it->first);
            if (info == nullptr || info->custom) it = kitCache.erase (it); else ++it;
        }
        if (auto* c = kitChooser.getComponent()) c->refreshKits();
    }

    // Build Your Own...: a kit from the pads of the other kits, saved to ~/Music/Beat Maker/Kits
    void showKitBuilder (const juce::String& startFrom)
    {
        auto* builder = new ui::KitBuilder (&engine.getGraph(), engine.getSampleRate(), startFrom);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (builder);
        options.dialogTitle = engine::DrumKitFactory::customKit (startFrom) != nullptr ? "Edit Kit" : "Build Your Own Kit";
        options.dialogBackgroundColour = ui::theme::panel;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        auto* window = options.launchAsync();
        builder->onCancel = [window] { window->setVisible (false); };
        builder->onSave = [this, window] (const engine::DrumKitFactory::CustomKit& def, bool addTrack)
        {
            if (const auto error = persistence::UserKits::save (persistence::UserKits::defaultFolder(), def); error.isNotEmpty()) { statusMessage = error; updateStatus(); return; }
            window->setVisible (false);
            userKitsChanged();
            statusMessage = "Saved kit " + def.name + " to " + persistence::UserKits::defaultFolder().getFullPathName(); updateStatus();
            if (addTrack) addDrumMachineTrack (def.name);
            // Tracks already on this kit pick up the new pads
            for (int i = 0; i < session.getNumTracks(); ++i)
                if (const auto* t = session.getTrack (i); t != nullptr && t->drumKit != nullptr && t->drumKit->name == def.name && ! addTrack)
                {
                    auto kit = std::make_shared<engine::DrumKit> (*kitNamed (def.name));
                    for (size_t pad = 0; pad < kit->pads.size(); ++pad) kit->pads[pad].gain = t->drumKit->pads[pad].gain;
                    session.execute (std::make_unique<model::ReplaceDrumKitCommand> (i, std::move (kit), "Update Kit"));
                }
            grabKeyboardFocus();
        };
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
        menu.addItem (13, "Rename Session...");
        menu.addItem (14, "Delete Session...");
        menu.addItem (5, "Save As Template...");
        menu.addSeparator();
        menu.addItem (6, "Import Audio Files...");
        menu.addItem (8, "Import Session Data...  (Shift+Alt+I)");
        menu.addItem (9, "Export Stems...  (Ctrl+Alt+B)");
        menu.addItem (12, "Export AAF...  (Ctrl+Alt+A)");
        menu.addItem (7, "Memory Locations...  (Ctrl+5)");
        juce::PopupMenu samples;
        int sid = 100;
        for (const auto& sample : persistence::SampleProjects::list()) samples.addItem (sid++, sample.name);
        menu.addSubMenu ("Open Sample Project", samples);
        menu.addSeparator();
        menu.addItem (10, "Welcome / Tour / Tutorials...");
        menu.addItem (11, "Keyboard Shortcuts  (Ctrl+/)");
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
                case 8: chooseSessionToImport(); break;
                case 9: showStemExport(); break;
                case 12: showAafExport(); break;
                case 13: showRenameSession(); break;
                case 14: showDeleteSession(); break;
                case 10: showWelcome(); break;
                case 11: showShortcuts(); break;
                default:
                    if (r >= 100) { const auto list = persistence::SampleProjects::list(); if (r - 100 < (int) list.size()) openSampleProject (list[(size_t) (r - 100)].name); }
                    break;
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
        ui::UiProfiler::Scope scope ("autosave");
        if (sessionFile == juce::File() || ! session.getHistory().canUndo()) return;
        const auto backups = sessionFile.getChildFile ("Session File Backups");
        backups.createDirectory();
        const auto target = backups.getChildFile ("autosave-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".bmkt");
        persistence::SessionFile::save (session, transportState(), target);
        // keep the five newest
        auto old = backups.findChildFiles (juce::File::findDirectories, false, "autosave-*.bmkt");
        old.sort();
        while (old.size() > juce::jmax (1, prefs.getInt ("operation.autosaveCount"))) { old.getReference (0).deleteRecursively(); old.remove (0); }
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
        snapshot->panDepthDb = panDepthDb();
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

    // The session's tempo is the source of truth; the transport follows it here
    // (undo and redo included). When clips follow bars, the cycle range and the
    // playhead keep their bar positions too.
    void mirrorTempo()
    {
        auto& tr = engine.getTransport();
        const double sb = session.getBpm();
        if (std::abs (sb - mirroredBpm) > 1e-9)
        {
            if (mirroredBpm > 0.0 && prefs.getBool ("editing.clipsFollowTempo"))
            {
                const double f = mirroredBpm / sb;
                tr.setLoopRange ((juce::int64) std::llround ((double) tr.getLoopStart() * f), (juce::int64) std::llround ((double) tr.getLoopEnd() * f));
                if (! tr.isPlaying()) tr.setPositionSamples ((juce::int64) std::llround ((double) tr.getPositionSamples() * f));
            }
            mirroredBpm = sb;
        }
        tr.setBpm (sb);
        tr.setBeatsPerBar (session.getBeatsPerBar());
    }

    // One undo step for the tempo (clips follow bars per preference), then the
    // loops with a known source tempo are re-conformed as a second step.
    void setTempo (double bpm, int beatsPerBar, bool reconformLoops = true)
    {
        bpm = juce::jlimit (20.0, 400.0, bpm);
        if (std::abs (bpm - session.getBpm()) < 1e-9 && beatsPerBar == session.getBeatsPerBar()) { engine.getTransport().setBpm (bpm); return; }
        const double oldBpm = session.getBpm();
        session.execute (std::make_unique<model::SetTempoCommand> (bpm, beatsPerBar, prefs.getBool ("editing.clipsFollowTempo")));
        statusMessage = "Tempo " + juce::String (bpm, 1) + " BPM, " + juce::String (beatsPerBar) + "/4";
        updateStatus();
        if (reconformLoops && std::abs (bpm - oldBpm) > 1e-9 && prefs.getBool ("editing.loopsFollowTempo")) reconformLoopsToTempo (bpm);
    }

    // Every audio clip with a source tempo gets an Elastic conform to `bpm`; all
    // renders become one "Conform Loops" undo step (background job when long).
    void reconformLoopsToTempo (double bpm)
    {
        std::vector<std::unique_ptr<model::SetClipElasticCommand>> cmds;
        double totalSeconds = 0.0;
        for (int t = 0; t < session.getNumTracks(); ++t)
        {
            const auto& track = session.getTracks()[(size_t) t];
            for (int c = 0; c < (int) track.clips.size(); ++c)
            {
                const auto& clip = track.clips[(size_t) c];
                if (clip.sourceBpm <= 0.0 || clip.audio == nullptr) continue;
                engine::StretchMode mode = clip.isElastic() ? clip.elastic.mode : engine::StretchMode::polyphonic;
                if (! clip.isElastic() && clip.sourceFile.existsAsFile())
                    if (persistence::LoopLibrary::analyse (clip.sourceFile, loader.getFormatManager()).category == persistence::LoopInfo::Category::drums) mode = engine::StretchMode::rhythmic;
                auto cmd = std::make_unique<model::SetClipElasticCommand> (session, model::ClipRef { t, model::ClipRef::Kind::audio, c }, model::Elastic::forTempo (clip, bpm, mode), "Conform to Tempo", true);
                if (cmd->wasCancelled()) continue;
                totalSeconds += cmd->getSampleRate() > 0.0 ? (double) cmd->getSourceLength() / cmd->getSampleRate() : 0.0;
                cmds.push_back (std::move (cmd));
            }
        }
        if (cmds.empty()) return;
        juce::ignoreUnused (totalSeconds);
        ui::ElasticJob::runMany (session, std::move (cmds), "Conforming loops to the new tempo...", "Conform Loops to Tempo", [this] (int n)
        {
            statusMessage = "Conformed " + juce::String (n) + " loop(s) to " + juce::String (session.getBpm(), 1) + " BPM"; updateStatus();
        });
    }

    void showTempoDialog()
    {
        auto* w = new juce::AlertWindow ("Set Tempo", "Tempo in beats per minute (20 to 400) and beats per bar. Clips, memory locations and automation keep their bar positions, and library loops are re-conformed, unless switched off in Preferences > Editing.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("bpm", juce::String (session.getBpm(), 1), "BPM");
        juce::StringArray sigs; for (int n = 2; n <= 7; ++n) sigs.add (juce::String (n) + "/4");
        w->addComboBox ("sig", sigs, "Time signature");
        w->getComboBoxComponent ("sig")->setSelectedId (juce::jlimit (1, 6, session.getBeatsPerBar() - 1));
        w->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1)
            {
                const double bpm = w->getTextEditorContents ("bpm").retainCharacters ("0123456789.").getDoubleValue();
                if (bpm >= 20.0 && bpm <= 400.0) setTempo (bpm, w->getComboBoxComponent ("sig")->getSelectedId() + 1);
                else { statusMessage = "Tempo must be between 20 and 400 BPM"; updateStatus(); }
            }
        }), true);
    }

    // Tap Tempo: the average interval of the recent taps; the tempo is applied
    // (with loop conforming) once the taps stop.
    void tapTempo()
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (! tapTimes.empty() && now - tapTimes.back() > 2.0) tapTimes.clear();
        tapTimes.push_back (now);
        if (tapTimes.size() > 8) tapTimes.erase (tapTimes.begin());
        if (tapTimes.size() < 2) { statusMessage = "Tap Tempo: keep tapping (Ctrl+Alt+T)"; updateStatus(); return; }
        const double bpm = juce::jlimit (20.0, 400.0, 60.0 * (double) (tapTimes.size() - 1) / (tapTimes.back() - tapTimes.front()));
        const double rounded = std::round (bpm * 10.0) / 10.0;
        engine.getTransport().setBpm (rounded);   // live preview
        statusMessage = "Tap Tempo: " + juce::String (rounded, 1) + " BPM (" + juce::String ((int) tapTimes.size()) + " taps)"; updateStatus();
        const int generation = ++tapGeneration;
        juce::Timer::callAfterDelay (1500, [this, generation, rounded] { if (generation == tapGeneration) setTempo (rounded, session.getBeatsPerBar()); });
    }

    void sessionChanged (model::Session&) override
    {
        ui::UiProfiler::Scope scope ("sessionChanged");
        mirrorTempo();
        if (session.getHistory().canUndo()) ui::CrashReporter::get().addBreadcrumb ("edit " + session.getHistory().getUndoName());
        { ui::UiProfiler::Scope s ("pushSnapshot"); pushSnapshot(); }
        updateLoopRange();
        { ui::UiProfiler::Scope s ("updateSequencerTarget"); updateSequencerTarget(); }
        { ui::UiProfiler::Scope s ("updateStatus"); updateStatus(); }
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
        if (statusMessage.isNotEmpty() && statusMessage != lastAnnounced && prefs.getBool ("display.announceStatus"))
        {
            lastAnnounced = statusMessage;
            juce::AccessibilityHandler::postAnnouncement (statusMessage, juce::AccessibilityHandler::AnnouncementPriority::low);
        }
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
    // The bar between the tracks and the editor panel: drag it to change the panel's height.
    struct EditorResizer final : juce::Component
    {
        std::function<void (int deltaY, int startHeight)> onDrag;
        std::function<int()> currentHeight;
        int startHeight = 0;
        EditorResizer() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); setTitle ("Editor panel height"); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (ui::theme::panelDark);
            g.setColour (isMouseOverOrDragging() ? ui::theme::accent : ui::theme::gridStrong);
            g.fillRoundedRectangle (getLocalBounds().withSizeKeepingCentre (48, 3).toFloat(), 1.5f);
        }
        void mouseDown (const juce::MouseEvent&) override { startHeight = currentHeight ? currentHeight() : 300; }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY(), startHeight); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
    } editorResizer;
    ui::SmartControls smartControls { session };
    ui::MixerView mixerView { session, engine.getGraph(), [this] { return engine.getSampleRate(); } };
    model::AutomationRecorder automation { session, engine.getTransport() };
    bool controlsVisible = true;
    bool mixerVisible = false;
    bool metronomeOn = false;
    persistence::LoopLibrary loopLibrary { loader.getFormatManager() };
    ui::LoopBrowser loopBrowser { loopLibrary };
    struct LibraryWindow final : public juce::DocumentWindow
    {
        LibraryWindow (juce::Component& content, std::function<void()> onClose)
            : juce::DocumentWindow ("Sample Library", ui::theme::panel, juce::DocumentWindow::closeButton), close (std::move (onClose))
        {
            setUsingNativeTitleBar (false);   // our own title bar: a tiling compositor draws no decorations, so the close button must be ours
            setContentNonOwned (&content, false);
            setResizable (true, false);
            setResizeLimits (260, 320, 1000, 1600);
            centreWithSize (340, 560);
        }
        void closeButtonPressed() override { if (close) close(); }
        std::function<void()> close;
    };
    std::unique_ptr<LibraryWindow> libraryWindow;   // after loopBrowser: it shows the browser and must go first
    std::unique_ptr<juce::PropertiesFile> appSettings;
    std::shared_ptr<const juce::AudioBuffer<float>> previewAudio;
    std::shared_ptr<const engine::DrumKit> defaultKit;
    std::map<juce::String, std::shared_ptr<const engine::DrumKit>> kitCache;
    juce::Component::SafePointer<ui::DrumKitChooser> kitChooser;
    juce::Component::SafePointer<ui::InstrumentChooser> instrumentChooser;
    bool editorVisible = true;
    bool loopRecording = false;
    juce::int64 loopRecordStart = 0, loopRecordEnd = 0;
    juce::int64 autoStopSample = -1;   // post-roll end while punch recording
    juce::File sessionFile;            // the open .bmk bundle (empty = Untitled)
    juce::String untitledName = "Untitled";
    int savedHistorySize = 0, autosaveCounter = 0;
    juce::String lastDeviceInfo;
    double mirroredBpm = 0.0;
    juce::Component::SafePointer<juce::Component> lastFocused;
    ui::UiProfiler::StallDetector stallDetector;
    juce::String debugFocusName;
    std::vector<double> tapTimes; int tapGeneration = 0;
    juce::Component::SafePointer<juce::DialogWindow> memoryWindow, preferencesWindow, paletteWindow;
    juce::Component::SafePointer<ui::PreferencesWindow> preferencesContent;
    juce::Component::SafePointer<juce::DialogWindow> eventListWindow, syncWindow;
    juce::Component::SafePointer<ui::MidiEventList> eventListContent;
    engine::MidiSyncController midiSync { engine.getTransport() };
    HostAdapter scriptHost { *this };
    scripting::LuaEngine lua { scriptHost };
    juce::Component::SafePointer<juce::DialogWindow> scriptWindow;
    std::unique_ptr<StemJob> stemJob;
    std::unique_ptr<model::Session> importSource;
    juce::TooltipWindow tooltips { nullptr, 500 };
    juce::LookAndFeel_V4 lookAndFeel;
    juce::String lastAnnounced;
    ui::TourOverlay tour;
    juce::Component::SafePointer<juce::DialogWindow> welcomeWindow, tutorialsWindow, shortcutsWindow, usageWindow;
    bool playedOnce = false, bouncedOnce = false, mixedOnce = false, paletteOpenedOnce = false;
    juce::Component::SafePointer<ui::ScriptConsole> scriptConsoleContent;
    juce::StringArray scriptCommands;
    ui::Preferences prefs { preferencesFile() };
    ui::CommandRegistry commands;
    int prefsListener = -1;
    bool wasPlayingLastTick = false;
    juce::int64 playStartSample = 0;
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
    const juce::String getApplicationVersion() override { return BEATMAKER_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        if (getCommandLineParameterArray().contains ("--version"))
        {
            std::cout << "Beat Maker " << BEATMAKER_VERSION_STRING << " (JUCE " << JUCE_MAJOR_VERSION << "." << JUCE_MINOR_VERSION << "." << JUCE_BUILDNUMBER << ")" << std::endl;
            quit();
            return;
        }
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

        {
            auto& reporter = ui::CrashReporter::get();
            reporter.install (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Beat Maker"), getApplicationVersion());
            reporter.addBreadcrumb ("launch " + getCommandLineParameterArray().joinIntoString (" ").substring (0, 120));
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
            else if (arg.startsWith ("--drums=")) main.addDrumMachineTrackFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--kit-chooser") juce::Timer::callAfterDelay (300, [&main] { main.showDrumKitChooserFromCommandLine(); });
            else if (arg == "--kit-builder" || arg.startsWith ("--kit-builder=")) juce::Timer::callAfterDelay (300, [&main, arg] { main.showKitBuilderFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false)); });
            else if (arg == "--preset-builder" || arg.startsWith ("--preset-builder=")) juce::Timer::callAfterDelay (300, [&main, arg] { main.showPresetBuilderFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false)); });
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
            else if (arg.startsWith ("--track-colour=")) main.setTrackColourFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--add-effect=")) main.addTrackEffectFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--import=")) main.importToTrackFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--send=")) main.setSendFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--pan=")) main.setPanFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--rename=")) main.renameSessionFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--delete-session=")) main.deleteSessionFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--save=")) main.saveSessionFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg.startsWith ("--template=")) main.templateFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--marker-demo") main.markerDemoFromCommandLine();
            else if (arg == "--prefs-window") main.showPreferencesFromCommandLine();
            else if (arg == "--event-list") main.showMidiEventListFromCommandLine();
            else if (arg.startsWith ("--script=")) main.runScriptFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg.startsWith ("--lua=")) main.runLuaFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--script-console") main.showScriptConsoleFromCommandLine();
            else if (arg.startsWith ("--freeze=")) main.freezeFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false).getIntValue());
            else if (arg.startsWith ("--import-session=")) main.importSessionFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--welcome") main.showWelcomeFromCommandLine();
            else if (arg == "--instrument-chooser") juce::Timer::callAfterDelay (300, [&main] { main.showInstrumentChooserFromCommandLine(); });
            else if (arg == "--tour") main.startTourFromCommandLine();
            else if (arg == "--tutorials") main.showTutorialsFromCommandLine();
            else if (arg == "--shortcuts") main.showShortcutsFromCommandLine();
            else if (arg == "--system-usage") main.showSystemUsageFromCommandLine();
            else if (arg.startsWith ("--sample-project=")) main.openSampleFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--import-dialog=")) main.showImportDialogFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg.startsWith ("--unfreeze=")) main.unfreezeFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false).getIntValue());
            else if (arg.startsWith ("--commit=")) main.commitFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false).getIntValue());
            else if (arg.startsWith ("--export-aaf=")) main.exportAafFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--aaf-dialog") main.showAafExportFromCommandLine();
            else if (arg == "--open-dialog" || arg.startsWith ("--open-dialog=")) juce::Timer::callAfterDelay (400, [&main, arg] { main.showOpenDialogFromCommandLine (arg.contains ("=") ? juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)) : juce::File()); });
            else if (arg == "--crash") { ui::CrashReporter::get().addBreadcrumb ("deliberate crash (--crash)"); ui::CrashReporter::crashNow(); }
            else if (arg == "--crash-dialog") main.showPendingCrashReportFromCommandLine();
            else if (arg == "--diagnostics") main.reportProblemFromCommandLine();
            else if (arg.startsWith ("--lua-later=")) juce::Timer::callAfterDelay (1500, [&main, code = arg.fromFirstOccurrenceOf ("=", false, false)] { main.runLuaFromCommandLine (code); });
            else if (arg.startsWith ("--click-button=")) juce::Timer::callAfterDelay (600, [&main, text = arg.fromFirstOccurrenceOf ("=", false, false)] { main.clickButtonFromCommandLine (text); });
            else if (arg.startsWith ("--scroll-tracks=")) main.scrollTracksFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false).getIntValue());
            else if (arg == "--notes-focus") juce::Timer::callAfterDelay (400, [&main] { main.focusNotesFromCommandLine(); });   // after the window takes focus
            else if (arg.startsWith ("--tempo=")) main.setTempoFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue());
            else if (arg.startsWith ("--dump-docs=")) main.dumpDocsFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg.startsWith ("--stems=")) main.stemsFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false)));
            else if (arg == "--sync-window") main.showSyncDialogFromCommandLine();
            else if (arg.startsWith ("--sync=")) main.setSyncFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--palette")) main.showPaletteFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--pref="))
            {
                const auto spec = arg.fromFirstOccurrenceOf ("=", false, false);
                main.setPreferenceFromCommandLine (spec.upToFirstOccurrenceOf ("=", false, false), spec.fromFirstOccurrenceOf ("=", false, false));
            }
            else if (arg == "--focus") main.setFocusModeFromCommandLine (true);
            else if (arg.startsWith ("--run="))   main.runCommandFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg.startsWith ("--stop-at="))    // seconds after launch: stop the transport (smoke tests)
                juce::Timer::callAfterDelay (juce::roundToInt (arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue() * 1000.0), [&main] { main.stopTransportFromCommandLine(); });
            else if (arg.startsWith ("--punch-at="))   // seconds after launch: toggle punch (QuickPunch/TrackPunch smoke tests)
                juce::Timer::callAfterDelay (juce::roundToInt (arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue() * 1000.0), [&main] { main.punchFromCommandLine(); });
            else if (arg == "--cycle")  main.setCycleEnabled (true);
            else if (arg == "--metronome") main.setMetronomeFromCommandLine (true);
            else if (arg == "--play")   play = true;
            else if (arg.startsWith ("--bounce=")) bounceFile = juce::File::getCurrentWorkingDirectory()
                                                                    .getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));
            else if (arg == "--bounce-dialog") main.showBounceDialog();
            else if (arg == "--mixer")  main.setMixerVisibleFromCommandLine (true);
            else if (arg == "--library") main.showLibraryWindowFromCommandLine();
            else if (arg.startsWith ("--sends-panel=")) main.openSendsPanelFromCommandLine (arg.fromFirstOccurrenceOf ("=", false, false).getIntValue() - 1);
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

        // First launch without arguments: the Welcome window.
        if (getCommandLineParameterArray().isEmpty() && main.wantsWelcomeAtStartup())
            juce::Timer::callAfterDelay (300, [&main] { main.showWelcomeFromCommandLine(); });
        // A crash last time: the report and recovery offer (unless a flag already opened it).
        if (! getCommandLineParameterArray().contains ("--crash-dialog") && ui::CrashReporter::get().isEnabled())
            juce::Timer::callAfterDelay (400, [&main] { main.showPendingCrashReportFromCommandLine(); });

        // --profile-ui[=seconds]: print where the message thread's time went, then quit.
        for (const auto& arg : getCommandLineParameterArray())
            if (arg.startsWith ("--profile-ui"))
            {
                const double seconds = arg.contains ("=") ? arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue() : 5.0;
                juce::Timer::callAfterDelay (juce::roundToInt (seconds * 1000.0), [&main, this] { main.printUiProfile(); quit(); });
            }
        // --quit: done with the command line (stems, saves, scripts) - exit.
        if (getCommandLineParameterArray().contains ("--quit")) { quit(); return; }

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

    void shutdown() override { quitPoller.stopTimer(); ui::SessionChooser::closeAny(); mainWindow.reset(); ui::CrashReporter::get().shutdownLogging(); }
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
            if (isActiveWindow() && (getCurrentlyFocusedComponent() == nullptr || getCurrentlyFocusedComponent() == this))
                getMainComponent().restoreFocus();   // back to the editor that had it, not the window
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace beatmaker

START_JUCE_APPLICATION (beatmaker::BeatMakerApplication)
