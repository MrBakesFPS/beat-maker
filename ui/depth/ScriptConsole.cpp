#include "ScriptConsole.h"

namespace beatmaker::ui
{

ScriptConsole::ScriptConsole (scripting::LuaEngine& engine) : lua (engine)
{
    for (auto* e : { &editor, &output })
    {
        e->setMultiLine (true, false);
        e->setReturnKeyStartsNewLine (true);
        e->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        e->setColour (juce::TextEditor::backgroundColourId, theme::background);
        e->setColour (juce::TextEditor::textColourId, theme::text);
        addAndMakeVisible (e);
    }
    editor.setTabKeyUsedAsCharacter (true);
    editor.setText ("-- Lua: beatmaker.session / transport / app\n"
                    "local s = beatmaker.session\n"
                    "for _, t in ipairs(s.tracks()) do\n"
                    "  print(t.index, t.name, t.kind, string.format('%.1f dB', t.gain_db))\n"
                    "end\n"
                    "beatmaker.app.status('Listed ' .. s.num_tracks() .. ' tracks')\n");
    output.setReadOnly (true);
    output.setColour (juce::TextEditor::textColourId, theme::accent);
    addAndMakeVisible (runButton);
    runButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    runButton.onClick = [this] { runCode(); };
    addAndMakeVisible (saveButton);
    saveButton.onClick = [this] { if (onSaveScript) onSaveScript (editor.getText()); };
    addAndMakeVisible (clearButton);
    clearButton.onClick = [this] { output.clear(); };
    addAndMakeVisible (helpButton);
    helpButton.onClick = [this] { append (scripting::LuaEngine::apiReference()); };
    setSize (preferredWidth, preferredHeight);
}

void ScriptConsole::append (const juce::String& text)
{
    output.moveCaretToEnd();
    output.insertTextAtCaret (text + "\n");
}

void ScriptConsole::runCode()
{
    const auto r = lua.run (editor.getText(), "console");
    if (r.output.isNotEmpty()) append (r.output.trimEnd());
    append (r.ok ? "-- ok" : "-- error: " + r.error);
}

bool ScriptConsole::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::commandModifier, 0)) { runCode(); return true; }
    return false;
}

void ScriptConsole::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Script", 16, 10, 200, 14, juce::Justification::centredLeft);
    g.drawText ("Output", 16, getHeight() / 2 + 22, 200, 14, juce::Justification::centredLeft);
}

void ScriptConsole::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (10);
    auto buttons = area.removeFromBottom (26);
    runButton.setBounds (buttons.removeFromLeft (150)); buttons.removeFromLeft (6);
    saveButton.setBounds (buttons.removeFromLeft (130)); buttons.removeFromLeft (6);
    clearButton.setBounds (buttons.removeFromLeft (100)); buttons.removeFromLeft (6);
    helpButton.setBounds (buttons.removeFromLeft (50));
    area.removeFromBottom (8);
    const int half = area.getHeight() / 2;
    editor.setBounds (area.removeFromTop (half - 8).withTrimmedTop (4));
    area.removeFromTop (20);
    output.setBounds (area);
}

} // namespace beatmaker::ui
