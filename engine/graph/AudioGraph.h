// AudioGraph: the real-time render callback. Per block it
//   1. swaps in any pending RenderSnapshot,
//   2. mixes audio clips that overlap the block,
//   3. schedules step-sequencer hits into the DrumMachine voice pool,
//   4. renders drum voices (also while stopped, for pad previews and tails),
//   5. advances the transport, wrapping at the loop end when cycling,
//   6. hands device input to the Recorder and mixes monitored inputs through.
//
// Snapshot handoff is lock-free:
//   message thread  --incoming-->  audio thread  --retired-->  message thread
// The audio thread never allocates or frees; retired snapshots are freed by
// collectGarbage() on the message thread.
#pragma once

#include "RenderSnapshot.h"
#include "../dsp/DrumMachine.h"
#include "../dsp/Synth.h"
#include "../io/Recorder.h"
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

    // Message thread. Audition a note on a synth instrument in the snapshot.
    void triggerNotePreview (int instrumentId, int pitch, float velocity, double seconds = 0.3);

    int getNumSynthVoices() const noexcept;

    // Optional recorder that receives every input block (message thread, before start).
    void setRecorder (Recorder* r) noexcept { recorder = r; }

    // Offline rendering (tests, bounce). Same code path as the live callback.
    void renderBlock (const float* const* inputs, int numInputs,
                      float* const* outputs, int numOutputs, int numSamples);
    void renderBlock (float* const* outputs, int numOutputs, int numSamples)
    {
        renderBlock (nullptr, 0, outputs, numOutputs, numSamples);
    }

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
    void mixMonitoredInputs (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples);
    void mixPreview (float* const* outputs, int numOutputs, int numSamples);
    void mixClips (float* const* outputs, int numOutputs, juce::int64 rangeStart, int numSamples);
    void scheduleSequencer (juce::int64 rangeStart, int numSamples);
    void scheduleMidi (juce::int64 rangeStart, int numSamples);
    void rebindSynthSlots();
    Synth* synthForId (int instrumentId) noexcept;
    void releaseAllSynths (bool immediate) noexcept;
    void processPreviewEvents();
    bool snapshotHasKit (const DrumKit* kit) const noexcept;
    void swapInPendingSnapshot() noexcept;

    Transport& transport;
    DrumMachine drums;
    Recorder* recorder = nullptr;

    static constexpr int maxSynths = 16;
    struct SynthSlot { int id = -1; Synth synth; };
    std::array<SynthSlot, maxSynths> synthSlots;
    bool wasPlaying = false;

    const juce::AudioBuffer<float>* previewSource = nullptr;  // identity of the current preview
    int previewPosition = 0;

    RenderSnapshot* current = nullptr;                 // owned by the audio thread
    std::atomic<RenderSnapshot*> incoming { nullptr }; // message -> audio

    static constexpr int retiredCapacity = 64;
    std::array<RenderSnapshot*, retiredCapacity> retired {};
    juce::AbstractFifo retiredFifo { retiredCapacity }; // audio -> message

    struct PreviewEvent
    {
        const DrumKit* kit = nullptr;   // pad preview when non-null
        int pad = 0;
        int instrumentId = -1;          // note preview when >= 0
        int pitch = 60;
        int gateSamples = 0;
        float velocity = 1.0f;
    };
    static constexpr int previewCapacity = 64;
    std::array<PreviewEvent, previewCapacity> previewEvents {};
    juce::AbstractFifo previewFifo { previewCapacity };  // message -> audio

    std::array<std::atomic<float>, 2> outputPeak { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioGraph)
};

} // namespace beatmaker::engine
