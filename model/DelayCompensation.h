// Automatic Delay Compensation: every strip is delayed so that all signals
// arrive at the mix aligned, whatever their inserts' latency.
//
//   source strips -> (bus | main)     aligned to  L_src = max source latency
//   aux strips    -> main             aligned to  L_src + L_aux (max aux latency)
//   source strips routed to main are delayed by the aux stage too, so a dry
//   track and the same track returning through a reverb aux stay in phase.
#pragma once

#include "Session.h"
#include <vector>

namespace beatmaker::model
{

struct StripDelayInfo
{
    int insertLatency = 0;   // samples added by this strip's inserts
    int compensation = 0;    // samples the engine adds to align it
    int userOffset = 0;      // manual +/- samples
    int total() const noexcept { return juce::jmax (0, compensation + userOffset); }
};

class DelayCompensation
{
public:
    static int insertLatency (const Track&);
    // One entry per track (track order). All zeros when compensation is off.
    static std::vector<StripDelayInfo> compute (const Session&);
};

} // namespace beatmaker::model
