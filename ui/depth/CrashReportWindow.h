// CrashReportWindow: shown on the launch after a crash. Explains what was
// saved, previews the report, and offers to copy it, open the folder or
// recover the session's newest auto-backup.
#pragma once

#include "../shared/Theme.h"
#include "../shared/CrashReporter.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class CrashReportWindow final : public juce::Component
{
public:
    CrashReportWindow (const juce::File& report, const juce::File& autosave, const juce::File& originalSession);
    std::function<void (const juce::File& autosave, const juce::File& original)> onRecover;
    std::function<void()> onClose;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 620, preferredHeight = 520;

private:
    juce::File report, autosave, original;
    juce::TextEditor preview;
    juce::TextButton copyButton { "Copy Report" }, folderButton { "Show Reports Folder" }, recoverButton { "Recover Auto-backup" }, closeButton { "Continue" };
    juce::Label note;
};

} // namespace beatmaker::ui
