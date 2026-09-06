#include "CrashReportWindow.h"

namespace beatmaker::ui
{

CrashReportWindow::CrashReportWindow (const juce::File& r, const juce::File& a, const juce::File& o) : report (r), autosave (a), original (o)
{
    addAndMakeVisible (preview);
    preview.setMultiLine (true); preview.setReadOnly (true); preview.setScrollbarsShown (true);
    preview.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
    preview.setText (CrashReporter::prettify (report.loadFileAsString()), false);
    for (auto* b : { &copyButton, &folderButton, &recoverButton, &closeButton }) addAndMakeVisible (b);
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (report.loadFileAsString()); copyButton.setButtonText ("Copied"); };
    folderButton.onClick = [this] { report.getParentDirectory().revealToUser(); };
    recoverButton.setEnabled (autosave.existsAsFile() || autosave.isDirectory());
    recoverButton.setTooltip (autosave == juce::File() ? "No auto-backup newer than the session file was found" : "Opens " + autosave.getFileName() + " in place of " + original.getFileName());
    recoverButton.onClick = [this] { if (onRecover) onRecover (autosave, original); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (note);
    note.setFont (juce::FontOptions (12.0f));
    note.setColour (juce::Label::textColourId, theme::textDim);
    juce::String text = "The report below was written as Beat Maker went down. It holds no audio, only the version, the session path, recent actions and a backtrace. "
                        "To report the problem, copy it into an issue at the project's tracker, or use Help > Report a Problem for a fuller diagnostics file.";
    if (autosave != juce::File()) text << "\n\nAn auto-backup of " << original.getFileName() << " from " << autosave.getLastModificationTime().toString (true, true, false) << " is newer than the last save.";
    note.setText (text, juce::dontSendNotification);
    setSize (preferredWidth, preferredHeight);
}

void CrashReportWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("Beat Maker quit unexpectedly last time", 16, 12, getWidth() - 32, 22, juce::Justification::centredLeft);
}

void CrashReportWindow::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (34);
    note.setBounds (area.removeFromTop (76));
    area.removeFromTop (8);
    auto buttons = area.removeFromBottom (28);
    closeButton.setBounds (buttons.removeFromRight (100)); buttons.removeFromRight (8);
    recoverButton.setBounds (buttons.removeFromRight (160)); buttons.removeFromRight (8);
    folderButton.setBounds (buttons.removeFromRight (150)); buttons.removeFromRight (8);
    copyButton.setBounds (buttons.removeFromRight (110));
    area.removeFromBottom (10);
    preview.setBounds (area);
}

} // namespace beatmaker::ui
