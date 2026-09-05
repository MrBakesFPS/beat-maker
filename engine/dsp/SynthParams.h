// SynthParams: the sound of a subtractive synth. Immutable once shared with
// the engine (copy-on-write from the model), like DrumKit.
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

struct SynthParams
{
    enum class Wave { saw, square, triangle, sine };

    juce::String name = "Init";
    Wave wave = Wave::saw;
    bool secondOscillator = true;
    float detuneCents = 7.0f;

    float cutoffHz = 6000.0f;
    float resonance = 0.1f;          // 0..1
    float filterEnvOctaves = 0.0f;   // how far the envelope opens the filter

    float attackSeconds = 0.005f;
    float decaySeconds = 0.2f;
    float sustainLevel = 0.7f;
    float releaseSeconds = 0.3f;

    float gain = 0.4f;

    static std::vector<SynthParams> presets()
    {
        std::vector<SynthParams> p;
        { SynthParams s; s.name = "Init Saw"; p.push_back (s); }
        { SynthParams s; s.name = "Pluck"; s.cutoffHz = 900.0f; s.resonance = 0.3f; s.filterEnvOctaves = 3.5f;
          s.attackSeconds = 0.001f; s.decaySeconds = 0.28f; s.sustainLevel = 0.0f; s.releaseSeconds = 0.2f; s.gain = 0.45f; p.push_back (s); }
        { SynthParams s; s.name = "Soft Pad"; s.wave = Wave::triangle; s.detuneCents = 12.0f; s.cutoffHz = 2500.0f;
          s.attackSeconds = 0.4f; s.decaySeconds = 0.5f; s.sustainLevel = 0.8f; s.releaseSeconds = 0.9f; s.gain = 0.35f; p.push_back (s); }
        { SynthParams s; s.name = "Square Lead"; s.wave = Wave::square; s.detuneCents = 5.0f; s.cutoffHz = 5000.0f; s.resonance = 0.2f;
          s.attackSeconds = 0.005f; s.decaySeconds = 0.1f; s.sustainLevel = 0.9f; s.releaseSeconds = 0.15f; s.gain = 0.3f; p.push_back (s); }
        { SynthParams s; s.name = "Sub Bass"; s.wave = Wave::sine; s.secondOscillator = false; s.cutoffHz = 800.0f;
          s.attackSeconds = 0.003f; s.decaySeconds = 0.3f; s.sustainLevel = 0.8f; s.releaseSeconds = 0.1f; s.gain = 0.7f; p.push_back (s); }
        return p;
    }

    static std::shared_ptr<const SynthParams> preset (int index)
    {
        auto all = presets();
        return std::make_shared<const SynthParams> (all[(size_t) juce::jlimit (0, (int) all.size() - 1, index)]);
    }
};

} // namespace beatmaker::engine
