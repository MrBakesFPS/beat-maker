#include "AafWriter.h"
#include "AafDictionaryData.h"

namespace beatmaker::persistence::aaf
{

Auid auidFromHex (const char* hex)
{
    Auid a {};
    for (size_t i = 0; i < 16; ++i)
        a[i] = (unsigned char) ((juce::CharacterFunctions::getHexDigitValue ((juce::juce_wchar) hex[i * 2]) << 4)
                                | juce::CharacterFunctions::getHexDigitValue ((juce::juce_wchar) hex[i * 2 + 1]));
    return a;
}

MobIdBytes makeMobId()
{
    MobIdBytes m {};
    const unsigned char label[] = { 0x06, 0x0a, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x05, 0x01, 0x01, 0x0f, 0x20 };
    std::copy (std::begin (label), std::end (label), m.begin());
    m[12] = 0x13;                     // length of the material and instance numbers
    m[13] = m[14] = m[15] = 0;        // instance number
    const auto uuid = juce::Uuid();
    std::copy (uuid.getRawData(), uuid.getRawData() + 16, m.begin() + 16);
    return m;
}

MobIdBytes nullMobId() { return MobIdBytes {}; }

namespace classes
{
const Auid root              = auidFromHex ("a598b3b3901cd4118053080036210804");
const Auid header            = auidFromHex ("0101010d0101002f060e2b3402060101");
const Auid identification    = auidFromHex ("0101010d01010030060e2b3402060101");
const Auid contentStorage    = auidFromHex ("0101010d01010018060e2b3402060101");
const Auid compositionMob    = auidFromHex ("0101010d01010035060e2b3402060101");
const Auid masterMob         = auidFromHex ("0101010d01010036060e2b3402060101");
const Auid sourceMob         = auidFromHex ("0101010d01010037060e2b3402060101");
const Auid timelineMobSlot   = auidFromHex ("0101010d0101003b060e2b3402060101");
const Auid eventMobSlot      = auidFromHex ("0101010d01010039060e2b3402060101");
const Auid sequence          = auidFromHex ("0101010d0101000f060e2b3402060101");
const Auid sourceClip        = auidFromHex ("0101010d01010011060e2b3402060101");
const Auid filler            = auidFromHex ("0101010d01010009060e2b3402060101");
const Auid timecode          = auidFromHex ("0101010d01010014060e2b3402060101");
const Auid descriptiveMarker = auidFromHex ("0101010d01010041060e2b3402060101");
const Auid pcmDescriptor     = auidFromHex ("0101010d01010048060e2b3402060101");
const Auid networkLocator    = auidFromHex ("0101010d01010032060e2b3402060101");
const Auid essenceData       = auidFromHex ("0101010d01010023060e2b3402060101");
const Auid metaDictionary    = auidFromHex ("0101010d25020000060e2b3402060101");
const Auid dictionary        = auidFromHex ("0101010d01010022060e2b3402060101");
}
namespace ids
{
const Auid fileKind4096              = auidFromHex ("0102010d00020000060e2b3403020101");
const Auid opEditProtocol            = auidFromHex ("0112010d00010000060e2b3404010105");
const Auid usageTopLevel             = auidFromHex ("0201010d01010007060e2b3404010101");
const Auid dataDefSound              = auidFromHex ("0202030100020000060e2b3404010101");
const Auid dataDefTimecode           = auidFromHex ("0102030100010000060e2b3404010101");
const Auid dataDefDescriptiveMetadata = auidFromHex ("0102030100100000060e2b3404010101");
const Auid containerAaf              = auidFromHex ("71b51343bad8d211809b006008143e6f");
const Auid containerExternal         = auidFromHex ("72b51343bad8d211809b006008143e6f");
}

//==============================================================================
static void appendU16 (std::vector<unsigned char>& v, uint16_t x) { v.push_back ((unsigned char) (x & 0xff)); v.push_back ((unsigned char) (x >> 8)); }
static void appendU32 (std::vector<unsigned char>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((unsigned char) ((x >> (8 * i)) & 0xff)); }
static void appendU64 (std::vector<unsigned char>& v, uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back ((unsigned char) ((x >> (8 * i)) & 0xff)); }
static void appendUtf16 (std::vector<unsigned char>& v, const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
    {
        juce::CharPointer_UTF16::CharType units[3] = {};
        juce::CharPointer_UTF16 w (units);
        w.write (*p);
        for (int i = 0; i < 2 && units[i] != 0; ++i) appendU16 (v, (uint16_t) units[i]);
    }
    appendU16 (v, 0);
}

void Object::addData (uint16_t pid, const void* data, size_t size)
{
    jassert (size <= 0xffff);
    Prop p { pid, sfData, {} };
    p.value.assign ((const unsigned char*) data, (const unsigned char*) data + size);
    props.push_back (std::move (p));
}
void Object::addU16 (uint16_t pid, uint16_t v) { std::vector<unsigned char> b; appendU16 (b, v); addData (pid, b.data(), b.size()); }
void Object::addU32 (uint16_t pid, uint32_t v) { std::vector<unsigned char> b; appendU32 (b, v); addData (pid, b.data(), b.size()); }
void Object::addI64 (uint16_t pid, int64_t v) { std::vector<unsigned char> b; appendU64 (b, (uint64_t) v); addData (pid, b.data(), b.size()); }
void Object::addRational (uint16_t pid, int32_t num, int32_t den) { std::vector<unsigned char> b; appendU32 (b, (uint32_t) num); appendU32 (b, (uint32_t) den); addData (pid, b.data(), b.size()); }
void Object::addString (uint16_t pid, const juce::String& s) { std::vector<unsigned char> b; appendUtf16 (b, s); addData (pid, b.data(), b.size()); }
void Object::addTimeStamp (uint16_t pid, juce::Time t)
{
    std::vector<unsigned char> b;
    appendU16 (b, (uint16_t) t.getYear());
    b.push_back ((unsigned char) (t.getMonth() + 1)); b.push_back ((unsigned char) t.getDayOfMonth());
    b.push_back ((unsigned char) t.getHours()); b.push_back ((unsigned char) t.getMinutes()); b.push_back ((unsigned char) t.getSeconds());
    b.push_back ((unsigned char) (t.getMilliseconds() / 4));   // 1/250 s
    addData (pid, b.data(), b.size());
}
void Object::addVersion (uint16_t pid, uint16_t major, uint16_t minor, uint16_t tertiary, uint16_t patch, uint8_t type)
{
    std::vector<unsigned char> b; appendU16 (b, major); appendU16 (b, minor); appendU16 (b, tertiary); appendU16 (b, patch); b.push_back (type);
    addData (pid, b.data(), b.size());
}
void Object::addWeakRef (uint16_t pid, RefPath path, const Auid& key)
{
    Prop p { pid, sfWeakRef, {} };
    appendU16 (p.value, (uint16_t) path); appendU16 (p.value, pidDefinitionIdentification); p.value.push_back (16);
    p.value.insert (p.value.end(), key.begin(), key.end());
    props.push_back (std::move (p));
}

juce::String Object::mangle (const char* propertyName, uint16_t pid, int maxLength)
{
    const auto hex = juce::String::toHexString ((int) pid);
    const int room = maxLength - hex.length() - 1;
    juce::String name (propertyName);
    if (name.length() > room)
    {
        const int half = room / 2;
        juce::String squeezed;
        for (int i = 0; i < room; ++i)
            squeezed << (i < half ? name[i] : i == half ? juce::juce_wchar ('-') : name[name.length() - (room - i)]);
        name = squeezed;
    }
    return name + "-" + hex;
}

void Object::addStrongRef (uint16_t pid, const char* propertyName, Object child)
{
    StrongRefChild r; r.name = mangle (propertyName, pid); r.objects.push_back (std::move (child));
    Prop p { pid, sfStrongRef, {} }; appendUtf16 (p.value, r.name);
    props.push_back (std::move (p)); refs.push_back (std::move (r));
}
void Object::addStrongVector (uint16_t pid, const char* propertyName, std::vector<Object> children)
{
    StrongRefChild r; r.name = mangle (propertyName, pid, 25); r.objects = std::move (children); r.isVector = true;
    Prop p { pid, sfStrongVector, {} }; appendUtf16 (p.value, r.name);
    props.push_back (std::move (p)); refs.push_back (std::move (r));
}
void Object::addStrongSet (uint16_t pid, const char* propertyName, std::vector<Object> children, uint16_t keyPid, const std::vector<std::vector<unsigned char>>& keys)
{
    jassert (keys.size() == children.size());
    StrongRefChild r; r.name = mangle (propertyName, pid, 25); r.objects = std::move (children); r.isSet = true; r.keyPid = keyPid; r.keys = keys;
    Prop p { pid, sfStrongSet, {} }; appendUtf16 (p.value, r.name);
    props.push_back (std::move (p)); refs.push_back (std::move (r));
}
void Object::addStream (uint16_t pid, const char* propertyName, juce::MemoryBlock data)
{
    StreamChild s; s.name = mangle (propertyName, pid); s.data = std::move (data);
    Prop p { pid, sfStream, {} }; p.value.push_back (0x55); appendUtf16 (p.value, s.name);   // 0x55: byte order unspecified
    props.push_back (std::move (p)); streams.push_back (std::move (s));
}

cfb::Node& Object::emit (cfb::Node& parent, const juce::String& storageName) const
{
    auto& node = parent.addStorage (storageName, clsid);
    std::vector<unsigned char> stream;
    stream.push_back (0x4c); stream.push_back (0x20);   // little-endian, stored format version 32
    appendU16 (stream, (uint16_t) props.size());
    for (const auto& p : props) { appendU16 (stream, p.pid); appendU16 (stream, p.sf); appendU16 (stream, (uint16_t) p.value.size()); }
    for (const auto& p : props) stream.insert (stream.end(), p.value.begin(), p.value.end());
    node.addStream ("properties", stream.data(), stream.size());
    for (const auto& r : refs)
    {
        if (! r.isVector && ! r.isSet) { r.objects.front().emit (node, r.name); continue; }
        std::vector<unsigned char> index;
        appendU32 (index, (uint32_t) r.objects.size());
        appendU32 (index, (uint32_t) r.objects.size());   // first free key
        appendU32 (index, 0xFFFFFFFF);                     // last free key
        if (r.isSet) { appendU16 (index, r.keyPid); index.push_back (r.keys.empty() ? 16 : (unsigned char) r.keys.front().size()); }
        for (size_t i = 0; i < r.objects.size(); ++i)
        {
            appendU32 (index, (uint32_t) i);
            if (r.isSet) { appendU32 (index, 1); index.insert (index.end(), r.keys[i].begin(), r.keys[i].end()); }
            r.objects[i].emit (node, r.name + "{" + juce::String::toHexString ((int) i) + "}");
        }
        node.addStream (r.name + " index", index.data(), index.size());
    }
    for (const auto& s : streams) node.addStream (s.name, s.data);
    return node;
}

//==============================================================================
static bool readNode (juce::MemoryInputStream& in, cfb::Node& parent)
{
    const auto kind = in.readByte();
    const auto nameLen = (uint16_t) in.readShort();
    juce::MemoryBlock nameBytes ((size_t) nameLen);
    in.read (nameBytes.getData(), nameLen);
    const auto name = juce::String::fromUTF8 ((const char*) nameBytes.getData(), nameLen);
    if (kind == 1)
    {
        cfb::Clsid c {}; in.read (c.data(), 16);
        auto& node = parent.addStorage (name, c);
        const auto count = (uint32_t) in.readInt();
        for (uint32_t i = 0; i < count; ++i) if (! readNode (in, node)) return false;
        return true;
    }
    if (kind == 2)
    {
        const auto size = (uint32_t) in.readInt();
        juce::MemoryBlock data ((size_t) size);
        if (size > 0 && in.read (data.getData(), (int) size) != (int) size) return false;
        parent.addStream (name, data);
        return true;
    }
    return false;
}

bool addStandardDictionaries (cfb::Node& root, cfb::Node& headerStorage, juce::String& error)
{
    juce::MemoryInputStream gz (dictionaryGz, dictionaryGzSize, false);
    juce::GZIPDecompressorInputStream in (&gz, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock raw;
    in.readIntoMemoryBlock (raw);
    juce::MemoryInputStream tree (raw, false);
    if (! readNode (tree, root)) { error = "Bad embedded MetaDictionary"; return false; }
    if (! readNode (tree, headerStorage)) { error = "Bad embedded Dictionary"; return false; }
    root.addStream ("referenced properties", referencedProperties, referencedPropertiesSize);
    return true;
}

} // namespace beatmaker::persistence::aaf
