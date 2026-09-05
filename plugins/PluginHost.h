// Plugin hosting: wraps JUCE-hosted plugins (VST3, LV2 on Linux, AU on macOS)
// as insert Effects, and manages scanning with out-of-process validation so a
// crashing plugin is blacklisted instead of bringing the host down.
#pragma once

#include <dsp/Effects.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include <memory>

namespace beatmaker::plugins
{

// A hosted plugin instance as an insert. Parameters live inside the plugin.
class PluginEffect final : public engine::Effect
{
public:
    PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance, juce::PluginDescription description);
    ~PluginEffect() override;

    void reset() override;
    void process (juce::AudioBuffer<float>&, int numSamples, const engine::InsertParams&) noexcept override;
    int getLatencySamples (const engine::InsertParams&) const noexcept override;
    void setAutomatedParameter (int index, float normalised) noexcept override;
    juce::String getDisplayName() const override { return description.name; }

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }
    const juce::PluginDescription& getDescription() const noexcept { return description; }

protected:
    void prepareImpl (int maxBlockSize) override;

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription description;
    juce::MidiBuffer midi;
    int preparedBlockSize = 0;
};

class PluginManager
{
public:
    // `hostExecutable` is relaunched with "--scan-plugin=<format>|<id>" to validate plugins out of process.
    PluginManager (juce::File settingsFile, juce::File hostExecutable);
    ~PluginManager();

    juce::AudioPluginFormatManager& getFormats() noexcept { return formats; }
    juce::KnownPluginList& getKnownPlugins() noexcept { return known; }
    juce::StringArray getFormatNames() const;

    // Background scan of the default paths for every format; progress on the message thread.
    void scanAsync (std::function<void (float progress, const juce::String& currentFile)> onProgress,
                    std::function<void()> onFinished);
    bool isScanning() const noexcept { return scanning.load(); }
    void cancelScan();

    // Message thread. Returns nullptr and sets `error` on failure.
    std::shared_ptr<PluginEffect> instantiate (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);

    void save();
    void load();

    // Child-process entry point: scans one plugin and prints its descriptions as XML. Returns the exit code.
    static int runScanChild (const juce::String& formatName, const juce::String& fileOrIdentifier);
    static constexpr int scanTimeoutMs = 30000;

private:
    class OutOfProcessScanner;
    class ScanThread;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    juce::File settings, executable;
    std::unique_ptr<ScanThread> scanThread;
    std::atomic<bool> scanning { false };
};

} // namespace beatmaker::plugins
