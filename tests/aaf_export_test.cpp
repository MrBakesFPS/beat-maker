#include <catch2/catch_test_macros.hpp>
#include <AafExport.h>
#include <aaf/CfbWriter.h>
#include <aaf/AafWriter.h>
#include <Session.h>
#include <map>

using namespace beatmaker;

namespace
{
// A small Compound File reader (version 3 or 4) for checking what the writer produced.
struct CfbReader
{
    juce::MemoryBlock file;
    uint32_t sectorSize = 0, miniSize = 64, miniCutoff = 4096;
    std::vector<uint32_t> fat, miniFat;
    struct Entry { juce::String name; int type = 0; uint32_t left, right, child, start; uint64_t size; std::array<unsigned char, 16> clsid {}; };
    std::vector<Entry> entries;
    juce::MemoryBlock miniStream;

    uint16_t u16 (size_t o) const { return (uint16_t) (((const unsigned char*) file.getData())[o] | (((const unsigned char*) file.getData())[o + 1] << 8)); }
    uint32_t u32 (size_t o) const { return (uint32_t) u16 (o) | ((uint32_t) u16 (o + 2) << 16); }
    uint64_t u64 (size_t o) const { return (uint64_t) u32 (o) | ((uint64_t) u32 (o + 4) << 32); }
    size_t sectorOffset (uint32_t s) const { return (size_t) (s + 1) * sectorSize; }

    bool open (const juce::File& f)
    {
        if (! f.loadFileAsData (file)) return false;
        const unsigned char sig[] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
        if (memcmp (file.getData(), sig, 8) != 0) return false;
        sectorSize = 1u << u16 (30); miniSize = 1u << u16 (32); miniCutoff = u32 (56);
        const uint32_t fatCount = u32 (44), difatStart = u32 (68), difatCount = u32 (72);
        std::vector<uint32_t> fatSectors;
        for (uint32_t i = 0; i < 109 && i < fatCount; ++i) fatSectors.push_back (u32 (76 + 4 * i));
        uint32_t d = difatStart;
        for (uint32_t n = 0; n < difatCount && d != 0xFFFFFFFE; ++n)
        {
            const auto base = sectorOffset (d);
            for (uint32_t i = 0; i + 1 < sectorSize / 4 && fatSectors.size() < fatCount; ++i) fatSectors.push_back (u32 (base + 4 * i));
            d = u32 (base + sectorSize - 4);
        }
        for (auto s : fatSectors) for (uint32_t i = 0; i < sectorSize / 4; ++i) fat.push_back (u32 (sectorOffset (s) + 4 * i));
        auto readChain = [&] (uint32_t start, uint64_t size) { juce::MemoryBlock out; for (uint32_t s = start; s != 0xFFFFFFFE && out.getSize() < size; s = fat[s]) out.append ((const char*) file.getData() + sectorOffset (s), sectorSize); out.setSize ((size_t) size); return out; };
        const auto dir = readChain (u32 (48), (uint64_t) sectorSize * juce::jmax<uint32_t> (1, u32 (40) == 0 ? 0xFFFF : u32 (40)));
        for (size_t o = 0; o + 128 <= dir.getSize(); o += 128)
        {
            const auto* p = (const unsigned char*) dir.getData() + o;
            Entry e; e.type = p[66];
            if (e.type == 0) { if (entries.empty()) break; entries.push_back (e); continue; }
            const auto nameLen = (uint16_t) (p[64] | (p[65] << 8));
            juce::MemoryBlock nameBytes (p, nameLen); e.name = juce::String (juce::CharPointer_UTF16 ((const juce::CharPointer_UTF16::CharType*) nameBytes.getData()));
            e.left = juce::ByteOrder::littleEndianInt (p + 68); e.right = juce::ByteOrder::littleEndianInt (p + 72); e.child = juce::ByteOrder::littleEndianInt (p + 76);
            memcpy (e.clsid.data(), p + 80, 16);
            e.start = juce::ByteOrder::littleEndianInt (p + 116); e.size = juce::ByteOrder::littleEndianInt64 (p + 120);
            entries.push_back (e);
        }
        const uint32_t miniFatStart = u32 (60), miniFatCount = u32 (64);
        if (miniFatCount > 0) { const auto mf = readChain (miniFatStart, (uint64_t) miniFatCount * sectorSize); for (size_t i = 0; i + 4 <= mf.getSize(); i += 4) miniFat.push_back (juce::ByteOrder::littleEndianInt ((const char*) mf.getData() + i)); }
        if (entries[0].size > 0) miniStream = readChain (entries[0].start, entries[0].size);
        return true;
    }
    juce::MemoryBlock read (const Entry& e) const
    {
        juce::MemoryBlock out;
        if (e.size == 0) return out;
        if (e.size < miniCutoff) { for (uint32_t s = e.start; s != 0xFFFFFFFE && out.getSize() < e.size; s = miniFat[s]) out.append ((const char*) miniStream.getData() + (size_t) s * miniSize, miniSize); }
        else for (uint32_t s = e.start; s != 0xFFFFFFFE && out.getSize() < e.size; s = fat[s]) out.append ((const char*) file.getData() + sectorOffset (s), sectorSize);
        out.setSize ((size_t) e.size);
        return out;
    }
    // Children of a storage by walking its sibling tree.
    std::vector<const Entry*> children (const Entry& storage) const
    {
        std::vector<const Entry*> out;
        std::function<void (uint32_t)> walk = [&] (uint32_t id) { if (id == 0xFFFFFFFF || id >= entries.size()) return; walk (entries[id].left); out.push_back (&entries[id]); walk (entries[id].right); };
        walk (storage.child);
        return out;
    }
    const Entry* find (const juce::String& path) const
    {
        const Entry* cur = &entries[0];
        for (const auto& part : juce::StringArray::fromTokens (path, "/", {}))
        {
            if (part.isEmpty()) continue;
            const Entry* next = nullptr;
            for (auto* c : children (*cur)) if (c->name == part) next = c;
            if (next == nullptr) return nullptr;
            cur = next;
        }
        return cur;
    }
    // Decodes an AAF properties stream into pid -> (storedForm, value).
    std::map<uint16_t, std::pair<uint16_t, juce::MemoryBlock>> properties (const juce::String& storagePath) const
    {
        std::map<uint16_t, std::pair<uint16_t, juce::MemoryBlock>> out;
        const auto* e = find (storagePath + "/properties");
        if (e == nullptr) return out;
        const auto data = read (*e);
        const auto* p = (const unsigned char*) data.getData();
        const auto count = (uint16_t) (p[2] | (p[3] << 8));
        size_t off = 4 + 6 * (size_t) count;
        for (uint16_t i = 0; i < count; ++i)
        {
            const auto* h = p + 4 + 6 * i;
            const uint16_t pid = (uint16_t) (h[0] | (h[1] << 8)), sf = (uint16_t) (h[2] | (h[3] << 8)), len = (uint16_t) (h[4] | (h[5] << 8));
            out[pid] = { sf, juce::MemoryBlock (p + off, len) };
            off += len;
        }
        return out;
    }
};

juce::String utf16 (const juce::MemoryBlock& b) { return juce::String (juce::CharPointer_UTF16 ((const juce::CharPointer_UTF16::CharType*) b.getData())); }
int64_t i64 (const juce::MemoryBlock& b) { return (int64_t) juce::ByteOrder::littleEndianInt64 (b.getData()); }
uint32_t u32 (const juce::MemoryBlock& b) { return juce::ByteOrder::littleEndianInt (b.getData()); }
}

TEST_CASE ("CfbWriter produces a structured storage file that reads back")
{
    persistence::cfb::Node root; root.name = "Root Entry";
    juce::MemoryBlock big (10000); for (size_t i = 0; i < big.getSize(); ++i) ((unsigned char*) big.getData())[i] = (unsigned char) (i * 7);
    juce::MemoryBlock small (100); for (size_t i = 0; i < small.getSize(); ++i) ((unsigned char*) small.getData())[i] = (unsigned char) (i + 1);
    juce::MemoryBlock exact (4096, true);
    root.addStream ("big", big);
    root.addStream ("small", small);
    root.addStream ("empty", nullptr, 0);
    auto& sub = root.addStorage ("Sub-1", persistence::aaf::classes::header);
    sub.addStream ("exact", exact);
    sub.addStream ("a", small);
    auto& deep = sub.addStorage ("Deep{0}", persistence::aaf::classes::sequence);
    deep.addStream ("properties", small);
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_cfb_test.bin");
    { juce::FileOutputStream out (file); out.setPosition (0); out.truncate(); juce::String err; REQUIRE (persistence::cfb::write (root, persistence::aaf::ids::fileKind4096, out, err)); CHECK (err.isEmpty()); }
    CfbReader r; REQUIRE (r.open (file));
    CHECK (r.sectorSize == 4096);
    CHECK (r.u16 (26) == 4);                  // major version 4
    CHECK (r.u32 (68) == 0xFFFFFFFE);         // no DIFAT sectors -> end of chain
    CHECK (file.getSize() % 4096 == 0);
    REQUIRE (r.find ("/big") != nullptr);
    CHECK (r.read (*r.find ("/big")) == big);
    CHECK (r.read (*r.find ("/small")) == small);
    CHECK (r.find ("/empty")->size == 0);
    CHECK (r.read (*r.find ("/Sub-1/exact")) == exact);
    CHECK (r.read (*r.find ("/Sub-1/a")) == small);
    CHECK (r.read (*r.find ("/Sub-1/Deep{0}/properties")) == small);
    CHECK (r.find ("/Sub-1")->clsid == persistence::aaf::classes::header);
    CHECK (r.find ("/Sub-1/Deep{0}")->clsid == persistence::aaf::classes::sequence);
    CHECK (r.children (r.entries[0]).size() == 4);
    CHECK (persistence::cfb::compareNames ("b", "AA") < 0);      // shorter first
    CHECK (persistence::cfb::compareNames ("abc", "ABD") < 0);   // then case-insensitive
    file.deleteFile();
}

TEST_CASE ("AAF export writes a composition with slots, clips, markers and embedded essence that reads back")
{
    model::Session session;
    const double rate = 48000.0;
    auto mono = std::make_shared<juce::AudioBuffer<float>> (1, 4800);
    for (int i = 0; i < 4800; ++i) mono->setSample (0, i, std::sin (i * 0.05f) * 0.5f);
    auto stereo = std::make_shared<juce::AudioBuffer<float>> (2, 9600);
    for (int i = 0; i < 9600; ++i) { stereo->setSample (0, i, 0.25f); stereo->setSample (1, i, -0.25f); }
    {
        model::Track t; t.name = "Vox"; t.type = model::Track::Type::audio;
        model::AudioClip a; a.name = "Vox take"; a.audio = mono; a.sampleRate = rate; a.timelineStart = 48000; a.sourceOffset = 1000; a.length = 3000; a.gain = 0.5f; a.fadeIn = 100;
        model::AudioClip b = a; b.name = "Vox again"; b.timelineStart = 96000; b.sourceOffset = 0; b.length = 4800; b.gain = 1.0f; b.fadeIn = 0;
        t.clips = { a, b };
        session.execute (std::make_unique<model::AddTrackCommand> (t));
    }
    {
        model::Track t; t.name = "Pad"; t.type = model::Track::Type::audio;
        model::AudioClip a; a.name = "Pad"; a.audio = stereo; a.sampleRate = rate; a.timelineStart = 0; a.length = 9600;
        t.clips = { a };
        session.execute (std::make_unique<model::AddTrackCommand> (t));
    }
    { model::Track t; t.name = "Synth"; t.type = model::Track::Type::instrument; session.execute (std::make_unique<model::AddTrackCommand> (t)); }
    session.execute (std::make_unique<model::AddMarkerCommand> (model::Marker { 0, "Verse", 1.0 }));
    session.execute (std::make_unique<model::AddMarkerCommand> (model::Marker { 0, "Chorus", 2.5, 3.5 }));

    const auto exportable = persistence::AafExport::exportableTracks (session);
    CHECK (exportable == std::vector<int> { 0, 1 });   // the unfrozen instrument track is skipped

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_export_test.aaf");
    persistence::AafExportOptions o; o.bitDepth = 24;
    const auto summary = persistence::AafExport::write (session, rate, "Test Session", file, o);
    REQUIRE (summary.ok());
    CHECK (summary.tracks == 2); CHECK (summary.slots == 3); CHECK (summary.clips == 4); CHECK (summary.essences == 4); CHECK (summary.markers == 2);
    CHECK (summary.fileBytes == file.getSize());

    CfbReader r; REQUIRE (r.open (file));
    std::array<unsigned char, 16> hdrClsid; memcpy (hdrClsid.data(), (const char*) r.file.getData() + 8, 16);
    CHECK (hdrClsid == persistence::aaf::ids::fileKind4096);
    CHECK (r.entries[0].clsid == persistence::aaf::classes::root);
    REQUIRE (r.find ("/MetaDictionary-1") != nullptr);
    REQUIRE (r.find ("/referenced properties") != nullptr);
    REQUIRE (r.find ("/Header-2/Dictionary-3b04/properties") != nullptr);
    const auto rootProps = r.properties ("/");
    REQUIRE (rootProps.count (0x0002));
    CHECK (utf16 (rootProps.at (0x0002).second) == "Header-2");
    const auto header = r.properties ("/Header-2");
    CHECK (utf16 (header.at (0x3b03).second) == "Content-3b03");
    CHECK (utf16 (header.at (0x3b04).second) == "Dictionary-3b04");
    CHECK (header.at (0x3b01).second.getSize() == 2);
    // Mobs: composition + (master + source) per essence
    const auto content = r.properties ("/Header-2/Content-3b03");
    CHECK (content.at (0x1901).first == 0x3A);
    const auto mobsIndex = r.read (*r.find ("/Header-2/Content-3b03/Mobs-1901 index"));
    CHECK (u32 (mobsIndex) == 1 + 2 * 4);
    CHECK (((const unsigned char*) mobsIndex.getData())[12] == 0x01); CHECK (((const unsigned char*) mobsIndex.getData())[13] == 0x44);   // key pid 0x4401
    const auto comp = r.properties ("/Header-2/Content-3b03/Mobs-1901{0}");
    CHECK (utf16 (comp.at (0x4402).second) == "Test Session");
    CHECK (comp.at (0x4408).second.getSize() == 16);   // UsageCode
    // Slots: timecode, Vox, Pad.L, Pad.R, Markers
    const auto slotsIndex = r.read (*r.find ("/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403 index"));
    CHECK (u32 (slotsIndex) == 5);
    CHECK (utf16 (r.properties ("/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{0}").at (0x4802).second) == "Timecode");
    CHECK (utf16 (r.properties ("/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{1}").at (0x4802).second) == "Vox");
    CHECK (utf16 (r.properties ("/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{2}").at (0x4802).second) == "Pad.L");
    CHECK (utf16 (r.properties ("/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{3}").at (0x4802).second) == "Pad.R");
    CHECK (utf16 (r.properties ("/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{4}").at (0x4802).second) == "Markers");
    CHECK (r.find ("/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{4}")->clsid == persistence::aaf::classes::eventMobSlot);
    // Vox sequence: filler 48000, clip 3000, filler 45000, clip 4800
    const juce::String voxSeq = "/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{1}/Segment-4803";
    CHECK (r.find (voxSeq)->clsid == persistence::aaf::classes::sequence);
    CHECK (i64 (r.properties (voxSeq).at (0x0202).second) == 96000 + 4800);
    CHECK (u32 (r.read (*r.find (voxSeq + "/Components-1001 index"))) == 4);
    CHECK (r.find (voxSeq + "/Components-1001{0}")->clsid == persistence::aaf::classes::filler);
    CHECK (i64 (r.properties (voxSeq + "/Components-1001{0}").at (0x0202).second) == 48000);
    const auto clip1 = r.properties (voxSeq + "/Components-1001{1}");
    CHECK (r.find (voxSeq + "/Components-1001{1}")->clsid == persistence::aaf::classes::sourceClip);
    CHECK (i64 (clip1.at (0x0202).second) == 3000);
    CHECK (i64 (clip1.at (0x1201).second) == 0);            // consolidated: starts at the essence start
    CHECK (clip1.at (0x0201).first == 0x02);                // weak reference to the sound data definition
    CHECK (clip1.at (0x0201).second.getSize() == 21);
    CHECK (i64 (r.properties (voxSeq + "/Components-1001{2}").at (0x0202).second) == 45000);
    CHECK (i64 (r.properties (voxSeq + "/Components-1001{3}").at (0x0202).second) == 4800);
    // Markers
    const juce::String markerSeq = "/Header-2/Content-3b03/Mobs-1901{0}/Slots-4403{4}/Segment-4803";
    CHECK (i64 (r.properties (markerSeq + "/Components-1001{0}").at (0x0601).second) == 48000);
    CHECK (utf16 (r.properties (markerSeq + "/Components-1001{0}").at (0x0602).second) == "Verse");
    CHECK (i64 (r.properties (markerSeq + "/Components-1001{1}").at (0x0202).second) == 48000);   // ranged marker length
    // Essence: the first clip's consolidated audio (gain 0.5, 100-sample fade in, offset 1000)
    const auto essenceIndex = r.read (*r.find ("/Header-2/Content-3b03/EssenceData-1902 index"));
    CHECK (u32 (essenceIndex) == 4);
    const auto ed0 = r.properties ("/Header-2/Content-3b03/EssenceData-1902{0}");
    CHECK (ed0.at (0x2702).first == 0x42);
    CHECK (utf16 (juce::MemoryBlock ((const char*) ed0.at (0x2702).second.getData() + 1, ed0.at (0x2702).second.getSize() - 1)) == "Data-2702");
    const auto pcm = r.read (*r.find ("/Header-2/Content-3b03/EssenceData-1902{0}/Data-2702"));
    REQUIRE (pcm.getSize() == 3000 * 3);
    auto sample24 = [&] (int i) { const auto* p = (const unsigned char*) pcm.getData() + i * 3; int v = p[0] | (p[1] << 8) | (p[2] << 16); if (v & 0x800000) v -= 0x1000000; return v / 8388607.0f; };
    CHECK (std::abs (sample24 (0)) < 1e-4f);                                             // fade in starts at silence
    CHECK (std::abs (sample24 (2000) - mono->getSample (0, 3000) * 0.5f) < 2e-4f);       // offset 1000 + gain 0.5
    // Source mob descriptor, master mob chain
    const auto desc = r.properties ("/Header-2/Content-3b03/Mobs-1901{2}/EssenceDescription-4701");
    CHECK (r.find ("/Header-2/Content-3b03/Mobs-1901{2}/EssenceDescription-4701")->clsid == persistence::aaf::classes::pcmDescriptor);
    CHECK (i64 (desc.at (0x3002).second) == 3000);
    CHECK (u32 (desc.at (0x3d07).second) == 1);
    CHECK (u32 (desc.at (0x3d01).second) == 24);
    CHECK (desc.at (0x3004).first == 0x02);
    const auto masterClip = r.properties ("/Header-2/Content-3b03/Mobs-1901{1}/Slots-4403{0}/Segment-4803");
    const auto sourceMobId = r.properties ("/Header-2/Content-3b03/Mobs-1901{2}").at (0x4401).second;
    CHECK (masterClip.at (0x1101).second == sourceMobId);
    CHECK (r.properties ("/Header-2/Content-3b03/EssenceData-1902{0}").at (0x2701).second == sourceMobId);
    file.deleteFile();

    // Linked mode with whole files: WAVs beside the AAF, clips keep their source offsets, essences shared per buffer/channel
    const auto linked = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_linked_test.aaf");
    persistence::AafExportOptions lo; lo.embedAudio = false; lo.consolidateClips = false;
    const auto ls = persistence::AafExport::write (session, rate, "Linked", linked, lo);
    REQUIRE (ls.ok());
    CHECK (ls.essences == 3);                       // mono buffer + stereo L + stereo R
    REQUIRE (ls.mediaFiles.size() == 3);
    for (const auto& f : ls.mediaFiles) CHECK (f.existsAsFile());
    CHECK (ls.mediaFiles[0].getParentDirectory().getFileName() == "beatmaker_linked_test Media");
    CfbReader lr; REQUIRE (lr.open (linked));
    CHECK (u32 (lr.read (*lr.find ("/Header-2/Content-3b03/EssenceData-1902 index"))) == 0);
    CHECK (i64 (lr.properties (voxSeq + "/Components-1001{1}").at (0x1201).second) == 1000);   // source offset kept
    const auto ldesc = lr.properties ("/Header-2/Content-3b03/Mobs-1901{2}/EssenceDescription-4701");
    CHECK (utf16 (lr.properties ("/Header-2/Content-3b03/Mobs-1901{2}/EssenceDescription-4701/Locator-2f01{0}").at (0x4001).second).startsWith ("file://"));
    CHECK (i64 (ldesc.at (0x3002).second) == 4800);
    for (const auto& f : ls.mediaFiles) f.deleteFile();
    ls.mediaFiles[0].getParentDirectory().deleteFile();
    linked.deleteFile();
}
