// Built-in insert effects. Instances are stateful (filters, delay lines,
// reverb) and owned by the model; parameters are immutable snapshots passed
// in per block, so parameter edits are copy-on-write like everything else.
// process() must never allocate or lock.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

enum class EffectType { none, eq, compressor, limiter, gate, deesser, delay, reverb, chorus, flanger, phaser, saturation, ampSim, utility, plugin, convolution, pitchCorrection };

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
    std::array<float, 24> values {};
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

    // Sidechain (key input). The graph hands the key signal for the current
    // block to effects that accept one just before process() and clears it
    // after. Pointers only: RT-safe.
    virtual bool acceptsSidechain() const noexcept { return false; }
    void setSidechain (const float* const* key, int numChannels) noexcept { keyChannels = key; keyNumChannels = numChannels; }
    void clearSidechain() noexcept { keyChannels = nullptr; keyNumChannels = 0; }

    // Message-thread hook: the stored parameters changed (or the effect was
    // just created). Effects with non-RT-safe reconfiguration (loading an
    // impulse response) do it here; the audio thread keeps using the old state.
    virtual void paramsChanged (const InsertParams&) {}

    static std::unique_ptr<Effect> create (EffectType, double sampleRate, int maxBlockSize = 8192);
    static const std::vector<ParamInfo>& paramInfo (EffectType);
    // Named choices for an enumerated parameter (null when the parameter is continuous).
    static const std::vector<const char*>* choices (EffectType, int paramIndex);
    static InsertParams defaultParams (EffectType);
    static const char* typeName (EffectType);
    static const std::vector<EffectType>& availableTypes();

protected:
    virtual void prepareImpl (int maxBlockSize) = 0;
    double sampleRate = 44100.0;

    // Detector level for sample i: the key input when one is connected, else the audio itself.
    float detectorLevel (const juce::AudioBuffer<float>& buffer, int i, int channels) const noexcept
    {
        float level = 0.0f;
        if (keyChannels != nullptr)
            for (int ch = 0; ch < keyNumChannels; ++ch) level = juce::jmax (level, std::abs (keyChannels[ch][i]));
        else
            for (int ch = 0; ch < channels; ++ch) level = juce::jmax (level, std::abs (buffer.getSample (ch, i)));
        return level;
    }
    bool hasSidechain() const noexcept { return keyChannels != nullptr && keyNumChannels > 0; }
    const float* const* keyChannels = nullptr;
    int keyNumChannels = 0;

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
    static Coefficients lowPass   (double sampleRate, double freq, double q) noexcept;

    // |H(e^jw)| at `freq` in dB, for drawing responses.
    static double magnitudeDb (const Coefficients&, double sampleRate, double freq) noexcept;

    static void process (const Coefficients&, State&, float* data, int numSamples) noexcept;
};

//==============================================================================

// 7-band parametric: high-pass, low shelf, three peaks, high shelf, low-pass.
class EqEffect final : public Effect
{
public:
    enum Param { hpFreq, lowGain, lowFreq, lmGain, lmFreq, lmQ, midGain, midFreq, midQ, hmGain, hmFreq, hmQ, highGain, highFreq, lpFreq };
    static constexpr int numBands = 7;
    EqEffect() : Effect (EffectType::eq) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;

    // Coefficients for each band (for drawing the response); a band is
    // inactive when its entry is a pass-through.
    static std::array<Biquad::Coefficients, numBands> coefficientsFor (const InsertParams&, double sampleRate) noexcept;
    static std::array<bool, numBands> activeBands (const InsertParams&) noexcept;
protected:
    void prepareImpl (int) override {}
private:
    std::array<float, 24> cached {};
    bool coefficientsValid = false;
    std::array<Biquad::Coefficients, numBands> bands;
    std::array<bool, numBands> active {};
    std::array<std::array<Biquad::State, 2>, numBands> state;
};

class CompressorEffect final : public Effect
{
public:
    enum Param { threshold, ratio, attack, release, makeup, lookahead };
    static constexpr float maxLookaheadMs = 10.0f;
    CompressorEffect() : Effect (EffectType::compressor) {}
    bool acceptsSidechain() const noexcept override { return true; }
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
    enum Param { time, feedback, mix, highCut, mode };   // mode: 0 digital, 1 tape, 2 ping-pong
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

class LimiterEffect final : public Effect
{
public:
    enum Param { ceiling, release, inputGain };
    static constexpr float lookaheadMs = 2.0f;
    LimiterEffect() : Effect (EffectType::limiter) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
    float getMeter() const noexcept override { return gainReductionDb.load (std::memory_order_relaxed); }
    int getLatencySamples (const InsertParams&) const noexcept override { return lookaheadSamples; }
protected:
    void prepareImpl (int) override;
private:
    std::array<std::vector<float>, 2> lines;
    int writePos = 0, lookaheadSamples = 0;
    float envelope = 0.0f;
    std::atomic<float> gainReductionDb { 0.0f };
};

class GateEffect final : public Effect
{
public:
    enum Param { threshold, ratio, attack, hold, release, range };
    GateEffect() : Effect (EffectType::gate) {}
    bool acceptsSidechain() const noexcept override { return true; }
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
    float getMeter() const noexcept override { return gainReductionDb.load (std::memory_order_relaxed); }
protected:
    void prepareImpl (int) override {}
private:
    float envelope = 0.0f, gain = 1.0f;
    int holdCounter = 0;
    std::atomic<float> gainReductionDb { 0.0f };
};

// De-esser: a high-passed detector drives a dynamic high-shelf cut at the
// same frequency (broadband/shelf style), so the full signal stays phase-coherent.
class DeEsserEffect final : public Effect
{
public:
    enum Param { frequency, threshold, range, release };
    DeEsserEffect() : Effect (EffectType::deesser) {}
    bool acceptsSidechain() const noexcept override { return true; }
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
    float getMeter() const noexcept override { return gainReductionDb.load (std::memory_order_relaxed); }
protected:
    void prepareImpl (int) override {}
private:
    float cachedFreq = -1.0f, cachedShelfDb = 1.0e9f;
    Biquad::Coefficients hp, shelf;
    std::array<Biquad::State, 2> hpState, shelfState, keyState;
    float envelope = 0.0f;
    std::atomic<float> gainReductionDb { 0.0f };
};

// Modulated delay lines shared by chorus and flanger.
class ModulatedDelayEffect final : public Effect
{
public:
    enum Param { rate, depth, feedback, mix, voices };   // flanger ignores voices
    explicit ModulatedDelayEffect (EffectType t) : Effect (t) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override;
private:
    std::array<std::vector<float>, 2> lines;
    int writePos = 0;
    double phase = 0.0;
    std::array<float, 2> feedbackState {};
};

class PhaserEffect final : public Effect
{
public:
    enum Param { rate, depth, stages, feedback, mix };
    static constexpr int maxStages = 8;
    PhaserEffect() : Effect (EffectType::phaser) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override {}
private:
    double phase = 0.0;
    std::array<std::array<float, maxStages>, 2> allpassState {};
    std::array<float, 2> feedbackState {};
};

class SaturationEffect final : public Effect
{
public:
    enum Param { drive, type, tone, output };   // type: 0 soft, 1 hard, 2 tube
    SaturationEffect() : Effect (EffectType::saturation) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override {}
private:
    std::array<float, 2> lpState {};
};

class AmpSimEffect final : public Effect
{
public:
    enum Param { gain, bass, mid, treble, presence, cabinet, master };
    AmpSimEffect() : Effect (EffectType::ampSim) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override {}
private:
    std::array<float, 24> cached {};
    bool valid = false;
    Biquad::Coefficients bassC, midC, trebleC, presenceC, cabC, cabHpC;
    std::array<Biquad::State, 2> bassS, midS, trebleS, presenceS, cabS, cabHpS;
};

class UtilityEffect final : public Effect
{
public:
    enum Param { gain, invertL, invertR, width, mono };
    UtilityEffect() : Effect (EffectType::utility) {}
    void reset() override {}
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
protected:
    void prepareImpl (int) override {}
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

// Convolution reverb: bundled synthetic spaces or a user impulse response,
// through JUCE's partitioned FFT convolution. Impulse (re)loading happens on
// the message thread in paramsChanged(); the audio thread crossfades to the
// new response when it is ready.
class ConvolutionEffect final : public Effect
{
public:
    enum Param { impulse, predelay, decay, lowCut, highCut, width, mix };
    enum Impulse { hall, chamber, room, plate, ambience, cathedral, custom, numImpulses };
    static constexpr float maxPredelayMs = 250.0f;

    ConvolutionEffect() : Effect (EffectType::convolution) {}
    void reset() override;
    void process (juce::AudioBuffer<float>&, int, const InsertParams&) noexcept override;
    void paramsChanged (const InsertParams&) override;
    juce::String getDisplayName() const override;

    static const char* impulseName (int);
    // Deterministic synthetic response for a bundled space, stereo, at `sampleRate`.
    static juce::AudioBuffer<float> generateImpulse (int which, double sampleRate);

    // Custom impulse (message thread). `sampleRate` is that of the buffer.
    void setCustomImpulse (std::shared_ptr<const juce::AudioBuffer<float>>, double sampleRate, juce::String name, juce::String path = {});
    std::shared_ptr<const juce::AudioBuffer<float>> getCustomImpulse() const { return customImpulse; }
    double getCustomImpulseRate() const noexcept { return customRate; }
    juce::String getCustomImpulseName() const { return customName; }
    juce::String getCustomImpulsePath() const { return customPath; }
    bool isResponseLoaded() const noexcept { return loadedImpulse >= 0; }

protected:
    void prepareImpl (int maxBlockSize) override;

private:
    void loadResponse (const InsertParams&);

    juce::dsp::Convolution convolution;
    int maxBlock = 8192;
    int loadedImpulse = -1;
    float loadedDecay = -1.0f;
    std::shared_ptr<const juce::AudioBuffer<float>> customImpulse;
    double customRate = 44100.0;
    juce::String customName, customPath;
    int customGeneration = 0, loadedCustomGeneration = -1;

    juce::AudioBuffer<float> wet { 2, 8192 };
    std::array<std::vector<float>, 2> predelayLines;
    int predelayWrite = 0;
    float cachedLowCut = -1.0f, cachedHighCut = -1.0f;
    Biquad::Coefficients lowCutCoefs, highCutCoefs;
    std::array<Biquad::State, 2> lowCutState, highCutState;
};

} // namespace beatmaker::engine
