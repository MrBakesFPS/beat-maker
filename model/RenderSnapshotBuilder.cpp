#include "RenderSnapshotBuilder.h"

namespace beatmaker::model
{

std::unique_ptr<engine::RenderSnapshot> buildRenderSnapshot (const Session& session)
{
    auto snapshot = std::make_unique<engine::RenderSnapshot>();

    bool anySolo = false;
    for (const auto& t : session.getTracks())
        anySolo = anySolo || t.solo;

    for (const auto& track : session.getTracks())
    {
        const bool audible = ! track.mute && (! anySolo || track.solo);

        if (track.isAudio() && track.monitor && audible)
            snapshot->monitors.push_back ({ track.firstInput, track.numInputs, track.gain });

        for (const auto& clip : track.clips)
        {
            if (! audible || clip.audio == nullptr)
                continue;

            engine::RenderClip rc;
            rc.audio         = clip.audio;
            rc.timelineStart = clip.timelineStart;
            rc.sourceOffset  = clip.sourceOffset;
            rc.length        = clip.length;
            rc.gain          = clip.gain * track.gain;
            snapshot->clips.push_back (std::move (rc));
        }

        // Pattern entries are kept even when muted (with zero length) so the
        // kit stays alive for pad previews; a zero-length pattern never fires.
        for (const auto& clip : track.patternClips)
        {
            if (clip.pattern == nullptr || track.drumKit == nullptr)
                continue;

            engine::RenderPattern rp;
            rp.pattern       = clip.pattern;
            rp.kit           = track.drumKit;
            rp.timelineStart = clip.timelineStart;
            rp.length        = audible ? clip.length : 0;
            rp.gain          = clip.gain * track.gain;
            snapshot->patterns.push_back (std::move (rp));
        }
    }

    return snapshot;
}

} // namespace beatmaker::model
