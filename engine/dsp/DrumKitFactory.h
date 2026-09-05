// Synthesises the built-in drum kit so the app makes sound with no sample
// library installed. Every pad is generated deterministically at the engine
// sample rate.
#pragma once

#include "DrumKit.h"

namespace beatmaker::engine
{

class DrumKitFactory
{
public:
    static std::shared_ptr<const DrumKit> createDefaultKit (double sampleRate);

    // Pad layout used by createDefaultKit and StepPattern::createDefaultBeat.
    enum Pad
    {
        kick = 0, snare, clap, rim, closedHat, openHat, lowTom, midTom,
        highTom, crash, ride, cowbell, shaker, clave, conga, sub
    };
};

} // namespace beatmaker::engine
