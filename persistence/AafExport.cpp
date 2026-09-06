#include "AafExport.h"
#include "aaf/AafWriter.h"
#include <dsp/Fades.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace beatmaker::persistence
{

using namespace aaf;

namespace
{
// Property ids
constexpr uint16_t pidRootMetaDictionary = 0x0001, pidRootHeader = 0x0002;
constexpr uint16_t pidHeaderByteOrder = 0x3b01, pidHeaderLastModified = 0x3b02, pidHeaderContent = 0x3b03, pidHeaderDictionary = 0x3b04,
    pidHeaderVersion = 0x3b05, pidHeaderIdentificationList = 0x3b06, pidHeaderObjectModelVersion = 0x3b07, pidHeaderOperationalPattern = 0x3b09;
constexpr uint16_t pidIdCompanyName = 0x3c01, pidIdProductName = 0x3c02, pidIdProductVersion = 0x3c03, pidIdProductVersionString = 0x3c04,
    pidIdProductID = 0x3c05, pidIdDate = 0x3c06, pidIdToolkitVersion = 0x3c07, pidIdPlatform = 0x3c08, pidIdGenerationAUID = 0x3c09;
constexpr uint16_t pidContentMobs = 0x1901, pidContentEssenceData = 0x1902;
constexpr uint16_t pidMobMobID = 0x4401, pidMobName = 0x4402, pidMobSlots = 0x4403, pidMobLastModified = 0x4404, pidMobCreationTime = 0x4405, pidMobUsageCode = 0x4408;
constexpr uint16_t pidSourceMobEssenceDescription = 0x4701;
constexpr uint16_t pidSlotSlotID = 0x4801, pidSlotSlotName = 0x4802, pidSlotSegment = 0x4803, pidSlotPhysicalTrackNumber = 0x4804;
constexpr uint16_t pidTimelineEditRate = 0x4b01, pidTimelineOrigin = 0x4b02, pidEventSlotEditRate = 0x4901;
constexpr uint16_t pidComponentDataDefinition = 0x0201, pidComponentLength = 0x0202, pidSequenceComponents = 0x1001;
constexpr uint16_t pidSourceRefSourceID = 0x1101, pidSourceRefSourceMobSlotID = 0x1102, pidSourceClipStartTime = 0x1201;
constexpr uint16_t pidTimecodeStart = 0x1501, pidTimecodeFPS = 0x1502, pidTimecodeDrop = 0x1503;
constexpr uint16_t pidEventPosition = 0x0601, pidEventComment = 0x0602;
constexpr uint16_t pidFileDescSampleRate = 0x3001, pidFileDescLength = 0x3002, pidFileDescContainerFormat = 0x3004;
constexpr uint16_t pidEssenceDescLocator = 0x2f01, pidNetworkLocatorURL = 0x4001;
constexpr uint16_t pidSoundQuantizationBits = 0x3d01, pidSoundAudioSamplingRate = 0x3d03, pidSoundChannels = 0x3d07, pidPcmAverageBPS = 0x3d09, pidPcmBlockAlign = 0x3d0a;
constexpr uint16_t pidEssenceDataMobID = 0x2701, pidEssenceDataData = 0x2702;

const Auid productId = auidFromHex ("7b2e6d3f9a1c4e0aa1b2c3d4e5f60718");   // Beat Maker's product identifier

struct Essence
{
    juce::String name;
    MobIdBytes sourceMobId, masterMobId;
    std::vector<float> samples;   // mono
    double sampleRate = 48000.0;
    juce::File file;              // linked mode
};

std::vector<unsigned char> keyOf (const MobIdBytes& m) { return { m.begin(), m.end() }; }

juce::MemoryBlock pcmBytes (const std::vector<float>& samples, int bitDepth)
{
    juce::MemoryBlock out (samples.size() * (size_t) (bitDepth / 8));
    auto* p = (unsigned char*) out.getData();
    for (float s : samples)
    {
        const float c = juce::jlimit (-1.0f, 1.0f, s);
        if (bitDepth == 16) { const auto v = (int16_t) std::lround (c * 32767.0f); *p++ = (unsigned char) (v & 0xff); *p++ = (unsigned char) ((v >> 8) & 0xff); }
        else { const auto v = (int32_t) std::lround (c * 8388607.0f); *p++ = (unsigned char) (v & 0xff); *p++ = (unsigned char) ((v >> 8) & 0xff); *p++ = (unsigned char) ((v >> 16) & 0xff); }
    }
    return out;
}

bool writeWav (const juce::File& file, const std::vector<float>& samples, double rate, int bitDepth, juce::String& error)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! dynamic_cast<juce::FileOutputStream&> (*stream).openedOk()) { error = "Could not create " + file.getFullPathName(); return false; }
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (bitDepth)));
    if (writer == nullptr) { error = "Could not write " + file.getFullPathName(); return false; }
    juce::AudioBuffer<float> b (1, (int) samples.size());
    if (! samples.empty()) b.copyFrom (0, 0, samples.data(), (int) samples.size());
    if (! writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) { error = "Could not write " + file.getFullPathName(); return false; }
    return true;
}

Object makeSourceClip (const MobIdBytes& source, uint32_t slot, int64_t start, int64_t length)
{
    Object clip (classes::sourceClip);
    clip.addWeakRef (pidComponentDataDefinition, refDataDefinitions, ids::dataDefSound);
    clip.addI64 (pidComponentLength, length);
    clip.addI64 (pidSourceClipStartTime, start);
    clip.addMobId (pidSourceRefSourceID, source);
    clip.addU32 (pidSourceRefSourceMobSlotID, slot);
    return clip;
}

Object makeTimelineSlot (uint32_t id, const juce::String& name, int32_t rateNum, int32_t rateDen, Object segment, uint32_t physicalTrack = 0)
{
    Object slot (classes::timelineMobSlot);
    slot.addU32 (pidSlotSlotID, id);
    slot.addString (pidSlotSlotName, name);
    if (physicalTrack > 0) slot.addU32 (pidSlotPhysicalTrackNumber, physicalTrack);
    slot.addRational (pidTimelineEditRate, rateNum, rateDen);
    slot.addI64 (pidTimelineOrigin, 0);
    slot.addStrongRef (pidSlotSegment, "Segment", std::move (segment));
    return slot;
}

void addMobCommon (Object& mob, const MobIdBytes& id, const juce::String& name, juce::Time now)
{
    mob.addMobId (pidMobMobID, id);
    mob.addString (pidMobName, name);
    mob.addTimeStamp (pidMobCreationTime, now);
    mob.addTimeStamp (pidMobLastModified, now);
}
} // namespace

std::vector<int> AafExport::exportableTracks (const model::Session& session)
{
    std::vector<int> out;
    const auto& tracks = session.getTracks();
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const auto& t = tracks[(size_t) i];
        if (t.type == model::Track::Type::audio && ! t.clips.empty()) out.push_back (i);
        else if (t.type == model::Track::Type::instrument && t.isFrozen()) out.push_back (i);
    }
    return out;
}

AafExportSummary AafExport::write (const model::Session& session, double sampleRate, const juce::String& sessionName,
                                   const juce::File& aafFile, const AafExportOptions& options)
{
    AafExportSummary summary;
    const auto tracks = options.tracks.empty() ? exportableTracks (session) : options.tracks;
    if (tracks.empty())
    {
        juce::String detail;
        for (const auto& t : session.getTracks()) detail << " [" << t.name << ": type " << (int) t.type << ", " << (int) t.clips.size() << " clips]";
        summary.error = "No audio to export: AAF carries audio tracks (and frozen instrument tracks)" + detail; return summary;
    }
    if (sampleRate <= 0.0) { summary.error = "Invalid sample rate"; return summary; }
    const int bitDepth = options.bitDepth == 16 ? 16 : 24;
    const auto now = juce::Time::getCurrentTime();
    const int32_t rate = (int32_t) std::lround (sampleRate);
    const juce::File mediaFolder = aafFile.getSiblingFile (aafFile.getFileNameWithoutExtension() + " Media");
    if (! options.embedAudio && ! mediaFolder.createDirectory()) { summary.error = "Could not create " + mediaFolder.getFullPathName(); return summary; }

    // Clips per slot, essences shared where the audio is shared.
    struct SlotClip { int64_t start, length, sourceStart; size_t essence; };
    struct Slot { juce::String name; std::vector<SlotClip> clips; };
    std::vector<Slot> slots;
    std::vector<Essence> essences;
    std::map<juce::String, size_t> essenceIndex;
    int64_t sessionEnd = 0;

    auto addEssence = [&] (const juce::String& key, const juce::String& name, double clipRate, std::function<void (std::vector<float>&)> fill) -> size_t
    {
        if (auto it = essenceIndex.find (key); it != essenceIndex.end()) return it->second;
        Essence e; e.name = name; e.sampleRate = clipRate; e.sourceMobId = makeMobId(); e.masterMobId = makeMobId();
        fill (e.samples);
        essences.push_back (std::move (e));
        essenceIndex[key] = essences.size() - 1;
        return essences.size() - 1;
    };

    for (int trackIndex : tracks)
    {
        if (! juce::isPositiveAndBelow (trackIndex, session.getNumTracks())) continue;
        const auto& track = session.getTracks()[(size_t) trackIndex];
        std::vector<model::AudioClip> clips;
        if (track.type == model::Track::Type::audio) clips = track.clips;
        else if (track.isFrozen())
        {
            model::AudioClip c; c.name = track.name; c.audio = track.freeze.audio; c.sampleRate = track.freeze.sampleRate;
            c.timelineStart = 0; c.sourceOffset = 0; c.length = track.freeze.audio->getNumSamples();
            clips.push_back (std::move (c));
        }
        else continue;
        std::sort (clips.begin(), clips.end(), [] (const auto& a, const auto& b) { return a.timelineStart < b.timelineStart; });
        int channels = 1;
        for (const auto& c : clips) if (c.audio != nullptr) channels = juce::jmax (channels, c.audio->getNumChannels());
        channels = juce::jmin (channels, 2);
        ++summary.tracks;
        for (int ch = 0; ch < channels; ++ch)
        {
            Slot slot;
            slot.name = channels == 1 ? track.name : track.name + (ch == 0 ? ".L" : ".R");
            for (size_t ci = 0; ci < clips.size(); ++ci)
            {
                const auto& clip = clips[ci];
                if (clip.audio == nullptr || clip.length <= 0) continue;
                const int srcCh = juce::jmin (ch, clip.audio->getNumChannels() - 1);
                const double clipRate = clip.sampleRate > 0.0 ? clip.sampleRate : sampleRate;
                const double scale = sampleRate / clipRate;   // clip samples -> session samples
                int64_t start = (int64_t) std::llround ((double) clip.timelineStart * scale);
                int64_t length = (int64_t) std::llround ((double) clip.length * scale);
                if (ci + 1 < clips.size())   // a later clip on top truncates this one
                {
                    const auto nextStart = (int64_t) std::llround ((double) clips[ci + 1].timelineStart * scale);
                    if (nextStart < start + length) length = juce::jmax<int64_t> (0, nextStart - start);
                }
                if (length <= 0) continue;
                const auto baseName = clip.name.isNotEmpty() ? clip.name : track.name;
                size_t essence; int64_t sourceStart;
                if (options.consolidateClips)
                {
                    const auto key = juce::String::toHexString ((juce::pointer_sized_int) clip.audio.get()) + ":" + juce::String (srcCh) + ":" + juce::String (clip.sourceOffset) + ":" + juce::String (clip.length)
                                     + ":" + juce::String (clip.gain) + ":" + juce::String (clip.fadeIn) + ":" + juce::String (clip.fadeOut) + ":" + juce::String ((int) clip.fadeInShape) + ":" + juce::String ((int) clip.fadeOutShape)
                                     + ":" + juce::String::toHexString ((juce::pointer_sized_int) clip.gainLane.get());
                    essence = addEssence (key, baseName, clipRate, [&] (std::vector<float>& out)
                    {
                        out.resize ((size_t) clip.length, 0.0f);
                        const auto* src = clip.audio->getReadPointer (srcCh);
                        const auto total = (int64_t) clip.audio->getNumSamples();
                        const bool lane = clip.gainLane != nullptr && ! clip.gainLane->isEmpty();
                        for (int64_t i = 0; i < clip.length; ++i)
                        {
                            const auto s = clip.sourceOffset + i;
                            float v = s >= 0 && s < total ? src[s] : 0.0f;
                            v *= clip.gain * engine::clipEnvelopeAt (i, clip.length, clip.fadeIn, clip.fadeInShape, clip.fadeOut, clip.fadeOutShape);
                            if (lane) v *= clip.gainLane->valueAt (s, 1.0f);
                            out[(size_t) i] = v;
                        }
                    });
                    sourceStart = 0;
                }
                else
                {
                    const auto key = juce::String::toHexString ((juce::pointer_sized_int) clip.audio.get()) + ":" + juce::String (srcCh);
                    essence = addEssence (key, clip.sourceFile != juce::File() ? clip.sourceFile.getFileNameWithoutExtension() : baseName, clipRate, [&] (std::vector<float>& out)
                    {
                        const auto* src = clip.audio->getReadPointer (srcCh);
                        out.assign (src, src + clip.audio->getNumSamples());
                    });
                    sourceStart = clip.sourceOffset;
                }
                slot.clips.push_back ({ start, length, sourceStart, essence });
                sessionEnd = juce::jmax (sessionEnd, start + length);
                ++summary.clips;
            }
            slots.push_back (std::move (slot));
        }
    }
    summary.slots = (int) slots.size();
    summary.essences = (int) essences.size();

    // Linked mode: WAV files beside the AAF.
    if (! options.embedAudio)
    {
        std::map<juce::String, int> used;
        for (auto& e : essences)
        {
            auto base = juce::File::createLegalFileName (e.name);
            if (base.isEmpty()) base = "Audio";
            const int n = ++used[base];
            e.file = mediaFolder.getChildFile (n > 1 ? base + "-" + juce::String (n) + ".wav" : base + ".wav");
            if (! writeWav (e.file, e.samples, e.sampleRate, bitDepth, summary.error)) return summary;
            summary.mediaFiles.push_back (e.file);
        }
    }

    // Objects
    cfb::Node root; root.name = "Root Entry"; root.clsid = classes::root;
    {
        Object rootObject (classes::root);   // only its properties stream is used; the storages are added below
        rootObject.addStrongRef (pidRootMetaDictionary, "MetaDictionary", Object (classes::metaDictionary));
        rootObject.addStrongRef (pidRootHeader, "Header", Object (classes::header));
        cfb::Node scratch; rootObject.emit (scratch, "x");
        root.addStream ("properties", scratch.children.front()->find ("properties")->data);
    }
    auto& headerNode = root.addStorage ("Header-2", classes::header);
    if (! addStandardDictionaries (root, headerNode, summary.error)) return summary;

    // Header properties (the Dictionary storage came from the generated data)
    {
        std::vector<Object> idents;
        Object ident (classes::identification);
        ident.addString (pidIdCompanyName, "Beat Maker");
        ident.addString (pidIdProductName, "Beat Maker");
        ident.addVersion (pidIdProductVersion, 1, 0, 0, 0, 4);   // 4 = beta
        ident.addString (pidIdProductVersionString, "1.0 beta");
        ident.addAuid (pidIdProductID, productId);
        ident.addTimeStamp (pidIdDate, now);
        ident.addVersion (pidIdToolkitVersion, 1, 0, 0, 0, 4);
        ident.addString (pidIdPlatform, juce::SystemStats::getOperatingSystemName());
        juce::Uuid gen; Auid genId {}; std::copy (gen.getRawData(), gen.getRawData() + 16, genId.begin());
        ident.addAuid (pidIdGenerationAUID, genId);
        idents.push_back (std::move (ident));

        cfb::Node scratch;
        // The Dictionary strong reference names the generated storage; the object itself is skipped when emitting.
        Object h (classes::header);
        h.addU16 (pidHeaderByteOrder, 0x4949);
        h.addTimeStamp (pidHeaderLastModified, now);
        { const unsigned char v[2] = { 1, 2 }; h.addData (pidHeaderVersion, v, 2); }
        h.addU32 (pidHeaderObjectModelVersion, 1);
        h.addAuid (pidHeaderOperationalPattern, ids::opEditProtocol);
        h.addStrongRef (pidHeaderDictionary, "Dictionary", Object (classes::dictionary));
        h.addStrongVector (pidHeaderIdentificationList, "IdentificationList", std::move (idents));

        // Content storage
        std::vector<Object> mobs; std::vector<std::vector<unsigned char>> mobKeys;
        std::vector<Object> essenceData; std::vector<std::vector<unsigned char>> essenceKeys;

        // Composition
        {
            Object comp (classes::compositionMob);
            const auto compId = makeMobId();
            addMobCommon (comp, compId, sessionName.isNotEmpty() ? sessionName : juce::String ("Beat Maker Session"), now);
            comp.addAuid (pidMobUsageCode, ids::usageTopLevel);
            std::vector<Object> compSlots;
            uint32_t slotId = 1;
            // Timecode slot
            {
                const bool drop = std::abs (options.timecodeFps - 29.97) < 0.01;
                const int32_t fpsNum = drop ? 30000 : (int32_t) std::lround (options.timecodeFps), fpsDen = drop ? 1001 : 1;
                const int fps = drop ? 30 : (int) std::lround (options.timecodeFps);
                Object tc (classes::timecode);
                tc.addWeakRef (pidComponentDataDefinition, refDataDefinitions, ids::dataDefTimecode);
                tc.addI64 (pidComponentLength, juce::jmax<int64_t> (1, (int64_t) std::ceil ((double) sessionEnd / sampleRate * options.timecodeFps)));
                tc.addI64 (pidTimecodeStart, 0);
                tc.addU16 (pidTimecodeFPS, (uint16_t) fps);
                tc.addBool (pidTimecodeDrop, drop);
                compSlots.push_back (makeTimelineSlot (slotId++, "Timecode", fpsNum, fpsDen, std::move (tc)));
            }
            uint32_t physical = 1;
            for (const auto& slot : slots)
            {
                Object seq (classes::sequence);
                seq.addWeakRef (pidComponentDataDefinition, refDataDefinitions, ids::dataDefSound);
                std::vector<Object> comps;
                int64_t pos = 0;
                for (const auto& c : slot.clips)
                {
                    if (c.start > pos)
                    {
                        Object fill (classes::filler);
                        fill.addWeakRef (pidComponentDataDefinition, refDataDefinitions, ids::dataDefSound);
                        fill.addI64 (pidComponentLength, c.start - pos);
                        comps.push_back (std::move (fill));
                    }
                    comps.push_back (makeSourceClip (essences[c.essence].masterMobId, 1, c.sourceStart, c.length));
                    pos = c.start + c.length;
                }
                seq.addI64 (pidComponentLength, pos);
                seq.addStrongVector (pidSequenceComponents, "Components", std::move (comps));
                compSlots.push_back (makeTimelineSlot (slotId++, slot.name, rate, 1, std::move (seq), physical++));
            }
            // Markers
            if (options.includeMarkers && ! session.getMarkers().empty())
            {
                Object seq (classes::sequence);
                seq.addWeakRef (pidComponentDataDefinition, refDataDefinitions, ids::dataDefDescriptiveMetadata);
                std::vector<Object> events;
                auto markers = session.getMarkers();
                std::sort (markers.begin(), markers.end(), [] (const auto& a, const auto& b) { return a.seconds < b.seconds; });
                int64_t seqLength = 0;
                for (const auto& m : markers)
                {
                    Object ev (classes::descriptiveMarker);
                    ev.addWeakRef (pidComponentDataDefinition, refDataDefinitions, ids::dataDefDescriptiveMetadata);
                    const auto position = (int64_t) std::llround (m.seconds * sampleRate);
                    const auto length = m.endSeconds > m.seconds ? (int64_t) std::llround ((m.endSeconds - m.seconds) * sampleRate) : (int64_t) 1;
                    ev.addI64 (pidComponentLength, length);
                    ev.addI64 (pidEventPosition, position);
                    ev.addString (pidEventComment, m.name);
                    events.push_back (std::move (ev));
                    seqLength = juce::jmax (seqLength, position + length);
                    ++summary.markers;
                }
                seq.addI64 (pidComponentLength, seqLength);
                seq.addStrongVector (pidSequenceComponents, "Components", std::move (events));
                Object slot (classes::eventMobSlot);
                slot.addU32 (pidSlotSlotID, slotId++);
                slot.addString (pidSlotSlotName, "Markers");
                slot.addRational (pidEventSlotEditRate, rate, 1);
                slot.addStrongRef (pidSlotSegment, "Segment", std::move (seq));
                compSlots.push_back (std::move (slot));
            }
            comp.addStrongVector (pidMobSlots, "Slots", std::move (compSlots));
            mobs.push_back (std::move (comp)); mobKeys.push_back (keyOf (compId));
        }

        // Master and source mobs per essence
        for (const auto& e : essences)
        {
            const auto length = (int64_t) e.samples.size();
            const int32_t eRate = (int32_t) std::lround (e.sampleRate);

            Object master (classes::masterMob);
            addMobCommon (master, e.masterMobId, e.name, now);
            std::vector<Object> mslots;
            mslots.push_back (makeTimelineSlot (1, e.name, eRate, 1, makeSourceClip (e.sourceMobId, 1, 0, length), 1));
            master.addStrongVector (pidMobSlots, "Slots", std::move (mslots));
            mobs.push_back (std::move (master)); mobKeys.push_back (keyOf (e.masterMobId));

            Object source (classes::sourceMob);
            addMobCommon (source, e.sourceMobId, options.embedAudio ? e.name : e.file.getFileName(), now);
            std::vector<Object> sslots;
            sslots.push_back (makeTimelineSlot (1, e.name, eRate, 1, makeSourceClip (nullMobId(), 0, 0, length), 1));
            source.addStrongVector (pidMobSlots, "Slots", std::move (sslots));
            Object desc (classes::pcmDescriptor);
            desc.addRational (pidFileDescSampleRate, eRate, 1);
            desc.addI64 (pidFileDescLength, length);
            desc.addWeakRef (pidFileDescContainerFormat, refContainerDefinitions, options.embedAudio ? ids::containerAaf : ids::containerExternal);
            desc.addRational (pidSoundAudioSamplingRate, eRate, 1);
            desc.addU32 (pidSoundChannels, 1);
            desc.addU32 (pidSoundQuantizationBits, (uint32_t) bitDepth);
            desc.addU16 (pidPcmBlockAlign, (uint16_t) (bitDepth / 8));
            desc.addU32 (pidPcmAverageBPS, (uint32_t) (eRate * bitDepth / 8));
            if (! options.embedAudio)
            {
                Object loc (classes::networkLocator);
                loc.addString (pidNetworkLocatorURL, juce::URL (e.file).toString (false));
                std::vector<Object> locs; locs.push_back (std::move (loc));
                desc.addStrongVector (pidEssenceDescLocator, "Locator", std::move (locs));
            }
            source.addStrongRef (pidSourceMobEssenceDescription, "EssenceDescription", std::move (desc));
            mobs.push_back (std::move (source)); mobKeys.push_back (keyOf (e.sourceMobId));

            if (options.embedAudio)
            {
                Object data (classes::essenceData);
                data.addMobId (pidEssenceDataMobID, e.sourceMobId);
                data.addStream (pidEssenceDataData, "Data", pcmBytes (e.samples, bitDepth));
                essenceData.push_back (std::move (data)); essenceKeys.push_back (keyOf (e.sourceMobId));
            }
        }

        Object content (classes::contentStorage);
        content.addStrongSet (pidContentMobs, "Mobs", std::move (mobs), pidMobMobID, mobKeys);
        content.addStrongSet (pidContentEssenceData, "EssenceData", std::move (essenceData), pidEssenceDataMobID, essenceKeys);
        h.addStrongRef (pidHeaderContent, "Content", std::move (content));

        // Emit the header into a scratch node, then move its pieces into the real
        // header storage (which already holds the generated Dictionary).
        h.emit (scratch, "Header-2");
        auto& built = *scratch.children.front();
        for (auto& child : built.children)
        {
            if (child->name == "Dictionary-3b04") continue;   // the generated one is already there
            headerNode.children.push_back (std::move (child));
        }
    }

    // Write
    aafFile.deleteFile();
    juce::FileOutputStream out (aafFile);
    if (! out.openedOk()) { summary.error = "Could not create " + aafFile.getFullPathName(); return summary; }
    if (! cfb::write (root, ids::fileKind4096, out, summary.error)) return summary;
    out.flush();
    summary.fileBytes = aafFile.getSize();
    return summary;
}

} // namespace beatmaker::persistence
