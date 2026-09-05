// Elastic audio on clips: re-rendering a clip through a StretchSpec as one
// undoable command, plus the Pro Tools-style operations built on it (TCE
// trim, conform to tempo, quantize to grid, warp markers) and the Beat
// Detective-style helpers that share the transient detector.
#pragma once

#include "Session.h"
#include "ClipEdits.h"
#include <dsp/TimeStretch.h>

namespace beatmaker::model
{

// Replaces a clip's stretch spec and rendered audio. Offsets, length, fades
// and the gain line are remapped from the old rendering to the new one so the
// visible clip keeps its start and covers the same source material. The clip
// keeps its timeline start (TCE anchors the left edge).
class SetClipElasticCommand final : public Command
{
public:
    // The rendering happens in the constructor (caller's thread).
    SetClipElasticCommand (const Session&, ClipRef, engine::StretchSpec, juce::String name = "Elastic Audio",
                           engine::TimeStretch::ProgressFn progress = {});
    juce::String getName() const override { return name; }
    void execute (Session&) override;
    void undo (Session&) override;
    bool wasCancelled() const noexcept { return cancelled; }

private:
    ClipRef ref;
    juce::String name;
    bool cancelled = false;
    AudioClip before, after;   // full snapshots of the clip's audio-related state
};

class Elastic
{
public:
    // Rebuilds `spec` for a clip after a change of mode/pitch keeping its stretch.
    static engine::StretchSpec withMode (const AudioClip&, engine::StretchMode);
    static engine::StretchSpec withPitch (const AudioClip&, double semitones);

    // TCE: stretch so the clip's visible length becomes `newLength` (rendered samples).
    static engine::StretchSpec forVisibleLength (const AudioClip&, juce::int64 newLength);

    // Conform: stretch so audio recorded at clip.sourceBpm plays at `sessionBpm`.
    static engine::StretchSpec forTempo (const AudioClip&, double sessionBpm, engine::StretchMode fallbackMode = engine::StretchMode::polyphonic);

    // Warp markers, positions given in rendered samples (relative to the audio, not the clip).
    static engine::StretchSpec withMarkerAt (const AudioClip&, juce::int64 renderedSample);
    static engine::StretchSpec withMarkerMoved (const AudioClip&, int markerIndex, juce::int64 newRenderedSample);
    static engine::StretchSpec withoutMarker (const AudioClip&, int markerIndex);
    static engine::StretchSpec withoutMarkers (const AudioClip&);

    // Transients of the clip's visible region, as rendered samples relative to the audio.
    static std::vector<juce::int64> transients (const AudioClip&, float sensitivity = 0.5f);

    // Quantize: warp every transient inside the clip to the nearest grid line.
    // `strength` 1 = all the way. Returns nullopt when nothing to do.
    static std::optional<engine::StretchSpec> quantizeToGrid (const AudioClip&, double bpm, double gridBeats,
                                                              float strength = 1.0f, float sensitivity = 0.5f);

    // Beat Detective-style: separate a clip at its transients (one undo step).
    static std::unique_ptr<Command> separateAtTransients (const Session&, const ClipRef&, float sensitivity = 0.5f);

    // Tab-to-transient: next/previous transient after `sample` across the
    // audio clips of `trackIndex` (-1 = every audio track). Timeline samples.
    static std::optional<juce::int64> nextTransient (const Session&, int trackIndex, juce::int64 sample, bool forward, float sensitivity = 0.5f);
};

} // namespace beatmaker::model
