#include "Instrument.h"
#include <algorithm>
#include "VoiceHelpers.h"
#include <array>
#include <cmath>
#include <vector>

namespace beatmaker::engine
{

using namespace dsp;

//==============================================================================
// Metadata

const std::vector<InstrumentType>& Instrument::availableTypes()
{
    static const std::vector<InstrumentType> types { InstrumentType::subtractive, InstrumentType::fm, InstrumentType::wavetable,
                                                     InstrumentType::stack, InstrumentType::chip, InstrumentType::vox,
                                                     InstrumentType::sampler, InstrumentType::piano, InstrumentType::electricPiano, InstrumentType::organ,
                                                     InstrumentType::mallets, InstrumentType::pluck, InstrumentType::strings,
                                                     InstrumentType::brass, InstrumentType::flute, InstrumentType::bass };
    return types;
}

const char* Instrument::typeName (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::none:          return "None";
        case InstrumentType::subtractive:   return "Synth";
        case InstrumentType::fm:            return "FM Synth";
        case InstrumentType::wavetable:     return "Wavetable";
        case InstrumentType::sampler:       return "Sampler";
        case InstrumentType::electricPiano: return "Electric Piano";
        case InstrumentType::bass:          return "Bass";
        case InstrumentType::pluck:         return "Pluck";
        case InstrumentType::organ:         return "Organ";
        case InstrumentType::stack:         return "Stack";
        case InstrumentType::chip:          return "Chip";
        case InstrumentType::vox:           return "Vox";
        case InstrumentType::piano:         return "Piano";
        case InstrumentType::strings:       return "Strings";
        case InstrumentType::mallets:       return "Mallets";
        case InstrumentType::brass:         return "Brass";
        case InstrumentType::flute:         return "Flute";
    }
    return "";
}

const char* Instrument::typeCategory (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::none:          return "";
        case InstrumentType::subtractive:   return "Synths";
        case InstrumentType::fm:            return "Synths";
        case InstrumentType::wavetable:     return "Synths";
        case InstrumentType::sampler:       return "Samplers";
        case InstrumentType::electricPiano: return "Keys";
        case InstrumentType::bass:          return "Bass";
        case InstrumentType::pluck:         return "Strings";
        case InstrumentType::organ:         return "Keys";
        case InstrumentType::stack:         return "Synths";
        case InstrumentType::chip:          return "Synths";
        case InstrumentType::vox:           return "Synths";
        case InstrumentType::piano:         return "Keys";
        case InstrumentType::strings:       return "Strings";
        case InstrumentType::mallets:       return "Mallets";
        case InstrumentType::brass:         return "Brass";
        case InstrumentType::flute:         return "Winds";
    }
    return "";
}

const char* Instrument::typeDescription (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::none:          return "";
        case InstrumentType::subtractive:   return "Polyphonic subtractive synth: saw, square, triangle or sine, a detuned second oscillator, a resonant low-pass with its own envelope, ADSR. Leads, pads, plucks and chords.";
        case InstrumentType::fm:            return "Two-operator FM with a decaying modulation index and feedback. Bells, electric keys, metallic plucks and punchy basses.";
        case InstrumentType::wavetable:     return "Two detuned oscillators morphing across band-limited wavetables, with a filter envelope. Evolving pads, digital leads and textures.";
        case InstrumentType::sampler:       return "Plays an audio file across the keyboard, pitched around C3: tune, one-shot or loop, ADSR and a filter. Drop a file onto the track to load it.";
        case InstrumentType::electricPiano: return "A tine electric piano: velocity-dependent brightness, an inharmonic bell partial, per-note decay and tremolo.";
        case InstrumentType::bass:          return "Monophonic bass with last-note priority, legato, glide, a sub oscillator, a filter envelope and drive.";
        case InstrumentType::pluck:         return "A plucked string, physically modelled: brightness and pick position shape the attack, damping and decay the ring, body adds resonance. Guitars, harps, kotos, dulcimers.";
        case InstrumentType::organ:         return "A drawbar organ: nine drawbars over a sine per harmonic, percussion on the second harmonic, key click and vibrato. Jazz, gospel, church and rock organs.";
        case InstrumentType::stack:         return "Up to seven detuned saws per note spread across the stereo field, through a resonant filter with envelope. Trance leads, wide pads and stacked chords.";
        case InstrumentType::chip:          return "An 8-bit sound chip: thin pulses, a stepped triangle and noise, bit-crushed, with vibrato and the chip arpeggio trick that turns one note into a chord. Game melodies, bleeps and retro leads.";
        case InstrumentType::vox:           return "A voice-like synth: a detuned saw through three formant filters that morph between the vowels A, E, I, O and U, with breath noise. Choirs, ahhs and talking pads.";
        case InstrumentType::piano:         return "An acoustic-style piano: stiff, inharmonic strings modelled as decaying partials, two detuned strings per note, hammer hardness that follows velocity and a soft thump. Grand, upright, bright and felt pianos.";
        case InstrumentType::strings:       return "An ensemble of bowed strings: detuned saws with slow movement, delayed vibrato and a bow filter, from a full section to a solo violin, cello or pizzicato.";
        case InstrumentType::mallets:       return "Struck bars and tines modelled by their modes: marimba, vibraphone (with tremolo), glockenspiel and kalimba, with mallet hardness, decay and strike noise.";
        case InstrumentType::brass:         return "A brass section: detuned saws with the filter opening in a blat at the attack, a pitch dip into each note and delayed vibrato. Trumpets, horns, trombones, synth brass.";
        case InstrumentType::flute:         return "A breathy flute: a soft tone with air noise shaped at the note's pitch, a chiff at the start, overblow into the octave and delayed vibrato. Flute, pan pipes, recorder, shakuhachi.";
    }
    return "";
}

std::vector<juce::String> Instrument::categories()
{
    std::vector<juce::String> out;
    for (auto type : availableTypes())
    {
        const juce::String c (typeCategory (type));
        if (c.isNotEmpty() && std::find (out.begin(), out.end(), c) == out.end()) out.push_back (c);
    }
    return out;
}

const std::vector<ParamInfo>& Instrument::paramInfo (InstrumentType t)
{
    static const std::vector<ParamInfo> none;
    static const std::vector<ParamInfo> sub {
        { "Wave",      0.0f, 3.0f,      0.0f,   0.0f,   "" },        // saw, square, triangle, sine
        { "Osc 2",     0.0f, 1.0f,      1.0f,   0.0f,   "" },
        { "Detune",    0.0f, 50.0f,     7.0f,  10.0f,   " ct" },
        { "Cutoff",   20.0f, 20000.0f, 6000.0f, 1000.0f, " Hz" },
        { "Reso",      0.0f, 1.0f,      0.1f,   0.5f,   "" },
        { "Filt Env",  0.0f, 6.0f,      0.0f,   3.0f,   " oct" },
        { "Attack",  0.001f, 4.0f,     0.005f,  0.1f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.2f,   0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.7f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.3f,   0.5f,   " s" },
        { "Level",     0.0f, 1.0f,      0.4f,   0.4f,   "" } };
    static const std::vector<ParamInfo> fm {
        { "Ratio",     0.5f, 12.0f,     2.0f,   3.0f,   "" },
        { "Index",     0.0f, 12.0f,     3.0f,   3.0f,   "" },
        { "Idx Decay", 0.01f, 4.0f,     0.4f,   0.4f,   " s" },
        { "Feedback",  0.0f, 1.0f,      0.0f,   0.0f,   "" },
        { "Attack",  0.001f, 4.0f,     0.002f,  0.1f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.6f,   0.4f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.5f,   0.5f,   " s" },
        { "Cutoff",   20.0f, 20000.0f, 12000.0f, 2000.0f, " Hz" },
        { "Level",     0.0f, 1.0f,      0.3f,   0.4f,   "" } };
    static const std::vector<ParamInfo> wt {
        { "Position",  0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Detune",    0.0f, 50.0f,     9.0f,  10.0f,   " ct" },
        { "Cutoff",   20.0f, 20000.0f, 5000.0f, 1000.0f, " Hz" },
        { "Reso",      0.0f, 1.0f,      0.15f,  0.5f,   "" },
        { "Filt Env",  0.0f, 6.0f,      1.0f,   3.0f,   " oct" },
        { "Attack",  0.001f, 4.0f,      0.01f,  0.1f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.5f,   0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.6f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.4f,   0.5f,   " s" },
        { "Level",     0.0f, 1.0f,      0.35f,  0.4f,   "" } };
    static const std::vector<ParamInfo> smp {
        { "Tune",    -24.0f, 24.0f,     0.0f,   0.0f,   " st" },
        { "Loop",      0.0f, 1.0f,      0.0f,   0.0f,   "" },
        { "Attack",  0.001f, 4.0f,     0.002f,  0.1f,   " s" },
        { "Decay",    0.01f, 8.0f,      8.0f,   1.0f,   " s" },
        { "Sustain",   0.0f, 1.0f,      1.0f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.2f,   0.5f,   " s" },
        { "Cutoff",   20.0f, 20000.0f, 20000.0f, 2000.0f, " Hz" },
        { "Level",     0.0f, 1.0f,      0.8f,   0.5f,   "" } };
    static const std::vector<ParamInfo> ep {
        { "Tone",      0.0f, 1.0f,      0.5f,   0.5f,   "" },
        { "Decay",     0.5f, 12.0f,     4.0f,   3.0f,   " s" },
        { "Release",  0.01f, 3.0f,      0.15f,  0.3f,   " s" },
        { "Trem Rate", 0.0f, 12.0f,     5.5f,   4.0f,   " Hz" },
        { "Trem Depth", 0.0f, 1.0f,     0.3f,   0.5f,   "" },
        { "Bell",      0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Level",     0.0f, 1.0f,      0.6f,   0.5f,   "" } };
    static const std::vector<ParamInfo> bass {
        { "Wave",      0.0f, 1.0f,      0.0f,   0.0f,   "" },        // saw, square
        { "Sub",       0.0f, 1.0f,      0.5f,   0.5f,   "" },
        { "Glide",     0.0f, 0.5f,      0.05f,  0.1f,   " s" },
        { "Cutoff",   20.0f, 20000.0f, 800.0f, 800.0f,  " Hz" },
        { "Reso",      0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Filt Env",  0.0f, 6.0f,      2.5f,   3.0f,   " oct" },
        { "Decay",    0.01f, 4.0f,      0.35f,  0.3f,   " s" },
        { "Drive",     0.0f, 24.0f,     6.0f,   6.0f,   " dB" },
        { "Level",     0.0f, 1.0f,      0.6f,   0.5f,   "" } };

    static const std::vector<ParamInfo> pluck {
        { "Bright",    0.0f, 1.0f,      0.7f,   0.0f,   "" },
        { "Damping",   0.0f, 1.0f,      0.3f,   0.0f,   "" },
        { "Decay",     0.1f, 10.0f,     2.5f,   2.0f,   " s" },
        { "Position",  0.02f, 0.5f,     0.2f,   0.0f,   "" },
        { "Body",      0.0f, 1.0f,      0.3f,   0.0f,   "" },
        { "Release",   0.01f, 2.0f,     0.3f,   0.3f,   " s" },
        { "Level",     0.0f, 1.0f,      0.6f,   0.6f,   "" } };
    static const std::vector<ParamInfo> organ {
        { "16'",       0.0f, 8.0f,      8.0f,   0.0f,   "" },
        { "5 1/3'",    0.0f, 8.0f,      0.0f,   0.0f,   "" },
        { "8'",        0.0f, 8.0f,      8.0f,   0.0f,   "" },
        { "4'",        0.0f, 8.0f,      8.0f,   0.0f,   "" },
        { "2 2/3'",    0.0f, 8.0f,      0.0f,   0.0f,   "" },
        { "2'",        0.0f, 8.0f,      0.0f,   0.0f,   "" },
        { "1 3/5'",    0.0f, 8.0f,      0.0f,   0.0f,   "" },
        { "1 1/3'",    0.0f, 8.0f,      0.0f,   0.0f,   "" },
        { "1'",        0.0f, 8.0f,      0.0f,   0.0f,   "" },
        { "Perc",      0.0f, 1.0f,      0.0f,   0.0f,   "" },
        { "Perc Decay",0.05f, 1.0f,     0.3f,   0.3f,   " s" },
        { "Click",     0.0f, 1.0f,      0.2f,   0.0f,   "" },
        { "Vibrato",   0.0f, 1.0f,      0.2f,   0.0f,   "" },
        { "Vib Rate",  0.5f, 10.0f,     6.0f,   0.0f,   " Hz" },
        { "Level",     0.0f, 1.0f,      0.3f,   0.3f,   "" } };
    static const std::vector<ParamInfo> stack {
        { "Voices",    1.0f, 7.0f,      7.0f,   0.0f,   "" },
        { "Detune",    0.0f, 100.0f,    25.0f,  20.0f,  " ct" },
        { "Width",     0.0f, 1.0f,      0.8f,   0.0f,   "" },
        { "Mix",       0.0f, 1.0f,      0.7f,   0.0f,   "" },
        { "Cutoff",   20.0f, 20000.0f, 8000.0f, 1000.0f, " Hz" },
        { "Reso",      0.0f, 1.0f,      0.1f,   0.5f,   "" },
        { "Filt Env",  0.0f, 6.0f,      0.0f,   3.0f,   " oct" },
        { "Attack",  0.001f, 4.0f,     0.01f,   0.1f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.3f,   0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.8f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.3f,   0.5f,   " s" },
        { "Level",     0.0f, 1.0f,      0.3f,   0.3f,   "" } };
    static const std::vector<ParamInfo> chip {
        { "Wave",      0.0f, 4.0f,      1.0f,   0.0f,   "" },        // pulse 12.5%, pulse 25%, square, triangle, noise
        { "Bits",      2.0f, 16.0f,     8.0f,   0.0f,   "" },
        { "Arp",       0.0f, 4.0f,      0.0f,   0.0f,   "" },        // off, octave, major, minor, fifth
        { "Arp Rate",  2.0f, 30.0f,     12.0f,  0.0f,   " Hz" },
        { "Vib Rate",  0.5f, 12.0f,     5.0f,   0.0f,   " Hz" },
        { "Vibrato",   0.0f, 1.0f,      0.1f,   0.0f,   "" },
        { "Attack",  0.001f, 2.0f,     0.001f,  0.05f,  " s" },
        { "Decay",    0.01f, 4.0f,      0.15f,  0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.6f,   0.5f,   "" },
        { "Release",  0.01f, 4.0f,      0.05f,  0.3f,   " s" },
        { "Level",     0.0f, 1.0f,      0.25f,  0.25f,  "" } };
    static const std::vector<ParamInfo> piano {
        { "Hardness",  0.0f, 1.0f,      0.6f,   0.0f,   "" },
        { "Stiffness", 0.0f, 1.0f,      0.35f,  0.0f,   "" },
        { "Decay",     0.5f, 12.0f,     5.0f,   3.0f,   " s" },
        { "Beat",      0.0f, 4.0f,      0.8f,   0.0f,   " Hz" },      // how fast the two strings of a note beat
        { "Thump",     0.0f, 1.0f,      0.4f,   0.0f,   "" },
        { "Tone",      0.0f, 1.0f,      0.5f,   0.0f,   "" },
        { "Release",  0.02f, 2.0f,      0.25f,  0.3f,   " s" },
        { "Level",     0.0f, 1.0f,      0.55f,  0.55f,  "" } };
    static const std::vector<ParamInfo> strings {
        { "Ensemble",  0.0f, 40.0f,     12.0f,  10.0f,  " ct" },
        { "Movement",  0.0f, 1.0f,      0.5f,   0.0f,   "" },
        { "Vibrato",   0.0f, 1.0f,      0.3f,   0.0f,   "" },
        { "Vib Rate",  2.0f, 9.0f,      5.5f,   0.0f,   " Hz" },
        { "Bow",     300.0f, 12000.0f, 3500.0f, 2000.0f, " Hz" },
        { "Attack",  0.002f, 3.0f,      0.35f,  0.3f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.5f,   0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.9f,   0.5f,   "" },
        { "Release",  0.02f, 4.0f,      0.4f,   0.5f,   " s" },
        { "Level",     0.0f, 1.0f,      0.35f,  0.35f,  "" } };
    static const std::vector<ParamInfo> mallets {
        { "Bars",      0.0f, 3.0f,      0.0f,   0.0f,   "" },        // marimba, vibraphone, glockenspiel, kalimba
        { "Hardness",  0.0f, 1.0f,      0.5f,   0.0f,   "" },
        { "Decay",     0.2f, 3.0f,      1.0f,   0.0f,   "x" },
        { "Trem Rate", 0.5f, 10.0f,     4.5f,   0.0f,   " Hz" },
        { "Tremolo",   0.0f, 1.0f,      0.0f,   0.0f,   "" },
        { "Strike",    0.0f, 1.0f,      0.4f,   0.0f,   "" },
        { "Release",  0.02f, 2.0f,      0.2f,   0.3f,   " s" },
        { "Level",     0.0f, 1.0f,      0.35f,  0.35f,  "" } };
    static const std::vector<ParamInfo> brass {
        { "Detune",    0.0f, 30.0f,     6.0f,   0.0f,   " ct" },
        { "Blat",      0.0f, 4.0f,      2.0f,   0.0f,   " oct" },
        { "Blat Time", 0.01f, 0.6f,     0.08f,  0.1f,   " s" },
        { "Cutoff",  200.0f, 12000.0f, 1500.0f, 1500.0f, " Hz" },
        { "Reso",      0.0f, 1.0f,      0.15f,  0.0f,   "" },
        { "Dip",       0.0f, 3.0f,      0.5f,   0.0f,   " st" },
        { "Vibrato",   0.0f, 1.0f,      0.25f,  0.0f,   "" },
        { "Vib Rate",  2.0f, 9.0f,      5.0f,   0.0f,   " Hz" },
        { "Attack",  0.005f, 1.0f,      0.04f,  0.1f,   " s" },
        { "Release",  0.02f, 3.0f,      0.15f,  0.3f,   " s" },
        { "Level",     0.0f, 1.0f,      0.4f,   0.4f,   "" } };
    static const std::vector<ParamInfo> flute {
        { "Breath",    0.0f, 1.0f,      0.35f,  0.0f,   "" },
        { "Air",       0.0f, 1.0f,      0.4f,   0.0f,   "" },
        { "Chiff",     0.0f, 1.0f,      0.5f,   0.0f,   "" },
        { "Overblow",  0.0f, 1.0f,      0.2f,   0.0f,   "" },
        { "Vibrato",   0.0f, 1.0f,      0.35f,  0.0f,   "" },
        { "Vib Rate",  2.0f, 9.0f,      5.0f,   0.0f,   " Hz" },
        { "Vib Delay", 0.0f, 2.0f,      0.4f,   0.0f,   " s" },
        { "Attack",  0.005f, 1.0f,      0.06f,  0.1f,   " s" },
        { "Release",  0.02f, 2.0f,      0.2f,   0.3f,   " s" },
        { "Level",     0.0f, 1.0f,      0.5f,   0.5f,   "" } };
    static const std::vector<ParamInfo> vox {
        { "Vowel",     0.0f, 4.0f,      0.0f,   0.0f,   "" },        // A E I O U, morphing between
        { "Drift",     0.0f, 1.0f,      0.3f,   0.0f,   "" },
        { "Drift Rate",0.05f, 4.0f,     0.3f,   0.5f,   " Hz" },
        { "Breath",    0.0f, 1.0f,      0.15f,  0.0f,   "" },
        { "Width",     0.0f, 30.0f,     8.0f,   0.0f,   " ct" },
        { "Tone",      0.0f, 1.0f,      0.5f,   0.0f,   "" },
        { "Attack",  0.001f, 4.0f,      0.15f,  0.2f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.3f,   0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.8f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.4f,   0.5f,   " s" },
        { "Level",     0.0f, 1.0f,      0.6f,   0.6f,   "" } };
    switch (t)
    {
        case InstrumentType::subtractive:   return sub;
        case InstrumentType::fm:            return fm;
        case InstrumentType::wavetable:     return wt;
        case InstrumentType::sampler:       return smp;
        case InstrumentType::electricPiano: return ep;
        case InstrumentType::bass:          return bass;
        case InstrumentType::pluck:         return pluck;
        case InstrumentType::organ:         return organ;
        case InstrumentType::stack:         return stack;
        case InstrumentType::chip:          return chip;
        case InstrumentType::vox:           return vox;
        case InstrumentType::piano:         return piano;
        case InstrumentType::strings:       return strings;
        case InstrumentType::mallets:       return mallets;
        case InstrumentType::brass:         return brass;
        case InstrumentType::flute:         return flute;
        case InstrumentType::none:          break;
    }
    return none;
}

InstrumentParams Instrument::defaultParams (InstrumentType t)
{
    InstrumentParams p;
    p.type = t;
    p.presetName = "Init";
    const auto& info = paramInfo (t);
    for (size_t i = 0; i < info.size() && i < p.values.size(); ++i) p.values[i] = info[i].def;
    return p;
}

namespace
{
    std::vector<Instrument::UserPreset>& userPresetStore() { static std::vector<Instrument::UserPreset> v; return v; }
}

std::vector<InstrumentParams> Instrument::presets (InstrumentType t)
{
    auto out = bundledPresets (t);
    for (const auto& u : userPresetStore()) if (u.type == t) out.push_back (u.params);
    return out;
}

void Instrument::setUserPresets (std::vector<UserPreset> presets)
{
    std::vector<UserPreset> kept;
    for (auto& u : presets)
    {
        if (u.type == InstrumentType::none || u.params.presetName.trim().isEmpty()) continue;
        u.params.type = u.type;
        bool taken = false;
        for (const auto& b : bundledPresets (u.type)) if (b.presetName == u.params.presetName) taken = true;
        for (const auto& k : kept) if (k.type == u.type && k.params.presetName == u.params.presetName) taken = true;
        if (! taken) kept.push_back (std::move (u));
    }
    userPresetStore() = std::move (kept);
}

const std::vector<Instrument::UserPreset>& Instrument::userPresets() { return userPresetStore(); }

bool Instrument::isUserPreset (InstrumentType t, const juce::String& name)
{
    for (const auto& u : userPresetStore()) if (u.type == t && u.params.presetName == name) return true;
    return false;
}

InstrumentType Instrument::typeNamed (const juce::String& name)
{
    for (auto type : availableTypes())
        if (juce::String (typeName (type)).removeCharacters (" ").equalsIgnoreCase (name.removeCharacters (" -_"))) return type;
    return InstrumentType::none;
}

std::vector<InstrumentParams> Instrument::bundledPresets (InstrumentType t)
{
    std::vector<InstrumentParams> out;
    auto add = [&] (const char* name, std::initializer_list<std::pair<int, float>> changes)
    {
        auto p = defaultParams (t);
        p.presetName = name;
        for (const auto& c : changes) p.values[(size_t) c.first] = c.second;
        out.push_back (p);
    };

    switch (t)
    {
        case InstrumentType::subtractive:
            add ("Init Saw", {});
            add ("Pluck",       { { SubtractiveParams::cutoff, 900.0f }, { SubtractiveParams::resonance, 0.3f }, { SubtractiveParams::filterEnv, 3.5f },
                                  { SubtractiveParams::attack, 0.001f }, { SubtractiveParams::decay, 0.28f }, { SubtractiveParams::sustain, 0.0f },
                                  { SubtractiveParams::release, 0.2f }, { SubtractiveParams::level, 0.45f } });
            add ("Soft Pad",    { { SubtractiveParams::wave, 2.0f }, { SubtractiveParams::detune, 12.0f }, { SubtractiveParams::cutoff, 2500.0f },
                                  { SubtractiveParams::attack, 0.4f }, { SubtractiveParams::decay, 0.5f }, { SubtractiveParams::sustain, 0.8f },
                                  { SubtractiveParams::release, 0.9f }, { SubtractiveParams::level, 0.35f } });
            add ("Square Lead", { { SubtractiveParams::wave, 1.0f }, { SubtractiveParams::detune, 5.0f }, { SubtractiveParams::cutoff, 5000.0f },
                                  { SubtractiveParams::resonance, 0.2f }, { SubtractiveParams::decay, 0.1f }, { SubtractiveParams::sustain, 0.9f },
                                  { SubtractiveParams::release, 0.15f }, { SubtractiveParams::level, 0.3f } });
            add ("Sub Bass",    { { SubtractiveParams::wave, 3.0f }, { SubtractiveParams::osc2, 0.0f }, { SubtractiveParams::cutoff, 800.0f },
                                  { SubtractiveParams::attack, 0.003f }, { SubtractiveParams::decay, 0.3f }, { SubtractiveParams::sustain, 0.8f },
                                  { SubtractiveParams::release, 0.1f }, { SubtractiveParams::level, 0.7f } });
            break;
        case InstrumentType::fm:
            add ("FM Bell",  { { FmParams::ratio, 3.5f }, { FmParams::index, 4.0f }, { FmParams::indexDecay, 1.2f }, { FmParams::decay, 2.0f }, { FmParams::sustain, 0.0f }, { FmParams::release, 1.5f }, { FmParams::level, 0.22f } });
            add ("FM Bass",  { { FmParams::ratio, 1.0f }, { FmParams::index, 5.0f }, { FmParams::indexDecay, 0.15f }, { FmParams::decay, 0.3f }, { FmParams::sustain, 0.5f }, { FmParams::release, 0.1f }, { FmParams::level, 0.6f } });
            add ("FM Keys",  { { FmParams::ratio, 2.0f }, { FmParams::index, 2.0f }, { FmParams::indexDecay, 0.6f }, { FmParams::feedback, 0.2f }, { FmParams::decay, 1.0f }, { FmParams::sustain, 0.2f } });
            add ("FM Glass", { { FmParams::ratio, 7.0f }, { FmParams::index, 1.5f }, { FmParams::indexDecay, 2.0f }, { FmParams::attack, 0.3f }, { FmParams::sustain, 0.6f }, { FmParams::release, 2.0f } });
            break;
        case InstrumentType::wavetable:
            add ("WT Saw Sweep", {});
            add ("WT Hollow",    { { WavetableParams::position, 0.75f }, { WavetableParams::cutoff, 3000.0f }, { WavetableParams::detune, 14.0f } });
            add ("WT Pluck",     { { WavetableParams::position, 0.15f }, { WavetableParams::cutoff, 700.0f }, { WavetableParams::filterEnv, 3.0f }, { WavetableParams::decay, 0.25f }, { WavetableParams::sustain, 0.0f } });
            add ("WT Pad",       { { WavetableParams::position, 0.5f }, { WavetableParams::attack, 0.6f }, { WavetableParams::sustain, 0.9f }, { WavetableParams::release, 1.5f }, { WavetableParams::cutoff, 2500.0f } });
            break;
        case InstrumentType::sampler:
            add ("One Shot", {});
            add ("Looped",   { { SamplerParams::loop, 1.0f } });
            add ("Pad",      { { SamplerParams::loop, 1.0f }, { SamplerParams::attack, 0.3f }, { SamplerParams::release, 1.0f }, { SamplerParams::cutoff, 4000.0f } });
            break;
        case InstrumentType::electricPiano:
            add ("Tine Piano", {});
            add ("Bright EP",  { { ElectricPianoParams::tone, 0.85f }, { ElectricPianoParams::bell, 0.6f } });
            add ("Warm EP",    { { ElectricPianoParams::tone, 0.25f }, { ElectricPianoParams::tremoloDepth, 0.5f }, { ElectricPianoParams::decay, 6.0f } });
            break;
        case InstrumentType::bass:
            add ("Acid Bass",   {});
            add ("Deep Sub",    { { BassParams::wave, 0.0f }, { BassParams::sub, 1.0f }, { BassParams::cutoff, 300.0f }, { BassParams::filterEnv, 1.0f }, { BassParams::drive, 0.0f } });
            add ("Square Bass", { { BassParams::wave, 1.0f }, { BassParams::cutoff, 1200.0f }, { BassParams::resonance, 0.5f }, { BassParams::drive, 12.0f } });
            break;
        case InstrumentType::pluck:
            add ("Nylon Guitar", {});
            add ("Steel String", { { PluckParams::brightness, 0.9f }, { PluckParams::damping, 0.15f }, { PluckParams::decay, 4.0f }, { PluckParams::position, 0.12f } });
            add ("Harp",         { { PluckParams::brightness, 0.6f }, { PluckParams::damping, 0.2f }, { PluckParams::decay, 5.0f }, { PluckParams::position, 0.4f }, { PluckParams::body, 0.5f } });
            add ("Koto",         { { PluckParams::brightness, 1.0f }, { PluckParams::damping, 0.5f }, { PluckParams::decay, 1.2f }, { PluckParams::position, 0.08f }, { PluckParams::body, 0.1f } });
            add ("Muted Pluck",  { { PluckParams::brightness, 0.4f }, { PluckParams::damping, 0.8f }, { PluckParams::decay, 0.4f }, { PluckParams::release, 0.1f }, { PluckParams::level, 0.8f } });
            break;
        case InstrumentType::organ:
            add ("Jazz Organ",   { { OrganParams::percussion, 0.6f }, { OrganParams::vibrato, 0.35f } });
            add ("Full Organ",   { { OrganParams::bar5, 6.0f }, { OrganParams::bar2b3, 6.0f }, { OrganParams::bar2, 5.0f }, { OrganParams::bar1b3b5, 4.0f }, { OrganParams::bar1b1b3, 3.0f }, { OrganParams::bar1, 3.0f }, { OrganParams::vibrato, 0.4f }, { OrganParams::level, 0.2f } });
            add ("Church",       { { OrganParams::bar16, 8.0f }, { OrganParams::bar8, 8.0f }, { OrganParams::bar4, 6.0f }, { OrganParams::bar2, 6.0f }, { OrganParams::bar1, 4.0f }, { OrganParams::click, 0.0f }, { OrganParams::vibrato, 0.0f }, { OrganParams::level, 0.22f } });
            add ("Soft Flute",   { { OrganParams::bar16, 0.0f }, { OrganParams::bar8, 8.0f }, { OrganParams::bar4, 2.0f }, { OrganParams::click, 0.05f }, { OrganParams::vibrato, 0.5f }, { OrganParams::vibratoRate, 5.0f } });
            add ("Rock Organ",   { { OrganParams::bar5, 8.0f }, { OrganParams::bar2b3, 8.0f }, { OrganParams::click, 0.5f }, { OrganParams::vibrato, 0.6f }, { OrganParams::vibratoRate, 7.0f }, { OrganParams::level, 0.25f } });
            break;
        case InstrumentType::stack:
            add ("Trance Saw", {});
            add ("Wide Pad",     { { StackParams::detune, 40.0f }, { StackParams::width, 1.0f }, { StackParams::cutoff, 3000.0f }, { StackParams::attack, 0.6f }, { StackParams::sustain, 1.0f }, { StackParams::release, 1.5f }, { StackParams::level, 0.25f } });
            add ("Unison Lead",  { { StackParams::voices, 3.0f }, { StackParams::detune, 12.0f }, { StackParams::width, 0.3f }, { StackParams::cutoff, 12000.0f }, { StackParams::resonance, 0.25f }, { StackParams::sustain, 0.9f }, { StackParams::release, 0.1f } });
            add ("Stack Pluck",  { { StackParams::cutoff, 600.0f }, { StackParams::filterEnv, 4.0f }, { StackParams::decay, 0.35f }, { StackParams::sustain, 0.0f }, { StackParams::release, 0.25f }, { StackParams::level, 0.4f } });
            break;
        case InstrumentType::chip:
            add ("Pulse Lead", {});
            add ("Square Bleep", { { ChipParams::wave, 2.0f }, { ChipParams::decay, 0.08f }, { ChipParams::sustain, 0.0f }, { ChipParams::vibratoDepth, 0.0f } });
            add ("Arp Chord",    { { ChipParams::arp, 2.0f }, { ChipParams::arpRate, 15.0f }, { ChipParams::sustain, 0.8f } });
            add ("Tri Bass",     { { ChipParams::wave, 3.0f }, { ChipParams::bits, 4.0f }, { ChipParams::vibratoDepth, 0.0f }, { ChipParams::sustain, 0.9f }, { ChipParams::level, 0.4f } });
            add ("Noise Hit",    { { ChipParams::wave, 4.0f }, { ChipParams::decay, 0.12f }, { ChipParams::sustain, 0.0f }, { ChipParams::vibratoDepth, 0.0f } });
            break;
        case InstrumentType::vox:
            add ("Choir Ahh", {});
            add ("Ooh Pad",      { { VoxParams::vowel, 3.0f }, { VoxParams::drift, 0.15f }, { VoxParams::attack, 0.5f }, { VoxParams::release, 1.2f }, { VoxParams::tone, 0.3f } });
            add ("Talking Lead", { { VoxParams::drift, 1.0f }, { VoxParams::driftRate, 1.5f }, { VoxParams::attack, 0.01f }, { VoxParams::sustain, 0.9f }, { VoxParams::release, 0.1f }, { VoxParams::breath, 0.05f } });
            add ("Whisper",      { { VoxParams::vowel, 2.0f }, { VoxParams::breath, 0.8f }, { VoxParams::tone, 0.8f }, { VoxParams::attack, 0.3f }, { VoxParams::level, 0.8f } });
            break;
        case InstrumentType::piano:
            add ("Grand Piano", {});
            add ("Bright Piano", { { PianoParams::hardness, 0.9f }, { PianoParams::tone, 0.8f }, { PianoParams::stiffness, 0.45f } });
            add ("Upright",      { { PianoParams::hardness, 0.5f }, { PianoParams::stiffness, 0.6f }, { PianoParams::detune, 1.6f }, { PianoParams::decay, 3.5f }, { PianoParams::thump, 0.6f } });
            add ("Felt Piano",   { { PianoParams::hardness, 0.2f }, { PianoParams::tone, 0.2f }, { PianoParams::thump, 0.7f }, { PianoParams::decay, 4.0f }, { PianoParams::level, 0.7f } });
            add ("Honky Tonk",   { { PianoParams::detune, 3.5f }, { PianoParams::hardness, 0.8f }, { PianoParams::stiffness, 0.7f }, { PianoParams::decay, 2.5f } });
            break;
        case InstrumentType::strings:
            add ("String Ensemble", {});
            add ("Solo Violin",  { { StringsParams::ensemble, 0.0f }, { StringsParams::movement, 0.1f }, { StringsParams::vibrato, 0.6f }, { StringsParams::vibratoRate, 6.0f }, { StringsParams::bow, 6000.0f }, { StringsParams::attack, 0.12f }, { StringsParams::level, 0.4f } });
            add ("Cellos",       { { StringsParams::ensemble, 8.0f }, { StringsParams::bow, 1800.0f }, { StringsParams::vibrato, 0.35f }, { StringsParams::vibratoRate, 4.5f }, { StringsParams::attack, 0.25f }, { StringsParams::level, 0.45f } });
            add ("Pizzicato",    { { StringsParams::ensemble, 4.0f }, { StringsParams::movement, 0.0f }, { StringsParams::vibrato, 0.0f }, { StringsParams::bow, 2500.0f }, { StringsParams::attack, 0.002f }, { StringsParams::decay, 0.25f }, { StringsParams::sustain, 0.0f }, { StringsParams::release, 0.15f }, { StringsParams::level, 0.6f } });
            add ("Slow Pad",     { { StringsParams::ensemble, 20.0f }, { StringsParams::movement, 0.8f }, { StringsParams::bow, 2200.0f }, { StringsParams::attack, 1.2f }, { StringsParams::release, 2.0f }, { StringsParams::level, 0.3f } });
            break;
        case InstrumentType::mallets:
            add ("Marimba", {});
            add ("Vibraphone",   { { MalletParams::instrument, 1.0f }, { MalletParams::hardness, 0.4f }, { MalletParams::tremoloDepth, 0.5f } });
            add ("Glockenspiel", { { MalletParams::instrument, 2.0f }, { MalletParams::hardness, 0.8f }, { MalletParams::level, 0.25f } });
            add ("Kalimba",      { { MalletParams::instrument, 3.0f }, { MalletParams::hardness, 0.3f }, { MalletParams::strike, 0.6f } });
            add ("Soft Marimba", { { MalletParams::hardness, 0.15f }, { MalletParams::decay, 1.4f }, { MalletParams::strike, 0.2f } });
            break;
        case InstrumentType::brass:
            add ("Brass Section", {});
            add ("Trumpet",      { { BrassParams::detune, 0.0f }, { BrassParams::blat, 2.5f }, { BrassParams::blatTime, 0.05f }, { BrassParams::cutoff, 2200.0f }, { BrassParams::dip, 0.8f }, { BrassParams::vibrato, 0.35f }, { BrassParams::level, 0.35f } });
            add ("French Horn",  { { BrassParams::detune, 3.0f }, { BrassParams::blat, 1.2f }, { BrassParams::blatTime, 0.15f }, { BrassParams::cutoff, 900.0f }, { BrassParams::dip, 0.3f }, { BrassParams::vibrato, 0.1f }, { BrassParams::attack, 0.08f } });
            add ("Trombone",     { { BrassParams::detune, 2.0f }, { BrassParams::blat, 1.8f }, { BrassParams::blatTime, 0.1f }, { BrassParams::cutoff, 1200.0f }, { BrassParams::dip, 1.5f }, { BrassParams::vibratoRate, 4.0f } });
            add ("Synth Brass",  { { BrassParams::detune, 14.0f }, { BrassParams::blat, 3.0f }, { BrassParams::blatTime, 0.12f }, { BrassParams::cutoff, 1800.0f }, { BrassParams::resonance, 0.4f }, { BrassParams::dip, 0.0f }, { BrassParams::vibrato, 0.0f }, { BrassParams::release, 0.3f } });
            break;
        case InstrumentType::flute:
            add ("Flute", {});
            add ("Pan Pipes",    { { FluteParams::breath, 0.6f }, { FluteParams::air, 0.7f }, { FluteParams::chiff, 0.9f }, { FluteParams::overblow, 0.05f }, { FluteParams::vibrato, 0.2f }, { FluteParams::attack, 0.03f } });
            add ("Recorder",     { { FluteParams::breath, 0.15f }, { FluteParams::air, 0.3f }, { FluteParams::chiff, 0.3f }, { FluteParams::overblow, 0.4f }, { FluteParams::vibrato, 0.1f }, { FluteParams::attack, 0.02f } });
            add ("Shakuhachi",   { { FluteParams::breath, 0.8f }, { FluteParams::air, 0.5f }, { FluteParams::chiff, 0.6f }, { FluteParams::vibrato, 0.6f }, { FluteParams::vibratoRate, 3.5f }, { FluteParams::vibratoDelay, 0.8f }, { FluteParams::attack, 0.15f } });
            break;
        case InstrumentType::none:
            break;
    }
    if (out.empty()) out.push_back (defaultParams (t));
    return out;
}

//==============================================================================
// Common polyphonic voice pool

namespace
{
    constexpr int maxVoices = 16;

    struct VoiceBase
    {
        bool active = false;
        int pitch = 0;
        float velocity = 0.0f, gain = 1.0f;
        int delay = 0, gate = -1;
        Adsr env;
        Svf filter;
        std::uint32_t order = 0;
    };

    template <typename Voice>
    Voice* allocateVoice (std::array<Voice, maxVoices>& voices, std::uint32_t& counter)
    {
        Voice* target = nullptr;
        for (auto& v : voices) if (! v.active) { target = &v; break; }
        if (target == nullptr)
            for (auto& v : voices) if (v.env.stage == Adsr::Stage::release && (target == nullptr || v.order < target->order)) target = &v;
        if (target == nullptr) { target = &voices[0]; for (auto& v : voices) if (v.order < target->order) target = &v; }
        *target = Voice {};
        target->active = true;
        target->order = ++counter;
        return target;
    }

    // Gate countdown -> release; returns false when the voice has gone silent.
    template <typename Voice>
    bool stepGate (Voice& v, double releaseSeconds, double sr) noexcept
    {
        if (v.gate == 0 && v.env.stage != Adsr::Stage::release) { v.env.release (releaseSeconds, sr); v.gate = -1; }
        else if (v.gate > 0) --v.gate;
        return v.env.isActive();
    }

    template <typename Voice>
    void releaseAll (std::array<Voice, maxVoices>& voices, bool immediate) noexcept
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (immediate) { v.active = false; v.env.kill(); }
            else if (v.env.stage != Adsr::Stage::release) v.gate = 0;
        }
    }

    template <typename Voice>
    void noteOffAll (std::array<Voice, maxVoices>& voices, int pitch) noexcept
    {
        for (auto& v : voices) if (v.active && v.pitch == pitch && v.gate < 0 && v.env.stage != Adsr::Stage::release) v.gate = 0;
    }

    template <typename Voice>
    int countActive (const std::array<Voice, maxVoices>& voices) noexcept
    {
        int n = 0; for (const auto& v : voices) n += v.active ? 1 : 0; return n;
    }

    void addToOutputs (float* const* outputs, int numOutputs, int i, float sample) noexcept
    {
        for (int ch = 0; ch < juce::jmin (numOutputs, 32); ++ch)
            if (outputs[ch] != nullptr) outputs[ch][i] += sample;
    }

    // Left and right to the first two outputs; any others get the mono sum
    void addStereo (float* const* outputs, int numOutputs, int i, float l, float r) noexcept
    {
        if (numOutputs >= 2 && outputs[0] != nullptr && outputs[1] != nullptr)
        {
            outputs[0][i] += l; outputs[1][i] += r;
            for (int ch = 2; ch < juce::jmin (numOutputs, 32); ++ch) if (outputs[ch] != nullptr) outputs[ch][i] += 0.5f * (l + r);
        }
        else addToOutputs (outputs, numOutputs, i, 0.5f * (l + r));
    }

    struct Noise { std::uint32_t s = 0x9e3779b9u; float next() noexcept { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 8388608.0f) - 1.0f; } };

    // RBJ band-pass, 0 dB peak gain: the formant filters of the Vox
    struct BandPass
    {
        float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
        void set (double hz, double q, double sr) noexcept
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (20.0, 0.45 * sr, hz) / sr;
            const double alpha = std::sin (w) / (2.0 * juce::jmax (0.5, q)), a0 = 1.0 + alpha;
            b0 = (float) (alpha / a0); a1 = (float) (-2.0 * std::cos (w) / a0); a2 = (float) ((1.0 - alpha) / a0);
        }
        float process (float x) noexcept   // transposed direct form II; b1 = 0, b2 = -b0
        {
            const float y = b0 * x + z1;
            z1 = -a1 * y + z2;
            z2 = -b0 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
    };
}

//==============================================================================
// Subtractive synth (two oscillators, SVF, ADSR)

class SubtractiveSynth final : public Instrument
{
    struct Voice : VoiceBase { double phase1 = 0.0, phase2 = 0.37, inc1 = 0.0, inc2 = 0.0; };
public:
    SubtractiveSynth() : Instrument (InstrumentType::subtractive) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const double f = midiToHz (pitch), det = p.values[SubtractiveParams::detune];
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc1 = f * std::pow (2.0, -det / 1200.0) / sampleRate;
        v.inc2 = f * std::pow (2.0,  det / 1200.0) / sampleRate;
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const auto wave = (Wave) juce::jlimit (0, 3, (int) std::lround (pv[SubtractiveParams::wave]));
        const bool osc2 = pv[SubtractiveParams::osc2] >= 0.5f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[SubtractiveParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[SubtractiveParams::attack], pv[SubtractiveParams::decay], pv[SubtractiveParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                if (((i - offset) & 15) == 0)
                    v.filter.setCutoff (pv[SubtractiveParams::cutoff] * std::pow (2.0, pv[SubtractiveParams::filterEnv] * env), pv[SubtractiveParams::resonance], sampleRate);
                float osc = oscillator (wave, v.phase1, v.inc1);
                v.phase1 += v.inc1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
                if (osc2) { osc = 0.5f * (osc + oscillator (wave, v.phase2, v.inc2)); v.phase2 += v.inc2; if (v.phase2 >= 1.0) v.phase2 -= 1.0; }
                addToOutputs (outputs, numOutputs, i, v.filter.process (osc) * env * v.velocity * v.gain * pv[SubtractiveParams::level]);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// FM synth: one modulator into one carrier, index decays over time

class FmSynth final : public Instrument
{
    struct Voice : VoiceBase { double cPhase = 0.0, mPhase = 0.0, cInc = 0.0, mInc = 0.0; float indexEnv = 1.0f, lastMod = 0.0f; };
public:
    FmSynth() : Instrument (InstrumentType::fm) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const double f = midiToHz (pitch);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.cInc = f / sampleRate; v.mInc = f * p.values[FmParams::ratio] / sampleRate; v.indexEnv = 1.0f;
        v.filter.setCutoff (p.values[FmParams::cutoff], 0.0f, sampleRate);
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const float indexDecayCoef = (float) std::exp (-1.0 / juce::jmax (1.0, pv[FmParams::indexDecay] * sampleRate));
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[FmParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[FmParams::attack], pv[FmParams::decay], pv[FmParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                v.indexEnv *= indexDecayCoef;
                const float index = pv[FmParams::index] * (0.15f + 0.85f * v.indexEnv) * (0.5f + 0.5f * v.velocity);
                const float mod = (float) std::sin (juce::MathConstants<double>::twoPi * v.mPhase + pv[FmParams::feedback] * v.lastMod);
                v.lastMod = mod;
                const float carrier = (float) std::sin (juce::MathConstants<double>::twoPi * v.cPhase + index * mod);
                v.cPhase += v.cInc; if (v.cPhase >= 1.0) v.cPhase -= 1.0;
                v.mPhase += v.mInc; if (v.mPhase >= 1.0) v.mPhase -= 1.0;
                addToOutputs (outputs, numOutputs, i, v.filter.process (carrier) * env * v.velocity * v.gain * pv[FmParams::level]);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// Wavetable synth: three band-limited tables morphed by Position

class WavetableSynth final : public Instrument
{
    static constexpr int tableSize = 2048, numTables = 3;
    struct Voice : VoiceBase { double phase1 = 0.0, phase2 = 0.5, inc1 = 0.0, inc2 = 0.0; };
public:
    WavetableSynth() : Instrument (InstrumentType::wavetable)
    {
        // Table 0: saw (32 harmonics), 1: "hollow" (odd harmonics 1/n, like a square with soft top), 2: formant-ish (harmonics 1..8 with a peak at 4)
        for (int t = 0; t < numTables; ++t)
            for (int i = 0; i < tableSize; ++i)
            {
                const double ph = (double) i / tableSize;
                double v = 0.0;
                for (int h = 1; h <= 32; ++h)
                {
                    double amp = 0.0;
                    if (t == 0)      amp = 1.0 / h;
                    else if (t == 1) amp = (h % 2 == 1) ? 1.0 / h : 0.0;
                    else if (h <= 8) amp = std::exp (-0.5 * std::pow ((h - 4.0) / 1.5, 2.0));
                    v += amp * std::sin (juce::MathConstants<double>::twoPi * h * ph);
                }
                tables[(size_t) t][(size_t) i] = (float) v;
            }
        for (auto& tbl : tables)
        {
            float peak = 0.0f; for (float x : tbl) peak = juce::jmax (peak, std::abs (x));
            if (peak > 0.0f) for (auto& x : tbl) x /= peak;
        }
    }
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const double f = midiToHz (pitch), det = p.values[WavetableParams::detune];
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc1 = f * std::pow (2.0, -det / 1200.0) / sampleRate; v.inc2 = f * std::pow (2.0, det / 1200.0) / sampleRate;
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    float read (double phase, float position) const noexcept
    {
        const float pos = juce::jlimit (0.0f, 1.0f, position) * (numTables - 1);
        const int t0 = juce::jmin (numTables - 2, (int) pos); const float frac = pos - t0;
        const double idx = phase * tableSize;
        const int i0 = ((int) idx) % tableSize, i1 = (i0 + 1) % tableSize;
        const float f = (float) (idx - std::floor (idx));
        auto sample = [&] (int t) { return tables[(size_t) t][(size_t) i0] + (tables[(size_t) t][(size_t) i1] - tables[(size_t) t][(size_t) i0]) * f; };
        return sample (t0) * (1.0f - frac) + sample (t0 + 1) * frac;
    }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[WavetableParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[WavetableParams::attack], pv[WavetableParams::decay], pv[WavetableParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                if (((i - offset) & 15) == 0)
                    v.filter.setCutoff (pv[WavetableParams::cutoff] * std::pow (2.0, pv[WavetableParams::filterEnv] * env), pv[WavetableParams::resonance], sampleRate);
                const float osc = 0.5f * (read (v.phase1, pv[WavetableParams::position]) + read (v.phase2, pv[WavetableParams::position]));
                v.phase1 += v.inc1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
                v.phase2 += v.inc2; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
                addToOutputs (outputs, numOutputs, i, v.filter.process (osc) * env * v.velocity * v.gain * pv[WavetableParams::level]);
            }
        }
    }
private:
    std::array<std::array<float, tableSize>, numTables> tables {};
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// Sampler: one sample pitched around its root note, one-shot or looped

class Sampler final : public Instrument
{
    struct Voice : VoiceBase { double position = 0.0, rate = 1.0; };
public:
    Sampler() : Instrument (InstrumentType::sampler) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (p.sample == nullptr || p.sample->getNumSamples() < 2 || ! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.rate = std::pow (2.0, (pitch - p.rootNote + p.values[SamplerParams::tune]) / 12.0) * (p.sampleRate / sampleRate);
        v.filter.setCutoff (p.values[SamplerParams::cutoff], 0.0f, sampleRate);
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        if (p.sample == nullptr) { for (auto& v : voices) v.active = false; return; }
        const auto& pv = p.values;
        const auto& s = *p.sample;
        const int length = s.getNumSamples(), channels = s.getNumChannels();
        const bool loop = pv[SamplerParams::loop] >= 0.5f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[SamplerParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[SamplerParams::attack], pv[SamplerParams::decay], pv[SamplerParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                if (v.position >= length - 1)
                {
                    if (loop) v.position = std::fmod (v.position, (double) (length - 1));
                    else { v.active = false; break; }
                }
                const int i0 = (int) v.position; const float frac = (float) (v.position - i0);
                float sum = 0.0f;
                for (int ch = 0; ch < channels; ++ch) { const float* d = s.getReadPointer (ch); sum += d[i0] + (d[i0 + 1] - d[i0]) * frac; }
                v.position += v.rate;
                addToOutputs (outputs, numOutputs, i, v.filter.process (sum / (float) channels) * env * v.velocity * v.gain * pv[SamplerParams::level]);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// Electric piano: tine (fundamental + inharmonic partials with faster decay) with tremolo

class ElectricPiano final : public Instrument
{
    struct Voice : VoiceBase { double ph1 = 0.0, ph2 = 0.0, ph3 = 0.0, inc = 0.0; float amp = 0.0f, ampCoef = 1.0f, brightCoef = 1.0f, bright = 1.0f; };
public:
    ElectricPiano() : Instrument (InstrumentType::electricPiano) {}
    void reset() override { for (auto& v : voices) v = {}; tremPhase = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc = midiToHz (pitch) / sampleRate;
        // Higher notes decay faster, like a real tine
        const double decaySeconds = p.values[ElectricPianoParams::decay] * std::pow (0.5, (pitch - 60) / 24.0);
        v.amp = 1.0f;
        v.ampCoef = (float) std::exp (-1.0 / juce::jmax (0.01, decaySeconds * sampleRate));
        v.bright = 0.3f + 0.7f * v.velocity;                                 // velocity -> attack brightness
        v.brightCoef = (float) std::exp (-1.0 / (0.25 * sampleRate));        // partials fade in ~0.25 s
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const double tremInc = pv[ElectricPianoParams::tremoloRate] / sampleRate;
        const float tremDepth = pv[ElectricPianoParams::tremoloDepth], tone = pv[ElectricPianoParams::tone], bell = pv[ElectricPianoParams::bell];
        for (int i = 0; i < n; ++i)
        {
            tremPhase += tremInc; if (tremPhase >= 1.0) tremPhase -= 1.0;
            const float trem = 1.0f - tremDepth * 0.5f * (1.0f + (float) std::sin (juce::MathConstants<double>::twoPi * tremPhase));
            float mix = 0.0f;
            bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[ElectricPianoParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (0.001, 100.0, 1.0f, sampleRate);   // held; the tine decay is v.amp
                if (! v.env.isActive()) { v.active = false; continue; }
                v.amp *= v.ampCoef; v.bright *= v.brightCoef;
                if (v.amp < 1.0e-4f) { v.active = false; continue; }
                const float fundamental = (float) std::sin (juce::MathConstants<double>::twoPi * v.ph1);
                const float second = (float) std::sin (juce::MathConstants<double>::twoPi * v.ph2) * (0.2f + 0.5f * tone) * v.bright;
                const float tineBell = (float) std::sin (juce::MathConstants<double>::twoPi * v.ph3) * bell * v.bright * 0.5f;
                v.ph1 += v.inc; if (v.ph1 >= 1.0) v.ph1 -= 1.0;
                v.ph2 += v.inc * 2.0; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
                v.ph3 += v.inc * 5.9; if (v.ph3 >= 1.0) v.ph3 -= 1.0;   // inharmonic bell partial
                mix += (fundamental + second + tineBell) * v.amp * env * v.velocity * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * trem * pv[ElectricPianoParams::level] * 0.5f);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double tremPhase = 0.0;
};

//==============================================================================
// Mono bass: last-note priority, glide, oscillator + sub, filter envelope, drive

class BassSynth final : public Instrument
{
public:
    BassSynth() : Instrument (InstrumentType::bass) {}
    void reset() override { active = false; env.kill(); heldNotes.clear(); }

    void noteOn (int pitch, float newVelocity, float newGain, int delaySamples, int gateSamples, const InstrumentParams&) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        if (heldNotes.size() < heldNotes.capacity()) heldNotes.push_back (pitch);
        targetFreq = midiToHz (pitch);
        if (! active) { currentFreq = targetFreq; phase = 0.0; subPhase = 0.0; filterEnv = 1.0f; env.start(); filter.reset(); }
        else filterEnv = 1.0f;    // retrigger the filter envelope legato
        active = true; velocity = juce::jlimit (0.0f, 1.0f, newVelocity); gain = newGain;
        delay = juce::jmax (0, delaySamples); gate = gateSamples; currentPitch = pitch;
    }
    void noteOff (int pitch) noexcept override
    {
        heldNotes.erase (pitch);
        if (! heldNotes.empty()) { targetFreq = midiToHz (heldNotes.back()); currentPitch = heldNotes.back(); }
        else if (pitch == currentPitch && gate < 0) gate = 0;
    }
    void allNotesOff (bool immediate) noexcept override
    {
        heldNotes.clear();
        if (immediate) { active = false; env.kill(); }
        else if (active && env.stage != Adsr::Stage::release) gate = 0;
    }
    int getNumActiveVoices() const noexcept override { return active ? 1 : 0; }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        if (! active) return;
        const auto& pv = p.values;
        const auto wave = pv[BassParams::wave] >= 0.5f ? Wave::square : Wave::saw;
        const float glideCoef = (float) std::exp (-1.0 / juce::jmax (1.0, pv[BassParams::glide] * sampleRate));
        const float fEnvCoef = (float) std::exp (-1.0 / juce::jmax (1.0, pv[BassParams::decay] * sampleRate));
        const float driveGain = juce::Decibels::decibelsToGain (pv[BassParams::drive]);
        const int offset = juce::jmin (delay, n); delay -= offset;
        for (int i = offset; i < n; ++i)
        {
            if (gate == 0 && env.stage != Adsr::Stage::release) { env.release (0.08, sampleRate); gate = -1; }
            else if (gate > 0) --gate;
            const float e = env.next (0.003, 0.2, 1.0f, sampleRate);
            if (! env.isActive()) { active = false; break; }
            currentFreq += (targetFreq - currentFreq) * (1.0f - glideCoef);
            filterEnv *= fEnvCoef;
            if (((i - offset) & 15) == 0)
                filter.setCutoff (pv[BassParams::cutoff] * std::pow (2.0, pv[BassParams::filterEnv] * filterEnv), pv[BassParams::resonance], sampleRate);
            const double inc = currentFreq / sampleRate;
            float osc = oscillator (wave, phase, inc) + pv[BassParams::sub] * (float) std::sin (juce::MathConstants<double>::twoPi * subPhase);
            phase += inc; if (phase >= 1.0) phase -= 1.0;
            subPhase += inc * 0.5; if (subPhase >= 1.0) subPhase -= 1.0;
            float x = filter.process (osc);
            x = std::tanh (x * driveGain) / juce::jmax (1.0f, std::tanh (driveGain));
            addToOutputs (outputs, numOutputs, i, x * e * velocity * gain * pv[BassParams::level]);
        }
    }
private:
    bool active = false;
    int currentPitch = 0, delay = 0, gate = -1;
    float velocity = 0.0f, gain = 1.0f, filterEnv = 1.0f;
    double phase = 0.0, subPhase = 0.0, currentFreq = 110.0, targetFreq = 110.0;
    Adsr env;
    Svf filter;
    struct HeldNotes { std::array<int, 16> notes {}; int count = 0;
        size_t size() const { return (size_t) count; } size_t capacity() const { return 16; } bool empty() const { return count == 0; }
        void push_back (int n) { notes[(size_t) count++] = n; } int back() const { return notes[(size_t) count - 1]; } void clear() { count = 0; }
        void erase (int n) { int w = 0; for (int i = 0; i < count; ++i) if (notes[(size_t) i] != n) notes[(size_t) w++] = notes[(size_t) i]; count = w; }
    } heldNotes;
};

//==============================================================================
// Pluck: Karplus-Strong string. A burst of noise (filtered by Bright, combed by
// the pick Position) circulates in a delay line tuned to the note; a one-pole
// in the loop (Damping) and the loop gain (Decay, as a T60) shape the ring.

class PluckSynth final : public Instrument
{
    struct Voice : VoiceBase { int write = 0; double length = 100.0; float loopGain = 0.99f, lp = 0.0f, amp = 1.0f, dampCoef = 1.0f; };
public:
    PluckSynth() : Instrument (InstrumentType::pluck) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128) || lines[0].empty()) return;
        const int index = (int) (allocateVoice (voices, counter) - voices.data());
        auto& v = voices[(size_t) index]; auto& line = lines[(size_t) index];
        const auto& pv = p.values;
        const double f = midiToHz (pitch);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.dampCoef = 1.0f - 0.92f * pv[PluckParams::damping];
        // The loop's one-pole delays the wave by about (1 - c) / c samples: take that off the line so the pitch stays true
        v.length = juce::jlimit (2.0, (double) line.size() - 2.0, sampleRate / f - (1.0 - v.dampCoef) / v.dampCoef);
        v.loopGain = (float) std::exp (-6.9078 / juce::jmax (1.0, pv[PluckParams::decay] * f));   // T60 in periods
        v.amp = 1.0f; v.lp = 0.0f; v.write = 0;
        // Excite: noise, low-passed by Bright, then combed at the pick position (a pluck's missing harmonics)
        const int n = (int) std::ceil (v.length);
        const float bright = 0.04f + 0.96f * pv[PluckParams::brightness] * (0.5f + 0.5f * v.velocity);
        float y = 0.0f;
        for (int i = 0; i < n; ++i) { y += bright * (noise.next() - y); line[(size_t) i] = y; }
        const int pick = juce::jlimit (1, n - 1, (int) (pv[PluckParams::position] * n));
        for (int i = n - 1; i >= pick; --i) line[(size_t) i] -= line[(size_t) (i - pick)];
        for (int i = n; i < (int) line.size(); ++i) line[(size_t) i] = 0.0f;
        v.write = n % (int) line.size();   // the read point (one period back) lands on the burst, not the silent tail
        v.filter.reset();
        v.filter.setCutoff (4500.0 - 3000.0 * pv[PluckParams::body], 0.55f * pv[PluckParams::body], sampleRate);
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        for (size_t vi = 0; vi < voices.size(); ++vi)
        {
            auto& v = voices[vi]; auto& line = lines[vi];
            if (! v.active || line.empty()) continue;
            const int len = (int) line.size();
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[PluckParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (0.0005, 100.0, 1.0f, sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                // Read one period back, with linear interpolation for the fractional part
                const double readPos = v.write - v.length;
                const int i0 = ((int) std::floor (readPos) % len + len) % len, i1 = (i0 + 1) % len;
                const float frac = (float) (readPos - std::floor (readPos));
                const float x = line[(size_t) i0] + frac * (line[(size_t) i1] - line[(size_t) i0]);
                v.lp += v.dampCoef * (x - v.lp);
                const float out = v.lp * v.loopGain;
                line[(size_t) v.write] = out;
                v.write = (v.write + 1) % len;
                v.amp = juce::jmax (v.amp * 0.9997f, std::abs (out));
                if (v.amp < 1.0e-4f) { v.active = false; break; }
                addToOutputs (outputs, numOutputs, i, v.filter.process (out) * env * (0.4f + 0.6f * v.velocity) * v.gain * pv[PluckParams::level]);
            }
        }
    }
protected:
    void prepareImpl() override { for (auto& l : lines) l.assign ((size_t) (sampleRate / 25.0) + 4, 0.0f); }   // room for a G0
private:
    std::array<Voice, maxVoices> voices;
    std::array<std::vector<float>, maxVoices> lines;   // outside the voices: allocateVoice resets a voice by assignment
    std::uint32_t counter = 0;
    Noise noise;
};

//==============================================================================
// Organ: nine drawbars, each a sine at a harmonic of the note, plus percussion
// (a decaying burst on the second harmonic), key click and a global vibrato.

class OrganSynth final : public Instrument
{
    struct Voice : VoiceBase { std::array<double, 9> phase {}; double inc = 0.0; float perc = 0.0f, click = 0.0f; };
    static constexpr double ratios[9] = { 0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };
public:
    OrganSynth() : Instrument (InstrumentType::organ) {}
    void reset() override { for (auto& v : voices) v = {}; lfo = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = 0.7f + 0.3f * juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc = midiToHz (pitch) / sampleRate;
        for (size_t h = 0; h < 9; ++h) v.phase[h] = 0.1 * (double) h;
        v.perc = p.values[OrganParams::percussion]; v.click = p.values[OrganParams::click];
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        std::array<float, 9> bars;
        for (size_t h = 0; h < 9; ++h) { const float d = pv[h] / 8.0f; bars[h] = d * d * 0.35f; }   // drawbars are roughly logarithmic
        const float percCoef = (float) std::exp (-1.0 / juce::jmax (1.0, pv[OrganParams::percDecay] * sampleRate));
        const float clickCoef = (float) std::exp (-1.0 / (0.004 * sampleRate));
        const double lfoInc = pv[OrganParams::vibratoRate] / sampleRate;
        const float vib = pv[OrganParams::vibrato];
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
            const double bend = 1.0 + 0.007 * vib * std::sin (juce::MathConstants<double>::twoPi * lfo);
            float mix = 0.0f; bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, 0.04, sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (0.004, 0.01, 1.0f, sampleRate);
                if (! v.env.isActive()) { v.active = false; continue; }
                float sum = 0.0f;
                for (size_t h = 0; h < 9; ++h)
                {
                    if (bars[h] > 0.0f || (h == 3 && v.perc > 0.0f))
                        sum += (float) std::sin (juce::MathConstants<double>::twoPi * v.phase[h]) * (bars[h] + (h == 3 ? v.perc * 0.5f : 0.0f));
                    v.phase[h] += v.inc * ratios[h] * bend; if (v.phase[h] >= 1.0) v.phase[h] -= 1.0;
                }
                v.perc *= percCoef;
                sum += noise.next() * v.click * 0.3f; v.click *= clickCoef;
                mix += sum * env * v.velocity * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * pv[OrganParams::level]);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double lfo = 0.0;
    Noise noise;
};

//==============================================================================
// Stack: a "supersaw" of up to seven detuned saws per note, fanned out across
// the stereo field, through a filter with envelope.

class StackSynth final : public Instrument
{
    struct Voice : VoiceBase { std::array<double, 7> phase {}, inc {}; Svf filterR; };
public:
    StackSynth() : Instrument (InstrumentType::stack) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const auto& pv = p.values;
        const double f = midiToHz (pitch);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        for (int k = 0; k < 7; ++k)
        {
            const double spread = (k - 3) / 3.0;                     // -1 .. 1, the centre saw at 0
            v.inc[(size_t) k] = f * std::pow (2.0, spread * pv[StackParams::detune] / 1200.0) / sampleRate;
            v.phase[(size_t) k] = 0.5 + 0.5 * noise.next();          // free-running phases: no flanging on every note
        }
        v.filter.reset(); v.filterR.reset();
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const int count = juce::jlimit (1, 7, (int) std::lround (pv[StackParams::voices]));
        const float width = pv[StackParams::width], side = pv[StackParams::mix];
        // Which of the seven saws play: the centre first, then outward in pairs
        static constexpr int order[7] = { 3, 2, 4, 1, 5, 0, 6 };
        const float norm = 1.0f / std::sqrt ((float) count);
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[StackParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[StackParams::attack], pv[StackParams::decay], pv[StackParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                if (((i - offset) & 15) == 0)
                {
                    const double cutoff = pv[StackParams::cutoff] * std::pow (2.0, pv[StackParams::filterEnv] * env);
                    v.filter.setCutoff (cutoff, pv[StackParams::resonance], sampleRate);
                    v.filterR.setCutoff (cutoff, pv[StackParams::resonance], sampleRate);
                }
                float l = 0.0f, r = 0.0f;
                for (int c = 0; c < count; ++c)
                {
                    const int k = order[c];
                    const float saw = oscillator (Wave::saw, v.phase[(size_t) k], v.inc[(size_t) k]);
                    v.phase[(size_t) k] += v.inc[(size_t) k]; if (v.phase[(size_t) k] >= 1.0) v.phase[(size_t) k] -= 1.0;
                    const float pan = (float) (k - 3) / 3.0f * width;                       // -width .. width
                    const float g = (k == 3 ? 1.0f : side) * norm;
                    l += saw * g * (1.0f - juce::jmax (0.0f, pan));
                    r += saw * g * (1.0f + juce::jmin (0.0f, pan));
                }
                const float amp = env * v.velocity * v.gain * pv[StackParams::level];
                addStereo (outputs, numOutputs, i, v.filter.process (l) * amp, v.filterR.process (r) * amp);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    Noise noise;
};

//==============================================================================
// Chip: an 8-bit sound chip. Thin pulses, a stepped triangle and LFSR noise,
// quantised to a few bits, with vibrato and the classic chip arpeggio.

class ChipSynth final : public Instrument
{
    struct Voice : VoiceBase { double phase = 0.0, inc = 0.0; int arpStep = 0, arpCounter = 0; std::uint16_t lfsr = 0xACE1; float noiseHold = 0.0f; int noiseCounter = 0; };
public:
    ChipSynth() : Instrument (InstrumentType::chip) {}
    void reset() override { for (auto& v : voices) v = {}; lfo = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams&) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc = midiToHz (pitch) / sampleRate;
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const int wave = juce::jlimit (0, 4, (int) std::lround (pv[ChipParams::wave]));
        const int arp = juce::jlimit (0, 4, (int) std::lround (pv[ChipParams::arp]));
        static constexpr int arpNotes[5][3] = { { 0, 0, 0 }, { 0, 12, 0 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 7, 12 } };
        const int arpSamples = juce::jmax (1, (int) (sampleRate / juce::jmax (1.0f, pv[ChipParams::arpRate])));
        const float steps = (float) (1 << (juce::jlimit (2, 16, (int) std::lround (pv[ChipParams::bits])) - 1));
        const double lfoInc = pv[ChipParams::vibratoRate] / sampleRate;
        const float vibDepth = pv[ChipParams::vibratoDepth];
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
            const double bend = std::pow (2.0, 0.5 * vibDepth * std::sin (juce::MathConstants<double>::twoPi * lfo) / 12.0);   // up to a quarter tone
            float mix = 0.0f; bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[ChipParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (pv[ChipParams::attack], pv[ChipParams::decay], pv[ChipParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; continue; }
                if (arp > 0 && ++v.arpCounter >= arpSamples) { v.arpCounter = 0; v.arpStep = (v.arpStep + 1) % (arp == 1 ? 2 : 3); }
                const double inc = v.inc * bend * (arp > 0 ? std::pow (2.0, arpNotes[arp][v.arpStep] / 12.0) : 1.0);
                float x;
                if (wave == 4)
                {
                    // LFSR noise clocked at the note's pitch times 16, like a chip's noise channel
                    if (++v.noiseCounter >= juce::jmax (1, (int) (1.0 / (inc * 16.0))))
                    {
                        v.noiseCounter = 0;
                        const std::uint16_t bit = (std::uint16_t) (((v.lfsr >> 0) ^ (v.lfsr >> 2) ^ (v.lfsr >> 3) ^ (v.lfsr >> 5)) & 1u);
                        v.lfsr = (std::uint16_t) ((v.lfsr >> 1) | (bit << 15));
                        v.noiseHold = (v.lfsr & 1u) ? 1.0f : -1.0f;
                    }
                    x = v.noiseHold;
                }
                else if (wave == 3) x = (float) (4.0 * std::abs (v.phase - 0.5) - 1.0);
                else { const double duty = wave == 0 ? 0.125 : wave == 1 ? 0.25 : 0.5; x = v.phase < duty ? 1.0f : -1.0f; }
                v.phase += inc; if (v.phase >= 1.0) v.phase -= 1.0;
                x = std::round (x * steps) / steps;   // bit depth
                mix += x * env * v.velocity * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * pv[ChipParams::level]);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double lfo = 0.0;
};

//==============================================================================
// Vox: a detuned saw pair (plus breath noise) through three formant band-passes
// that morph between vowels; a slow drift moves the vowel by itself.

class VoxSynth final : public Instrument
{
    struct Voice : VoiceBase { double ph1 = 0.0, ph2 = 0.5, inc1 = 0.0, inc2 = 0.0; std::array<BandPass, 3> formants; int coefCounter = 0; };
    struct Vowel { double f[3]; float g[3]; };
    static constexpr Vowel vowels[5] = { { { 800.0, 1150.0, 2900.0 }, { 1.0f, 0.5f, 0.25f } },     // A
                                         { { 400.0, 1600.0, 2700.0 }, { 1.0f, 0.35f, 0.2f } },     // E
                                         { { 270.0, 2300.0, 3000.0 }, { 1.0f, 0.2f, 0.15f } },     // I
                                         { { 450.0, 800.0, 2830.0 }, { 1.0f, 0.6f, 0.1f } },       // O
                                         { { 325.0, 700.0, 2530.0 }, { 1.0f, 0.35f, 0.08f } } };   // U
public:
    VoxSynth() : Instrument (InstrumentType::vox) {}
    void reset() override { for (auto& v : voices) v = {}; lfo = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const double f = midiToHz (pitch), det = p.values[VoxParams::width];
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc1 = f * std::pow (2.0, -det / 1200.0) / sampleRate;
        v.inc2 = f * std::pow (2.0,  det / 1200.0) / sampleRate;
        for (auto& b : v.formants) b.reset();
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const double lfoInc = pv[VoxParams::driftRate] / sampleRate;
        const float breath = pv[VoxParams::breath], tone = pv[VoxParams::tone];
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
            // The vowel position wanders up to one vowel either way with Drift, wrapping around the five
            double pos = pv[VoxParams::vowel] + pv[VoxParams::drift] * std::sin (juce::MathConstants<double>::twoPi * lfo);
            pos = std::fmod (pos + 5.0, 5.0);
            const int a = (int) std::floor (pos), b = (a + 1) % 5;
            const float t = (float) (pos - a);
            float mix = 0.0f; bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[VoxParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (pv[VoxParams::attack], pv[VoxParams::decay], pv[VoxParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; continue; }
                if (v.coefCounter-- <= 0)
                {
                    v.coefCounter = 31;
                    for (size_t k = 0; k < 3; ++k)
                        v.formants[k].set (vowels[a].f[k] + t * (vowels[b].f[k] - vowels[a].f[k]), k == 0 ? 8.0 : 12.0, sampleRate);
                }
                float src = 0.5f * (oscillator (Wave::saw, v.ph1, v.inc1) + oscillator (Wave::saw, v.ph2, v.inc2));
                v.ph1 += v.inc1; if (v.ph1 >= 1.0) v.ph1 -= 1.0;
                v.ph2 += v.inc2; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
                src = src * (1.0f - 0.7f * breath) + noise.next() * breath * 0.6f;
                float y = 0.0f;
                for (size_t k = 0; k < 3; ++k)
                {
                    const float g = vowels[a].g[k] + t * (vowels[b].g[k] - vowels[a].g[k]);
                    y += v.formants[k].process (src) * g * (k == 0 ? 1.0f : 0.5f + tone);
                }
                mix += y * env * v.velocity * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * pv[VoxParams::level] * 2.5f);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double lfo = 0.0;
    Noise noise;
};

//==============================================================================
// Piano: each note is two slightly detuned strings, each a set of inharmonic
// partials (stiffness stretches the upper ones) that decay faster the higher
// they are; hardness and velocity set how many partials the hammer excites.

class PianoSynth final : public Instrument
{
    static constexpr int partials = 8;
    struct Voice : VoiceBase { std::array<double, partials * 2> phase {}, inc {}; std::array<float, partials * 2> amp {}, coef {}; float thump = 0.0f, thumpCoef = 1.0f, peak = 1.0f; };
public:
    PianoSynth() : Instrument (InstrumentType::piano) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const auto& pv = p.values;
        const double f = midiToHz (pitch);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        // Inharmonicity grows with pitch, as on a real piano; the two strings of the note beat at Beat Hz (a tuner's
        // unison spread), which keeps the shimmer even from the bass to the treble
        const double B = 0.00012 * (0.2 + pv[PianoParams::stiffness]) * std::pow (2.0, (pitch - 48) / 18.0);
        const double f2 = f + pv[PianoParams::detune];
        const float bright = 0.25f + 0.75f * pv[PianoParams::hardness] * (0.35f + 0.65f * v.velocity);
        const double decaySeconds = pv[PianoParams::decay] * std::pow (0.5, (pitch - 48) / 24.0);   // higher notes die sooner
        for (int k = 0; k < partials; ++k)
        {
            const double n = k + 1, stretch = n * std::sqrt (1.0 + B * n * n);
            v.inc[(size_t) k] = f * stretch / sampleRate; v.inc[(size_t) (k + partials)] = f2 * stretch / sampleRate;
            v.phase[(size_t) k] = 0.0; v.phase[(size_t) (k + partials)] = 0.25;
            v.amp[(size_t) k] = (float) std::pow (bright, k) / (float) n * (k == 0 ? 1.0f : 0.5f + pv[PianoParams::tone]);
            v.coef[(size_t) k] = (float) std::exp (-1.0 / juce::jmax (0.005, decaySeconds / (1.0 + 0.6 * k) * sampleRate));
            // The second string is a little quieter and dies sooner: the prompt sound and the aftersound of a real
            // piano, and the pair never cancels completely as they beat
            v.amp[(size_t) (k + partials)] = v.amp[(size_t) k] * 0.75f;
            v.coef[(size_t) (k + partials)] = (float) std::exp (-1.0 / juce::jmax (0.005, 0.7 * decaySeconds / (1.0 + 0.6 * k) * sampleRate));
        }
        v.thump = pv[PianoParams::thump] * 0.6f * v.velocity; v.thumpCoef = (float) std::exp (-1.0 / (0.008 * sampleRate));
        v.peak = 1.0f;
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[PianoParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (0.0015, 100.0, 1.0f, sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                float sum = 0.0f, total = 0.0f;
                for (int k = 0; k < partials * 2; ++k)
                {
                    const float a = v.amp[(size_t) k];
                    if (a < 1.0e-5f) continue;
                    sum += a * (float) std::sin (juce::MathConstants<double>::twoPi * v.phase[(size_t) k]);
                    v.phase[(size_t) k] += v.inc[(size_t) k]; if (v.phase[(size_t) k] >= 1.0) v.phase[(size_t) k] -= 1.0;
                    v.amp[(size_t) k] = a * v.coef[(size_t) k];
                    total += a;
                }
                sum += noise.next() * v.thump; v.thump *= v.thumpCoef;
                if (total < 1.0e-4f) { v.active = false; break; }
                addToOutputs (outputs, numOutputs, i, sum * 0.5f * env * (0.3f + 0.7f * v.velocity) * v.gain * pv[PianoParams::level]);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    Noise noise;
};

//==============================================================================
// Strings: three detuned saws per note whose detune slowly wanders (Movement),
// vibrato that comes in after the attack, and a bow filter.

class StringsSynth final : public Instrument
{
    struct Voice : VoiceBase { std::array<double, 3> phase {}; double inc = 0.0; float age = 0.0f; };
public:
    StringsSynth() : Instrument (InstrumentType::strings) {}
    void reset() override { for (auto& v : voices) v = {}; lfo = 0.0; slow1 = 0.0; slow2 = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc = midiToHz (pitch) / sampleRate;
        for (auto& ph : v.phase) ph = 0.5 + 0.5 * noise.next();
        v.age = 0.0f;
        v.filter.reset();
        v.filter.setCutoff (p.values[StringsParams::bow] * (0.6 + 0.6 * v.velocity), 0.1f, sampleRate);
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const double lfoInc = pv[StringsParams::vibratoRate] / sampleRate;
        const float move = pv[StringsParams::movement];
        const double ens = pv[StringsParams::ensemble];
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
            slow1 += 0.23 / sampleRate; if (slow1 >= 1.0) slow1 -= 1.0;   // two slow, unrelated wobbles make the ensemble breathe
            slow2 += 0.31 / sampleRate; if (slow2 >= 1.0) slow2 -= 1.0;
            const double vib = std::sin (juce::MathConstants<double>::twoPi * lfo);
            const double d1 = ens * (1.0 + 0.5 * move * std::sin (juce::MathConstants<double>::twoPi * slow1));
            const double d2 = ens * (1.0 + 0.5 * move * std::sin (juce::MathConstants<double>::twoPi * slow2 + 2.0));
            float mix = 0.0f; bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[StringsParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (pv[StringsParams::attack], pv[StringsParams::decay], pv[StringsParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; continue; }
                v.age = juce::jmin (1.0f, v.age + (float) (1.0 / (0.35 * sampleRate)));   // vibrato fades in over the first third of a second
                const double bend = std::pow (2.0, pv[StringsParams::vibrato] * v.age * 0.25 * vib / 12.0);
                const double incs[3] = { v.inc * bend, v.inc * bend * std::pow (2.0, d1 / 1200.0), v.inc * bend * std::pow (2.0, -d2 / 1200.0) };
                float sum = 0.0f;
                const int count = ens > 0.01 ? 3 : 1;
                for (int k = 0; k < count; ++k)
                {
                    sum += oscillator (Wave::saw, v.phase[(size_t) k], incs[k]);
                    v.phase[(size_t) k] += incs[k]; if (v.phase[(size_t) k] >= 1.0) v.phase[(size_t) k] -= 1.0;
                }
                mix += v.filter.process (sum / (float) count) * env * (0.4f + 0.6f * v.velocity) * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * pv[StringsParams::level]);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double lfo = 0.0, slow1 = 0.0, slow2 = 0.0;
    Noise noise;
};

//==============================================================================
// Mallets: a struck bar or tine as its first modes, each a sine at the
// instrument's ratio that decays at its own rate; a harder mallet brings out
// the upper modes. Vibraphone adds tremolo, kalimba a buzzy strike.

class MalletSynth final : public Instrument
{
    static constexpr int modes = 4;
    struct Voice : VoiceBase { std::array<double, modes> phase {}, inc {}; std::array<float, modes> amp {}, coef {}; float strike = 0.0f, strikeCoef = 1.0f; };
    struct Bar { double ratio[modes]; float weight[modes]; double decay; };
    static constexpr Bar bars[4] = { { { 1.0, 3.93, 9.53, 14.2 }, { 1.0f, 0.5f, 0.25f, 0.1f }, 0.7 },      // marimba: tuned bar, short
                                     { { 1.0, 4.0, 10.0, 18.4 }, { 1.0f, 0.35f, 0.15f, 0.05f }, 4.0 },     // vibraphone: long ring
                                     { { 1.0, 2.71, 5.15, 8.86 }, { 1.0f, 0.6f, 0.4f, 0.25f }, 2.5 },      // glockenspiel: free bar, bright
                                     { { 1.0, 4.6, 10.4, 17.0 }, { 1.0f, 0.3f, 0.12f, 0.05f }, 1.2 } };    // kalimba tine
public:
    MalletSynth() : Instrument (InstrumentType::mallets) {}
    void reset() override { for (auto& v : voices) v = {}; trem = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const auto& pv = p.values;
        const auto& bar = bars[juce::jlimit (0, 3, (int) std::lround (pv[MalletParams::instrument]))];
        const double f = midiToHz (pitch);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        const float hard = 0.2f + 0.8f * pv[MalletParams::hardness] * (0.5f + 0.5f * v.velocity);
        const double decaySeconds = bar.decay * pv[MalletParams::decay] * std::pow (0.5, (pitch - 60) / 24.0);
        for (int k = 0; k < modes; ++k)
        {
            v.inc[(size_t) k] = f * bar.ratio[k] / sampleRate;
            v.phase[(size_t) k] = 0.0;
            v.amp[(size_t) k] = bar.weight[k] * (k == 0 ? 1.0f : (float) std::pow (hard, k));
            v.coef[(size_t) k] = (float) std::exp (-1.0 / juce::jmax (0.003, decaySeconds / (1.0 + 1.2 * k) * sampleRate));
        }
        v.strike = pv[MalletParams::strike] * (0.3f + 0.7f * hard) * 0.45f; v.strikeCoef = (float) std::exp (-1.0 / (0.003 * sampleRate));
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const double tremInc = pv[MalletParams::tremoloRate] / sampleRate;
        const float tremDepth = pv[MalletParams::tremoloDepth];
        for (int i = 0; i < n; ++i)
        {
            trem += tremInc; if (trem >= 1.0) trem -= 1.0;
            const float tremGain = 1.0f - tremDepth * 0.5f * (1.0f + (float) std::sin (juce::MathConstants<double>::twoPi * trem));
            float mix = 0.0f; bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[MalletParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (0.0008, 100.0, 1.0f, sampleRate);
                if (! v.env.isActive()) { v.active = false; continue; }
                float sum = 0.0f, total = 0.0f;
                for (int k = 0; k < modes; ++k)
                {
                    sum += v.amp[(size_t) k] * (float) std::sin (juce::MathConstants<double>::twoPi * v.phase[(size_t) k]);
                    v.phase[(size_t) k] += v.inc[(size_t) k]; if (v.phase[(size_t) k] >= 1.0) v.phase[(size_t) k] -= 1.0;
                    v.amp[(size_t) k] *= v.coef[(size_t) k];
                    total += v.amp[(size_t) k];
                }
                sum += noise.next() * v.strike; v.strike *= v.strikeCoef;
                if (total < 1.0e-4f) { v.active = false; continue; }
                mix += sum * env * (0.3f + 0.7f * v.velocity) * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * tremGain * pv[MalletParams::level]);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double trem = 0.0;
    Noise noise;
};

//==============================================================================
// Brass: two detuned saws through a filter that opens in a "blat" as the note
// starts, with a pitch dip into each note and vibrato that arrives late.

class BrassSynth final : public Instrument
{
    struct Voice : VoiceBase { double ph1 = 0.0, ph2 = 0.5, inc = 0.0; float blat = 0.0f, blatUp = 0.0f, dip = 1.0f, age = 0.0f; };
public:
    BrassSynth() : Instrument (InstrumentType::brass) {}
    void reset() override { for (auto& v : voices) v = {}; lfo = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc = midiToHz (pitch) / sampleRate;
        v.blat = 0.0f; v.blatUp = 1.0f; v.dip = (float) std::pow (2.0, -p.values[BrassParams::dip] / 12.0); v.age = 0.0f;
        v.filter.reset();
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const double lfoInc = pv[BrassParams::vibratoRate] / sampleRate;
        const float upStep = (float) (1.0 / juce::jmax (1.0, pv[BrassParams::blatTime] * sampleRate));
        const float settle = (float) std::exp (-1.0 / (0.25 * sampleRate));     // the blat relaxes to half over about a quarter second
        const float dipCoef = (float) std::exp (-1.0 / (0.05 * sampleRate));
        const double det = std::pow (2.0, pv[BrassParams::detune] / 1200.0);
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
            const double vib = std::sin (juce::MathConstants<double>::twoPi * lfo);
            float mix = 0.0f; bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[BrassParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (pv[BrassParams::attack], 0.15, 0.85f, sampleRate);
                if (! v.env.isActive()) { v.active = false; continue; }
                // Filter envelope: up over Blat Time, then down to half and held
                if (v.blatUp > 0.0f) { v.blat += upStep; if (v.blat >= 1.0f) { v.blat = 1.0f; v.blatUp = 0.0f; } }
                else v.blat = 0.5f + (v.blat - 0.5f) * settle;
                v.dip = 1.0f + (v.dip - 1.0f) * dipCoef;
                v.age = juce::jmin (1.0f, v.age + (float) (1.0 / (0.4 * sampleRate)));
                if ((i & 15) == 0)
                    v.filter.setCutoff (pv[BrassParams::cutoff] * std::pow (2.0, pv[BrassParams::blat] * v.blat) * (0.7 + 0.5 * v.velocity), pv[BrassParams::resonance], sampleRate);
                const double bend = v.dip * std::pow (2.0, pv[BrassParams::vibrato] * v.age * 0.3 * vib / 12.0);
                const double inc1 = v.inc * bend, inc2 = v.inc * bend * det;
                float sum = oscillator (Wave::saw, v.ph1, inc1) + oscillator (Wave::saw, v.ph2, inc2);
                v.ph1 += inc1; if (v.ph1 >= 1.0) v.ph1 -= 1.0;
                v.ph2 += inc2; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
                mix += v.filter.process (sum * 0.5f) * env * (0.4f + 0.6f * v.velocity) * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * pv[BrassParams::level]);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double lfo = 0.0;
};

//==============================================================================
// Flute: a soft tone (sine with a touch of the octave) plus breath noise
// band-passed at the note (Air) and a broadband chiff at the start; vibrato
// after a delay.

class FluteSynth final : public Instrument
{
    struct Voice : VoiceBase { double ph = 0.0, inc = 0.0; BandPass air; float chiff = 0.0f, chiffCoef = 1.0f, age = 0.0f, hp = 0.0f; };
public:
    FluteSynth() : Instrument (InstrumentType::flute) {}
    void reset() override { for (auto& v : voices) v = {}; lfo = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const auto& pv = p.values;
        const double f = midiToHz (pitch);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc = f / sampleRate;
        v.air.reset(); v.air.set (f, 6.0 + 20.0 * (1.0 - pv[FluteParams::air]), sampleRate);   // narrower = more whistle, wider = more hiss
        v.chiff = pv[FluteParams::chiff] * 0.5f; v.chiffCoef = (float) std::exp (-1.0 / (0.04 * sampleRate));
        v.age = 0.0f; v.hp = 0.0f;
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const double lfoInc = pv[FluteParams::vibratoRate] / sampleRate;
        const float breath = pv[FluteParams::breath], over = pv[FluteParams::overblow];
        const float ageStep = (float) (1.0 / juce::jmax (0.01, pv[FluteParams::vibratoDelay] * sampleRate));
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
            const double vib = std::sin (juce::MathConstants<double>::twoPi * lfo);
            float mix = 0.0f; bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[FluteParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (pv[FluteParams::attack], 0.1, 0.9f, sampleRate);
                if (! v.env.isActive()) { v.active = false; continue; }
                v.age = juce::jmin (1.0f, v.age + ageStep);
                const double inc = v.inc * std::pow (2.0, pv[FluteParams::vibrato] * v.age * 0.2 * vib / 12.0);
                const float tone = (float) std::sin (juce::MathConstants<double>::twoPi * v.ph) + over * 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::twoPi * v.ph)
                                 + 0.08f * (float) std::sin (3.0 * juce::MathConstants<double>::twoPi * v.ph);
                v.ph += inc; if (v.ph >= 1.0) v.ph -= 1.0;
                const float white = noise.next();
                v.hp += 0.3f * (white - v.hp);                                   // a little of the hiss stays broadband
                const float breathy = v.air.process (white) * 2.5f + (white - v.hp) * 0.08f;
                float x = tone * (1.0f - 0.5f * breath) + breathy * breath;
                x += (white - v.hp) * v.chiff; v.chiff *= v.chiffCoef;
                mix += x * env * (0.5f + 0.5f * v.velocity) * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * pv[FluteParams::level]);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double lfo = 0.0;
    Noise noise;
};

//==============================================================================

std::unique_ptr<Instrument> Instrument::create (InstrumentType t, double sr)
{
    std::unique_ptr<Instrument> inst;
    switch (t)
    {
        case InstrumentType::subtractive:   inst = std::make_unique<SubtractiveSynth>(); break;
        case InstrumentType::fm:            inst = std::make_unique<FmSynth>(); break;
        case InstrumentType::wavetable:     inst = std::make_unique<WavetableSynth>(); break;
        case InstrumentType::sampler:       inst = std::make_unique<Sampler>(); break;
        case InstrumentType::electricPiano: inst = std::make_unique<ElectricPiano>(); break;
        case InstrumentType::bass:          inst = std::make_unique<BassSynth>(); break;
        case InstrumentType::pluck:         inst = std::make_unique<PluckSynth>(); break;
        case InstrumentType::organ:         inst = std::make_unique<OrganSynth>(); break;
        case InstrumentType::stack:         inst = std::make_unique<StackSynth>(); break;
        case InstrumentType::chip:          inst = std::make_unique<ChipSynth>(); break;
        case InstrumentType::vox:           inst = std::make_unique<VoxSynth>(); break;
        case InstrumentType::piano:         inst = std::make_unique<PianoSynth>(); break;
        case InstrumentType::strings:       inst = std::make_unique<StringsSynth>(); break;
        case InstrumentType::mallets:       inst = std::make_unique<MalletSynth>(); break;
        case InstrumentType::brass:         inst = std::make_unique<BrassSynth>(); break;
        case InstrumentType::flute:         inst = std::make_unique<FluteSynth>(); break;
        case InstrumentType::none:          return nullptr;
    }
    inst->prepare (sr);
    return inst;
}

} // namespace beatmaker::engine
