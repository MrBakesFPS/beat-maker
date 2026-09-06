#include "SessionImport.h"
#include <ClipEdits.h>

namespace beatmaker::persistence
{

using namespace model;

juce::String SessionImport::open (Session& source, TransportState& sourceTransport, const juce::File& bundle, const LoadContext& ctx, juce::StringArray& warnings)
{
    return SessionFile::load (source, sourceTransport, bundle, ctx, warnings);
}

namespace
{
    void shiftLane (std::shared_ptr<const engine::AutomationLane>& lane, juce::int64 offset)
    {
        if (lane == nullptr || offset == 0) return;
        auto copy = std::make_shared<engine::AutomationLane> (*lane);
        for (auto& p : copy->points) p.time = juce::jmax<juce::int64> (0, p.time + offset);
        lane = copy;
    }

    Track shifted (Track t, juce::int64 offset, const ImportOptions& o)
    {
        for (auto& c : t.clips) c.timelineStart = juce::jmax<juce::int64> (0, c.timelineStart + offset);
        for (auto& alt : t.alternates) for (auto& c : alt.clips) c.timelineStart = juce::jmax<juce::int64> (0, c.timelineStart + offset);
        for (auto& c : t.patternClips) c.timelineStart = juce::jmax<juce::int64> (0, c.timelineStart + offset);
        for (auto& c : t.midiClips) c.timelineStart = juce::jmax<juce::int64> (0, c.timelineStart + offset);
        if (! o.includePlaylists) t.alternates.clear();
        if (! o.includeInserts) for (auto& ins : t.inserts) ins = Insert();
        if (! o.includeSends) for (auto& s : t.sends) s = Send();
        if (! o.includeAutomation) t.automation.clear();
        else for (auto& lane : t.automation) shiftLane (lane, offset);
        t.armed = false; t.punched = false; t.monitor = false; t.writing.clear(); t.volumeTrim = 1.0f;
        t.id = 0;   // a fresh id in the destination
        return t;
    }
}

std::unique_ptr<Command> SessionImport::build (const Session& destination, const Session& source, double sourceSampleRate,
                                               const ImportOptions& o, ImportSummary& summary)
{
    summary = {};
    summary.sourceBpm = source.getBpm();
    summary.sourceBeatsPerBar = source.getBeatsPerBar();
    const juce::int64 offset = (juce::int64) std::llround (o.offsetSeconds * sourceSampleRate);
    auto compound = std::make_unique<CompoundCommand> ("Import Session Data");

    // Map source track id -> destination track id for VCA assignments (only when the VCA comes along too)
    std::map<int, int> newIdFor;
    int nextId = 1;
    for (const auto& t : destination.getTracks()) nextId = juce::jmax (nextId, t.id + 1);
    std::vector<std::pair<int, Track>> newTracks;   // (source index, track)

    for (int si : o.tracks)
    {
        const auto* src = source.getTrack (si);
        if (src == nullptr) { summary.warnings.add ("No track " + juce::String (si + 1) + " in the source"); continue; }

        if (o.destination == ImportOptions::Destination::matchByName)
        {
            int match = -1;
            for (int d = 0; d < destination.getNumTracks(); ++d)
            {
                const auto& dt = destination.getTracks()[(size_t) d];
                if (dt.name == src->name && dt.type == src->type && dt.instrumentKind == src->instrumentKind) { match = d; break; }
            }
            if (match >= 0)
            {
                const auto t = shifted (*src, offset, o);
                for (const auto& c : t.clips)        { compound->add (std::make_unique<AddClipCommand> (match, c)); ++summary.clipsAdded; }
                for (const auto& c : t.patternClips) { compound->add (std::make_unique<AddPatternClipCommand> (match, c)); ++summary.clipsAdded; }
                for (const auto& c : t.midiClips)    { compound->add (std::make_unique<AddMidiClipCommand> (match, c)); ++summary.clipsAdded; }
                ++summary.tracksMerged;
                continue;
            }
        }
        auto t = shifted (*src, offset, o);
        t.id = nextId++;
        newIdFor[src->id] = t.id;
        summary.clipsAdded += (int) (t.clips.size() + t.patternClips.size() + t.midiClips.size());
        newTracks.emplace_back (si, std::move (t));
    }

    for (auto& [si, t] : newTracks)
    {
        // VCA assignment survives only if the VCA master is imported in the same pass
        if (t.vcaTrackId >= 0)
        {
            const auto it = newIdFor.find (t.vcaTrackId);
            t.vcaTrackId = it != newIdFor.end() ? it->second : -1;
        }
        compound->add (std::make_unique<AddTrackCommand> (t));
        ++summary.tracksAdded;
    }

    if (o.importMarkers)
        for (const auto& m : source.getMarkers())
        {
            bool exists = false;
            for (const auto& dm : destination.getMarkers()) if (dm.name == m.name && std::abs (dm.seconds - (m.seconds + o.offsetSeconds)) < 1e-6) exists = true;
            if (exists) continue;
            Marker copy = m;
            copy.id = 0;
            copy.seconds += o.offsetSeconds; if (copy.isRange()) copy.endSeconds += o.offsetSeconds;
            if (copy.recallSelection) { copy.selectionStart += o.offsetSeconds; copy.selectionEnd += o.offsetSeconds; }
            compound->add (std::make_unique<AddMarkerCommand> (copy));
            ++summary.markersAdded;
        }

    return compound->isEmpty() ? nullptr : std::move (compound);
}

} // namespace beatmaker::persistence
