// A minimal CLAP plugin for the host tests: stereo gain with a sidechain
// input that ducks the signal by the key's peak, one automatable "Gain"
// parameter, 32 samples of latency, and state = the gain value.
#include <clap/clap.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace
{
    struct Fake
    {
        clap_plugin_t plugin {};
        const clap_host_t* host = nullptr;
        double gain = 0.5;
        double pendingGain = 0.5;
    };

    const char* const features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr };
    const clap_plugin_descriptor_t descriptor {
        CLAP_VERSION_INIT, "com.beatmaker.tests.fakegain", "Fake CLAP Gain", "Beat Maker Tests", "", "", "", "1.0.0", "Test gain with sidechain", features };

    Fake& self (const clap_plugin_t* p) { return *static_cast<Fake*> (p->plugin_data); }

    void applyEvents (Fake& f, const clap_input_events_t* in)
    {
        if (in == nullptr) return;
        for (uint32_t i = 0; i < in->size (in); ++i)
        {
            const auto* e = in->get (in, i);
            if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
            {
                const auto* pv = reinterpret_cast<const clap_event_param_value_t*> (e);
                if (pv->param_id == 0) f.gain = pv->value;
            }
        }
    }

    // ---- params
    uint32_t paramsCount (const clap_plugin_t*) { return 1; }
    bool paramsGetInfo (const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
    {
        if (index != 0) return false;
        info->id = 0; info->flags = CLAP_PARAM_IS_AUTOMATABLE; info->cookie = nullptr;
        std::strncpy (info->name, "Gain", CLAP_NAME_SIZE); std::strncpy (info->module, "", CLAP_PATH_SIZE);
        info->min_value = 0.0; info->max_value = 1.0; info->default_value = 0.5;
        return true;
    }
    bool paramsGetValue (const clap_plugin_t* p, clap_id id, double* out) { if (id != 0) return false; *out = self (p).gain; return true; }
    bool paramsValueToText (const clap_plugin_t*, clap_id id, double v, char* out, uint32_t size) { if (id != 0) return false; std::snprintf (out, size, "%.0f %%", v * 100.0); return true; }
    bool paramsTextToValue (const clap_plugin_t*, clap_id id, const char* text, double* out) { if (id != 0) return false; *out = std::atof (text) / 100.0; return true; }
    void paramsFlush (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) { applyEvents (self (p), in); }
    const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

    // ---- audio ports: main stereo in, sidechain stereo in, stereo out
    uint32_t portsCount (const clap_plugin_t*, bool isInput) { return isInput ? 2 : 1; }
    bool portsGet (const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
    {
        if (index >= (isInput ? 2u : 1u)) return false;
        info->id = index;
        std::strncpy (info->name, isInput ? (index == 0 ? "In" : "Sidechain") : "Out", CLAP_NAME_SIZE);
        info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }
    const clap_plugin_audio_ports_t portsExt { portsCount, portsGet };

    // ---- latency
    uint32_t latencyGet (const clap_plugin_t*) { return 32; }
    const clap_plugin_latency_t latencyExt { latencyGet };

    // ---- state
    bool stateSave (const clap_plugin_t* p, const clap_ostream_t* s) { const double g = self (p).gain; return s->write (s, &g, sizeof (g)) == sizeof (g); }
    bool stateLoad (const clap_plugin_t* p, const clap_istream_t* s) { double g = 0.0; if (s->read (s, &g, sizeof (g)) != sizeof (g)) return false; self (p).gain = g; return true; }
    const clap_plugin_state_t stateExt { stateSave, stateLoad };

    // ---- plugin
    bool pluginInit (const clap_plugin_t*) { return true; }
    void pluginDestroy (const clap_plugin_t* p) { delete &self (p); }
    bool pluginActivate (const clap_plugin_t*, double, uint32_t, uint32_t) { return true; }
    void pluginDeactivate (const clap_plugin_t*) {}
    bool pluginStartProcessing (const clap_plugin_t*) { return true; }
    void pluginStopProcessing (const clap_plugin_t*) {}
    void pluginReset (const clap_plugin_t*) {}
    clap_process_status pluginProcess (const clap_plugin_t* p, const clap_process_t* process)
    {
        auto& f = self (p);
        applyEvents (f, process->in_events);
        if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
        float keyPeak = 0.0f;
        if (process->audio_inputs_count >= 2)
            for (uint32_t ch = 0; ch < process->audio_inputs[1].channel_count; ++ch)
                for (uint32_t i = 0; i < process->frames_count; ++i)
                    keyPeak = std::max (keyPeak, std::abs (process->audio_inputs[1].data32[ch][i]));
        const float g = (float) f.gain * (1.0f - std::min (1.0f, keyPeak));
        const auto& in = process->audio_inputs[0];
        auto& out = process->audio_outputs[0];
        for (uint32_t ch = 0; ch < out.channel_count; ++ch)
        {
            const float* src = in.data32[std::min (ch, in.channel_count - 1)];
            for (uint32_t i = 0; i < process->frames_count; ++i) out.data32[ch][i] = src[i] * g;
        }
        return CLAP_PROCESS_CONTINUE;
    }
    const void* pluginGetExtension (const clap_plugin_t*, const char* id)
    {
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &portsExt;
        if (std::strcmp (id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &stateExt;
        return nullptr;
    }
    void pluginOnMainThread (const clap_plugin_t*) {}

    // ---- factory
    uint32_t factoryCount (const clap_plugin_factory_t*) { return 1; }
    const clap_plugin_descriptor_t* factoryDescriptor (const clap_plugin_factory_t*, uint32_t i) { return i == 0 ? &descriptor : nullptr; }
    const clap_plugin_t* factoryCreate (const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
    {
        if (std::strcmp (id, descriptor.id) != 0) return nullptr;
        auto* f = new Fake();
        f->host = host;
        f->plugin.desc = &descriptor;
        f->plugin.plugin_data = f;
        f->plugin.init = pluginInit; f->plugin.destroy = pluginDestroy;
        f->plugin.activate = pluginActivate; f->plugin.deactivate = pluginDeactivate;
        f->plugin.start_processing = pluginStartProcessing; f->plugin.stop_processing = pluginStopProcessing;
        f->plugin.reset = pluginReset; f->plugin.process = pluginProcess;
        f->plugin.get_extension = pluginGetExtension; f->plugin.on_main_thread = pluginOnMainThread;
        return &f->plugin;
    }
    const clap_plugin_factory_t factory { factoryCount, factoryDescriptor, factoryCreate };

    bool entryInit (const char*) { return true; }
    void entryDeinit() {}
    const void* entryGetFactory (const char* id) { return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
