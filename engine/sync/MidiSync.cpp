#include "MidiSync.h"
#include <cmath>

namespace beatmaker::engine
{

Timecode Timecode::fromSeconds (double s, MtcFrameRate rate)
{
    const double fps = mtcFramesPerSecond (rate);
    s = juce::jmax (0.0, s);
    Timecode t;
    const long long totalFrames = (long long) std::floor (s * fps + 1.0e-6);
    const long long framesPerSecond = (long long) std::round (fps);
    t.frames = (int) (totalFrames % framesPerSecond);
    const long long wholeSeconds = totalFrames / framesPerSecond;
    t.seconds = (int) (wholeSeconds % 60);
    t.minutes = (int) ((wholeSeconds / 60) % 60);
    t.hours = (int) ((wholeSeconds / 3600) % 24);
    return t;
}

double Timecode::toSeconds (MtcFrameRate rate) const
{
    return hours * 3600.0 + minutes * 60.0 + seconds + frames / std::round (mtcFramesPerSecond (rate));
}

juce::String Timecode::toString() const
{
    auto two = [] (int v) { return juce::String (v).paddedLeft ('0', 2); };
    return two (hours) + ":" + two (minutes) + ":" + two (seconds) + ":" + two (frames);
}

//==============================================================================

void MidiClockGenerator::reset() { wasPlaying = false; started = false; lastPosition = -1.0; lastTick = -1; }

std::vector<TimedMidiMessage> MidiClockGenerator::update (const TransportSnapshot& now, double wallTime)
{
    std::vector<TimedMidiMessage> out;
    const double beat = now.positionSeconds * now.bpm / 60.0;
    const long long tick = (long long) std::floor (beat * 24.0 + 1.0e-9);

    if (now.playing && ! wasPlaying)
    {
        // Song position (16th notes) then Start or Continue
        const int sixteenths = (int) juce::jlimit (0.0, 16383.0, std::floor (beat * 4.0));
        if (sixteenths > 0 || started)
        {
            out.push_back ({ juce::MidiMessage::songPositionPointer (sixteenths), wallTime });
            out.push_back ({ juce::MidiMessage::midiContinue(), wallTime });
        }
        else out.push_back ({ juce::MidiMessage::midiStart(), wallTime });
        started = true;
        lastTick = tick - 1;
    }
    else if (! now.playing && wasPlaying)
        out.push_back ({ juce::MidiMessage::midiStop(), wallTime });
    else if (! now.playing && lastPosition >= 0.0 && std::abs (now.positionSeconds - lastPosition) > 1.0e-6)
        out.push_back ({ juce::MidiMessage::songPositionPointer ((int) juce::jlimit (0.0, 16383.0, std::floor (beat * 4.0))), wallTime });

    if (now.playing)
    {
        // Every clock tick since the last update, timestamped by its exact position.
        if (tick < lastTick) lastTick = tick - 1;   // cycle wrapped
        const double secondsPerTick = 60.0 / now.bpm / 24.0;
        for (long long k = lastTick + 1; k <= tick; ++k)
            out.push_back ({ juce::MidiMessage::midiClock(), wallTime - (beat * 24.0 - (double) k) * secondsPerTick });
        lastTick = tick;
    }
    wasPlaying = now.playing;
    lastPosition = now.positionSeconds;
    return out;
}

//==============================================================================

void MtcGenerator::reset() { wasPlaying = false; lastPosition = -1.0; lastQuarter = -1; }

juce::MidiMessage MtcGenerator::fullFrame (const Timecode& tc, MtcFrameRate rate)
{
    const juce::uint8 data[] = { 0x7f, 0x7f, 0x01, 0x01, (juce::uint8) ((mtcRateCode (rate) << 5) | tc.hours), (juce::uint8) tc.minutes, (juce::uint8) tc.seconds, (juce::uint8) tc.frames };
    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

juce::MidiMessage MtcGenerator::quarterFrame (int piece, const Timecode& tc, MtcFrameRate rate)
{
    int value = 0;
    switch (piece & 7)
    {
        case 0: value = tc.frames & 0x0f; break;
        case 1: value = (tc.frames >> 4) & 0x01; break;
        case 2: value = tc.seconds & 0x0f; break;
        case 3: value = (tc.seconds >> 4) & 0x03; break;
        case 4: value = tc.minutes & 0x0f; break;
        case 5: value = (tc.minutes >> 4) & 0x03; break;
        case 6: value = tc.hours & 0x0f; break;
        case 7: value = ((tc.hours >> 4) & 0x01) | (mtcRateCode (rate) << 1); break;
    }
    return juce::MidiMessage::quarterFrame (piece & 7, value);
}

std::vector<TimedMidiMessage> MtcGenerator::update (const TransportSnapshot& now, double wallTime)
{
    std::vector<TimedMidiMessage> out;
    const double fps = mtcFramesPerSecond (frameRate);
    const double tcSeconds = now.positionSeconds + offset;
    const double quarterSeconds = 1.0 / fps / 4.0;
    const long long quarter = (long long) std::floor (tcSeconds * fps * 4.0 + 1.0e-9);

    if ((! now.playing && wasPlaying) || (! now.playing && lastPosition >= 0.0 && std::abs (now.positionSeconds - lastPosition) > 1.0e-6)
        || (now.playing && ! wasPlaying))
    {
        out.push_back ({ fullFrame (Timecode::fromSeconds (tcSeconds, frameRate), frameRate), wallTime });
        lastQuarter = quarter - 1;
    }
    if (now.playing)
    {
        if (quarter < lastQuarter) lastQuarter = quarter - 1;
        for (long long q = lastQuarter + 1; q <= quarter; ++q)
        {
            // A quarter-frame sequence spans two frames; the timecode it encodes is the frame the sequence started on.
            const long long sequenceStart = q - (q % 8 + 8) % 8;
            const auto tc = Timecode::fromSeconds ((double) sequenceStart * quarterSeconds, frameRate);
            out.push_back ({ quarterFrame ((int) ((q % 8 + 8) % 8), tc, frameRate), wallTime - (tcSeconds * fps * 4.0 - (double) q) * quarterSeconds });
        }
        lastQuarter = quarter;
    }
    wasPlaying = now.playing;
    lastPosition = now.positionSeconds;
    return out;
}

//==============================================================================

MidiClockChaser::Action MidiClockChaser::handle (const juce::MidiMessage& m, double time)
{
    Action a;
    if (m.isMidiStart())          { running = true; ticks = 0; tickTimes.clear(); a.play = true; a.locateBeats = 0.0; }
    else if (m.isMidiContinue())  { running = true; tickTimes.clear(); a.cont = true; a.play = true; a.locateBeats = getBeatPosition(); }
    else if (m.isMidiStop())      { running = false; a.stop = true; }
    else if (m.isSongPositionPointer()) { ticks = (long long) m.getSongPositionPointerMidiBeat() * 6; a.locateBeats = getBeatPosition(); }
    else if (m.isMidiClock())
    {
        if (running) ++ticks;
        tickTimes.push_back (time);
        if (tickTimes.size() > 48) tickTimes.erase (tickTimes.begin());
        if (tickTimes.size() >= 12)
        {
            const double span = tickTimes.back() - tickTimes.front();
            if (span > 0.0)
            {
                const double secondsPerTick = span / (double) (tickTimes.size() - 1);
                const double estimate = 60.0 / (secondsPerTick * 24.0);
                if (estimate > 20.0 && estimate < 400.0 && std::abs (estimate - bpm) > 0.05) { bpm = estimate; a.bpm = bpm; }
            }
        }
    }
    return a;
}

MtcChaser::Action MtcChaser::handle (const juce::MidiMessage& m, double time)
{
    Action a;
    if (m.isFullFrame())
    {
        int h, mi, s, f; juce::MidiMessage::SmpteTimecodeType type;
        m.getFullFrameParameters (h, mi, s, f, type);
        frameRate = (MtcFrameRate) juce::jlimit (0, 3, (int) type);
        a.positionSeconds = Timecode { h & 0x1f, mi, s, f }.toSeconds (frameRate);
        a.locate = true;
        received = 0;
        return a;
    }
    if (! m.isQuarterFrame()) return a;
    const int piece = m.getQuarterFrameSequenceNumber(), value = m.getQuarterFrameValue();
    lastQuarterTime = time;
    a.running = true;
    if (piece == 0) received = 0;
    if (piece == received) { nibbles[piece] = value; ++received; }
    else received = 0;
    if (received == 8)
    {
        received = 0;
        frameRate = (MtcFrameRate) ((nibbles[7] >> 1) & 3);
        Timecode tc;
        tc.frames = nibbles[0] | ((nibbles[1] & 1) << 4);
        tc.seconds = nibbles[2] | ((nibbles[3] & 3) << 4);
        tc.minutes = nibbles[4] | ((nibbles[5] & 3) << 4);
        tc.hours = nibbles[6] | ((nibbles[7] & 1) << 4);
        // The sequence encodes the frame it started on: two frames have elapsed by the time it completes.
        a.positionSeconds = tc.toSeconds (frameRate) + 2.0 / mtcFramesPerSecond (frameRate);
    }
    return a;
}

} // namespace beatmaker::engine
