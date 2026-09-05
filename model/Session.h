// Session: the document. Tracks contain clips; clips reference immutable
// audio buffers or step patterns. Non-destructive by design: source audio is
// never modified, and shared engine data is replaced (copy-on-write), never
// edited in place.
#pragma once

#include "Command.h"

#include <dsp/DrumKit.h>
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
    float gain = 1.0f;

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
    float gain = 1.0f;

    double getStartSeconds() const noexcept  { return (double) timelineStart / sampleRate; }
    double getLengthSeconds() const noexcept { return (double) length / sampleRate; }
    double getEndSeconds() const noexcept    { return getStartSeconds() + getLengthSeconds(); }
};

struct Track
{
    enum class Type { audio, instrument, aux, master };
    enum class InstrumentKind { none, drumMachine, synth };

    int id = 0;                       // stable identity, assigned by the Session
    juce::String name;
    Type type = Type::audio;
    juce::Colour colour { 0xff3498db };

    std::vector<AudioClip> clips;                 // audio tracks

    InstrumentKind instrumentKind = InstrumentKind::none;
    std::vector<PatternClip> patternClips;        // drum machine tracks
    std::shared_ptr<const engine::DrumKit> drumKit;
    std::vector<MidiClip> midiClips;              // synth tracks
    std::shared_ptr<const engine::SynthParams> synthParams;

    float gain = 1.0f;       // linear, 0..2
    float pan = 0.0f;        // -1..1
    bool mute = false;
    bool solo = false;

    // Recording (audio tracks)
    bool armed = false;      // record-enabled
    bool monitor = false;    // pass the input straight to the outputs
    int firstInput = 0;      // device input channel
    int numInputs = 1;       // 1 mono, 2 stereo pair

    bool isInstrument() const noexcept  { return type == Type::instrument; }
    bool isDrumMachine() const noexcept { return isInstrument() && instrumentKind == InstrumentKind::drumMachine; }
    bool isSynth() const noexcept       { return isInstrument() && instrumentKind == InstrumentKind::synth; }
    bool isAudio() const noexcept       { return type == Type::audio; }
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

    void notify() { listeners.call ([this] (Listener& l) { l.sessionChanged (*this); }); }

    std::vector<Track> tracks;
    int nextTrackId = 1;
    double bpm = 120.0;
    int beatsPerBar = 4;
    CommandHistory history;
    juce::ListenerList<Listener> listeners;
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
        auto& t = s.tracks[(size_t) index];
        oldGain = t.gain; oldPan = t.pan;
        t.gain = gain; t.pan = pan;
    }
    void undo (Session& s) override { auto& t = s.tracks[(size_t) index]; t.gain = oldGain; t.pan = oldPan; }
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
