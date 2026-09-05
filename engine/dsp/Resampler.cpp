#include "Resampler.h"
#include <cmath>

namespace beatmaker::engine
{

juce::AudioBuffer<float> Resampler::resample (const juce::AudioBuffer<float>& source, double ratio)
{
    const int numChannels = source.getNumChannels();
    const int sourceLength = source.getNumSamples();

    if (numChannels == 0 || sourceLength == 0 || ratio <= 0.0)
        return {};

    if (std::abs (ratio - 1.0) < 1.0e-9)
        return juce::AudioBuffer<float> (source);

    const int outLength = juce::jmax (1, (int) std::ceil (sourceLength / ratio));
    juce::AudioBuffer<float> out (numChannels, outLength);
    out.clear();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.reset();
        interpolator.process (ratio, source.getReadPointer (ch), out.getWritePointer (ch), outLength, sourceLength, 0);
    }

    return out;
}

} // namespace beatmaker::engine
