#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../ui/shared/Preferences.h"
#include "../ui/shared/CommandRegistry.h"
#include <graph/RenderSnapshot.h>

using namespace beatmaker;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Preferences have defaults, clamp values, search across categories, notify listeners and persist")
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_prefs_test.preferences");
    file.deleteFile();
    {
        ui::Preferences prefs (file);
        CHECK (prefs.all().size() >= 25);
        CHECK (prefs.categories().contains ("Mixing"));
        CHECK (prefs.getBool ("operation.latchRecordEnable"));
        CHECK (prefs.getInt ("mixing.panDepth") == 1);
        CHECK (prefs.choiceName ("mixing.panDepth") == "-3 dB");
        CHECK (prefs.isDefault ("editing.defaultFadeMs"));

        juce::StringArray changed;
        const int token = prefs.addListener ([&] (const juce::String& id) { changed.add (id); });
        prefs.set ("editing.defaultFadeMs", 25.0);
        prefs.set ("editing.defaultFadeMs", 99999.0);   // clamped to the max
        prefs.set ("mixing.panDepth", 3);
        prefs.set ("operation.latchRecordEnable", false);
        CHECK_THAT (prefs.getDouble ("editing.defaultFadeMs"), WithinAbs (5000.0, 1e-9));
        CHECK (prefs.choiceName ("mixing.panDepth") == "-6 dB");
        CHECK_FALSE (prefs.getBool ("operation.latchRecordEnable"));
        CHECK_FALSE (prefs.isDefault ("mixing.panDepth"));
        CHECK (changed.size() == 4);
        prefs.set ("mixing.panDepth", 3);   // unchanged: no notification
        CHECK (changed.size() == 4);
        prefs.removeListener (token);
        prefs.resetToDefault ("mixing.panDepth");
        CHECK (prefs.isDefault ("mixing.panDepth"));
        CHECK (changed.size() == 4);

        const auto hits = prefs.search ("fade");
        CHECK (hits.size() >= 2);
        for (const auto* d : hits) CHECK ((d->name + d->description + d->category).containsIgnoreCase ("fade"));
        CHECK (prefs.search ("pan depth").size() == 1);
        CHECK (prefs.search ("zzzz-nothing").empty());
        prefs.save();
    }
    CHECK (file.existsAsFile());
    {
        ui::Preferences prefs (file);   // reloaded from disk
        CHECK_THAT (prefs.getDouble ("editing.defaultFadeMs"), WithinAbs (5000.0, 1e-9));
        CHECK_FALSE (prefs.getBool ("operation.latchRecordEnable"));
        CHECK (prefs.getInt ("mixing.panDepth") == 1);
        prefs.resetAll();
        CHECK (prefs.isDefault ("editing.defaultFadeMs"));
    }
    file.deleteFile();
}

TEST_CASE ("Command registry dispatches shortcuts and focus keys and searches fuzzily")
{
    ui::CommandRegistry reg;
    int played = 0, trimmed = 0, zoomed = 0;
    bool canTrim = true;
    reg.add ({ "transport.play", "Play / Stop", "Transport", juce::KeyPress (juce::KeyPress::spaceKey), 0, [&] { ++played; }, nullptr });
    reg.add ({ "edit.trimStart", "Trim Start to Insertion", "Edit", {}, 'a', [&] { ++trimmed; }, [&] { return canTrim; } });
    reg.add ({ "view.zoomIn", "Zoom In", "View", juce::KeyPress ('t', juce::ModifierKeys::commandModifier, 0), 't', [&] { ++zoomed; }, nullptr });

    CHECK (reg.handleKey (juce::KeyPress (juce::KeyPress::spaceKey), false));
    CHECK (played == 1);
    CHECK_FALSE (reg.handleKey (juce::KeyPress ('a', 0, 'a'), false));   // focus mode off: plain letters do nothing
    CHECK (reg.handleKey (juce::KeyPress ('a', 0, 'a'), true));
    CHECK (trimmed == 1);
    canTrim = false;
    CHECK_FALSE (reg.handleKey (juce::KeyPress ('a', 0, 'a'), true));   // disabled command
    CHECK (trimmed == 1);
    CHECK (reg.handleKey (juce::KeyPress ('t', juce::ModifierKeys::commandModifier, 0), false));   // shortcut works regardless of focus mode
    CHECK (reg.handleKey (juce::KeyPress ('t', 0, 't'), true));
    CHECK (zoomed == 2);
    CHECK (reg.run ("transport.play"));
    CHECK_FALSE (reg.run ("nope"));
    CHECK (played == 2);

    auto hits = reg.search ("zoom");
    REQUIRE (hits.size() == 1);
    CHECK (hits[0]->id == "view.zoomIn");
    hits = reg.search ("tsi");                       // subsequence of "trim start to insertion"
    REQUIRE (! hits.empty());
    CHECK (hits[0]->id == "edit.trimStart");
    CHECK (reg.search ("").size() == 3);
    CHECK (reg.search ("edit trim").size() == 1);
    CHECK (reg.find ("view.zoomIn")->shortcutText().isNotEmpty());
}

TEST_CASE ("Pan depth keeps the centre at unity and puts the hard pan at +depth dB")
{
    using engine::panGainForChannel;
    for (float depth : { 2.5f, 3.0f, 4.5f, 6.0f })
    {
        INFO (depth);
        CHECK (panGainForChannel (0.0f, 0, depth) == 1.0f);
        CHECK (panGainForChannel (0.0f, 1, depth) == 1.0f);
        CHECK_THAT (juce::Decibels::gainToDecibels (panGainForChannel (-1.0f, 0, depth)), WithinAbs (depth, 0.05));
        CHECK_THAT (juce::Decibels::gainToDecibels (panGainForChannel (1.0f, 1, depth)), WithinAbs (depth, 0.05));
        CHECK (panGainForChannel (-1.0f, 1, depth) < 1.0e-3f);
        // Monotonic: panning left never raises the right channel
        float last = 2.0f;
        for (int i = -10; i <= 10; ++i)
        {
            const float pan = (float) i / 10.0f;
            const float l = panGainForChannel (pan, 0, depth);
            INFO ("pan " << pan << " left " << l << " previous " << last);
            CHECK (l <= last + 1e-6f);
            last = l;
        }
    }
    CHECK_THAT (panGainForChannel (-1.0f, 0), WithinAbs (juce::MathConstants<float>::sqrt2, 1e-6));   // default stays the -3 dB law
}
