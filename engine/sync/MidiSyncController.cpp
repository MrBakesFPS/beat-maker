#include "MidiSyncController.h"

namespace beatmaker::engine
{

MidiSyncController::MidiSyncController (Transport& t) : transport (t) {}

MidiSyncController::~MidiSyncController()
{
    stopTimer();
    if (input != nullptr) input->stop();
    if (output != nullptr) output->stopBackgroundThread();
}

void MidiSyncController::setMode (Mode m)
{
    mode = m;
    clock.reset(); mtc.reset();
    clockChaser = MidiClockChaser();
    mtcChaser = MtcChaser (frameRate);
    if (mode == Mode::sendClock || mode == Mode::sendMtc) startTimerHz (100);
    else if (mode == Mode::chaseMtc) startTimerHz (20);   // to notice the incoming stream stopping
    else stopTimer();
    if (input != nullptr) { if (mode == Mode::chaseClock || mode == Mode::chaseMtc) input->start(); else input->stop(); }
}

juce::StringArray MidiSyncController::outputDeviceNames()
{
    juce::StringArray names { virtualPortName };
    for (const auto& d : juce::MidiOutput::getAvailableDevices()) names.add (d.name);
    return names;
}

juce::StringArray MidiSyncController::inputDeviceNames()
{
    juce::StringArray names { virtualPortName };
    for (const auto& d : juce::MidiInput::getAvailableDevices()) names.add (d.name);
    return names;
}

bool MidiSyncController::setOutputDevice (const juce::String& name)
{
    if (output != nullptr) output->stopBackgroundThread();
    output.reset();
    outputName = {};
    if (name.isEmpty()) return true;
    if (name == virtualPortName) output = juce::MidiOutput::createNewDevice (virtualPortName);
    else
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name == name) { output = juce::MidiOutput::openDevice (d.identifier); break; }
    if (output == nullptr) return false;
    output->startBackgroundThread();
    outputName = name;
    return true;
}

bool MidiSyncController::setInputDevice (const juce::String& name)
{
    if (input != nullptr) input->stop();
    input.reset();
    inputName = {};
    if (name.isEmpty()) return true;
    if (name == virtualPortName) input = juce::MidiInput::createNewDevice (virtualPortName, this);
    else
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            if (d.name == name) { input = juce::MidiInput::openDevice (d.identifier, this); break; }
    if (input == nullptr) return false;
    inputName = name;
    if (mode == Mode::chaseClock || mode == Mode::chaseMtc) input->start();
    return true;
}

void MidiSyncController::send (const std::vector<TimedMidiMessage>& messages, double wallTime)
{
    if (output == nullptr || messages.empty()) return;
    juce::MidiBuffer buffer;
    for (const auto& m : messages)
        buffer.addEvent (m.message, (int) juce::jmax (0.0, (m.timeSeconds - wallTime) * 1000.0));
    output->sendBlockOfMessages (buffer, juce::Time::getMillisecondCounterHiRes(), 1000.0);
    sent += (long long) messages.size();
}

void MidiSyncController::timerCallback()
{
    const double wall = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (mode == Mode::sendClock || mode == Mode::sendMtc)
    {
        TransportSnapshot now;
        now.playing = transport.isPlaying();
        now.positionSeconds = transport.getPositionSeconds();
        now.bpm = transport.getBpm();
        send (mode == Mode::sendClock ? clock.update (now, wall) : mtc.update (now, wall), wall);
        if (mode == Mode::sendMtc) lastTimecode = Timecode::fromSeconds (now.positionSeconds + startOffset, frameRate).toString();
    }
    else if (mode == Mode::chaseMtc)
    {
        // The stream stopped: stop the transport
        const juce::ScopedLock sl (chaseLock);
        if (transport.isPlaying() && lastChaseTime >= 0.0 && ! mtcChaser.isRunning (wall)) { transport.stop(); lastChaseTime = -1.0; if (onTransportChanged) onTransportChanged(); }
    }
}

void MidiSyncController::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    received.fetch_add (1);
    const double time = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const juce::ScopedLock sl (chaseLock);
    if (mode == Mode::chaseClock)
    {
        const auto a = clockChaser.handle (message, time);
        if (a.bpm) { chasedBpm.store (*a.bpm); transport.setBpm (*a.bpm); }
        if (a.locateBeats) transport.setPositionSeconds (*a.locateBeats * 60.0 / transport.getBpm());
        if (a.play && ! transport.isPlaying()) transport.play();
        if (a.stop) transport.stop();
        // Keep the transport on the external beat: nudge when more than a 32nd out
        if (clockChaser.isRunning() && message.isMidiClock())
        {
            const double ours = transport.getPositionSeconds() * transport.getBpm() / 60.0;
            if (std::abs (ours - clockChaser.getBeatPosition()) > 0.125) transport.setPositionSeconds (clockChaser.getBeatPosition() * 60.0 / transport.getBpm());
        }
        if (a.play || a.stop || a.locateBeats) juce::MessageManager::callAsync ([this] { if (onTransportChanged) onTransportChanged(); });
    }
    else if (mode == Mode::chaseMtc)
    {
        const auto a = mtcChaser.handle (message, time);
        if (a.positionSeconds)
        {
            const double target = *a.positionSeconds - startOffset;
            lastTimecode = Timecode::fromSeconds (*a.positionSeconds, mtcChaser.getFrameRate()).toString();
            if (a.locate) { transport.setPositionSeconds (juce::jmax (0.0, target)); }
            else
            {
                if (std::abs (transport.getPositionSeconds() - target) > 0.05) transport.setPositionSeconds (juce::jmax (0.0, target));
                if (! transport.isPlaying()) { transport.play(); juce::MessageManager::callAsync ([this] { if (onTransportChanged) onTransportChanged(); }); }
            }
        }
        if (a.running) lastChaseTime = time;
    }
}

juce::String MidiSyncController::getStatus() const
{
    switch (mode)
    {
        case Mode::off:        return "Sync off";
        case Mode::sendClock:  return "Sending MIDI Beat Clock to " + (outputName.isEmpty() ? juce::String ("(no output)") : outputName) + "  " + juce::String (transport.getBpm(), 1) + " BPM  " + juce::String (sent) + " messages";
        case Mode::sendMtc:    return "Sending MTC to " + (outputName.isEmpty() ? juce::String ("(no output)") : outputName) + "  " + lastTimecode + "  " + juce::String (sent) + " messages";
        case Mode::chaseClock: return "Chasing MIDI Beat Clock from " + (inputName.isEmpty() ? juce::String ("(no input)") : inputName) + (chasedBpm.load() > 0.0 ? "  " + juce::String (chasedBpm.load(), 1) + " BPM" : juce::String ("  waiting")) + "  " + juce::String (received.load()) + " messages";
        case Mode::chaseMtc:   return "Chasing MTC from " + (inputName.isEmpty() ? juce::String ("(no input)") : inputName) + "  " + (lastTimecode.isEmpty() ? juce::String ("waiting") : lastTimecode) + "  " + juce::String (received.load()) + " messages";
    }
    return {};
}

} // namespace beatmaker::engine
