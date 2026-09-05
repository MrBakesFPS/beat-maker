// Edit mode, tool and grid: the Pro Tools edit-window state shared by the
// toolbar and the track area.
#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace beatmaker::ui
{

struct EditSettings
{
    enum class Mode { shuffle, slip, spot, grid };
    enum class Tool { zoomer, trimmer, selector, grabber, scrubber, pencil, smart };

    Mode mode = Mode::grid;
    Tool tool = Tool::smart;
    double gridBeats = 1.0;        // 4 = bar, 1 = beat, 0.5 = 1/8, 0.25 = 1/16
    bool relativeGrid = false;     // Grid mode: snap the *movement* rather than the start

    std::function<void()> onChanged;
    void notify() { if (onChanged) onChanged(); }

    static const char* modeName (Mode m)
    {
        switch (m) { case Mode::shuffle: return "Shuffle"; case Mode::slip: return "Slip"; case Mode::spot: return "Spot"; case Mode::grid: return "Grid"; }
        return "";
    }
    static const char* toolName (Tool t)
    {
        switch (t) { case Tool::zoomer: return "Zoomer"; case Tool::trimmer: return "Trimmer"; case Tool::selector: return "Selector";
                     case Tool::grabber: return "Grabber"; case Tool::scrubber: return "Scrubber"; case Tool::pencil: return "Pencil";
                     case Tool::smart: return "Smart Tool"; }
        return "";
    }
};

} // namespace beatmaker::ui
