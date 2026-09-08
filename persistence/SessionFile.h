// Session files: a `.bmk` directory bundle holding `session.json` (the whole
// document: tracks, clips, patterns, MIDI, instruments, inserts, automation,
// groups, playlists, I/O, markers, transport) and `Audio Files/` for audio
// that exists only in memory (pencil edits). Referenced audio inside the
// bundle is stored as a relative path, everything else as an absolute one.
// Templates (`.bmkt`) are the same format.
#pragma once

#include <Session.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>

namespace beatmaker::persistence
{

struct TransportState
{
    double bpm = 120.0;
    int beatsPerBar = 4;
    juce::int64 loopStart = 0, loopEnd = 0;
    bool loopEnabled = false;
};

struct LoadContext
{
    double sampleRate = 48000.0;
    // Loads a file at the engine sample rate; null on failure.
    std::function<std::shared_ptr<const juce::AudioBuffer<float>> (const juce::File&)> loadAudio;
    // Hosted plugins by identifier string (PluginDescription::createIdentifierString); may be empty.
    std::function<std::shared_ptr<engine::Effect> (const juce::String& identifier, juce::String& error)> instantiatePlugin;
    // The synthesised default drum kit.
    std::function<std::shared_ptr<const engine::DrumKit>()> defaultKit;
    // Leave Elastic clips of the main playlists unrendered (audio == sourceAudio,
    // offsets in the rendered domain as saved) so the app can render them on a
    // background thread; see model::Elastic::pendingRenders.
    bool deferElasticRenders = false;
};

class SessionFile
{
public:
    static constexpr const char* sessionExtension = ".bmk";
    static constexpr const char* templateExtension = ".bmkt";
    static constexpr int formatVersion = 1;

    // Writes the bundle; returns an error string (empty on success).
    static juce::String save (const model::Session&, const TransportState&, const juce::File& bundle);

    // Reads the bundle into `session` (replacing its contents, clearing history).
    // Missing files are reported in `warnings`; the load still succeeds.
    static juce::String load (model::Session&, TransportState&, const juce::File& bundle, const LoadContext&, juce::StringArray& warnings);

    static juce::File jsonFile (const juce::File& bundle) { return bundle.getChildFile ("session.json"); }
    static bool isSessionBundle (const juce::File& f)
    {
        return f.isDirectory() && (f.hasFileExtension ("bmk") || f.hasFileExtension ("bmkt")) && jsonFile (f).existsAsFile();
    }
    static juce::String sessionName (const juce::File& bundle) { return bundle.getFileNameWithoutExtension(); }

    // A session name as it may appear on disk: trimmed, with characters a file name cannot hold dropped.
    static juce::String legalSessionName (const juce::String& name) { return juce::File::createLegalFileName (name.trim()).trimEnd(); }

    // Renames a bundle folder in place (Name.bmk -> NewName.bmk, same folder, same extension). Everything inside
    // moves with it; the file records its media relative to the bundle, so the session stays whole. Returns the
    // new bundle, or an empty File with `error` set (bad name, a session of that name already there, or the move failed).
    static juce::File renameBundle (const juce::File& bundle, const juce::String& newName, juce::String& error)
    {
        error.clear();
        const auto name = legalSessionName (newName);
        if (! isSessionBundle (bundle)) { error = "Not a session bundle: " + bundle.getFullPathName(); return {}; }
        if (name.isEmpty()) { error = "A session needs a name"; return {}; }
        if (name == sessionName (bundle)) return bundle;
        const auto target = bundle.getSiblingFile (name + bundle.getFileExtension());
        if (target.exists()) { error = "There is already a session called " + name + " in that folder"; return {}; }
        if (! bundle.moveFileTo (target)) { error = "Could not rename the session folder to " + target.getFileName(); return {}; }
        return target;
    }
};

} // namespace beatmaker::persistence
