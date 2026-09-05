#include "Playlists.h"

namespace beatmaker::model
{

std::vector<AudioClip> Playlists::sliceRange (const std::vector<AudioClip>& source, juce::int64 start, juce::int64 end)
{
    std::vector<AudioClip> out;
    for (const auto& c : source)
    {
        const juce::int64 cStart = c.timelineStart, cEnd = c.timelineStart + c.length;
        const juce::int64 from = juce::jmax (cStart, start), to = juce::jmin (cEnd, end);
        if (from >= to) continue;
        AudioClip piece = c;
        piece.timelineStart = from;
        piece.sourceOffset = c.sourceOffset + (from - cStart);
        piece.length = to - from;
        if (from > cStart) piece.fadeIn = 0;
        if (to < cEnd) piece.fadeOut = 0;
        piece.clampFades();
        out.push_back (piece);
    }
    return out;
}

std::vector<AudioClip> Playlists::clearRange (const std::vector<AudioClip>& main, juce::int64 start, juce::int64 end)
{
    std::vector<AudioClip> out;
    for (const auto& c : main)
    {
        const juce::int64 cStart = c.timelineStart, cEnd = c.timelineStart + c.length;
        if (cEnd <= start || cStart >= end) { out.push_back (c); continue; }

        if (cStart < start)   // head survives
        {
            AudioClip head = c;
            head.length = start - cStart;
            head.fadeOut = 0;
            head.clampFades();
            out.push_back (head);
        }
        if (cEnd > end)       // tail survives
        {
            AudioClip tail = c;
            tail.timelineStart = end;
            tail.sourceOffset = c.sourceOffset + (end - cStart);
            tail.length = cEnd - end;
            tail.fadeIn = 0;
            tail.clampFades();
            out.push_back (tail);
        }
    }
    return out;
}

std::vector<AudioClip> Playlists::comp (const std::vector<AudioClip>& main, const std::vector<AudioClip>& alternate,
                                        juce::int64 start, juce::int64 end)
{
    auto out = clearRange (main, start, end);
    for (auto& piece : sliceRange (alternate, start, end)) out.push_back (std::move (piece));
    std::sort (out.begin(), out.end(), [] (const AudioClip& a, const AudioClip& b) { return a.timelineStart < b.timelineStart; });
    return out;
}

std::vector<Playlists::Pass> Playlists::loopPasses (juce::int64 recordStart, juce::int64 numSamples, juce::int64 loopStart, juce::int64 loopEnd,
                                                    juce::int64 minimumLength)
{
    std::vector<Pass> passes;
    const juce::int64 loopLength = loopEnd - loopStart;
    if (numSamples <= 0) return passes;
    if (loopLength <= 0 || recordStart < loopStart || recordStart >= loopEnd)
    {
        passes.push_back ({ 0, numSamples, recordStart });   // not looping: one take
        return passes;
    }

    juce::int64 fileOffset = 0;
    juce::int64 timelineStart = recordStart;
    while (fileOffset < numSamples)
    {
        const juce::int64 passLength = juce::jmin (numSamples - fileOffset, loopEnd - timelineStart);
        if (passLength >= juce::jmax<juce::int64> (1, minimumLength) || passes.empty())
            passes.push_back ({ fileOffset, passLength, timelineStart });
        fileOffset += passLength;
        timelineStart = loopStart;
    }
    return passes;
}

} // namespace beatmaker::model
