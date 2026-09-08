// SessionChooser: the Open Session file browser. A Beat Maker session is a
// folder bundle (Name.bmk), which a plain file browser would step into on a
// double-click or Return. This browser treats a bundle as the thing being
// chosen: double-click, Return and the Open button all open it, while other
// folders still navigate. The non-native JUCE browser is used on purpose so
// the same behaviour holds on every desktop.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../persistence/SessionFile.h"
#include <functional>
#include <memory>

namespace beatmaker::ui
{

class SessionBrowser final : public juce::FileBrowserComponent
{
public:
    SessionBrowser (int flags, const juce::File& start, const juce::FileFilter* filter)
        : juce::FileBrowserComponent (flags, start, filter, nullptr) {}

    std::function<void (const juce::File&)> onBundleChosen;

    // A double-click, Return in the file list or Return in the name box all land here
    void fileDoubleClicked (const juce::File& f) override
    {
        if (persistence::SessionFile::isSessionBundle (f) && onBundleChosen != nullptr) { onBundleChosen (f); return; }
        juce::FileBrowserComponent::fileDoubleClicked (f);
    }

    // Puts a bundle in front of the user: its folder is shown and the bundle selected (smoke tests, "recent" shortcuts)
    void showBundle (const juce::File& bundle)
    {
        setRoot (bundle.getParentDirectory());
        setFileName (bundle.getFileName());
    }
};

class SessionChooser
{
public:
    using Callback = std::function<void (const juce::File&)>;   // an empty File when cancelled

    // Opens the browser; the callback runs once, with the chosen bundle or file. Returns the browser (owned by the
    // dialog, alive until it closes) so a caller can drive it.
    static SessionBrowser* launch (const juce::String& title, const juce::File& startDir, const juce::String& wildcard, Callback done)
    {
        closeAny();
        auto* holder = new Holder (title, startDir, wildcard, std::move (done));
        activeDialog() = &holder->dialog;
        holder->browser.onBundleChosen = [holder] (const juce::File&) { holder->dialog.exitModalState (1); };
        holder->dialog.centreWithDefaultSize (nullptr);
        holder->dialog.enterModalState (true, juce::ModalCallbackFunction::create ([holder] (int result)
        {
            const auto chosen = result != 0 ? holder->browser.getSelectedFile (0) : juce::File();
            auto cb = std::move (holder->done);
            juce::MessageManager::callAsync ([holder] { delete holder; });   // not from inside the dialog's own callback
            if (cb) cb (chosen);
        }), false);
        return &holder->browser;
    }

    // Dismisses an open chooser (shutdown, or a second chooser replacing it)
    static void closeAny() { if (auto* d = activeDialog().getComponent()) d->exitModalState (0); }

private:
    static juce::Component::SafePointer<juce::FileChooserDialogBox>& activeDialog() { static juce::Component::SafePointer<juce::FileChooserDialogBox> d; return d; }

    struct Holder
    {
        Holder (const juce::String& title, const juce::File& startDir, const juce::String& wildcard, Callback cb)
            : filter (wildcard, "*", title),
              browser (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories, startDir, &filter),
              dialog (title, {}, browser, false, browser.findColour (juce::AlertWindow::backgroundColourId), nullptr),
              done (std::move (cb)) {}
        juce::WildcardFileFilter filter;
        SessionBrowser browser;
        juce::FileChooserDialogBox dialog;
        Callback done;
    };
};

} // namespace beatmaker::ui
