#include "SessionFile.h"
#include <dsp/TimeStretch.h>
#include <PluginHost.h>

namespace beatmaker::persistence
{

using namespace model;

namespace
{
    juce::var obj() { return juce::var (new juce::DynamicObject()); }
    void set (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
    juce::var get (const juce::var& o, const juce::Identifier& k, const juce::var& fallback = {}) { return o.hasProperty (k) ? o[k] : fallback; }
    juce::var arr() { return juce::var (juce::Array<juce::var>()); }
    void push (juce::var& a, const juce::var& v) { a.getArray()->add (v); }
    int count (const juce::var& a) { return a.isArray() ? a.size() : 0; }

    juce::String colourToString (juce::Colour c) { return c.toString(); }
    juce::Colour colourFrom (const juce::var& v, juce::Colour fallback) { return v.isString() ? juce::Colour::fromString (v.toString()) : fallback; }

    // ---- audio file references
    juce::String pathFor (const juce::File& file, const juce::File& bundle)
    {
        if (file == juce::File()) return {};
        return file.isAChildOf (bundle) ? file.getRelativePathFrom (bundle) : file.getFullPathName();
    }
    juce::File fileFor (const juce::String& path, const juce::File& bundle)
    {
        if (path.isEmpty()) return {};
        return juce::File::isAbsolutePath (path) ? juce::File (path) : bundle.getChildFile (path);
    }

    juce::File writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& dir, const juce::String& baseName, juce::String& error)
    {
        dir.createDirectory();
        auto file = dir.getChildFile (juce::File::createLegalFileName (baseName) + ".wav").getNonexistentSibling (false);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream&> (*stream).openedOk()) { error = "Cannot write " + file.getFullPathName(); return {}; }
        auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                                    .withNumChannels (audio.getNumChannels()).withBitsPerSample (32)));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) { error = "Cannot encode " + file.getFileName(); return {}; }
        return file;
    }

    // ---- lanes / params
    juce::var laneToVar (const engine::AutomationLane& lane)
    {
        auto o = obj();
        auto param = obj();
        set (param, "type", (int) lane.param.type); set (param, "index", lane.param.index); set (param, "sub", lane.param.sub);
        set (o, "param", param);
        auto points = arr();
        for (const auto& p : lane.points) { auto pt = arr(); push (pt, (juce::int64) p.time); push (pt, (double) p.value); push (points, pt); }
        set (o, "points", points);
        return o;
    }
    std::shared_ptr<engine::AutomationLane> laneFrom (const juce::var& v)
    {
        auto lane = std::make_shared<engine::AutomationLane>();
        const auto param = get (v, "param");
        lane->param.type = (engine::ParamId::Type) (int) get (param, "type", 0);
        lane->param.index = (int) get (param, "index", 0);
        lane->param.sub = (int) get (param, "sub", 0);
        const auto points = get (v, "points");
        for (int i = 0; i < count (points); ++i)
        {
            const auto pt = points[i];
            if (count (pt) >= 2) lane->points.push_back ({ (juce::int64) pt[0], (float) (double) pt[1] });
        }
        lane->sortPoints();
        return lane;
    }
    juce::var valuesToVar (const std::array<float, 24>& values) { auto a = arr(); for (float v : values) push (a, (double) v); return a; }
    void valuesFrom (const juce::var& a, std::array<float, 24>& values) { for (int i = 0; i < juce::jmin (24, count (a)); ++i) values[(size_t) i] = (float) (double) a[i]; }

    // ---- clips
    juce::var audioClipToVar (const AudioClip& c, const juce::File& bundle, const juce::File& audioDir, juce::String& error)
    {
        auto o = obj();
        set (o, "name", c.name);
        juce::File source = c.sourceFile;
        bool edited = false;
        if (c.audioModified && c.audio != nullptr)
        {
            // Pencil-edited audio exists only in memory: freeze it into the bundle.
            source = writeWav (*c.audio, c.sampleRate, audioDir, c.name + " (edited)", error);
            edited = true;
        }
        else if (source == juce::File() && c.originalAudio() != nullptr)
            source = writeWav (*c.originalAudio(), c.sampleRate, audioDir, c.name, error);   // e.g. loops placed from memory
        set (o, "path", pathFor (source, bundle));
        set (o, "sampleRate", c.sampleRate);
        set (o, "start", c.timelineStart); set (o, "offset", c.sourceOffset); set (o, "length", c.length);
        set (o, "gain", (double) c.gain);
        set (o, "fadeIn", c.fadeIn); set (o, "fadeOut", c.fadeOut);
        set (o, "fadeInShape", (int) c.fadeInShape); set (o, "fadeOutShape", (int) c.fadeOutShape);
        set (o, "sourceBpm", c.sourceBpm);
        if (c.gainLane != nullptr && ! c.gainLane->isEmpty()) set (o, "gainLane", laneToVar (*c.gainLane));
        if (! edited && c.isElastic())
        {
            auto e = obj();
            set (e, "mode", (int) c.elastic.mode); set (e, "ratio", c.elastic.ratio); set (e, "pitch", c.elastic.pitchSemitones);
            auto markers = arr();
            for (const auto& m : c.elastic.markers) { auto pt = arr(); push (pt, m.source); push (pt, m.output); push (markers, pt); }
            set (e, "markers", markers);
            set (o, "elastic", e);
        }
        return o;
    }

    bool audioClipFrom (const juce::var& v, const juce::File& bundle, const LoadContext& ctx, AudioClip& c, juce::StringArray& warnings, bool mayDefer = false)
    {
        c.name = get (v, "name", "Clip").toString();
        c.sourceFile = fileFor (get (v, "path").toString(), bundle);
        c.sampleRate = (double) get (v, "sampleRate", ctx.sampleRate);
        c.timelineStart = (juce::int64) get (v, "start", 0); c.sourceOffset = (juce::int64) get (v, "offset", 0); c.length = (juce::int64) get (v, "length", 0);
        c.gain = (float) (double) get (v, "gain", 1.0);
        c.fadeIn = (juce::int64) get (v, "fadeIn", 0); c.fadeOut = (juce::int64) get (v, "fadeOut", 0);
        c.fadeInShape = (engine::FadeShape) (int) get (v, "fadeInShape", 0); c.fadeOutShape = (engine::FadeShape) (int) get (v, "fadeOutShape", 0);
        c.sourceBpm = (double) get (v, "sourceBpm", 0.0);
        if (v.hasProperty ("gainLane")) c.gainLane = laneFrom (get (v, "gainLane"));

        auto audio = ctx.loadAudio ? ctx.loadAudio (c.sourceFile) : nullptr;
        if (audio == nullptr) { warnings.add ("Missing audio: " + c.sourceFile.getFullPathName() + " (clip " + c.name + " skipped)"); return false; }
        c.sampleRate = ctx.sampleRate;   // loadAudio resamples to the engine rate
        c.audio = audio;
        if (v.hasProperty ("elastic"))
        {
            const auto e = get (v, "elastic");
            c.elastic.mode = (engine::StretchMode) (int) get (e, "mode", 0);
            c.elastic.ratio = (double) get (e, "ratio", 1.0);
            c.elastic.pitchSemitones = (double) get (e, "pitch", 0.0);
            const auto markers = get (e, "markers");
            for (int i = 0; i < count (markers); ++i) if (count (markers[i]) >= 2) c.elastic.markers.push_back ({ (juce::int64) markers[i][0], (juce::int64) markers[i][1] });
            if (c.elastic.isActive())
            {
                c.sourceAudio = audio;
                if (mayDefer && ctx.deferElasticRenders)
                {
                    c.clampFades();
                    return true;   // pending: audio == sourceAudio, region as saved; the graph skips reads past the buffer
                }
                c.audio = std::make_shared<const juce::AudioBuffer<float>> (engine::TimeStretch::render (*audio, c.sampleRate, c.elastic));
            }
        }
        c.length = juce::jlimit<juce::int64> (1, juce::jmax<juce::int64> (1, c.audio->getNumSamples() - c.sourceOffset), c.length);
        c.clampFades();
        return true;
    }

    juce::var patternClipToVar (const PatternClip& c)
    {
        auto o = obj();
        set (o, "name", c.name); set (o, "sampleRate", c.sampleRate); set (o, "start", c.timelineStart); set (o, "length", c.length);
        set (o, "loopOffset", c.loopOffset); set (o, "gain", (double) c.gain); set (o, "loop", c.loop); set (o, "loopBaseBeats", c.loopBaseBeats);
        if (c.pattern != nullptr)
        {
            auto p = obj();
            set (p, "numSteps", c.pattern->numSteps); set (p, "stepsPerBeat", c.pattern->stepsPerBeat);
            auto rows = arr();
            for (int pad = 0; pad < engine::StepPattern::maxPads; ++pad)
            {
                juce::String row;
                for (int s = 0; s < c.pattern->numSteps; ++s) row += juce::String::toHexString ((int) c.pattern->velocity[(size_t) pad][(size_t) s]).paddedLeft ('0', 2);
                push (rows, row);
            }
            set (p, "rows", rows);
            bool anyHeld = false;
            for (int pad = 0; pad < engine::StepPattern::maxPads && ! anyHeld; ++pad) for (int s = 0; s < c.pattern->numSteps; ++s) if (c.pattern->get (pad, s) > 0 && c.pattern->getLength (pad, s) > 1) { anyHeld = true; break; }
            if (anyHeld)   // held hits (a gate over several steps), same hex rows as the velocities
            {
                auto lengths = arr();
                for (int pad = 0; pad < engine::StepPattern::maxPads; ++pad)
                {
                    juce::String row;
                    for (int s = 0; s < c.pattern->numSteps; ++s) row += juce::String::toHexString (c.pattern->get (pad, s) > 0 ? c.pattern->getLength (pad, s) : 0).paddedLeft ('0', 2);
                    push (lengths, row);
                }
                set (p, "lengths", lengths);
            }
            bool anyLate = false;
            for (int pad = 0; pad < engine::StepPattern::maxPads && ! anyLate; ++pad) for (int s = 0; s < c.pattern->numSteps; ++s) if (c.pattern->get (pad, s) > 0 && c.pattern->getOffset (pad, s) > 0) { anyLate = true; break; }
            if (anyLate)   // micro-timing in quarter steps, one digit per step
            {
                auto offsets = arr();
                for (int pad = 0; pad < engine::StepPattern::maxPads; ++pad)
                {
                    juce::String row;
                    for (int s = 0; s < c.pattern->numSteps; ++s) row += juce::String (c.pattern->get (pad, s) > 0 ? c.pattern->getOffset (pad, s) : 0);
                    push (offsets, row);
                }
                set (p, "offsets", offsets);
            }
            set (o, "pattern", p);
        }
        return o;
    }
    PatternClip patternClipFrom (const juce::var& v)
    {
        PatternClip c;
        c.name = get (v, "name", "Pattern").toString(); c.sampleRate = (double) get (v, "sampleRate", 48000.0);
        c.timelineStart = (juce::int64) get (v, "start", 0); c.length = (juce::int64) get (v, "length", 0);
        c.loopOffset = (juce::int64) get (v, "loopOffset", 0); c.gain = (float) (double) get (v, "gain", 1.0); c.loop = get (v, "loop", true); c.loopBaseBeats = (double) get (v, "loopBaseBeats", 0.0);   // older files looped
        auto pattern = std::make_shared<engine::StepPattern>();
        const auto p = get (v, "pattern");
        pattern->numSteps = juce::jlimit (1, engine::StepPattern::maxSteps, (int) get (p, "numSteps", 16));
        pattern->stepsPerBeat = juce::jlimit (1, 16, (int) get (p, "stepsPerBeat", 4));
        const auto rows = get (p, "rows");
        for (int pad = 0; pad < juce::jmin (engine::StepPattern::maxPads, count (rows)); ++pad)
        {
            const auto row = rows[pad].toString();
            for (int s = 0; s < pattern->numSteps && s * 2 + 1 < row.length(); ++s)
                pattern->velocity[(size_t) pad][(size_t) s] = (std::uint8_t) row.substring (s * 2, s * 2 + 2).getHexValue32();
        }
        const auto lengths = get (p, "lengths");
        for (int pad = 0; pad < juce::jmin (engine::StepPattern::maxPads, count (lengths)); ++pad)
        {
            const auto row = lengths[pad].toString();
            for (int s = 0; s < pattern->numSteps && s * 2 + 1 < row.length(); ++s)
                if (pattern->get (pad, s) > 0) pattern->setLength (pad, s, row.substring (s * 2, s * 2 + 2).getHexValue32());
        }
        const auto offsets = get (p, "offsets");
        for (int pad = 0; pad < juce::jmin (engine::StepPattern::maxPads, count (offsets)); ++pad)
        {
            const auto row = offsets[pad].toString();
            for (int s = 0; s < pattern->numSteps && s < row.length(); ++s)
                if (pattern->get (pad, s) > 0) pattern->setOffset (pad, s, row.substring (s, s + 1).getIntValue());
        }
        c.pattern = pattern;
        return c;
    }

    juce::var midiClipToVar (const MidiClip& c)
    {
        auto o = obj();
        set (o, "name", c.name); set (o, "sampleRate", c.sampleRate); set (o, "start", c.timelineStart); set (o, "length", c.length);
        set (o, "loopOffset", c.loopOffset); set (o, "gain", (double) c.gain); set (o, "loop", c.loop); set (o, "loopBaseBeats", c.loopBaseBeats);
        if (c.sequence != nullptr)
        {
            auto seq = obj();
            set (seq, "lengthBeats", c.sequence->lengthBeats);
            auto notes = arr();
            for (const auto& n : c.sequence->notes) { auto e = arr(); push (e, n.pitch); push (e, n.velocity); push (e, n.startBeat); push (e, n.lengthBeats); push (notes, e); }
            set (seq, "notes", notes);
            set (o, "sequence", seq);
        }
        return o;
    }
    MidiClip midiClipFrom (const juce::var& v)
    {
        MidiClip c;
        c.name = get (v, "name", "MIDI").toString(); c.sampleRate = (double) get (v, "sampleRate", 48000.0);
        c.timelineStart = (juce::int64) get (v, "start", 0); c.length = (juce::int64) get (v, "length", 0);
        c.loopOffset = (juce::int64) get (v, "loopOffset", 0); c.gain = (float) (double) get (v, "gain", 1.0); c.loop = get (v, "loop", true); c.loopBaseBeats = (double) get (v, "loopBaseBeats", 0.0);   // older files looped
        auto seq = std::make_shared<engine::MidiSequence>();
        const auto s = get (v, "sequence");
        seq->lengthBeats = (double) get (s, "lengthBeats", 8.0);
        const auto notes = get (s, "notes");
        for (int i = 0; i < count (notes); ++i)
            if (count (notes[i]) >= 4) seq->notes.push_back ({ (int) notes[i][0], (int) notes[i][1], (double) notes[i][2], (double) notes[i][3] });
        seq->sortNotes();
        c.sequence = seq;
        return c;
    }

    // ---- tracks
    juce::var trackToVar (const Track& t, const juce::File& bundle, const juce::File& audioDir, juce::String& error)
    {
        auto o = obj();
        set (o, "id", t.id); set (o, "name", t.name); set (o, "type", (int) t.type); set (o, "colour", colourToString (t.colour));
        set (o, "instrumentKind", (int) t.instrumentKind); set (o, "mainPlaylistName", t.mainPlaylistName);
        set (o, "gain", (double) t.gain); set (o, "pan", (double) t.pan); set (o, "mute", t.mute); set (o, "solo", t.solo);
        set (o, "outputBus", t.outputBus); set (o, "inputBus", t.inputBus); set (o, "meterType", (int) t.meterType); set (o, "vcaTrackId", t.vcaTrackId);
        set (o, "automationMode", (int) t.automationMode);
        set (o, "armed", t.armed); set (o, "monitor", t.monitor); set (o, "firstInput", t.firstInput); set (o, "numInputs", t.numInputs);
        set (o, "inputPath", t.inputPath); set (o, "outputPath", t.outputPath); set (o, "delayOffset", t.delayOffset);

        auto clips = arr();
        for (const auto& c : t.clips) push (clips, audioClipToVar (c, bundle, audioDir, error));
        set (o, "clips", clips);
        auto alternates = arr();
        for (const auto& alt : t.alternates)
        {
            auto a = obj(); set (a, "name", alt.name);
            auto altClips = arr();
            for (const auto& c : alt.clips) push (altClips, audioClipToVar (c, bundle, audioDir, error));
            set (a, "clips", altClips);
            push (alternates, a);
        }
        set (o, "alternates", alternates);

        auto patterns = arr();
        for (const auto& c : t.patternClips) push (patterns, patternClipToVar (c));
        set (o, "patternClips", patterns);
        if (t.drumKit != nullptr)
        {
            auto kit = obj();
            set (kit, "name", t.drumKit->name);
            auto pads = arr();
            for (const auto& pad : t.drumKit->pads)
            {
                auto p = obj(); set (p, "name", pad.name); set (p, "gain", (double) pad.gain); set (p, "path", pathFor (juce::File (pad.sourcePath), bundle));
                push (pads, p);
            }
            set (kit, "pads", pads);
            set (o, "drumKit", kit);
        }
        auto midi = arr();
        for (const auto& c : t.midiClips) push (midi, midiClipToVar (c));
        set (o, "midiClips", midi);
        {
            auto rp = obj(); const auto& p = t.midiProps;
            set (rp, "quantize", p.quantize); set (rp, "quantizeBeats", p.quantizeBeats); set (rp, "quantizeStrength", (double) p.quantizeStrength);
            set (rp, "transpose", p.transpose); set (rp, "velocityScale", (double) p.velocityScale); set (rp, "velocityOffset", p.velocityOffset);
            set (rp, "delayMs", p.delayMs); set (rp, "durationScale", (double) p.durationScale);
            set (o, "midiProps", rp);
        }
        if (t.isFrozen())
        {
            auto fz = obj();
            juce::File file = t.freeze.file;
            if (! file.existsAsFile()) file = writeWav (*t.freeze.audio, t.freeze.sampleRate, audioDir, t.name + " (frozen)", error);
            set (fz, "path", pathFor (file, bundle));
            set (o, "freeze", fz);
        }
        if (t.instrumentParams != nullptr)
        {
            auto inst = obj();
            const auto& p = *t.instrumentParams;
            set (inst, "type", (int) p.type); set (inst, "preset", p.presetName); set (inst, "values", valuesToVar (p.values));
            set (inst, "rootNote", p.rootNote); set (inst, "sampleName", p.sampleName); set (inst, "samplePath", pathFor (juce::File (p.samplePath), bundle));
            set (o, "instrument", inst);
        }

        auto inserts = arr();
        for (int slot = 0; slot < (int) t.inserts.size(); ++slot)
        {
            const auto& ins = t.inserts[(size_t) slot];
            if (ins.isEmpty()) continue;
            auto i = obj();
            set (i, "slot", slot); set (i, "type", (int) ins.type); set (i, "bypass", ins.bypass); set (i, "keyBus", ins.keyBus); set (i, "keyListen", ins.keyListen);
            if (ins.params != nullptr) set (i, "values", valuesToVar (ins.params->values));
            if (ins.isPlugin())
            {
                set (i, "pluginIdentifier", ins.pluginIdentifier);
                if (auto* plugin = dynamic_cast<plugins::PluginEffect*> (ins.instance.get()))
                {
                    juce::MemoryBlock state;
                    plugin->getInstance().getStateInformation (state);
                    set (i, "pluginState", state.toBase64Encoding());
                }
            }
            if (auto* conv = dynamic_cast<engine::ConvolutionEffect*> (ins.instance.get()))
            {
                set (i, "impulseName", conv->getCustomImpulseName());
                set (i, "impulsePath", pathFor (juce::File (conv->getCustomImpulsePath()), bundle));
            }
            push (inserts, i);
        }
        set (o, "inserts", inserts);
        auto sends = arr();
        for (const auto& send : t.sends) { auto sv = obj(); set (sv, "bus", send.bus); set (sv, "gain", (double) send.gain); set (sv, "preFader", send.preFader); push (sends, sv); }
        set (o, "sends", sends);
        auto lanes = arr();
        for (const auto& lane : t.automation) if (lane != nullptr && ! lane->isEmpty()) push (lanes, laneToVar (*lane));
        set (o, "automation", lanes);
        return o;
    }

    Track trackFrom (const juce::var& v, const juce::File& bundle, const LoadContext& ctx, juce::StringArray& warnings)
    {
        Track t;
        t.id = (int) get (v, "id", 0); t.name = get (v, "name", "Track").toString(); t.type = (Track::Type) (int) get (v, "type", 0);
        t.colour = colourFrom (get (v, "colour"), t.colour);
        t.instrumentKind = (Track::InstrumentKind) (int) get (v, "instrumentKind", 0); t.mainPlaylistName = get (v, "mainPlaylistName", "").toString();
        t.gain = (float) (double) get (v, "gain", 1.0); t.pan = (float) (double) get (v, "pan", 0.0); t.mute = get (v, "mute", false); t.solo = get (v, "solo", false);
        t.outputBus = (int) get (v, "outputBus", -1); t.inputBus = (int) get (v, "inputBus", -1); t.meterType = (MeterType) (int) get (v, "meterType", 0);
        t.vcaTrackId = (int) get (v, "vcaTrackId", -1); t.automationMode = (AutomationMode) (int) get (v, "automationMode", 1);
        t.armed = get (v, "armed", false); t.monitor = get (v, "monitor", false); t.firstInput = (int) get (v, "firstInput", 0); t.numInputs = (int) get (v, "numInputs", 1);
        t.inputPath = (int) get (v, "inputPath", -1); t.outputPath = (int) get (v, "outputPath", 0); t.delayOffset = (int) get (v, "delayOffset", 0);

        const auto clips = get (v, "clips");
        for (int i = 0; i < count (clips); ++i) { AudioClip c; if (audioClipFrom (clips[i], bundle, ctx, c, warnings, true)) t.clips.push_back (std::move (c)); }
        const auto alternates = get (v, "alternates");
        for (int a = 0; a < count (alternates); ++a)
        {
            Playlist alt; alt.name = get (alternates[a], "name", "").toString();
            const auto altClips = get (alternates[a], "clips");
            for (int i = 0; i < count (altClips); ++i) { AudioClip c; if (audioClipFrom (altClips[i], bundle, ctx, c, warnings)) alt.clips.push_back (std::move (c)); }
            t.alternates.push_back (std::move (alt));
        }
        const auto patterns = get (v, "patternClips");
        for (int i = 0; i < count (patterns); ++i) t.patternClips.push_back (patternClipFrom (patterns[i]));
        if (v.hasProperty ("drumKit"))
        {
            auto kit = std::make_shared<engine::DrumKit>();
            if (ctx.defaultKit) if (auto def = ctx.defaultKit()) *kit = *def;
            const auto kv = get (v, "drumKit");
            kit->name = get (kv, "name", kit->name).toString();
            const auto pads = get (kv, "pads");
            for (int i = 0; i < juce::jmin ((int) kit->pads.size(), count (pads)); ++i)
            {
                auto& pad = kit->pads[(size_t) i];
                pad.gain = (float) (double) get (pads[i], "gain", 1.0);
                const auto path = get (pads[i], "path", "").toString();
                if (path.isNotEmpty())
                {
                    const auto file = fileFor (path, bundle);
                    if (auto audio = ctx.loadAudio ? ctx.loadAudio (file) : nullptr) { pad.audio = audio; pad.name = get (pads[i], "name", pad.name).toString(); pad.sourcePath = file.getFullPathName(); }
                    else warnings.add ("Missing drum sample: " + file.getFullPathName());
                }
            }
            t.drumKit = kit;
        }
        const auto midi = get (v, "midiClips");
        for (int i = 0; i < count (midi); ++i) t.midiClips.push_back (midiClipFrom (midi[i]));
        if (v.hasProperty ("freeze"))
        {
            const auto file = fileFor (get (get (v, "freeze"), "path", "").toString(), bundle);
            if (auto audio = ctx.loadAudio ? ctx.loadAudio (file) : nullptr) { t.freeze.frozen = true; t.freeze.audio = audio; t.freeze.sampleRate = ctx.sampleRate; t.freeze.file = file; }
            else warnings.add ("Missing freeze file: " + file.getFullPathName() + " (track " + t.name + " unfrozen)");
        }
        if (v.hasProperty ("midiProps"))
        {
            const auto rp = get (v, "midiProps"); auto& p = t.midiProps;
            p.quantize = get (rp, "quantize", false); p.quantizeBeats = (double) get (rp, "quantizeBeats", 0.25); p.quantizeStrength = (float) (double) get (rp, "quantizeStrength", 1.0);
            p.transpose = (int) get (rp, "transpose", 0); p.velocityScale = (float) (double) get (rp, "velocityScale", 1.0); p.velocityOffset = (int) get (rp, "velocityOffset", 0);
            p.delayMs = (double) get (rp, "delayMs", 0.0); p.durationScale = (float) (double) get (rp, "durationScale", 1.0);
        }
        if (v.hasProperty ("instrument"))
        {
            const auto iv = get (v, "instrument");
            auto p = std::make_shared<engine::InstrumentParams> (engine::Instrument::defaultParams ((engine::InstrumentType) (int) get (iv, "type", 1)));
            p->presetName = get (iv, "preset", p->presetName).toString();
            valuesFrom (get (iv, "values"), p->values);
            p->rootNote = (int) get (iv, "rootNote", 60); p->sampleName = get (iv, "sampleName", "").toString();
            const auto samplePath = get (iv, "samplePath", "").toString();
            if (samplePath.isNotEmpty())
            {
                const auto file = fileFor (samplePath, bundle);
                if (auto audio = ctx.loadAudio ? ctx.loadAudio (file) : nullptr) { p->sample = audio; p->sampleRate = ctx.sampleRate; p->samplePath = file.getFullPathName(); }
                else warnings.add ("Missing sampler file: " + file.getFullPathName());
            }
            t.instrument = engine::Instrument::create (p->type, ctx.sampleRate);
            t.instrumentParams = p;
        }

        const auto inserts = get (v, "inserts");
        for (int i = 0; i < count (inserts); ++i)
        {
            const auto iv = inserts[i];
            const int slot = (int) get (iv, "slot", i);
            if (! juce::isPositiveAndBelow (slot, Track::numInsertSlots)) continue;
            Insert ins;
            ins.type = (engine::EffectType) (int) get (iv, "type", 0);
            ins.bypass = get (iv, "bypass", false); ins.keyBus = (int) get (iv, "keyBus", -1); ins.keyListen = get (iv, "keyListen", false);
            auto params = std::make_shared<engine::InsertParams> (engine::Effect::defaultParams (ins.type));
            params->type = ins.type;
            valuesFrom (get (iv, "values"), params->values);
            ins.params = params;
            if (ins.type == engine::EffectType::plugin)
            {
                ins.pluginIdentifier = get (iv, "pluginIdentifier", "").toString();
                juce::String error;
                ins.instance = ctx.instantiatePlugin ? ctx.instantiatePlugin (ins.pluginIdentifier, error) : nullptr;
                if (ins.instance == nullptr) { warnings.add ("Plugin not available: " + ins.pluginIdentifier + (error.isNotEmpty() ? " (" + error + ")" : juce::String())); continue; }
                if (auto* plugin = dynamic_cast<plugins::PluginEffect*> (ins.instance.get()))
                {
                    juce::MemoryBlock state;
                    if (state.fromBase64Encoding (get (iv, "pluginState", "").toString()) && state.getSize() > 0)
                        plugin->getInstance().setStateInformation (state.getData(), (int) state.getSize());
                }
            }
            else
            {
                ins.instance = engine::Effect::create (ins.type, ctx.sampleRate);
                if (ins.instance == nullptr) continue;
                if (auto* conv = dynamic_cast<engine::ConvolutionEffect*> (ins.instance.get()))
                {
                    const auto irPath = get (iv, "impulsePath", "").toString();
                    if (irPath.isNotEmpty())
                    {
                        const auto file = fileFor (irPath, bundle);
                        if (auto audio = ctx.loadAudio ? ctx.loadAudio (file) : nullptr) conv->setCustomImpulse (audio, ctx.sampleRate, get (iv, "impulseName", file.getFileNameWithoutExtension()).toString(), file.getFullPathName());
                        else warnings.add ("Missing impulse response: " + file.getFullPathName());
                    }
                }
                ins.instance->paramsChanged (*params);
            }
            t.inserts[(size_t) slot] = std::move (ins);
        }
        const auto sends = get (v, "sends");
        for (int i = 0; i < juce::jmin ((int) t.sends.size(), count (sends)); ++i)
        {
            t.sends[(size_t) i].bus = (int) get (sends[i], "bus", -1);
            t.sends[(size_t) i].gain = (float) (double) get (sends[i], "gain", 1.0);
            t.sends[(size_t) i].preFader = get (sends[i], "preFader", false);
        }
        const auto lanes = get (v, "automation");
        for (int i = 0; i < count (lanes); ++i) t.automation.push_back (laneFrom (lanes[i]));
        return t;
    }

    juce::var markerToVar (const Marker& m)
    {
        auto o = obj();
        set (o, "id", m.id); set (o, "name", m.name); set (o, "seconds", m.seconds); set (o, "endSeconds", m.endSeconds);
        set (o, "colour", colourToString (m.colour)); set (o, "isSection", m.isSection);
        set (o, "recallSelection", m.recallSelection); set (o, "selectionStart", m.selectionStart); set (o, "selectionEnd", m.selectionEnd);
        set (o, "recallZoom", m.recallZoom); set (o, "viewStart", m.viewStartSeconds); set (o, "pixelsPerSecond", m.pixelsPerSecond);
        return o;
    }
    Marker markerFrom (const juce::var& v)
    {
        Marker m;
        m.id = (int) get (v, "id", 0); m.name = get (v, "name", "").toString(); m.seconds = (double) get (v, "seconds", 0.0); m.endSeconds = (double) get (v, "endSeconds", -1.0);
        m.colour = colourFrom (get (v, "colour"), m.colour); m.isSection = get (v, "isSection", false);
        m.recallSelection = get (v, "recallSelection", false); m.selectionStart = (double) get (v, "selectionStart", 0.0); m.selectionEnd = (double) get (v, "selectionEnd", 0.0);
        m.recallZoom = get (v, "recallZoom", false); m.viewStartSeconds = (double) get (v, "viewStart", 0.0); m.pixelsPerSecond = (double) get (v, "pixelsPerSecond", 60.0);
        return m;
    }
}

//==============================================================================

juce::String SessionFile::save (const Session& s, const TransportState& transport, const juce::File& bundle)
{
    if (! bundle.createDirectory()) return "Cannot create " + bundle.getFullPathName();
    const auto audioDir = bundle.getChildFile ("Audio Files");
    juce::String error;

    auto root = obj();
    set (root, "format", "beatmaker-session"); set (root, "version", formatVersion);
    set (root, "bpm", transport.bpm); set (root, "beatsPerBar", transport.beatsPerBar);
    set (root, "loopStart", transport.loopStart); set (root, "loopEnd", transport.loopEnd); set (root, "loopEnabled", transport.loopEnabled);
    set (root, "delayCompensation", s.isDelayCompensationEnabled());
    auto rec = obj();
    const auto& rs = s.getRecordSettings();
    set (rec, "mode", (int) rs.mode); set (rec, "preRoll", rs.preRoll); set (rec, "postRoll", rs.postRoll); set (rec, "preRollSeconds", rs.preRollSeconds); set (rec, "postRollSeconds", rs.postRollSeconds);
    set (root, "record", rec);

    auto io = obj();
    auto ins = arr(), outs = arr(), buses = arr();
    for (const auto& p : s.getIO().inputs)  { auto pv = obj(); set (pv, "name", p.name); set (pv, "firstChannel", p.firstChannel); set (pv, "numChannels", p.numChannels); push (ins, pv); }
    for (const auto& p : s.getIO().outputs) { auto pv = obj(); set (pv, "name", p.name); set (pv, "firstChannel", p.firstChannel); set (pv, "numChannels", p.numChannels); push (outs, pv); }
    for (const auto& b : s.getIO().busNames) push (buses, b);
    set (io, "inputs", ins); set (io, "outputs", outs); set (io, "busNames", buses);
    set (root, "io", io);

    auto groups = arr();
    for (const auto& g : s.getGroups())
    {
        auto gv = obj();
        set (gv, "id", g.id); set (gv, "name", g.name); set (gv, "colour", colourToString (g.colour)); set (gv, "type", (int) g.type); set (gv, "active", g.active);
        auto ids = arr(); for (int id : g.trackIds) push (ids, id); set (gv, "trackIds", ids);
        auto at = obj(); set (at, "volume", g.attributes.volume); set (at, "mute", g.attributes.mute); set (at, "solo", g.attributes.solo); set (at, "pan", g.attributes.pan); set (at, "arm", g.attributes.arm);
        set (gv, "attributes", at);
        push (groups, gv);
    }
    set (root, "groups", groups);
    auto markers = arr();
    for (const auto& m : s.getMarkers()) push (markers, markerToVar (m));
    set (root, "markers", markers);

    set (root, "master", trackToVar (s.getMaster(), bundle, audioDir, error));
    auto tracks = arr();
    for (const auto& t : s.getTracks()) push (tracks, trackToVar (t, bundle, audioDir, error));
    set (root, "tracks", tracks);
    if (error.isNotEmpty()) return error;

    const auto json = juce::JSON::toString (root, false);
    const auto file = jsonFile (bundle);
    // Write through a temp file so a crash mid-save never leaves a half-written session.
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (json) || ! temp.overwriteTargetFileWithTemporary()) return "Cannot write " + file.getFullPathName();
    return {};
}

juce::String SessionFile::load (Session& s, TransportState& transport, const juce::File& bundle, const LoadContext& ctx, juce::StringArray& warnings)
{
    const auto file = jsonFile (bundle);
    if (! file.existsAsFile()) return "Not a session: " + bundle.getFullPathName();
    const auto root = juce::JSON::parse (file);
    if (! root.isObject() || get (root, "format").toString() != "beatmaker-session") return "Not a Beat Maker session: " + file.getFullPathName();
    if ((int) get (root, "version", 1) > formatVersion) return "This session was saved by a newer version of Beat Maker";

    transport.bpm = (double) get (root, "bpm", 120.0); transport.beatsPerBar = (int) get (root, "beatsPerBar", 4);
    transport.loopStart = (juce::int64) get (root, "loopStart", 0); transport.loopEnd = (juce::int64) get (root, "loopEnd", 0); transport.loopEnabled = get (root, "loopEnabled", false);

    LoadSessionCommand::Contents c;
    c.bpm = transport.bpm; c.beatsPerBar = transport.beatsPerBar;
    c.delayCompensation = get (root, "delayCompensation", true);
    const auto rec = get (root, "record");
    c.recordSettings.mode = (RecordMode) (int) get (rec, "mode", 0); c.recordSettings.preRoll = get (rec, "preRoll", false); c.recordSettings.postRoll = get (rec, "postRoll", false);
    c.recordSettings.preRollSeconds = (double) get (rec, "preRollSeconds", 2.0); c.recordSettings.postRollSeconds = (double) get (rec, "postRollSeconds", 2.0);

    const auto io = get (root, "io");
    c.io = IOSetup::createDefault (2, 2);
    if (io.isObject())
    {
        c.io.inputs.clear(); c.io.outputs.clear(); c.io.busNames.clear();
        const auto ins = get (io, "inputs"), outs = get (io, "outputs"), buses = get (io, "busNames");
        for (int i = 0; i < count (ins); ++i)  c.io.inputs.push_back ({ get (ins[i], "name", "").toString(), (int) get (ins[i], "firstChannel", 0), (int) get (ins[i], "numChannels", 2) });
        for (int i = 0; i < count (outs); ++i) c.io.outputs.push_back ({ get (outs[i], "name", "").toString(), (int) get (outs[i], "firstChannel", 0), (int) get (outs[i], "numChannels", 2) });
        for (int i = 0; i < count (buses); ++i) c.io.busNames.push_back (buses[i].toString());
        if (c.io.outputs.empty()) c.io = IOSetup::createDefault (2, 2);
    }
    const auto groups = get (root, "groups");
    for (int i = 0; i < count (groups); ++i)
    {
        const auto gv = groups[i];
        Group g;
        g.id = (int) get (gv, "id", i + 1); g.name = get (gv, "name", "Group").toString(); g.colour = colourFrom (get (gv, "colour"), g.colour);
        g.type = (Group::Type) (int) get (gv, "type", 2); g.active = get (gv, "active", true);
        const auto ids = get (gv, "trackIds"); for (int k = 0; k < count (ids); ++k) g.trackIds.push_back ((int) ids[k]);
        const auto at = get (gv, "attributes");
        g.attributes.volume = get (at, "volume", true); g.attributes.mute = get (at, "mute", true); g.attributes.solo = get (at, "solo", true); g.attributes.pan = get (at, "pan", false); g.attributes.arm = get (at, "arm", false);
        c.groups.push_back (g);
    }
    const auto markers = get (root, "markers");
    for (int i = 0; i < count (markers); ++i) c.markers.push_back (markerFrom (markers[i]));

    c.master = trackFrom (get (root, "master"), bundle, ctx, warnings);
    c.master.type = Track::Type::master;
    const auto tracks = get (root, "tracks");
    for (int i = 0; i < count (tracks); ++i) c.tracks.push_back (trackFrom (tracks[i], bundle, ctx, warnings));
    // Ids must be unique and positive
    int next = 1;
    for (auto& t : c.tracks) if (t.id <= 0) t.id = 100000 + next++;

    s.execute (std::make_unique<LoadSessionCommand> (std::move (c)));
    s.clearHistory();
    return {};
}

} // namespace beatmaker::persistence
