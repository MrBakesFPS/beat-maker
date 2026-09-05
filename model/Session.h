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
#include <dsp/SynthParams.h>
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
    std::vector<MidiClip> midiClips;              // synth tracks
    std::shared_ptr<const engine::SynthParams> synthParams;

    float gain = 1.0f;       // linear, 0..2
    float pan = 0.0f;        // -1..1
    bool mute = false;
    bool solo = false;

    // Mixer
    std::array<Insert, numInsertSlots> inserts;
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
    bool monitor = false;    // pass the input straight to the outputs
    int firstInput = 0;      // device input channel (resolved from inputPath when set)
    int numInputs = 1;       // 1 mono, 2 stereo pair
    int inputPath = -1;      // index into IOSetup::inputs, -1 = use firstInput/numInputs directly

    // Output path (IOSetup::outputs index) used when outputBus == -1; 0 = Main
    int outputPath = 0;
    int delayOffset = 0;     // user delay-compensation offset, samples

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

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Palette used to colour new tracks GarageBand-style.
    static juce::Colour colourForTrackIndex (int index);

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
    friend class SetPadSampleCommand;
    friend class AddMidiClipCommand;
    friend class ReplaceMidiSequenceCommand;
    friend class SetSynthParamsCommand;
    friend class SetTrackMixCommand;
    friend class ReplaceDrumKitCommand;
    friend class ReplaceAutomationLaneCommand;
    friend class SetAutomationModeCommand;
    friend class SetAutomationWritingCommand;
    friend class SetVolumeTrimCommand;
    friend class SetIOSetupCommand;
    friend class SetDelayCompensationCommand;
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
    explicit AddTrackCommand (Track t) : track (std::move (t)) {}
    juce::String getName() const override { return "Add Track"; }
    void execute (Session& s) override
    {
        if (track.id == 0) track.id = s.nextTrackId++;
        index = (int) s.tracks.size();
        s.tracks.push_back (track);
    }
    void undo (Session& s) override { s.tracks.erase (s.tracks.begin() + index); }
    int getTrackIndex() const noexcept { return index; }
private:
    Track track;
    int index = -1;
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

class SetSynthParamsCommand final : public Command
{
public:
    SetSynthParamsCommand (int trackIndex, std::shared_ptr<const engine::SynthParams> p)
        : index (trackIndex), newParams (std::move (p)) {}
    juce::String getName() const override { return "Change Synth Sound"; }
    void execute (Session& s) override { auto& t = s.tracks[(size_t) index]; oldParams = t.synthParams; t.synthParams = newParams; }
    void undo (Session& s) override    { s.tracks[(size_t) index].synthParams = oldParams; }
private:
    int index;
    std::shared_ptr<const engine::SynthParams> newParams, oldParams;
};

} // namespace beatmaker::model
