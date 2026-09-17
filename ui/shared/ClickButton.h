// ClickButton: a TextButton that tells a right-click apart from a click. A
// plain Button fires onClick for either, and at mouse-up the modifier flags
// no longer say which button it was, so a handler cannot tell. Here a
// right-click (or Ctrl-click on macOS) fires onRightClick on mouse-down and
// never onClick; a left click behaves as usual.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class ClickButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        juce::TextButton::mouseUp (e);
    }
};

} // namespace beatmaker::ui
