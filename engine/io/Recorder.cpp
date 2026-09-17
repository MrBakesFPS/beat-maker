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

juce::String Recorder::start (const std::vector<Slot>& slots, double sampleRate, int bitDepth, PunchMode mode)
{
    if (isRecording())
        return "Already recording";
    if (slots.empty())
        return "No tracks are record-armed";

    auto session = std::make_unique<Session>();
    session->mode = mode;
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

        auto ch = std::make_unique<Channel>();
        ch->trackId    = slot.trackId;
        ch->firstInput = slot.firstInput;
        ch->numInputs  = juce::jlimit (1, 2, slot.numInputs);
        ch->file       = slot.file;
        ch->writer     = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writeThread,
                                                                                     (int) (sampleRate * 8.0));
        if (slot.receiver != nullptr)
            ch->writer->setDataReceiver (slot.receiver);
        ch->ranges.reserve ((size_t) maxRangesPerTake);

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

    for (auto& chPtr : owned->channels)
    {
        auto& ch = *chPtr;
        ch.writer.reset(); // flushes and closes the file

        // Close a range that was still punched in.
        if (ch.punched && ch.ranges.size() < (size_t) maxRangesPerTake)
            ch.ranges.push_back ({ ch.rangeTimelineStart, ch.rangeFileStart, ch.written - ch.rangeFileStart });
        ch.punched = false;
        std::erase_if (ch.ranges, [] (const Range& r) { return r.length <= 0; });

        if (start < 0 || ch.written == 0 || ch.ranges.empty())
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
        t.ranges      = ch.ranges;
        takes.push_back (std::move (t));
    }

    owned.reset();
    return takes;
}

juce::int64 Recorder::getRecordStartSample() const noexcept
{
    return owned != nullptr ? owned->startSample.load (std::memory_order_acquire) : -1;
}

void Recorder::setAutoPunch (juce::int64 inSample, juce::int64 outSample) noexcept
{
    if (owned == nullptr) return;
    owned->autoIn.store (inSample, std::memory_order_release);
    owned->autoOut.store (outSample, std::memory_order_release);
}

void Recorder::setPunch (int trackId, bool punched) noexcept
{
    if (owned == nullptr) return;
    for (auto& ch : owned->channels)
        if (trackId < 0 || ch->trackId == trackId) ch->wantPunch.store (punched, std::memory_order_release);
}

bool Recorder::isPunched (int trackId) const noexcept
{
    if (owned == nullptr) return false;
    for (const auto& ch : owned->channels)
        if ((trackId < 0 || ch->trackId == trackId) && ch->punchedNow.load (std::memory_order_acquire)) return true;
    return false;
}

void Recorder::processInput (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    busy.store (true);
    auto* session = active.load();

    if (session != nullptr && transport.isRolling() && numSamples > 0)   // a count-in is not part of the take
    {
        const juce::int64 pos = transport.getPositionSamples();
        juce::int64 expected = -1;
        session->startSample.compare_exchange_strong (expected, pos);
        const juce::int64 autoIn = session->autoIn.load (std::memory_order_acquire);
        const juce::int64 autoOut = session->autoOut.load (std::memory_order_acquire);

        for (auto& chPtr : session->channels)
        {
            auto& ch = *chPtr;

            // Punch state across this block: whole mode follows the auto range
            // sample-accurately, manual mode follows the request at block granularity.
            auto desiredAt = [&] (juce::int64 t) -> bool
            {
                if (session->mode == PunchMode::manual) return ch.wantPunch.load (std::memory_order_acquire);
                return autoIn < 0 || (t >= autoIn && t < autoOut);
            };
            auto transition = [&] (int offset, bool nowPunched)
            {
                if (nowPunched) { ch.rangeFileStart = ch.written + offset; ch.rangeTimelineStart = pos + offset; }
                else if (ch.ranges.size() < (size_t) maxRangesPerTake)
                    ch.ranges.push_back ({ ch.rangeTimelineStart, ch.rangeFileStart, ch.written + offset - ch.rangeFileStart });
                ch.punched = nowPunched;
            };
            int checkpoints[3] = { 0, numSamples, numSamples };
            if (session->mode == PunchMode::whole && autoIn >= 0)
            {
                checkpoints[1] = (int) juce::jlimit<juce::int64> (0, numSamples, autoIn - pos);
                checkpoints[2] = (int) juce::jlimit<juce::int64> (0, numSamples, autoOut - pos);
            }
            for (int c = 0; c < 3; ++c)
            {
                const int offset = checkpoints[c];
                if (offset >= numSamples && c > 0) continue;
                const bool want = desiredAt (pos + offset);
                if (want != ch.punched) transition (offset, want);
            }
            ch.punchedNow.store (ch.punched, std::memory_order_release);

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
