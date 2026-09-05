// CLAP hosting as a JUCE AudioPluginFormat: .clap libraries are loaded with
// dlopen, each plugin is wrapped in an AudioPluginInstance (buses from its
// audio ports, hosted parameters from its params extension, state, latency,
// an X11-embedded editor when it has one), so scanning, blacklisting,
// automation, sidechain keys and the plugin window all work unchanged.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace beatmaker::plugins
{

class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "CLAP";

    juce::String getName() const override { return formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool allowPluginsWhichRequireAsynchronousInstantiation) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize, PluginCreationCallback) override;

    // Synchronous helper (the callback form above completes immediately anyway).
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);
};

// Adds JUCE's default formats plus CLAP to a manager.
void addAllPluginFormats (juce::AudioPluginFormatManager&);

} // namespace beatmaker::plugins
