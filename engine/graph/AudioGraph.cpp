#include "AudioGraph.h"
#include <algorithm>
#include <cmath>

namespace beatmaker::engine
{

AudioGraph::AudioGraph (Transport& transportToUse) : transport (transportToUse)
{
    for (auto& b : busBuffers) b.setSize (2, maxBlock);
    loudness.prepare (transport.getSampleRate(), maxBlock);
}

void AudioGraph::Meter::update (const juce::AudioBuffer<float>& b, int numSamples, double sampleRate) noexcept
{
    const float a = (float) std::exp (-(double) numSamples / (0.3 * sampleRate));   // 300 ms integration
    bool clip = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float pk = b.getMagnitude (ch, 0, numSamples);
        peak[(size_t) ch].store (pk, std::memory_order_relaxed);
        clip = clip || pk > 1.0f;
        double sum = 0.0;
        const float* d = b.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i) sum += (double) d[i] * d[i];
        const float ms = (float) (sum / juce::jmax (1, numSamples));
        meanSquare[(size_t) ch].store (a * meanSquare[(size_t) ch].load (std::memory_order_relaxed) + (1.0f - a) * ms, std::memory_order_relaxed);
    }
    if (clip) clipped.store (true, std::memory_order_relaxed);
}

void AudioGraph::Meter::clear() noexcept
{
    for (auto& p : peak) p.store (0.0f, std::memory_order_relaxed);
    for (auto& m : meanSquare) m.store (0.0f, std::memory_order_relaxed);
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

float AudioGraph::getStripRms (int strip, int channel) const noexcept
{
    return juce::isPositiveAndBelow (strip, maxStrips) && juce::isPositiveAndBelow (channel, 2)
             ? std::sqrt (stripMeters[(size_t) strip].meanSquare[(size_t) channel].load (std::memory_order_relaxed)) : 0.0f;
}

float AudioGraph::getMasterRms (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, 2) ? std::sqrt (masterMeter.meanSquare[(size_t) channel].load (std::memory_order_relaxed)) : 0.0f;
}

bool AudioGraph::getAndClearStripClip (int strip) noexcept
{
    return juce::isPositiveAndBelow (strip, maxStrips) ? stripMeters[(size_t) strip].clipped.exchange (false) : false;
}

bool AudioGraph::getAndClearMasterClip() noexcept { return masterMeter.clipped.exchange (false); }

void AudioGraph::startScrub (int strip, juce::int64 sample)
{
    scrubStrip.store (strip);
    scrubTarget.store (sample);
    scrubbing.store (true);
}

void AudioGraph::setScrubTarget (juce::int64 sample) { scrubTarget.store (sample); }
void AudioGraph::stopScrub() { scrubbing.store (false); }

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

            const bool hasGainLane = clip.gainLane != nullptr && ! clip.gainLane->isEmpty();

            if (! inFade && ! hasGainLane)
            {
                juce::FloatVectorOperations::addWithMultiply (dst, src, chGain, count);
            }
            else if (! inFade)
            {
                // Clip gain line: ramp between the block edges unless a breakpoint falls inside.
                const auto& lane = *clip.gainLane;
                const juce::int64 s0 = srcStart, s1 = srcStart + count;
                bool breakpointInside = false;
                for (const auto& p : lane.points) if (p.time > s0 && p.time < s1) { breakpointInside = true; break; }
                if (! breakpointInside)
                {
                    const float g0 = chGain * lane.valueAt (s0, 1.0f), g1 = chGain * lane.valueAt (s1, 1.0f);
                    const float step = (g1 - g0) / (float) count;
                    float g = g0;
                    for (int i = 0; i < count; ++i) { dst[i] += src[i] * g; g += step; }
                }
                else
                    for (int i = 0; i < count; ++i) dst[i] += src[i] * chGain * lane.valueAt (s0 + i, 1.0f);
            }
            else
            {
                for (int i = 0; i < count; ++i)
                {
                    float g = chGain * clipEnvelopeAt (relStart + i, clip.length, clip.fadeIn, clip.fadeInShape, clip.fadeOut, clip.fadeOutShape);
                    if (hasGainLane) g *= clip.gainLane->valueAt (srcStart + i, 1.0f);
                    dst[i] += src[i] * g;
                }
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

// Scrub: slide the read position toward the target at up to 2x speed and
// read the strip's clips with linear interpolation.
void AudioGraph::mixClipsScrub (int stripIndex, int numSamples)
{
    const double target = (double) scrubTarget.load (std::memory_order_relaxed);
    const double sr = transport.getSampleRate();
    float* outputs[2] = { stripBuffer.getWritePointer (0), stripBuffer.getWritePointer (1) };

    for (int i = 0; i < numSamples; ++i)
    {
        // Rate proportional to distance (settles in ~50 ms), capped at 2x
        const double rate = juce::jlimit (-2.0, 2.0, (target - scrubPosition) / (0.05 * sr));
        scrubPosition += rate;
        if (std::abs (rate) < 1.0e-3) continue;   // parked: silence

        for (const auto& clip : current->clips)
        {
            if (clip.audio == nullptr || clip.strip != stripIndex) continue;
            const double rel = scrubPosition - (double) clip.timelineStart;
            if (rel < 0.0 || rel >= (double) clip.length) continue;
            const double src = (double) clip.sourceOffset + rel;
            const int s0 = (int) src;
            if (s0 + 1 >= clip.audio->getNumSamples()) continue;
            const float frac = (float) (src - s0);
            const int channels = clip.audio->getNumChannels();
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = clip.audio->getReadPointer (juce::jmin (ch, channels - 1));
                outputs[ch][i] += (d[s0] + (d[s0 + 1] - d[s0]) * frac) * clip.gain;
            }
        }
    }
}

//==============================================================================
// Strips

void AudioGraph::renderStripSources (int stripIndex, const float* const* inputs, int numInputs, juce::int64 pos, bool playing, int numSamples)
{
    stripBuffer.clear (0, numSamples);

    const bool scrub = scrubbing.load (std::memory_order_relaxed) && ! playing;
    if (scrub && (scrubStrip.load (std::memory_order_relaxed) < 0 || scrubStrip.load (std::memory_order_relaxed) == stripIndex))
        mixClipsScrub (stripIndex, numSamples);
    else if (playing)
        mixClips (stripIndex, pos, numSamples);

    float* outs[2] = { stripBuffer.getWritePointer (0), stripBuffer.getWritePointer (1) };
    drums.render (outs, 2, numSamples, stripIndex);
    for (auto& slot : synthSlots)
        if (slot.id >= 0 && slot.strip == stripIndex)
            slot.synth.render (outs, 2, numSamples);

    mixMonitoredInputs (stripIndex, inputs, numInputs, numSamples);
}

const AutomationLane* AudioGraph::laneFor (const RenderStrip& strip, const ParamId& param) noexcept
{
    if (! strip.automationRead) return nullptr;
    for (const auto& a : strip.automation)
        if (! a.bypass && a.lane != nullptr && a.lane->param == param && ! a.lane->isEmpty())
            return a.lane.get();
    return nullptr;
}

void AudioGraph::processInserts (const std::vector<RenderInsert>& inserts, juce::AudioBuffer<float>& buffer, int numSamples,
                                 const RenderStrip* owner, juce::int64 blockStart)
{
    for (const auto& ins : inserts)
    {
        if (ins.fx == nullptr || ins.params == nullptr || ins.bypass) continue;
        if (std::abs (ins.fx->getSampleRate() - transport.getSampleRate()) > 0.5) continue;   // prepared for another rate: skip

        // Automated parameters override the stored values for this block.
        bool automated = false;
        InsertParams local;
        if (owner != nullptr && owner->automationRead)
            for (const auto& a : owner->automation)
                if (! a.bypass && a.lane != nullptr && ! a.lane->isEmpty()
                    && a.lane->param.type == ParamId::Type::insertParam && a.lane->param.index == ins.slot
                    && juce::isPositiveAndBelow (a.lane->param.sub, (int) local.values.size()))
                {
                    if (! automated) { local = *ins.params; automated = true; }
                    local.values[(size_t) a.lane->param.sub] = a.lane->valueAt (blockStart, local.values[(size_t) a.lane->param.sub]);
                }

        ins.fx->process (buffer, numSamples, automated ? local : *ins.params);
    }
}

void AudioGraph::StripDelay::process (juce::AudioBuffer<float>& io, int numSamples, int delay) noexcept
{
    delay = juce::jlimit (0, maxDelaySamples - 1, delay);
    if (delay == 0) return;   // ring stays untouched; a later non-zero delay starts from whatever is in it

    for (int i = 0; i < numSamples; ++i)
    {
        const int readPos = (writePos - delay + maxDelaySamples) % maxDelaySamples;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float in = io.getSample (ch, i);
            io.setSample (ch, i, ring.getSample (ch, readPos));
            ring.setSample (ch, writePos, in);
        }
        writePos = (writePos + 1) % maxDelaySamples;
    }
}

void AudioGraph::processStrip (const RenderStrip& strip, int stripIndex, juce::int64 blockStart, int numSamples,
                               float* const* outputs, int numOutputs)
{
    processInserts (strip.inserts, stripBuffer, numSamples, &strip, blockStart);

    // Automatic delay compensation (+ user offset), after the inserts so
    // sends and outputs are aligned alike.
    stripDelays[(size_t) juce::jlimit (0, maxStrips - 1, stripIndex)].process (stripBuffer, numSamples, strip.delaySamples);

    auto& meter = stripMeters[(size_t) juce::jlimit (0, maxStrips - 1, stripIndex)];

    // Automation (block-rate; volume ramps across the block)
    bool muted = strip.muted;
    if (auto* lane = laneFor (strip, ParamId::mute()))
        muted = muted || lane->valueAt (blockStart, 0.0f) >= 0.5f;

    if (muted)
    {
        meter.clear();
        return;
    }

    float gainStart = strip.gain, gainEnd = strip.gain;
    if (auto* lane = laneFor (strip, ParamId::volume()))
    {
        gainStart = lane->valueAt (blockStart, strip.gain);
        gainEnd   = lane->valueAt (blockStart + numSamples, strip.gain);
    }

    // Trim mode: a live relative offset on top of whatever the lane says.
    if (strip.automationRead) { gainStart *= strip.trimGain; gainEnd *= strip.trimGain; }

    // VCA master: its fader (and volume automation) scales ours.
    if (current != nullptr && juce::isPositiveAndBelow (strip.vcaStrip, (int) current->strips.size()))
    {
        const auto& vca = current->strips[(size_t) strip.vcaStrip];
        float vStart = vca.gain, vEnd = vca.gain;
        if (auto* lane = laneFor (vca, ParamId::volume()))
        {
            vStart = lane->valueAt (blockStart, vca.gain);
            vEnd   = lane->valueAt (blockStart + numSamples, vca.gain);
        }
        gainStart *= vStart;
        gainEnd   *= vEnd;
    }
    float pan = strip.pan;
    if (auto* lane = laneFor (strip, ParamId::pan()))
        pan = lane->valueAt (blockStart, strip.pan);

    auto sendGain = [&] (const RenderSend& send)
    {
        if (auto* lane = laneFor (strip, ParamId::send (send.slot))) return lane->valueAt (blockStart, send.gain);
        return send.gain;
    };

    // Pre-fader sends
    for (const auto& send : strip.sends)
        if (send.preFader && juce::isPositiveAndBelow (send.bus, numBuses))
            if (const float g = sendGain (send); g > 0.0f)
                for (int ch = 0; ch < 2; ++ch)
                    busBuffers[(size_t) send.bus].addFrom (ch, 0, stripBuffer, ch, 0, numSamples, g);

    // Fader (ramped) + pan
    for (int ch = 0; ch < 2; ++ch)
    {
        const float pg = panGainForChannel (pan, ch);
        stripBuffer.applyGainRamp (ch, 0, numSamples, gainStart * pg, gainEnd * pg);
    }

    meter.update (stripBuffer, numSamples, transport.getSampleRate());

    // Post-fader sends
    for (const auto& send : strip.sends)
        if (! send.preFader && juce::isPositiveAndBelow (send.bus, numBuses))
            if (const float g = sendGain (send); g > 0.0f)
                for (int ch = 0; ch < 2; ++ch)
                    busBuffers[(size_t) send.bus].addFrom (ch, 0, stripBuffer, ch, 0, numSamples, g);

    // Output: a bus, a direct device output pair (bypassing the master), or the main mix
    if (juce::isPositiveAndBelow (strip.outputBus, numBuses))
    {
        for (int ch = 0; ch < 2; ++ch)
            busBuffers[(size_t) strip.outputBus].addFrom (ch, 0, stripBuffer, ch, 0, numSamples);
    }
    else if (strip.outputChannel >= 0)
    {
        for (int ch = 0; ch < 2; ++ch)
            if (const int dev = strip.outputChannel + ch; dev < numOutputs && outputs[dev] != nullptr)
                juce::FloatVectorOperations::add (outputs[dev], stripBuffer.getReadPointer (ch), numSamples);
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
            mainBuffer.addFrom (ch, 0, stripBuffer, ch, 0, numSamples);
    }
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

    // Scrubbing starts from wherever the playhead is and drags it along.
    static thread_local bool wasScrubbing = false;
    const bool scrubNow = scrubbing.load (std::memory_order_relaxed) && ! playing;
    if (scrubNow && ! wasScrubbing) scrubPosition = (double) transport.getPositionSamples();
    wasScrubbing = scrubNow;

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
                if (strip.isVca) { stripMeters[(size_t) juce::jlimit (0, maxStrips - 1, i)].clear(); continue; }

                if (strip.isAux)
                {
                    stripBuffer.clear (0, numSamples);
                    if (juce::isPositiveAndBelow (strip.inputBus, numBuses))
                        for (int ch = 0; ch < 2; ++ch)
                            stripBuffer.copyFrom (ch, 0, busBuffers[(size_t) strip.inputBus], ch, 0, numSamples);
                }
                else
                    renderStripSources (i, inputs, numInputs, pos, playing, numSamples);

                processStrip (strip, i, pos, numSamples, outputs, numOutputs);
            }

        // Master (fader with its own volume automation and trim)
        processInserts (current->master.inserts, mainBuffer, numSamples);
        {
            const auto& m = current->master;
            float g0 = m.gain, g1 = m.gain;
            if (m.automationRead)
            {
                for (const auto& a : m.automation)
                    if (! a.bypass && a.lane != nullptr && a.lane->param == ParamId::volume() && ! a.lane->isEmpty())
                    {
                        g0 = a.lane->valueAt (pos, m.gain);
                        g1 = a.lane->valueAt (pos + numSamples, m.gain);
                        break;
                    }
                g0 *= m.trimGain; g1 *= m.trimGain;
            }
            for (int ch = 0; ch < 2; ++ch)
                mainBuffer.applyGainRamp (ch, 0, numSamples, g0, g1);
        }
        masterMeter.update (mainBuffer, numSamples, transport.getSampleRate());
        loudness.process (mainBuffer, numSamples);

        // Main output path: the first two channels get the master mix; extra
        // device channels (when the main path is the default) mirror it.
        const int mainFirst = juce::jmax (0, current->mainOutputChannel);
        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr) continue;
            if (ch >= mainFirst && ch < mainFirst + 2)
                juce::FloatVectorOperations::add (outputs[ch], mainBuffer.getReadPointer (ch - mainFirst), numSamples);
            else if (mainFirst == 0 && ch >= 2 && ! std::any_of (current->strips.begin(), current->strips.end(),
                                                                  [] (const RenderStrip& s) { return s.outputChannel >= 0; }))
                juce::FloatVectorOperations::add (outputs[ch], mainBuffer.getReadPointer (juce::jmin (ch, 1)), numSamples);
        }
    }

    mixPreview (outputs, numOutputs, numSamples);

    if (scrubNow)
        transport.setPositionSamples ((juce::int64) std::llround (scrubPosition));

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
    loudness.prepare (transport.getSampleRate(), maxBlock);
    for (auto& slot : synthSlots)
        if (slot.id >= 0) slot.synth.prepare (transport.getSampleRate());
}

void AudioGraph::audioDeviceStopped() {}

} // namespace beatmaker::engine
