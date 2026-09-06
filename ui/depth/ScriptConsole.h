// ScriptConsole: write and run Lua against the open session, see the output,
// save the script into the Scripts folder.
#pragma once

#include "../shared/Theme.h"
#include <LuaEngine.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace beatmaker::ui
{

class ScriptConsole final : public juce::Component
{
public:
    explicit ScriptConsole (scripting::LuaEngine&);
    std::function<void (const juce::String& code)> onSaveScript;
    void setCode (const juce::String& code) { editor.setText (code, juce::dontSendNotification); }
    void runCode();
    void append (const juce::String& text);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    static constexpr int preferredWidth = 720, preferredHeight = 520;

private:
    scripting::LuaEngine& lua;
    juce::TextEditor editor, output;
    juce::TextButton runButton { "Run  (Ctrl+Enter)" }, saveButton { "Save as Script..." }, clearButton { "Clear Output" }, helpButton { "API" };
};

} // namespace beatmaker::ui
