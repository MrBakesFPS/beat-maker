#include "CfbWriter.h"
#include <algorithm>
#include <functional>

namespace beatmaker::persistence::cfb
{

namespace
{
constexpr uint32_t sectorSize = 4096, miniSectorSize = 64, miniCutoff = 4096;
constexpr uint32_t entriesPerFat = sectorSize / 4, entriesPerDifat = entriesPerFat - 1, headerDifatEntries = 109;
constexpr uint32_t endOfChain = 0xFFFFFFFE, freeSect = 0xFFFFFFFF, fatSect = 0xFFFFFFFD, difSect = 0xFFFFFFFC, noStream = 0xFFFFFFFF;

struct Entry
{
    const Node* node = nullptr;
    uint32_t left = noStream, right = noStream, child = noStream;
    uint32_t startSector = endOfChain;
    uint64_t size = 0;
    bool inMiniStream = false;
};

void put16 (juce::MemoryOutputStream& o, uint16_t v) { o.writeShort ((short) v); }
void put32 (juce::MemoryOutputStream& o, uint32_t v) { o.writeInt ((int) v); }
void put64 (juce::MemoryOutputStream& o, uint64_t v) { o.writeInt64 ((juce::int64) v); }

uint32_t sectorsFor (uint64_t bytes, uint32_t sector) { return (uint32_t) ((bytes + sector - 1) / sector); }
} // namespace

Node& Node::addStorage (const juce::String& n, const Clsid& c)
{
    auto node = std::make_unique<Node>(); node->name = n; node->isStorage = true; node->clsid = c;
    children.push_back (std::move (node)); return *children.back();
}

Node& Node::addStream (const juce::String& n, const void* d, size_t size)
{
    auto node = std::make_unique<Node>(); node->name = n; node->isStorage = false;
    if (size > 0) node->data.append (d, size);
    children.push_back (std::move (node)); return *children.back();
}

const Node* Node::find (const juce::String& n) const
{
    for (const auto& c : children) if (c->name == n) return c.get();
    return nullptr;
}

int compareNames (const juce::String& a, const juce::String& b)
{
    const auto la = a.length(), lb = b.length();
    if (la != lb) return la < lb ? -1 : 1;
    return a.toUpperCase().compare (b.toUpperCase());
}

bool write (const Node& root, const Clsid& headerClsid, juce::OutputStream& out, juce::String& error)
{
    // 1. Directory entries: depth first, each storage's children arranged as a
    //    balanced binary search tree in the format's name order.
    std::vector<Entry> entries;
    std::function<uint32_t (const Node&)> add = [&] (const Node& node) -> uint32_t
    {
        if (node.name.length() > 31) { error = "Name too long: " + node.name; return noStream; }
        const auto id = (uint32_t) entries.size();
        entries.push_back ({ &node });
        if (! node.isStorage) return id;
        std::vector<const Node*> kids;
        for (const auto& c : node.children) kids.push_back (c.get());
        std::sort (kids.begin(), kids.end(), [] (const Node* a, const Node* b) { return compareNames (a->name, b->name) < 0; });
        for (size_t i = 1; i < kids.size(); ++i)
            if (compareNames (kids[i - 1]->name, kids[i]->name) == 0) { error = "Duplicate name: " + kids[i]->name; return noStream; }
        std::vector<uint32_t> ids;
        for (auto* k : kids) { const auto kid = add (*k); if (kid == noStream) return noStream; ids.push_back (kid); }
        std::function<uint32_t (int, int)> tree = [&] (int lo, int hi) -> uint32_t
        {
            if (lo > hi) return noStream;
            const int mid = (lo + hi) / 2;
            entries[ids[(size_t) mid]].left = tree (lo, mid - 1);
            entries[ids[(size_t) mid]].right = tree (mid + 1, hi);
            return ids[(size_t) mid];
        };
        entries[id].child = tree (0, (int) ids.size() - 1);
        return id;
    };
    if (add (root) == noStream) return false;

    // 2. Plan sectors: big streams, then the mini stream, mini FAT, directory, FAT, DIFAT.
    std::vector<uint32_t> fat;
    juce::MemoryBlock miniStream;
    std::vector<uint32_t> miniFat;
    std::vector<const Entry*> bigStreams;
    for (auto& e : entries)
    {
        if (e.node->isStorage) continue;
        e.size = e.node->data.getSize();
        if (e.size == 0) { e.startSector = endOfChain; continue; }
        if (e.size < miniCutoff)
        {
            e.inMiniStream = true;
            e.startSector = (uint32_t) miniFat.size();
            const auto n = sectorsFor (e.size, miniSectorSize);
            for (uint32_t i = 0; i < n; ++i) miniFat.push_back (i + 1 < n ? e.startSector + i + 1 : endOfChain);
            const auto before = miniStream.getSize();
            miniStream.append (e.node->data.getData(), e.size);
            miniStream.setSize (before + (size_t) n * miniSectorSize, true);   // pad this stream to whole mini sectors
        }
        else bigStreams.push_back (&e);
    }
    const uint32_t dirSectors = sectorsFor ((uint64_t) entries.size() * 128, sectorSize);
    const uint32_t miniStreamSectors = sectorsFor (miniStream.getSize(), sectorSize);
    const uint32_t miniFatSectors = sectorsFor ((uint64_t) miniFat.size() * 4, sectorSize);
    uint64_t dataSectors = miniStreamSectors + miniFatSectors + dirSectors;
    for (auto* e : bigStreams) dataSectors += sectorsFor (e->size, sectorSize);
    uint32_t fatSectors = juce::jmax<uint32_t> (1, sectorsFor (dataSectors * 4, sectorSize)), difatSectors = 0;
    for (;;)
    {
        difatSectors = fatSectors <= headerDifatEntries ? 0 : sectorsFor ((uint64_t) (fatSectors - headerDifatEntries) * 4, entriesPerDifat * 4);
        if ((uint64_t) fatSectors * entriesPerFat >= dataSectors + fatSectors + difatSectors) break;
        ++fatSectors;
    }

    // 3. Lay out the sector chains.
    auto chain = [&] (uint64_t bytes) -> uint32_t
    {
        const auto n = sectorsFor (bytes, sectorSize);
        const auto start = (uint32_t) fat.size();
        for (uint32_t i = 0; i < n; ++i) fat.push_back (i + 1 < n ? start + i + 1 : endOfChain);
        return n > 0 ? start : endOfChain;
    };
    for (auto* e : bigStreams) const_cast<Entry*> (e)->startSector = chain (e->size);
    const uint32_t miniStreamStart = chain (miniStream.getSize());
    const uint32_t miniFatStart = chain ((uint64_t) miniFat.size() * 4);
    const uint32_t dirStart = chain ((uint64_t) entries.size() * 128);
    const uint32_t fatStart = (uint32_t) fat.size();
    for (uint32_t i = 0; i < fatSectors; ++i) fat.push_back (fatSect);
    const uint32_t difatStart = (uint32_t) fat.size();
    for (uint32_t i = 0; i < difatSectors; ++i) fat.push_back (difSect);
    jassert (fat.size() <= (size_t) fatSectors * entriesPerFat);

    // Root entry: the mini stream lives in the root's chain.
    entries[0].startSector = miniStreamStart;
    entries[0].size = miniStream.getSize();

    // 4. Header (512 bytes, padded to a sector).
    juce::MemoryOutputStream h;
    const unsigned char sig[] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    h.write (sig, 8);
    h.write (headerClsid.data(), 16);
    put16 (h, 0x003E); put16 (h, 0x0004);   // minor, major (version 4: 4096-byte sectors)
    put16 (h, 0xFFFE);                       // byte order
    put16 (h, 12); put16 (h, 6);             // sector shift, mini sector shift
    for (int i = 0; i < 6; ++i) h.writeByte (0);
    put32 (h, dirSectors);
    put32 (h, fatSectors);
    put32 (h, dirStart);
    put32 (h, 0);                            // transaction signature
    put32 (h, miniCutoff);
    put32 (h, miniFat.empty() ? endOfChain : miniFatStart);
    put32 (h, miniFatSectors);
    put32 (h, difatSectors > 0 ? difatStart : endOfChain);
    put32 (h, difatSectors);
    for (uint32_t i = 0; i < headerDifatEntries; ++i) put32 (h, i < fatSectors ? fatStart + i : freeSect);
    jassert (h.getDataSize() == 512);
    out.write (h.getData(), h.getDataSize());
    for (uint32_t i = 512; i < sectorSize; ++i) out.writeByte (0);

    // 5. Sectors in order.
    auto writePadded = [&] (const void* data, uint64_t bytes)
    {
        out.write (data, (size_t) bytes);
        const auto pad = (uint64_t) sectorsFor (bytes, sectorSize) * sectorSize - bytes;
        for (uint64_t i = 0; i < pad; ++i) out.writeByte (0);
    };
    for (auto* e : bigStreams) writePadded (e->node->data.getData(), e->size);
    if (miniStream.getSize() > 0) writePadded (miniStream.getData(), miniStream.getSize());
    if (! miniFat.empty())
    {
        juce::MemoryOutputStream m;
        for (auto v : miniFat) put32 (m, v);
        while (m.getDataSize() < (size_t) miniFatSectors * sectorSize) put32 (m, freeSect);
        out.write (m.getData(), m.getDataSize());
    }
    {
        juce::MemoryOutputStream d;
        for (size_t i = 0; i < (size_t) dirSectors * (sectorSize / 128); ++i)
        {
            if (i < entries.size())
            {
                const auto& e = entries[i];
                juce::CharPointer_UTF16::CharType nameBuf[32] = {};
                juce::String (e.node->name).copyToUTF16 (nameBuf, sizeof (nameBuf));
                int units = 0; while (units < 32 && nameBuf[units] != 0) ++units;
                for (int u = 0; u < 32; ++u) put16 (d, (uint16_t) nameBuf[u]);
                put16 (d, (uint16_t) ((units + 1) * 2));
                d.writeByte (i == 0 ? 5 : e.node->isStorage ? 1 : 2);
                d.writeByte (1);   // black
                put32 (d, e.left); put32 (d, e.right); put32 (d, e.child);
                if (e.node->isStorage) d.write (e.node->clsid.data(), 16); else for (int b = 0; b < 16; ++b) d.writeByte (0);
                put32 (d, 0);                   // state bits
                put64 (d, 0); put64 (d, 0);     // creation / modification time
                put32 (d, e.node->isStorage && i != 0 ? 0 : e.startSector);
                put64 (d, e.node->isStorage && i != 0 ? 0 : e.size);
            }
            else
            {
                for (int u = 0; u < 32; ++u) put16 (d, 0);
                put16 (d, 0); d.writeByte (0); d.writeByte (0);
                put32 (d, noStream); put32 (d, noStream); put32 (d, noStream);
                for (int b = 0; b < 16; ++b) d.writeByte (0);
                put32 (d, 0); put64 (d, 0); put64 (d, 0); put32 (d, 0); put64 (d, 0);
            }
        }
        out.write (d.getData(), d.getDataSize());
    }
    {
        juce::MemoryOutputStream f;
        for (auto v : fat) put32 (f, v);
        while (f.getDataSize() < (size_t) fatSectors * sectorSize) put32 (f, freeSect);
        out.write (f.getData(), f.getDataSize());
    }
    if (difatSectors > 0)
    {
        juce::MemoryOutputStream d;
        uint32_t next = headerDifatEntries;
        for (uint32_t s = 0; s < difatSectors; ++s)
        {
            for (uint32_t i = 0; i < entriesPerDifat; ++i) { put32 (d, next < fatSectors ? fatStart + next : freeSect); ++next; }
            put32 (d, s + 1 < difatSectors ? difatStart + s + 1 : endOfChain);
        }
        out.write (d.getData(), d.getDataSize());
    }
    return true;
}

} // namespace beatmaker::persistence::cfb
