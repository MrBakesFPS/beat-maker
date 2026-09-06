// CommandPalette: type to find any command or preference, Enter runs it
// (Ctrl+Shift+P / Ctrl+K).
#pragma once

#include "../shared/Theme.h"
#include "../shared/CommandRegistry.h"
#include "../shared/Preferences.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class CommandPalette final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::TextEditor::Listener
{
public:
    CommandPalette (const CommandRegistry&, const Preferences&);

    std::function<void()> onDismiss;
    std::function<void (const juce::String& prefId)> onOpenPreference;   // a preference row was chosen

    void setQuery (const juce::String&);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override { if (isVisible()) input.grabKeyboardFocus(); }
    static constexpr int preferredWidth = 560, preferredHeight = 420;

private:
    struct Row { const AppCommand* command = nullptr; const PrefDef* pref = nullptr; };
    void refresh();
    void choose (int row);
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { choose (row); }
    void textEditorTextChanged (juce::TextEditor&) override { refresh(); }
    void textEditorReturnKeyPressed (juce::TextEditor&) override { choose (list.getSelectedRow()); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { if (onDismiss) onDismiss(); }

    const CommandRegistry& registry;
    const Preferences& prefs;
    juce::TextEditor input;
    juce::ListBox list;
    std::vector<Row> rows;
};

} // namespace beatmaker::ui
