#include "Transients.h"
#include <cmath>

namespace beatmaker::engine
{

std::vector<juce::int64> Transients::detect (const juce::AudioBuffer<float>& audio, double sampleRate,
                                             float sensitivity, double minSpacingSeconds)
{
    std::vector<juce::int64> onsets;
    const int channels = audio.getNumChannels(), length = audio.getNumSamples();
    if (channels <= 0 || length < hopSize * 2) return onsets;

    // Log-energy envelope per hop (all channels summed).
    const int hops = length / hopSize;
    std::vector<float> energy ((size_t) hops);
    for (int h = 0; h < hops; ++h)
    {
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* d = audio.getReadPointer (ch, h * hopSize);
            for (int i = 0; i < hopSize; ++i) sum += (double) d[i] * d[i];
        }
        energy[(size_t) h] = (float) std::log10 (1.0e-7 + sum / (hopSize * channels));   // ~ -7 (silence) .. 0 (full scale)
    }

    // Positive energy rise over a short lookback: transients rise fast.
    const int lookback = 3;   // hops (~8 ms at 48k)
    std::vector<float> flux ((size_t) hops, 0.0f);
    for (int h = lookback; h < hops; ++h)
    {
        float prev = energy[(size_t) (h - lookback)];
        for (int k = 1; k < lookback; ++k) prev = juce::jmax (prev, energy[(size_t) (h - k)]);
        flux[(size_t) h] = juce::jmax (0.0f, energy[(size_t) h] - prev);
    }

    // Threshold: a rise of `minRise` dB-decades above the local median-ish level.
    const float minRise = 0.45f - 0.35f * juce::jlimit (0.0f, 1.0f, sensitivity);   // 0.45 (strict) .. 0.10 (soft) log10 units
    const float floorLevel = -5.5f + 1.5f * juce::jlimit (0.0f, 1.0f, sensitivity);  // ignore rises out of near-silence into near-silence
    const int minSpacingHops = juce::jmax (1, (int) std::llround (minSpacingSeconds * sampleRate / hopSize));
    int lastOnsetHop = -minSpacingHops;

    for (int h = lookback; h < hops; ++h)
    {
        if (flux[(size_t) h] < minRise || energy[(size_t) h] < floorLevel) continue;
        // Local peak of the flux
        bool peak = true;
        for (int k = 1; k <= 2 && peak; ++k)
        {
            if (h - k >= 0 && flux[(size_t) (h - k)] > flux[(size_t) h]) peak = false;
            if (h + k < hops && flux[(size_t) (h + k)] > flux[(size_t) h]) peak = false;
        }
        if (! peak || h - lastOnsetHop < minSpacingHops) continue;

        // Refine to the sample where the rise begins: scan back within the
        // lookback window for the first sample exceeding 25% of the hop's peak.
        const int hopStart = juce::jmax (0, (h - lookback + 1) * hopSize), hopEnd = juce::jmin (length, (h + 1) * hopSize);
        float peakAbs = 0.0f;
        for (int ch = 0; ch < channels; ++ch) peakAbs = juce::jmax (peakAbs, audio.getMagnitude (ch, hopStart, hopEnd - hopStart));
        juce::int64 onset = hopStart;
        for (int i = hopStart; i < hopEnd; ++i)
        {
            float a = 0.0f;
            for (int ch = 0; ch < channels; ++ch) a = juce::jmax (a, std::abs (audio.getSample (ch, i)));
            if (a >= 0.25f * peakAbs) { onset = i; break; }
        }
        onsets.push_back (onset);
        lastOnsetHop = h;
    }
    return onsets;
}

} // namespace beatmaker::engine
