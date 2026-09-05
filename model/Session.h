// Session: the document. Tracks contain clips; clips reference immutable
// audio buffers or step patterns. Non-destructive by design: source audio is
// never modified, and shared engine data is replaced (copy-on-write), never
// edited in place.
#pragma once

#include "Command.h"

#include <dsp/DrumKit.h>
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

struct Track
{
    enum class Type { audio, instrument, aux, master };

    int id = 0;                       // stable identity, assigned by the Session
    juce::String name;
    Type type = Type::audio;
    juce::Colour colour { 0xff3498db };

    std::vector<AudioClip> clips;                 // audio tracks
    std::vector<PatternClip> patternClips;        // instrument tracks
    std::shared_ptr<const engine::DrumKit> drumKit; // instrument tracks

    float gain = 1.0f;
    bool mute = false;
    bool solo = false;

    bool isInstrument() const noexcept { return type == Type::instrument; }
    bool hasContent() const noexcept   { return ! clips.empty() || ! patternClips.empty(); }
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
    friend class AddPatternClipCommand;
    friend class SetStepCommand;
    friend class SetPadSampleCommand;

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
    enum class Flag { mute, solo };
    SetTrackFlagCommand (int trackIndex, Flag f, bool value) : index (trackIndex), flag (f), newValue (value) {}
    juce::String getName() const override { return flag == Flag::mute ? "Mute" : "Solo"; }
    void execute (Session& s) override { auto& t = s.tracks[(size_t) index]; auto& v = ref (t); oldValue = v; v = newValue; }
    void undo (Session& s) override    { ref (s.tracks[(size_t) index]) = oldValue; }
private:
    bool& ref (Track& t) const noexcept { return flag == Flag::mute ? t.mute : t.solo; }
    int index; Flag flag; bool newValue, oldValue = false;
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

} // namespace beatmaker::model
