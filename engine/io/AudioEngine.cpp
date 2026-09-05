#include "AudioEngine.h"

namespace beatmaker::engine
{

AudioEngine::AudioEngine()
{
    graph.setRecorder (&recorder);
    startTimer (200); // garbage collection of retired snapshots
}

AudioEngine::~AudioEngine()
{
    stopTimer();

    if (callbackAdded)
        deviceManager.removeAudioCallback (&graph);

    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise (int numInputChannels, int numOutputChannels)
{
    auto error = deviceManager.initialiseWithDefaultDevices (numInputChannels, numOutputChannels);

    if (error.isNotEmpty() && numInputChannels > 0)
        error = deviceManager.initialiseWithDefaultDevices (0, numOutputChannels);

    if (error.isNotEmpty())
        return error;

    if (auto* device = deviceManager.getCurrentAudioDevice())
        transport.setSampleRate (device->getCurrentSampleRate());

    deviceManager.addAudioCallback (&graph);
    callbackAdded = true;
    return {};
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    juce::StringArray names;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto all = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();
        for (int i = 0; i < all.size(); ++i)
            if (active[i]) names.add (all[i]);
    }
    return names;
}

} // namespace beatmaker::engine
