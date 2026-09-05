#include "AudioGraph.h"
#include <cmath>

namespace beatmaker::engine
{

AudioGraph::AudioGraph (Transport& transportToUse) : transport (transportToUse)
{
    for (auto& b : busBuffers) b.setSize (2, maxBlock);
}

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

float AudioGraph::getStripPeak (int strip, int channel) const noexcept
{
    return juce::isPositiveAndBelow (strip, maxStrips) && juce::isPositiveAndBelow (channel, 2)
             ? stripMeters[(size_t) strip].peak[(size_t) channel].load (std::memory_order_relaxed) : 0.0f;
}

float AudioGraph::getMasterPeak (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, 2) ? masterMeter.peak[(size_t) channel].load (std::memory_order_relaxed) : 0.0f;
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
        slot->strip = rs.strip;
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
                for (const auto& p : current->patterns)
                    if (p.kit.get() == e.kit) { drums.trigger (e.kit, e.pad, e.velocity, current->masterGain, 0, p.strip); break; }
            }
            else if (auto* synth = synthForId (e.instrumentId))
                synth->noteOn (e.pitch, e.velocity * current->masterGain, 0, juce::jmax (1, e.gateSamples));
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

void AudioGraph::mixClips (int stripIndex, juce::int64 blockStart, int numSamples)
{
    const juce::int64 blockEnd = blockStart + numSamples;
    float* outputs[2] = { stripBuffer.getWritePointer (0), stripBuffer.getWritePointer (1) };
    constexpr int numOutputs = 2;

    for (const auto& clip : current->clips)
    {
        if (clip.audio == nullptr || clip.length <= 0 || clip.strip != stripIndex)
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
            const float chGain = gain;

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
                    drums.trigger (rp.kit.get(), pad, (float) v / 127.0f, rp.gain * current->masterGain, delay, rp.strip);
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

void AudioGraph::mixMonitoredInputs (int stripIndex, const float* const* inputs, int numInputs, int numSamples)
{
    if (inputs == nullptr || numInputs <= 0)
        return;

    for (const auto& m : current->monitors)
    {
        if (m.strip != stripIndex) continue;
        for (int ch = 0; ch < 2; ++ch)
        {
            const int idx = m.firstInput + juce::jmin (ch, m.numInputs - 1);
            if (idx < 0 || idx >= numInputs || inputs[idx] == nullptr) continue;
            juce::FloatVectorOperations::addWithMultiply (stripBuffer.getWritePointer (ch), inputs[idx], m.gain, numSamples);
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

//==============================================================================
// Strips

void AudioGraph::renderStripSources (int stripIndex, const float* const* inputs, int numInputs, juce::int64 pos, bool playing, int numSamples)
{
    stripBuffer.clear (0, numSamples);

    if (playing)
        mixClips (stripIndex, pos, numSamples);

    float* outs[2] = { stripBuffer.getWritePointer (0), stripBuffer.getWritePointer (1) };
    drums.render (outs, 2, numSamples, stripIndex);
    for (auto& slot : synthSlots)
        if (slot.id >= 0 && slot.strip == stripIndex)
            slot.synth.render (outs, 2, numSamples);

    mixMonitoredInputs (stripIndex, inputs, numInputs, numSamples);
}

void AudioGraph::processInserts (const std::vector<RenderInsert>& inserts, juce::AudioBuffer<float>& buffer, int numSamples)
{
    for (const auto& ins : inserts)
    {
        if (ins.fx == nullptr || ins.params == nullptr || ins.bypass) continue;
        if (std::abs (ins.fx->getSampleRate() - transport.getSampleRate()) > 0.5) continue;   // prepared for another rate: skip
        ins.fx->process (buffer, numSamples, *ins.params);
    }
}

void AudioGraph::processStrip (const RenderStrip& strip, int stripIndex, int numSamples)
{
    processInserts (strip.inserts, stripBuffer, numSamples);

    auto& meter = stripMeters[(size_t) juce::jlimit (0, maxStrips - 1, stripIndex)];

    if (strip.muted)
    {
        for (auto& p : meter.peak) p.store (0.0f, std::memory_order_relaxed);
        return;
    }

    // Pre-fader sends
    for (const auto& send : strip.sends)
        if (send.preFader && juce::isPositiveAndBelow (send.bus, numBuses) && send.gain > 0.0f)
            for (int ch = 0; ch < 2; ++ch)
                busBuffers[(size_t) send.bus].addFrom (ch, 0, stripBuffer, ch, 0, numSamples, send.gain);

    // Fader + pan
    for (int ch = 0; ch < 2; ++ch)
        stripBuffer.applyGain (ch, 0, numSamples, strip.gain * panGainForChannel (strip.pan, ch));

    for (int ch = 0; ch < 2; ++ch)
        meter.peak[(size_t) ch].store (stripBuffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);

    // Post-fader sends
    for (const auto& send : strip.sends)
        if (! send.preFader && juce::isPositiveAndBelow (send.bus, numBuses) && send.gain > 0.0f)
            for (int ch = 0; ch < 2; ++ch)
                busBuffers[(size_t) send.bus].addFrom (ch, 0, stripBuffer, ch, 0, numSamples, send.gain);

    // Output
    auto& dest = juce::isPositiveAndBelow (strip.outputBus, numBuses) ? busBuffers[(size_t) strip.outputBus] : mainBuffer;
    for (int ch = 0; ch < 2; ++ch)
        dest.addFrom (ch, 0, stripBuffer, ch, 0, numSamples);
}

void AudioGraph::renderRange (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples)
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

        const juce::int64 pos = transport.getPositionSamples();
        if (playing)
        {
            scheduleSequencer (pos, numSamples);
            scheduleMidi (pos, numSamples);
        }

        mainBuffer.clear (0, numSamples);
        for (auto& b : busBuffers) b.clear (0, numSamples);

        const bool useDefault = current->strips.empty();
        const int numStrips = useDefault ? 1 : (int) current->strips.size();
        auto stripAt = [&] (int i) -> const RenderStrip& { return useDefault ? defaultStrip : current->strips[(size_t) i]; };

        // Source strips first, then aux strips (which read the buses the others filled).
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < numStrips; ++i)
            {
                const auto& strip = stripAt (i);
                if (strip.isAux != (pass == 1)) continue;

                if (strip.isAux)
                {
                    stripBuffer.clear (0, numSamples);
                    if (juce::isPositiveAndBelow (strip.inputBus, numBuses))
                        for (int ch = 0; ch < 2; ++ch)
                            stripBuffer.copyFrom (ch, 0, busBuffers[(size_t) strip.inputBus], ch, 0, numSamples);
                }
                else
                    renderStripSources (i, inputs, numInputs, pos, playing, numSamples);

                processStrip (strip, i, numSamples);
            }

        // Master
        processInserts (current->master.inserts, mainBuffer, numSamples);
        mainBuffer.applyGain (0, numSamples, current->master.gain);
        for (int ch = 0; ch < 2; ++ch)
            masterMeter.peak[(size_t) ch].store (mainBuffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);

        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::add (outputs[ch], mainBuffer.getReadPointer (juce::jmin (ch, 1)), numSamples);
    }

    mixPreview (outputs, numOutputs, numSamples);

    for (int ch = 0; ch < juce::jmin (numOutputs, (int) outputPeak.size()); ++ch)
        if (outputs[ch] != nullptr)
            outputPeak[(size_t) ch].store (juce::FloatVectorOperations::findMaximum (outputs[ch], numSamples),
                                           std::memory_order_relaxed);

    if (playing)
        transport.advance (numSamples);
}

void AudioGraph::renderBlock (const float* const* inputs, int numInputs,
                              float* const* outputs, int numOutputs, int numSamples)
{
    numOutputs = juce::jmin (numOutputs, maxOutputs);

    // Capture first: the transport position is still the block start.
    if (recorder != nullptr)
        recorder->processInput (inputs, numInputs, numSamples);
    std::array<float*, maxOutputs> offsetOutputs {};

    std::array<const float*, maxOutputs> offsetInputs {};
    numInputs = juce::jmin (numInputs, maxOutputs);

    int done = 0;
    while (done < numSamples)
    {
        int count = juce::jmin (numSamples - done, maxBlock);
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
            for (int ch = 0; ch < numInputs; ++ch)
                offsetInputs[(size_t) ch] = (inputs != nullptr && inputs[ch] != nullptr) ? inputs[ch] + done : nullptr;

            renderRange (inputs != nullptr ? offsetInputs.data() : nullptr, numInputs, offsetOutputs.data(), numOutputs, count);
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
