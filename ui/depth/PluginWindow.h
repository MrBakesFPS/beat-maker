// PluginWindow: a hosted plugin's own editor (or a generic one) in a window.
#pragma once

#include "../shared/Theme.h"
#include <PluginHost.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace beatmaker::ui
{

class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (plugins::PluginEffect& effect, std::function<void()> onClosed)
        : DocumentWindow (effect.getDescription().name, theme::panel, DocumentWindow::closeButton), closed (std::move (onClosed))
    {
        auto& instance = effect.getInstance();
        juce::AudioProcessorEditor* editor = instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (instance);
        setContentOwned (editor, true);
        setUsingNativeTitleBar (true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { if (closed) closed(); }

private:
    std::function<void()> closed;
};

} // namespace beatmaker::ui
