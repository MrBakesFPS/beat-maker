// IOSetup: named signal paths mapped to device channels, like Pro Tools'
// I/O Setup window (Input / Output / Bus tabs).
#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace beatmaker::model
{

struct IOPath
{
    juce::String name;
    int firstChannel = 0;   // device channel index
    int numChannels = 2;    // 1 mono, 2 stereo
    bool operator== (const IOPath& o) const noexcept { return name == o.name && firstChannel == o.firstChannel && numChannels == o.numChannels; }
};

struct IOSetup
{
    std::vector<IOPath> inputs;          // e.g. "In 1" (mono), "In 1-2" (stereo)
    std::vector<IOPath> outputs;         // outputs[0] is always the Main path
    std::vector<juce::String> busNames;  // one per internal bus

    static constexpr int numBuses = 8;

    // Sensible paths for a device with the given channel counts.
    static IOSetup createDefault (int numInputChannels, int numOutputChannels);

    const IOPath* input (int index) const noexcept  { return juce::isPositiveAndBelow (index, (int) inputs.size()) ? &inputs[(size_t) index] : nullptr; }
    const IOPath* output (int index) const noexcept { return juce::isPositiveAndBelow (index, (int) outputs.size()) ? &outputs[(size_t) index] : nullptr; }
    juce::String busName (int bus) const
    {
        if (juce::isPositiveAndBelow (bus, (int) busNames.size()) && busNames[(size_t) bus].isNotEmpty()) return busNames[(size_t) bus];
        return "Bus " + juce::String (bus * 2 + 1) + "-" + juce::String (bus * 2 + 2);
    }
};

} // namespace beatmaker::model
