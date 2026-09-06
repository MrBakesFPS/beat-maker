// CommandRegistry: every app command in one place: name, category, key
// shortcut, optional single-letter "Commands Keyboard Focus" key, action and
// enabled predicate. The command palette searches it and keyPressed dispatches
// through it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CrashReporter.h"
#include <functional>
#include <vector>

namespace beatmaker::ui
{

struct AppCommand
{
    juce::String id, name, category;
    juce::KeyPress shortcut;                  // may be invalid
    juce::juce_wchar focusKey = 0;            // a-z in Commands Focus mode
    std::function<void()> run;
    std::function<bool()> enabled;            // null = always
    juce::String shortcutText() const { return shortcut.isValid() ? shortcut.getTextDescriptionWithIcons() : juce::String(); }
    bool isEnabled() const { return enabled == nullptr || enabled(); }
};

class CommandRegistry
{
public:
    void add (AppCommand c) { commands.push_back (std::move (c)); }
    const std::vector<AppCommand>& all() const noexcept { return commands; }
    const AppCommand* find (const juce::String& id) const { for (const auto& c : commands) if (c.id == id) return &c; return nullptr; }

    bool run (const juce::String& id) const
    {
        CrashReporter::get().addBreadcrumb ("command " + id);
        if (const auto* c = find (id); c != nullptr && c->isEnabled() && c->run) { c->run(); return true; }
        return false;
    }

    // Dispatch a key press: shortcut match, then (in focus mode) the a-z key.
    bool handleKey (const juce::KeyPress& key, bool focusMode) const
    {
        // In Commands Focus, a bare letter goes to its focus command before any plain-letter shortcut.
        if (focusMode && ! key.getModifiers().isAnyModifierKeyDown())
            for (const auto& c : commands)
                if (c.focusKey != 0 && key.getTextCharacter() == c.focusKey && c.isEnabled() && c.run) { c.run(); return true; }
        for (const auto& c : commands)
            if (c.shortcut.isValid() && c.shortcut == key && c.isEnabled() && c.run) { c.run(); return true; }
        return false;
    }

    // Fuzzy search: every query word must appear in name/category (subsequence match per word), ranked by tightness.
    std::vector<const AppCommand*> search (const juce::String& query) const
    {
        std::vector<std::pair<int, const AppCommand*>> scored;
        const auto words = juce::StringArray::fromTokens (query.toLowerCase().trim(), " ", {});
        for (const auto& c : commands)
        {
            const auto text = (c.category + " " + c.name).toLowerCase();
            int score = 0; bool ok = true;
            for (const auto& w : words)
            {
                if (w.isEmpty()) continue;
                if (text.contains (w)) { score += 100 - juce::jmin (90, text.indexOf (w)); continue; }
                int pos = 0, matched = 0;
                for (auto ch : w) { const int at = text.indexOfChar (pos, ch); if (at < 0) break; pos = at + 1; ++matched; }
                if (matched != w.length()) { ok = false; break; }
                score += 10;
            }
            if (ok) scored.emplace_back (score, &c);
        }
        std::stable_sort (scored.begin(), scored.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<const AppCommand*> out;
        for (const auto& s : scored) out.push_back (s.second);
        return out;
    }

private:
    std::vector<AppCommand> commands;
};

} // namespace beatmaker::ui
