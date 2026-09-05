#include "PluginHost.h"
#include "ClapHost.h"

namespace beatmaker::plugins
{

//==============================================================================
// PluginEffect

PluginEffect::PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance, juce::PluginDescription desc)
    : Effect (engine::EffectType::plugin), plugin (std::move (instance)), description (std::move (desc))
{
    jassert (plugin != nullptr);
}

PluginEffect::~PluginEffect()
{
    if (plugin != nullptr) plugin->releaseResources();
}

void PluginEffect::prepareImpl (int maxBlockSize)
{
    preparedBlockSize = maxBlockSize;
    sidechainChannels = 0;

    // Main stereo in/out; a second input bus (the plugin's sidechain) is
    // enabled as stereo when the plugin accepts that layout.
    auto layout = plugin->getBusesLayout();
    if (plugin->getBusCount (true) >= 2 && plugin->getBusCount (false) >= 1)
    {
        juce::AudioProcessor::BusesLayout wanted;
        for (int i = 0; i < plugin->getBusCount (true); ++i)  wanted.inputBuses.add (i < 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        for (int i = 0; i < plugin->getBusCount (false); ++i) wanted.outputBuses.add (i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        if (plugin->checkBusesLayoutSupported (wanted) && plugin->setBusesLayout (wanted))
            sidechainChannels = wanted.inputBuses[1].size();
        else
        {
            wanted.inputBuses.set (1, juce::AudioChannelSet::mono());
            if (plugin->checkBusesLayoutSupported (wanted) && plugin->setBusesLayout (wanted)) sidechainChannels = 1;
        }
    }
    if (sidechainChannels == 0)
    {
        plugin->enableAllBuses();
        plugin->setPlayConfigDetails (2, 2, sampleRate, maxBlockSize);
    }
    plugin->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    plugin->prepareToPlay (sampleRate, maxBlockSize);
    keyScratch.setSize (juce::jmax (1, sidechainChannels), maxBlockSize);
    keyScratch.clear();
    channelPtrs.assign ((size_t) (2 + sidechainChannels), nullptr);
}

void PluginEffect::reset()
{
    if (plugin != nullptr) plugin->reset();
}

void PluginEffect::process (juce::AudioBuffer<float>& buffer, int numSamples, const engine::InsertParams&) noexcept
{
    if (plugin == nullptr || numSamples <= 0 || numSamples > preparedBlockSize) return;
    midi.clear();
    if (sidechainChannels > 0 && buffer.getNumChannels() >= 2)
    {
        // Main channels in place, key copied into the sidechain channels (silence when no key is routed).
        for (int ch = 0; ch < sidechainChannels; ++ch)
        {
            float* dest = keyScratch.getWritePointer (ch);
            if (keyChannels != nullptr && keyNumChannels > 0) juce::FloatVectorOperations::copy (dest, keyChannels[juce::jmin (ch, keyNumChannels - 1)], numSamples);
            else                                               juce::FloatVectorOperations::clear (dest, numSamples);
            channelPtrs[(size_t) (2 + ch)] = dest;
        }
        channelPtrs[0] = buffer.getWritePointer (0);
        channelPtrs[1] = buffer.getWritePointer (1);
        juce::AudioBuffer<float> view (channelPtrs.data(), 2 + sidechainChannels, 0, numSamples);
        plugin->processBlock (view, midi);
        return;
    }
    // A non-owning view with exactly numSamples: plugins process whole buffers.
    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), 0, numSamples);
    plugin->processBlock (view, midi);
}

int PluginEffect::getLatencySamples (const engine::InsertParams&) const noexcept
{
    return plugin != nullptr ? plugin->getLatencySamples() : 0;
}

void PluginEffect::setAutomatedParameter (int index, float normalised) noexcept
{
    if (plugin == nullptr) return;
    const auto& params = plugin->getParameters();
    if (juce::isPositiveAndBelow (index, params.size()))
        params[index]->setValue (juce::jlimit (0.0f, 1.0f, normalised));
}

//==============================================================================
// Out-of-process scanner: validates each plugin file in a child process.

class PluginManager::OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (juce::File exe) : executable (std::move (exe)) {}

    bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        if (! executable.existsAsFile())
        {
            // No host binary to relaunch (e.g. tests): scan in-process.
            format.findAllTypesForFile (result, fileOrIdentifier);
            return true;
        }

        juce::ChildProcess child;
        juce::StringArray args { executable.getFullPathName(), "--scan-plugin=" + format.getName() + "|" + fileOrIdentifier };
        if (! child.start (args, juce::ChildProcess::wantStdOut))
            return false;

        if (! child.waitForProcessToFinish (scanTimeoutMs))
        {
            child.kill();
            return false;   // hung: leave unlisted (the list blacklists it)
        }

        const auto output = child.readAllProcessOutput();
        if (child.getExitCode() != 0)
            return false;   // crashed or refused: blacklist

        // Keep only the XML lines (JUCE and the plugin may print other text), then
        // wrap the descriptions so they parse as one document.
        juce::String xmlOnly;
        for (const auto& line : juce::StringArray::fromLines (output))
            if (line.trimStart().startsWithChar ('<')) xmlOnly += line + "\n";

        if (auto xml = juce::parseXML ("<plugins>" + xmlOnly + "</plugins>"))
            for (auto* e : xml->getChildIterator())
            {
                auto desc = std::make_unique<juce::PluginDescription>();
                if (desc->loadFromXml (*e)) result.add (desc.release());
            }
        return true;
    }

private:
    juce::File executable;
};

int PluginManager::runScanChild (const juce::String& formatName, const juce::String& fileOrIdentifier)
{
    juce::AudioPluginFormatManager fm;
    addAllPluginFormats (fm);
    for (auto* format : fm.getFormats())
    {
        if (format->getName() != formatName) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile (found, fileOrIdentifier);
        for (auto* d : found)
            if (auto xml = d->createXml())
                std::cout << xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()) << "\n";
        return 0;
    }
    return 2;   // unknown format
}

//==============================================================================
// Scan thread

class PluginManager::ScanThread final : public juce::Thread
{
public:
    ScanThread (PluginManager& owner, std::function<void (float, const juce::String&)> progress, std::function<void()> finished)
        : Thread ("Plugin Scan"), manager (owner), onProgress (std::move (progress)), onFinished (std::move (finished)) {}

    void run() override
    {
        const int numFormats = manager.formats.getNumFormats();
        for (int f = 0; f < numFormats && ! threadShouldExit(); ++f)
        {
            auto* format = manager.formats.getFormat (f);
            juce::PluginDirectoryScanner dirScanner (manager.known, *format, format->getDefaultLocationsToSearch(), true, juce::File(), false);
            juce::String current;
            while (! threadShouldExit())
            {
                const bool more = dirScanner.scanNextFile (true, current);
                const float progress = ((float) f + dirScanner.getProgress()) / (float) juce::jmax (1, numFormats);
                juce::MessageManager::callAsync ([cb = onProgress, progress, current] { if (cb) cb (progress, current); });
                if (! more) break;
            }
        }
        juce::MessageManager::callAsync ([this] { manager.scanning.store (false); manager.save(); if (onFinished) onFinished(); });
    }

private:
    PluginManager& manager;
    std::function<void (float, const juce::String&)> onProgress;
    std::function<void()> onFinished;
};

//==============================================================================
// PluginManager

PluginManager::PluginManager (juce::File settingsFile, juce::File hostExecutable)
    : settings (std::move (settingsFile)), executable (std::move (hostExecutable))
{
    addAllPluginFormats (formats);
    known.setCustomScanner (std::make_unique<OutOfProcessScanner> (executable));
    load();
}

PluginManager::~PluginManager()
{
    cancelScan();
}

juce::StringArray PluginManager::getFormatNames() const
{
    juce::StringArray names;
    for (auto* f : formats.getFormats()) names.add (f->getName());
    return names;
}

void PluginManager::scanAsync (std::function<void (float, const juce::String&)> onProgress, std::function<void()> onFinished)
{
    if (scanning.exchange (true)) return;
    if (scanThread != nullptr) scanThread->stopThread (5000);
    scanThread = std::make_unique<ScanThread> (*this, std::move (onProgress), std::move (onFinished));
    scanThread->startThread();
}

void PluginManager::cancelScan()
{
    if (scanThread != nullptr) { scanThread->signalThreadShouldExit(); scanThread->stopThread (scanTimeoutMs + 5000); }
    scanning.store (false);
}

std::shared_ptr<PluginEffect> PluginManager::instantiate (const juce::PluginDescription& desc, double sampleRate, int blockSize, juce::String& error)
{
    auto instance = formats.createPluginInstance (desc, sampleRate, blockSize, error);
    if (instance == nullptr)
    {
        if (error.isEmpty()) error = "Could not load " + desc.name;
        return nullptr;
    }
    auto fx = std::make_shared<PluginEffect> (std::move (instance), desc);
    fx->prepare (sampleRate, blockSize);
    return fx;
}

void PluginManager::save()
{
    if (auto xml = known.createXml())
    {
        settings.getParentDirectory().createDirectory();
        xml->writeTo (settings);
    }
}

void PluginManager::load()
{
    if (auto xml = juce::parseXML (settings))
        known.recreateFromXml (*xml);
}

} // namespace beatmaker::plugins
