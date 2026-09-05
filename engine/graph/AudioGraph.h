// AudioGraph: the real-time render callback. Mixes every RenderClip in the
// active snapshot that overlaps the current block into the device outputs
// and advances the transport.
//
// Snapshot handoff is lock-free:
//   message thread  --incoming-->  audio thread  --retired-->  message thread
// The audio thread never allocates or frees; retired snapshots are freed by
// collectGarbage() on the message thread.
#pragma once

#include "RenderSnapshot.h"
#include "../transport/Transport.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <memory>

namespace beatmaker::engine
{

class AudioGraph final : public juce::AudioIODeviceCallback
{
public:
    explicit AudioGraph (Transport& transportToUse);
    ~AudioGraph() override;

    // Message thread. Takes ownership; the graph will start using it on the
    // next audio block.
    void setSnapshot (std::unique_ptr<RenderSnapshot> snapshot);

    // Message thread, called periodically. Frees snapshots the audio thread
    // has finished with.
    void collectGarbage();

    // Offline rendering (tests, bounce). Same code path as the live callback.
    void renderBlock (float* const* outputs, int numOutputs, int numSamples);

    float getOutputPeak (int channel) const noexcept;

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void swapInPendingSnapshot() noexcept;

    Transport& transport;

    RenderSnapshot* current = nullptr;                 // owned by the audio thread
    std::atomic<RenderSnapshot*> incoming { nullptr }; // message -> audio

    static constexpr int retiredCapacity = 64;
    std::array<RenderSnapshot*, retiredCapacity> retired {};
    juce::AbstractFifo retiredFifo { retiredCapacity }; // audio -> message

    std::array<std::atomic<float>, 2> outputPeak { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioGraph)
};

} // namespace beatmaker::engine
