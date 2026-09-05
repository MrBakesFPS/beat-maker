#include "GroupDialog.h"

namespace beatmaker::ui
{

GroupDialog::GroupDialog (const model::Session& session, model::Group initial) : group (std::move (initial))
{
    for (auto* l : { &nameLabel, &typeLabel, &followLabel, &membersLabel })
    {
        addAndMakeVisible (l);
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }

    addAndMakeVisible (nameEditor);
    nameEditor.setText (group.name, juce::dontSendNotification);

    addAndMakeVisible (typeBox);
    typeBox.addItem ("Edit", 1);
    typeBox.addItem ("Mix", 2);
    typeBox.addItem ("Edit and Mix", 3);
    typeBox.setSelectedId ((int) group.type + 1, juce::dontSendNotification);

    for (auto* t : { &volumeToggle, &muteToggle, &soloToggle, &panToggle, &armToggle }) addAndMakeVisible (t);
    volumeToggle.setToggleState (group.attributes.volume, juce::dontSendNotification);
    muteToggle.setToggleState (group.attributes.mute, juce::dontSendNotification);
    soloToggle.setToggleState (group.attributes.solo, juce::dontSendNotification);
    panToggle.setToggleState (group.attributes.pan, juce::dontSendNotification);
    armToggle.setToggleState (group.attributes.arm, juce::dontSendNotification);

    addAndMakeVisible (memberViewport);
    memberViewport.setViewedComponent (&memberHolder, false);
    memberViewport.setScrollBarsShown (true, false);
    for (const auto& t : session.getTracks())
    {
        if (t.isVca()) continue;
        auto* toggle = memberToggles.add (new juce::ToggleButton (t.name));
        toggle->setToggleState (group.contains (t.id), juce::dontSendNotification);
        memberHolder.addAndMakeVisible (toggle);
        memberIds.push_back (t.id);
    }

    addAndMakeVisible (applyButton);
    applyButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.5f));
    applyButton.onClick = [this] { if (onApply) onApply (build()); };
    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };

    setSize (preferredWidth, preferredHeight);
}

model::Group GroupDialog::build() const
{
    model::Group g = group;
    g.name = nameEditor.getText().trim().isEmpty() ? "Group" : nameEditor.getText().trim();
    g.type = (model::Group::Type) juce::jmax (0, typeBox.getSelectedId() - 1);
    g.attributes.volume = volumeToggle.getToggleState();
    g.attributes.mute = muteToggle.getToggleState();
    g.attributes.solo = soloToggle.getToggleState();
    g.attributes.pan = panToggle.getToggleState();
    g.attributes.arm = armToggle.getToggleState();
    g.trackIds.clear();
    for (int i = 0; i < memberToggles.size(); ++i)
        if (memberToggles[i]->getToggleState()) g.trackIds.push_back (memberIds[(size_t) i]);
    return g;
}

void GroupDialog::paint (juce::Graphics& g) { g.fillAll (theme::panel); }

void GroupDialog::resized()
{
    auto area = getLocalBounds().reduced (20);
    const int labelWidth = 70, rowHeight = 26, gap = 8;

    auto row = [&] (juce::Label& l, juce::Component& c)
    {
        auto r = area.removeFromTop (rowHeight);
        l.setBounds (r.removeFromLeft (labelWidth));
        r.removeFromLeft (8);
        c.setBounds (r);
        area.removeFromTop (gap);
    };
    row (nameLabel, nameEditor);
    row (typeLabel, typeBox);

    auto follow = area.removeFromTop (rowHeight);
    followLabel.setBounds (follow.removeFromLeft (labelWidth));
    follow.removeFromLeft (8);
    const int w = follow.getWidth() / 5;
    for (auto* t : { &volumeToggle, &muteToggle, &soloToggle, &panToggle, &armToggle }) t->setBounds (follow.removeFromLeft (w));
    area.removeFromTop (gap);

    auto buttons = area.removeFromBottom (28);
    applyButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (80));
    area.removeFromBottom (gap);

    auto members = area;
    membersLabel.setBounds (members.removeFromLeft (labelWidth).withHeight (rowHeight));
    members.removeFromLeft (8);
    memberViewport.setBounds (members);
    memberHolder.setSize (members.getWidth() - 10, juce::jmax (members.getHeight(), memberToggles.size() * 24));
    for (int i = 0; i < memberToggles.size(); ++i)
        memberToggles[i]->setBounds (0, i * 24, memberHolder.getWidth(), 24);
}

} // namespace beatmaker::ui
