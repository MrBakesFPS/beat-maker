// AudioEngine: owns the audio device, the transport, and the render graph.
// This is the single object the rest of the app talks to for playback.
#pragma once

#include "../graph/AudioGraph.h"
#include "../transport/Transport.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

namespace beatmaker::engine
{

class AudioEngine final : private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Opens the default output device. Returns an empty string on success or
    // an error message from the device layer.
    juce::String initialise (int numOutputChannels = 2);

    Transport&  getTransport() noexcept { return transport; }
    AudioGraph& getGraph() noexcept     { return graph; }
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    double getSampleRate() const noexcept { return transport.getSampleRate(); }

    void setSnapshot (std::unique_ptr<RenderSnapshot> snapshot) { graph.setSnapshot (std::move (snapshot)); }

private:
    void timerCallback() override { graph.collectGarbage(); }

    Transport transport;
    AudioGraph graph { transport };
    juce::AudioDeviceManager deviceManager;
    bool callbackAdded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace beatmaker::engine
