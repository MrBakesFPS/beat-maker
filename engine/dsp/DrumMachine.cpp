#include "DrumMachine.h"

namespace beatmaker::engine
{

void DrumMachine::trigger (const DrumKit* kit, int pad, float velocity, float gain, int delaySamples, int strip, int gateSamples) noexcept
{
    if (kit == nullptr || ! juce::isPositiveAndBelow (pad, DrumKit::numPads))
        return;

    const auto& sample = kit->pads[(size_t) pad];
    if (sample.audio == nullptr || sample.audio->getNumSamples() == 0)
        return;

    // Choke any voice already playing this pad, starting when the new hit lands.
    for (auto& v : voices)
        if (v.active && v.kit == kit && v.pad == pad && v.fadeRemaining < 0 && v.fadePending < 0)
            v.fadePending = juce::jmax (0, delaySamples);

    // Find a free voice, else steal the one furthest through its sample.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.position > target->position) target = &v;
    }

    *target = {};
    target->kit    = kit;
    target->audio  = sample.audio.get();
    target->pad    = pad;
    target->delay  = juce::jmax (0, delaySamples);
    target->gain   = velocity * sample.gain * gain;
    target->strip  = strip;
    if (gateSamples > 0) target->fadePending = gateSamples;   // the gate ends the way a choke does: a short fade
    target->active = true;
}

void DrumMachine::chokeStrip (int strip) noexcept
{
    for (auto& v : voices)
        if (v.active && v.strip == strip && v.fadeRemaining < 0 && v.fadePending < 0)
            v.fadePending = 0;
}

bool DrumMachine::hasVoicesForStrip (int strip) const noexcept
{
    for (const auto& v : voices) if (v.active && v.strip == strip) return true;
    return false;
}

void DrumMachine::render (float* const* outputs, int numOutputs, int numSamples, int strip) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active || (strip != -1 && v.strip != strip))
            continue;

        int pos = juce::jmin (v.delay, numSamples);   // position within this block
        v.delay -= pos;

        const int length = v.audio->getNumSamples();
        const int srcChannels = v.audio->getNumChannels();

        while (pos < numSamples && v.active)
        {
            int n = juce::jmin (numSamples - pos, length - v.position);
            if (n <= 0) { v.active = false; break; }

            enum class Mode { normal, fading } mode = Mode::normal;

            if (v.fadePending > 0)
            {
                n = juce::jmin (n, v.fadePending);       // play normally until the choke point
            }
            else if (v.fadePending == 0)
            {
                v.fadePending = -1;
                v.fadeRemaining = retriggerFadeSamples;
                continue;
            }

            if (v.fadeRemaining >= 0)
            {
                mode = Mode::fading;
                n = juce::jmin (n, v.fadeRemaining);
            }

            for (int ch = 0; ch < numOutputs; ++ch)
            {
                if (outputs[ch] == nullptr) continue;
                const float* src = v.audio->getReadPointer (juce::jmin (ch, srcChannels - 1), v.position);
                float* dst = outputs[ch] + pos;
                const float chGain = v.gain;

                if (mode == Mode::normal)
                    juce::FloatVectorOperations::addWithMultiply (dst, src, chGain, n);
                else
                    for (int i = 0; i < n; ++i)
                        dst[i] += src[i] * chGain * (float) (v.fadeRemaining - i) / (float) retriggerFadeSamples;
            }

            v.position += n;
            pos += n;
            if (v.fadePending > 0)   v.fadePending -= n;
            if (mode == Mode::fading) v.fadeRemaining -= n;

            if (v.position >= length || v.fadeRemaining == 0)
                v.active = false;
        }
    }
}

void DrumMachine::killVoicesNotUsing (const DrumKit* const* kits, int numKits) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active) continue;
        bool found = false;
        for (int i = 0; i < numKits && ! found; ++i)
            found = (kits[i] == v.kit);
        if (! found)
            v.active = false;
    }
}

void DrumMachine::reset() noexcept
{
    for (auto& v : voices) v.active = false;
}

int DrumMachine::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

} // namespace beatmaker::engine
