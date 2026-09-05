#include "Loudness.h"
#include <algorithm>
#include <cmath>

namespace beatmaker::engine
{

//==============================================================================
// BS.1770 K-weighting, derived for any sample rate (reproduces the published
// 48 kHz coefficients exactly).

namespace
{
    Biquad::Coefficients kWeightingShelf (double fs) noexcept
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        return { (float) ((Vh + Vb * K / Q + K * K) / a0), (float) (2.0 * (K * K - Vh) / a0), (float) ((Vh - Vb * K / Q + K * K) / a0),
                 (float) (2.0 * (K * K - 1.0) / a0), (float) ((1.0 - K / Q + K * K) / a0) };
    }

    Biquad::Coefficients kWeightingHighPass (double fs) noexcept
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        return { (float) (1.0 / a0), (float) (-2.0 / a0), (float) (1.0 / a0),
                 (float) (2.0 * (K * K - 1.0) / a0), (float) ((1.0 - K / Q + K * K) / a0) };
    }
}

//==============================================================================
// LoudnessSource (audio thread)

void LoudnessSource::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    shelf = kWeightingShelf (sr);
    highPass = kWeightingHighPass (sr);
    scratch.setSize (2, juce::jmax (maxBlockSize, 64));
    oversampled.assign ((size_t) scratch.getNumSamples() * 4 + 8, 0.0f);
    reset();
}

void LoudnessSource::reset() noexcept
{
    for (auto& s : shelfState) s = {};
    for (auto& s : hpState) s = {};
    for (auto& u : upsamplers) u.reset();
}

void LoudnessSource::process (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    numSamples = juce::jmin (numSamples, scratch.getNumSamples());
    if (numSamples <= 0 || buffer.getNumChannels() == 0) return;

    LoudnessBlock block;
    block.numSamples = numSamples;

    for (int ch = 0; ch < 2; ++ch)
    {
        const int src = juce::jmin (ch, buffer.getNumChannels() - 1);
        scratch.copyFrom (ch, 0, buffer, src, 0, numSamples);
        auto* d = scratch.getWritePointer (ch);

        // True peak on the unweighted signal, 4x oversampled
        const int outLen = numSamples * 4;
        if ((int) oversampled.size() >= outLen)
        {
            upsamplers[(size_t) ch].process (0.25, buffer.getReadPointer (src), oversampled.data(), outLen, numSamples, 0);
            block.truePeak = juce::jmax (block.truePeak, juce::FloatVectorOperations::findMaximum (oversampled.data(), outLen),
                                         -juce::FloatVectorOperations::findMinimum (oversampled.data(), outLen));
        }

        Biquad::process (shelf, shelfState[(size_t) ch], d, numSamples);
        Biquad::process (highPass, hpState[(size_t) ch], d, numSamples);

        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i) sum += (double) d[i] * d[i];
        block.sumSquares[ch] = (float) sum;
    }

    const auto scope = fifo.write (1);
    const int index = scope.blockSize1 == 1 ? scope.startIndex1 : scope.blockSize2 == 1 ? scope.startIndex2 : -1;
    if (index >= 0) blocks[(size_t) index] = block;   // full FIFO: drop the block (display only)
}

int LoudnessSource::drain (std::vector<LoudnessBlock>& out)
{
    const auto scope = fifo.read (fifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) out.push_back (blocks[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) out.push_back (blocks[(size_t) (scope.startIndex2 + i)]);
    return scope.blockSize1 + scope.blockSize2;
}

//==============================================================================
// LoudnessAnalyser (message thread)

void LoudnessAnalyser::reset()
{
    accEnergy = 0.0; accSamples = 0;
    hundredMs.clear();
    momentaryBlockEnergies.clear();
    shortTermHistory.clear();
    readings = {};
}

void LoudnessAnalyser::addBlock (const LoudnessBlock& b)
{
    if (b.numSamples <= 0) return;
    const float tpDb = juce::Decibels::gainToDecibels (b.truePeak, -100.0f);
    readings.truePeakDb = juce::jmax (readings.truePeakDb, tpDb);

    // Channel weights: L and R are 1.0 (BS.1770); mean square per channel summed.
    const double blockEnergy = (double) b.sumSquares[0] + (double) b.sumSquares[1];
    const int blockLen = (int) std::lround (sampleRate * 0.1);

    // Split the incoming block across 100 ms gating blocks.
    int remaining = b.numSamples;
    double energyPerSample = blockEnergy / b.numSamples;
    while (remaining > 0)
    {
        const int take = juce::jmin (remaining, blockLen - accSamples);
        accEnergy += energyPerSample * take;
        accSamples += take;
        remaining -= take;
        if (accSamples >= blockLen) finish100msBlock();
    }
}

void LoudnessAnalyser::finish100msBlock()
{
    const int blockLen = (int) std::lround (sampleRate * 0.1);
    hundredMs.push_back (accEnergy / juce::jmax (1, blockLen));   // mean energy of this 100 ms
    accEnergy = 0.0; accSamples = 0;
    while (hundredMs.size() > 30) hundredMs.pop_front();

    auto meanOfLast = [&] (size_t n)
    {
        if (hundredMs.size() < n) return -1.0;
        double sum = 0.0;
        for (size_t i = hundredMs.size() - n; i < hundredMs.size(); ++i) sum += hundredMs[i];
        return sum / (double) n;
    };

    // Momentary: 400 ms
    if (const double m = meanOfLast (4); m >= 0.0)
    {
        readings.momentary = energyToLoudness (m);
        readings.momentaryMax = juce::jmax (readings.momentaryMax, readings.momentary);
        momentaryBlockEnergies.push_back (m);
    }

    // Short-term: 3 s
    if (const double st = meanOfLast (30); st >= 0.0)
    {
        readings.shortTerm = energyToLoudness (st);
        readings.shortTermMax = juce::jmax (readings.shortTermMax, readings.shortTerm);
        shortTermHistory.push_back (readings.shortTerm);
    }

    // Integrated: absolute gate -70 LUFS, then relative gate at (ungated mean - 10 LU)
    {
        std::vector<double> passing;
        for (double e : momentaryBlockEnergies) if (energyToLoudness (e) > -70.0f) passing.push_back (e);
        if (! passing.empty())
        {
            double mean = 0.0; for (double e : passing) mean += e; mean /= (double) passing.size();
            const float relativeGate = energyToLoudness (mean) - 10.0f;
            double gatedMean = 0.0; int count = 0;
            for (double e : passing) if (energyToLoudness (e) > relativeGate) { gatedMean += e; ++count; }
            if (count > 0) readings.integrated = energyToLoudness (gatedMean / count);
        }
    }

    // Loudness range: short-term values above -70 LUFS and above (mean - 20 LU); 10th..95th percentile
    if (shortTermHistory.size() >= 2)
    {
        std::vector<float> v;
        for (float l : shortTermHistory) if (l > -70.0f) v.push_back (l);
        if (! v.empty())
        {
            double mean = 0.0; for (float l : v) mean += std::pow (10.0, (l + 0.691) / 10.0); mean /= (double) v.size();
            const float gate = energyToLoudness (mean) - 20.0f;
            std::vector<float> gated; for (float l : v) if (l > gate) gated.push_back (l);
            if (gated.size() >= 2)
            {
                std::sort (gated.begin(), gated.end());
                const auto pct = [&] (double p) { return gated[(size_t) juce::jlimit (0.0, (double) gated.size() - 1, std::floor (p * (gated.size() - 1)))]; };
                readings.range = juce::jmax (0.0f, pct (0.95) - pct (0.10));
            }
        }
    }
}

LoudnessAnalyser::Readings LoudnessAnalyser::getReadings() const { return readings; }

} // namespace beatmaker::engine
