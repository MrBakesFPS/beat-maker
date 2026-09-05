// AudioGraph: the real-time render callback. Per block it
//   1. swaps in any pending RenderSnapshot,
//   2. mixes audio clips that overlap the block,
//   3. schedules step-sequencer hits into the DrumMachine voice pool,
//   4. renders drum voices (also while stopped, for pad previews and tails),
//   5. advances the transport, wrapping at the loop end when cycling.
//
// Snapshot handoff is lock-free:
//   message thread  --incoming-->  audio thread  --retired-->  message thread
// The audio thread never allocates or frees; retired snapshots are freed by
// collectGarbage() on the message thread.
#pragma once

#include "RenderSnapshot.h"
#include "../dsp/DrumMachine.h"
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

    // Message thread. Takes ownership; the graph starts using it on the next block.
    void setSnapshot (std::unique_ptr<RenderSnapshot> snapshot);

    // Message thread, periodic. Frees snapshots the audio thread has finished with.
    void collectGarbage();

    // Message thread. Audition a pad. Ignored unless `kit` is part of the
    // active snapshot (that is what keeps it alive for the audio thread).
    void triggerPadPreview (const DrumKit* kit, int pad, float velocity);

    // Offline rendering (tests, bounce). Same code path as the live callback.
    void renderBlock (float* const* outputs, int numOutputs, int numSamples);

    float getOutputPeak (int channel) const noexcept;
    int getNumActiveVoices() const noexcept { return drums.getNumActiveVoices(); }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    static constexpr int maxOutputs = 32;

    void renderRange (float* const* outputs, int numOutputs, int numSamples);
    void mixClips (float* const* outputs, int numOutputs, juce::int64 rangeStart, int numSamples);
    void scheduleSequencer (juce::int64 rangeStart, int numSamples);
    void processPreviewEvents();
    bool snapshotHasKit (const DrumKit* kit) const noexcept;
    void swapInPendingSnapshot() noexcept;

    Transport& transport;
    DrumMachine drums;

    RenderSnapshot* current = nullptr;                 // owned by the audio thread
    std::atomic<RenderSnapshot*> incoming { nullptr }; // message -> audio

    static constexpr int retiredCapacity = 64;
    std::array<RenderSnapshot*, retiredCapacity> retired {};
    juce::AbstractFifo retiredFifo { retiredCapacity }; // audio -> message

    struct PreviewEvent { const DrumKit* kit = nullptr; int pad = 0; float velocity = 1.0f; };
    static constexpr int previewCapacity = 64;
    std::array<PreviewEvent, previewCapacity> previewEvents {};
    juce::AbstractFifo previewFifo { previewCapacity };  // message -> audio

    std::array<std::atomic<float>, 2> outputPeak { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioGraph)
};

} // namespace beatmaker::engine
