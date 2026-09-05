// GarageBand-style transport strip bound to the engine Transport.
#pragma once

#include "../shared/Theme.h"
#include <transport/Transport.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (engine::Transport& transport);

    std::function<void()> onOpenFile;
    std::function<void()> onRecord;
    std::function<void()> onBounce;
    std::function<void (bool)> onEditorToggled;
    std::function<void (bool)> onLibraryToggled;
    std::function<void (bool)> onControlsToggled;
    void setControlsVisible (bool visible) { controlsButton.setToggleState (visible, juce::dontSendNotification); }

    void setEditorVisible (bool visible)  { editorButton.setToggleState (visible, juce::dontSendNotification); }
    void setLibraryVisible (bool visible) { libraryButton.setToggleState (visible, juce::dontSendNotification); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateDisplay();

    engine::Transport& transport;
    bool blinkOn = false;
    int blinkCounter = 0;

    juce::ShapeButton recordButton { "Record", theme::record, theme::record.brighter(), theme::record.darker() };
    juce::ShapeButton playButton   { "Play",   theme::play,   theme::play.brighter(),   theme::play.darker() };
    juce::ShapeButton stopButton   { "Stop",   theme::stop,   theme::stop.brighter(),   theme::stop.darker() };
    juce::ShapeButton rtzButton    { "Return to Start", theme::stop, theme::stop.brighter(), theme::stop.darker() };

    juce::Label barsBeatsLcd, timeLcd, tempoLcd;
    juce::TextButton libraryButton { "Library" };
    juce::TextButton controlsButton { "Controls" };
    juce::TextButton cycleButton { "Cycle" };
    juce::TextButton editorButton { "Editor" };
    juce::TextButton openButton { "Open..." };
    juce::TextButton bounceButton { "Bounce..." };
};

} // namespace beatmaker::ui
