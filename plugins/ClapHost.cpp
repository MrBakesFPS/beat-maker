#include "ClapHost.h"

#include <clap/clap.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <dlfcn.h>
#include <map>
#include <mutex>

namespace beatmaker::plugins
{

//==============================================================================
// A loaded .clap library, shared by every instance created from it.

class ClapLibrary
{
public:
    static std::shared_ptr<ClapLibrary> open (const juce::File& file, juce::String& error)
    {
        static std::mutex mutex;
        static std::map<juce::String, std::weak_ptr<ClapLibrary>> cache;
        const std::lock_guard<std::mutex> lock (mutex);
        if (auto existing = cache[file.getFullPathName()].lock()) return existing;

        void* handle = dlopen (file.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) { error = "dlopen failed: " + juce::String (dlerror()); return nullptr; }
        auto* entry = static_cast<const clap_plugin_entry_t*> (dlsym (handle, "clap_entry"));
        if (entry == nullptr) { dlclose (handle); error = "no clap_entry symbol"; return nullptr; }
        if (! clap_version_is_compatible (entry->clap_version)) { dlclose (handle); error = "incompatible CLAP version"; return nullptr; }
        if (entry->init != nullptr && ! entry->init (file.getFullPathName().toRawUTF8())) { dlclose (handle); error = "clap_entry.init failed"; return nullptr; }
        auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
        if (factory == nullptr) { if (entry->deinit) entry->deinit(); dlclose (handle); error = "no plugin factory"; return nullptr; }

        auto lib = std::shared_ptr<ClapLibrary> (new ClapLibrary (handle, entry, factory, file));
        cache[file.getFullPathName()] = lib;
        return lib;
    }

    ~ClapLibrary()
    {
        if (entry != nullptr && entry->deinit != nullptr) entry->deinit();
        if (handle != nullptr) dlclose (handle);
    }

    uint32_t getNumPlugins() const { return factory->get_plugin_count (factory); }
    const clap_plugin_descriptor_t* getDescriptor (uint32_t i) const { return factory->get_plugin_descriptor (factory, i); }
    const clap_plugin_t* create (const clap_host_t* host, const char* id) const { return factory->create_plugin (factory, host, id); }
    const juce::File& getFile() const { return file; }

private:
    ClapLibrary (void* h, const clap_plugin_entry_t* e, const clap_plugin_factory_t* f, juce::File fl) : handle (h), entry (e), factory (f), file (std::move (fl)) {}
    void* handle;
    const clap_plugin_entry_t* entry;
    const clap_plugin_factory_t* factory;
    juce::File file;
};

static int clapIdHash (const char* id) { return juce::String (juce::CharPointer_UTF8 (id)).hashCode(); }

static void describe (const clap_plugin_descriptor_t& d, const juce::File& file, juce::PluginDescription& out, int numIn, int numOut)
{
    out.name = juce::CharPointer_UTF8 (d.name);
    out.descriptiveName = d.description != nullptr ? juce::String (juce::CharPointer_UTF8 (d.description)) : out.name;
    out.pluginFormatName = ClapPluginFormat::formatName;
    out.manufacturerName = d.vendor != nullptr ? juce::String (juce::CharPointer_UTF8 (d.vendor)) : juce::String();
    out.version = d.version != nullptr ? juce::String (juce::CharPointer_UTF8 (d.version)) : juce::String();
    out.fileOrIdentifier = file.getFullPathName();
    out.lastFileModTime = file.getLastModificationTime();
    out.lastInfoUpdateTime = juce::Time::getCurrentTime();
    out.uniqueId = clapIdHash (d.id);
    out.deprecatedUid = out.uniqueId;
    out.isInstrument = false;
    if (d.features != nullptr)
        for (const char* const* f = d.features; *f != nullptr; ++f)
            if (juce::String (*f) == CLAP_PLUGIN_FEATURE_INSTRUMENT) out.isInstrument = true;
    out.category = out.isInstrument ? "Instrument" : "Effect";
    out.numInputChannels = numIn;
    out.numOutputChannels = numOut;
}

//==============================================================================
// The instance

class ClapPluginInstance final : public juce::AudioPluginInstance
{
public:
    struct Port { clap_id id = 0; juce::String name; int channels = 2; bool isMain = false; };

    static std::unique_ptr<ClapPluginInstance> create (std::shared_ptr<ClapLibrary> lib, const clap_plugin_descriptor_t* desc, juce::String& error)
    {
        // Ports must be known before the AudioProcessor base is constructed, so
        // probe them with a throwaway instance.
        std::vector<Port> ins, outs;
        {
            clap_host_t probeHost = makeHost (nullptr);
            const clap_plugin_t* probe = lib->create (&probeHost, desc->id);
            if (probe == nullptr || ! probe->init (probe)) { if (probe) probe->destroy (probe); error = "plugin refused to initialise"; return nullptr; }
            readPorts (probe, ins, outs);
            probe->destroy (probe);
        }
        auto instance = std::unique_ptr<ClapPluginInstance> (new ClapPluginInstance (std::move (lib), desc, ins, outs));
        if (! instance->initialise (error)) return nullptr;
        return instance;
    }

    ~ClapPluginInstance() override
    {
        releaseResources();
        if (plugin != nullptr) plugin->destroy (plugin);
    }

    // ---- AudioPluginInstance
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        describe (*descriptor, library->getFile(), d, inputPorts.empty() ? 0 : inputPorts[0].channels, outputPorts.empty() ? 0 : outputPorts[0].channels);
    }
    const juce::String getName() const override { return juce::CharPointer_UTF8 (descriptor->name); }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        for (int i = 0; i < layout.inputBuses.size(); ++i)
        {
            const int want = i < (int) inputPorts.size() ? inputPorts[(size_t) i].channels : 0;
            const int have = layout.inputBuses[i].size();
            if (have != want && ! (i > 0 && have == 0)) return false;   // sidechain ports may be disabled
        }
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            if (layout.outputBuses[i].size() != (i < (int) outputPorts.size() ? outputPorts[(size_t) i].channels : 0)) return false;
        return true;
    }

    using AudioPluginInstance::processBlock;
    void prepareToPlay (double sampleRate, int newBlockSize) override
    {
        releaseResources();
        maxBlock = juce::jmax (1, newBlockSize);
        if (! plugin->activate (plugin, sampleRate, 1, (uint32_t) maxBlock)) return;
        activated = true;
        allocateProcessBuffers();
        refreshLatency();
        refreshParameterValues();
    }

    void releaseResources() override
    {
        if (processing) { plugin->stop_processing (plugin); processing = false; }
        if (activated) { plugin->deactivate (plugin); activated = false; }
    }

    void reset() override { if (plugin != nullptr && plugin->reset != nullptr) plugin->reset (plugin); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        if (! activated || n <= 0 || n > maxBlock) return;
        if (! processing) { if (! plugin->start_processing (plugin)) return; processing = true; }

        // Inputs: bus buffers (or the zero scratch for disabled/missing buses)
        for (size_t p = 0; p < inputPorts.size(); ++p)
        {
            auto bus = getBusBuffer (buffer, true, (int) p);
            for (int ch = 0; ch < inputPorts[p].channels; ++ch)
                inPtrs[p][(size_t) ch] = ch < bus.getNumChannels() ? bus.getWritePointer (ch) : zeros[(size_t) ch].data();
            inBuffers[p].data32 = inPtrs[p].data();
            inBuffers[p].channel_count = (uint32_t) inputPorts[p].channels;
        }
        for (size_t p = 0; p < outputPorts.size(); ++p)
        {
            auto bus = getBusBuffer (buffer, false, (int) p);
            for (int ch = 0; ch < outputPorts[p].channels; ++ch)
                outPtrs[p][(size_t) ch] = ch < bus.getNumChannels() ? bus.getWritePointer (ch) : scratchOut[(size_t) ch].data();
            outBuffers[p].data32 = outPtrs[p].data();
            outBuffers[p].channel_count = (uint32_t) outputPorts[p].channels;
        }

        // In-place ports: CLAP plugins may read the input after writing the
        // output, so give the main output its own scratch and copy back.
        if (! outputPorts.empty() && ! inputPorts.empty())
        {
            for (int ch = 0; ch < outputPorts[0].channels; ++ch) outPtrs[0][(size_t) ch] = scratchOut[(size_t) ch].data();
        }

        pendingEvents.clear();
        drainParameterQueue (n);
        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = (uint32_t) n;
        process.transport = nullptr;
        process.audio_inputs = inBuffers.data();
        process.audio_outputs = outBuffers.data();
        process.audio_inputs_count = (uint32_t) inputPorts.size();
        process.audio_outputs_count = (uint32_t) outputPorts.size();
        process.in_events = &inEvents;
        process.out_events = &outEvents;
        const auto status = plugin->process (plugin, &process);
        steadyTime += n;

        if (! outputPorts.empty() && ! inputPorts.empty())
        {
            auto bus = getBusBuffer (buffer, false, 0);
            for (int ch = 0; ch < juce::jmin (bus.getNumChannels(), outputPorts[0].channels); ++ch)
                juce::FloatVectorOperations::copy (bus.getWritePointer (ch), scratchOut[(size_t) ch].data(), n);
        }
        if (status == CLAP_PROCESS_ERROR) buffer.clear();
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return gui != nullptr && gui->is_api_supported (plugin, CLAP_WINDOW_API_X11, false); }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (state == nullptr) return;
        juce::MemoryOutputStream out (dest, false);
        clap_ostream_t stream { &out, [] (const clap_ostream_t* s, const void* data, uint64_t size) -> int64_t
                                {
                                    return static_cast<juce::MemoryOutputStream*> (s->ctx)->write (data, (size_t) size) ? (int64_t) size : -1;
                                } };
        state->save (plugin, &stream);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (state == nullptr) return;
        juce::MemoryInputStream in (data, (size_t) size, false);
        clap_istream_t stream { &in, [] (const clap_istream_t* s, void* buffer, uint64_t sz) -> int64_t
                                {
                                    return (int64_t) static_cast<juce::MemoryInputStream*> (s->ctx)->read (buffer, (int) sz);
                                } };
        state->load (plugin, &stream);
        refreshParameterValues();
        refreshLatency();
    }

    // ---- CLAP specifics used by the editor
    const clap_plugin_t* getPlugin() const noexcept { return plugin; }
    const clap_plugin_gui_t* getGui() const noexcept { return gui; }
    std::function<void (int, int)> onGuiResizeRequest;

private:
    //==========================================================================
    class Parameter final : public HostedParameter
    {
    public:
        Parameter (ClapPluginInstance& o, const clap_param_info_t& i) : owner (o), info (i), value (i.default_value) {}
        float getValue() const override { return toNormalised (value.load()); }
        void setValue (float normalised) override
        {
            const double plain = fromNormalised (normalised);
            value.store (plain);
            owner.queueParameterChange (info.id, info.cookie, plain);
        }
        float getDefaultValue() const override { return toNormalised (info.default_value); }
        juce::String getName (int maxLength) const override { return juce::String (juce::CharPointer_UTF8 (info.name)).substring (0, maxLength); }
        juce::String getLabel() const override { return {}; }
        juce::String getParameterID() const override { return juce::String ((int) info.id); }
        float getValueForText (const juce::String& text) const override { return toNormalised (owner.textToValue (info.id, text, fromNormalised (0.0f))); }
        juce::String getText (float normalised, int maxLength) const override { return owner.valueToText (info.id, fromNormalised (normalised)).substring (0, maxLength); }
        bool isAutomatable() const override { return (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
        bool isDiscrete() const override { return (info.flags & CLAP_PARAM_IS_STEPPED) != 0; }
        int getNumSteps() const override { return isDiscrete() ? (int) (info.max_value - info.min_value) + 1 : AudioProcessor::getDefaultNumParameterSteps(); }

        clap_id getId() const noexcept { return info.id; }
        void setPlainSilently (double v) noexcept { value.store (v); }
        double getPlain() const noexcept { return value.load(); }

    private:
        float toNormalised (double plain) const { const double range = info.max_value - info.min_value; return range > 0.0 ? (float) juce::jlimit (0.0, 1.0, (plain - info.min_value) / range) : 0.0f; }
        double fromNormalised (float n) const { return info.min_value + (double) juce::jlimit (0.0f, 1.0f, n) * (info.max_value - info.min_value); }
        ClapPluginInstance& owner;
        clap_param_info_t info;
        std::atomic<double> value;
    };

    static juce::AudioProcessor::BusesProperties busesFor (const std::vector<Port>& ins, const std::vector<Port>& outs)
    {
        BusesProperties props;
        auto set = [] (int channels) { return channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::canonicalChannelSet (channels); };
        for (const auto& p : ins)  props.addBus (true, p.name, set (p.channels), true);
        for (const auto& p : outs) props.addBus (false, p.name, set (p.channels), true);
        return props;
    }

    ClapPluginInstance (std::shared_ptr<ClapLibrary> lib, const clap_plugin_descriptor_t* desc, std::vector<Port> ins, std::vector<Port> outs)
        : AudioPluginInstance (busesFor (ins, outs)), library (std::move (lib)), descriptor (desc), inputPorts (std::move (ins)), outputPorts (std::move (outs))
    {
        host = makeHost (this);
        inEvents = { this, [] (const clap_input_events_t* l) { return (uint32_t) static_cast<ClapPluginInstance*> (l->ctx)->pendingEvents.size(); },
                           [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t*
                           {
                               auto& self = *static_cast<ClapPluginInstance*> (l->ctx);
                               return i < self.pendingEvents.size() ? &self.pendingEvents[i].header : nullptr;
                           } };
        outEvents = { this, [] (const clap_output_events_t* l, const clap_event_header_t* e)
                            {
                                static_cast<ClapPluginInstance*> (l->ctx)->handleOutputEvent (e);
                                return true;
                            } };
        queuedEvents.resize (512);
    }

    bool initialise (juce::String& error)
    {
        plugin = library->create (&host, descriptor->id);
        if (plugin == nullptr) { error = "create_plugin failed"; return false; }
        if (! plugin->init (plugin)) { error = "plugin init failed"; return false; }
        params  = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state   = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
        latency = static_cast<const clap_plugin_latency_t*> (plugin->get_extension (plugin, CLAP_EXT_LATENCY));
        gui     = static_cast<const clap_plugin_gui_t*> (plugin->get_extension (plugin, CLAP_EXT_GUI));

        if (params != nullptr)
            for (uint32_t i = 0; i < params->count (plugin); ++i)
            {
                clap_param_info_t info {};
                if (! params->get_info (plugin, i, &info) || (info.flags & CLAP_PARAM_IS_HIDDEN) != 0) continue;
                auto p = std::make_unique<Parameter> (*this, info);
                double v = info.default_value;
                if (params->get_value (plugin, info.id, &v)) p->setPlainSilently (v);
                hostedParams.push_back (p.get());
                addHostedParameter (std::move (p));
            }
        return true;
    }

    static void readPorts (const clap_plugin_t* p, std::vector<Port>& ins, std::vector<Port>& outs)
    {
        auto* ports = static_cast<const clap_plugin_audio_ports_t*> (p->get_extension (p, CLAP_EXT_AUDIO_PORTS));
        auto read = [&] (bool isInput, std::vector<Port>& out)
        {
            if (ports == nullptr) { if (! isInput || true) out.push_back ({ 0, isInput ? "In" : "Out", 2, true }); return; }
            const uint32_t count = ports->count (p, isInput);
            for (uint32_t i = 0; i < count; ++i)
            {
                clap_audio_port_info_t info {};
                if (! ports->get (p, i, isInput, &info)) continue;
                Port port;
                port.id = info.id;
                port.name = juce::String (juce::CharPointer_UTF8 (info.name));
                if (port.name.isEmpty()) port.name = isInput ? (i == 0 ? "In" : "Sidechain") : "Out";
                port.channels = juce::jlimit (1, 8, (int) info.channel_count);
                port.isMain = (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0 || i == 0;
                out.push_back (port);
            }
        };
        read (true, ins);
        read (false, outs);
    }

    void allocateProcessBuffers()
    {
        inBuffers.assign (inputPorts.size(), clap_audio_buffer_t {});
        outBuffers.assign (outputPorts.size(), clap_audio_buffer_t {});
        inPtrs.assign (inputPorts.size(), {});
        outPtrs.assign (outputPorts.size(), {});
        int maxChannels = 2;
        for (size_t p = 0; p < inputPorts.size(); ++p)  { inPtrs[p].assign ((size_t) inputPorts[p].channels, nullptr); maxChannels = juce::jmax (maxChannels, inputPorts[p].channels); }
        for (size_t p = 0; p < outputPorts.size(); ++p) { outPtrs[p].assign ((size_t) outputPorts[p].channels, nullptr); maxChannels = juce::jmax (maxChannels, outputPorts[p].channels); }
        zeros.assign ((size_t) maxChannels, std::vector<float> ((size_t) maxBlock, 0.0f));
        scratchOut.assign ((size_t) maxChannels, std::vector<float> ((size_t) maxBlock, 0.0f));
        pendingEvents.reserve (512);
    }

    void refreshLatency()
    {
        const int l = latency != nullptr ? (int) latency->get (plugin) : 0;
        setLatencySamples (l);
    }

    void refreshParameterValues()
    {
        if (params == nullptr) return;
        for (auto* p : hostedParams)
        {
            double v = 0.0;
            if (params->get_value (plugin, p->getId(), &v)) p->setPlainSilently (v);
        }
    }

    juce::String valueToText (clap_id id, double plain) const
    {
        char text[128] = {};
        if (params != nullptr && params->value_to_text (plugin, id, plain, text, sizeof (text))) return juce::String (juce::CharPointer_UTF8 (text));
        return juce::String (plain, 2);
    }
    double textToValue (clap_id id, const juce::String& text, double fallback) const
    {
        double v = fallback;
        if (params != nullptr && params->text_to_value (plugin, id, text.toRawUTF8(), &v)) return v;
        return text.getDoubleValue();
    }

    // UI -> audio thread parameter changes: a lock-free queue drained into the next process() call,
    // or flushed straight away when the plugin is not active.
    void queueParameterChange (clap_id id, void* cookie, double plain)
    {
        clap_event_param_value_t e {};
        e.header.size = sizeof (e); e.header.time = 0; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID; e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.param_id = id; e.cookie = cookie; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.value = plain;
        if (! activated)
        {
            if (params != nullptr) { pendingEvents.assign (1, e); params->flush (plugin, &inEvents, &outEvents); pendingEvents.clear(); }
            return;
        }
        const auto scope = parameterQueue.write (1);
        if (scope.blockSize1 == 1) queuedEvents[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 == 1) queuedEvents[(size_t) scope.startIndex2] = e;
    }

    void drainParameterQueue (int)
    {
        const auto scope = parameterQueue.read (parameterQueue.getNumReady());
        auto take = [this] (int start, int count) { for (int i = 0; i < count; ++i) pendingEvents.push_back (queuedEvents[(size_t) (start + i)]); };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    void handleOutputEvent (const clap_event_header_t* e)
    {
        if (e == nullptr || e->space_id != CLAP_CORE_EVENT_SPACE_ID || e->type != CLAP_EVENT_PARAM_VALUE) return;
        auto* pv = reinterpret_cast<const clap_event_param_value_t*> (e);
        for (auto* p : hostedParams) if (p->getId() == pv->param_id) { p->setPlainSilently (pv->value); break; }
    }

    //==========================================================================
    // Host side

    static ClapPluginInstance* self (const clap_host_t* h) { return static_cast<ClapPluginInstance*> (h->host_data); }

    static clap_host_t makeHost (ClapPluginInstance* instance)
    {
        clap_host_t h {};
        h.clap_version = CLAP_VERSION_INIT;
        h.host_data = instance;
        h.name = "Beat Maker"; h.vendor = "Beat Maker"; h.url = ""; h.version = "0.1";
        h.get_extension = [] (const clap_host_t* host, const char* id) -> const void*
        {
            const juce::String ext (id);
            if (ext == CLAP_EXT_LOG)          return &hostLog;
            if (ext == CLAP_EXT_THREAD_CHECK) return &hostThreadCheck;
            if (ext == CLAP_EXT_PARAMS)       return &hostParams;
            if (ext == CLAP_EXT_LATENCY)      return &hostLatency;
            if (ext == CLAP_EXT_STATE)        return &hostState;
            if (ext == CLAP_EXT_AUDIO_PORTS)  return &hostAudioPorts;
            if (ext == CLAP_EXT_GUI)          return &hostGui;
            juce::ignoreUnused (host);
            return nullptr;
        };
        h.request_restart = [] (const clap_host_t*) {};
        h.request_process = [] (const clap_host_t*) {};
        h.request_callback = [] (const clap_host_t* host)
        {
            if (auto* s = self (host))
                juce::MessageManager::callAsync ([p = s->plugin] { if (p != nullptr && p->on_main_thread != nullptr) p->on_main_thread (p); });
        };
        return h;
    }

    static bool isMessageThread()
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm == nullptr || mm->isThisTheMessageThread();
    }

    static const clap_host_log_t hostLog;
    static const clap_host_thread_check_t hostThreadCheck;
    static const clap_host_params_t hostParams;
    static const clap_host_latency_t hostLatency;
    static const clap_host_state_t hostState;
    static const clap_host_audio_ports_t hostAudioPorts;
    static const clap_host_gui_t hostGui;

    std::shared_ptr<ClapLibrary> library;
    const clap_plugin_descriptor_t* descriptor;
    std::vector<Port> inputPorts, outputPorts;
    clap_host_t host {};
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_latency_t* latency = nullptr;
    const clap_plugin_gui_t* gui = nullptr;
    std::vector<Parameter*> hostedParams;

    bool activated = false, processing = false;
    int maxBlock = 512;
    int64_t steadyTime = 0;
    std::vector<clap_audio_buffer_t> inBuffers, outBuffers;
    std::vector<std::vector<float*>> inPtrs, outPtrs;
    std::vector<std::vector<float>> zeros, scratchOut;
    std::vector<clap_event_param_value_t> pendingEvents, queuedEvents;
    juce::AbstractFifo parameterQueue { 512 };
    clap_input_events_t inEvents {};
    clap_output_events_t outEvents {};
};

// Host extension tables (defined after the class so they can reach its members)
const clap_host_log_t ClapPluginInstance::hostLog { [] (const clap_host_t*, clap_log_severity, const char* msg) { juce::Logger::writeToLog ("CLAP: " + juce::String (msg)); } };
const clap_host_thread_check_t ClapPluginInstance::hostThreadCheck { [] (const clap_host_t*) { return isMessageThread(); },
                                                                     [] (const clap_host_t*) { return ! isMessageThread(); } };
const clap_host_params_t ClapPluginInstance::hostParams {
    [] (const clap_host_t* host, clap_param_rescan_flags) { if (auto* s = self (host)) s->refreshParameterValues(); },
    [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
    [] (const clap_host_t* host)
    {
        if (auto* s = self (host); s != nullptr && ! s->activated && s->params != nullptr)
        { s->pendingEvents.clear(); s->params->flush (s->plugin, &s->inEvents, &s->outEvents); }
    } };
const clap_host_latency_t ClapPluginInstance::hostLatency { [] (const clap_host_t* host) { if (auto* s = self (host)) s->refreshLatency(); } };
const clap_host_state_t ClapPluginInstance::hostState { [] (const clap_host_t* host) { if (auto* s = self (host)) s->updateHostDisplay(); } };
const clap_host_audio_ports_t ClapPluginInstance::hostAudioPorts { [] (const clap_host_t*, uint32_t) { return false; }, [] (const clap_host_t*, uint32_t) {} };
const clap_host_gui_t ClapPluginInstance::hostGui {
    [] (const clap_host_t*) {},
    [] (const clap_host_t* host, uint32_t w, uint32_t h) { if (auto* s = self (host); s != nullptr && s->onGuiResizeRequest) { s->onGuiResizeRequest ((int) w, (int) h); return true; } return false; },
    [] (const clap_host_t*) { return false; },
    [] (const clap_host_t*) { return false; },
    [] (const clap_host_t*, bool) {} };

//==============================================================================
// Editor: the plugin's X11 window embedded in a JUCE component

class ClapEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ClapEditor (ClapPluginInstance& p) : AudioProcessorEditor (p), instance (p), embed (false, false)
    {
        addAndMakeVisible (embed);
        auto* gui = instance.getGui();
        auto* plugin = instance.getPlugin();
        created = gui->create (plugin, CLAP_WINDOW_API_X11, false);
        uint32_t w = 400, h = 300;
        if (created)
        {
            gui->set_scale (plugin, 1.0);
            clap_window_t window {};
            window.api = CLAP_WINDOW_API_X11;
            window.x11 = (clap_xwnd) embed.getHostWindowID();
            gui->set_parent (plugin, &window);
            gui->get_size (plugin, &w, &h);
            gui->show (plugin);
            instance.onGuiResizeRequest = [this] (int newW, int newH) { setSize (newW, newH); };
        }
        setResizable (created && gui->can_resize (plugin), false);
        setSize ((int) w, (int) h);
    }

    ~ClapEditor() override
    {
        instance.onGuiResizeRequest = nullptr;
        if (created)
        {
            auto* gui = instance.getGui();
            gui->hide (instance.getPlugin());
            gui->destroy (instance.getPlugin());
        }
    }

    void resized() override
    {
        embed.setBounds (getLocalBounds());
        if (created && instance.getGui()->can_resize (instance.getPlugin()))
            instance.getGui()->set_size (instance.getPlugin(), (uint32_t) getWidth(), (uint32_t) getHeight());
    }

private:
    ClapPluginInstance& instance;
    juce::XEmbedComponent embed;
    bool created = false;
};

juce::AudioProcessorEditor* ClapPluginInstance::createEditor() { return hasEditor() ? new ClapEditor (*this) : nullptr; }

//==============================================================================
// Format

void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier)
{
    if (! fileMightContainThisPluginType (fileOrIdentifier)) return;
    juce::String error;
    auto lib = ClapLibrary::open (juce::File (fileOrIdentifier), error);
    if (lib == nullptr) return;
    for (uint32_t i = 0; i < lib->getNumPlugins(); ++i)
    {
        const auto* d = lib->getDescriptor (i);
        if (d == nullptr || d->id == nullptr) continue;
        auto instance = ClapPluginInstance::create (lib, d, error);
        auto desc = std::make_unique<juce::PluginDescription>();
        if (instance != nullptr) instance->fillInPluginDescription (*desc);
        else describe (*d, lib->getFile(), *desc, 2, 2);
        results.add (desc.release());
    }
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    const juce::File f (fileOrIdentifier);
    return f.hasFileExtension ("clap") && f.exists();
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File::createFileWithoutCheckingPath (fileOrIdentifier).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& d) { return juce::File (d.fileOrIdentifier).exists(); }

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& paths, bool recursive, bool)
{
    juce::StringArray found;
    for (int i = 0; i < paths.getNumPaths(); ++i)
    {
        const auto dir = paths[i];
        if (! dir.isDirectory()) continue;
        for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, recursive, "*.clap"))
            found.add (f.getFullPathName());
    }
    return found;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath path;
    path.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".clap"));
    path.add (juce::File ("/usr/lib/clap"));
    path.add (juce::File ("/usr/local/lib/clap"));
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("CLAP_PATH", {}); env.isNotEmpty())
        for (const auto& p : juce::StringArray::fromTokens (env, ":", {}))
            if (p.isNotEmpty()) path.add (juce::File (p));
    return path;
}

std::unique_ptr<juce::AudioPluginInstance> ClapPluginFormat::createInstance (const juce::PluginDescription& desc, double sampleRate, int blockSize, juce::String& error)
{
    if (desc.pluginFormatName != formatName) { error = "not a CLAP description"; return nullptr; }
    auto lib = ClapLibrary::open (juce::File (desc.fileOrIdentifier), error);
    if (lib == nullptr) return nullptr;
    for (uint32_t i = 0; i < lib->getNumPlugins(); ++i)
    {
        const auto* d = lib->getDescriptor (i);
        if (d == nullptr || d->id == nullptr) continue;
        if (clapIdHash (d->id) != desc.uniqueId && juce::String (juce::CharPointer_UTF8 (d->name)) != desc.name) continue;
        auto instance = ClapPluginInstance::create (lib, d, error);
        if (instance != nullptr) instance->prepareToPlay (sampleRate, blockSize);
        return instance;
    }
    error = "plugin not found in " + desc.fileOrIdentifier;
    return nullptr;
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& desc, double sampleRate, int blockSize, PluginCreationCallback callback)
{
    juce::String error;
    auto instance = createInstance (desc, sampleRate, blockSize, error);
    callback (std::move (instance), error);
}

void addAllPluginFormats (juce::AudioPluginFormatManager& fm)
{
    juce::addDefaultFormatsToManager (fm);
    fm.addFormat (std::make_unique<ClapPluginFormat>());
}

} // namespace beatmaker::plugins
