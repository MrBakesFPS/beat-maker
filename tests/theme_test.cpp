#include <catch2/catch_test_macros.hpp>
#include "../ui/shared/Theme.h"

using namespace beatmaker::ui;

TEST_CASE ("Every palette meets WCAG contrast for text, and High Contrast reaches AAA")
{
    REQUIRE (theme::palettes().size() == 3);
    CHECK (theme::paletteNames()[1] == "High Contrast");
    for (const auto& p : theme::palettes())
    {
        INFO (p.name);
        const juce::Colour bg (p.background), panel (p.panel), text (p.text), dim (p.textDim), accent (p.accent);
        CHECK (theme::contrastRatio (text, bg) >= 4.5);        // WCAG AA body text
        CHECK (theme::contrastRatio (text, panel) >= 4.5);
        CHECK (theme::contrastRatio (dim, panel) >= 3.0);      // secondary text: at least large-text AA
        CHECK (theme::contrastRatio (accent, bg) >= 3.0);      // UI components
        if (juce::String (p.name) == "High Contrast")
        {
            CHECK (theme::contrastRatio (text, bg) >= 7.0);    // AAA
            CHECK (theme::contrastRatio (dim, bg) >= 7.0);
            CHECK (theme::contrastRatio (accent, bg) >= 7.0);
        }
    }
    // Reference values of the WCAG formula
    CHECK (std::abs (theme::contrastRatio (juce::Colours::white, juce::Colours::black) - 21.0) < 0.01);
    CHECK (std::abs (theme::contrastRatio (juce::Colour (0xff777777), juce::Colours::white) - 4.48) < 0.02);

    // Applying a palette changes the live colours and can be undone by applying Dark again
    theme::applyPalette (theme::palette (1));
    CHECK (theme::background == juce::Colours::black);
    CHECK (theme::text == juce::Colours::white);
    theme::applyPalette (theme::palette (0));
    CHECK (theme::background == juce::Colour (0xff1c1f24));
    juce::LookAndFeel_V4 laf;
    theme::applyLookAndFeel (laf);
    CHECK (laf.findColour (juce::TextButton::textColourOffId) == theme::text);
}
