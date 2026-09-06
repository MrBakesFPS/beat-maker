// CfbWriter: writes a Microsoft Compound File Binary (OLE2 structured
// storage) container, the file format AAF is built on. Version 4 with
// 4096-byte sectors, a mini stream for small streams, and DIFAT sectors when
// the file is large enough to need them.
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>

namespace beatmaker::persistence::cfb
{

using Clsid = std::array<unsigned char, 16>;   // GUID in its stored (little-endian) byte order

struct Node
{
    juce::String name;          // at most 31 UTF-16 units
    bool isStorage = true;
    Clsid clsid {};
    juce::MemoryBlock data;     // streams only
    std::vector<std::unique_ptr<Node>> children;

    Node& addStorage (const juce::String& name, const Clsid& clsid);
    Node& addStream (const juce::String& name, const void* data, size_t size);
    Node& addStream (const juce::String& name, const juce::MemoryBlock& data) { return addStream (name, data.getData(), data.getSize()); }
    const Node* find (const juce::String& name) const;
};

// Writes the tree with `root` as the root storage. `headerClsid` goes in the
// file header (AAF puts its file-kind signature there).
bool write (const Node& root, const Clsid& headerClsid, juce::OutputStream& out, juce::String& error);

// Directory ordering used by the format: shorter names first, then a
// case-insensitive comparison of UTF-16 units.
int compareNames (const juce::String& a, const juce::String& b);

} // namespace beatmaker::persistence::cfb
