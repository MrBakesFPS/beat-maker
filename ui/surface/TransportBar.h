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

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateDisplay();

    engine::Transport& transport;

    juce::ShapeButton recordButton { "Record", theme::record, theme::record.brighter(), theme::record.darker() };
    juce::ShapeButton playButton   { "Play",   theme::play,   theme::play.brighter(),   theme::play.darker() };
    juce::ShapeButton stopButton   { "Stop",   theme::stop,   theme::stop.brighter(),   theme::stop.darker() };
    juce::ShapeButton rtzButton    { "Return to Start", theme::stop, theme::stop.brighter(), theme::stop.darker() };

    juce::Label barsBeatsLcd, timeLcd, tempoLcd;
    juce::TextButton openButton { "Open Audio File..." };
};

} // namespace beatmaker::ui
