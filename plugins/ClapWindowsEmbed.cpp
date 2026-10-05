#include "ClapWindowsEmbed.h"

#if JUCE_WINDOWS

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace beatmaker::plugins
{

ClapWindowsEmbed::ClapWindowsEmbed()
{
    // A plain STATIC window is enough: it only hosts the plugin's own child windows.
    auto* hwnd = ::CreateWindowExW (0, L"STATIC", L"", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                    0, 0, 400, 300, nullptr, nullptr, ::GetModuleHandleW (nullptr), nullptr);
    handle = hwnd;
    if (hwnd != nullptr) setHWND (hwnd);
}

ClapWindowsEmbed::~ClapWindowsEmbed()
{
    // The editor has already destroyed the plugin GUI; now drop and destroy the container.
    setHWND (nullptr);
    if (handle != nullptr) ::DestroyWindow ((HWND) handle);
}

} // namespace beatmaker::plugins

#endif
