// Session: the document. Tracks contain clips; clips reference immutable
// audio buffers or step patterns. Non-destructive by design: source audio is
// never modified, and shared engine data is replaced (copy-on-write), never
// edited in place.
#pragma once

#include "Command.h"
#include "IOSetup.h"

#include <automation/Automation.h>
#include <dsp/DrumKit.h>
#include <dsp/Effects.h>
#include <dsp/Fades.h>
#include <dsp/Instrument.h>
#include <dsp/TimeStretch.h>
#include <sequencer/MidiSequence.h>
#include <sequencer/StepPattern.h>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <memory>
#include <vector>

namespace beatmaker::model
{

struct AudioClip
{
    juce::String name;
    juce::File sourceFile;
    std::shared_ptr<const juce::AudioBuffer<float>> audio; // at engine sample rate
    double sampleRate = 44100.0;     // rate of `audio`
    juce::int64 timelineStart = 0;   // samples at `sampleRate`
    juce::int64 sourceOffset  = 0;
    juce::int64 length        = 0;
    float gain = 1.0f;               // clip gain, linear (shown in dB)

    juce::int64 fadeIn = 0, fadeOut = 0;   // samples
    engine::FadeShape fadeInShape = engine::FadeShape::linear, fadeOutShape = engine::FadeShape::linear;

    // Clip gain line: breakpoint times are source-sample positions, so the
    // line stays glued to the audio through trims, splits and moves.
    std::shared_ptr<const engine::AutomationLane> gainLane;
    bool audioModified = false;   // pencil edits: `audio` no longer matches sourceFile

    // Elastic (time stretch / pitch shift). `audio` is the rendered result;
    // `sourceAudio` the unstretched original (null while elastic is off, in
    // which case `audio` is the original). Offsets, lengths, fades and the
    // gain line are all in the rendered domain.
    engine::StretchSpec elastic;
    std::shared_ptr<const juce::AudioBuffer<float>> sourceAudio;
    double sourceBpm = 0.0;       // tempo the audio was recorded/analysed at (0 = unknown)

    const juce::AudioBuffer<float>* originalAudio() const noexcept { return sourceAudio != nullptr ? sourceAudio.get() : audio.get(); }
    bool isElastic() const noexcept { return elastic.isActive(); }

    // Fades can never overlap or exceed the clip.
    void clampFades() noexcept
    {
        fadeIn = juce::jlimit<juce::int64> (0, length, fadeIn);
        fadeOut = juce::jlimit<juce::int64> (0, length - fadeIn, fadeOut);
    }

    double getStartSeconds() const noexcept  { return (double) timelineStart / sampleRate; }
    double getLengthSeconds() const noexcept { return (double) length / sampleRate; }
    double getEndSeconds() const noexcept    { return getStartSeconds() + getLengthSeconds(); }
};

// A step pattern placed on the timeline. The pattern loops for `length`.
struct PatternClip
{
    juce::String name;
    std::shared_ptr<const engine::StepPattern> pattern;
    double sampleRate = 44100.0;
    juce::int64 timelineStart = 0;
    juce::int64 length        = 0;
    juce::int64 loopOffset    = 0;   // pattern position (samples) at timelineStart; lets trims/splits keep phase
    float gain = 1.0f;
    bool loop = false;               // repeat the pattern for the clip's length (off: it plays once)
    double loopBaseBeats = 0.0;      // the content length when Loop was switched on; what "back to normal" restores (0 = unknown)

    double getStartSeconds() const noexcept  { return (double) timelineStart / sampleRate; }
    double getLengthSeconds() const noexcept { return (double) length / sampleRate; }
    double getEndSeconds() const noexcept    { return getStartSeconds() + getLengthSeconds(); }
};

// Notes placed on the timeline. The sequence loops for `length`.
struct MidiClip
{
    juce::String name;
    std::shared_ptr<const engine::MidiSequence> sequence;
    double sampleRate = 44100.0;
    juce::int64 timelineStart = 0;
    juce::int64 length        = 0;
    juce::int64 loopOffset    = 0;   // sequence position (samples) at timelineStart
    float gain = 1.0f;
    bool loop = false;               // repeat the sequence for the clip's length (off: it plays once)
    double loopBaseBeats = 0.0;      // the content length when Loop was switched on; what "back to normal" restores (0 = unknown)

    double getStartSeconds() const noexcept  { return (double) timelineStart / sampleRate; }
    double getLengthSeconds() const noexcept { return (double) length / sampleRate; }
    double getEndSeconds() const noexcept    { return getStartSeconds() + getLengthSeconds(); }
};

// Mixer insert slot. The effect instance is stateful and shared between the
// model and the render snapshot; its parameters are immutable.
struct Insert
{
    engine::EffectType type = engine::EffectType::none;
    std::shared_ptr<engine::Effect> instance;
    std::shared_ptr<const engine::InsertParams> params;
    bool bypass = false;
    int keyBus = -1;                 // sidechain key input (bus index), -1 = internal
    bool keyListen = false;
    juce::String pluginIdentifier;   // hosted plugins: PluginDescription::createIdentifierString()
    bool isEmpty() const noexcept { return type == engine::EffectType::none || instance == nullptr; }
    bool isPlugin() const noexcept { return type == engine::EffectType::plugin; }
    juce::String displayName() const { return instance != nullptr ? instance->getDisplayName() : juce::String (engine::Effect::typeName (type)); }
};

struct Send
{
    int bus = -1;          // -1 = no send
    float gain = 1.0f;     // linear
    bool preFader = false;
    bool isActive() const noexcept { return bus >= 0; }
};

enum class AutomationMode { off, read, touch, latch, write, trim };

// Pro Tools-style meter types. K-System meters show RMS with 0 at -12/-14/-20 dBFS.
enum class MeterType { samplePeak, rms, peakAndRms, vu, k12, k14, k20 };

inline const char* meterTypeName (MeterType m)
{
    switch (m)
    {
        case MeterType::samplePeak: return "Sample Peak";
        case MeterType::rms:        return "RMS";
        case MeterType::peakAndRms: return "Peak + RMS";
        case MeterType::vu:         return "VU";
        case MeterType::k12:        return "K-12";
        case MeterType::k14:        return "K-14";
        case MeterType::k20:        return "K-20";
    }
    return "";
}

inline const char* automationModeName (AutomationMode m)
{
    switch (m) { case AutomationMode::off: return "Off"; case AutomationMode::read: return "Read"; case AutomationMode::touch: return "Touch";
                 case AutomationMode::latch: return "Latch"; case AutomationMode::write: return "Write"; case AutomationMode::trim: return "Trim"; }
    return "";
}

// Pro Tools-style group: a set of tracks whose controls (Mix) and/or clip
// edits and selections (Edit) follow each other.
struct Group
{
    enum class Type { edit, mix, both };
    struct Attributes { bool volume = true, mute = true, solo = true, pan = false, arm = false; };

    int id = 0;                    // stable id; also drives the badge letter (a, b, c...)
    juce::String name;
    juce::Colour colour { 0xffe67e22 };
    Type type = Type::both;
    bool active = true;
    std::vector<int> trackIds;
    Attributes attributes;

    bool contains (int trackId) const noexcept { return std::find (trackIds.begin(), trackIds.end(), trackId) != trackIds.end(); }
    bool isEdit() const noexcept { return type != Type::mix; }
    bool isMix() const noexcept  { return type != Type::edit; }
    juce::String badge() const   { return juce::String::charToString ((juce::juce_wchar) ('a' + ((id - 1) % 26))); }
};

// An alternate take list for an audio track. The track's main (playing)
// playlist is Track::clips; alternates are stored here and can be swapped in.
struct Playlist
{
    juce::String name;
    std::vector<AudioClip> clips;
};

// Record modes (Pro Tools): Normal records between punch points (the time
// selection) or until stop; QuickPunch rolls with the inputs captured and
// the Record button punches in/out; TrackPunch punches per track with the
// R buttons; Loop makes every Cycle pass a take.
enum class RecordMode { normal, quickPunch, trackPunch, loop };
inline const char* recordModeName (RecordMode m)
{
    switch (m) { case RecordMode::normal: return "Normal"; case RecordMode::quickPunch: return "QuickPunch"; case RecordMode::trackPunch: return "TrackPunch"; case RecordMode::loop: return "Loop"; }
    return "";
}

// Memory locations (Pro Tools) and arrangement sections. A marker is a
// point (endSeconds < 0) or a range; sections are ranges shown in the
// arrangement strip and can be reordered, moving their contents. Markers
// optionally recall the edit selection and zoom.
struct Marker
{
    int id = 0;                    // stable, also the "memory location number"
    juce::String name;
    double seconds = 0.0;
    double endSeconds = -1.0;      // < 0: a point marker
    juce::Colour colour { 0xffe6b422 };
    bool isSection = false;
    bool recallSelection = false;
    double selectionStart = 0.0, selectionEnd = 0.0;
    bool recallZoom = false;
    double viewStartSeconds = 0.0, pixelsPerSecond = 60.0;

    bool isRange() const noexcept { return endSeconds > seconds; }
    double length() const noexcept { return isRange() ? endSeconds - seconds : 0.0; }
};

struct RecordSettings
{
    RecordMode mode = RecordMode::normal;
    bool preRoll = false, postRoll = false;
    double preRollSeconds = 2.0, postRollSeconds = 2.0;
};

struct Track
{
    enum class Type { audio, instrument, aux, master, vca };
    enum class InstrumentKind { none, drumMachine, synth };
    static constexpr int numInsertSlots = 10;
    static constexpr int numSendSlots = 5;
    static constexpr int numBuses = 8;

    int id = 0;                       // stable identity, assigned by the Session
    juce::String name;
    Type type = Type::audio;
    juce::Colour colour { 0xff3498db };

    std::vector<AudioClip> clips;                 // audio tracks: the main playlist
    juce::String mainPlaylistName;                // e.g. "Vox.01" (empty = track name)
    std::vector<Playlist> alternates;             // other takes

    InstrumentKind instrumentKind = InstrumentKind::none;
    std::vector<PatternClip> patternClips;        // drum machine tracks
    std::shared_ptr<const engine::DrumKit> drumKit;
    std::vector<MidiClip> midiClips;              // synth (instrument) tracks
    engine::MidiRealtimeProps midiProps;          // real-time properties (quantize, transpose, velocity, delay, duration)

    // Freeze: the track's post-insert (pre-fader) output rendered to audio.
    // While frozen the strip plays this instead of its sources and inserts;
    // fader, pan, sends and their automation stay live.
    struct FreezeState
    {
        bool frozen = false;
        std::shared_ptr<const juce::AudioBuffer<float>> audio;   // at the session sample rate, from timeline 0
        double sampleRate = 48000.0;
        juce::File file;                                         // where the render was written (for session files)
    };
    FreezeState freeze;
    bool isFrozen() const noexcept { return freeze.frozen && freeze.audio != nullptr; }
    std::shared_ptr<engine::Instrument> instrument;                    // stateful voice pool, owned here like an Effect
    std::shared_ptr<const engine::InstrumentParams> instrumentParams;  // immutable, copy-on-write

    engine::InstrumentType instrumentType() const noexcept { return instrumentParams != nullptr ? instrumentParams->type : engine::InstrumentType::none; }
    bool hasInstrument() const noexcept { return isSynth() && instrument != nullptr && instrumentParams != nullptr; }

    float gain = 1.0f;       // linear, 0..2
    float pan = 0.0f;        // -1..1
    bool mute = false;
    bool solo = false;

    // Mixer
    std::array<Insert, numInsertSlots> inserts;
    int firstEmptyInsert() const noexcept { for (int i = 0; i < numInsertSlots; ++i) if (inserts[(size_t) i].isEmpty()) return i; return -1; }
    std::array<Send, numSendSlots> sends;
    int outputBus = -1;      // -1 = main mix
    int inputBus = -1;       // aux tracks: which bus feeds this strip

    MeterType meterType = MeterType::samplePeak;
    int vcaTrackId = -1;     // VCA master this track is assigned to (-1 = none)

    // Automation
    AutomationMode automationMode = AutomationMode::read;
    std::vector<std::shared_ptr<const engine::AutomationLane>> automation;   // one lane per parameter
    std::vector<engine::ParamId> writing;   // parameters in an active write pass (transient)
    float volumeTrim = 1.0f;                // live Trim-mode offset (linear), transient

    const engine::AutomationLane* laneFor (const engine::ParamId& p) const noexcept
    {
        for (const auto& l : automation) if (l != nullptr && l->param == p) return l.get();
        return nullptr;
    }
    bool isWriting (const engine::ParamId& p) const noexcept
    {
        return std::find (writing.begin(), writing.end(), p) != writing.end();
    }

    // Recording (audio tracks)
    bool armed = false;      // record-enabled
    bool punched = false;    // TrackPunch: currently punched in (transient, while recording)
    bool monitor = false;    // pass the input straight to the outputs
    int firstInput = 0;      // device input channel (resolved from inputPath when set)
    int numInputs = 1;       // 1 mono, 2 stereo pair
    int inputPath = -1;      // index into IOSetup::inputs, -1 = use firstInput/numInputs directly

    // Output path (IOSetup::outputs index) used when outputBus == -1; 0 = Main
    int outputPath = 0;
    int delayOffset = 0;     // user delay-compensation offset, samples

    // Auto-fades: every clip that lands on the track by recording or import gets these fades (0 = none)
    double autoFadeInSeconds = 0.0, autoFadeOutSeconds = 0.0;
    engine::FadeShape autoFadeShape = engine::FadeShape::linear;
    void applyAutoFades (AudioClip& clip) const noexcept
    {
        if (autoFadeInSeconds <= 0.0 && autoFadeOutSeconds <= 0.0) return;
        const auto half = clip.length / 2;
        clip.fadeIn  = juce::jlimit<juce::int64> (0, half, (juce::int64) std::llround (autoFadeInSeconds * clip.sampleRate));
        clip.fadeOut = juce::jlimit<juce::int64> (0, half, (juce::int64) std::llround (autoFadeOutSeconds * clip.sampleRate));
        clip.fadeInShape = clip.fadeOutShape = autoFadeShape;
    }

    bool isInstrument() const noexcept  { return type == Type::instrument; }
    bool isDrumMachine() const noexcept { return isInstrument() && instrumentKind == InstrumentKind::drumMachine; }
    bool isSynth() const noexcept       { return isInstrument() && instrumentKind == InstrumentKind::synth; }
    bool isAudio() const noexcept       { return type == Type::audio; }
    bool isAux() const noexcept         { return type == Type::aux; }
    bool isMaster() const noexcept      { return type == Type::master; }
    bool isVca() const noexcept         { return type == Type::vca; }
    bool carriesAudio() const noexcept  { return type == Type::audio || type == Type::instrument || type == Type::aux; }
    bool hasContent() const noexcept    { return ! clips.empty() || ! patternClips.empty() || ! midiClips.empty(); }
};

class Session
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void sessionChanged (Session&) = 0;
    };

    // ---- Read access ----
    const std::vector<Track>& getTracks() const noexcept { return tracks; }
    int getNumTracks() const noexcept { return (int) tracks.size(); }
    const Track* getTrack (int index) const noexcept
    {
        return juce::isPositiveAndBelow (index, getNumTracks()) ? &tracks[(size_t) index] : nullptr;
    }
    int indexOfTrackId (int id) const noexcept;
    const Track& getMaster() const noexcept { return master; }
    const Track* getTrackOrMaster (int index) const noexcept { return index == -1 ? &master : getTrack (index); }
    const IOSetup& getIO() const noexcept { return io; }
    const std::vector<Group>& getGroups() const noexcept { return groups; }
    const Group* getGroup (int groupId) const noexcept
    {
        for (const auto& g : groups) if (g.id == groupId) return &g;
        return nullptr;
    }
    juce::String busName (int bus) const { return io.busName (bus); }
    bool isDelayCompensationEnabled() const noexcept { return delayCompensation; }
    const RecordSettings& getRecordSettings() const noexcept { return recordSettings; }
    const std::vector<Marker>& getMarkers() const noexcept { return markers; }
    const Marker* getMarker (int id) const noexcept { for (const auto& m : markers) if (m.id == id) return &m; return nullptr; }
    int indexOfMarkerId (int id) const noexcept { for (int i = 0; i < (int) markers.size(); ++i) if (markers[(size_t) i].id == id) return i; return -1; }
    // Sections in timeline order.
    std::vector<const Marker*> getSections() const;

    // The device input channels a track records/monitors from.
    std::pair<int, int> resolveInput (const Track& t) const noexcept
    {
        if (auto* path = io.input (t.inputPath)) return { path->firstChannel, path->numChannels };
        return { t.firstInput, t.numInputs };
    }
    double getBpm() const noexcept { return bpm; }
    int getBeatsPerBar() const noexcept { return beatsPerBar; }
    double getLengthSeconds() const;   // end of the last clip of any kind, or 0

    // ---- Mutation (through commands only) ----
    void execute (std::unique_ptr<Command> command) { history.execute (*this, std::move (command)); notify(); }
    bool undo() { const bool ok = history.undo (*this); if (ok) notify(); return ok; }
    bool redo() { const bool ok = history.redo (*this); if (ok) notify(); return ok; }
    const CommandHistory& getHistory() const noexcept { return history; }
    void clearHistory() { history.clear(); notify(); }

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Palette used to colour new tracks GarageBand-style.
    static juce::Colour colourForTrackIndex (int index);
    static const std::vector<juce::Colour>& trackPalette();   // the same colours, for choosing by hand

private:
    // Commands are the only code allowed to touch these.
    friend class AddTrackCommand;
    friend class RemoveTrackCommand;
    friend class AddClipCommand;
    friend class RemoveClipCommand;
    friend class SetTrackFlagCommand;
    friend class SetTrackInputCommand;
    friend class AddPatternClipCommand;
    friend class SetStepCommand;
    friend class ReplacePatternCommand;
    friend class SetClipLoopCommand;
    friend class SetClipLoopBaseCommand;
    friend class SetPadSampleCommand;
    friend class AddMidiClipCommand;
    friend class ReplaceMidiSequenceCommand;
    friend class SetInstrumentParamsCommand;
    friend class SetInstrumentCommand;
    friend class SetTrackMixCommand;
    friend class ReplaceDrumKitCommand;
    friend class ReplaceAutomationLaneCommand;
    friend class SetAutomationModeCommand;
    friend class SetAutomationWritingCommand;
    friend class SetVolumeTrimCommand;
    friend class SetIOSetupCommand;
    friend class SetDelayCompensationCommand;
    friend class SetRecordSettingsCommand;
    friend class SetTempoCommand;
    friend class SetTrackPunchCommand;
    friend class AddMarkerCommand;
    friend class SetTrackMidiPropsCommand;
    friend class RenameTrackCommand;
    friend class SetTrackColourCommand;
    friend class SetTrackAutoFadesCommand;
    friend class FreezeTrackCommand;
    friend class UnfreezeTrackCommand;
    friend class RemoveMarkerCommand;
    friend class ReplaceMarkerCommand;
    friend class LoadSessionCommand;
    friend class CreateGroupCommand;
    friend class RemoveGroupCommand;
    friend class ReplaceGroupCommand;
    friend class SetGroupActiveCommand;
    friend class SetTrackVcaCommand;
    friend class NewPlaylistCommand;
    friend class DuplicatePlaylistCommand;
    friend class SwitchPlaylistCommand;
    friend class DeletePlaylistCommand;
    friend class AddAlternatePlaylistCommand;
    friend class ReplaceMainClipsCommand;
    friend struct EditAccess;

    void notify() { listeners.call ([this] (Listener& l) { l.sessionChanged (*this); }); }

    std::vector<Track> tracks;
    Track master = [] { Track m; m.name = "Master"; m.type = Track::Type::master; m.colour = juce::Colour (0xffb0b8c4); return m; }();
    IOSetup io = IOSetup::createDefault (2, 2);
    bool delayCompensation = true;
    RecordSettings recordSettings;
    std::vector<Marker> markers;
    int nextMarkerId = 1;
    std::vector<Group> groups;
    int nextGroupId = 1;
    int nextTrackId = 1;
    double bpm = 120.0;
    int beatsPerBar = 4;
    CommandHistory history;
    juce::ListenerList<Listener> listeners;
};

// Gateway for commands defined outside this header (see ClipEdits.h).
struct EditAccess
{
    static std::vector<Track>& tracks (Session& s) noexcept { return s.tracks; }
    static Track& master (Session& s) noexcept { return s.master; }
    // index -1 addresses the master strip
    static Track* trackOrMaster (Session& s, int index) noexcept
    {
        if (index == -1) return &s.master;
        return juce::isPositiveAndBelow (index, (int) s.tracks.size()) ? &s.tracks[(size_t) index] : nullptr;
    }
};

//==============================================================================
// Built-in commands

class AddTrackCommand final : public Command
{
public:
    explicit AddTrackCommand (Track t, int insertAt = -1) : track (std::move (t)), insertIndex (insertAt) {}
    juce::String getName() const override { return "Add Track"; }
    void execute (Session& s) override
    {
        if (track.id == 0) track.id = s.nextTrackId++;
        index = juce::isPositiveAndBelow (insertIndex, (int) s.tracks.size() + 1) ? insertIndex : (int) s.tracks.size();
        s.tracks.insert (s.tracks.begin() + index, track);
    }
    void undo (Session& s) override { s.tracks.erase (s.tracks.begin() + index); }
    int getTrackIndex() const noexcept { return index; }
private:
    Track track;
    int index = -1, insertIndex = -1;
};

class RemoveTrackCommand final : public Command
{
public:
    explicit RemoveTrackCommand (int trackIndex) : index (trackIndex) {}
    juce::String getName() const override { return "Remove Track"; }
    void execute (Session& s) override { removed = s.tracks[(size_t) index]; s.tracks.erase (s.tracks.begin() + index); }
    void undo (Session& s) override    { s.tracks.insert (s.tracks.begin() + index, removed); }
private:
    int index;
    Track removed;
};

class AddClipCommand final : public Command
{
public:
    AddClipCommand (int trackIndex, AudioClip c) : index (trackIndex), clip (std::move (c)) {}
    juce::String getName() const override { return "Add Clip"; }
    void execute (Session& s) override
    {
        auto& clips = s.tracks[(size_t) index].clips;
        clipIndex = (int) clips.size();
        clips.push_back (clip);
    }
    void undo (Session& s) override { auto& clips = s.tracks[(size_t) index].clips; clips.erase (clips.begin() + clipIndex); }
private:
    int index;
    AudioClip clip;
    int clipIndex = -1;
};

class RemoveClipCommand final : public Command
{
public:
    RemoveClipCommand (int trackIndex, int clipIdx) : index (trackIndex), clipIndex (clipIdx) {}
    juce::String getName() const override { return "Remove Clip"; }
    void execute (Session& s) override
    {
        auto& clips = s.tracks[(size_t) index].clips;
        removed = clips[(size_t) clipIndex];
        clips.erase (clips.begin() + clipIndex);
    }
    void undo (Session& s) override { auto& clips = s.tracks[(size_t) index].clips; clips.insert (clips.begin() + clipIndex, removed); }
private:
    int index, clipIndex;
    AudioClip removed;
};

class SetTrackFlagCommand final : public Command
{
public:
    enum class Flag { mute, solo, arm, monitor };
    SetTrackFlagCommand (int trackIndex, Flag f, bool value) : index (trackIndex), flag (f), newValue (value) {}
    juce::String getName() const override
    {
        switch (flag)
        {
            case Flag::mute:    return "Mute";
            case Flag::solo:    return "Solo";
            case Flag::arm:     return "Record Arm";
            case Flag::monitor: return "Input Monitor";
        }
        return {};
    }
    bool isUndoable() const override { return flag == Flag::mute || flag == Flag::solo; }
    void execute (Session& s) override { auto& t = s.tracks[(size_t) index]; auto& v = ref (t); oldValue = v; v = newValue; }
    void undo (Session& s) override    { ref (s.tracks[(size_t) index]) = oldValue; }
private:
    bool& ref (Track& t) const noexcept
    {
        switch (flag)
        {
            case Flag::mute:    return t.mute;
            case Flag::solo:    return t.solo;
            case Flag::arm:     return t.armed;
            case Flag::monitor: return t.monitor;
        }
        return t.mute;
    }
    int index; Flag flag; bool newValue, oldValue = false;
};

class SetTrackInputCommand final : public Command
{
public:
    SetTrackInputCommand (int trackIndex, int firstInputChannel, int inputCount)
        : index (trackIndex), first (firstInputChannel), count (juce::jlimit (1, 2, inputCount)) {}
    juce::String getName() const override { return "Set Track Input"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override { auto& t = s.tracks[(size_t) index]; t.firstInput = first; t.numInputs = count; }
    void undo (Session&) override {}
private:
    int index, first, count;
};

class AddPatternClipCommand final : public Command
{
public:
    AddPatternClipCommand (int trackIndex, PatternClip c) : index (trackIndex), clip (std::move (c)) {}
    juce::String getName() const override { return "Add Pattern"; }
    void execute (Session& s) override
    {
        auto& clips = s.tracks[(size_t) index].patternClips;
        clipIndex = (int) clips.size();
        clips.push_back (clip);
    }
    void undo (Session& s) override
    {
        auto& clips = s.tracks[(size_t) index].patternClips;
        clips.erase (clips.begin() + clipIndex);
    }
private:
    int index;
    PatternClip clip;
    int clipIndex = -1;
};

// Copy-on-write edit of one step. The engine keeps playing the old pattern
// until the new snapshot lands.
class SetStepCommand final : public Command
{
public:
    SetStepCommand (int trackIndex, int patternClipIndex, int padIndex, int stepIndex, std::uint8_t newVelocity)
        : index (trackIndex), clipIndex (patternClipIndex), pad (padIndex), step (stepIndex), velocity (newVelocity) {}
    juce::String getName() const override { return velocity > 0 ? "Set Step" : "Clear Step"; }
    void execute (Session& s) override
    {
        auto& clip = s.tracks[(size_t) index].patternClips[(size_t) clipIndex];
        oldPattern = clip.pattern;
        auto updated = std::make_shared<engine::StepPattern> (oldPattern != nullptr ? *oldPattern : engine::StepPattern());
        updated->set (pad, step, velocity);
        clip.pattern = std::move (updated);
    }
    void undo (Session& s) override { s.tracks[(size_t) index].patternClips[(size_t) clipIndex].pattern = oldPattern; }
private:
    int index, clipIndex, pad, step;
    std::uint8_t velocity;
    std::shared_ptr<const engine::StepPattern> oldPattern;
};

// Whole-pattern replacement (paint drags, selection edits, unrolling).
class ReplacePatternCommand final : public Command
{
public:
    ReplacePatternCommand (int trackIndex, int patternClipIndex, std::shared_ptr<const engine::StepPattern> pattern, juce::String actionName)
        : index (trackIndex), clipIndex (patternClipIndex), newPattern (std::move (pattern)), name (std::move (actionName)) {}
    juce::String getName() const override { return name; }
    void execute (Session& s) override
    {
        auto& clip = s.tracks[(size_t) index].patternClips[(size_t) clipIndex];
        oldPattern = clip.pattern;
        clip.pattern = newPattern;
    }
    void undo (Session& s) override { s.tracks[(size_t) index].patternClips[(size_t) clipIndex].pattern = oldPattern; }
private:
    int index, clipIndex;
    std::shared_ptr<const engine::StepPattern> newPattern, oldPattern;
    juce::String name;
};

// Loop on/off for a pattern or MIDI clip.
class SetClipLoopCommand final : public Command
{
public:
    SetClipLoopCommand (int trackIndex, bool isMidi, int clipIndex, bool loopOn) : index (trackIndex), midi (isMidi), clip (clipIndex), on (loopOn) {}
    juce::String getName() const override { return on ? "Loop Clip" : "Play Clip Once"; }
    void execute (Session& s) override { apply (s, on, &was); }
    void undo (Session& s) override { apply (s, was, nullptr); }
private:
    void apply (Session& s, bool value, bool* remember)
    {
        if (! juce::isPositiveAndBelow (index, (int) s.tracks.size())) return;
        auto& t = s.tracks[(size_t) index];
        if (midi) { if (juce::isPositiveAndBelow (clip, (int) t.midiClips.size())) { if (remember) *remember = t.midiClips[(size_t) clip].loop; t.midiClips[(size_t) clip].loop = value; } }
        else      { if (juce::isPositiveAndBelow (clip, (int) t.patternClips.size())) { if (remember) *remember = t.patternClips[(size_t) clip].loop; t.patternClips[(size_t) clip].loop = value; } }
    }
    int index; bool midi; int clip; bool on, was = false;
};

// Remembers (or forgets, with 0) the content length a clip had when Loop was switched on.
class SetClipLoopBaseCommand final : public Command
{
public:
    SetClipLoopBaseCommand (int trackIndex, bool isMidi, int clipIndex, double beats) : index (trackIndex), midi (isMidi), clip (clipIndex), value (beats) {}
    juce::String getName() const override { return "Loop Base"; }
    void execute (Session& s) override { apply (s, value, &was); }
    void undo (Session& s) override { apply (s, was, nullptr); }
private:
    void apply (Session& s, double v, double* remember)
    {
        if (! juce::isPositiveAndBelow (index, (int) s.tracks.size())) return;
        auto& t = s.tracks[(size_t) index];
        if (midi) { if (juce::isPositiveAndBelow (clip, (int) t.midiClips.size())) { if (remember) *remember = t.midiClips[(size_t) clip].loopBaseBeats; t.midiClips[(size_t) clip].loopBaseBeats = v; } }
        else      { if (juce::isPositiveAndBelow (clip, (int) t.patternClips.size())) { if (remember) *remember = t.patternClips[(size_t) clip].loopBaseBeats; t.patternClips[(size_t) clip].loopBaseBeats = v; } }
    }
    int index; bool midi; int clip; double value, was = 0.0;
};

// Copy-on-write replacement of one pad's sample.
class SetPadSampleCommand final : public Command
{
public:
    SetPadSampleCommand (int trackIndex, int padIndex, engine::DrumSample newSample)
        : index (trackIndex), pad (padIndex), sample (std::move (newSample)) {}
    juce::String getName() const override { return "Load Pad Sample"; }
    void execute (Session& s) override
    {
        auto& track = s.tracks[(size_t) index];
        oldKit = track.drumKit;
        auto updated = std::make_shared<engine::DrumKit> (oldKit != nullptr ? *oldKit : engine::DrumKit());
        updated->pads[(size_t) pad] = sample;
        track.drumKit = std::move (updated);
    }
    void undo (Session& s) override { s.tracks[(size_t) index].drumKit = oldKit; }
private:
    int index, pad;
    engine::DrumSample sample;
    std::shared_ptr<const engine::DrumKit> oldKit;
};

class SetTrackMixCommand final : public Command
{
public:
    SetTrackMixCommand (int trackIndex, float newGain, float newPan)
        : index (trackIndex), gain (juce::jlimit (0.0f, 2.0f, newGain)), pan (juce::jlimit (-1.0f, 1.0f, newPan)) {}
    juce::String getName() const override { return "Adjust Volume/Pan"; }
    void execute (Session& s) override
    {
        if (auto* t = EditAccess::trackOrMaster (s, index)) { oldGain = t->gain; oldPan = t->pan; t->gain = gain; t->pan = pan; }
    }
    void undo (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) { t->gain = oldGain; t->pan = oldPan; } }
private:
    int index;
    float gain, pan, oldGain = 1.0f, oldPan = 0.0f;
};

// Copy-on-write replacement of the whole kit (pad levels etc.).
class ReplaceDrumKitCommand final : public Command
{
public:
    ReplaceDrumKitCommand (int trackIndex, std::shared_ptr<const engine::DrumKit> kit, juce::String actionName = "Adjust Kit")
        : index (trackIndex), newKit (std::move (kit)), name (std::move (actionName)) {}
    juce::String getName() const override { return name; }
    void execute (Session& s) override { auto& t = s.tracks[(size_t) index]; oldKit = t.drumKit; t.drumKit = newKit; }
    void undo (Session& s) override    { s.tracks[(size_t) index].drumKit = oldKit; }
private:
    int index;
    std::shared_ptr<const engine::DrumKit> newKit, oldKit;
    juce::String name;
};

// Copy-on-write replacement of one parameter's lane (add/move/delete points,
// or a whole recorded pass). An empty lane removes it.
class ReplaceAutomationLaneCommand final : public Command
{
public:
    ReplaceAutomationLaneCommand (int trackIndex, std::shared_ptr<const engine::AutomationLane> lane, juce::String actionName = "Automation")
        : index (trackIndex), newLane (std::move (lane)), name (std::move (actionName)) {}
    juce::String getName() const override { return name; }
    void execute (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr || newLane == nullptr) return;
        auto& lanes = t->automation;
        auto it = std::find_if (lanes.begin(), lanes.end(), [&] (const auto& l) { return l != nullptr && l->param == newLane->param; });
        oldLane = it != lanes.end() ? *it : nullptr;
        if (it != lanes.end()) lanes.erase (it);
        if (! newLane->isEmpty()) lanes.push_back (newLane);
    }
    void undo (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr || newLane == nullptr) return;
        auto& lanes = t->automation;
        std::erase_if (lanes, [&] (const auto& l) { return l != nullptr && l->param == newLane->param; });
        if (oldLane != nullptr) lanes.push_back (oldLane);
    }
private:
    int index;
    std::shared_ptr<const engine::AutomationLane> newLane, oldLane;
    juce::String name;
};

class SetAutomationModeCommand final : public Command
{
public:
    SetAutomationModeCommand (int trackIndex, AutomationMode m) : index (trackIndex), mode (m) {}
    juce::String getName() const override { return "Automation Mode"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) t->automationMode = mode; }
    void undo (Session&) override {}
private:
    int index;
    AutomationMode mode;
};

// Marks a parameter as being written (the engine then follows the live value).
class SetAutomationWritingCommand final : public Command
{
public:
    SetAutomationWritingCommand (int trackIndex, engine::ParamId p, bool on) : index (trackIndex), param (p), writing (on) {}
    juce::String getName() const override { return "Automation Write"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr) return;
        std::erase (t->writing, param);
        if (writing) t->writing.push_back (param);
    }
    void undo (Session&) override {}
private:
    int index;
    engine::ParamId param;
    bool writing;
};

//==============================================================================
// Groups & VCA

class CreateGroupCommand final : public Command
{
public:
    explicit CreateGroupCommand (Group g) : group (std::move (g)) {}
    juce::String getName() const override { return "Create Group"; }
    void execute (Session& s) override
    {
        if (group.id == 0) group.id = s.nextGroupId++;
        s.groups.push_back (group);
    }
    void undo (Session& s) override { std::erase_if (s.groups, [&] (const Group& g) { return g.id == group.id; }); }
    int getGroupId() const noexcept { return group.id; }
private:
    Group group;
};

class RemoveGroupCommand final : public Command
{
public:
    explicit RemoveGroupCommand (int groupId) : id (groupId) {}
    juce::String getName() const override { return "Delete Group"; }
    void execute (Session& s) override
    {
        auto it = std::find_if (s.groups.begin(), s.groups.end(), [&] (const Group& g) { return g.id == id; });
        if (it != s.groups.end()) { removed = *it; index = (int) (it - s.groups.begin()); s.groups.erase (it); }
    }
    void undo (Session& s) override { if (index >= 0) s.groups.insert (s.groups.begin() + juce::jmin (index, (int) s.groups.size()), removed); }
private:
    int id, index = -1;
    Group removed;
};

// Replace a group's name/type/attributes/membership wholesale.
class ReplaceGroupCommand final : public Command
{
public:
    explicit ReplaceGroupCommand (Group g) : group (std::move (g)) {}
    juce::String getName() const override { return "Edit Group"; }
    void execute (Session& s) override
    {
        for (auto& g : s.groups) if (g.id == group.id) { old = g; g = group; return; }
    }
    void undo (Session& s) override { for (auto& g : s.groups) if (g.id == group.id) g = old; }
private:
    Group group, old;
};

class SetGroupActiveCommand final : public Command
{
public:
    SetGroupActiveCommand (int groupId, bool on) : id (groupId), active (on) {}
    juce::String getName() const override { return "Group Enable"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override { for (auto& g : s.groups) if (g.id == id) g.active = active; }
    void undo (Session&) override {}
private:
    int id;
    bool active;
};

class SetTrackVcaCommand final : public Command
{
public:
    SetTrackVcaCommand (int trackIndex, int vcaTrackId) : index (trackIndex), vca (vcaTrackId) {}
    juce::String getName() const override { return "Assign VCA"; }
    void execute (Session& s) override
    {
        if (auto* t = EditAccess::trackOrMaster (s, index))
        {
            old = t->vcaTrackId;
            const int vcaIndex = s.indexOfTrackId (vca);
            t->vcaTrackId = (vcaIndex >= 0 && s.tracks[(size_t) vcaIndex].isVca() && ! t->isVca()) ? vca : -1;
        }
    }
    void undo (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) t->vcaTrackId = old; }
private:
    int index, vca, old = -1;
};

//==============================================================================
// Playlists

inline juce::String defaultPlaylistName (const Track& t, int number)
{
    return t.name + "." + juce::String (number).paddedLeft ('0', 2);
}

// Move the current main playlist to the alternates and start an empty one.
class NewPlaylistCommand final : public Command
{
public:
    NewPlaylistCommand (int trackIndex, juce::String newName = {}) : index (trackIndex), name (std::move (newName)) {}
    juce::String getName() const override { return "New Playlist"; }
    void execute (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr) return;
        oldMainName = t->mainPlaylistName;
        const auto mainName = t->mainPlaylistName.isNotEmpty() ? t->mainPlaylistName : defaultPlaylistName (*t, 1);
        t->alternates.push_back ({ mainName, std::move (t->clips) });
        t->clips.clear();
        t->mainPlaylistName = name.isNotEmpty() ? name : defaultPlaylistName (*t, (int) t->alternates.size() + 1);
    }
    void undo (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr || t->alternates.empty()) return;
        t->clips = std::move (t->alternates.back().clips);
        t->alternates.pop_back();
        t->mainPlaylistName = oldMainName;
    }
private:
    int index;
    juce::String name, oldMainName;
};

// Keep a copy of the main playlist as an alternate and carry on editing the main.
class DuplicatePlaylistCommand final : public Command
{
public:
    explicit DuplicatePlaylistCommand (int trackIndex) : index (trackIndex) {}
    juce::String getName() const override { return "Duplicate Playlist"; }
    void execute (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr) return;
        oldMainName = t->mainPlaylistName;
        const auto mainName = t->mainPlaylistName.isNotEmpty() ? t->mainPlaylistName : defaultPlaylistName (*t, 1);
        t->alternates.push_back ({ mainName, t->clips });
        t->mainPlaylistName = defaultPlaylistName (*t, (int) t->alternates.size() + 1);
    }
    void undo (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr || t->alternates.empty()) return;
        t->alternates.pop_back();
        t->mainPlaylistName = oldMainName;
    }
private:
    int index;
    juce::String oldMainName;
};

// Swap an alternate with the main playlist.
class SwitchPlaylistCommand final : public Command
{
public:
    SwitchPlaylistCommand (int trackIndex, int alternateIndex) : index (trackIndex), alt (alternateIndex) {}
    juce::String getName() const override { return "Switch Playlist"; }
    void execute (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr || ! juce::isPositiveAndBelow (alt, (int) t->alternates.size())) return;
        auto& a = t->alternates[(size_t) alt];
        const auto mainName = t->mainPlaylistName.isNotEmpty() ? t->mainPlaylistName : defaultPlaylistName (*t, 1);
        std::swap (a.clips, t->clips);
        t->mainPlaylistName = a.name;
        a.name = mainName;
    }
    void undo (Session& s) override { execute (s); }   // swapping again restores
private:
    int index, alt;
};

class DeletePlaylistCommand final : public Command
{
public:
    DeletePlaylistCommand (int trackIndex, int alternateIndex) : index (trackIndex), alt (alternateIndex) {}
    juce::String getName() const override { return "Delete Playlist"; }
    void execute (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr || ! juce::isPositiveAndBelow (alt, (int) t->alternates.size())) return;
        removed = t->alternates[(size_t) alt];
        t->alternates.erase (t->alternates.begin() + alt);
        didRemove = true;
    }
    void undo (Session& s) override
    {
        auto* t = EditAccess::trackOrMaster (s, index);
        if (t == nullptr || ! didRemove) return;
        t->alternates.insert (t->alternates.begin() + juce::jmin (alt, (int) t->alternates.size()), removed);
    }
private:
    int index, alt;
    Playlist removed;
    bool didRemove = false;
};

// Add a ready-made alternate (loop-record passes).
class AddAlternatePlaylistCommand final : public Command
{
public:
    AddAlternatePlaylistCommand (int trackIndex, Playlist p) : index (trackIndex), playlist (std::move (p)) {}
    juce::String getName() const override { return "Add Take"; }
    void execute (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) t->alternates.push_back (playlist); }
    void undo (Session& s) override    { if (auto* t = EditAccess::trackOrMaster (s, index); t != nullptr && ! t->alternates.empty()) t->alternates.pop_back(); }
private:
    int index;
    Playlist playlist;
};

// Replace the whole main clip list (comping, clearing). Copy-on-write, one undo step.
class ReplaceMainClipsCommand final : public Command
{
public:
    ReplaceMainClipsCommand (int trackIndex, std::vector<AudioClip> clips, juce::String actionName)
        : index (trackIndex), newClips (std::move (clips)), name (std::move (actionName)) {}
    juce::String getName() const override { return name; }
    void execute (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) { old = t->clips; t->clips = newClips; } }
    void undo (Session& s) override    { if (auto* t = EditAccess::trackOrMaster (s, index)) t->clips = old; }
private:
    int index;
    std::vector<AudioClip> newClips, old;
    juce::String name;
};

class SetMeterTypeCommand final : public Command
{
public:
    SetMeterTypeCommand (int trackIndex, MeterType type) : index (trackIndex), meterType (type) {}
    juce::String getName() const override { return "Meter Type"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) t->meterType = meterType; }
    void undo (Session&) override {}
private:
    int index;
    MeterType meterType;
};

class SetIOSetupCommand final : public Command
{
public:
    explicit SetIOSetupCommand (IOSetup setup) : newSetup (std::move (setup)) {}
    juce::String getName() const override { return "I/O Setup"; }
    void execute (Session& s) override
    {
        old = s.io;
        if (newSetup.outputs.empty()) newSetup.outputs.push_back ({ "Main", 0, 2 });
        newSetup.busNames.resize ((size_t) IOSetup::numBuses);
        s.io = newSetup;
        // Keep track references valid (and remember them for undo)
        oldTrackPaths.clear();
        for (auto& t : s.tracks)
        {
            oldTrackPaths.emplace_back (t.inputPath, t.outputPath);
            if (t.inputPath >= (int) s.io.inputs.size()) t.inputPath = -1;
            if (t.outputPath >= (int) s.io.outputs.size()) t.outputPath = 0;
        }
    }
    void undo (Session& s) override
    {
        s.io = old;
        for (size_t i = 0; i < oldTrackPaths.size() && i < s.tracks.size(); ++i)
        {
            s.tracks[i].inputPath = oldTrackPaths[i].first;
            s.tracks[i].outputPath = oldTrackPaths[i].second;
        }
    }
private:
    IOSetup newSetup, old;
    std::vector<std::pair<int, int>> oldTrackPaths;
};

class RenameTrackCommand final : public Command
{
public:
    RenameTrackCommand (int trackIndex, juce::String newName) : index (trackIndex), name (std::move (newName)) {}
    juce::String getName() const override { return "Rename Track"; }
    void execute (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) { old = s.tracks[(size_t) index].name; s.tracks[(size_t) index].name = name; } }
    void undo (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) s.tracks[(size_t) index].name = old; }
private:
    int index;
    juce::String name, old;
};

// The colour a track (or the master, index -1) shows in its header, clips and mixer strip
class SetTrackColourCommand final : public Command
{
public:
    SetTrackColourCommand (int trackIndex, juce::Colour newColour) : index (trackIndex), colour (newColour) {}
    juce::String getName() const override { return "Colour Track"; }
    void execute (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) { old = t->colour; t->colour = colour; } }
    void undo (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) t->colour = old; }
private:
    int index;
    juce::Colour colour, old;
};

// The fades a track gives clips that land on it (recording, import); undoable, saved with the session
class SetTrackAutoFadesCommand final : public Command
{
public:
    SetTrackAutoFadesCommand (int trackIndex, double fadeInSeconds, double fadeOutSeconds, engine::FadeShape shape)
        : index (trackIndex), in (juce::jmax (0.0, fadeInSeconds)), out (juce::jmax (0.0, fadeOutSeconds)), newShape (shape) {}
    juce::String getName() const override { return "Track Auto-Fades"; }
    void execute (Session& s) override
    {
        if (auto* t = EditAccess::trackOrMaster (s, index)) { oldIn = t->autoFadeInSeconds; oldOut = t->autoFadeOutSeconds; oldShape = t->autoFadeShape; t->autoFadeInSeconds = in; t->autoFadeOutSeconds = out; t->autoFadeShape = newShape; }
    }
    void undo (Session& s) override
    {
        if (auto* t = EditAccess::trackOrMaster (s, index)) { t->autoFadeInSeconds = oldIn; t->autoFadeOutSeconds = oldOut; t->autoFadeShape = oldShape; }
    }
private:
    int index;
    double in, out, oldIn = 0.0, oldOut = 0.0;
    engine::FadeShape newShape, oldShape = engine::FadeShape::linear;
};

class FreezeTrackCommand final : public Command
{
public:
    FreezeTrackCommand (int trackIndex, Track::FreezeState state) : index (trackIndex), freeze (std::move (state)) { freeze.frozen = true; }
    juce::String getName() const override { return "Freeze Track"; }
    void execute (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) { old = s.tracks[(size_t) index].freeze; s.tracks[(size_t) index].freeze = freeze; } }
    void undo (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) s.tracks[(size_t) index].freeze = old; }
private:
    int index;
    Track::FreezeState freeze, old;
};

class UnfreezeTrackCommand final : public Command
{
public:
    explicit UnfreezeTrackCommand (int trackIndex) : index (trackIndex) {}
    juce::String getName() const override { return "Unfreeze Track"; }
    void execute (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) { old = s.tracks[(size_t) index].freeze; s.tracks[(size_t) index].freeze = {}; } }
    void undo (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) s.tracks[(size_t) index].freeze = old; }
private:
    int index;
    Track::FreezeState old;
};

class SetTrackMidiPropsCommand final : public Command
{
public:
    SetTrackMidiPropsCommand (int trackIndex, engine::MidiRealtimeProps p) : index (trackIndex), props (p) {}
    juce::String getName() const override { return "Real-Time Properties"; }
    void execute (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) { old = s.tracks[(size_t) index].midiProps; s.tracks[(size_t) index].midiProps = props; } }
    void undo (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) s.tracks[(size_t) index].midiProps = old; }
private:
    int index;
    engine::MidiRealtimeProps props, old;
};

class AddMarkerCommand final : public Command
{
public:
    explicit AddMarkerCommand (Marker m) : marker (std::move (m)) {}
    juce::String getName() const override { return marker.isSection ? "Add Section" : "Add Memory Location"; }
    void execute (Session& s) override
    {
        if (marker.id <= 0 || s.getMarker (marker.id) != nullptr) marker.id = s.nextMarkerId;
        s.nextMarkerId = juce::jmax (s.nextMarkerId, marker.id + 1);
        s.markers.push_back (marker);
        std::stable_sort (s.markers.begin(), s.markers.end(), [] (const Marker& a, const Marker& b) { return a.seconds < b.seconds; });
    }
    void undo (Session& s) override { std::erase_if (s.markers, [this] (const Marker& m) { return m.id == marker.id; }); }
    int getMarkerId() const noexcept { return marker.id; }
private:
    Marker marker;
};

class RemoveMarkerCommand final : public Command
{
public:
    explicit RemoveMarkerCommand (int markerId) : id (markerId) {}
    juce::String getName() const override { return "Delete Memory Location"; }
    void execute (Session& s) override
    {
        if (const auto* m = s.getMarker (id)) removed = *m;
        std::erase_if (s.markers, [this] (const Marker& m) { return m.id == id; });
    }
    void undo (Session& s) override
    {
        if (removed.id == id && s.getMarker (id) == nullptr)
        {
            s.markers.push_back (removed);
            std::stable_sort (s.markers.begin(), s.markers.end(), [] (const Marker& a, const Marker& b) { return a.seconds < b.seconds; });
        }
    }
private:
    int id;
    Marker removed;
};

class ReplaceMarkerCommand final : public Command
{
public:
    ReplaceMarkerCommand (Marker m, juce::String commandName = "Edit Memory Location") : marker (std::move (m)), name (std::move (commandName)) {}
    juce::String getName() const override { return name; }
    void execute (Session& s) override
    {
        const int i = s.indexOfMarkerId (marker.id);
        if (i < 0) return;
        old = s.markers[(size_t) i];
        s.markers[(size_t) i] = marker;
        std::stable_sort (s.markers.begin(), s.markers.end(), [] (const Marker& a, const Marker& b) { return a.seconds < b.seconds; });
    }
    void undo (Session& s) override
    {
        const int i = s.indexOfMarkerId (marker.id);
        if (i < 0) return;
        s.markers[(size_t) i] = old;
        std::stable_sort (s.markers.begin(), s.markers.end(), [] (const Marker& a, const Marker& b) { return a.seconds < b.seconds; });
    }
private:
    Marker marker, old;
    juce::String name;
};

// Replaces the whole document (opening a session / template). Not undoable:
// the history is cleared by the caller.
class LoadSessionCommand final : public Command
{
public:
    struct Contents
    {
        std::vector<Track> tracks;
        Track master;
        IOSetup io;
        bool delayCompensation = true;
        RecordSettings recordSettings;
        std::vector<Marker> markers;
        std::vector<Group> groups;
        double bpm = 120.0;
        int beatsPerBar = 4;
    };
    explicit LoadSessionCommand (Contents c) : contents (std::move (c)) {}
    juce::String getName() const override { return "Open Session"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override
    {
        s.tracks = contents.tracks;
        s.master = contents.master;
        s.io = contents.io;
        s.delayCompensation = contents.delayCompensation;
        s.recordSettings = contents.recordSettings;
        s.markers = contents.markers;
        s.groups = contents.groups;
        s.bpm = contents.bpm;
        s.beatsPerBar = contents.beatsPerBar;
        int maxTrack = 0, maxGroup = 0, maxMarker = 0;
        for (const auto& t : s.tracks) maxTrack = juce::jmax (maxTrack, t.id);
        for (const auto& g : s.groups) maxGroup = juce::jmax (maxGroup, g.id);
        for (const auto& m : s.markers) maxMarker = juce::jmax (maxMarker, m.id);
        s.nextTrackId = maxTrack + 1; s.nextGroupId = maxGroup + 1; s.nextMarkerId = maxMarker + 1;
    }
    void undo (Session&) override {}
private:
    Contents contents;
};

// Record mode and pre/post-roll: transport settings, not document edits.
// Sets the session tempo and time signature. With `clipsFollowBars` every
// clip, marker and automation breakpoint keeps its bar position (times scale
// by old/new tempo) as on a tick-based track; without it they keep their
// time in seconds. Loop re-conforming (Elastic) is the app's job afterwards.
class SetTempoCommand final : public Command
{
public:
    SetTempoCommand (double newBpm, int newBeatsPerBar, bool clipsFollowBars)
        : bpm (juce::jlimit (20.0, 400.0, newBpm)), beatsPerBar (juce::jlimit (1, 16, newBeatsPerBar)), follow (clipsFollowBars) {}
    juce::String getName() const override { return "Set Tempo"; }
    void execute (Session& s) override
    {
        oldBpm = s.bpm; oldBeatsPerBar = s.beatsPerBar;
        if (! captured) { capture (s); captured = true; }
        s.bpm = bpm; s.beatsPerBar = beatsPerBar;
        if (follow && std::abs (oldBpm - bpm) > 1e-9) scale (s, oldBpm / bpm);
    }
    void undo (Session& s) override
    {
        s.bpm = oldBpm; s.beatsPerBar = oldBeatsPerBar;
        if (follow) restore (s);
    }
    double getOldBpm() const noexcept { return oldBpm; }

private:
    struct TrackTimes
    {
        std::vector<juce::int64> audioStarts;                         // main playlist
        std::vector<std::vector<juce::int64>> alternateStarts;
        std::vector<std::pair<juce::int64, juce::int64>> patterns;    // start, length
        std::vector<std::array<juce::int64, 3>> midi;                 // start, length, loopOffset
        std::vector<std::shared_ptr<const engine::AutomationLane>> automation;
    };
    static TrackTimes captureTrack (const Track& t)
    {
        TrackTimes tt;
        for (const auto& c : t.clips) tt.audioStarts.push_back (c.timelineStart);
        for (const auto& p : t.alternates) { std::vector<juce::int64> v; for (const auto& c : p.clips) v.push_back (c.timelineStart); tt.alternateStarts.push_back (std::move (v)); }
        for (const auto& c : t.patternClips) tt.patterns.emplace_back (c.timelineStart, c.length);
        for (const auto& c : t.midiClips) tt.midi.push_back ({ c.timelineStart, c.length, c.loopOffset });
        tt.automation = t.automation;
        return tt;
    }
    static void restoreTrack (Track& t, const TrackTimes& tt)
    {
        for (size_t i = 0; i < t.clips.size() && i < tt.audioStarts.size(); ++i) t.clips[i].timelineStart = tt.audioStarts[i];
        for (size_t p = 0; p < t.alternates.size() && p < tt.alternateStarts.size(); ++p)
            for (size_t i = 0; i < t.alternates[p].clips.size() && i < tt.alternateStarts[p].size(); ++i) t.alternates[p].clips[i].timelineStart = tt.alternateStarts[p][i];
        for (size_t i = 0; i < t.patternClips.size() && i < tt.patterns.size(); ++i) { t.patternClips[i].timelineStart = tt.patterns[i].first; t.patternClips[i].length = tt.patterns[i].second; }
        for (size_t i = 0; i < t.midiClips.size() && i < tt.midi.size(); ++i) { t.midiClips[i].timelineStart = tt.midi[i][0]; t.midiClips[i].length = tt.midi[i][1]; t.midiClips[i].loopOffset = tt.midi[i][2]; }
        t.automation = tt.automation;
    }
    static void scaleTrack (Track& t, double f)
    {
        auto sc = [f] (juce::int64 v) { return (juce::int64) std::llround ((double) v * f); };
        for (auto& c : t.clips) c.timelineStart = sc (c.timelineStart);
        for (auto& p : t.alternates) for (auto& c : p.clips) c.timelineStart = sc (c.timelineStart);
        for (auto& c : t.patternClips) { c.timelineStart = sc (c.timelineStart); c.length = juce::jmax<juce::int64> (1, sc (c.length)); c.loopOffset = sc (c.loopOffset); }
        for (auto& c : t.midiClips) { c.timelineStart = sc (c.timelineStart); c.length = juce::jmax<juce::int64> (1, sc (c.length)); c.loopOffset = sc (c.loopOffset); }
        for (auto& lane : t.automation)
        {
            if (lane == nullptr || lane->isEmpty()) continue;
            auto copy = std::make_shared<engine::AutomationLane> (*lane);
            for (auto& pt : copy->points) pt.time = sc (pt.time);
            lane = copy;
        }
    }
    void capture (Session& s)
    {
        tracks.clear();
        for (const auto& t : s.tracks) tracks.push_back (captureTrack (t));
        master = captureTrack (s.master);
        markers = s.markers;
    }
    void restore (Session& s)
    {
        for (size_t i = 0; i < s.tracks.size() && i < tracks.size(); ++i) restoreTrack (s.tracks[i], tracks[i]);
        restoreTrack (s.master, master);
        s.markers = markers;
    }
    void scale (Session& s, double f)
    {
        for (auto& t : s.tracks) scaleTrack (t, f);
        scaleTrack (s.master, f);
        for (auto& m : s.markers)
        {
            m.seconds *= f;
            if (m.endSeconds >= 0.0) m.endSeconds *= f;
            m.selectionStart *= f; m.selectionEnd *= f; m.viewStartSeconds *= f;
        }
    }
    double bpm, oldBpm = 120.0;
    int beatsPerBar, oldBeatsPerBar = 4;
    bool follow, captured = false;
    std::vector<TrackTimes> tracks;
    TrackTimes master;
    std::vector<Marker> markers;
};

class SetRecordSettingsCommand final : public Command
{
public:
    explicit SetRecordSettingsCommand (RecordSettings s) : settings (s) {}
    juce::String getName() const override { return "Record Settings"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override { s.recordSettings = settings; }
    void undo (Session&) override {}
private:
    RecordSettings settings;
};

// TrackPunch state of a track (transient).
class SetTrackPunchCommand final : public Command
{
public:
    SetTrackPunchCommand (int trackIndex, bool on) : index (trackIndex), punched (on) {}
    juce::String getName() const override { return punched ? "Punch In" : "Punch Out"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override { if (juce::isPositiveAndBelow (index, (int) s.tracks.size())) s.tracks[(size_t) index].punched = punched; }
    void undo (Session&) override {}
private:
    int index;
    bool punched;
};

class SetDelayCompensationCommand final : public Command
{
public:
    explicit SetDelayCompensationCommand (bool on) : enabled (on) {}
    juce::String getName() const override { return enabled ? "Enable Delay Compensation" : "Disable Delay Compensation"; }
    void execute (Session& s) override { old = s.delayCompensation; s.delayCompensation = enabled; }
    void undo (Session& s) override    { s.delayCompensation = old; }
private:
    bool enabled, old = true;
};

class SetTrackDelayOffsetCommand final : public Command
{
public:
    SetTrackDelayOffsetCommand (int trackIndex, int samples) : index (trackIndex), offset (juce::jlimit (-16000, 16000, samples)) {}
    juce::String getName() const override { return "Delay Offset"; }
    void execute (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) { old = t->delayOffset; t->delayOffset = offset; } }
    void undo (Session& s) override    { if (auto* t = EditAccess::trackOrMaster (s, index)) t->delayOffset = old; }
private:
    int index, offset, old = 0;
};

// Input path (-1 = raw channels) and output path for a track.
class SetTrackPathsCommand final : public Command
{
public:
    SetTrackPathsCommand (int trackIndex, int inputPathIndex, int outputPathIndex)
        : index (trackIndex), in (inputPathIndex), out (outputPathIndex) {}
    juce::String getName() const override { return "Track I/O"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override
    {
        if (auto* t = EditAccess::trackOrMaster (s, index))
        {
            t->inputPath = in < (int) s.getIO().inputs.size() ? in : -1;
            t->outputPath = juce::isPositiveAndBelow (out, (int) s.getIO().outputs.size()) ? out : 0;
            const auto [first, num] = s.resolveInput (*t);
            t->firstInput = first; t->numInputs = num;
        }
    }
    void undo (Session&) override {}
private:
    int index, in, out;
};

// Live Trim offset (transient, like the writing flags).
class SetVolumeTrimCommand final : public Command
{
public:
    SetVolumeTrimCommand (int trackIndex, float linearTrim) : index (trackIndex), trim (juce::jlimit (0.0f, 8.0f, linearTrim)) {}
    juce::String getName() const override { return "Trim"; }
    bool isUndoable() const override { return false; }
    void execute (Session& s) override { if (auto* t = EditAccess::trackOrMaster (s, index)) t->volumeTrim = trim; }
    void undo (Session&) override {}
private:
    int index;
    float trim;
};

class AddMidiClipCommand final : public Command
{
public:
    AddMidiClipCommand (int trackIndex, MidiClip c) : index (trackIndex), clip (std::move (c)) {}
    juce::String getName() const override { return "Add MIDI Clip"; }
    void execute (Session& s) override
    {
        auto& clips = s.tracks[(size_t) index].midiClips;
        clipIndex = (int) clips.size();
        clips.push_back (clip);
    }
    void undo (Session& s) override
    {
        auto& clips = s.tracks[(size_t) index].midiClips;
        clips.erase (clips.begin() + clipIndex);
    }
private:
    int index;
    MidiClip clip;
    int clipIndex = -1;
};

// Copy-on-write replacement of a clip's notes (add/move/resize/delete are all
// expressed as "here is the new sequence").
class ReplaceMidiSequenceCommand final : public Command
{
public:
    ReplaceMidiSequenceCommand (int trackIndex, int midiClipIndex, std::shared_ptr<const engine::MidiSequence> seq,
                                juce::String actionName)
        : index (trackIndex), clipIndex (midiClipIndex), newSequence (std::move (seq)), name (std::move (actionName)) {}
    juce::String getName() const override { return name; }
    void execute (Session& s) override
    {
        auto& clip = s.tracks[(size_t) index].midiClips[(size_t) clipIndex];
        oldSequence = clip.sequence;
        clip.sequence = newSequence;
    }
    void undo (Session& s) override { s.tracks[(size_t) index].midiClips[(size_t) clipIndex].sequence = oldSequence; }
private:
    int index, clipIndex;
    std::shared_ptr<const engine::MidiSequence> newSequence, oldSequence;
    juce::String name;
};

// Changes the sound of an instrument track (knob moves, presets, sampler
// sample loads). The params must be of the track's current instrument type;
// use SetInstrumentCommand to switch instruments.
class SetInstrumentParamsCommand final : public Command
{
public:
    SetInstrumentParamsCommand (int trackIndex, std::shared_ptr<const engine::InstrumentParams> p, juce::String commandName = "Change Sound")
        : index (trackIndex), newParams (std::move (p)), name (std::move (commandName)) {}
    juce::String getName() const override { return name; }
    void execute (Session& s) override { auto& t = s.tracks[(size_t) index]; oldParams = t.instrumentParams; t.instrumentParams = newParams; }
    void undo (Session& s) override    { s.tracks[(size_t) index].instrumentParams = oldParams; }
private:
    int index;
    std::shared_ptr<const engine::InstrumentParams> newParams, oldParams;
    juce::String name;
};

// Replaces the instrument itself (new instance + its default or preset
// params). Undo restores the previous instance, voices and all.
class SetInstrumentCommand final : public Command
{
public:
    SetInstrumentCommand (int trackIndex, std::shared_ptr<engine::Instrument> inst, std::shared_ptr<const engine::InstrumentParams> p)
        : index (trackIndex), newInstance (std::move (inst)), newParams (std::move (p)) {}
    juce::String getName() const override { return "Change Instrument"; }
    void execute (Session& s) override
    {
        auto& t = s.tracks[(size_t) index];
        oldInstance = t.instrument; oldParams = t.instrumentParams;
        t.instrument = newInstance; t.instrumentParams = newParams;
    }
    void undo (Session& s) override
    {
        auto& t = s.tracks[(size_t) index];
        t.instrument = oldInstance; t.instrumentParams = oldParams;
    }
private:
    int index;
    std::shared_ptr<engine::Instrument> newInstance, oldInstance;
    std::shared_ptr<const engine::InstrumentParams> newParams, oldParams;
};

} // namespace beatmaker::model
