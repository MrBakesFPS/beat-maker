// LoopBrowser: the GarageBand-style Library panel. Search and filter the
// loop library, click a loop to audition it (looping), drag it onto a track
// or double-click to add it at the playhead.
#pragma once

#include "../shared/Theme.h"
#include <LoopLibrary.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class LoopBrowser final : public juce::Component,
                          private juce::ListBoxModel,
                          private persistence::LoopLibrary::Listener
{
public:
    static constexpr const char* dragPrefix = "loop:";   // drag description = dragPrefix + full path

    explicit LoopBrowser (persistence::LoopLibrary& library);
    ~LoopBrowser() override;

    std::function<void (const persistence::LoopInfo*)> onPreview;          // nullptr = stop
    std::function<void (const persistence::LoopInfo&)> onAddAtPlayhead;    // double-click
    std::function<void()> onAddFolder;
    std::function<void()> onClose;   // the Close button and Escape (when the browser is a window of its own)

    void setSessionBpm (double bpm) { sessionBpm = bpm; list.repaint(); }
    void stopPreview();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // ListBoxModel
    int getNumRows() override { return (int) filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getTooltipForRow (int row) override;
    void backgroundClicked (const juce::MouseEvent&) override { stopPreview(); }

    void loopLibraryChanged() override { refilter(); }
    void refilter();

    persistence::LoopLibrary& library;
    std::vector<const persistence::LoopInfo*> filtered;

    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> categoryButtons;
    int categoryFilter = -1;   // -1 = all, else LoopInfo::Category
    juce::ListBox list;
    juce::TextButton addFolderButton { "+ Folder" }, rescanButton { "Rescan" }, closeButton { "Close" };
    juce::Label statusLabel;

    const persistence::LoopInfo* previewing = nullptr;
    double sessionBpm = 120.0;
};

} // namespace beatmaker::ui
