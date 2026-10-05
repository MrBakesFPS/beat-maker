#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ClapHost.h>
#include <PluginHost.h>

using namespace beatmaker;
using Catch::Matchers::WithinAbs;

namespace
{
    juce::File fakeClap() { return juce::File (BEATMAKER_FAKE_CLAP_PATH); }
}

TEST_CASE ("CLAP format scans a .clap file and describes its plugins")
{
    REQUIRE (fakeClap().existsAsFile());
    plugins::ClapPluginFormat format;
    CHECK (format.getName() == "CLAP");
    CHECK (format.fileMightContainThisPluginType (fakeClap().getFullPathName()));
    CHECK_FALSE (format.fileMightContainThisPluginType ("/nonexistent/x.clap"));
    CHECK (format.getNameOfPluginFromIdentifier (fakeClap().getFullPathName()) == fakeClap().getFileNameWithoutExtension());

    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, fakeClap().getFullPathName());
    REQUIRE (found.size() == 1);
    const auto& d = *found[0];
    CHECK (d.name == "Fake CLAP Gain");
    CHECK (d.pluginFormatName == "CLAP");
    CHECK (d.manufacturerName == "Beat Maker Tests");
    CHECK (d.numInputChannels == 2);
    CHECK (d.numOutputChannels == 2);
    CHECK_FALSE (d.isInstrument);
    CHECK (format.doesPluginStillExist (d));
    CHECK_FALSE (format.pluginNeedsRescanning (d));

    // Directory search finds it
    const auto paths = format.searchPathsForPlugins (juce::FileSearchPath (fakeClap().getParentDirectory().getFullPathName()), false, false);
    CHECK (paths.contains (fakeClap().getFullPathName()));
   #if JUCE_WINDOWS
    CHECK (format.getDefaultLocationsToSearch().getNumPaths() >= 2);   // %COMMONPROGRAMFILES%\CLAP, %LOCALAPPDATA%\Programs\Common\CLAP
   #else
    CHECK (format.getDefaultLocationsToSearch().getNumPaths() >= 3);   // ~/.clap, /usr/lib/clap, /usr/local/lib/clap
   #endif
}

TEST_CASE ("A CLAP plugin instance processes audio, exposes parameters, latency, state and a sidechain bus")
{
    plugins::ClapPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, fakeClap().getFullPathName());
    REQUIRE (found.size() == 1);

    juce::String error;
    auto instance = format.createInstance (*found[0], 48000.0, 512, error);
    REQUIRE (instance != nullptr);
    CHECK (error.isEmpty());
    CHECK (instance->getName() == "Fake CLAP Gain");
    CHECK (instance->getLatencySamples() == 32);
    CHECK (instance->getBusCount (true) == 2);
    CHECK (instance->getBusCount (false) == 1);
    CHECK (instance->getBus (true, 1)->getName() == "Sidechain");

    // Parameters
    REQUIRE (instance->getParameters().size() == 1);
    auto* gain = instance->getParameters()[0];
    CHECK (gain->getName (32) == "Gain");
    CHECK_THAT (gain->getValue(), WithinAbs (0.5f, 1e-6));
    CHECK (gain->getText (0.25f, 32) == "25 %");
    CHECK (gain->isAutomatable());

    // Process: default gain 0.5
    instance->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (4, 512);   // main + sidechain
    buffer.clear();
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 512);
    juce::MidiBuffer midi;
    instance->processBlock (buffer, midi);
    CHECK_THAT (buffer.getSample (0, 100), WithinAbs (0.5f, 1e-6));

    // A parameter change reaches the plugin as an event in the next block
    gain->setValue (0.25f);
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 512);
    instance->processBlock (buffer, midi);
    CHECK_THAT (buffer.getSample (1, 100), WithinAbs (0.25f, 1e-6));

    // Sidechain: key at 0.6 ducks to 0.25 * 0.4
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 512);
    for (int ch = 2; ch < 4; ++ch) juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.6f, 512);
    instance->processBlock (buffer, midi);
    CHECK_THAT (buffer.getSample (0, 100), WithinAbs (0.1f, 1e-5));

    // State round trip
    gain->setValue (0.8f);
    instance->processBlock (buffer, midi);
    juce::MemoryBlock saved;
    instance->getStateInformation (saved);
    CHECK (saved.getSize() == sizeof (double));
    gain->setValue (0.1f);
    instance->processBlock (buffer, midi);
    CHECK_THAT (gain->getValue(), WithinAbs (0.1f, 1e-6));
    instance->setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK_THAT (gain->getValue(), WithinAbs (0.8f, 1e-6));
    for (int ch = 0; ch < 4; ++ch) juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), ch < 2 ? 1.0f : 0.0f, 512);
    instance->processBlock (buffer, midi);
    CHECK_THAT (buffer.getSample (0, 100), WithinAbs (0.8f, 1e-6));

    CHECK_FALSE (instance->hasEditor());   // the fake has no GUI: the generic editor is used
    instance->releaseResources();
}

TEST_CASE ("The plugin manager lists CLAP among its formats and wraps a CLAP plugin as an insert with a key input")
{
    auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_clap_test_plugins.xml");
    settings.deleteFile();
    plugins::PluginManager manager (settings, juce::File());   // no host executable: in-process scanning
    CHECK (manager.getFormatNames().contains ("CLAP"));

    juce::AudioPluginFormat* clap = nullptr;
    for (auto* f : manager.getFormats().getFormats()) if (f->getName() == "CLAP") clap = f;
    REQUIRE (clap != nullptr);
    juce::OwnedArray<juce::PluginDescription> found;
    CHECK (manager.getKnownPlugins().scanAndAddFile (fakeClap().getFullPathName(), true, found, *clap));
    CHECK (manager.getKnownPlugins().getNumTypes() == 1);
    CHECK (manager.getKnownPlugins().getTypes()[0].pluginFormatName == "CLAP");

    juce::String error;
    auto fx = manager.instantiate (manager.getKnownPlugins().getTypes()[0], 48000.0, 512, error);
    REQUIRE (fx != nullptr);
    CHECK (fx->getDisplayName() == "Fake CLAP Gain");
    CHECK (fx->getLatencySamples ({}) == 32);
    CHECK (fx->acceptsSidechain());
    CHECK (fx->getSidechainChannels() == 2);

    juce::AudioBuffer<float> strip (2, 512);
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (strip.getWritePointer (ch), 1.0f, 512);
    fx->process (strip, 512, {});
    CHECK_THAT (strip.getSample (0, 10), WithinAbs (0.5f, 1e-6));

    juce::AudioBuffer<float> key (2, 512);
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (key.getWritePointer (ch), 0.5f, 512);
    const float* keyPtrs[2] = { key.getReadPointer (0), key.getReadPointer (1) };
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (strip.getWritePointer (ch), 1.0f, 512);
    fx->setSidechain (keyPtrs, 2);
    fx->process (strip, 512, {});
    fx->clearSidechain();
    CHECK_THAT (strip.getSample (1, 10), WithinAbs (0.25f, 1e-6));

    // Automation lane values go through the hosted parameter
    fx->setAutomatedParameter (0, 1.0f);
    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (strip.getWritePointer (ch), 1.0f, 512);
    fx->process (strip, 512, {});
    CHECK_THAT (strip.getSample (0, 10), WithinAbs (1.0f, 1e-6));

    manager.save();
    CHECK (settings.existsAsFile());
    settings.deleteFile();
}
