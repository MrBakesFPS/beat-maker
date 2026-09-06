// PreferencesWindow: Pro Tools-depth settings with categories on the left,
// a search box that filters every category, and a control per setting.
#pragma once

#include "../shared/Theme.h"
#include "../shared/Preferences.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class PreferencesWindow final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::TextEditor::Listener
{
public:
    explicit PreferencesWindow (Preferences&);
    ~PreferencesWindow() override;

    void showSetting (const juce::String& prefId);   // search for it and scroll to it
    std::function<void (const juce::File&)> chooseFolder;   // async folder picker (set by the owner)

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 760, preferredHeight = 520;

private:
    class Row;
    void rebuildRows();
    int getNumRows() override { return prefs.categories().size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override { rebuildRows(); }
    void textEditorTextChanged (juce::TextEditor&) override { rebuildRows(); }

    Preferences& prefs;
    int listenerToken = -1;
    juce::TextEditor search;
    juce::ListBox categories;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Row> rows;
    juce::TextButton resetButton { "Reset category to defaults" };
    juce::Label countLabel;
};

} // namespace beatmaker::ui
