#include "AudioGraph.h"
#include <cmath>

namespace beatmaker::engine
{

AudioGraph::AudioGraph (Transport& transportToUse) : transport (transportToUse) {}

AudioGraph::~AudioGraph()
{
    // By now the device has stopped, so it is safe to free everything here.
    collectGarbage();
    delete incoming.exchange (nullptr);
    delete current;
}

//==============================================================================
// Message-thread API

void AudioGraph::setSnapshot (std::unique_ptr<RenderSnapshot> snapshot)
{
    // Only this thread stores into `incoming`; the audio thread only ever
    // exchanges it for nullptr, so deleting a not-yet-collected pending
    // snapshot here is safe.
    delete incoming.exchange (snapshot.release());
}

void AudioGraph::collectGarbage()
{
    const auto scope = retiredFifo.read (retiredFifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i) delete std::exchange (retired[(size_t) (scope.startIndex1 + i)], nullptr);
    for (int i = 0; i < scope.blockSize2; ++i) delete std::exchange (retired[(size_t) (scope.startIndex2 + i)], nullptr);
}

void AudioGraph::triggerPadPreview (const DrumKit* kit, int pad, float velocity)
{
    const auto scope = previewFifo.write (1);
    const int index = scope.blockSize1 == 1 ? scope.startIndex1 : scope.blockSize2 == 1 ? scope.startIndex2 : -1;
    if (index >= 0)
        previewEvents[(size_t) index] = { kit, pad, velocity };
}

//==============================================================================
// Audio thread

void AudioGraph::swapInPendingSnapshot() noexcept
{
    auto* next = incoming.exchange (nullptr, std::memory_order_acq_rel);

    if (next == nullptr)
        return;

    if (current != nullptr)
    {
        const auto scope = retiredFifo.write (1);

        if (scope.blockSize1 == 1)
            retired[(size_t) scope.startIndex1] = current;
        else if (scope.blockSize2 == 1)
            retired[(size_t) scope.startIndex2] = current;
        else
        {
            // Retire queue full: keep the old snapshot and try again next block.
            RenderSnapshot* expected = nullptr;
            if (! incoming.compare_exchange_strong (expected, next))
                delete next; // only if the message thread raced us; practically unreachable
            return;
        }
    }

    current = next;

    // A different preview buffer restarts from the top.
    if (current->preview.get() != previewSource)
    {
        previewSource = current->preview.get();
        previewPosition = 0;
    }

    // Voices may only reference kits the new snapshot keeps alive.
    std::array<const DrumKit*, 64> kits {};
    int numKits = 0;
    for (const auto& p : current->patterns)
        if (p.kit != nullptr && numKits < (int) kits.size())
            kits[(size_t) numKits++] = p.kit.get();
    drums.killVoicesNotUsing (kits.data(), numKits);
}

bool AudioGraph::snapshotHasKit (const DrumKit* kit) const noexcept
{
    if (current == nullptr || kit == nullptr) return false;
    for (const auto& p : current->patterns)
        if (p.kit.get() == kit) return true;
    return false;
}

void AudioGraph::processPreviewEvents()
{
    const auto scope = previewFifo.read (previewFifo.getNumReady());

    auto handle = [this] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& e = previewEvents[(size_t) (start + i)];
            if (snapshotHasKit (e.kit))
                drums.trigger (e.kit, e.pad, e.velocity, current->masterGain, 0);
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

void AudioGraph::mixClips (float* const* outputs, int numOutputs, juce::int64 blockStart, int numSamples)
{
    const juce::int64 blockEnd = blockStart + numSamples;

    for (const auto& clip : current->clips)
    {
        if (clip.audio == nullptr || clip.length <= 0)
            continue;

        const juce::int64 clipEnd = clip.timelineStart + clip.length;
        const juce::int64 from = juce::jmax (blockStart, clip.timelineStart);
        const juce::int64 to   = juce::jmin (blockEnd, clipEnd);

        if (from >= to)
            continue;

        const int outOffset = static_cast<int> (from - blockStart);
        const int count     = static_cast<int> (to - from);
        const juce::int64 srcStart = clip.sourceOffset + (from - clip.timelineStart);

        if (srcStart < 0 || srcStart + count > clip.audio->getNumSamples())
            continue; // malformed clip; never read out of bounds on the audio thread

        const int srcChannels = clip.audio->getNumChannels();
        const float gain = clip.gain * current->masterGain;

        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr)
                continue;

            // Mono sources feed every output; multichannel sources map 1:1
            // and the last channel repeats for any extra outputs.
            const int srcCh = juce::jmin (ch, srcChannels - 1);
            juce::FloatVectorOperations::addWithMultiply (outputs[ch] + outOffset,
                                                          clip.audio->getReadPointer (srcCh, (int) srcStart),
                                                          gain, count);
        }
    }
}

void AudioGraph::scheduleSequencer (juce::int64 rangeStart, int numSamples)
{
    const juce::int64 rangeEnd = rangeStart + numSamples;
    const double sampleRate = transport.getSampleRate();
    const double bpm = transport.getBpm();

    for (const auto& rp : current->patterns)
    {
        if (rp.pattern == nullptr || rp.kit == nullptr || rp.length <= 0 || rp.pattern->numSteps <= 0)
            continue;

        const double stepDur = rp.pattern->getStepDurationSamples (sampleRate, bpm);
        if (stepDur < 1.0)
            continue;

        const juce::int64 clipEnd = rp.timelineStart + rp.length;
        const juce::int64 from = juce::jmax (rangeStart, rp.timelineStart);
        const juce::int64 to   = juce::jmin (rangeEnd, clipEnd);
        if (from >= to)
            continue;

        // First step index that could fall at or after `from`.
        juce::int64 k = (juce::int64) std::floor ((double) (from - rp.timelineStart) / stepDur);
        if (k < 0) k = 0;

        for (;; ++k)
        {
            const juce::int64 t = rp.timelineStart + (juce::int64) std::llround ((double) k * stepDur);
            if (t >= to) break;
            if (t < from) continue;

            const int step = (int) (k % rp.pattern->numSteps);
            const int delay = (int) (t - rangeStart);

            for (int pad = 0; pad < juce::jmin (DrumKit::numPads, StepPattern::maxPads); ++pad)
            {
                const auto v = rp.pattern->velocity[(size_t) pad][(size_t) step];
                if (v > 0)
                    drums.trigger (rp.kit.get(), pad, (float) v / 127.0f, rp.gain * current->masterGain, delay);
            }
        }
    }
}

void AudioGraph::renderRange (float* const* outputs, int numOutputs, int numSamples)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    swapInPendingSnapshot();

    if (current != nullptr)
    {
        processPreviewEvents();

        if (transport.isPlaying())
        {
            const juce::int64 pos = transport.getPositionSamples();
            mixClips (outputs, numOutputs, pos, numSamples);
            scheduleSequencer (pos, numSamples);
        }
    }

    // Drum voices always render so previews sound while stopped and tails
    // ring out after stop. Library preview likewise ignores the transport.
    drums.render (outputs, numOutputs, numSamples);
    mixPreview (outputs, numOutputs, numSamples);

    for (int ch = 0; ch < juce::jmin (numOutputs, (int) outputPeak.size()); ++ch)
        if (outputs[ch] != nullptr)
            outputPeak[(size_t) ch].store (juce::FloatVectorOperations::findMaximum (outputs[ch], numSamples),
                                           std::memory_order_relaxed);

    if (transport.isPlaying())
        transport.advance (numSamples);
}

void AudioGraph::mixMonitoredInputs (const float* const* inputs, int numInputs,
                                     float* const* outputs, int numOutputs, int numSamples)
{
    if (current == nullptr || inputs == nullptr || numInputs <= 0)
        return;

    for (const auto& m : current->monitors)
    {
        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr) continue;
            const int idx = m.firstInput + juce::jmin (ch, m.numInputs - 1);
            if (idx < 0 || idx >= numInputs || inputs[idx] == nullptr) continue;
            juce::FloatVectorOperations::addWithMultiply (outputs[ch], inputs[idx], m.gain, numSamples);
        }
    }
}

void AudioGraph::mixPreview (float* const* outputs, int numOutputs, int numSamples)
{
    if (current == nullptr || current->preview == nullptr)
        return;

    const auto& src = *current->preview;
    const int length = src.getNumSamples();
    const int srcChannels = src.getNumChannels();
    if (length <= 0 || srcChannels <= 0)
        return;

    for (int done = 0; done < numSamples; )
    {
        if (previewPosition >= length)
            previewPosition = 0;                     // loop

        const int n = juce::jmin (numSamples - done, length - previewPosition);
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::addWithMultiply (outputs[ch] + done,
                                                              src.getReadPointer (juce::jmin (ch, srcChannels - 1), previewPosition),
                                                              current->previewGain, n);
        previewPosition += n;
        done += n;
    }
}

void AudioGraph::renderBlock (const float* const* inputs, int numInputs,
                              float* const* outputs, int numOutputs, int numSamples)
{
    numOutputs = juce::jmin (numOutputs, maxOutputs);

    // Capture first: the transport position is still the block start.
    if (recorder != nullptr)
        recorder->processInput (inputs, numInputs, numSamples);
    std::array<float*, maxOutputs> offsetOutputs {};

    int done = 0;
    while (done < numSamples)
    {
        int count = numSamples - done;
        bool wrapAfter = false;

        // Split the block at the loop end so the wrap is sample-accurate.
        if (transport.isPlaying() && transport.hasValidLoop())
        {
            const juce::int64 pos = transport.getPositionSamples();
            const juce::int64 loopEnd = transport.getLoopEnd();
            if (pos < loopEnd && pos + count >= loopEnd)
            {
                count = (int) (loopEnd - pos);
                wrapAfter = true;
            }
        }

        if (count > 0)
        {
            for (int ch = 0; ch < numOutputs; ++ch)
                offsetOutputs[(size_t) ch] = outputs[ch] != nullptr ? outputs[ch] + done : nullptr;

            renderRange (offsetOutputs.data(), numOutputs, count);
            done += count;
        }

        if (wrapAfter)
            transport.setPositionSamples (transport.getLoopStart());

        if (count <= 0)
            break;
    }

    mixMonitoredInputs (inputs, numInputs, outputs, numOutputs, numSamples);
}

float AudioGraph::getOutputPeak (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, (int) outputPeak.size())
             ? outputPeak[(size_t) channel].load (std::memory_order_relaxed) : 0.0f;
}

void AudioGraph::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                   float* const* outputChannelData, int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    renderBlock (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void AudioGraph::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
        transport.setSampleRate (device->getCurrentSampleRate());
    drums.reset();
}

void AudioGraph::audioDeviceStopped() {}

} // namespace beatmaker::engine
