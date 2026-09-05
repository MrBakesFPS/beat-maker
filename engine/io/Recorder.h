// Recorder: captures device input to disk while the transport plays.
//
// The audio thread copies input channels into per-track ThreadedWriters
// (lock-free ring buffers); a background TimeSliceThread flushes them to WAV.
// start()/stop() run on the message thread. Handoff to the audio thread uses
// an atomic session pointer plus a `busy` flag (Dekker-style) so stop() can
// safely tear the session down once the callback has let go of it.
#pragma once

#include "../transport/Transport.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

class Recorder
{
public:
    struct Slot
    {
        int trackId = 0;
        int firstInput = 0;      // device input channel index
        int numInputs = 1;       // 1 = mono, 2 = stereo pair
        juce::File file;         // destination WAV
        juce::AudioFormatWriter::ThreadedWriter::IncomingDataReceiver* receiver = nullptr; // e.g. an AudioThumbnail
    };

    struct Take
    {
        int trackId = 0;
        juce::File file;
        juce::int64 startSample = 0;   // timeline position where recording began
        juce::int64 numSamples = 0;
        int numChannels = 0;
    };

    explicit Recorder (Transport& transport);
    ~Recorder();

    // Message thread. Returns an empty string on success.
    juce::String start (const std::vector<Slot>& slots, double sampleRate, int bitDepth = 24);

    // Message thread. Finalises files and returns the takes (empty files are deleted).
    std::vector<Take> stop();

    bool isRecording() const noexcept { return owned != nullptr; }

    // -1 until the first block has been captured.
    juce::int64 getRecordStartSample() const noexcept;

    // Number of blocks the disk writer could not keep up with (message-thread readout).
    int getDropoutCount() const noexcept { return dropouts.load (std::memory_order_relaxed); }

    // Audio thread. Call once per block before rendering.
    void processInput (const float* const* inputs, int numInputs, int numSamples) noexcept;

private:
    struct Channel
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
        int trackId = 0, firstInput = 0, numInputs = 1;
        juce::File file;
        juce::int64 written = 0;
    };

    struct Session
    {
        std::vector<Channel> channels;
        std::atomic<juce::int64> startSample { -1 };
        static constexpr int silenceLength = 16384;
        std::vector<float> silence = std::vector<float> ((size_t) silenceLength, 0.0f);
    };

    Transport& transport;
    juce::TimeSliceThread writeThread { "Beat Maker Recording" };

    std::unique_ptr<Session> owned;            // message thread
    std::atomic<Session*> active { nullptr };  // message -> audio
    std::atomic<bool> busy { false };          // audio thread holds `active`
    std::atomic<int> dropouts { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};

} // namespace beatmaker::engine
