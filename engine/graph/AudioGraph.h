// AudioGraph: the real-time render callback. Per block it
//   1. swaps in any pending RenderSnapshot,
//   2. schedules step-sequencer hits and MIDI notes,
//   3. renders every channel strip: its clips, drum voices, synth and
//      monitored inputs -> inserts -> pre-fader sends -> fader/pan -> post
//      sends -> main mix or a bus; then aux strips read their bus; then the
//      master strip (inserts, fader) feeds the device outputs,
//   4. advances the transport, wrapping at the loop end when cycling,
//   5. hands device input to the Recorder.
// Instruments render while stopped too (previews, tails).
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
#include "../metering/Loudness.h"
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

    // Scrubbing (message thread): the audio of `strip` (-1 = every strip) is
    // dragged toward `targetSample` at a limited rate while the transport is
    // stopped; the transport position follows so the playhead moves.
    void startScrub (int strip, juce::int64 sample);
    void setScrubTarget (juce::int64 sample);
    void stopScrub();
    bool isScrubbing() const noexcept { return scrubbing.load (std::memory_order_relaxed); }

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

    static constexpr int maxBlock = 8192;   // largest block rendered in one pass (plugins are prepared for this)

    // Meters (message thread readout): post-fader peak per strip and master.
    static constexpr int maxStrips = 64;
    static constexpr int numBuses = 8;
    float getStripPeak (int strip, int channel) const noexcept;
    float getMasterPeak (int channel) const noexcept;
    float getStripRms (int strip, int channel) const noexcept;    // 300 ms integrated
    float getMasterRms (int channel) const noexcept;
    bool  getAndClearStripClip (int strip) noexcept;              // true if any sample exceeded 0 dBFS since last call
    bool  getAndClearMasterClip() noexcept;
    LoudnessSource& getLoudnessSource() noexcept { return loudness; }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    static constexpr int maxOutputs = 32;

    void renderRange (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples);
    void renderStripSources (int stripIndex, const float* const* inputs, int numInputs, juce::int64 pos, bool playing, int numSamples);
    void processStrip (const RenderStrip&, int stripIndex, juce::int64 blockStart, int numSamples,
                       float* const* outputs, int numOutputs);
    void processInserts (const std::vector<RenderInsert>&, juce::AudioBuffer<float>&, int numSamples,
                         const RenderStrip* automationOwner = nullptr, juce::int64 blockStart = 0);
    static const AutomationLane* laneFor (const RenderStrip&, const ParamId&) noexcept;
    void mixPreview (float* const* outputs, int numOutputs, int numSamples);
    void mixClips (int stripIndex, juce::int64 rangeStart, int numSamples);
    void mixClipsScrub (int stripIndex, int numSamples);
    void mixMonitoredInputs (int stripIndex, const float* const* inputs, int numInputs, int numSamples);
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
    struct SynthSlot { int id = -1; int strip = 0; Synth synth; };
    std::array<SynthSlot, maxSynths> synthSlots;
    bool wasPlaying = false;

    // Mixer buffers (allocated once; blocks are chunked to maxBlock)
    juce::AudioBuffer<float> stripBuffer { 2, maxBlock };
    juce::AudioBuffer<float> mainBuffer { 2, maxBlock };
    std::array<juce::AudioBuffer<float>, numBuses> busBuffers;
    struct Meter
    {
        std::array<std::atomic<float>, 2> peak { 0.0f, 0.0f };
        std::array<std::atomic<float>, 2> meanSquare { 0.0f, 0.0f };   // 300 ms exponential average
        std::atomic<bool> clipped { false };
        void update (const juce::AudioBuffer<float>& b, int numSamples, double sampleRate) noexcept;
        void clear() noexcept;
    };
    std::array<Meter, maxStrips> stripMeters;
    Meter masterMeter;
    const RenderStrip defaultStrip {};

    // Delay compensation lines, one stereo ring buffer per strip.
    static constexpr int maxDelaySamples = 16384;
    struct StripDelay
    {
        juce::AudioBuffer<float> ring { 2, maxDelaySamples };
        int writePos = 0;
        void process (juce::AudioBuffer<float>& io, int numSamples, int delay) noexcept;
    };
    std::array<StripDelay, maxStrips> stripDelays;
    LoudnessSource loudness;

    std::atomic<bool> scrubbing { false };
    std::atomic<int> scrubStrip { -1 };
    std::atomic<juce::int64> scrubTarget { 0 };
    double scrubPosition = 0.0;      // audio thread

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
