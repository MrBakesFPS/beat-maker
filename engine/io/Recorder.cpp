#include "Recorder.h"
#include <thread>

namespace beatmaker::engine
{

Recorder::Recorder (Transport& t) : transport (t)
{
    writeThread.startThread();
}

Recorder::~Recorder()
{
    stop();
    writeThread.stopThread (2000);
}

juce::String Recorder::start (const std::vector<Slot>& slots, double sampleRate, int bitDepth)
{
    if (isRecording())
        return "Already recording";
    if (slots.empty())
        return "No tracks are record-armed";

    auto session = std::make_unique<Session>();
    juce::WavAudioFormat wav;

    for (const auto& slot : slots)
    {
        if (! slot.file.getParentDirectory().createDirectory())
            return "Cannot create " + slot.file.getParentDirectory().getFullPathName();

        slot.file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (slot.file);
        if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
            return "Cannot write " + slot.file.getFullPathName();

        const auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                             .withNumChannels (juce::jlimit (1, 2, slot.numInputs))
                                                             .withBitsPerSample (bitDepth);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return "Cannot create WAV writer for " + slot.file.getFileName();

        Channel ch;
        ch.trackId    = slot.trackId;
        ch.firstInput = slot.firstInput;
        ch.numInputs  = juce::jlimit (1, 2, slot.numInputs);
        ch.file       = slot.file;
        ch.writer     = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writeThread,
                                                                                    (int) (sampleRate * 8.0));
        if (slot.receiver != nullptr)
            ch.writer->setDataReceiver (slot.receiver);

        session->channels.push_back (std::move (ch));
    }

    dropouts.store (0);
    owned = std::move (session);
    active.store (owned.get());
    return {};
}

std::vector<Recorder::Take> Recorder::stop()
{
    std::vector<Take> takes;
    if (! isRecording())
        return takes;

    // Detach from the audio thread, then wait until it is no longer inside processInput().
    active.store (nullptr);
    for (int spins = 0; busy.load() && spins < 20000; ++spins)
        std::this_thread::yield();

    const juce::int64 start = owned->startSample.load();

    for (auto& ch : owned->channels)
    {
        ch.writer.reset(); // flushes and closes the file

        if (start < 0 || ch.written == 0)
        {
            ch.file.deleteFile();
            continue;
        }

        Take t;
        t.trackId     = ch.trackId;
        t.file        = ch.file;
        t.startSample = start;
        t.numSamples  = ch.written;
        t.numChannels = ch.numInputs;
        takes.push_back (std::move (t));
    }

    owned.reset();
    return takes;
}

juce::int64 Recorder::getRecordStartSample() const noexcept
{
    return owned != nullptr ? owned->startSample.load (std::memory_order_acquire) : -1;
}

void Recorder::processInput (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    busy.store (true);
    auto* session = active.load();

    if (session != nullptr && transport.isPlaying() && numSamples > 0)
    {
        juce::int64 expected = -1;
        session->startSample.compare_exchange_strong (expected, transport.getPositionSamples());

        for (auto& ch : session->channels)
        {
            for (int done = 0; done < numSamples; )
            {
                const int n = juce::jmin (numSamples - done, Session::silenceLength);
                const float* ptrs[2] = { session->silence.data(), session->silence.data() };

                for (int i = 0; i < ch.numInputs; ++i)
                {
                    const int idx = ch.firstInput + i;
                    if (idx < numInputs && inputs != nullptr && inputs[idx] != nullptr)
                        ptrs[i] = inputs[idx] + done;
                }

                if (! ch.writer->write (ptrs, n))
                    dropouts.fetch_add (1, std::memory_order_relaxed);

                done += n;
            }
            ch.written += numSamples;
        }
    }

    busy.store (false);
}

} // namespace beatmaker::engine
