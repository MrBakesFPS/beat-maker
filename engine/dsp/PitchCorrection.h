// Pitch correction insert: a monophonic pitch detector (YIN, decimated)
// drives a Rubber Band real-time pitch shifter toward the nearest note of a
// key/scale, at a chosen retune speed. Formant preservation keeps the
// character of a voice while it is shifted.
#pragma once

#include "Effects.h"
#include <rubberband/RubberBandStretcher.h>
#include <atomic>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

// RT-safe monophonic pitch detector. Feed audio continuously; ask for the
// pitch of the most recent window.
class PitchDetector
{
public:
    void prepare (double sampleRate);
    void reset();
    void push (const float* mono, int numSamples) noexcept;
    // Fundamental in Hz of the latest window, or 0 when unvoiced/silent.
    float detect() noexcept;

    static constexpr float minHz = 60.0f, maxHz = 1200.0f;

private:
    double sampleRate = 48000.0;
    int decimation = 2;
    std::vector<float> ring;      // decimated history
    int writePos = 0;
    std::vector<float> window, difference;
    float decimAccum = 0.0f;
    int decimCount = 0;
};

class PitchCorrectionEffect final : public Effect
{
public:
    enum Param { key, scale, speed, amount, transpose, formant, mix };
    enum Scale { chromatic, major, minor, majorPentatonic, minorPentatonic, numScales };

    PitchCorrectionEffect() : Effect (EffectType::pitchCorrection) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
    int getLatencySamples (const InsertParams&) const noexcept override { return latency; }
    float getMeter() const noexcept override { return std::abs (currentCorrection.load (std::memory_order_relaxed)) * 100.0f; }   // cents moved

    // For displays: detected input note (MIDI, fractional; < 0 = none) and the note it is pulled toward.
    float getDetectedMidi() const noexcept { return detectedMidi.load (std::memory_order_relaxed); }
    float getTargetMidi() const noexcept { return targetMidi.load (std::memory_order_relaxed); }

    static const char* scaleName (int);
    static const char* keyName (int);
    // Nearest scale note to a fractional MIDI pitch (with transpose applied after).
    static float nearestScaleNote (float midi, int keyIndex, int scaleIndex) noexcept;
    static bool scaleContains (int scaleIndex, int semitoneFromRoot) noexcept;

protected:
    void prepareImpl (int maxBlockSize) override;

private:
    std::unique_ptr<RubberBand::RubberBandStretcher> shifter;
    PitchDetector detector;
    int latency = 0, maxBlock = 8192;

    std::vector<float> monoScratch;
    juce::AudioBuffer<float> wetRing { 2, 1 };        // shifter output waiting to be played
    int ringWrite = 0, ringRead = 0, ringCount = 0, ringSize = 1;
    juce::AudioBuffer<float> dryLine { 2, 1 };        // dry delayed by the latency for the mix control
    int dryWrite = 0;
    juce::AudioBuffer<float> shiftOut { 2, 1 };
    bool formantPreserved = false;

    std::atomic<float> currentCorrection { 0.0f };     // semitones applied right now
    std::atomic<float> detectedMidi { -1.0f }, targetMidi { -1.0f };
    float lastTarget = -1.0f;
};

} // namespace beatmaker::engine
