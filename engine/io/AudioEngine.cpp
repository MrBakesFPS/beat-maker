#include "AudioEngine.h"

namespace beatmaker::engine
{

AudioEngine::AudioEngine()
{
    startTimer (200); // garbage collection of retired snapshots
}

AudioEngine::~AudioEngine()
{
    stopTimer();

    if (callbackAdded)
        deviceManager.removeAudioCallback (&graph);

    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise (int numOutputChannels)
{
    const auto error = deviceManager.initialiseWithDefaultDevices (0, numOutputChannels);

    if (error.isNotEmpty())
        return error;

    if (auto* device = deviceManager.getCurrentAudioDevice())
        transport.setSampleRate (device->getCurrentSampleRate());

    deviceManager.addAudioCallback (&graph);
    callbackAdded = true;
    return {};
}

} // namespace beatmaker::engine
