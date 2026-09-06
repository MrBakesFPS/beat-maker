// Track freeze, commit and stem export: snapshots that isolate one track's
// output for an offline render, and the commands that apply the result.
#pragma once

#include "Session.h"
#include <graph/RenderSnapshot.h>

namespace beatmaker::model
{

class Freeze
{
public:
    // The track's post-insert, pre-fader output: every other strip muted, the
    // target at unity/centre with sends off and fader/pan/mute automation
    // ignored (insert automation stays), no master inserts, master at unity.
    // `latencyToTrim` = samples the rendered audio arrives late by (the
    // track's own insert latency) so the caller can drop them.
    static std::unique_ptr<engine::RenderSnapshot> renderSnapshotForTrack (const Session&, int trackIndex, int& latencyToTrim);

    // A stem: what the track contributes to the main mix (post-fader, with
    // automation and sends). Other source strips are muted; aux returns are
    // muted unless included; the master's inserts are dropped unless asked for.
    static std::unique_ptr<engine::RenderSnapshot> stemSnapshot (const Session&, int trackIndex, bool includeAuxReturns, bool throughMasterInserts);

    // Tracks that can be frozen / exported as stems (audio, instrument, aux).
    static std::vector<int> renderableTracks (const Session&);
    static bool canFreeze (const Track& t) { return t.isAudio() || t.isInstrument(); }

    // Commit: a new audio track after `trackIndex` holding the render, and the source muted.
    static std::unique_ptr<Command> commitCommand (const Session&, int trackIndex, std::shared_ptr<const juce::AudioBuffer<float>> rendered,
                                                   double sampleRate, const juce::File& file, bool muteSource = true);
};

} // namespace beatmaker::model
