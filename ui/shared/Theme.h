// Colours and metrics shared by every view. Dark neutral ground, one accent.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace beatmaker::ui::theme
{

inline const juce::Colour background   { 0xff1c1f24 };
inline const juce::Colour panel        { 0xff2b2f36 };
inline const juce::Colour panelDark    { 0xff23262c };
inline const juce::Colour grid         { 0xff30343b };
inline const juce::Colour gridStrong   { 0xff3c414a };
inline const juce::Colour text         { 0xffe6e8eb };
inline const juce::Colour textDim      { 0xff8b929c };
inline const juce::Colour accent       { 0xff9fe1cb };
inline const juce::Colour lcdBackground{ 0xff101418 };
inline const juce::Colour playhead     { 0xffffffff };
inline const juce::Colour record       { 0xffc0392b };
inline const juce::Colour play         { 0xff27ae60 };
inline const juce::Colour stop         { 0xff7f8c8d };

constexpr int transportHeight = 56;
constexpr int rulerHeight     = 28;
constexpr int trackHeaderWidth= 200;
constexpr int trackHeight     = 124;

} // namespace beatmaker::ui::theme
