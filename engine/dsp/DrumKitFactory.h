// Synthesises the built-in drum kit so the app makes sound with no sample
// library installed. Every pad is generated deterministically at the engine
// sample rate.
#pragma once

#include "DrumKit.h"
#include <vector>

namespace beatmaker::engine
{

class DrumKitFactory
{
public:
    // The bundled kits, in display order. Every kit uses the same 16-pad layout (Pad below), so the starter beat
    // and the Smart Controls groups (Kick, Snare, Hats, ...) mean the same thing on each.
    struct KitInfo { juce::String name, category, description; bool custom = false; };
    static const std::vector<KitInfo>& availableKits();                  // bundled kits, then the user's own
    static std::vector<juce::String> categories();                       // in display order
    static const KitInfo* info (const juce::String& name);               // null when unknown
    static const char* defaultKitName() { return "Studio Kit"; }
    static const char* customCategory() { return "My Kits"; }

    // Synthesises a kit at the engine sample rate; an unknown name gives the default kit.
    static std::shared_ptr<const DrumKit> createKit (const juce::String& name, double sampleRate);

    // A kit of the user's own: each pad taken from a pad of any kit (bundled or custom, no cycles are made).
    struct PadSource { juce::String kit; int pad = 0; };
    struct CustomKit { juce::String name; std::array<PadSource, DrumKit::numPads> pads; };
    static std::shared_ptr<const DrumKit> createCustomKit (const CustomKit&, double sampleRate);
    // The registered custom kits appear in availableKits() under "My Kits" and resolve by name in createKit
    static void setCustomKits (std::vector<CustomKit>);
    static const std::vector<CustomKit>& customKits();
    static const CustomKit* customKit (const juce::String& name);
    static std::shared_ptr<const DrumKit> createDefaultKit (double sampleRate) { return createKit (defaultKitName(), sampleRate); }

    // Pad layout used by createDefaultKit and StepPattern::createDefaultBeat.
    enum Pad
    {
        kick = 0, snare, clap, rim, closedHat, openHat, lowTom, midTom,
        highTom, crash, ride, cowbell, shaker, clave, conga, sub
    };
};

} // namespace beatmaker::engine
