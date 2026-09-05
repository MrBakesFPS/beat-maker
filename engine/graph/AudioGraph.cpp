#include "AudioGraph.h"

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

void AudioGraph::setSnapshot (std::unique_ptr<RenderSnapshot> snapshot)
{
    // If the audio thread has not yet picked up the previous pending snapshot
    // it is safe to delete it here: only this thread ever stores into
    // `incoming`, and the audio thread only ever exchanges it for nullptr.
    delete incoming.exchange (snapshot.release());
}

void AudioGraph::collectGarbage()
{
    const auto scope = retiredFifo.read (retiredFifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i) delete std::exchange (retired[(size_t) (scope.startIndex1 + i)], nullptr);
    for (int i = 0; i < scope.blockSize2; ++i) delete std::exchange (retired[(size_t) (scope.startIndex2 + i)], nullptr);
}

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
            // Put `next` back so it is not lost.
            RenderSnapshot* expected = nullptr;
            if (! incoming.compare_exchange_strong (expected, next))
                delete next; // only reachable if the message thread raced us; extremely unlikely
            return;
        }
    }

    current = next;
}

void AudioGraph::renderBlock (float* const* outputs, int numOutputs, int numSamples)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    swapInPendingSnapshot();

    if (current == nullptr || ! transport.isPlaying() || numSamples <= 0)
    {
        for (auto& p : outputPeak) p.store (0.0f, std::memory_order_relaxed);
        return;
    }

    const juce::int64 blockStart = transport.getPositionSamples();
    const juce::int64 blockEnd   = blockStart + numSamples;

    for (const auto& clip : current->clips)
    {
        if (clip.audio == nullptr || clip.length <= 0)
            continue;

        const juce::int64 clipEnd = clip.timelineStart + clip.length;
        const juce::int64 from = juce::jmax (blockStart, clip.timelineStart);
        const juce::int64 to   = juce::jmin (blockEnd, clipEnd);

        if (from >= to)
            continue;

        const int outOffset  = static_cast<int> (from - blockStart);
        const int count      = static_cast<int> (to - from);
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

    for (int ch = 0; ch < juce::jmin (numOutputs, (int) outputPeak.size()); ++ch)
        if (outputs[ch] != nullptr)
            outputPeak[(size_t) ch].store (juce::FloatVectorOperations::findMaximum (outputs[ch], numSamples),
                                           std::memory_order_relaxed);

    transport.advance (numSamples);
}

float AudioGraph::getOutputPeak (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, (int) outputPeak.size())
             ? outputPeak[(size_t) channel].load (std::memory_order_relaxed) : 0.0f;
}

void AudioGraph::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                   float* const* outputChannelData, int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    renderBlock (outputChannelData, numOutputChannels, numSamples);
}

void AudioGraph::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
        transport.setSampleRate (device->getCurrentSampleRate());
}

void AudioGraph::audioDeviceStopped() {}

} // namespace beatmaker::engine
