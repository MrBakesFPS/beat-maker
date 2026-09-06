// Lua scripting: a sandboxed Lua 5.4 state exposing `beatmaker.session`,
// `beatmaker.transport` and `beatmaker.app`. Everything a script changes
// goes through the same undoable commands the UI uses. App-level services
// (creating typed tracks, bouncing, saving, status) come from a ScriptHost.
#pragma once

#include <Session.h>
#include <transport/Transport.h>
#include <functional>
#include <memory>

struct lua_State;

namespace beatmaker::scripting
{

class ScriptHost
{
public:
    virtual ~ScriptHost() = default;
    virtual model::Session& session() = 0;
    virtual engine::Transport& transport() = 0;
    virtual double sampleRate() const = 0;
    virtual void setTempo (double bpm) { transport().setBpm (bpm); }   // apps route this through their undoable tempo command
    // kind: audio | drums | synth | fm | wavetable | sampler | electricpiano | bass | aux | vca. Returns the track index or -1.
    virtual int addTrack (const juce::String& kind, const juce::String& name) = 0;
    virtual void status (const juce::String&) = 0;
    virtual void log (const juce::String&) = 0;
    virtual bool runCommand (const juce::String& id) = 0;
    virtual juce::String bounce (const juce::File&) = 0;      // empty = ok
    virtual bool saveSession (const juce::File&) = 0;
    virtual bool openSession (const juce::File&) = 0;
    virtual juce::var getPreference (const juce::String& id) = 0;
    virtual void setPreference (const juce::String& id, const juce::var&) = 0;
    virtual void registerCommand (const juce::String& id, const juce::String& name, std::function<void()> run) = 0;
};

class LuaEngine
{
public:
    explicit LuaEngine (ScriptHost&);
    ~LuaEngine();

    struct Result { bool ok = true; juce::String error; juce::String output; };
    Result run (const juce::String& code, const juce::String& chunkName = "script");
    Result runFile (const juce::File&);

    // Calls a Lua function a script registered through beatmaker.app.register_command.
    Result callRegistered (const juce::String& id);

    static juce::String apiReference();   // one-line-per-function summary for the console

private:
    ScriptHost& host;
    lua_State* L = nullptr;
    juce::String output;
    void installApi();
    friend struct LuaBindings;
};

} // namespace beatmaker::scripting
