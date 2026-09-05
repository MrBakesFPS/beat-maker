// ITU-R BS.1770 loudness metering.
//
// The audio thread K-weights the master mix and pushes one energy block per
// callback into a lock-free FIFO (LoudnessSource). The message thread feeds
// those blocks to LoudnessAnalyser, which produces Momentary (400 ms),
// Short-term (3 s), Integrated (absolute -70 LUFS and relative -10 LU gates),
// Loudness Range (EBU R128 LRA) and True Peak (4x oversampled).
#pragma once

#include "../dsp/Effects.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <deque>
#include <vector>

namespace beatmaker::engine
{

struct LoudnessBlock
{
    float sumSquares[2] = { 0.0f, 0.0f };   // K-weighted energy per channel
    int numSamples = 0;
    float truePeak = 0.0f;                  // linear, 4x oversampled
};

// Audio-thread side: K-weighting filters, true-peak estimation, FIFO producer.
class LoudnessSource
{
public:
    static constexpr int fifoCapacity = 512;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    // Analyse a stereo buffer (does not modify it). RT-safe.
    void process (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    // Message thread: drain blocks into `out`, returns the number read.
    int drain (std::vector<LoudnessBlock>& out);

private:
    double sampleRate = 48000.0;
    Biquad::Coefficients shelf, highPass;
    std::array<Biquad::State, 2> shelfState, hpState;
    juce::AudioBuffer<float> scratch { 2, 8192 };
    std::array<juce::LagrangeInterpolator, 2> upsamplers;
    std::vector<float> oversampled;
    std::array<LoudnessBlock, fifoCapacity> blocks;
    juce::AbstractFifo fifo { fifoCapacity };
};

// Message-thread side: integrates blocks into the standard measurements.
class LoudnessAnalyser
{
public:
    struct Readings
    {
        float momentary = -100.0f;    // LUFS
        float shortTerm = -100.0f;    // LUFS
        float integrated = -100.0f;   // LUFS (gated)
        float range = 0.0f;           // LU
        float truePeakDb = -100.0f;   // dBTP, held maximum
        float momentaryMax = -100.0f; // held
        float shortTermMax = -100.0f; // held
    };

    void setSampleRate (double sr) noexcept { sampleRate = sr; }
    void addBlock (const LoudnessBlock&);
    void reset();
    Readings getReadings() const;

    static float energyToLoudness (double meanEnergy) noexcept
    {
        return meanEnergy > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanEnergy)) : -100.0f;
    }

private:
    void finish100msBlock();

    double sampleRate = 48000.0;
    // Accumulator for the current 100 ms gating block
    double accEnergy = 0.0;
    int accSamples = 0;
    // 100 ms block energies (per channel summed with unit weights)
    std::deque<double> hundredMs;                 // last 30 (3 s)
    std::vector<double> momentaryBlockEnergies;   // 400 ms windows, every 100 ms, for the integrated gate
    std::vector<float> shortTermHistory;          // for LRA
    Readings readings;
};

} // namespace beatmaker::engine
