// Playlist helpers: comping a range from an alternate into the main playlist,
// and splitting a loop-recorded file into per-pass takes.
#pragma once

#include "Session.h"
#include <vector>

namespace beatmaker::model
{

class Playlists
{
public:
    // Clips of `source` trimmed to [start, end); pieces outside are dropped,
    // pieces overlapping the edges are trimmed (sourceOffset adjusted, fades
    // cleared at the cut).
    static std::vector<AudioClip> sliceRange (const std::vector<AudioClip>& source, juce::int64 start, juce::int64 end);

    // Everything in `main` except [start, end) (clips spanning an edge are trimmed/split).
    static std::vector<AudioClip> clearRange (const std::vector<AudioClip>& main, juce::int64 start, juce::int64 end);

    // The main playlist with [start, end) replaced by the same range of `alternate`.
    static std::vector<AudioClip> comp (const std::vector<AudioClip>& main, const std::vector<AudioClip>& alternate,
                                        juce::int64 start, juce::int64 end);

    // Loop recording: a continuous file recorded from `recordStart` while the
    // transport cycled [loopStart, loopEnd) splits into passes.
    struct Pass { juce::int64 fileOffset; juce::int64 length; juce::int64 timelineStart; };
    static std::vector<Pass> loopPasses (juce::int64 recordStart, juce::int64 numSamples, juce::int64 loopStart, juce::int64 loopEnd,
                                         juce::int64 minimumLength = 0);
};

} // namespace beatmaker::model
