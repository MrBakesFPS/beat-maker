#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <DelayCompensation.h>
#include <MixerCommands.h>
#include <PluginHost.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <graph/AudioGraph.h>

using namespace beatmaker;
using Catch::Matchers::WithinAbs;

namespace
{
    // A minimal hosted "plugin": a gain with one parameter and fixed latency,
    // standing in for a real VST3/LV2 instance.
    class FakeGainPlugin final : public juce::AudioPluginInstance
    {
    public:
        // Hosted plugins expose HostedParameters (with a stable ID) rather than plain parameters.
        class GainParam final : public juce::AudioPluginInstance::HostedParameter
        {
        public:
            juce::String getParameterID() const override { return "gain"; }
            float getValue() const override { return value.load(); }
            void setValue (float v) override { value.store (v); }
            float getDefaultValue() const override { return 0.5f; }
            juce::String getName (int) const override { return "Gain"; }
            juce::String getLabel() const override { return {}; }
            float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }
            float get() const noexcept { return value.load(); }
        private:
            std::atomic<float> value { 0.5f };
        };

        FakeGainPlugin() : AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo()))
        {
            auto p = std::make_unique<GainParam>();
            gain = p.get();
            addHostedParameter (std::move (p));
            setLatencySamples (64);
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "Fake Gain"; d.pluginFormatName = "Fake"; d.manufacturerName = "Tests"; d.uniqueId = 1234; }
        const juce::String getName() const override { return "Fake Gain"; }
        void prepareToPlay (double, int) override { prepared = true; }
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            lastBlockSize = b.getNumSamples();
            b.applyGain (gain->get());
        }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        GainParam* gain = nullptr;
        bool prepared = false;
        int lastBlockSize = 0;
    };

    std::shared_ptr<const juce::AudioBuffer<float>> makeDc (int length, float value)
    {
        juce::AudioBuffer<float> b (1, length);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), value, length);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }
}

TEST_CASE ("PluginEffect wraps a hosted instance: prepare, exact-length processing, latency, automation, name")
{
    auto raw = std::make_unique<FakeGainPlugin>();
    auto* fake = raw.get();
    juce::PluginDescription desc;
    fake->fillInPluginDescription (desc);

    plugins::PluginEffect fx (std::move (raw), desc);
    fx.prepare (48000.0, 512);
    CHECK (fake->prepared);
    CHECK (fx.getType() == engine::EffectType::plugin);
    CHECK (fx.getDisplayName() == "Fake Gain");
    CHECK (fx.getLatencySamples ({}) == 64);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::FloatVectorOperations::fill (buffer.getWritePointer (0), 1.0f, 512);
    juce::FloatVectorOperations::fill (buffer.getWritePointer (1), 1.0f, 512);
    fx.process (buffer, 100, {});
    CHECK (fake->lastBlockSize == 100);                          // the plugin saw exactly numSamples
    CHECK_THAT (buffer.getSample (0, 50), WithinAbs (0.5f, 1e-6));
    CHECK_THAT (buffer.getSample (0, 200), WithinAbs (1.0f, 1e-6));  // beyond numSamples untouched

    fx.setAutomatedParameter (0, 0.25f);
    CHECK_THAT (fake->gain->get(), WithinAbs (0.25f, 1e-6));
    fx.setAutomatedParameter (7, 0.9f);                          // out of range: ignored
    CHECK_THAT (fake->gain->get(), WithinAbs (0.25f, 1e-6));
}

TEST_CASE ("A hosted plugin insert flows through the strip: gain, automation lane, ADC latency, undo keeps the instance")
{
    using namespace model;
    auto raw = std::make_unique<FakeGainPlugin>();
    auto* fake = raw.get();
    juce::PluginDescription desc;
    fake->fillInPluginDescription (desc);
    auto fx = std::make_shared<plugins::PluginEffect> (std::move (raw), desc);
    fx->prepare (44100.0, 8192);

    Session s;
    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    AudioClip c; c.audio = makeDc (10000, 0.8f); c.length = 10000;
    s.execute (std::make_unique<AddClipCommand> (0, c));
    s.execute (std::make_unique<SetPluginInsertCommand> (0, 2, fx, desc.createIdentifierString()));

    const auto& ins = s.getTracks()[0].inserts[2];
    CHECK (ins.isPlugin());
    CHECK (ins.displayName() == "Fake Gain");
    CHECK (ins.pluginIdentifier == desc.createIdentifierString());
    CHECK (DelayCompensation::insertLatency (s.getTracks()[0]) == 64);

    // Automate the plugin's first parameter to 1.0 (unity)
    auto lane = std::make_shared<engine::AutomationLane>();
    lane->param = engine::ParamId::insert (2, 0);
    lane->points = { { 0, 1.0f } };
    s.execute (std::make_unique<ReplaceAutomationLaneCommand> (0, lane));

    engine::Transport tr;
    engine::AudioGraph graph (tr);
    graph.setSnapshot (buildRenderSnapshot (s));
    tr.play();
    juce::AudioBuffer<float> out (2, 256);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 256);
    CHECK_THAT (fake->gain->get(), WithinAbs (1.0f, 1e-6));       // lane applied on the audio thread
    CHECK_THAT (out.getSample (0, 100), WithinAbs (0.8f, 1e-6));  // unity gain through the plugin

    s.undo();   // lane
    s.undo();   // insert
    CHECK (s.getTracks()[0].inserts[2].isEmpty());
    s.redo();
    CHECK (s.getTracks()[0].inserts[2].instance.get() == fx.get());   // same instance, state intact
    graph.collectGarbage();
}

TEST_CASE ("PluginManager: default formats, known-list persistence, child scan protocol")
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bm-plugins-" + juce::Uuid().toString() + ".xml");
    {
        plugins::PluginManager pm (file, juce::File());   // no host executable: scans in-process
        const auto names = pm.getFormatNames();
        CHECK (names.contains ("VST3"));
       #if JUCE_LINUX
        CHECK (names.contains ("LV2"));
       #endif
        CHECK_FALSE (pm.isScanning());

        juce::PluginDescription d;
        d.name = "Remembered"; d.pluginFormatName = "VST3"; d.fileOrIdentifier = "/nowhere/remembered.vst3"; d.uniqueId = 42;
        pm.getKnownPlugins().addType (d);
        pm.getKnownPlugins().addToBlacklist ("/nowhere/crashy.vst3");
        pm.save();
        CHECK (file.existsAsFile());
    }
    {
        plugins::PluginManager pm (file, juce::File());
        REQUIRE (pm.getKnownPlugins().getNumTypes() == 1);
        CHECK (pm.getKnownPlugins().getTypes()[0].name == "Remembered");
        CHECK (pm.getKnownPlugins().getBlacklistedFiles().contains ("/nowhere/crashy.vst3"));
    }
    file.deleteFile();

    // The child scan returns a non-zero code for an unknown format, and a
    // missing plugin file yields no descriptions but a clean exit.
    CHECK (plugins::PluginManager::runScanChild ("NoSuchFormat", "/nowhere") == 2);
    CHECK (plugins::PluginManager::runScanChild ("VST3", "/nowhere/missing.vst3") == 0);
}
