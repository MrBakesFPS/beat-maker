#include "SyncDialog.h"

namespace beatmaker::ui
{

SyncDialog::SyncDialog (engine::MidiSyncController& s) : sync (s)
{
    using Mode = engine::MidiSyncController::Mode;
    for (auto* l : { &modeLabel, &rateLabel, &startLabel, &outLabel, &inLabel }) { l->setColour (juce::Label::textColourId, theme::textDim); addAndMakeVisible (l); }
    addAndMakeVisible (modeBox);
    int id = 1;
    for (auto m : { Mode::off, Mode::sendClock, Mode::sendMtc, Mode::chaseClock, Mode::chaseMtc }) modeBox.addItem (engine::MidiSyncController::modeName (m), id++);
    modeBox.setSelectedId ((int) sync.getMode() + 1, juce::dontSendNotification);
    addAndMakeVisible (rateBox);
    rateBox.addItem ("24 fps", 1); rateBox.addItem ("25 fps", 2); rateBox.addItem ("29.97 fps drop", 3); rateBox.addItem ("30 fps", 4);
    rateBox.setSelectedId ((int) sync.getFrameRate() + 1, juce::dontSendNotification);
    addAndMakeVisible (startEditor);
    startEditor.setText (engine::Timecode::fromSeconds (sync.getStartOffsetSeconds(), sync.getFrameRate()).toString(), juce::dontSendNotification);
    startEditor.setTooltip ("Timecode at the session start, hh:mm:ss:ff");
    addAndMakeVisible (outBox);
    id = 1;
    for (const auto& n : engine::MidiSyncController::outputDeviceNames()) outBox.addItem (n, id++);
    outBox.setText (sync.getOutputDeviceName().isEmpty() ? juce::String ("(none)") : sync.getOutputDeviceName(), juce::dontSendNotification);
    addAndMakeVisible (inBox);
    id = 1;
    for (const auto& n : engine::MidiSyncController::inputDeviceNames()) inBox.addItem (n, id++);
    inBox.setText (sync.getInputDeviceName().isEmpty() ? juce::String ("(none)") : sync.getInputDeviceName(), juce::dontSendNotification);
    for (auto* b : { &modeBox, &rateBox, &outBox, &inBox }) b->onChange = [this] { apply(); };
    startEditor.onReturnKey = [this] { apply(); };
    startEditor.onFocusLost = [this] { apply(); };
    addAndMakeVisible (status);
    status.setColour (juce::Label::textColourId, theme::accent);
    status.setFont (juce::FontOptions (12.0f));
    setSize (preferredWidth, preferredHeight);
    startTimerHz (5);
}

void SyncDialog::apply()
{
    using Mode = engine::MidiSyncController::Mode;
    if (rateBox.getSelectedId() > 0) sync.setFrameRate ((engine::MtcFrameRate) (rateBox.getSelectedId() - 1));
    const auto parts = juce::StringArray::fromTokens (startEditor.getText(), ":", {});
    if (parts.size() == 4)
    {
        engine::Timecode tc { parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getIntValue(), parts[3].getIntValue() };
        sync.setStartOffsetSeconds (tc.toSeconds (sync.getFrameRate()));
    }
    if (outBox.getSelectedId() > 0 && outBox.getText() != sync.getOutputDeviceName()) sync.setOutputDevice (outBox.getText());
    if (inBox.getSelectedId() > 0 && inBox.getText() != sync.getInputDeviceName()) sync.setInputDevice (inBox.getText());
    if (modeBox.getSelectedId() > 0) sync.setMode ((Mode) (modeBox.getSelectedId() - 1));
    if (onChanged) onChanged();
    timerCallback();
}

void SyncDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Synchronization", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Send clock or timecode to other gear, or chase it. \"Beat Maker Sync\" is a virtual port other apps can connect to.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void SyncDialog::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (40);
    auto row = [&] (juce::Label& l, juce::Component& c) { auto r = area.removeFromTop (28); l.setBounds (r.removeFromLeft (120)); c.setBounds (r); area.removeFromTop (6); };
    row (modeLabel, modeBox); row (rateLabel, rateBox); row (startLabel, startEditor); row (outLabel, outBox); row (inLabel, inBox);
    status.setBounds (area.removeFromTop (24));
}

} // namespace beatmaker::ui
