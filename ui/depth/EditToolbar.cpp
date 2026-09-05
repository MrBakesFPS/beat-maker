#include "EditToolbar.h"

namespace beatmaker::ui
{

EditToolbar::EditToolbar (EditSettings& s) : settings (s)
{
    using Mode = EditSettings::Mode;
    using Tool = EditSettings::Tool;

    auto makeButton = [this] (juce::OwnedArray<juce::TextButton>& list, const juce::String& name, int group,
                              const juce::String& tooltip, std::function<void()> onClick)
    {
        auto* b = list.add (new juce::TextButton (name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (group);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.45f));
        b->setTooltip (tooltip);
        b->onClick = [this, onClick] { onClick(); settings.notify(); refresh(); };
        addAndMakeVisible (b);
    };

    makeButton (modeButtons, "Shuffle", 2001, "Shuffle mode (F1): clips stay end to end", [this] { settings.mode = Mode::shuffle; });
    makeButton (modeButtons, "Slip",    2001, "Slip mode (F2): free movement",              [this] { settings.mode = Mode::slip; });
    makeButton (modeButtons, "Spot",    2001, "Spot mode (F3): click a clip to type its position", [this] { settings.mode = Mode::spot; });
    makeButton (modeButtons, "Grid",    2001, "Grid mode (F4): snap to the grid",           [this] { settings.mode = Mode::grid; });

    makeButton (toolButtons, "Zoom",   2002, "Zoomer (F5): click to zoom in, Alt-click out, drag a range", [this] { settings.tool = Tool::zoomer; });
    makeButton (toolButtons, "Trim",   2002, "Trimmer (F6): drag clip edges",                             [this] { settings.tool = Tool::trimmer; });
    makeButton (toolButtons, "Select", 2002, "Selector (F7): drag a time selection",                      [this] { settings.tool = Tool::selector; });
    makeButton (toolButtons, "Grab",   2002, "Grabber (F8): move clips",                                  [this] { settings.tool = Tool::grabber; });
    makeButton (toolButtons, "Smart",  2002, "Smart Tool (F9): edges trim, lower half grabs, upper half selects", [this] { settings.tool = Tool::smart; });

    addAndMakeVisible (gridBox);
    gridBox.addItem ("Bar", 1);
    gridBox.addItem ("1/4", 2);
    gridBox.addItem ("1/8", 3);
    gridBox.addItem ("1/16", 4);
    gridBox.setTooltip ("Grid value (also the nudge amount)");
    gridBox.onChange = [this]
    {
        const double values[] = { 4.0, 1.0, 0.5, 0.25 };
        if (gridBox.getSelectedId() > 0) settings.gridBeats = values[gridBox.getSelectedId() - 1];
        settings.notify();
    };

    addAndMakeVisible (relativeButton);
    relativeButton.setClickingTogglesState (true);
    relativeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.45f));
    relativeButton.setTooltip ("Relative Grid: snap the movement, keep the clip's offset from the grid");
    relativeButton.onClick = [this] { settings.relativeGrid = relativeButton.getToggleState(); settings.notify(); };

    addAndMakeVisible (ioButton);
    ioButton.setTooltip ("I/O Setup: input/output/bus paths and delay compensation (Ctrl+Alt+I)");
    ioButton.onClick = [this] { if (onIOSetup) onIOSetup(); };

    addAndMakeVisible (hint);
    hint.setColour (juce::Label::textColourId, theme::textDim);
    hint.setFont (juce::FontOptions (11.0f));
    hint.setJustificationType (juce::Justification::centredRight);

    refresh();
}

void EditToolbar::refresh()
{
    modeButtons[(int) settings.mode]->setToggleState (true, juce::dontSendNotification);
    toolButtons[(int) settings.tool]->setToggleState (true, juce::dontSendNotification);
    const int id = settings.gridBeats >= 4.0 ? 1 : settings.gridBeats >= 1.0 ? 2 : settings.gridBeats >= 0.5 ? 3 : 4;
    gridBox.setSelectedId (id, juce::dontSendNotification);
    relativeButton.setToggleState (settings.relativeGrid, juce::dontSendNotification);
    relativeButton.setEnabled (settings.mode == EditSettings::Mode::grid);

    juce::String h = juce::String (EditSettings::modeName (settings.mode)) + "  |  " + EditSettings::toolName (settings.tool)
                   + "    Del: delete   Ctrl+E: separate   Ctrl+D: duplicate   Ctrl+F: fades   , . : nudge   Ctrl+Shift+Up/Down: clip gain";
    hint.setText (h, juce::dontSendNotification);
}

void EditToolbar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelDark);
    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void EditToolbar::resized()
{
    auto area = getLocalBounds().reduced (8, 4);
    for (auto* b : modeButtons) { b->setBounds (area.removeFromLeft (58)); area.removeFromLeft (2); }
    area.removeFromLeft (12);
    for (auto* b : toolButtons) { b->setBounds (area.removeFromLeft (54)); area.removeFromLeft (2); }
    area.removeFromLeft (12);
    gridBox.setBounds (area.removeFromLeft (64));
    area.removeFromLeft (4);
    relativeButton.setBounds (area.removeFromLeft (36));
    area.removeFromLeft (12);
    ioButton.setBounds (area.removeFromRight (56));
    area.removeFromRight (8);
    hint.setBounds (area);
}

} // namespace beatmaker::ui
