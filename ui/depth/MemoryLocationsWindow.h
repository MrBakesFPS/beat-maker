// MemoryLocationsWindow: Pro Tools-style list of memory locations and
// sections (Ctrl+5). Click a row to recall it.
#pragma once

#include "../shared/Theme.h"
#include <Session.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class MemoryLocationsWindow final : public juce::Component,
                                    private juce::ListBoxModel,
                                    private model::Session::Listener
{
public:
    explicit MemoryLocationsWindow (model::Session&);
    ~MemoryLocationsWindow() override;

    std::function<void (int markerId)> onRecall;
    std::function<void (int markerId)> onRename;
    std::function<void()> onAdd;

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 440, preferredHeight = 360;

private:
    int getNumRows() override { return (int) session.getMarkers().size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int row) override;
    void sessionChanged (model::Session&) override { list.updateContent(); list.repaint(); }

    model::Session& session;
    juce::ListBox list;
    juce::TextButton addButton { "New Location" }, deleteButton { "Delete" };
};

} // namespace beatmaker::ui
