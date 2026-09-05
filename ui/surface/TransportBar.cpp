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

    recordButton.setTooltip ("Record (R): arm a track with its R button first");
    recordButton.onClick = [this] { if (onRecord) onRecord(); };
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
    tempoLcd.setFont     (juce::FontOptions (15.0f, juce::Font::bold));

    addAndMakeVisible (openButton);
    openButton.setTooltip ("Import an audio file (Ctrl+O)");
    openButton.onClick = [this] { if (onOpenFile) onOpenFile(); };

    addAndMakeVisible (bounceButton);
    bounceButton.setTooltip ("Bounce the arrangement to an audio file (Ctrl+B)");
    bounceButton.onClick = [this] { if (onBounce) onBounce(); };

    addAndMakeVisible (cycleButton);
    cycleButton.setClickingTogglesState (true);
    cycleButton.setToggleState (transport.isLoopEnabled(), juce::dontSendNotification);
    cycleButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.4f));
    cycleButton.setTooltip ("Cycle: loop the whole arrangement (C)");
    cycleButton.onClick = [this] { transport.setLoopEnabled (cycleButton.getToggleState()); };

    addAndMakeVisible (libraryButton);
    libraryButton.setClickingTogglesState (true);
    libraryButton.setToggleState (true, juce::dontSendNotification);
    libraryButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.4f));
    libraryButton.setTooltip ("Show or hide the loop library (L)");
    libraryButton.onClick = [this] { if (onLibraryToggled) onLibraryToggled (libraryButton.getToggleState()); };

    addAndMakeVisible (controlsButton);
    controlsButton.setClickingTogglesState (true);
    controlsButton.setToggleState (true, juce::dontSendNotification);
    controlsButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.4f));
    controlsButton.setTooltip ("Show or hide Smart Controls (B)");
    controlsButton.onClick = [this] { if (onControlsToggled) onControlsToggled (controlsButton.getToggleState()); };

    addAndMakeVisible (editorButton);
    editorButton.setClickingTogglesState (true);
    editorButton.setToggleState (true, juce::dontSendNotification);
    editorButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.4f));
    editorButton.setTooltip ("Show or hide the step sequencer (E)");
    editorButton.onClick = [this] { if (onEditorToggled) onEditorToggled (editorButton.getToggleState()); };

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
    stopButton.setBounds   (area.removeFromLeft (size).reduced (8)); area.removeFromLeft (10);
    cycleButton.setBounds  (area.removeFromLeft (60).reduced (0, 8)); area.removeFromLeft (16);

    bounceButton.setBounds (area.removeFromRight (90).reduced (0, 6));
    area.removeFromRight (8);
    openButton.setBounds (area.removeFromRight (80).reduced (0, 6));
    area.removeFromRight (8);
    editorButton.setBounds (area.removeFromRight (64).reduced (0, 8));
    area.removeFromRight (6);
    controlsButton.setBounds (area.removeFromRight (74).reduced (0, 8));
    area.removeFromRight (6);
    libraryButton.setBounds (area.removeFromRight (68).reduced (0, 8));
    area.removeFromRight (16);

    // LCDs share whatever is left, shrinking proportionally on narrow windows.
    const int ideal = 200 + 4 + 130 + 4 + 170;
    const double scale = juce::jmin (1.0, (double) area.getWidth() / ideal);
    barsBeatsLcd.setBounds (area.removeFromLeft (juce::roundToInt (200 * scale))); area.removeFromLeft (4);
    timeLcd.setBounds      (area.removeFromLeft (juce::roundToInt (130 * scale))); area.removeFromLeft (4);
    tempoLcd.setBounds     (area.removeFromLeft (juce::roundToInt (170 * scale)));
    tempoLcd.setVisible (scale > 0.55);
}

void TransportBar::timerCallback()
{
    updateDisplay();
    cycleButton.setToggleState (transport.isLoopEnabled(), juce::dontSendNotification);
    playButton.setColours (transport.isPlaying() ? theme::play.brighter (0.6f) : theme::play,
                           theme::play.brighter(), theme::play.darker());
    playButton.repaint();

    // Blink while recording, steady when idle.
    if (transport.isRecordEnabled())
    {
        if (++blinkCounter % 12 == 0) blinkOn = ! blinkOn;
        recordButton.setColours (blinkOn ? theme::record.brighter (0.8f) : theme::record,
                                 theme::record.brighter(), theme::record.darker());
    }
    else
    {
        blinkOn = false;
        recordButton.setColours (theme::record, theme::record.brighter(), theme::record.darker());
    }
    recordButton.repaint();
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
