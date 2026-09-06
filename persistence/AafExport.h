// AafExport: writes the session as an Advanced Authoring Format file (the
// interchange format Pro Tools, Media Composer, Nuendo and Resolve read):
// one timeline slot per track channel, clips as SourceClips into master and
// source mobs, audio embedded as PCM essence or linked to WAV files beside
// the AAF, a timecode slot and markers.
#pragma once

#include <Session.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace beatmaker::persistence
{

struct AafExportOptions
{
    std::vector<int> tracks;            // empty = every exportable track
    bool embedAudio = true;             // false: WAV files in "<name> Media" next to the AAF
    bool consolidateClips = true;       // render clip gain, gain line and fades into each clip's audio (no handles)
                                        // false: whole source files with offsets; gain and fades are not carried
    int bitDepth = 24;                  // 16 or 24
    double timecodeFps = 30.0;          // 24, 25, 29.97 (drop) or 30
    bool includeMarkers = true;
};

struct AafExportSummary
{
    juce::String error;
    int tracks = 0, slots = 0, clips = 0, essences = 0, markers = 0;
    juce::int64 fileBytes = 0;
    std::vector<juce::File> mediaFiles;
    bool ok() const noexcept { return error.isEmpty(); }
};

class AafExport
{
public:
    // Audio tracks with clips and frozen instrument tracks (their render is the clip).
    static std::vector<int> exportableTracks (const model::Session&);

    static AafExportSummary write (const model::Session&, double sampleRate, const juce::String& sessionName,
                                   const juce::File& aafFile, const AafExportOptions&);
};

} // namespace beatmaker::persistence
