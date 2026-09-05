#include "PitchCorrection.h"
#include <cmath>

namespace beatmaker::engine
{

//==============================================================================
// Detector

void PitchDetector::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    decimation = sampleRate > 60000.0 ? 4 : 2;
    const double dsr = sampleRate / decimation;
    const int windowSize = (int) (dsr * 0.04);                 // 40 ms: two periods of 60 Hz... enough for the YIN lag search
    const int maxLag = (int) (dsr / minHz) + 1;
    window.assign ((size_t) (windowSize + maxLag), 0.0f);
    difference.assign ((size_t) maxLag + 1, 0.0f);
    ring.assign (window.size() * 2, 0.0f);
    reset();
}

void PitchDetector::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos = 0;
    decimAccum = 0.0f;
    decimCount = 0;
}

void PitchDetector::push (const float* mono, int n) noexcept
{
    if (ring.empty()) return;
    for (int i = 0; i < n; ++i)
    {
        decimAccum += mono[i];
        if (++decimCount == decimation)
        {
            ring[(size_t) writePos] = decimAccum / (float) decimation;
            writePos = (writePos + 1) % (int) ring.size();
            decimAccum = 0.0f;
            decimCount = 0;
        }
    }
}

float PitchDetector::detect() noexcept
{
    if (ring.empty()) return 0.0f;
    const int total = (int) window.size();
    const int maxLag = (int) difference.size() - 1;
    const int frame = total - maxLag;
    const double dsr = sampleRate / decimation;

    // Latest `total` decimated samples, oldest first.
    int start = (writePos - total + (int) ring.size()) % (int) ring.size();
    float energy = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        const float v = ring[(size_t) ((start + i) % (int) ring.size())];
        window[(size_t) i] = v;
        energy += v * v;
    }
    if (energy / (float) total < 1.0e-6f) return 0.0f;   // ~ -60 dB RMS: silence

    // YIN difference function with cumulative mean normalisation
    const int minLag = juce::jmax (2, (int) (dsr / maxHz));
    difference[0] = 1.0f;
    float running = 0.0f;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        float d = 0.0f;
        for (int i = 0; i < frame; ++i)
        {
            const float diff = window[(size_t) i] - window[(size_t) (i + lag)];
            d += diff * diff;
        }
        running += d;
        difference[(size_t) lag] = running > 0.0f ? d * (float) lag / running : 1.0f;
    }

    // First dip below the threshold, refined to its local minimum
    const float threshold = 0.15f;
    int best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (difference[(size_t) lag] < threshold)
        {
            while (lag + 1 <= maxLag && difference[(size_t) lag + 1] < difference[(size_t) lag]) ++lag;
            best = lag;
            break;
        }
    }
    if (best < 0) return 0.0f;

    // Parabolic interpolation around the minimum
    double refined = best;
    if (best > 0 && best < maxLag)
    {
        const float a = difference[(size_t) best - 1], b = difference[(size_t) best], c = difference[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        if (std::abs (denom) > 1.0e-9f) refined += 0.5 * (a - c) / denom;
    }
    return (float) (dsr / refined);
}

//==============================================================================
// Scales

const char* PitchCorrectionEffect::keyName (int k)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[juce::jlimit (0, 11, k)];
}

const char* PitchCorrectionEffect::scaleName (int s)
{
    switch (s)
    {
        case chromatic:       return "Chromatic";
        case major:           return "Major";
        case minor:           return "Minor";
        case majorPentatonic: return "Major Pentatonic";
        case minorPentatonic: return "Minor Pentatonic";
        default:              return "";
    }
}

bool PitchCorrectionEffect::scaleContains (int s, int semitone) noexcept
{
    semitone = ((semitone % 12) + 12) % 12;
    switch (s)
    {
        case major:           return semitone == 0 || semitone == 2 || semitone == 4 || semitone == 5 || semitone == 7 || semitone == 9 || semitone == 11;
        case minor:           return semitone == 0 || semitone == 2 || semitone == 3 || semitone == 5 || semitone == 7 || semitone == 8 || semitone == 10;
        case majorPentatonic: return semitone == 0 || semitone == 2 || semitone == 4 || semitone == 7 || semitone == 9;
        case minorPentatonic: return semitone == 0 || semitone == 3 || semitone == 5 || semitone == 7 || semitone == 10;
        case chromatic:
        default:              return true;
    }
}

float PitchCorrectionEffect::nearestScaleNote (float midi, int keyIndex, int scaleIndex) noexcept
{
    const int rounded = (int) std::lround (midi);
    float best = (float) rounded;
    float bestDistance = 1.0e9f;
    for (int candidate = rounded - 6; candidate <= rounded + 6; ++candidate)
    {
        if (! scaleContains (scaleIndex, candidate - keyIndex)) continue;
        const float distance = std::abs ((float) candidate - midi);
        if (distance < bestDistance - 1.0e-6f || (std::abs (distance - bestDistance) <= 1.0e-6f && candidate < best))
        {
            bestDistance = distance;
            best = (float) candidate;
        }
    }
    return best;
}

//==============================================================================
// Effect

void PitchCorrectionEffect::prepareImpl (int maxBlockSize)
{
    maxBlock = juce::jmax (16, maxBlockSize);
    using RB = RubberBand::RubberBandStretcher;
    shifter = std::make_unique<RB> ((size_t) sampleRate, 2,
                                    RB::OptionProcessRealTime | RB::OptionEngineFiner | RB::OptionPitchHighConsistency
                                        | RB::OptionWindowShort | RB::OptionChannelsTogether | RB::OptionFormantShifted,
                                    1.0, 1.0);
    shifter->setMaxProcessSize ((size_t) maxBlock);
    latency = (int) shifter->getStartDelay();
    formantPreserved = false;

    detector.prepare (sampleRate);
    monoScratch.assign ((size_t) maxBlock, 0.0f);
    ringSize = latency + maxBlock * 4 + 16;
    wetRing.setSize (2, ringSize);
    shiftOut.setSize (2, maxBlock * 4 + latency + 16);
    dryLine.setSize (2, latency + 1);
    reset();
}

void PitchCorrectionEffect::reset()
{
    if (shifter != nullptr) shifter->reset();
    detector.reset();
    wetRing.clear(); dryLine.clear();
    ringWrite = ringRead = ringCount = 0;
    dryWrite = 0;
    currentCorrection.store (0.0f);
    detectedMidi.store (-1.0f);
    targetMidi.store (-1.0f);
    lastTarget = -1.0f;
}

void PitchCorrectionEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    if (shifter == nullptr || n > maxBlock) return;
    const int channels = juce::jmin (2, buffer.getNumChannels());
    if (channels == 0) return;

    // ---- Detection on a mono mix of this block
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < channels; ++ch) m += buffer.getSample (ch, i);
        monoScratch[(size_t) i] = m / (float) channels;
    }
    detector.push (monoScratch.data(), n);
    const float hz = detector.detect();

    const int keyIndex = juce::jlimit (0, 11, (int) std::lround (p.values[key]));
    const int scaleIndex = juce::jlimit (0, (int) numScales - 1, (int) std::lround (p.values[scale]));
    const float amountFraction = juce::jlimit (0.0f, 1.0f, p.values[amount] * 0.01f);
    const float transposeSemis = juce::jlimit (-12.0f, 12.0f, p.values[transpose]);

    float target = 0.0f;   // semitones of correction wanted
    if (hz > 0.0f)
    {
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const float note = nearestScaleNote (midi, keyIndex, scaleIndex);
        target = (note - midi) * amountFraction;
        detectedMidi.store (midi, std::memory_order_relaxed);
        targetMidi.store (note + transposeSemis, std::memory_order_relaxed);
        lastTarget = target;
    }
    else
    {
        detectedMidi.store (-1.0f, std::memory_order_relaxed);
        targetMidi.store (-1.0f, std::memory_order_relaxed);
        target = lastTarget >= -100.0f ? lastTarget : 0.0f;   // unvoiced: hold the last correction
    }

    // Retune speed: time constant of the glide toward the target correction
    const float speedMs = juce::jmax (0.0f, p.values[speed]);
    float correction = currentCorrection.load (std::memory_order_relaxed);
    if (speedMs < 0.5f) correction = target;
    else
    {
        const float coef = 1.0f - std::exp (-(float) n / (float) (speedMs * 0.001 * sampleRate));
        correction += (target - correction) * coef;
    }
    currentCorrection.store (correction, std::memory_order_relaxed);

    const bool wantFormant = p.values[formant] >= 0.5f;
    if (wantFormant != formantPreserved)
    {
        formantPreserved = wantFormant;
        shifter->setFormantOption (wantFormant ? RubberBand::RubberBandStretcher::OptionFormantPreserved
                                               : RubberBand::RubberBandStretcher::OptionFormantShifted);
    }
    shifter->setPitchScale (std::pow (2.0, (correction + transposeSemis) / 12.0));

    // ---- Shift: feed the block, pull whatever is ready into the ring
    const float* in[2] = { buffer.getReadPointer (0), buffer.getReadPointer (juce::jmin (1, channels - 1)) };
    shifter->process (in, (size_t) n, false);
    for (int avail = shifter->available(); avail > 0; avail = shifter->available())
    {
        const int take = juce::jmin (avail, shiftOut.getNumSamples());
        float* outs[2] = { shiftOut.getWritePointer (0), shiftOut.getWritePointer (1) };
        const int got = (int) shifter->retrieve (outs, (size_t) take);
        if (got <= 0) break;
        for (int i = 0; i < got; ++i)
        {
            if (ringCount < ringSize)
            {
                wetRing.setSample (0, ringWrite, shiftOut.getSample (0, i));
                wetRing.setSample (1, ringWrite, shiftOut.getSample (1, i));
                ringWrite = (ringWrite + 1) % ringSize;
                ++ringCount;
            }
        }
        if (take < avail) continue;
        break;
    }

    // ---- Output: wet from the ring (zeros while the latency fills), dry delayed to match
    const float wetGain = juce::jlimit (0.0f, 1.0f, p.values[mix] * 0.01f), dryGain = 1.0f - wetGain;
    const int dryLen = dryLine.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        float wetL = 0.0f, wetR = 0.0f;
        if (ringCount > 0)
        {
            wetL = wetRing.getSample (0, ringRead);
            wetR = wetRing.getSample (1, ringRead);
            ringRead = (ringRead + 1) % ringSize;
            --ringCount;
        }
        const int readPos = (dryWrite - latency + dryLen) % dryLen;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float dryNow = buffer.getSample (ch, i);
            const float dryDelayed = latency > 0 ? dryLine.getSample (ch, readPos) : dryNow;
            dryLine.setSample (ch, dryWrite, dryNow);
            buffer.setSample (ch, i, dryDelayed * dryGain + (ch == 0 ? wetL : wetR) * wetGain);
        }
        dryWrite = (dryWrite + 1) % dryLen;
    }
}

} // namespace beatmaker::engine
