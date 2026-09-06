// ShortcutsWindow: every command with its key, filterable.
#pragma once

#include "../shared/Theme.h"
#include "../shared/CommandRegistry.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace beatmaker::ui
{

class ShortcutsWindow final : public juce::Component,
                              private juce::ListBoxModel,
                              private juce::TextEditor::Listener
{
public:
    explicit ShortcutsWindow (const CommandRegistry&);
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 560, preferredHeight = 520;

private:
    void refresh();
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void textEditorTextChanged (juce::TextEditor&) override { refresh(); }

    const CommandRegistry& registry;
    std::vector<const AppCommand*> rows;
    juce::TextEditor search;
    juce::ListBox list;
};

} // namespace beatmaker::ui
