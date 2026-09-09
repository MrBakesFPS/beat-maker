// UserPresets: the user's own instrument presets, one JSON file each under
// ~/Music/Beat Maker/Presets/<Instrument>/. Loaded at start-up and registered
// with engine::Instrument, so they list in every Sound menu and chooser.
#pragma once

#include <dsp/Instrument.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace beatmaker::persistence
{

class UserPresets
{
public:
    static juce::File defaultFolder() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker").getChildFile ("Presets"); }
    static juce::File folderFor (const juce::File& root, engine::InstrumentType type) { return root.getChildFile (juce::File::createLegalFileName (engine::Instrument::typeName (type))); }
    static juce::File fileFor (const juce::File& root, engine::InstrumentType type, const juce::String& name) { return folderFor (root, type).getChildFile (juce::File::createLegalFileName (name) + ".preset.json"); }

    static juce::var toVar (engine::InstrumentType type, const engine::InstrumentParams& p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("instrument", juce::String (engine::Instrument::typeName (type)));
        o->setProperty ("name", p.presetName);
        juce::Array<juce::var> values;
        for (size_t i = 0; i < engine::Instrument::paramInfo (type).size() && i < p.values.size(); ++i) values.add ((double) p.values[i]);
        o->setProperty ("values", values);
        if (type == engine::InstrumentType::sampler)
        {
            o->setProperty ("samplePath", p.samplePath); o->setProperty ("sampleName", p.sampleName); o->setProperty ("rootNote", p.rootNote);
        }
        return juce::var (o);
    }

    static bool fromVar (const juce::var& v, engine::Instrument::UserPreset& out)
    {
        if (! v.isObject()) return false;
        out.type = engine::Instrument::typeNamed (v.getProperty ("instrument", "").toString());
        if (out.type == engine::InstrumentType::none) return false;
        out.params = engine::Instrument::defaultParams (out.type);
        out.params.presetName = v.getProperty ("name", "").toString().trim();
        if (out.params.presetName.isEmpty()) return false;
        const auto values = v.getProperty ("values", juce::var());
        const auto& info = engine::Instrument::paramInfo (out.type);
        for (int i = 0; i < juce::jmin ((int) info.size(), values.size()); ++i)
            out.params.values[(size_t) i] = juce::jlimit (info[(size_t) i].min, info[(size_t) i].max, (float) (double) values[i]);
        if (out.type == engine::InstrumentType::sampler)
        {
            out.params.samplePath = v.getProperty ("samplePath", "").toString(); out.params.sampleName = v.getProperty ("sampleName", "").toString();
            out.params.rootNote = juce::jlimit (0, 127, (int) v.getProperty ("rootNote", 60));
        }
        return true;
    }

    static juce::String save (const juce::File& root, engine::InstrumentType type, const engine::InstrumentParams& p)
    {
        if (type == engine::InstrumentType::none) return "No instrument";
        if (p.presetName.trim().isEmpty()) return "A preset needs a name";
        folderFor (root, type).createDirectory();
        const auto file = fileFor (root, type, p.presetName);
        return file.replaceWithText (juce::JSON::toString (toVar (type, p))) ? juce::String() : "Could not write " + file.getFullPathName();
    }

    static bool remove (const juce::File& root, engine::InstrumentType type, const juce::String& name) { return fileFor (root, type, name).deleteFile(); }

    static std::vector<engine::Instrument::UserPreset> load (const juce::File& root)
    {
        std::vector<engine::Instrument::UserPreset> out;
        for (const auto& f : root.findChildFiles (juce::File::findFiles, true, "*.preset.json"))
        {
            engine::Instrument::UserPreset u;
            if (fromVar (juce::JSON::parse (f), u)) out.push_back (u);
        }
        std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.params.presetName.compareIgnoreCase (b.params.presetName) < 0; });
        return out;
    }
};

} // namespace beatmaker::persistence
