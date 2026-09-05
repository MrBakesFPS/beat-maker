// GroupDialog: create or edit a Mix/Edit group: name, type, which controls
// follow, and membership.
#pragma once

#include "../shared/Theme.h"
#include <Session.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class GroupDialog final : public juce::Component
{
public:
    GroupDialog (const model::Session& session, model::Group initial);

    std::function<void (const model::Group&)> onApply;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 460, preferredHeight = 420;

private:
    model::Group build() const;

    model::Group group;
    juce::Label nameLabel { {}, "Name" }, typeLabel { {}, "Type" }, followLabel { {}, "Follows" }, membersLabel { {}, "Members" };
    juce::TextEditor nameEditor;
    juce::ComboBox typeBox;
    juce::ToggleButton volumeToggle { "Volume" }, muteToggle { "Mute" }, soloToggle { "Solo" }, panToggle { "Pan" }, armToggle { "Record" };
    juce::Viewport memberViewport;
    juce::Component memberHolder;
    juce::OwnedArray<juce::ToggleButton> memberToggles;
    std::vector<int> memberIds;
    juce::TextButton applyButton { "OK" }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
