// Preferences: a searchable registry of every deep setting, persisted to a
// properties file. Each entry has a category, a description and a type so
// the Preferences window can build itself and the command palette can find
// them. Listeners apply changes live.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <memory>
#include <vector>

namespace beatmaker::ui
{

struct PrefDef
{
    enum class Type { toggle, number, choice, text, folder };
    juce::String id, category, name, description;
    Type type = Type::toggle;
    juce::var defaultValue;
    double min = 0.0, max = 1.0, step = 1.0;   // number
    juce::String unit;                          // number
    juce::StringArray choices;                  // choice (value = index)
};

class Preferences
{
public:
    // `file` empty = in-memory only (tests).
    explicit Preferences (juce::File file = {});
    ~Preferences();

    const std::vector<PrefDef>& all() const noexcept { return defs; }
    juce::StringArray categories() const;
    const PrefDef* def (const juce::String& id) const;

    juce::var get (const juce::String& id) const;
    bool getBool (const juce::String& id) const { return (bool) get (id); }
    double getDouble (const juce::String& id) const { return (double) get (id); }
    int getInt (const juce::String& id) const { return (int) get (id); }
    juce::String getString (const juce::String& id) const { return get (id).toString(); }
    juce::String choiceName (const juce::String& id) const;   // current choice's label

    void set (const juce::String& id, const juce::var& value);
    void resetToDefault (const juce::String& id);
    void resetAll();
    bool isDefault (const juce::String& id) const { return get (id) == (def (id) != nullptr ? def (id)->defaultValue : juce::var()); }

    // Search across names, descriptions and categories (case-insensitive, every word must match).
    std::vector<const PrefDef*> search (const juce::String& query) const;

    // Listeners: called on the message thread with the changed id ("" = everything).
    int addListener (std::function<void (const juce::String& id)>);
    void removeListener (int token);

    void save();
    static std::vector<PrefDef> definitions();

private:
    std::vector<PrefDef> defs;
    std::unique_ptr<juce::PropertiesFile> file;
    juce::NamedValueSet values;
    std::vector<std::pair<int, std::function<void (const juce::String&)>>> listeners;
    int nextToken = 1;
    void notify (const juce::String& id);
};

} // namespace beatmaker::ui
