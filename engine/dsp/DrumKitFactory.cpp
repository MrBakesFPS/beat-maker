#include "DrumKitFactory.h"
#include <cmath>
#include <functional>

namespace beatmaker::engine
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

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

            const float mag = b.getMagnitude (0, 0, length);
            if (mag > 0.0f)
                b.applyGain (peak / mag);

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
    };
}

std::shared_ptr<const DrumKit> DrumKitFactory::createDefaultKit (double sampleRate)
{
    Synth s { sampleRate };
    auto kit = std::make_shared<DrumKit>();
    kit->name = "Studio Kit";

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

    return kit;
}

} // namespace beatmaker::engine
