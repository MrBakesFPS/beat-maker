#include "DrumKitFactory.h"
#include <cmath>
#include <functional>
#include <algorithm>

namespace beatmaker::engine
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    constexpr float kitHeadroom = 0.5f;   // -6 dB

    struct Synth
    {
        double sr;
        juce::Random rng { 0x5EED };

        double noise() { return rng.nextDouble() * 2.0 - 1.0; }

        // Render `seconds` of mono audio from a per-sample function of time,
        // normalise to `peak`, wrap in a shared buffer.
        DrumSample make (const juce::String& name, double seconds, float peak,
                         const std::function<double (double t, int i)>& fn)
        {
            const int length = (int) std::ceil (seconds * sr);
            juce::AudioBuffer<float> b (1, length);
            auto* out = b.getWritePointer (0);

            for (int i = 0; i < length; ++i)
                out[i] = (float) fn (i / sr, i);

            // Short fade-out so nothing clicks at the end.
            const int fade = juce::jmin (256, length / 4);
            for (int i = 0; i < fade; ++i)
                out[length - 1 - i] *= (float) i / (float) fade;

            // `peak` values are relative levels; the whole kit sits about 6 dB
            // below full scale so a busy pattern still leaves mix headroom.
            const float mag = b.getMagnitude (0, 0, length);
            if (mag > 0.0f)
                b.applyGain (peak * kitHeadroom / mag);

            DrumSample s;
            s.name = name;
            s.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
            return s;
        }

        // A sine whose frequency glides exponentially from f0 to f1 with time constant tau.
        std::function<double (double, int)> glideSine (double f0, double f1, double tau, double decay)
        {
            auto phase = std::make_shared<double> (0.0);
            return [=, this] (double t, int)
            {
                const double f = f1 + (f0 - f1) * std::exp (-t / tau);
                *phase += twoPi * f / sr;
                return std::sin (*phase) * std::exp (-t * decay);
            };
        }

        // First-difference "high-passed" noise burst.
        std::function<double (double, int)> hpNoise (double decay, double attack = 0.0)
        {
            auto last = std::make_shared<double> (0.0);
            return [=, this] (double t, int)
            {
                const double n = noise();
                const double hp = n - *last;
                *last = n;
                const double env = std::exp (-t * decay) * (attack > 0.0 ? juce::jmin (1.0, t / attack) : 1.0);
                return hp * env;
            };
        }

        // A plain sine at one pitch with an exponential decay.
        std::function<double (double, int)> sine (double hz, double decay)
        {
            return [=] (double t, int) { return std::sin (twoPi * hz * t) * std::exp (-t * decay); };
        }

        // Six square waves at inharmonic ratios, high-passed twice: the metallic tone of analogue hats, rides
        // and cowbells (the 808's cymbals are built exactly like this). `bright` moves the cluster up.
        std::function<double (double, int)> metal (double decay, double bright = 1.0, double attack = 0.0)
        {
            auto last1 = std::make_shared<double> (0.0);
            auto last2 = std::make_shared<double> (0.0);
            return [=] (double t, int)
            {
                static constexpr double ratios[6] = { 1.0, 1.34, 1.72, 2.31, 2.76, 3.47 };
                double sum = 0.0;
                for (double r : ratios) sum += std::fmod (t * 380.0 * bright * r, 1.0) < 0.5 ? 1.0 : -1.0;
                const double hp1 = sum - *last1; *last1 = sum;
                const double hp2 = hp1 - *last2; *last2 = hp1;
                const double env = std::exp (-t * decay) * (attack > 0.0 ? juce::jmin (1.0, t / attack) : 1.0);
                return hp2 / 12.0 * env;
            };
        }

        // Runs another generator through a one-pole low-pass (coef 0..1, higher = brighter).
        std::function<double (double, int)> lowpass (std::function<double (double, int)> fn, double coef)
        {
            auto state = std::make_shared<double> (0.0);
            return [=] (double t, int i) { *state += coef * (fn (t, i) - *state); return *state; };
        }

        // Bit-crushes and sample-holds another generator: the grit of an old sampler.
        std::function<double (double, int)> crush (std::function<double (double, int)> fn, int bits, int hold)
        {
            auto held = std::make_shared<double> (0.0);
            const double steps = (double) (1 << (bits - 1));
            return [=] (double t, int i)
            {
                if (i % juce::jmax (1, hold) == 0) *held = std::round (fn (t, i) * steps) / steps;
                return *held;
            };
        }

        // Adds `amount` of hissy noise under another generator while it sounds (a dusty sample's floor).
        std::function<double (double, int)> dusty (std::function<double (double, int)> fn, double amount, double decay)
        {
            return [=, this] (double t, int i) { return fn (t, i) + noise() * amount * std::exp (-t * decay); };
        }
    };

    void buildStudio (Synth&, DrumKit&);
    void build808 (Synth&, DrumKit&);
    void build909 (Synth&, DrumKit&);
    void buildLoFi (Synth&, DrumKit&);
    void buildPercussion (Synth&, DrumKit&);

    // A drum-kit style: the numbers that make a rock kit a rock kit and a 606 a 606. The standard sixteen pads are
    // built from it, named with `prefix`. `bits`/`hold`/`lp`/`dust` run the whole kit through an old sampler.
    struct Style
    {
        const char* prefix;
        double kickF0, kickF1, kickTau, kickDecay, kickClick, kickDrive;
        double snareF, snareDecay, snareNoise, snareNoiseDecay;
        double hatBright, hatClosedDecay, hatOpenDecay, hatNoise;
        double tomDecay, tomDrop;
        double crashSeconds, rideSeconds, roomTail;
        int bits, hold; double lp, dust;
    };

    void buildStyled (Synth& s, DrumKit& kit, const Style& st)
    {
        enum { kick = DrumKitFactory::kick, snare = DrumKitFactory::snare, clap = DrumKitFactory::clap, rim = DrumKitFactory::rim, closedHat = DrumKitFactory::closedHat,
               openHat = DrumKitFactory::openHat, lowTom = DrumKitFactory::lowTom, midTom = DrumKitFactory::midTom, highTom = DrumKitFactory::highTom, crash = DrumKitFactory::crash,
               ride = DrumKitFactory::ride, cowbell = DrumKitFactory::cowbell, shaker = DrumKitFactory::shaker, clave = DrumKitFactory::clave, conga = DrumKitFactory::conga, sub = DrumKitFactory::sub };
        const juce::String pre = juce::String (st.prefix) + " ";
        // The sampler stage, applied to every pad when the style asks for it
        auto post = [&s, &st] (std::function<double (double, int)> fn) -> std::function<double (double, int)>
        {
            if (st.bits > 0) fn = s.crush (fn, st.bits, juce::jmax (1, st.hold));
            if (st.lp > 0.0) fn = s.lowpass (fn, st.lp);
            if (st.dust > 0.0) fn = s.dusty (fn, st.dust, 10.0);
            return fn;
        };
        // A room: a little reverberant noise tail under the hits
        auto room = [&s, &st] (std::function<double (double, int)> fn, double amount) -> std::function<double (double, int)>
        {
            if (st.roomTail <= 0.0) return fn;
            auto nz = s.hpNoise (1.0 / juce::jmax (0.02, st.roomTail));
            auto lp = std::make_shared<double> (0.0);
            return [=] (double t, int i) { *lp += 0.12 * (nz (t, i) - *lp); return fn (t, i) + *lp * amount * st.roomTail * juce::jmin (1.0, t / 0.01); };
        };
        const double kickLen = juce::jlimit (0.3, 1.6, 6.0 / st.kickDecay + st.roomTail);
        kit.pads[kick] = s.make (pre + "Kick", kickLen, 0.95, post (room ([body = s.glideSine (st.kickF0, st.kickF1, st.kickTau, st.kickDecay), nz = s.hpNoise (400.0), drive = st.kickDrive, click = st.kickClick] (double t, int i)
                                                                        { return std::tanh (body (t, i) * drive) / std::tanh (drive) + click * nz (t, i); }, 0.3)));
        kit.pads[snare] = s.make (pre + "Snare", juce::jlimit (0.25, 0.8, 5.0 / st.snareNoiseDecay + st.roomTail), 0.85,
                                  post (room ([tone = s.glideSine (st.snareF * 1.15, st.snareF, 0.02, st.snareDecay), nz = s.hpNoise (st.snareNoiseDecay), mix = st.snareNoise] (double t, int i) { return (1.0 - 0.5 * mix) * tone (t, i) + mix * nz (t, i); }, 0.5)));
        kit.pads[clap] = s.make (pre + "Clap", 0.45, 0.8, post (room ([nz = s.hpNoise (0.0)] (double t, int i)
        {
            const double burst = t < 0.03 ? std::exp (-std::fmod (t, 0.01) * 300.0) : std::exp (-(t - 0.03) * 16.0);
            return nz (t, i) * burst;
        }, 0.5)));
        kit.pads[rim] = s.make (pre + "Rim", 0.12, 0.7, post ([tone = s.glideSine (1700.0, 1500.0, 0.01, 55.0), nz = s.hpNoise (120.0)] (double t, int i) { return tone (t, i) + 0.5 * nz (t, i); }));
        kit.pads[closedHat] = s.make (pre + "Closed Hat", juce::jlimit (0.06, 0.3, 4.0 / st.hatClosedDecay), 0.6,
                                      post ([m = s.metal (st.hatClosedDecay, st.hatBright), nz = s.hpNoise (st.hatClosedDecay * 1.2), mix = st.hatNoise] (double t, int i) { return (1.0 - mix) * m (t, i) + mix * nz (t, i); }));
        kit.pads[openHat] = s.make (pre + "Open Hat", juce::jlimit (0.3, 1.2, 4.0 / st.hatOpenDecay), 0.6,
                                    post ([m = s.metal (st.hatOpenDecay, st.hatBright), nz = s.hpNoise (st.hatOpenDecay * 1.2), mix = st.hatNoise] (double t, int i) { return (1.0 - mix) * m (t, i) + mix * nz (t, i); }));
        const double tomF[3] = { 150.0, 200.0, 270.0 };
        const int tomPads[3] = { lowTom, midTom, highTom };
        const char* tomNames[3] = { "Low Tom", "Mid Tom", "High Tom" };
        for (int k = 0; k < 3; ++k)
            kit.pads[tomPads[k]] = s.make (pre + tomNames[k], juce::jlimit (0.3, 1.0, 5.0 / st.tomDecay + st.roomTail), 0.85,
                                           post (room ([b = s.glideSine (tomF[k], tomF[k] * st.tomDrop, 0.05, st.tomDecay + k * 1.2), nz = s.hpNoise (200.0)] (double t, int i) { return b (t, i) + 0.2 * nz (t, i); }, 0.3)));
        kit.pads[crash] = s.make (pre + "Crash", st.crashSeconds, 0.7, post ([nz = s.hpNoise (2.0 / st.crashSeconds * 2.0), m = s.metal (2.0 / st.crashSeconds * 2.0, st.hatBright * 1.2)] (double t, int i) { return 0.6 * nz (t, i) + 0.5 * m (t, i); }));
        kit.pads[ride]  = s.make (pre + "Ride", st.rideSeconds, 0.6, post ([nz = s.hpNoise (3.5 / st.rideSeconds * 1.3), m = s.metal (3.5 / st.rideSeconds * 1.3, st.hatBright), ping = s.sine (2200.0 * st.hatBright, 8.0)] (double t, int i) { return 0.35 * nz (t, i) + 0.5 * m (t, i) + 0.3 * ping (t, i); }));
        kit.pads[cowbell] = s.make (pre + "Cowbell", 0.4, 0.7, post ([] (double t, int)
        {
            const double a = std::fmod (t * 587.0, 1.0) < 0.5 ? 1.0 : -1.0, b = std::fmod (t * 845.0, 1.0) < 0.5 ? 1.0 : -1.0;
            return (a + b) * 0.5 * std::exp (-t * 14.0);
        }));
        kit.pads[shaker] = s.make (pre + "Shaker", 0.14, 0.5, post (s.hpNoise (28.0, 0.012)));
        kit.pads[clave]  = s.make (pre + "Clave", 0.1, 0.7, post (s.glideSine (2500.0, 2450.0, 0.01, 42.0)));
        kit.pads[conga]  = s.make (pre + "Conga", 0.35, 0.8, post (s.glideSine (240.0, 215.0, 0.02, 13.0)));
        kit.pads[sub]    = s.make (pre + "Sub", 1.1, 0.9, post (s.glideSine (st.kickF1 * 1.1, st.kickF1 * 0.85, 0.1, 3.2)));
    }

    //                     prefix        kick f0   f1  tau   decay click drive | snare f dec noise ndec | hat bright cl op noise | tom dec drop | crash ride room | bits hold lp dust
    const Style styles[] = {
        { "Rock",        170.0, 55.0, 0.035, 6.0, 0.5, 1.6,   200.0, 20.0, 0.55, 12.0,   1.2, 45.0, 6.0, 0.4,   6.0, 0.62,   2.2, 1.6, 0.25,   0, 0, 0.0, 0.0 },
        { "Jazz",        190.0, 75.0, 0.03,  9.0, 0.2, 1.1,   240.0, 18.0, 0.45, 16.0,   1.4, 60.0, 5.0, 0.5,   9.0, 0.75,   1.6, 2.2, 0.18,   0, 0, 0.0, 0.0 },
        { "Vintage",     150.0, 58.0, 0.04,  8.0, 0.15, 1.4,  185.0, 22.0, 0.5, 18.0,    0.9, 55.0, 8.0, 0.5,   8.0, 0.7,    1.4, 1.2, 0.1,    0, 0, 0.55, 0.0 },
        { "Room",        165.0, 52.0, 0.035, 5.0, 0.4, 1.5,   210.0, 16.0, 0.6, 9.0,     1.3, 40.0, 5.0, 0.35,  5.0, 0.6,    2.5, 1.8, 0.45,   0, 0, 0.0, 0.0 },
        { "Brush",       160.0, 70.0, 0.03,  10.0, 0.05, 1.0, 230.0, 14.0, 0.8, 7.0,     1.1, 30.0, 4.0, 0.7,   9.0, 0.78,   1.2, 1.5, 0.15,   0, 0, 0.5, 0.0 },
        { "606",         240.0, 62.0, 0.012, 12.0, 0.6, 1.2,  260.0, 30.0, 0.6, 26.0,    2.0, 80.0, 9.0, 0.0,   12.0, 0.7,   1.0, 0.9, 0.0,    0, 0, 0.0, 0.0 },
        { "Linn",        160.0, 55.0, 0.03,  7.0, 0.3, 1.5,   200.0, 22.0, 0.55, 14.0,   1.1, 50.0, 6.0, 0.6,   7.0, 0.65,   1.5, 1.3, 0.0,    12, 2, 0.6, 0.0 },
        { "Electro",     300.0, 48.0, 0.015, 8.0, 0.7, 2.2,   350.0, 28.0, 0.35, 20.0,   1.8, 70.0, 7.0, 0.1,   14.0, 0.45,  1.3, 1.0, 0.0,    0, 0, 0.0, 0.0 },
        { "Techno",      200.0, 50.0, 0.02,  6.5, 0.6, 3.5,   190.0, 24.0, 0.7, 13.0,    1.5, 60.0, 7.0, 0.3,   8.0, 0.6,    1.8, 1.4, 0.05,   0, 0, 0.0, 0.0 },
        { "House",       185.0, 54.0, 0.022, 7.0, 0.45, 1.9,  205.0, 21.0, 0.6, 11.0,    1.6, 55.0, 5.5, 0.35,  8.0, 0.65,   2.0, 1.6, 0.1,    0, 0, 0.0, 0.0 },
        { "Boom Bap",    150.0, 52.0, 0.035, 7.5, 0.35, 1.7,  195.0, 24.0, 0.55, 15.0,   1.0, 55.0, 7.0, 0.55,  7.0, 0.65,   1.3, 1.1, 0.08,   10, 2, 0.35, 0.02 },
        { "Trap",        140.0, 40.0, 0.03,  3.2, 0.4, 1.8,   220.0, 26.0, 0.5, 18.0,    2.2, 90.0, 12.0, 0.2,  6.0, 0.6,    1.4, 1.0, 0.0,    0, 0, 0.0, 0.0 },
        { "Drill",       130.0, 36.0, 0.05,  2.8, 0.3, 1.9,   215.0, 24.0, 0.45, 20.0,   2.0, 85.0, 10.0, 0.25, 6.0, 0.6,    1.2, 1.0, 0.0,    0, 0, 0.0, 0.0 },
        { "Neo Soul",    155.0, 60.0, 0.04,  8.5, 0.1, 1.2,   180.0, 20.0, 0.5, 12.0,    0.9, 45.0, 6.0, 0.6,   8.0, 0.7,    1.2, 1.3, 0.12,   0, 0, 0.45, 0.01 },
        { "SP Vintage",  160.0, 55.0, 0.035, 7.0, 0.3, 1.5,   200.0, 22.0, 0.55, 14.0,   1.0, 55.0, 7.0, 0.5,   7.0, 0.65,   1.2, 1.0, 0.05,   12, 4, 0.3, 0.03 } };

    // World kits: sixteen hand percussion instruments each, described pad by pad
    struct PadSpec { const char* name; int kind; double a, b, c, d, e, seconds, peak; };   // kind: 0 membrane, 1 drum+noise, 2 metal, 3 noise, 4 sine pair, 5 bell, 6 guiro, 7 rising membrane
    struct WorldKit { const char* name; PadSpec pads[16]; };

    std::function<double (double, int)> padGenerator (Synth& s, const PadSpec& p)
    {
        switch (p.kind)
        {
            case 0: return s.glideSine (p.a, p.b, p.c, p.d);                                                                                  // a f0, b f1, c tau, d decay
            case 1: return [b = s.glideSine (p.a, p.b, p.c, p.d), nz = s.hpNoise (p.d * 4.0), mix = p.e] (double t, int i) { return b (t, i) + mix * nz (t, i); };
            case 2: return [m = s.metal (p.a, p.b, p.c), nz = s.hpNoise (p.a * 1.5, p.c), mix = p.e] (double t, int i) { return (1.0 - mix) * m (t, i) + mix * nz (t, i); };   // a decay, b bright, c attack, e noise
            case 3: return s.hpNoise (p.a, p.b);                                                                                              // a decay, b attack
            case 4: return [x = s.sine (p.a, p.b), y = s.sine (p.c, p.d), mix = p.e] (double t, int i) { return x (t, i) + mix * y (t, i); };  // two sines
            case 5: return [f1 = p.a, f2 = p.b, decay = p.c] (double t, int) { const double x = std::fmod (t * f1, 1.0) < 0.5 ? 1.0 : -1.0, y = std::fmod (t * f2, 1.0) < 0.5 ? 1.0 : -1.0; return (x + y) * 0.5 * std::exp (-t * decay); };
            case 6: return [nz = s.hpNoise (p.a, 0.01), rate = p.b] (double t, int i) { return nz (t, i) * (0.55 + 0.45 * std::sin (twoPi * rate * t)); };
            default: return s.glideSine (p.a, p.b, p.c, p.d);                                                                                 // 7: a < b rises (tabla)
        }
    }

    const WorldKit worldKits[] = {
        { "Latin", { { "Surdo", 1, 95.0, 70.0, 0.05, 5.0, 0.25, 0.8, 0.95 }, { "Timbale", 1, 330.0, 300.0, 0.02, 11.0, 0.7, 0.4, 0.85 }, { "Bongo Hi", 0, 520.0, 460.0, 0.015, 22.0, 0, 0.25, 0.8 }, { "Bongo Lo", 0, 380.0, 340.0, 0.015, 18.0, 0, 0.3, 0.8 },
                    { "Cabasa", 3, 55.0, 0.008, 0, 0, 0, 0.12, 0.55 }, { "Maracas", 3, 30.0, 0.012, 0, 0, 0, 0.2, 0.55 }, { "Conga Low", 0, 200.0, 180.0, 0.02, 9.0, 0, 0.5, 0.85 }, { "Conga Mid", 0, 250.0, 225.0, 0.02, 10.0, 0, 0.45, 0.85 },
                    { "Conga Slap", 1, 330.0, 290.0, 0.015, 16.0, 0.5, 0.3, 0.85 }, { "Timbale Bell", 5, 640.0, 960.0, 10.0, 0, 0, 0.6, 0.6 }, { "Cowbell", 5, 587.0, 845.0, 14.0, 0, 0, 0.4, 0.7 }, { "Agogo", 4, 720.0, 14.0, 1080.0, 16.0, 0.5, 0.4, 0.7 },
                    { "Guiro", 6, 10.0, 38.0, 0, 0, 0, 0.3, 0.55 }, { "Clave", 4, 2500.0, 55.0, 3700.0, 60.0, 0.2, 0.1, 0.7 }, { "Cuica", 7, 260.0, 420.0, 0.08, 12.0, 0, 0.35, 0.8 }, { "Bombo", 0, 70.0, 55.0, 0.08, 3.5, 0, 1.0, 0.9 } } },
        { "Afro",  { { "Djembe Bass", 1, 110.0, 80.0, 0.04, 6.0, 0.2, 0.7, 0.95 }, { "Djembe Slap", 1, 320.0, 240.0, 0.015, 14.0, 0.6, 0.35, 0.85 }, { "Djembe Tone", 0, 260.0, 220.0, 0.02, 10.0, 0, 0.4, 0.85 }, { "Talking Drum", 7, 180.0, 300.0, 0.15, 9.0, 0, 0.5, 0.8 },
                    { "Shekere", 3, 40.0, 0.01, 0, 0, 0, 0.18, 0.55 }, { "Shekere Long", 3, 12.0, 0.03, 0, 0, 0, 0.4, 0.55 }, { "Dundun Low", 1, 90.0, 65.0, 0.06, 4.5, 0.15, 0.9, 0.95 }, { "Dundun Mid", 1, 140.0, 105.0, 0.05, 6.0, 0.15, 0.7, 0.9 },
                    { "Dundun High", 1, 200.0, 150.0, 0.04, 8.0, 0.15, 0.5, 0.9 }, { "Gankogui Low", 5, 480.0, 730.0, 9.0, 0, 0, 0.6, 0.6 }, { "Gankogui High", 5, 720.0, 1090.0, 11.0, 0, 0, 0.5, 0.6 }, { "Agogo", 4, 760.0, 14.0, 1140.0, 16.0, 0.5, 0.4, 0.7 },
                    { "Caxixi", 3, 45.0, 0.006, 0, 0, 0, 0.14, 0.55 }, { "Slit Drum", 4, 380.0, 12.0, 570.0, 14.0, 0.4, 0.4, 0.7 }, { "Udu", 7, 120.0, 190.0, 0.1, 7.0, 0, 0.6, 0.85 }, { "Log Drum Low", 4, 190.0, 8.0, 285.0, 9.0, 0.4, 0.7, 0.8 } } },
        { "Taiko", { { "Odaiko", 1, 80.0, 55.0, 0.07, 3.5, 0.3, 1.4, 1.0 }, { "Shime", 1, 420.0, 380.0, 0.01, 22.0, 0.6, 0.25, 0.85 }, { "Nagado Rim", 1, 1200.0, 1000.0, 0.005, 60.0, 0.7, 0.12, 0.7 }, { "Kane", 4, 1800.0, 9.0, 2700.0, 11.0, 0.5, 0.8, 0.6 },
                    { "Shime Roll", 3, 60.0, 0.005, 0, 0, 0, 0.1, 0.55 }, { "Chappa", 2, 12.0, 2.0, 0.0, 0, 0.3, 0.5, 0.6 }, { "Nagado Low", 1, 120.0, 85.0, 0.06, 4.5, 0.25, 1.0, 0.95 }, { "Nagado Mid", 1, 160.0, 115.0, 0.05, 5.5, 0.25, 0.8, 0.9 },
                    { "Nagado High", 1, 210.0, 150.0, 0.04, 7.0, 0.25, 0.6, 0.9 }, { "Gong", 2, 1.2, 0.6, 0.3, 0, 0.2, 2.5, 0.7 }, { "Atarigane", 4, 2300.0, 6.0, 3450.0, 8.0, 0.4, 1.0, 0.55 }, { "Hyoshigi", 4, 2100.0, 50.0, 3100.0, 60.0, 0.3, 0.12, 0.7 },
                    { "Suzu Bells", 2, 6.0, 3.2, 0.02, 0, 0.4, 0.6, 0.5 }, { "Fue Chirp", 7, 1800.0, 2600.0, 0.05, 20.0, 0, 0.15, 0.6 }, { "Uchiwa", 1, 300.0, 250.0, 0.02, 12.0, 0.4, 0.35, 0.8 }, { "Odaiko Deep", 0, 60.0, 48.0, 0.1, 2.8, 0, 1.6, 0.95 } } },
        { "Indian", { { "Dhol Bass", 1, 100.0, 70.0, 0.05, 5.0, 0.2, 0.8, 0.95 }, { "Dhol Tak", 1, 700.0, 600.0, 0.008, 30.0, 0.6, 0.15, 0.8 }, { "Tabla Na", 4, 560.0, 18.0, 1120.0, 24.0, 0.3, 0.3, 0.8 }, { "Tabla Tin", 4, 640.0, 26.0, 1280.0, 30.0, 0.2, 0.2, 0.75 },
                    { "Kanjira", 3, 50.0, 0.006, 0, 0, 0, 0.12, 0.55 }, { "Ghungroo", 2, 9.0, 3.0, 0.02, 0, 0.5, 0.5, 0.5 }, { "Bayan Ghe", 7, 90.0, 140.0, 0.12, 8.0, 0, 0.45, 0.9 }, { "Bayan Ke", 1, 110.0, 95.0, 0.01, 20.0, 0.3, 0.2, 0.85 },
                    { "Dholak Hi", 1, 300.0, 260.0, 0.015, 14.0, 0.4, 0.3, 0.85 }, { "Manjira", 4, 3200.0, 5.0, 4800.0, 7.0, 0.5, 1.0, 0.5 }, { "Ghatam", 7, 150.0, 230.0, 0.06, 9.0, 0, 0.4, 0.85 }, { "Mridangam Thom", 1, 85.0, 65.0, 0.06, 4.0, 0.15, 0.9, 0.95 },
                    { "Mridangam Dhin", 4, 330.0, 10.0, 660.0, 14.0, 0.4, 0.5, 0.8 }, { "Chenda", 1, 400.0, 340.0, 0.01, 18.0, 0.7, 0.25, 0.85 }, { "Damaru", 7, 220.0, 330.0, 0.05, 15.0, 0, 0.25, 0.8 }, { "Nagara", 0, 75.0, 60.0, 0.08, 3.2, 0, 1.2, 0.9 } } },
        { "Middle East", { { "Darbuka Dum", 1, 140.0, 95.0, 0.04, 7.0, 0.2, 0.55, 0.95 }, { "Darbuka Tek", 1, 900.0, 800.0, 0.006, 40.0, 0.6, 0.12, 0.8 }, { "Darbuka Ka", 1, 1100.0, 950.0, 0.005, 50.0, 0.7, 0.1, 0.75 }, { "Darbuka Slap", 1, 500.0, 420.0, 0.01, 22.0, 0.6, 0.2, 0.85 },
                    { "Riq Jingles", 2, 25.0, 3.4, 0.005, 0, 0.3, 0.2, 0.55 }, { "Riq Shake", 2, 7.0, 3.4, 0.03, 0, 0.4, 0.5, 0.55 }, { "Bendir", 1, 110.0, 85.0, 0.05, 5.0, 0.25, 0.8, 0.9 }, { "Daf", 1, 130.0, 100.0, 0.04, 6.0, 0.3, 0.7, 0.9 },
                    { "Tar", 1, 190.0, 160.0, 0.03, 9.0, 0.35, 0.45, 0.85 }, { "Sagat", 4, 4200.0, 6.0, 6300.0, 8.0, 0.5, 0.8, 0.5 }, { "Zil", 4, 3600.0, 4.0, 5100.0, 6.0, 0.5, 1.2, 0.5 }, { "Tabla Baladi", 7, 120.0, 180.0, 0.1, 8.0, 0, 0.5, 0.9 },
                    { "Frame Rim", 4, 1500.0, 40.0, 2250.0, 50.0, 0.3, 0.12, 0.7 }, { "Cajon Bass", 1, 90.0, 70.0, 0.05, 6.0, 0.35, 0.6, 0.95 }, { "Cajon Slap", 1, 600.0, 500.0, 0.008, 28.0, 0.8, 0.18, 0.85 }, { "Davul", 0, 70.0, 52.0, 0.08, 3.5, 0, 1.2, 0.95 } } } };

    struct Entry { DrumKitFactory::KitInfo info; std::function<void (Synth&, DrumKit&)> build; };
    const std::vector<Entry>& entries()
    {
        static const std::vector<Entry> all = []
        {
            std::vector<Entry> v;
            v.push_back ({ { "Studio Kit", "Acoustic", "A tight, dry kit: a punchy kick, a snappy snare with a noise crack, clap, rimshot, three toms, hats, crash and ride, plus cowbell, shaker, clave, conga and a sub. The kit every new drum track starts with." }, buildStudio });
            const char* desc[] = {
                "A big rock kit: a driven kick with click, a loud cracking snare, ringing toms, a long crash and a bright ride, with a little room. Rock, punk, indie.",
                "A small jazz kit: a high tuned kick with a long ring, a lively snare, warm toms, a soft ride and closed hats that speak. Swing, bop, ballads.",
                "A dampened seventies kit: a dead kick, a fat low snare, muffled toms, darker cymbals, all a touch soft. Soul, funk, classic pop.",
                "A kit in a live room: every hit carries a short reverberant tail, the snare is loose and bright, the crash rings out. Indie, gospel, live sound.",
                "Brushed and soft: a gentle kick, a snare that is mostly wire noise, quiet toms, dark cymbals. Ballads, folk, quiet jazz.",
                "A thin, buzzy little box: a fast clicking kick, a fizzy snare, hats and cymbals of pure metal, toms that zap. Minimal, early techno, indie electronica.",
                "A crunchy twelve-bit machine: gated snare, tight kick, dry hats with a bit of grain. Eighties pop, synth-funk, R&B.",
                "Electro: a zapping high kick, a laser snare, falling toms, sharp bright hats. Electro, breakdance, freestyle.",
                "Techno: a distorted, saturated kick, a noisy snare, hard hats, an industrial edge. Techno, industrial, hard dance.",
                "House: a rounded punchy kick, a crisp snare and clap, bright hats, warm toms. House, garage, disco.",
                "Boom bap: a thumping kick, a cracking snare, dusty hats, everything a touch crushed and dark. Golden-era hip-hop.",
                "Trap: a long booming sub kick, a sharp snare, rattling bright hats that roll. Trap, modern hip-hop.",
                "Drill: an even deeper sliding kick, a hard snare, cold hats. UK and NY drill.",
                "Neo soul: a soft round kick, a warm papery snare, gentle hats, all smooth and dark. Neo soul, R&B, lo-fi jazz.",
                "An old twelve-bit sampler: held samples with grit and hiss, a dusty punchy kick, a papery snare. Beat tapes, lo-fi hip-hop." };
            const char* cats[] = { "Acoustic", "Acoustic", "Acoustic", "Acoustic", "Acoustic", "Electronic", "Electronic", "Electronic", "Electronic", "Electronic",
                                   "Hip-Hop", "Hip-Hop", "Hip-Hop", "Hip-Hop", "Hip-Hop" };
            const char* names[] = { "Rock Kit", "Jazz Kit", "Vintage Kit", "Room Kit", "Brush Kit", "606", "Linn", "Electro", "Techno Kit", "House Kit",
                                    "Boom Bap", "Trap", "Drill", "Neo Soul", "SP Vintage" };
            // Interleave so each category lists its bundled classics first
            v.push_back ({ { "808", "Electronic", "The classic analogue drum machine: a long booming kick, a snappy snare, metallic hats and cowbell made from square waves, and a deep sub. Hip-hop, trap, electro, pop." }, build808 });
            v.push_back ({ { "909", "Electronic", "The house and techno machine: a hard clicking kick, a crunchy snare, bright metallic hats, big noise crash and ride. Four-on-the-floor of every kind." }, build909 });
            v.push_back ({ { "Lo-Fi", "Hip-Hop", "The studio kit put through an old sampler: bit-crushed, low-passed and dusty, with a soft thud of a kick and a papery snare. Boom bap, lo-fi beats, chillhop." }, buildLoFi });
            v.push_back ({ { "Percussion", "World", "Hand and stick percussion instead of a drum kit: surdo, timbale, bongo, woodblock, cabasa, shaker, three congas, bell tree, triangle, agogo, guiro, tabla, djembe and a low drum. Latin, Afro, world rhythms." }, buildPercussion });
            for (int i = 0; i < 15; ++i)
                v.push_back ({ { names[i], cats[i], desc[i] }, [i] (Synth& s, DrumKit& k) { buildStyled (s, k, styles[i]); } });
            const char* worldDesc[] = {
                "Latin percussion: surdo, timbale and its bell, bongos, cabasa, maracas, congas, cowbell, agogo, guiro, clave, cuica and a bombo. Salsa, samba, cumbia, bossa.",
                "West African: djembe bass, slap and tone, a talking drum, shekere, the three dunduns, gankogui bells, agogo, caxixi, slit drum, udu and a log drum. Afrobeat, highlife, traditional.",
                "Japanese taiko: the great odaiko, shime, nagado drums, kane and atarigane bells, chappa cymbals, a gong, hyoshigi clappers and suzu bells. Taiko ensembles, cinematic drums.",
                "Indian: dhol, tabla strokes, bayan slides, dholak, kanjira, ghungroo, manjira, ghatam, mridangam, chenda, damaru and nagara. Bhangra, classical, film.",
                "Middle Eastern: darbuka dum, tek, ka and slap, riq jingles, bendir, daf, tar, sagat and zil finger cymbals, tabla baladi, cajon and davul. Arabic, Turkish, Persian, flamenco." };
            for (int i = 0; i < 5; ++i)
                v.push_back ({ { worldKits[i].name, "World", worldDesc[i] }, [i] (Synth& s, DrumKit& k)
                {
                    for (int pad = 0; pad < DrumKit::numPads; ++pad)
                    {
                        const auto& ps = worldKits[i].pads[pad];
                        k.pads[(size_t) pad] = s.make (ps.name, ps.seconds, (float) ps.peak, padGenerator (s, ps));
                    }
                } });
            // Sort each category's members together, in the order above
            std::vector<Entry> sorted;
            for (const char* cat : { "Acoustic", "Electronic", "Hip-Hop", "World" })
                for (const auto& e : v) if (e.info.category == cat) sorted.push_back (e);
            return sorted;
        }();
        return all;
    }

    std::vector<DrumKitFactory::CustomKit>& customKitsStore() { static std::vector<DrumKitFactory::CustomKit> kits; return kits; }
    std::vector<DrumKitFactory::KitInfo>& allInfos() { static std::vector<DrumKitFactory::KitInfo> infos; return infos; }
    void rebuildInfos()
    {
        auto& infos = allInfos();
        infos.clear();
        for (const auto& e : entries()) infos.push_back (e.info);
        for (const auto& c : customKitsStore())
        {
            juce::StringArray sources;
            for (const auto& p : c.pads) if (! sources.contains (p.kit)) sources.add (p.kit);
            infos.push_back ({ c.name, DrumKitFactory::customCategory(), "Your own kit, with pads from " + sources.joinIntoString (", ") + ". Build Your Own in the kit chooser edits it.", true });
        }
    }
}

const std::vector<DrumKitFactory::KitInfo>& DrumKitFactory::availableKits()
{
    if (allInfos().empty()) rebuildInfos();
    return allInfos();
}

std::vector<juce::String> DrumKitFactory::categories()
{
    std::vector<juce::String> out;
    for (const auto& k : availableKits()) if (std::find (out.begin(), out.end(), k.category) == out.end()) out.push_back (k.category);
    return out;
}

const DrumKitFactory::KitInfo* DrumKitFactory::info (const juce::String& name)
{
    for (const auto& k : availableKits()) if (name == k.name) return &k;
    return nullptr;
}

void DrumKitFactory::setCustomKits (std::vector<CustomKit> kits)
{
    // A custom kit's name may not shadow a bundled kit, and each name appears once
    std::vector<CustomKit> kept;
    for (auto& k : kits)
    {
        bool bundled = false;
        for (const auto& e : entries()) if (e.info.name == k.name) bundled = true;
        bool dup = false;
        for (const auto& other : kept) if (other.name == k.name) dup = true;
        if (! bundled && ! dup && k.name.isNotEmpty()) kept.push_back (std::move (k));
    }
    customKitsStore() = std::move (kept);
    rebuildInfos();
}

const std::vector<DrumKitFactory::CustomKit>& DrumKitFactory::customKits() { return customKitsStore(); }

const DrumKitFactory::CustomKit* DrumKitFactory::customKit (const juce::String& name)
{
    for (const auto& k : customKitsStore()) if (k.name == name) return &k;
    return nullptr;
}

std::shared_ptr<const DrumKit> DrumKitFactory::createCustomKit (const CustomKit& def, double sampleRate)
{
    auto kit = std::make_shared<DrumKit>();
    kit->name = def.name;
    std::vector<std::pair<juce::String, std::shared_ptr<const DrumKit>>> sources;   // each source kit synthesised once
    for (int pad = 0; pad < DrumKit::numPads; ++pad)
    {
        const auto& src = def.pads[(size_t) pad];
        std::shared_ptr<const DrumKit> source;
        for (const auto& s : sources) if (s.first == src.kit) source = s.second;
        if (source == nullptr)
        {
            // Bundled sources only inside a custom kit: another custom kit is resolved to its own sources, so no kit refers to itself
            if (const auto* other = customKit (src.kit); other != nullptr && other->name != def.name)
                source = createCustomKit (*other, sampleRate);
            else
                source = createKit (customKit (src.kit) != nullptr ? juce::String (defaultKitName()) : src.kit, sampleRate);
            sources.push_back ({ src.kit, source });
        }
        kit->pads[(size_t) pad] = source->pads[(size_t) juce::jlimit (0, DrumKit::numPads - 1, src.pad)];
    }
    return kit;
}

std::shared_ptr<const DrumKit> DrumKitFactory::createKit (const juce::String& name, double sampleRate)
{
    if (const auto* custom = customKit (name)) return createCustomKit (*custom, sampleRate);
    const Entry* entry = &entries().front();
    for (const auto& e : entries()) if (name == e.info.name) entry = &e;
    Synth s { sampleRate };
    auto kit = std::make_shared<DrumKit>();
    kit->name = entry->info.name;
    entry->build (s, *kit);
    return kit;
}

namespace
{

void buildStudio (Synth& s, DrumKit& kitRef)
{
    using namespace std;
    auto* kit = &kitRef;
    enum { kick = DrumKitFactory::kick, snare = DrumKitFactory::snare, clap = DrumKitFactory::clap, rim = DrumKitFactory::rim, closedHat = DrumKitFactory::closedHat,
           openHat = DrumKitFactory::openHat, lowTom = DrumKitFactory::lowTom, midTom = DrumKitFactory::midTom, highTom = DrumKitFactory::highTom, crash = DrumKitFactory::crash,
           ride = DrumKitFactory::ride, cowbell = DrumKitFactory::cowbell, shaker = DrumKitFactory::shaker, clave = DrumKitFactory::clave, conga = DrumKitFactory::conga, sub = DrumKitFactory::sub };

    kit->pads[kick] = s.make ("Kick", 0.6, 0.95, [&, body = s.glideSine (160.0, 48.0, 0.04, 7.0)] (double t, int i)
    {
        const double click = std::exp (-t * 400.0) * s.noise() * 0.4;
        return body (t, i) + click;
    });

    kit->pads[snare] = s.make ("Snare", 0.35, 0.85, [&, tone = s.glideSine (220.0, 170.0, 0.02, 22.0), nz = s.hpNoise (14.0)] (double t, int i)
    {
        return tone (t, i) * 0.5 + nz (t, i) * 0.8;
    });

    kit->pads[clap] = s.make ("Clap", 0.4, 0.8, [&, nz = s.hpNoise (0.0)] (double t, int i)
    {
        double env = 0.0;
        for (double burst : { 0.0, 0.011, 0.022 })
            if (t >= burst) env = juce::jmax (env, std::exp (-(t - burst) * 90.0));
        if (t > 0.03) env = juce::jmax (env, std::exp (-(t - 0.03) * 12.0) * 0.8);
        return nz (t, i) * env;
    });

    kit->pads[rim] = s.make ("Rimshot", 0.12, 0.7, [&, tone = s.glideSine (1700.0, 1500.0, 0.01, 55.0), nz = s.hpNoise (120.0)] (double t, int i)
    {
        return tone (t, i) * 0.7 + nz (t, i) * 0.5;
    });

    kit->pads[closedHat] = s.make ("Closed Hat", 0.09, 0.6, s.hpNoise (48.0));
    kit->pads[openHat]   = s.make ("Open Hat",   0.55, 0.6, [&, nz = s.hpNoise (6.5)] (double t, int i)
    {
        return nz (t, i) + std::sin (twoPi * 6200.0 * t) * std::exp (-t * 9.0) * 0.15;
    });

    kit->pads[lowTom]  = s.make ("Low Tom",  0.55, 0.85, s.glideSine (150.0, 95.0,  0.06, 6.0));
    kit->pads[midTom]  = s.make ("Mid Tom",  0.45, 0.85, s.glideSine (200.0, 135.0, 0.05, 7.0));
    kit->pads[highTom] = s.make ("High Tom", 0.35, 0.85, s.glideSine (270.0, 190.0, 0.04, 8.5));

    kit->pads[crash] = s.make ("Crash", 2.0, 0.7, [&, nz = s.hpNoise (2.0)] (double t, int i)
    {
        double shimmer = 0.0;
        for (double f : { 3100.0, 4700.0, 6900.0 })
            shimmer += std::sin (twoPi * f * t) * std::exp (-t * 3.0);
        return nz (t, i) + shimmer * 0.08;
    });

    kit->pads[ride] = s.make ("Ride", 1.3, 0.6, [&, nz = s.hpNoise (3.5)] (double t, int i)
    {
        const double ping = std::sin (twoPi * 3600.0 * t) * std::exp (-t * 4.5) * 0.35
                          + std::sin (twoPi * 5400.0 * t) * std::exp (-t * 6.0) * 0.2;
        return nz (t, i) * 0.6 + ping;
    });

    kit->pads[cowbell] = s.make ("Cowbell", 0.4, 0.7, [] (double t, int)
    {
        const double env = std::exp (-t * 11.0);
        const double a = std::sin (twoPi * 587.0 * t), b = std::sin (twoPi * 845.0 * t);
        return (juce::jlimit (-0.6, 0.6, a) + juce::jlimit (-0.6, 0.6, b)) * env;
    });

    kit->pads[shaker] = s.make ("Shaker", 0.14, 0.5, s.hpNoise (28.0, 0.012));
    kit->pads[clave]  = s.make ("Clave",  0.1,  0.7, s.glideSine (2500.0, 2450.0, 0.01, 42.0));
    kit->pads[conga]  = s.make ("Conga",  0.35, 0.8, s.glideSine (240.0, 215.0, 0.02, 13.0));
    kit->pads[sub]    = s.make ("Sub",    1.1,  0.9, s.glideSine (52.0, 44.0, 0.1, 3.2));
}

#define BM_PADS enum { kick = DrumKitFactory::kick, snare = DrumKitFactory::snare, clap = DrumKitFactory::clap, rim = DrumKitFactory::rim, closedHat = DrumKitFactory::closedHat, \
           openHat = DrumKitFactory::openHat, lowTom = DrumKitFactory::lowTom, midTom = DrumKitFactory::midTom, highTom = DrumKitFactory::highTom, crash = DrumKitFactory::crash, \
           ride = DrumKitFactory::ride, cowbell = DrumKitFactory::cowbell, shaker = DrumKitFactory::shaker, clave = DrumKitFactory::clave, conga = DrumKitFactory::conga, sub = DrumKitFactory::sub }

void build808 (Synth& s, DrumKit& kit)
{
    BM_PADS;
    kit.pads[kick] = s.make ("808 Kick", 1.0, 1.0, [body = s.glideSine (150.0, 46.0, 0.025, 3.8)] (double t, int i) { return std::tanh (body (t, i) * 1.6); });
    kit.pads[snare] = s.make ("808 Snare", 0.3, 0.85, [tone = s.glideSine (238.0, 176.0, 0.02, 24.0), nz = s.hpNoise (20.0)] (double t, int i) { return 0.55 * tone (t, i) + 0.6 * nz (t, i); });
    kit.pads[clap] = s.make ("808 Clap", 0.45, 0.8, [nz = s.hpNoise (0.0)] (double t, int i)
    {
        const double burst = t < 0.03 ? std::exp (-std::fmod (t, 0.01) * 300.0) : std::exp (-(t - 0.03) * 16.0);
        return nz (t, i) * burst;
    });
    kit.pads[rim] = s.make ("808 Rim", 0.1, 0.7, [tone = s.glideSine (1000.0, 950.0, 0.01, 70.0), nz = s.hpNoise (150.0)] (double t, int i) { return tone (t, i) + 0.4 * nz (t, i); });
    kit.pads[closedHat] = s.make ("808 Closed Hat", 0.1, 0.6, s.metal (60.0));
    kit.pads[openHat]   = s.make ("808 Open Hat", 0.6, 0.6, s.metal (7.0));
    kit.pads[lowTom]  = s.make ("808 Low Tom",  0.6, 0.85, s.glideSine (110.0, 80.0,  0.05, 6.0));
    kit.pads[midTom]  = s.make ("808 Mid Tom",  0.5, 0.85, s.glideSine (160.0, 120.0, 0.04, 7.0));
    kit.pads[highTom] = s.make ("808 High Tom", 0.4, 0.85, s.glideSine (230.0, 170.0, 0.03, 8.0));
    kit.pads[crash] = s.make ("808 Crash", 1.8, 0.65, [m = s.metal (2.2, 1.4), nz = s.hpNoise (2.5)] (double t, int i) { return 0.6 * m (t, i) + 0.5 * nz (t, i); });
    kit.pads[ride]  = s.make ("808 Ride", 1.2, 0.55, s.metal (3.5, 1.2));
    kit.pads[cowbell] = s.make ("808 Cowbell", 0.4, 0.7, [] (double t, int)
    {
        const double a = std::fmod (t * 587.0, 1.0) < 0.5 ? 1.0 : -1.0, b = std::fmod (t * 845.0, 1.0) < 0.5 ? 1.0 : -1.0;
        return (a + b) * 0.5 * std::exp (-t * 14.0);
    });
    kit.pads[shaker] = s.make ("808 Maracas", 0.12, 0.5, s.hpNoise (45.0, 0.01));
    kit.pads[clave]  = s.make ("808 Clave", 0.09, 0.7, s.sine (2500.0, 55.0));
    kit.pads[conga]  = s.make ("808 Conga", 0.35, 0.8, s.glideSine (260.0, 230.0, 0.02, 12.0));
    kit.pads[sub]    = s.make ("808 Sub", 1.4, 0.95, s.glideSine (60.0, 40.0, 0.08, 2.4));
}

void build909 (Synth& s, DrumKit& kit)
{
    BM_PADS;
    kit.pads[kick] = s.make ("909 Kick", 0.55, 1.0, [body = s.glideSine (210.0, 52.0, 0.018, 7.5), nz = s.hpNoise (400.0)] (double t, int i) { return std::tanh (body (t, i) * 1.8) + 0.5 * nz (t, i); });
    kit.pads[snare] = s.make ("909 Snare", 0.35, 0.9, [tone = s.glideSine (200.0, 180.0, 0.02, 22.0), nz = s.hpNoise (12.0)] (double t, int i) { return 0.5 * tone (t, i) + 0.8 * nz (t, i); });
    kit.pads[clap] = s.make ("909 Clap", 0.5, 0.85, [nz = s.hpNoise (0.0)] (double t, int i)
    {
        const double burst = t < 0.04 ? std::exp (-std::fmod (t, 0.0125) * 260.0) : std::exp (-(t - 0.04) * 12.0);
        return nz (t, i) * burst;
    });
    kit.pads[rim] = s.make ("909 Rim", 0.1, 0.7, [tone = s.glideSine (1800.0, 1650.0, 0.01, 60.0), nz = s.hpNoise (140.0)] (double t, int i) { return tone (t, i) + 0.5 * nz (t, i); });
    kit.pads[closedHat] = s.make ("909 Closed Hat", 0.12, 0.6, [m = s.metal (55.0, 1.6), nz = s.hpNoise (70.0)] (double t, int i) { return 0.7 * m (t, i) + 0.4 * nz (t, i); });
    kit.pads[openHat]   = s.make ("909 Open Hat", 0.7, 0.6, [m = s.metal (6.0, 1.6), nz = s.hpNoise (7.0)] (double t, int i) { return 0.7 * m (t, i) + 0.4 * nz (t, i); });
    kit.pads[lowTom]  = s.make ("909 Low Tom",  0.5, 0.85, [b = s.glideSine (170.0, 100.0, 0.05, 7.0), nz = s.hpNoise (200.0)] (double t, int i) { return b (t, i) + 0.3 * nz (t, i); });
    kit.pads[midTom]  = s.make ("909 Mid Tom",  0.45, 0.85, [b = s.glideSine (220.0, 140.0, 0.045, 8.0), nz = s.hpNoise (200.0)] (double t, int i) { return b (t, i) + 0.3 * nz (t, i); });
    kit.pads[highTom] = s.make ("909 High Tom", 0.4, 0.85, [b = s.glideSine (300.0, 200.0, 0.04, 9.0), nz = s.hpNoise (200.0)] (double t, int i) { return b (t, i) + 0.3 * nz (t, i); });
    kit.pads[crash] = s.make ("909 Crash", 2.2, 0.7, [nz = s.hpNoise (1.8), m = s.metal (2.0, 1.8)] (double t, int i) { return 0.8 * nz (t, i) + 0.3 * m (t, i); });
    kit.pads[ride]  = s.make ("909 Ride", 1.5, 0.6, [nz = s.hpNoise (3.0), m = s.metal (3.0, 1.5)] (double t, int i) { return 0.5 * nz (t, i) + 0.6 * m (t, i); });
    kit.pads[cowbell] = s.make ("909 Bell", 0.35, 0.65, [] (double t, int)
    {
        const double a = std::fmod (t * 640.0, 1.0) < 0.5 ? 1.0 : -1.0, b = std::fmod (t * 960.0, 1.0) < 0.5 ? 1.0 : -1.0;
        return (a + b) * 0.5 * std::exp (-t * 16.0);
    });
    kit.pads[shaker] = s.make ("909 Shaker", 0.14, 0.5, s.hpNoise (35.0, 0.012));
    kit.pads[clave]  = s.make ("909 Clave", 0.1, 0.7, s.sine (2200.0, 45.0));
    kit.pads[conga]  = s.make ("909 Conga", 0.35, 0.8, s.glideSine (250.0, 220.0, 0.02, 12.0));
    kit.pads[sub]    = s.make ("909 Sub", 1.0, 0.9, s.glideSine (58.0, 46.0, 0.08, 3.0));
}

void buildLoFi (Synth& s, DrumKit& kit)
{
    BM_PADS;
    // The studio kit's recipes, then an old sampler: 8 bits, held every third sample, a dark low-pass and some dust
    auto lofi = [&s] (std::function<double (double, int)> fn, double bright = 0.25, double dust = 0.02) { return s.dusty (s.lowpass (s.crush (fn, 8, 3), bright), dust, 10.0); };
    kit.pads[kick] = s.make ("Dusty Kick", 0.5, 0.95, lofi ([body = s.glideSine (140.0, 50.0, 0.035, 8.0)] (double t, int i) { return std::tanh (body (t, i) * 1.4); }, 0.2, 0.015));
    kit.pads[snare] = s.make ("Paper Snare", 0.28, 0.85, lofi ([tone = s.glideSine (210.0, 170.0, 0.02, 24.0), nz = s.hpNoise (16.0)] (double t, int i) { return 0.5 * tone (t, i) + 0.7 * nz (t, i); }, 0.3, 0.03));
    kit.pads[clap] = s.make ("Dusty Clap", 0.35, 0.8, lofi ([nz = s.hpNoise (0.0)] (double t, int i)
    {
        const double burst = t < 0.03 ? std::exp (-std::fmod (t, 0.01) * 300.0) : std::exp (-(t - 0.03) * 18.0);
        return nz (t, i) * burst;
    }, 0.3, 0.03));
    kit.pads[rim] = s.make ("Lo-Fi Rim", 0.1, 0.7, lofi (s.glideSine (1500.0, 1300.0, 0.01, 55.0), 0.4));
    kit.pads[closedHat] = s.make ("Dusty Closed Hat", 0.08, 0.55, lofi (s.hpNoise (55.0), 0.5, 0.03));
    kit.pads[openHat]   = s.make ("Dusty Open Hat", 0.45, 0.55, lofi (s.hpNoise (7.5), 0.45, 0.03));
    kit.pads[lowTom]  = s.make ("Lo-Fi Low Tom",  0.45, 0.85, lofi (s.glideSine (140.0, 90.0,  0.06, 7.0)));
    kit.pads[midTom]  = s.make ("Lo-Fi Mid Tom",  0.4, 0.85, lofi (s.glideSine (190.0, 130.0, 0.05, 8.0)));
    kit.pads[highTom] = s.make ("Lo-Fi High Tom", 0.35, 0.85, lofi (s.glideSine (250.0, 180.0, 0.04, 9.0)));
    kit.pads[crash] = s.make ("Dusty Crash", 1.4, 0.6, lofi (s.hpNoise (2.5), 0.35, 0.04));
    kit.pads[ride]  = s.make ("Dusty Ride", 1.0, 0.55, lofi (s.metal (4.0), 0.35, 0.03));
    kit.pads[cowbell] = s.make ("Lo-Fi Cowbell", 0.3, 0.65, lofi ([] (double t, int)
    {
        const double a = std::fmod (t * 560.0, 1.0) < 0.5 ? 1.0 : -1.0, b = std::fmod (t * 845.0, 1.0) < 0.5 ? 1.0 : -1.0;
        return (a + b) * 0.5 * std::exp (-t * 14.0);
    }, 0.3));
    kit.pads[shaker] = s.make ("Dusty Shaker", 0.14, 0.5, lofi (s.hpNoise (28.0, 0.012), 0.5, 0.03));
    kit.pads[clave]  = s.make ("Lo-Fi Clave", 0.1, 0.7, lofi (s.sine (2400.0, 42.0), 0.4));
    kit.pads[conga]  = s.make ("Lo-Fi Conga", 0.35, 0.8, lofi (s.glideSine (240.0, 215.0, 0.02, 13.0)));
    kit.pads[sub]    = s.make ("Lo-Fi Sub", 1.0, 0.9, lofi (s.glideSine (55.0, 45.0, 0.1, 3.4), 0.15, 0.0));
}

void buildPercussion (Synth& s, DrumKit& kit)
{
    BM_PADS;
    kit.pads[kick] = s.make ("Surdo", 0.8, 0.95, [b = s.glideSine (95.0, 70.0, 0.05, 5.0), nz = s.hpNoise (120.0)] (double t, int i) { return b (t, i) + 0.25 * nz (t, i); });
    kit.pads[snare] = s.make ("Timbale", 0.4, 0.85, [tone = s.glideSine (330.0, 300.0, 0.02, 11.0), nz = s.hpNoise (25.0)] (double t, int i) { return 0.7 * tone (t, i) + 0.5 * nz (t, i); });
    kit.pads[clap] = s.make ("Bongo Hi", 0.25, 0.8, s.glideSine (520.0, 460.0, 0.015, 22.0));
    kit.pads[rim] = s.make ("Woodblock", 0.12, 0.7, [a = s.sine (880.0, 55.0), b = s.sine (1320.0, 70.0)] (double t, int i) { return a (t, i) + 0.4 * b (t, i); });
    kit.pads[closedHat] = s.make ("Cabasa", 0.12, 0.55, s.hpNoise (55.0, 0.008));
    kit.pads[openHat]   = s.make ("Shaker Long", 0.35, 0.55, s.hpNoise (14.0, 0.02));
    kit.pads[lowTom]  = s.make ("Conga Low",  0.5, 0.85, s.glideSine (200.0, 180.0, 0.02, 9.0));
    kit.pads[midTom]  = s.make ("Conga Mid",  0.45, 0.85, s.glideSine (250.0, 225.0, 0.02, 10.0));
    kit.pads[highTom] = s.make ("Conga Slap", 0.3, 0.85, [b = s.glideSine (330.0, 290.0, 0.015, 16.0), nz = s.hpNoise (90.0)] (double t, int i) { return b (t, i) + 0.5 * nz (t, i); });
    kit.pads[crash] = s.make ("Bell Tree", 1.6, 0.6, [m = s.metal (2.5, 2.4), nz = s.hpNoise (3.0, 0.15)] (double t, int i) { return 0.7 * m (t, i) + 0.3 * nz (t, i); });
    kit.pads[ride]  = s.make ("Triangle", 1.4, 0.55, [a = s.sine (2900.0, 3.0), b = s.sine (4300.0, 4.0), c = s.sine (6100.0, 5.0)] (double t, int i) { return (a (t, i) + 0.6 * b (t, i) + 0.4 * c (t, i)) / 2.0; });
    kit.pads[cowbell] = s.make ("Agogo", 0.4, 0.7, [a = s.sine (720.0, 14.0), b = s.sine (1080.0, 16.0)] (double t, int i) { return a (t, i) + 0.5 * b (t, i); });
    kit.pads[shaker] = s.make ("Guiro", 0.3, 0.55, [nz = s.hpNoise (10.0, 0.01)] (double t, int i) { return nz (t, i) * (0.55 + 0.45 * std::sin (twoPi * 38.0 * t)); });
    kit.pads[clave]  = s.make ("Tabla", 0.35, 0.75, s.glideSine (95.0, 150.0, 0.12, 9.0));      // the bayan's rising slide
    kit.pads[conga]  = s.make ("Djembe", 0.4, 0.85, [b = s.glideSine (290.0, 210.0, 0.02, 11.0), nz = s.hpNoise (60.0)] (double t, int i) { return b (t, i) + 0.35 * nz (t, i); });
    kit.pads[sub]    = s.make ("Low Drum", 1.0, 0.9, s.glideSine (62.0, 50.0, 0.1, 3.0));
}

#undef BM_PADS
}   // namespace (kit builders)

} // namespace beatmaker::engine
