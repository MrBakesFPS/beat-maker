// NewSessionDialog: "New Session from Template" (Ctrl+N): built-in
// starting points plus the user's saved templates.
#pragma once

#include "../shared/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class NewSessionDialog final : public juce::Component,
                               private juce::ListBoxModel
{
public:
    struct Choice { juce::String name, description; juce::File templateFile; };   // no file = built-in recipe
    NewSessionDialog (std::vector<Choice> choices, juce::String defaultName);

    std::function<void (const Choice&, const juce::String& sessionName)> onCreate;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 480, preferredHeight = 400;

private:
    int getNumRows() override { return (int) choices.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { create(); }
    void create();

    std::vector<Choice> choices;
    juce::ListBox list;
    juce::Label nameLabel { {}, "Session name" };
    juce::TextEditor nameEditor;
    juce::TextButton createButton { "Create" }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
