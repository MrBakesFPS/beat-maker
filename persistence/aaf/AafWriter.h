// AafWriter: the AAF stored format on top of CfbWriter. An Object is one
// storage with a "properties" stream (byte order, version, property index,
// values) and child storages or streams for strong references, vectors,
// sets and data streams, named the way the AAF SDK names them.
#pragma once

#include "CfbWriter.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <vector>

namespace beatmaker::persistence::aaf
{

using Auid = cfb::Clsid;                        // 16 bytes in stored order
using MobIdBytes = std::array<unsigned char, 32>;

Auid auidFromHex (const char* hex32);           // "0101010d0101002f060e2b3402060101"
MobIdBytes makeMobId();                         // SMPTE UMID with a random material number
MobIdBytes nullMobId();

// Stored forms
constexpr uint16_t sfData = 0x82, sfStream = 0x42, sfStrongRef = 0x22, sfStrongVector = 0x32, sfStrongSet = 0x3A, sfWeakRef = 0x02;

// Class identifiers (stored byte order)
namespace classes
{
extern const Auid root, header, identification, contentStorage, compositionMob, masterMob, sourceMob, timelineMobSlot, eventMobSlot,
    sequence, sourceClip, filler, timecode, descriptiveMarker, pcmDescriptor, networkLocator, essenceData, metaDictionary, dictionary;
}
namespace ids
{
extern const Auid fileKind4096, opEditProtocol, usageTopLevel, dataDefSound, dataDefTimecode, dataDefDescriptiveMetadata, containerAaf, containerExternal;
}
// Referenced-property table indices (the table itself is generated data)
enum RefPath : uint16_t { refClassDefinitions = 0, refTypeDefinitions = 1, refDataDefinitions = 2, refContainerDefinitions = 3 };
constexpr uint16_t pidDefinitionIdentification = 0x1b01;

class Object
{
public:
    explicit Object (const Auid& classId) : clsid (classId) {}

    void addData (uint16_t pid, const void* data, size_t size);
    void addU8 (uint16_t pid, uint8_t v)   { addData (pid, &v, 1); }
    void addU16 (uint16_t pid, uint16_t v);
    void addU32 (uint16_t pid, uint32_t v);
    void addI64 (uint16_t pid, int64_t v);
    void addBool (uint16_t pid, bool v)     { addU8 (pid, v ? 1 : 0); }
    void addRational (uint16_t pid, int32_t num, int32_t den);
    void addAuid (uint16_t pid, const Auid& a) { addData (pid, a.data(), a.size()); }
    void addMobId (uint16_t pid, const MobIdBytes& m) { addData (pid, m.data(), m.size()); }
    void addString (uint16_t pid, const juce::String&);
    void addTimeStamp (uint16_t pid, juce::Time);
    void addVersion (uint16_t pid, uint16_t major, uint16_t minor, uint16_t tertiary, uint16_t patch, uint8_t type);
    void addWeakRef (uint16_t pid, RefPath path, const Auid& key);
    void addStrongRef (uint16_t pid, const char* propertyName, Object child);
    void addStrongVector (uint16_t pid, const char* propertyName, std::vector<Object> children);
    // Sets are keyed by one of each member's properties (its AUID or MobID).
    void addStrongSet (uint16_t pid, const char* propertyName, std::vector<Object> children, uint16_t keyPid, const std::vector<std::vector<unsigned char>>& keys);
    void addStream (uint16_t pid, const char* propertyName, juce::MemoryBlock data);

    // Builds this object as a storage named `storageName` inside `parent`.
    cfb::Node& emit (cfb::Node& parent, const juce::String& storageName) const;
    static juce::String mangle (const char* propertyName, uint16_t pid, int maxLength = 31);

private:
    struct Prop { uint16_t pid, sf; std::vector<unsigned char> value; };
    struct StrongRefChild { juce::String name; std::vector<Object> objects; bool isVector = false, isSet = false; uint16_t keyPid = 0; std::vector<std::vector<unsigned char>> keys; };
    struct StreamChild { juce::String name; juce::MemoryBlock data; };
    Auid clsid;
    std::vector<Prop> props;
    std::vector<StrongRefChild> refs;
    std::vector<StreamChild> streams;
};

// Adds the generated MetaDictionary and Dictionary subtrees. `dictionaryParent`
// is the Header storage; the root gets the MetaDictionary and the
// "referenced properties" stream.
bool addStandardDictionaries (cfb::Node& root, cfb::Node& headerStorage, juce::String& error);

} // namespace beatmaker::persistence::aaf
