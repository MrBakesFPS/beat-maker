#include "LuaEngine.h"
#include <ClipEdits.h>
#include <MixerCommands.h>

extern "C"
{
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace beatmaker::scripting
{

using namespace model;

//==============================================================================
// Helpers

struct LuaBindings
{
    static LuaEngine& engine (lua_State* L) { return *static_cast<LuaEngine*> (lua_touserdata (L, lua_upvalueindex (1))); }
    static Session& session (lua_State* L) { return engine (L).host.session(); }
    static engine::Transport& transport (lua_State* L) { return engine (L).host.transport(); }

    static int trackIndex (lua_State* L, int arg)   // 1-based in Lua
    {
        const int i = (int) luaL_checkinteger (L, arg) - 1;
        if (session (L).getTrack (i) == nullptr) luaL_error (L, "no track %d", i + 1);
        return i;
    }
    static ClipRef clipRef (lua_State* L, int trackArg, int clipArg)
    {
        const int t = trackIndex (L, trackArg);
        const auto& track = session (L).getTracks()[(size_t) t];
        const auto kind = ClipEdits::kindForTrack (track);
        const int c = (int) luaL_checkinteger (L, clipArg) - 1;
        if (! juce::isPositiveAndBelow (c, ClipEdits::numClips (track, kind))) luaL_error (L, "no clip %d on track %d", c + 1, t + 1);
        return { t, kind, c };
    }
    static juce::String str (lua_State* L, int arg) { return juce::String (juce::CharPointer_UTF8 (luaL_checkstring (L, arg))); }
    static void pushString (lua_State* L, const juce::String& s) { lua_pushstring (L, s.toRawUTF8()); }
    static void setField (lua_State* L, const char* key, const juce::String& v) { pushString (L, v); lua_setfield (L, -2, key); }
    static void setField (lua_State* L, const char* key, double v) { lua_pushnumber (L, v); lua_setfield (L, -2, key); }
    static void setField (lua_State* L, const char* key, int v) { lua_pushinteger (L, v); lua_setfield (L, -2, key); }
    static void setField (lua_State* L, const char* key, bool v) { lua_pushboolean (L, v); lua_setfield (L, -2, key); }

    static const char* trackKind (const Track& t)
    {
        if (t.isDrumMachine()) return "drums";
        if (t.isSynth()) return "synth";
        switch (t.type) { case Track::Type::audio: return "audio"; case Track::Type::aux: return "aux"; case Track::Type::vca: return "vca"; case Track::Type::master: return "master"; case Track::Type::instrument: default: return "instrument"; }
    }

    static void pushTrack (lua_State* L, const Track& t, int index)
    {
        lua_newtable (L);
        setField (L, "index", index + 1);
        setField (L, "id", t.id);
        setField (L, "name", t.name);
        setField (L, "colour", "#" + t.colour.toDisplayString (false));
        setField (L, "kind", juce::String (trackKind (t)));
        setField (L, "gain", (double) t.gain);
        setField (L, "gain_db", (double) juce::Decibels::gainToDecibels (t.gain, -100.0f));
        setField (L, "pan", (double) t.pan);
        setField (L, "mute", t.mute);
        setField (L, "solo", t.solo);
        setField (L, "armed", t.armed);
        setField (L, "num_clips", ClipEdits::numClips (t, ClipEdits::kindForTrack (t)));
        setField (L, "output_bus", t.outputBus);
    }

    // ---- print -> output buffer + host log
    static int print (lua_State* L)
    {
        auto& e = engine (L);
        juce::String line;
        const int n = lua_gettop (L);
        for (int i = 1; i <= n; ++i)
        {
            size_t len = 0;
            const char* s = luaL_tolstring (L, i, &len);
            if (i > 1) line += "\t";
            line += juce::String (juce::CharPointer_UTF8 (s), len);
            lua_pop (L, 1);
        }
        e.output += line + "\n";
        e.host.log (line);
        return 0;
    }

    // ---- transport
    static int play (lua_State* L) { transport (L).play(); return 0; }
    static int stop (lua_State* L) { transport (L).stop(); return 0; }
    static int isPlaying (lua_State* L) { lua_pushboolean (L, transport (L).isPlaying()); return 1; }
    static int position (lua_State* L) { lua_pushnumber (L, transport (L).getPositionSeconds()); return 1; }
    static int setPosition (lua_State* L) { transport (L).setPositionSeconds (juce::jmax (0.0, luaL_checknumber (L, 1))); return 0; }
    static int bpm (lua_State* L) { lua_pushnumber (L, transport (L).getBpm()); return 1; }
    static int setBpm (lua_State* L) { engine (L).host.setTempo (juce::jlimit (20.0, 400.0, luaL_checknumber (L, 1))); return 0; }
    static int bar (lua_State* L) { lua_pushinteger (L, transport (L).getBarBeat().bar); return 1; }
    static int locateBar (lua_State* L)
    {
        auto& t = transport (L);
        t.setPositionSeconds (t.beatsToSeconds ((luaL_checkinteger (L, 1) - 1) * (double) t.getBeatsPerBar()));
        return 0;
    }
    static int beatsToSeconds (lua_State* L) { lua_pushnumber (L, transport (L).beatsToSeconds (luaL_checknumber (L, 1))); return 1; }
    static int cycle (lua_State* L) { transport (L).setLoopEnabled (lua_toboolean (L, 1)); return 0; }

    // ---- session
    static int tracks (lua_State* L)
    {
        const auto& s = session (L);
        lua_newtable (L);
        for (int i = 0; i < s.getNumTracks(); ++i) { pushTrack (L, s.getTracks()[(size_t) i], i); lua_rawseti (L, -2, i + 1); }
        return 1;
    }
    static int track (lua_State* L) { const int i = trackIndex (L, 1); pushTrack (L, session (L).getTracks()[(size_t) i], i); return 1; }
    static int numTracks (lua_State* L) { lua_pushinteger (L, session (L).getNumTracks()); return 1; }
    static int addTrack (lua_State* L)
    {
        const auto kind = str (L, 1);
        const auto name = lua_gettop (L) >= 2 ? str (L, 2) : juce::String();
        const int index = engine (L).host.addTrack (kind, name);
        if (index < 0) return luaL_error (L, "unknown track kind '%s' (audio, drums, synth, fm, wavetable, sampler, electricpiano, bass, aux, vca)", kind.toRawUTF8());
        lua_pushinteger (L, index + 1);
        return 1;
    }
    static int renameTrack (lua_State* L) { session (L).execute (std::make_unique<RenameTrackCommand> (trackIndex (L, 1), str (L, 2))); return 0; }
    static int setTrackColour (lua_State* L)   // "#rrggbb" or "rrggbb"
    {
        const auto hex = str (L, 2).trim().removeCharacters ("#");
        if (hex.length() != 6) return luaL_error (L, "colour must be #rrggbb");
        session (L).execute (std::make_unique<SetTrackColourCommand> (trackIndex (L, 1), juce::Colour::fromString ("ff" + hex)));
        return 0;
    }
    static int removeTrack (lua_State* L) { session (L).execute (std::make_unique<RemoveTrackCommand> (trackIndex (L, 1))); return 0; }
    static int setGain (lua_State* L)
    {
        const int i = trackIndex (L, 1);
        const auto& t = session (L).getTracks()[(size_t) i];
        session (L).execute (std::make_unique<SetTrackMixCommand> (i, (float) juce::jlimit (0.0, 2.0, luaL_checknumber (L, 2)), t.pan));
        return 0;
    }
    static int setGainDb (lua_State* L)
    {
        const int i = trackIndex (L, 1);
        const auto& t = session (L).getTracks()[(size_t) i];
        session (L).execute (std::make_unique<SetTrackMixCommand> (i, juce::Decibels::decibelsToGain ((float) luaL_checknumber (L, 2), -100.0f), t.pan));
        return 0;
    }
    static int setPan (lua_State* L)
    {
        const int i = trackIndex (L, 1);
        const auto& t = session (L).getTracks()[(size_t) i];
        session (L).execute (std::make_unique<SetTrackMixCommand> (i, t.gain, (float) juce::jlimit (-1.0, 1.0, luaL_checknumber (L, 2))));
        return 0;
    }
    static int setFlag (lua_State* L, SetTrackFlagCommand::Flag flag)
    {
        session (L).execute (std::make_unique<SetTrackFlagCommand> (trackIndex (L, 1), flag, lua_toboolean (L, 2) != 0));
        return 0;
    }
    static int setMute (lua_State* L) { return setFlag (L, SetTrackFlagCommand::Flag::mute); }
    static int setSolo (lua_State* L) { return setFlag (L, SetTrackFlagCommand::Flag::solo); }
    static int setArm (lua_State* L) { return setFlag (L, SetTrackFlagCommand::Flag::arm); }

    static int clips (lua_State* L)
    {
        const int i = trackIndex (L, 1);
        const auto& s = session (L);
        lua_newtable (L);
        int n = 1;
        for (const auto& ref : ClipEdits::allClips (s, i))
        {
            const auto t = ClipEdits::timing (s, ref);
            if (! t) continue;
            lua_newtable (L);
            setField (L, "index", ref.index + 1);
            setField (L, "kind", juce::String (ref.kind == ClipRef::Kind::audio ? "audio" : ref.kind == ClipRef::Kind::pattern ? "pattern" : "midi"));
            setField (L, "name", t->name);
            setField (L, "start", (double) t->start / t->sampleRate);
            setField (L, "length", (double) t->length / t->sampleRate);
            setField (L, "end", (double) (t->start + t->length) / t->sampleRate);
            lua_rawseti (L, -2, n++);
        }
        return 1;
    }
    static juce::int64 samples (lua_State* L, double seconds) { return (juce::int64) std::llround (seconds * transport (L).getSampleRate()); }
    static int moveClip (lua_State* L)
    {
        const auto ref = clipRef (L, 1, 2);
        session (L).execute (std::make_unique<MoveClipCommand> (ref, ref.track, juce::jmax<juce::int64> (0, samples (L, luaL_checknumber (L, 3)))));
        return 0;
    }
    static int trimClip (lua_State* L)
    {
        const auto ref = clipRef (L, 1, 2);
        session (L).execute (std::make_unique<TrimClipCommand> (ref, samples (L, luaL_checknumber (L, 3)), juce::jmax<juce::int64> (1, samples (L, luaL_checknumber (L, 4)))));
        return 0;
    }
    static int deleteClip (lua_State* L) { session (L).execute (std::make_unique<RemoveAnyClipCommand> (clipRef (L, 1, 2))); return 0; }
    static int duplicateClip (lua_State* L) { session (L).execute (std::make_unique<DuplicateClipCommand> (clipRef (L, 1, 2))); return 0; }
    static int setClipGain (lua_State* L)
    {
        const auto ref = clipRef (L, 1, 2);
        if (ref.kind != ClipRef::Kind::audio) return luaL_error (L, "clip gain applies to audio clips");
        session (L).execute (std::make_unique<SetClipGainCommand> (ref, (float) juce::jlimit (0.0, 4.0, luaL_checknumber (L, 3))));
        return 0;
    }

    static int markers (lua_State* L)
    {
        lua_newtable (L);
        int n = 1;
        for (const auto& m : session (L).getMarkers())
        {
            lua_newtable (L);
            setField (L, "id", m.id); setField (L, "name", m.name); setField (L, "seconds", m.seconds);
            setField (L, "end_seconds", m.endSeconds); setField (L, "is_section", m.isSection);
            lua_rawseti (L, -2, n++);
        }
        return 1;
    }
    static int addMarker (lua_State* L)
    {
        Marker m;
        m.name = str (L, 1);
        m.seconds = juce::jmax (0.0, luaL_checknumber (L, 2));
        if (lua_gettop (L) >= 3 && ! lua_isnil (L, 3)) { m.endSeconds = luaL_checknumber (L, 3); m.isSection = true; }
        auto cmd = std::make_unique<AddMarkerCommand> (m);
        auto* raw = cmd.get();
        session (L).execute (std::move (cmd));
        lua_pushinteger (L, raw->getMarkerId());
        return 1;
    }
    static int removeMarker (lua_State* L) { session (L).execute (std::make_unique<RemoveMarkerCommand> ((int) luaL_checkinteger (L, 1))); return 0; }

    static engine::EffectType effectTypeFor (const juce::String& name)
    {
        for (auto t : engine::Effect::availableTypes())
            if (juce::String (engine::Effect::typeName (t)).equalsIgnoreCase (name) || juce::String (engine::Effect::typeName (t)).removeCharacters (" -/").equalsIgnoreCase (name.removeCharacters (" -/_")))
                return t;
        return engine::EffectType::none;
    }
    static int setInsert (lua_State* L)
    {
        const int i = (int) luaL_checkinteger (L, 1) - 1;
        if (i != -1 && session (L).getTrack (i) == nullptr) return luaL_error (L, "no track %d", i + 1);
        const int slot = (int) luaL_checkinteger (L, 2) - 1;
        if (! juce::isPositiveAndBelow (slot, Track::numInsertSlots)) return luaL_error (L, "insert slot must be 1..%d", Track::numInsertSlots);
        const auto name = str (L, 3);
        const auto type = name.isEmpty() || name.equalsIgnoreCase ("none") ? engine::EffectType::none : effectTypeFor (name);
        if (type == engine::EffectType::none && name.isNotEmpty() && ! name.equalsIgnoreCase ("none")) return luaL_error (L, "unknown effect '%s'", name.toRawUTF8());
        session (L).execute (std::make_unique<SetInsertCommand> (i, slot, type, engine (L).host.sampleRate()));
        return 0;
    }
    static int setInsertParam (lua_State* L)
    {
        const int i = (int) luaL_checkinteger (L, 1) - 1;
        const auto* t = session (L).getTrackOrMaster (i);
        const int slot = (int) luaL_checkinteger (L, 2) - 1;
        if (t == nullptr || ! juce::isPositiveAndBelow (slot, Track::numInsertSlots) || t->inserts[(size_t) slot].isEmpty()) return luaL_error (L, "no insert in slot %d of track %d", slot + 1, i + 1);
        const auto& ins = t->inserts[(size_t) slot];
        auto p = std::make_shared<engine::InsertParams> (*ins.params);
        const auto& info = engine::Effect::paramInfo (ins.type);
        int param = -1;
        if (lua_type (L, 3) == LUA_TSTRING)
        {
            // Exact name first, then a prefix either way ("Thresh" / "Threshold")
            const auto pname = str (L, 3);
            for (int k = 0; k < (int) info.size() && param < 0; ++k) if (juce::String (info[(size_t) k].name).equalsIgnoreCase (pname)) param = k;
            for (int k = 0; k < (int) info.size() && param < 0; ++k)
            {
                const juce::String n (info[(size_t) k].name);
                if (n.startsWithIgnoreCase (pname) || pname.startsWithIgnoreCase (n)) param = k;
            }
        }
        else param = (int) luaL_checkinteger (L, 3) - 1;
        if (! juce::isPositiveAndBelow (param, (int) info.size())) return luaL_error (L, "no such parameter");
        p->values[(size_t) param] = juce::jlimit (info[(size_t) param].min, info[(size_t) param].max, (float) luaL_checknumber (L, 4));
        session (L).execute (std::make_unique<SetInsertParamsCommand> (i, slot, p));
        return 0;
    }
    static int insertParams (lua_State* L)
    {
        const int i = (int) luaL_checkinteger (L, 1) - 1;
        const auto* t = session (L).getTrackOrMaster (i);
        const int slot = (int) luaL_checkinteger (L, 2) - 1;
        lua_newtable (L);
        if (t == nullptr || ! juce::isPositiveAndBelow (slot, Track::numInsertSlots) || t->inserts[(size_t) slot].isEmpty()) return 1;
        const auto& ins = t->inserts[(size_t) slot];
        const auto& info = engine::Effect::paramInfo (ins.type);
        for (int k = 0; k < (int) info.size(); ++k)
        {
            lua_newtable (L);
            setField (L, "name", juce::String (info[(size_t) k].name)); setField (L, "value", (double) ins.params->values[(size_t) k]);
            setField (L, "min", (double) info[(size_t) k].min); setField (L, "max", (double) info[(size_t) k].max);
            lua_rawseti (L, -2, k + 1);
        }
        return 1;
    }
    static int setSend (lua_State* L)
    {
        const int i = trackIndex (L, 1);
        const int slot = (int) luaL_checkinteger (L, 2) - 1;
        if (! juce::isPositiveAndBelow (slot, Track::numSendSlots)) return luaL_error (L, "send slot must be 1..%d", Track::numSendSlots);
        Send send;
        send.bus = lua_isnil (L, 3) ? -1 : (int) luaL_checkinteger (L, 3) - 1;
        send.gain = lua_gettop (L) >= 4 ? (float) juce::jlimit (0.0, 2.0, luaL_checknumber (L, 4)) : 1.0f;
        send.preFader = lua_gettop (L) >= 5 && lua_toboolean (L, 5);
        session (L).execute (std::make_unique<SetSendCommand> (i, slot, send));
        return 0;
    }

    static int midiNotes (lua_State* L)
    {
        const auto ref = clipRef (L, 1, 2);
        if (ref.kind != ClipRef::Kind::midi) return luaL_error (L, "not a MIDI clip");
        const auto& clip = session (L).getTracks()[(size_t) ref.track].midiClips[(size_t) ref.index];
        lua_newtable (L);
        if (clip.sequence == nullptr) return 1;
        int n = 1;
        for (const auto& note : clip.sequence->notes)
        {
            lua_newtable (L);
            setField (L, "pitch", note.pitch); setField (L, "velocity", note.velocity); setField (L, "start", note.startBeat); setField (L, "length", note.lengthBeats);
            lua_rawseti (L, -2, n++);
        }
        return 1;
    }
    static int setMidiNotes (lua_State* L)
    {
        const auto ref = clipRef (L, 1, 2);
        if (ref.kind != ClipRef::Kind::midi) return luaL_error (L, "not a MIDI clip");
        luaL_checktype (L, 3, LUA_TTABLE);
        const auto& clip = session (L).getTracks()[(size_t) ref.track].midiClips[(size_t) ref.index];
        auto seq = std::make_shared<engine::MidiSequence> (clip.sequence != nullptr ? *clip.sequence : engine::MidiSequence());
        seq->notes.clear();
        const int count = (int) lua_rawlen (L, 3);
        for (int k = 1; k <= count; ++k)
        {
            lua_rawgeti (L, 3, k);
            if (lua_istable (L, -1))
            {
                engine::NoteEvent n;
                lua_getfield (L, -1, "pitch"); n.pitch = juce::jlimit (0, 127, (int) luaL_optinteger (L, -1, 60)); lua_pop (L, 1);
                lua_getfield (L, -1, "velocity"); n.velocity = juce::jlimit (1, 127, (int) luaL_optinteger (L, -1, 100)); lua_pop (L, 1);
                lua_getfield (L, -1, "start"); n.startBeat = juce::jmax (0.0, luaL_optnumber (L, -1, 0.0)); lua_pop (L, 1);
                lua_getfield (L, -1, "length"); n.lengthBeats = juce::jmax (0.01, luaL_optnumber (L, -1, 1.0)); lua_pop (L, 1);
                seq->notes.push_back (n);
            }
            lua_pop (L, 1);
        }
        seq->sortNotes();
        session (L).execute (std::make_unique<ReplaceMidiSequenceCommand> (ref.track, ref.index, seq, "Script: Set Notes"));
        return 0;
    }
    static int addMidiNote (lua_State* L)
    {
        const auto ref = clipRef (L, 1, 2);
        if (ref.kind != ClipRef::Kind::midi) return luaL_error (L, "not a MIDI clip");
        const auto& clip = session (L).getTracks()[(size_t) ref.track].midiClips[(size_t) ref.index];
        auto seq = std::make_shared<engine::MidiSequence> (clip.sequence != nullptr ? *clip.sequence : engine::MidiSequence());
        engine::NoteEvent n;
        n.pitch = juce::jlimit (0, 127, (int) luaL_checkinteger (L, 3));
        n.velocity = juce::jlimit (1, 127, (int) luaL_optinteger (L, 4, 100));
        n.startBeat = juce::jmax (0.0, luaL_optnumber (L, 5, 0.0));
        n.lengthBeats = juce::jmax (0.01, luaL_optnumber (L, 6, 1.0));
        seq->notes.push_back (n);
        seq->sortNotes();
        session (L).execute (std::make_unique<ReplaceMidiSequenceCommand> (ref.track, ref.index, seq, "Script: Add Note"));
        return 0;
    }
    static int undo (lua_State* L) { lua_pushboolean (L, session (L).undo()); return 1; }
    static int redo (lua_State* L) { lua_pushboolean (L, session (L).redo()); return 1; }
    static int length (lua_State* L) { lua_pushnumber (L, session (L).getLengthSeconds()); return 1; }

    // ---- app
    static int status (lua_State* L) { engine (L).host.status (str (L, 1)); return 0; }
    static int log (lua_State* L) { engine (L).host.log (str (L, 1)); return 0; }
    static int runCommand (lua_State* L) { lua_pushboolean (L, engine (L).host.runCommand (str (L, 1))); return 1; }
    static int bounce (lua_State* L)
    {
        const auto msg = engine (L).host.bounce (juce::File::getCurrentWorkingDirectory().getChildFile (str (L, 1)));
        if (msg.isNotEmpty()) { pushString (L, msg); return 1; }
        lua_pushboolean (L, true); return 1;
    }
    static int save (lua_State* L) { lua_pushboolean (L, engine (L).host.saveSession (juce::File::getCurrentWorkingDirectory().getChildFile (str (L, 1)))); return 1; }
    static int open (lua_State* L) { lua_pushboolean (L, engine (L).host.openSession (juce::File::getCurrentWorkingDirectory().getChildFile (str (L, 1)))); return 1; }
    static int pref (lua_State* L)
    {
        const auto v = engine (L).host.getPreference (str (L, 1));
        if (v.isBool()) lua_pushboolean (L, (bool) v);
        else if (v.isDouble() || v.isInt() || v.isInt64()) lua_pushnumber (L, (double) v);
        else if (v.isVoid()) lua_pushnil (L);
        else pushString (L, v.toString());
        return 1;
    }
    static int setPref (lua_State* L)
    {
        juce::var v;
        if (lua_isboolean (L, 2)) v = lua_toboolean (L, 2) != 0;
        else if (lua_isnumber (L, 2)) v = lua_tonumber (L, 2);
        else v = str (L, 2);
        engine (L).host.setPreference (str (L, 1), v);
        return 0;
    }
    static int registerCommand (lua_State* L)
    {
        const auto id = str (L, 1), name = str (L, 2);
        luaL_checktype (L, 3, LUA_TFUNCTION);
        lua_getglobal (L, "beatmaker"); lua_getfield (L, -1, "_commands");
        lua_pushvalue (L, 3); lua_setfield (L, -2, id.toRawUTF8());
        lua_pop (L, 2);
        auto* e = &engine (L);
        e->host.registerCommand (id, name, [e, id] { e->callRegistered (id); });
        return 0;
    }
};

//==============================================================================

LuaEngine::LuaEngine (ScriptHost& h) : host (h)
{
    L = luaL_newstate();
    luaL_openlibs (L);
    // Sandbox: no io, no process control
    lua_pushnil (L); lua_setglobal (L, "io");
    lua_getglobal (L, "os");
    for (const char* f : { "exit", "execute", "remove", "rename", "tmpname", "getenv", "setlocale" }) { lua_pushnil (L); lua_setfield (L, -2, f); }
    lua_pop (L, 1);
    lua_pushnil (L); lua_setglobal (L, "dofile");
    lua_pushnil (L); lua_setglobal (L, "loadfile");
    installApi();
}

LuaEngine::~LuaEngine() { if (L != nullptr) lua_close (L); }

void LuaEngine::installApi()
{
    auto reg = [this] (const char* name, lua_CFunction fn) { lua_pushlightuserdata (L, this); lua_pushcclosure (L, fn, 1); lua_setfield (L, -2, name); };
    lua_newtable (L);   // beatmaker
    lua_pushstring (L, "0.1"); lua_setfield (L, -2, "version");
    lua_newtable (L); lua_setfield (L, -2, "_commands");

    lua_newtable (L);   // transport
    reg ("play", LuaBindings::play); reg ("stop", LuaBindings::stop); reg ("is_playing", LuaBindings::isPlaying);
    reg ("position", LuaBindings::position); reg ("set_position", LuaBindings::setPosition);
    reg ("bpm", LuaBindings::bpm); reg ("set_bpm", LuaBindings::setBpm); reg ("bar", LuaBindings::bar); reg ("locate_bar", LuaBindings::locateBar);
    reg ("beats_to_seconds", LuaBindings::beatsToSeconds); reg ("set_cycle", LuaBindings::cycle);
    lua_setfield (L, -2, "transport");

    lua_newtable (L);   // session
    reg ("tracks", LuaBindings::tracks); reg ("track", LuaBindings::track); reg ("num_tracks", LuaBindings::numTracks);
    reg ("add_track", LuaBindings::addTrack); reg ("rename_track", LuaBindings::renameTrack); reg ("remove_track", LuaBindings::removeTrack);
    reg ("set_track_colour", LuaBindings::setTrackColour);
    reg ("set_gain", LuaBindings::setGain); reg ("set_gain_db", LuaBindings::setGainDb); reg ("set_pan", LuaBindings::setPan);
    reg ("set_mute", LuaBindings::setMute); reg ("set_solo", LuaBindings::setSolo); reg ("set_arm", LuaBindings::setArm);
    reg ("clips", LuaBindings::clips); reg ("move_clip", LuaBindings::moveClip); reg ("trim_clip", LuaBindings::trimClip);
    reg ("delete_clip", LuaBindings::deleteClip); reg ("duplicate_clip", LuaBindings::duplicateClip); reg ("set_clip_gain", LuaBindings::setClipGain);
    reg ("markers", LuaBindings::markers); reg ("add_marker", LuaBindings::addMarker); reg ("remove_marker", LuaBindings::removeMarker);
    reg ("set_insert", LuaBindings::setInsert); reg ("set_insert_param", LuaBindings::setInsertParam); reg ("insert_params", LuaBindings::insertParams);
    reg ("set_send", LuaBindings::setSend);
    reg ("midi_notes", LuaBindings::midiNotes); reg ("set_midi_notes", LuaBindings::setMidiNotes); reg ("add_midi_note", LuaBindings::addMidiNote);
    reg ("undo", LuaBindings::undo); reg ("redo", LuaBindings::redo); reg ("length", LuaBindings::length);
    lua_setfield (L, -2, "session");

    lua_newtable (L);   // app
    reg ("status", LuaBindings::status); reg ("log", LuaBindings::log); reg ("run_command", LuaBindings::runCommand);
    reg ("bounce", LuaBindings::bounce); reg ("save", LuaBindings::save); reg ("open", LuaBindings::open);
    reg ("pref", LuaBindings::pref); reg ("set_pref", LuaBindings::setPref); reg ("register_command", LuaBindings::registerCommand);
    lua_setfield (L, -2, "app");

    lua_setglobal (L, "beatmaker");
    lua_pushlightuserdata (L, this); lua_pushcclosure (L, LuaBindings::print, 1); lua_setglobal (L, "print");
}

LuaEngine::Result LuaEngine::run (const juce::String& code, const juce::String& chunkName)
{
    Result r;
    output = {};
    if (luaL_loadbuffer (L, code.toRawUTF8(), code.getNumBytesAsUTF8(), chunkName.toRawUTF8()) != LUA_OK
        || lua_pcall (L, 0, 0, 0) != LUA_OK)
    {
        r.ok = false;
        r.error = juce::String (juce::CharPointer_UTF8 (lua_tostring (L, -1)));
        lua_pop (L, 1);
    }
    r.output = output;
    return r;
}

LuaEngine::Result LuaEngine::runFile (const juce::File& file)
{
    if (! file.existsAsFile()) { Result r; r.ok = false; r.error = "No such script: " + file.getFullPathName(); return r; }
    return run (file.loadFileAsString(), file.getFileName());
}

LuaEngine::Result LuaEngine::callRegistered (const juce::String& id)
{
    Result r;
    output = {};
    lua_getglobal (L, "beatmaker"); lua_getfield (L, -1, "_commands"); lua_getfield (L, -1, id.toRawUTF8());
    if (! lua_isfunction (L, -1)) { lua_pop (L, 3); r.ok = false; r.error = "no script command " + id; return r; }
    if (lua_pcall (L, 0, 0, 0) != LUA_OK) { r.ok = false; r.error = juce::String (juce::CharPointer_UTF8 (lua_tostring (L, -1))); lua_pop (L, 1); }
    lua_pop (L, 2);
    r.output = output;
    return r;
}

juce::String LuaEngine::apiReference()
{
    return "beatmaker.transport: play() stop() is_playing() position() set_position(s) bpm() set_bpm(b) bar() locate_bar(n) beats_to_seconds(b) set_cycle(on)\n"
           "beatmaker.session: tracks() track(i) num_tracks() add_track(kind[, name]) rename_track(i, name) remove_track(i) set_track_colour(i, '#rrggbb')\n"
           "  set_gain(i, g) set_gain_db(i, db) set_pan(i, p) set_mute(i, on) set_solo(i, on) set_arm(i, on)\n"
           "  clips(i) move_clip(i, c, start) trim_clip(i, c, start, len) delete_clip(i, c) duplicate_clip(i, c) set_clip_gain(i, c, g)\n"
           "  markers() add_marker(name, s[, end_s]) remove_marker(id) set_insert(i|0=master, slot, effect) set_insert_param(i, slot, name|n, v) insert_params(i, slot)\n"
           "  set_send(i, slot, bus, gain, pre) midi_notes(i, c) set_midi_notes(i, c, {{pitch=,velocity=,start=,length=}...}) add_midi_note(i, c, pitch, vel, start, len)\n"
           "  undo() redo() length()\n"
           "beatmaker.app: status(msg) log(msg) run_command(id) bounce(path) save(path) open(path) pref(id) set_pref(id, v) register_command(id, name, fn)\n"
           "Indices are 1-based. Track kinds: audio drums synth fm wavetable sampler electricpiano bass aux vca.";
}

} // namespace beatmaker::scripting
