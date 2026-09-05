// Clip editing: a kind-agnostic handle to any clip (audio, pattern, MIDI)
// plus the undoable commands behind the edit tools: move, trim, split,
// duplicate, remove, shuffle re-pack, and a compound wrapper so a
// multi-step edit (e.g. move + shuffle) is one undo entry.
#pragma once

#include "Session.h"
#include <optional>

namespace beatmaker::model
{

struct ClipRef
{
    enum class Kind { audio, pattern, midi };
    int track = -1;
    Kind kind = Kind::audio;
    int index = -1;

    bool isValid() const noexcept { return track >= 0 && index >= 0; }
    bool operator== (const ClipRef& o) const noexcept { return track == o.track && kind == o.kind && index == o.index; }
    bool operator!= (const ClipRef& o) const noexcept { return ! (*this == o); }
};

// Timing common to every clip kind.
struct ClipTiming
{
    juce::int64 start = 0;
    juce::int64 length = 0;
    juce::int64 offset = 0;      // sourceOffset for audio, loopOffset for pattern/MIDI
    juce::int64 maxLength = 0;   // audio: samples available after offset; 0 = unbounded (looping clips)
    double sampleRate = 44100.0;
    juce::String name;
};

class ClipEdits
{
public:
    static std::optional<ClipTiming> timing (const Session&, const ClipRef&);
    static ClipRef::Kind kindForTrack (const Track&);
    static int numClips (const Track&, ClipRef::Kind);
    static std::vector<ClipRef> allClips (const Session&, int trackIndex);

    // Which clip (if any) covers `sample` on a track.
    static std::optional<ClipRef> clipAt (const Session&, int trackIndex, juce::int64 sample);

    // Can `ref` live on `trackIndex`? (audio clips need audio tracks etc.)
    static bool canPlaceOn (const Session&, const ClipRef&, int trackIndex);
};

//==============================================================================

class CompoundCommand final : public Command
{
public:
    explicit CompoundCommand (juce::String actionName) : name (std::move (actionName)) {}
    void add (std::unique_ptr<Command> c) { if (c) commands.push_back (std::move (c)); }
    bool isEmpty() const noexcept { return commands.empty(); }
    juce::String getName() const override { return name; }
    void execute (Session& s) override { for (auto& c : commands) c->execute (s); }
    void undo (Session& s) override    { for (auto it = commands.rbegin(); it != commands.rend(); ++it) (*it)->undo (s); }
private:
    std::vector<std::unique_ptr<Command>> commands;
    juce::String name;
};

// Move to a new start and/or track of a compatible kind. The clip keeps its
// content; on a different track it is appended to that track's clip list.
class MoveClipCommand final : public Command
{
public:
    MoveClipCommand (ClipRef ref, int newTrack, juce::int64 newStart);
    juce::String getName() const override { return "Move Clip"; }
    void execute (Session&) override;
    void undo (Session&) override;
    ClipRef getResultingRef() const noexcept { return result; }
private:
    ClipRef ref, result;
    int newTrack;
    juce::int64 newStart, oldStart = 0;
};

// Change start and/or end. Start trims adjust the offset so the content
// stays anchored to the timeline (non-destructive).
class TrimClipCommand final : public Command
{
public:
    TrimClipCommand (ClipRef ref, juce::int64 newStart, juce::int64 newLength);
    juce::String getName() const override { return "Trim Clip"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    ClipRef ref;
    juce::int64 newStart, newLength;
    ClipTiming old;
    juce::int64 oldFadeIn = 0, oldFadeOut = 0;
};

class RemoveAnyClipCommand final : public Command
{
public:
    explicit RemoveAnyClipCommand (ClipRef r) : ref (r) {}
    juce::String getName() const override { return "Delete Clip"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    ClipRef ref;
    std::optional<AudioClip> audio;
    std::optional<PatternClip> pattern;
    std::optional<MidiClip> midi;
};

// Separate a clip at an absolute sample: the original is shortened and a new
// clip carrying the remainder is appended to the track.
class SplitClipCommand final : public Command
{
public:
    SplitClipCommand (ClipRef r, juce::int64 atSample) : ref (r), at (atSample) {}
    juce::String getName() const override { return "Separate Clip"; }
    void execute (Session&) override;
    void undo (Session&) override;
    ClipRef getSecondHalf() const noexcept { return second; }
private:
    ClipRef ref, second;
    juce::int64 at;
    juce::int64 oldLength = 0, oldFadeOut = 0;
    bool didSplit = false;
};

// Append a copy of the clip immediately after itself.
class DuplicateClipCommand final : public Command
{
public:
    explicit DuplicateClipCommand (ClipRef r) : ref (r) {}
    juce::String getName() const override { return "Duplicate Clip"; }
    void execute (Session&) override;
    void undo (Session&) override;
    ClipRef getCopy() const noexcept { return copy; }
private:
    ClipRef ref, copy;
};

// Audio clips only: set fade lengths (samples) and shapes. Lengths are
// clamped so the fades fit the clip.
class SetClipFadesCommand final : public Command
{
public:
    SetClipFadesCommand (ClipRef r, juce::int64 fadeIn, engine::FadeShape newInShape, juce::int64 fadeOut, engine::FadeShape newOutShape)
        : ref (r), in (fadeIn), out (fadeOut), inShape (newInShape), outShape (newOutShape) {}
    juce::String getName() const override { return "Fades"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    ClipRef ref;
    juce::int64 in, out, oldIn = 0, oldOut = 0;
    engine::FadeShape inShape, outShape, oldInShape = engine::FadeShape::linear, oldOutShape = engine::FadeShape::linear;
};

// Audio clips only: static clip gain (linear, clamped to -inf..+12 dB).
class SetClipGainCommand final : public Command
{
public:
    SetClipGainCommand (ClipRef r, float linearGain) : ref (r), gain (juce::jlimit (0.0f, 4.0f, linearGain)) {}
    juce::String getName() const override { return "Clip Gain"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    ClipRef ref;
    float gain, oldGain = 1.0f;
};

// Shuffle mode: lay a track's clips end to end in start order, keeping the
// earliest clip where it is.
class RepackTrackCommand final : public Command
{
public:
    explicit RepackTrackCommand (int trackIndex) : track (trackIndex) {}
    juce::String getName() const override { return "Shuffle"; }
    void execute (Session&) override;
    void undo (Session&) override;
private:
    int track;
    std::vector<std::pair<ClipRef, juce::int64>> oldStarts;
};

} // namespace beatmaker::model
