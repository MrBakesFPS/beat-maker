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
        if (! audible)
            continue;

        for (const auto& clip : track.clips)
        {
            if (clip.audio == nullptr)
                continue;

            engine::RenderClip rc;
            rc.audio         = clip.audio;
            rc.timelineStart = clip.timelineStart;
            rc.sourceOffset  = clip.sourceOffset;
            rc.length        = clip.length;
            rc.gain          = clip.gain * track.gain;
            snapshot->clips.push_back (std::move (rc));
        }
    }

    return snapshot;
}

} // namespace beatmaker::model
