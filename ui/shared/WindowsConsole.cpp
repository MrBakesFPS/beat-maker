#include "WindowsConsole.h"

#if JUCE_WINDOWS

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <iostream>

namespace beatmaker::ui
{

void attachToParentConsole()
{
    const auto out = ::GetStdHandle (STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;   // already a pipe or file
    if (! ::AttachConsole (ATTACH_PARENT_PROCESS)) return;         // launched from Explorer: nothing to attach to
    FILE* f = nullptr;
    freopen_s (&f, "CONOUT$", "w", stdout);
    freopen_s (&f, "CONOUT$", "w", stderr);
    std::cout.clear();
    std::cerr.clear();
    std::fputs ("\n", stdout);   // the prompt has already been printed; start on a fresh line
}

} // namespace beatmaker::ui

#endif
