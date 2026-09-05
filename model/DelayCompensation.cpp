#include "DelayCompensation.h"

namespace beatmaker::model
{

int DelayCompensation::insertLatency (const Track& t)
{
    int total = 0;
    for (const auto& ins : t.inserts)
        if (! ins.isEmpty() && ins.params != nullptr)
            total += ins.instance->getLatencySamples (*ins.params);   // bypassed inserts still count: stable timing
    return total;
}

std::vector<StripDelayInfo> DelayCompensation::compute (const Session& s)
{
    const auto& tracks = s.getTracks();
    std::vector<StripDelayInfo> info (tracks.size());

    int maxSource = 0, maxAux = 0;
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        info[i].insertLatency = insertLatency (tracks[i]);
        info[i].userOffset = tracks[i].delayOffset;
        if (tracks[i].isAux()) maxAux = juce::jmax (maxAux, info[i].insertLatency);
        else                   maxSource = juce::jmax (maxSource, info[i].insertLatency);
    }

    if (! s.isDelayCompensationEnabled())
        return info;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& t = tracks[i];
        if (t.isAux())
            info[i].compensation = maxAux - info[i].insertLatency;
        else if (t.outputBus >= 0)
            info[i].compensation = maxSource - info[i].insertLatency;               // feeds an aux: align at the bus
        else
            info[i].compensation = maxSource - info[i].insertLatency + maxAux;      // straight to main: wait for the aux stage too
    }
    return info;
}

} // namespace beatmaker::model
