// Beat Maker.exe is a GUI-subsystem program on Windows, so a terminal that runs it
// with command-line flags (--version, --bounce=, --export-aaf=) would see no output.
// attachToParentConsole() reconnects stdout and stderr to the launching console when
// there is one and they are not already redirected (the plugin-scan child's pipe).
#pragma once

#include <juce_core/juce_core.h>

namespace beatmaker::ui
{
#if JUCE_WINDOWS
void attachToParentConsole();
#else
inline void attachToParentConsole() {}
#endif
} // namespace beatmaker::ui
