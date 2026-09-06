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
};

} // namespace beatmaker::persistence
