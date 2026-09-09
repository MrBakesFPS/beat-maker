// UserKits: the user's own drum kits (DrumKitFactory::CustomKit), one JSON
// file each in ~/Music/Beat Maker/Kits. Loaded at start-up and registered
// with the factory, so they list and resolve like the bundled kits.
#pragma once

#include <dsp/DrumKitFactory.h>
#include <juce_core/juce_core.h>

namespace beatmaker::persistence
{

class UserKits
{
public:
    static juce::File defaultFolder() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Kits"); }

    static juce::var toVar (const engine::DrumKitFactory::CustomKit& kit)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", kit.name);
        juce::Array<juce::var> pads;
        for (const auto& p : kit.pads) { auto* po = new juce::DynamicObject(); po->setProperty ("kit", p.kit); po->setProperty ("pad", p.pad); pads.add (juce::var (po)); }
        o->setProperty ("pads", pads);
        return juce::var (o);
    }

    static bool fromVar (const juce::var& v, engine::DrumKitFactory::CustomKit& kit)
    {
        if (! v.isObject()) return false;
        kit.name = v.getProperty ("name", "").toString().trim();
        const auto pads = v.getProperty ("pads", juce::var());
        if (kit.name.isEmpty() || ! pads.isArray()) return false;
        for (int i = 0; i < engine::DrumKit::numPads; ++i)
        {
            const auto p = i < pads.size() ? pads[i] : juce::var();
            kit.pads[(size_t) i].kit = p.getProperty ("kit", engine::DrumKitFactory::defaultKitName()).toString();
            kit.pads[(size_t) i].pad = juce::jlimit (0, engine::DrumKit::numPads - 1, (int) p.getProperty ("pad", i));
        }
        return true;
    }

    static juce::File fileFor (const juce::File& folder, const juce::String& name) { return folder.getChildFile (juce::File::createLegalFileName (name) + ".kit.json"); }

    static juce::String save (const juce::File& folder, const engine::DrumKitFactory::CustomKit& kit)
    {
        if (kit.name.trim().isEmpty()) return "A kit needs a name";
        folder.createDirectory();
        const auto file = fileFor (folder, kit.name);
        return file.replaceWithText (juce::JSON::toString (toVar (kit))) ? juce::String() : "Could not write " + file.getFullPathName();
    }

    static bool remove (const juce::File& folder, const juce::String& name) { return fileFor (folder, name).deleteFile(); }

    static std::vector<engine::DrumKitFactory::CustomKit> load (const juce::File& folder)
    {
        std::vector<engine::DrumKitFactory::CustomKit> kits;
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.kit.json"))
        {
            engine::DrumKitFactory::CustomKit kit;
            if (fromVar (juce::JSON::parse (f), kit)) kits.push_back (kit);
        }
        std::sort (kits.begin(), kits.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        return kits;
    }
};

} // namespace beatmaker::persistence
