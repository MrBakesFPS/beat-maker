#include "RenderSnapshotBuilder.h"
#include "DelayCompensation.h"

namespace beatmaker::model
{

namespace
{
    std::vector<engine::RenderInsert> renderInserts (const Track& t)
    {
        std::vector<engine::RenderInsert> out;
        for (int slot = 0; slot < (int) t.inserts.size(); ++slot)
        {
            const auto& ins = t.inserts[(size_t) slot];
            if (! ins.isEmpty() && ins.params != nullptr)
                out.push_back ({ ins.instance, ins.params, ins.bypass, slot, ins.keyBus, ins.keyListen });
        }
        return out;
    }
}

std::vector<int> computeStripOrder (const std::vector<engine::RenderStrip>& strips)
{
    const int n = (int) strips.size();
    // feeds[i] = buses strip i sends to (sends, or its output); keyed[j] = buses strip j's inserts are keyed from
    auto feedsBus = [&] (int i, int bus)
    {
        const auto& s = strips[(size_t) i];
        if (s.outputBus == bus) return true;
        for (const auto& send : s.sends) if (send.bus == bus) return true;
        return false;
    };
    std::vector<std::vector<int>> dependsOn ((size_t) n);   // j depends on i: i must run first
    for (int j = 0; j < n; ++j)
        for (const auto& ins : strips[(size_t) j].inserts)
            if (ins.keyBus >= 0 && ins.fx != nullptr && ins.fx->acceptsSidechain())
                for (int i = 0; i < n; ++i)
                    if (i != j && strips[(size_t) i].isAux == strips[(size_t) j].isAux && feedsBus (i, ins.keyBus))
                        dependsOn[(size_t) j].push_back (i);

    // Kahn's algorithm, stable: among ready strips the lowest index goes first.
    std::vector<int> order, indegree ((size_t) n, 0);
    std::vector<bool> placed ((size_t) n, false);
    for (int j = 0; j < n; ++j) indegree[(size_t) j] = (int) dependsOn[(size_t) j].size();
    for (int placedCount = 0; placedCount < n; )
    {
        int pick = -1;
        for (int j = 0; j < n; ++j) if (! placed[(size_t) j] && indegree[(size_t) j] == 0) { pick = j; break; }
        if (pick < 0)   // cycle: fall back to the first unplaced strip (its key sees the previous state)
            for (int j = 0; j < n; ++j) if (! placed[(size_t) j]) { pick = j; break; }
        placed[(size_t) pick] = true;
        order.push_back (pick);
        ++placedCount;
        for (int j = 0; j < n; ++j)
            if (! placed[(size_t) j])
                for (int dep : dependsOn[(size_t) j]) if (dep == pick) --indegree[(size_t) j];
    }
    return order;
}

std::unique_ptr<engine::RenderSnapshot> buildRenderSnapshot (const Session& session)
{
    auto snapshot = std::make_unique<engine::RenderSnapshot>();

    const auto& all = session.getTracks();
    auto vcaOf = [&] (const Track& t) -> const Track*
    {
        const int i = session.indexOfTrackId (t.vcaTrackId);
        return (i >= 0 && all[(size_t) i].isVca()) ? &all[(size_t) i] : nullptr;
    };

    // Solo logic: a VCA's solo solos its members; a VCA's mute mutes them.
    bool anySolo = false;
    for (const auto& t : all)
        anySolo = anySolo || (t.solo && ! t.isVca()) || (t.isVca() && t.solo);

    const auto& tracks = session.getTracks();
    const auto delays = DelayCompensation::compute (session);
    const auto& io = session.getIO();
    if (auto* main = io.output (0)) snapshot->mainOutputChannel = main->firstChannel;

    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const auto& track = tracks[(size_t) i];
        const auto* vca = vcaOf (track);
        const bool soloed = track.solo || (vca != nullptr && vca->solo);
        const bool muted = track.mute || (vca != nullptr && vca->mute);
        const bool audible = ! muted && (! anySolo || soloed) && ! track.isVca();

        // ---- Channel strip (one per track, same index) ----
        engine::RenderStrip strip;
        strip.isVca     = track.isVca();
        strip.vcaStrip  = vca != nullptr ? session.indexOfTrackId (vca->id) : -1;
        strip.trackId   = track.id;
        strip.isAux     = track.isAux();
        strip.inputBus  = track.inputBus;
        strip.outputBus = track.outputBus;
        strip.gain      = track.gain;
        strip.pan       = track.pan;
        strip.muted     = ! audible;
        strip.inserts   = renderInserts (track);
        for (int slot = 0; slot < (int) track.sends.size(); ++slot)
        {
            const auto& send = track.sends[(size_t) slot];
            if (send.isActive())
                strip.sends.push_back ({ send.bus, send.gain, send.preFader, slot });
        }
        strip.delaySamples = delays[(size_t) i].total();
        if (track.outputBus < 0 && track.outputPath > 0)
            if (auto* path = io.output (track.outputPath)) strip.outputChannel = path->firstChannel;
        strip.automationRead = track.automationMode != AutomationMode::off;
        strip.trimGain = track.volumeTrim;
        for (const auto& lane : track.automation)
            if (lane != nullptr && ! lane->isEmpty())
                strip.automation.push_back ({ lane, track.isWriting (lane->param) });
        snapshot->strips.push_back (std::move (strip));

        // ---- Sources ----
        if (track.isAudio() && track.monitor && audible)
        {
            const auto [first, num] = session.resolveInput (track);
            snapshot->monitors.push_back ({ first, num, 1.0f, i });
        }

        for (const auto& clip : track.clips)
        {
            if (clip.audio == nullptr)
                continue;

            engine::RenderClip rc;
            rc.audio         = clip.audio;
            rc.timelineStart = clip.timelineStart;
            rc.sourceOffset  = clip.sourceOffset;
            rc.length        = clip.length;
            rc.gain          = clip.gain;
            rc.strip         = i;
            rc.fadeIn        = clip.fadeIn;
            rc.fadeOut       = clip.fadeOut;
            rc.fadeInShape   = clip.fadeInShape;
            rc.fadeOutShape  = clip.fadeOutShape;
            rc.gainLane      = clip.gainLane;
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
            rp.loopOffset    = clip.loopOffset;
            rp.gain          = clip.gain;
            rp.strip         = i;
            snapshot->patterns.push_back (std::move (rp));
        }

        if (track.hasInstrument())
        {
            snapshot->instruments.push_back ({ track.id, track.instrument, track.instrumentParams, i });

            for (const auto& clip : track.midiClips)
            {
                if (clip.sequence == nullptr) continue;
                engine::RenderMidiClip rm;
                rm.sequence      = clip.sequence;
                rm.instrumentId  = track.id;
                rm.timelineStart = clip.timelineStart;
                rm.length        = audible ? clip.length : 0;
                rm.loopOffset    = clip.loopOffset;
                rm.gain          = clip.gain;
                rm.props         = track.midiProps;
                snapshot->midiClips.push_back (std::move (rm));
            }
        }
    }

    // ---- Strip order: sidechain senders before the strips they key ----
    snapshot->stripOrder = computeStripOrder (snapshot->strips);

    // ---- Master ----
    const auto& master = session.getMaster();
    snapshot->master.gain = master.gain;
    snapshot->master.inserts = renderInserts (master);
    snapshot->master.automationRead = master.automationMode != AutomationMode::off;
    snapshot->master.trimGain = master.volumeTrim;
    for (const auto& lane : master.automation)
        if (lane != nullptr && ! lane->isEmpty())
            snapshot->master.automation.push_back ({ lane, master.isWriting (lane->param) });

    return snapshot;
}

} // namespace beatmaker::model
