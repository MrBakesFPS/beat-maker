// Import Session Data (Pro Tools): bring tracks, markers and the tempo from
// another session bundle into the open one, as one undoable command.
#pragma once

#include "SessionFile.h"
#include <Session.h>

namespace beatmaker::persistence
{

struct ImportOptions
{
    enum class Destination { newTracks, matchByName };   // matchByName appends clips to a same-named track of the same kind, else makes a new one
    std::vector<int> tracks;                              // indices into the source session
    Destination destination = Destination::newTracks;
    double offsetSeconds = 0.0;                           // 0 = maintain absolute time; e.g. the playhead
    bool importMarkers = false;
    bool importTempo = false;                             // caller applies `tempoOut` to the transport
    bool includeInserts = true, includeSends = true, includeAutomation = true, includePlaylists = true;
};

struct ImportSummary
{
    int tracksAdded = 0, tracksMerged = 0, clipsAdded = 0, markersAdded = 0;
    double sourceBpm = 120.0;
    int sourceBeatsPerBar = 4;
    juce::StringArray warnings;
};

class SessionImport
{
public:
    // Loads the source bundle into `source` (a scratch session) so the caller can list its tracks.
    static juce::String open (model::Session& source, TransportState& sourceTransport, const juce::File& bundle, const LoadContext&, juce::StringArray& warnings);

    // Builds the undoable command that applies `options` from `source` to `destination`. Null when nothing to import.
    static std::unique_ptr<model::Command> build (const model::Session& destination, const model::Session& source, double sourceSampleRate,
                                                  const ImportOptions&, ImportSummary&);
};

} // namespace beatmaker::persistence
