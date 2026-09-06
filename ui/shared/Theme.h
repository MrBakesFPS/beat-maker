// Colours and metrics shared by every view. The colours are runtime values
// so a palette (dark, high contrast, light) can be applied from Preferences;
// every view paints from these names, and the LookAndFeel scheme follows.
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace beatmaker::ui::theme
{

inline juce::Colour background   { 0xff1c1f24 };
inline juce::Colour panel        { 0xff2b2f36 };
inline juce::Colour panelDark    { 0xff23262c };
inline juce::Colour grid         { 0xff30343b };
inline juce::Colour gridStrong   { 0xff3c414a };
inline juce::Colour text         { 0xffe6e8eb };
inline juce::Colour textDim      { 0xff8b929c };
inline juce::Colour accent       { 0xff9fe1cb };
inline juce::Colour lcdBackground{ 0xff101418 };
inline juce::Colour playhead     { 0xffffffff };
inline juce::Colour record       { 0xffc0392b };
inline juce::Colour play         { 0xff27ae60 };
inline juce::Colour stop         { 0xff7f8c8d };

constexpr int transportHeight = 56;
constexpr int rulerHeight     = 48;   // markers/sections strip (top 20 px) + bars/beats ruler
constexpr int markerStripHeight = 20;
constexpr int trackHeaderWidth= 200;
constexpr int trackHeight     = 124;

struct Palette
{
    const char* name;
    juce::uint32 background, panel, panelDark, grid, gridStrong, text, textDim, accent, lcdBackground, playhead, record, play, stop;
};

inline const std::vector<Palette>& palettes()
{
    static const std::vector<Palette> all {
        { "Dark",          0xff1c1f24, 0xff2b2f36, 0xff23262c, 0xff30343b, 0xff3c414a, 0xffe6e8eb, 0xff8b929c, 0xff9fe1cb, 0xff101418, 0xffffffff, 0xffc0392b, 0xff27ae60, 0xff7f8c8d },
        { "High Contrast", 0xff000000, 0xff101010, 0xff080808, 0xff3a3a3a, 0xff6a6a6a, 0xffffffff, 0xffd0d0d0, 0xffffe066, 0xff000000, 0xffffff00, 0xffff4040, 0xff40ff40, 0xffc0c0c0 },
        { "Light",         0xfff4f5f7, 0xffe6e8ec, 0xffdde0e5, 0xffcdd2d9, 0xffb4bac3, 0xff1b1e23, 0xff4f5661, 0xff1f7a5c, 0xffffffff, 0xff000000, 0xffc0392b, 0xff1e8449, 0xff5d6d7e } };
    return all;
}

inline juce::StringArray paletteNames() { juce::StringArray n; for (const auto& p : palettes()) n.add (p.name); return n; }

inline const Palette& palette (int index) { const auto& all = palettes(); return all[(size_t) juce::jlimit (0, (int) all.size() - 1, index)]; }

inline void applyPalette (const Palette& p)
{
    background = juce::Colour (p.background); panel = juce::Colour (p.panel); panelDark = juce::Colour (p.panelDark);
    grid = juce::Colour (p.grid); gridStrong = juce::Colour (p.gridStrong); text = juce::Colour (p.text); textDim = juce::Colour (p.textDim);
    accent = juce::Colour (p.accent); lcdBackground = juce::Colour (p.lcdBackground); playhead = juce::Colour (p.playhead);
    record = juce::Colour (p.record); play = juce::Colour (p.play); stop = juce::Colour (p.stop);
}

// WCAG 2 relative luminance and contrast ratio (1..21).
inline double relativeLuminance (juce::Colour c)
{
    auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin (c.getFloatRed()) + 0.7152 * lin (c.getFloatGreen()) + 0.0722 * lin (c.getFloatBlue());
}
inline double contrastRatio (juce::Colour a, juce::Colour b)
{
    const double la = relativeLuminance (a), lb = relativeLuminance (b);
    return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
}

// Standard widgets (buttons, boxes, sliders, lists) follow the palette too.
inline void applyLookAndFeel (juce::LookAndFeel_V4& laf)
{
    juce::LookAndFeel_V4::ColourScheme scheme (background, panel, panelDark, gridStrong, text, accent.darker (0.5f), accent, text, text);
    laf.setColourScheme (scheme);
    laf.setColour (juce::TextButton::buttonColourId, panel.brighter (0.15f));
    laf.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.45f));
    laf.setColour (juce::TextButton::textColourOffId, text);
    laf.setColour (juce::TextButton::textColourOnId, text);
    laf.setColour (juce::ComboBox::backgroundColourId, panelDark);
    laf.setColour (juce::ComboBox::textColourId, text);
    laf.setColour (juce::ComboBox::outlineColourId, gridStrong);
    laf.setColour (juce::ComboBox::arrowColourId, text);
    laf.setColour (juce::PopupMenu::backgroundColourId, panel);
    laf.setColour (juce::PopupMenu::textColourId, text);
    laf.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.darker (0.4f));
    laf.setColour (juce::PopupMenu::highlightedTextColourId, text);
    laf.setColour (juce::TextEditor::backgroundColourId, panelDark);
    laf.setColour (juce::TextEditor::textColourId, text);
    laf.setColour (juce::TextEditor::outlineColourId, gridStrong);
    laf.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    laf.setColour (juce::Label::textColourId, text);
    laf.setColour (juce::Slider::textBoxTextColourId, text);
    laf.setColour (juce::Slider::textBoxBackgroundColourId, panelDark);
    laf.setColour (juce::Slider::thumbColourId, accent);
    laf.setColour (juce::Slider::trackColourId, accent.darker (0.3f));
    laf.setColour (juce::ListBox::backgroundColourId, background);
    laf.setColour (juce::ListBox::textColourId, text);
    laf.setColour (juce::ToggleButton::textColourId, text);
    laf.setColour (juce::ToggleButton::tickColourId, accent);
    laf.setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    laf.setColour (juce::ScrollBar::thumbColourId, gridStrong);
    laf.setColour (juce::TooltipWindow::backgroundColourId, panel.brighter (0.1f));
    laf.setColour (juce::TooltipWindow::textColourId, text);
    laf.setColour (juce::TooltipWindow::outlineColourId, accent);
    laf.setColour (juce::AlertWindow::backgroundColourId, panel);
    laf.setColour (juce::AlertWindow::textColourId, text);
    laf.setColour (juce::TableHeaderComponent::backgroundColourId, panel);
    laf.setColour (juce::TableHeaderComponent::textColourId, text);
    laf.setColour (juce::DocumentWindow::backgroundColourId, background);
}

} // namespace beatmaker::ui::theme
