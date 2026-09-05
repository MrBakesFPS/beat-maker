#include "TransportBar.h"

namespace beatmaker::ui
{

TransportBar::TransportBar (engine::Transport& t) : transport (t)
{
    // Vector icons so the look never depends on installed fonts.
    juce::Path circle, triangle, square, rtz;
    circle.addEllipse (0.0f, 0.0f, 1.0f, 1.0f);
    triangle.addTriangle (0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 1.0f);
    square.addRectangle (0.0f, 0.0f, 1.0f, 1.0f);
    rtz.addRectangle (0.0f, 0.0f, 0.18f, 1.0f);
    rtz.addTriangle (1.0f, 0.0f, 0.22f, 0.5f, 1.0f, 1.0f);

    recordButton.setShape (circle,   true, true, false);
    playButton.setShape   (triangle, true, true, false);
    stopButton.setShape   (square,   true, true, false);
    rtzButton.setShape    (rtz,      true, true, false);

    for (auto* b : { &rtzButton, &recordButton, &playButton, &stopButton })
    {
        b->setOutline (juce::Colours::transparentBlack, 0.0f);
        addAndMakeVisible (b);
    }

    recordButton.setEnabled (false); // recording arrives in Phase 1
    recordButton.setTooltip ("Record (coming in Phase 1)");
    playButton.setTooltip ("Play (Space)");
    stopButton.setTooltip ("Stop (Space)");
    rtzButton.setTooltip ("Return to start (Return)");

    playButton.onClick = [this] { transport.play(); };
    stopButton.onClick = [this]
    {
        if (transport.isPlaying()) transport.stop();
        else                       transport.returnToStart(); // second press = return to zero
        updateDisplay();
    };
    rtzButton.onClick = [this] { transport.returnToStart(); updateDisplay(); };

    for (auto* l : { &barsBeatsLcd, &timeLcd, &tempoLcd })
    {
        addAndMakeVisible (l);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::backgroundColourId, theme::lcdBackground);
        l->setColour (juce::Label::textColourId, theme::accent);
    }
    barsBeatsLcd.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    timeLcd.setFont      (juce::FontOptions (18.0f));
    tempoLcd.setFont     (juce::FontOptions (16.0f, juce::Font::bold));

    addAndMakeVisible (openButton);
    openButton.onClick = [this] { if (onOpenFile) onOpenFile(); };

    updateDisplay();
    startTimerHz (30);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::panelDark);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    const int size = area.getHeight();

    rtzButton.setBounds    (area.removeFromLeft (size).reduced (9)); area.removeFromLeft (6);
    recordButton.setBounds (area.removeFromLeft (size).reduced (8)); area.removeFromLeft (6);
    playButton.setBounds   (area.removeFromLeft (size).reduced (6)); area.removeFromLeft (6);
    stopButton.setBounds   (area.removeFromLeft (size).reduced (8)); area.removeFromLeft (16);

    openButton.setBounds (area.removeFromRight (150).reduced (0, 6));
    area.removeFromRight (16);

    barsBeatsLcd.setBounds (area.removeFromLeft (200)); area.removeFromLeft (4);
    timeLcd.setBounds      (area.removeFromLeft (130)); area.removeFromLeft (4);
    tempoLcd.setBounds     (area.removeFromLeft (150));
}

void TransportBar::timerCallback()
{
    updateDisplay();
    playButton.setColours (transport.isPlaying() ? theme::play.brighter (0.6f) : theme::play,
                           theme::play.brighter(), theme::play.darker());
    playButton.repaint();
}

void TransportBar::updateDisplay()
{
    const auto bb = transport.getBarBeat();
    barsBeatsLcd.setText (juce::String::formatted ("%03d | %d | %03d", bb.bar, bb.beat, bb.tick), juce::dontSendNotification);

    const double secs = transport.getPositionSeconds();
    const int mins = (int) (secs / 60.0);
    const double remSecs = secs - mins * 60.0;
    timeLcd.setText (juce::String::formatted ("%02d:%06.3f", mins, remSecs), juce::dontSendNotification);

    tempoLcd.setText (juce::String (transport.getBpm(), 1) + " BPM   " + juce::String (transport.getBeatsPerBar()) + "/4",
                      juce::dontSendNotification);
}

} // namespace beatmaker::ui
