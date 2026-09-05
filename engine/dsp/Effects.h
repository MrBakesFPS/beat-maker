// Built-in insert effects. Instances are stateful (filters, delay lines,
// reverb) and owned by the model; parameters are immutable snapshots passed
// in per block, so parameter edits are copy-on-write like everything else.
// process() must never allocate or lock.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

enum class EffectType { none, eq, compressor, delay, reverb, plugin };

struct ParamInfo
{
    const char* name;
    float min, max, def;
    float skewMidpoint;     // 0 = linear
    const char* suffix;
};

struct InsertParams
{
    EffectType type = EffectType::none;
    std::array<float, 8> values {};
};

class Effect
{
public:
    explicit Effect (EffectType t) : type (t) {}
    virtual ~Effect() = default;

    EffectType getType() const noexcept { return type; }
    double getSampleRate() const noexcept { return sampleRate; }

    void prepare (double newSampleRate, int maxBlockSize) { sampleRate = newSampleRate; prepareImpl (maxBlockSize); reset(); }
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, int numSamples, const InsertParams& params) noexcept = 0;

    // Optional meter (e.g. compressor gain reduction in dB, >= 0)
    virtual float getMeter() const noexcept { return 0.0f; }

    // Samples of delay this effect adds for the given parameters (for ADC).
    virtual int getLatencySamples (const InsertParams&) const noexcept { return 0; }

    // Hosted plugins: set a parameter from an automation lane (normalised 0..1). RT-safe.
    virtual void setAutomatedParameter (int /*index*/, float /*normalised*/) noexcept {}
    virtual juce::String getDisplayName() const { return typeName (getType()); }

    static std::unique_ptr<Effect> create (EffectType, double sampleRate, int maxBlockSize = 8192);
    static const std::vector<ParamInfo>& paramInfo (EffectType);
    static InsertParams defaultParams (EffectType);
    static const char* typeName (EffectType);
    static const std::vector<EffectType>& availableTypes();

protected:
    virtual void prepareImpl (int maxBlockSize) = 0;
    double sampleRate = 44100.0;

private:
    EffectType type;
};

//==============================================================================
// RBJ biquad, direct form I, per-channel state.
struct Biquad
{
    struct Coefficients { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    static Coefficients lowShelf  (double sampleRate, double freq, double gainDb, double slope = 1.0) noexcept;
    static Coefficients highShelf (double sampleRate, double freq, double gainDb, double slope = 1.0) noexcept;
    static Coefficients peak      (double sampleRate, double freq, double gainDb, double q) noexcept;
    static Coefficients highShelfQ (double sampleRate, double freq, double gainDb, double q) noexcept;
    static Coefficients highPass  (double sampleRate, double freq, double q) noexcept;

    static void process (const Coefficients&, State&, float* data, int numSamples) noexcept;
};

//==============================================================================

class EqEffect final : public Effect
{
public:
    enum Param { lowGain, lowFreq, midGain, midFreq, midQ, highGain, highFreq };
    EqEffect() : Effect (EffectType::eq) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override {}
private:
    std::array<float, 8> cached {};
    bool coefficientsValid = false;
    Biquad::Coefficients low, mid, high;
    std::array<Biquad::State, 2> lowState, midState, highState;
};

class CompressorEffect final : public Effect
{
public:
    enum Param { threshold, ratio, attack, release, makeup, lookahead };
    static constexpr float maxLookaheadMs = 10.0f;
    CompressorEffect() : Effect (EffectType::compressor) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
    float getMeter() const noexcept override { return gainReductionDb.load (std::memory_order_relaxed); }
    int getLatencySamples (const InsertParams&) const noexcept override;
protected:
    void prepareImpl (int) override;
private:
    float envelope = 0.0f;
    std::atomic<float> gainReductionDb { 0.0f };
    // Lookahead: the audio is delayed while the detector sees the live signal.
    std::array<std::vector<float>, 2> lookaheadLines;
    int lookaheadWrite = 0;
};

class DelayEffect final : public Effect
{
public:
    enum Param { time, feedback, mix, highCut };
    DelayEffect() : Effect (EffectType::delay) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override;
private:
    static constexpr double maxDelaySeconds = 2.0;
    std::array<std::vector<float>, 2> lines;
    int writePos = 0;
    std::array<float, 2> lpState {};
};

class ReverbEffect final : public Effect
{
public:
    enum Param { roomSize, damping, width, mix };
    ReverbEffect() : Effect (EffectType::reverb) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override;
private:
    juce::Reverb reverb;
};

} // namespace beatmaker::engine
