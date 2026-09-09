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

    using Build = void (*) (Synth&, DrumKit&);
    void buildStudio (Synth&, DrumKit&);
    void build808 (Synth&, DrumKit&);
    void build909 (Synth&, DrumKit&);
    void buildLoFi (Synth&, DrumKit&);
    void buildPercussion (Synth&, DrumKit&);

    struct Entry { DrumKitFactory::KitInfo info; Build build; };
    const std::vector<Entry>& entries()
    {
        static const std::vector<Entry> all {
            { { "Studio Kit", "Acoustic", "A tight, dry kit: a punchy kick, a snappy snare with a noise crack, clap, rimshot, three toms, hats, crash and ride, plus cowbell, shaker, clave, conga and a sub. The kit every new drum track starts with." }, buildStudio },
            { { "808", "Electronic", "The classic analogue drum machine: a long booming kick, a snappy snare, metallic hats and cowbell made from square waves, and a deep sub. Hip-hop, trap, electro, pop." }, build808 },
            { { "909", "Electronic", "The house and techno machine: a hard clicking kick, a crunchy snare, bright metallic hats, big noise crash and ride. Four-on-the-floor of every kind." }, build909 },
            { { "Lo-Fi", "Hip-Hop", "The studio kit put through an old sampler: bit-crushed, low-passed and dusty, with a soft thud of a kick and a papery snare. Boom bap, lo-fi beats, chillhop." }, buildLoFi },
            { { "Percussion", "World", "Hand and stick percussion instead of a drum kit: surdo, timbale, bongo, woodblock, cabasa, shaker, three congas, bell tree, triangle, agogo, guiro, tabla, djembe and a low drum. Latin, Afro, world rhythms." }, buildPercussion } };
        return all;
    }
}

const std::vector<DrumKitFactory::KitInfo>& DrumKitFactory::availableKits()
{
    static const std::vector<KitInfo> infos = [] { std::vector<KitInfo> v; for (const auto& e : entries()) v.push_back (e.info); return v; }();
    return infos;
}

std::vector<juce::String> DrumKitFactory::categories()
{
    std::vector<juce::String> out;
    for (const auto& k : availableKits()) if (std::find (out.begin(), out.end(), juce::String (k.category)) == out.end()) out.push_back (k.category);
    return out;
}

const DrumKitFactory::KitInfo* DrumKitFactory::info (const juce::String& name)
{
    for (const auto& k : availableKits()) if (name == k.name) return &k;
    return nullptr;
}

std::shared_ptr<const DrumKit> DrumKitFactory::createKit (const juce::String& name, double sampleRate)
{
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
