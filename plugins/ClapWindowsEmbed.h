// A native child window a CLAP plugin can parent its Win32 GUI into (Windows only).
// The window starts as a hidden popup; juce::HWNDComponent re-parents it into the
// plugin window's peer when the editor goes on screen, and keeps its bounds in step.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WINDOWS

namespace beatmaker::plugins
{

class ClapWindowsEmbed final : public juce::HWNDComponent
{
public:
    ClapWindowsEmbed();
    ~ClapWindowsEmbed() override;

    void* getNativeHandle() const noexcept { return handle; }

private:
    void* handle = nullptr;
    JUCE_DECLARE_NON_COPYABLE (ClapWindowsEmbed)
};

} // namespace beatmaker::plugins

#endif
