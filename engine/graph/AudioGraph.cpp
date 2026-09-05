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
    {
        PreviewEvent e;
        e.kit = kit; e.pad = pad; e.velocity = velocity;
        previewEvents[(size_t) index] = e;
    }
}

void AudioGraph::triggerNotePreview (int instrumentId, int pitch, float velocity, double seconds)
{
    const auto scope = previewFifo.write (1);
    const int index = scope.blockSize1 == 1 ? scope.startIndex1 : scope.blockSize2 == 1 ? scope.startIndex2 : -1;
    if (index >= 0)
    {
        PreviewEvent e;
        e.instrumentId = instrumentId; e.pitch = pitch; e.velocity = velocity;
        e.gateSamples = (int) std::llround (seconds * transport.getSampleRate());
        previewEvents[(size_t) index] = e;
    }
}

int AudioGraph::getNumSynthVoices() const noexcept
{
    int n = 0;
    for (const auto& slot : synthSlots) if (slot.id >= 0) n += slot.synth.getNumActiveVoices();
    return n;
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

    rebindSynthSlots();

    // Voices may only reference kits the new snapshot keeps alive.
    std::array<const DrumKit*, 64> kits {};
    int numKits = 0;
    for (const auto& p : current->patterns)
        if (p.kit != nullptr && numKits < (int) kits.size())
            kits[(size_t) numKits++] = p.kit.get();
    drums.killVoicesNotUsing (kits.data(), numKits);
}

void AudioGraph::rebindSynthSlots()
{
    // Drop slots whose instrument left the snapshot.
    for (auto& slot : synthSlots)
    {
        if (slot.id < 0) continue;
        bool present = false;
        for (const auto& rs : current->synths) if (rs.instrumentId == slot.id) { present = true; break; }
        if (! present) { slot.synth.reset(); slot.synth.setParams (nullptr); slot.id = -1; }
    }

    // Bind every synth in the snapshot to a slot (existing or free).
    for (const auto& rs : current->synths)
    {
        SynthSlot* slot = nullptr;
        for (auto& s : synthSlots) if (s.id == rs.instrumentId) { slot = &s; break; }
        if (slot == nullptr)
            for (auto& s : synthSlots) if (s.id < 0) { slot = &s; break; }
        if (slot == nullptr) continue;   // more than maxSynths instruments: extras are silent

        if (slot->id < 0) { slot->id = rs.instrumentId; slot->synth.prepare (transport.getSampleRate()); }
        slot->synth.setParams (rs.params.get());
        slot->synth.setPan (rs.pan);
    }
}

Synth* AudioGraph::synthForId (int instrumentId) noexcept
{
    for (auto& s : synthSlots) if (s.id == instrumentId) return &s.synth;
    return nullptr;
}

void AudioGraph::releaseAllSynths (bool immediate) noexcept
{
    for (auto& s : synthSlots) if (s.id >= 0) s.synth.allNotesOff (immediate);
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
            if (e.kit != nullptr)
            {
                if (snapshotHasKit (e.kit))
                    drums.trigger (e.kit, e.pad, e.velocity, current->masterGain, 0);
            }
            else if (auto* synth = synthForId (e.instrumentId))
                synth->noteOn (e.pitch, e.velocity * current->masterGain, 0, juce::jmax (1, e.gateSamples));
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

        // Does this segment touch a fade region?
        const juce::int64 relStart = from - clip.timelineStart;
        const juce::int64 relEnd = relStart + count;
        const bool inFade = (clip.fadeIn > 0 && relStart < clip.fadeIn)
                         || (clip.fadeOut > 0 && relEnd > clip.length - clip.fadeOut);

        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr)
                continue;

            // Mono sources feed every output; multichannel sources map 1:1
            // and the last channel repeats for any extra outputs.
            const int srcCh = juce::jmin (ch, srcChannels - 1);
            const float* src = clip.audio->getReadPointer (srcCh, (int) srcStart);
            float* dst = outputs[ch] + outOffset;
            const float chGain = gain * panGainForChannel (clip.pan, ch);

            if (! inFade)
            {
                juce::FloatVectorOperations::addWithMultiply (dst, src, chGain, count);
            }
            else
            {
                for (int i = 0; i < count; ++i)
                    dst[i] += src[i] * chGain * clipEnvelopeAt (relStart + i, clip.length, clip.fadeIn, clip.fadeInShape,
                                                                clip.fadeOut, clip.fadeOutShape);
            }
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

        // Pattern time zero sits `loopOffset` samples before the clip start.
        const juce::int64 origin = rp.timelineStart - rp.loopOffset;
        juce::int64 k = (juce::int64) std::floor ((double) (from - origin) / stepDur);

        for (;; ++k)
        {
            const juce::int64 t = origin + (juce::int64) std::llround ((double) k * stepDur);
            if (t >= to) break;
            if (t < from) continue;

            const int numSteps = rp.pattern->numSteps;
            const int step = (int) (((k % numSteps) + numSteps) % numSteps);
            const int delay = (int) (t - rangeStart);

            for (int pad = 0; pad < juce::jmin (DrumKit::numPads, StepPattern::maxPads); ++pad)
            {
                const auto v = rp.pattern->velocity[(size_t) pad][(size_t) step];
                if (v > 0)
                    drums.trigger (rp.kit.get(), pad, (float) v / 127.0f, rp.gain * current->masterGain, delay, rp.pan);
            }
        }
    }
}

void AudioGraph::scheduleMidi (juce::int64 rangeStart, int numSamples)
{
    const juce::int64 rangeEnd = rangeStart + numSamples;
    const double samplesPerBeat = transport.getSampleRate() * 60.0 / transport.getBpm();

    for (const auto& clip : current->midiClips)
    {
        if (clip.sequence == nullptr || clip.length <= 0) continue;
        auto* synth = synthForId (clip.instrumentId);
        if (synth == nullptr) continue;

        const auto& seq = *clip.sequence;
        const juce::int64 clipEnd = clip.timelineStart + clip.length;
        const juce::int64 from = juce::jmax (rangeStart, clip.timelineStart);
        const juce::int64 to   = juce::jmin (rangeEnd, clipEnd);
        if (from >= to) continue;

        const double periodSamples = seq.lengthBeats * samplesPerBeat;
        const bool loops = periodSamples >= 1.0;
        const juce::int64 origin = clip.timelineStart - clip.loopOffset;
        juce::int64 firstIteration = loops ? (juce::int64) std::floor ((double) (from - origin) / periodSamples) : 0;

        for (juce::int64 k = firstIteration; ; ++k)
        {
            const juce::int64 iterationStart = origin + (juce::int64) std::llround ((double) k * periodSamples);
            if (iterationStart >= to) break;

            for (const auto& note : seq.notes)
            {
                const juce::int64 t = iterationStart + (juce::int64) std::llround (note.startBeat * samplesPerBeat);
                if (t < from || t >= to) continue;

                const juce::int64 gate = juce::jmin ((juce::int64) std::llround (note.lengthBeats * samplesPerBeat), clipEnd - t);
                synth->noteOn (note.pitch, (float) note.velocity / 127.0f * clip.gain * current->masterGain,
                               (int) (t - rangeStart), (int) juce::jmax<juce::int64> (1, gate));
            }

            if (! loops) break;
        }
    }
}

void AudioGraph::renderRange (float* const* outputs, int numOutputs, int numSamples)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    swapInPendingSnapshot();

    const bool playing = transport.isPlaying();
    if (wasPlaying && ! playing)
        releaseAllSynths (false);       // stop: let held notes release
    wasPlaying = playing;

    if (current != nullptr)
    {
        processPreviewEvents();

        if (playing)
        {
            const juce::int64 pos = transport.getPositionSamples();
            mixClips (outputs, numOutputs, pos, numSamples);
            scheduleSequencer (pos, numSamples);
            scheduleMidi (pos, numSamples);
        }
    }

    // Instrument voices always render so previews sound while stopped and
    // tails ring out after stop. Library preview likewise ignores the transport.
    drums.render (outputs, numOutputs, numSamples);
    for (auto& slot : synthSlots)
        if (slot.id >= 0) slot.synth.render (outputs, numOutputs, numSamples);
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
        {
            transport.setPositionSamples (transport.getLoopStart());
            releaseAllSynths (false);   // notes don't hang across the loop point
        }

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
    for (auto& slot : synthSlots)
        if (slot.id >= 0) slot.synth.prepare (transport.getSampleRate());
}

void AudioGraph::audioDeviceStopped() {}

} // namespace beatmaker::engine
