#include "SampleProjects.h"
#include "LoopLibrary.h"
#include <Elastic.h>
#include <MixerCommands.h>
#include <dsp/DrumKitFactory.h>
#include <dsp/Instrument.h>

namespace beatmaker::persistence
{

using namespace model;

std::vector<SampleProjects::Info> SampleProjects::list()
{
    return { { "Lo-fi Beat", "85 BPM: drums, conformed bass and pad loops, FM keys, a reverb bus, sections and automation." },
             { "Synth Sketch", "110 BPM: bass, electric piano chords and a wavetable arp over the drum machine, with a delay send." },
             { "Podcast Intro", "120 BPM: two audio tracks with a riser and a drum loop, fades, clip gain, EQ and compression." } };
}

namespace
{
    struct Builder
    {
        Session& s;
        double sr;
        double bpm;
        const juce::File& loops;
        const std::function<std::shared_ptr<const juce::AudioBuffer<float>> (const juce::File&)>& load;
        juce::String& error;

        double beats (double n) const { return n * 60.0 / bpm; }
        juce::int64 samples (double seconds) const { return (juce::int64) std::llround (seconds * sr); }

        int track (const juce::String& name, Track::Type type, juce::Colour colour)
        {
            Track t; t.name = name; t.type = type; t.colour = colour;
            s.execute (std::make_unique<AddTrackCommand> (t));
            return s.getNumTracks() - 1;
        }
        int drums (const juce::String& name, int bars, juce::Colour colour)
        {
            Track t; t.name = name; t.type = Track::Type::instrument; t.instrumentKind = Track::InstrumentKind::drumMachine; t.colour = colour;
            t.drumKit = engine::DrumKitFactory::createDefaultKit (sr);
            s.execute (std::make_unique<AddTrackCommand> (t));
            const int index = s.getNumTracks() - 1;
            PatternClip pc; pc.name = "Beat"; pc.sampleRate = sr; pc.length = samples (beats (4.0 * bars));
            pc.pattern = std::make_shared<const engine::StepPattern> (engine::StepPattern::createDefaultBeat());
            s.execute (std::make_unique<AddPatternClipCommand> (index, pc));
            return index;
        }
        int instrument (const juce::String& name, engine::InstrumentType type, const juce::String& preset, juce::Colour colour)
        {
            Track t; t.name = name; t.type = Track::Type::instrument; t.instrumentKind = Track::InstrumentKind::synth; t.colour = colour;
            t.instrument = engine::Instrument::create (type, sr);
            auto params = engine::Instrument::presets (type)[0];
            for (const auto& p : engine::Instrument::presets (type)) if (p.presetName == preset) params = p;
            t.instrumentParams = std::make_shared<const engine::InstrumentParams> (params);
            s.execute (std::make_unique<AddTrackCommand> (t));
            return s.getNumTracks() - 1;
        }
        void midi (int track, const juce::String& name, double startBeat, double lengthBeats, double loopBeats, std::vector<engine::NoteEvent> notes)
        {
            MidiClip c; c.name = name; c.sampleRate = sr; c.timelineStart = samples (beats (startBeat)); c.length = samples (beats (lengthBeats));
            auto seq = std::make_shared<engine::MidiSequence>(); seq->lengthBeats = loopBeats; seq->notes = std::move (notes); seq->sortNotes();
            c.sequence = seq;
            s.execute (std::make_unique<AddMidiClipCommand> (track, c));
        }
        // A bundled loop placed at `startBeat`, conformed to the session tempo, repeated `times`.
        bool loop (int track, const juce::String& file, double startBeat, int times, engine::StretchMode mode, float gain = 1.0f)
        {
            const auto f = loops.getChildFile (file);
            auto audio = load (f);
            if (audio == nullptr) { error = "Missing bundled loop: " + f.getFullPathName(); return false; }
            const auto info = LoopLibrary::analyse (f, formatManager);
            for (int k = 0; k < times; ++k)
            {
                AudioClip c; c.name = info.name; c.sourceFile = f; c.audio = audio; c.sampleRate = sr; c.length = audio->getNumSamples(); c.gain = gain; c.sourceBpm = info.bpm;
                const double loopBeats = info.bpm > 0.0 ? (double) audio->getNumSamples() / sr * info.bpm / 60.0 : 8.0;
                c.timelineStart = samples (beats (startBeat + k * loopBeats));
                s.execute (std::make_unique<AddClipCommand> (track, c));
                const ClipRef ref { track, ClipRef::Kind::audio, (int) s.getTracks()[(size_t) track].clips.size() - 1 };
                if (info.bpm > 0.0 && std::abs (info.bpm - bpm) > 0.01)
                    s.execute (std::make_unique<SetClipElasticCommand> (s, ref, Elastic::forTempo (s.getTracks()[(size_t) track].clips[(size_t) ref.index], bpm, mode)));
            }
            return true;
        }
        void section (const juce::String& name, double fromBar, double toBar, juce::uint32 colour)
        {
            Marker m; m.name = name; m.isSection = true; m.seconds = beats (fromBar * 4.0); m.endSeconds = beats (toBar * 4.0); m.colour = juce::Colour (colour);
            s.execute (std::make_unique<AddMarkerCommand> (m));
        }
        void marker (const juce::String& name, double bar)
        {
            Marker m; m.name = name; m.seconds = beats (bar * 4.0);
            s.execute (std::make_unique<AddMarkerCommand> (m));
        }
        void insert (int track, int slot, engine::EffectType type, std::initializer_list<std::pair<int, float>> values = {})
        {
            s.execute (std::make_unique<SetInsertCommand> (track, slot, type, sr));
            if (values.size() == 0) return;
            auto p = std::make_shared<engine::InsertParams> (*s.getTrackOrMaster (track)->inserts[(size_t) slot].params);
            for (const auto& v : values) p->values[(size_t) v.first] = v.second;
            s.execute (std::make_unique<SetInsertParamsCommand> (track, slot, p));
        }
        void mix (int track, float gainDb, float pan) { s.execute (std::make_unique<SetTrackMixCommand> (track, juce::Decibels::decibelsToGain (gainDb), pan)); }
        void send (int track, int slot, int bus, float gain) { Send sd; sd.bus = bus; sd.gain = gain; s.execute (std::make_unique<SetSendCommand> (track, slot, sd)); }
        void volumeRamp (int track, double fromBeat, double toBeat, float fromGain, float toGain)
        {
            auto lane = std::make_shared<engine::AutomationLane>();
            lane->param = engine::ParamId::volume();
            lane->points = { { samples (beats (fromBeat)), fromGain }, { samples (beats (toBeat)), toGain } };
            s.execute (std::make_unique<ReplaceAutomationLaneCommand> (track, lane));
        }
        juce::AudioFormatManager formatManager;
        Builder (Session& session, double sampleRate, double tempo, const juce::File& loopsFolder,
                 const std::function<std::shared_ptr<const juce::AudioBuffer<float>> (const juce::File&)>& loader, juce::String& err)
            : s (session), sr (sampleRate), bpm (tempo), loops (loopsFolder), load (loader), error (err) { formatManager.registerBasicFormats(); }
    };

    std::vector<engine::NoteEvent> chord (double start, double length, std::initializer_list<int> pitches, int velocity = 90)
    {
        std::vector<engine::NoteEvent> out;
        for (int p : pitches) out.push_back ({ p, velocity, start, length });
        return out;
    }
}

bool SampleProjects::create (const juce::String& name, Session& session, TransportState& transport, double sr, const juce::File& loopsFolder,
                             const std::function<std::shared_ptr<const juce::AudioBuffer<float>> (const juce::File&)>& loadAudio, juce::String& error)
{
    if (name == "Lo-fi Beat")
    {
        transport.bpm = 85.0; transport.beatsPerBar = 4;
        Builder b (session, sr, 85.0, loopsFolder, loadAudio, error);
        const int drums = b.drums ("Drums", 8, juce::Colour (0xff3498db));
        const int bass = b.track ("Bass", Track::Type::audio, juce::Colour (0xffe67e22));
        if (! b.loop (bass, "Sub Bass Line 120 Am.wav", 4.0, 3, engine::StretchMode::monophonic)) return false;
        const int pad = b.track ("Pad", Track::Type::audio, juce::Colour (0xff9b59b6));
        if (! b.loop (pad, "Synth Pad 120 Am.wav", 0.0, 4, engine::StretchMode::polyphonic, 0.7f)) return false;
        const int keys = b.instrument ("Keys", engine::InstrumentType::fm, "FM Keys", juce::Colour (0xff1abc9c));
        std::vector<engine::NoteEvent> melody;
        const int notes[] = { 69, 72, 76, 74, 72, 69, 67, 69 };
        for (int i = 0; i < 8; ++i) melody.push_back ({ notes[i], 80 + (i % 2) * 15, i * 0.5, 0.45 });
        b.midi (keys, "Melody", 16.0, 16.0, 4.0, melody);
        const int verb = b.track ("Verb", Track::Type::aux, juce::Colour (0xff95a5a6));
        session.execute (std::make_unique<SetTrackRoutingCommand> (verb, 0, -1));
        b.insert (verb, 0, engine::EffectType::convolution, { { engine::ConvolutionEffect::impulse, (float) engine::ConvolutionEffect::hall }, { engine::ConvolutionEffect::mix, 100.0f } });
        b.send (keys, 0, 0, 0.5f); b.send (pad, 0, 0, 0.3f);
        b.insert (drums, 0, engine::EffectType::saturation);
        b.insert (-1, 0, engine::EffectType::limiter);
        b.mix (drums, -3.0f, 0.0f); b.mix (bass, -4.0f, 0.0f); b.mix (pad, -8.0f, 0.0f); b.mix (keys, -6.0f, 0.2f); b.mix (verb, -6.0f, 0.0f);
        b.volumeRamp (pad, 0.0, 16.0, 0.0f, juce::Decibels::decibelsToGain (-8.0f));
        b.section ("Intro", 0, 4, 0xff3498db); b.section ("Verse", 4, 8, 0xff2ecc71);
        b.marker ("Keys in", 4);
        return true;
    }
    if (name == "Synth Sketch")
    {
        transport.bpm = 110.0; transport.beatsPerBar = 4;
        Builder b (session, sr, 110.0, loopsFolder, loadAudio, error);
        const int drums = b.drums ("Drums", 8, juce::Colour (0xff3498db));
        const int bass = b.instrument ("Bass", engine::InstrumentType::bass, "Acid Bass", juce::Colour (0xffe67e22));
        std::vector<engine::NoteEvent> bassline;
        for (int i = 0; i < 8; ++i) bassline.push_back ({ i % 4 == 3 ? 43 : 45, 110, i * 0.5, 0.4 });
        b.midi (bass, "Bassline", 0.0, 32.0, 4.0, bassline);
        const int keys = b.instrument ("Keys", engine::InstrumentType::electricPiano, "Warm EP", juce::Colour (0xff1abc9c));
        auto chords = chord (0.0, 3.5, { 57, 60, 64 }); auto c2 = chord (4.0, 3.5, { 55, 59, 62 });
        chords.insert (chords.end(), c2.begin(), c2.end());
        b.midi (keys, "Chords", 0.0, 32.0, 8.0, chords);
        const int lead = b.instrument ("Lead", engine::InstrumentType::wavetable, "WT Pluck", juce::Colour (0xff9b59b6));
        std::vector<engine::NoteEvent> arp;
        const int pitches[] = { 69, 72, 76, 81, 76, 72 };
        for (int i = 0; i < 16; ++i) arp.push_back ({ pitches[i % 6], 90, i * 0.25, 0.2 });
        b.midi (lead, "Arp", 16.0, 16.0, 4.0, arp);
        const int delay = b.track ("Delay", Track::Type::aux, juce::Colour (0xff95a5a6));
        session.execute (std::make_unique<SetTrackRoutingCommand> (delay, 1, -1));
        b.insert (delay, 0, engine::EffectType::delay, { { engine::DelayEffect::mix, 100.0f }, { engine::DelayEffect::mode, 2.0f } });
        b.send (lead, 0, 1, 0.6f);
        b.insert (-1, 0, engine::EffectType::limiter);
        b.mix (drums, -3.0f, 0.0f); b.mix (bass, -5.0f, 0.0f); b.mix (keys, -7.0f, -0.2f); b.mix (lead, -8.0f, 0.3f); b.mix (delay, -9.0f, 0.0f);
        b.section ("A", 0, 4, 0xff3498db); b.section ("B", 4, 8, 0xffe67e22);
        return true;
    }
    if (name == "Podcast Intro")
    {
        transport.bpm = 120.0; transport.beatsPerBar = 4;
        Builder b (session, sr, 120.0, loopsFolder, loadAudio, error);
        const int riser = b.track ("Riser", Track::Type::audio, juce::Colour (0xff9b59b6));
        if (! b.loop (riser, "Riser FX 120.wav", 0.0, 1, engine::StretchMode::polyphonic, 0.8f)) return false;
        const int beat = b.track ("Beat", Track::Type::audio, juce::Colour (0xff3498db));
        if (! b.loop (beat, "Drum Loop 120.wav", 4.0, 2, engine::StretchMode::rhythmic)) return false;
        const int voice = b.track ("Voice", Track::Type::audio, juce::Colour (0xffe67e22));
        b.insert (voice, 0, engine::EffectType::eq); b.insert (voice, 1, engine::EffectType::compressor);
        b.insert (beat, 0, engine::EffectType::eq);
        b.insert (-1, 0, engine::EffectType::limiter);
        // Fade the beat out at the end
        const auto& last = session.getTracks()[(size_t) beat].clips.back();
        session.execute (std::make_unique<SetClipFadesCommand> (ClipRef { beat, ClipRef::Kind::audio, (int) session.getTracks()[(size_t) beat].clips.size() - 1 },
                                                                0, engine::FadeShape::linear, last.length / 2, engine::FadeShape::equalPower));
        b.mix (riser, -6.0f, 0.0f); b.mix (beat, -4.0f, 0.0f);
        b.marker ("Intro sting", 0); b.marker ("Beat in", 4); b.marker ("Voice", 12);
        return true;
    }
    error = "Unknown sample project: " + name;
    return false;
}

} // namespace beatmaker::persistence
