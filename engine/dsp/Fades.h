// Fade shapes shared by the engine (rendering) and the UI (drawing).
#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace beatmaker::engine
{

enum class FadeShape { linear, equalPower, sCurve };

// Gain of a fade-in at normalised position t in [0,1]. Fade-outs use 1 - t.
inline float fadeGain (FadeShape shape, double t) noexcept
{
    t = juce::jlimit (0.0, 1.0, t);
    switch (shape)
    {
        case FadeShape::linear:     return (float) t;
        case FadeShape::equalPower: return (float) std::sin (t * juce::MathConstants<double>::halfPi);
        case FadeShape::sCurve:     return (float) (0.5 - 0.5 * std::cos (t * juce::MathConstants<double>::pi));
    }
    return (float) t;
}

inline const char* fadeShapeName (FadeShape s) noexcept
{
    switch (s) { case FadeShape::linear: return "Standard"; case FadeShape::equalPower: return "Equal Power"; case FadeShape::sCurve: return "S-Curve"; }
    return "";
}

// Combined envelope for a clip of `length` samples at clip-relative `pos`.
inline float clipEnvelopeAt (juce::int64 pos, juce::int64 length, juce::int64 fadeIn, FadeShape inShape,
                             juce::int64 fadeOut, FadeShape outShape) noexcept
{
    float g = 1.0f;
    if (fadeIn > 0 && pos < fadeIn)
        g *= fadeGain (inShape, (double) pos / (double) fadeIn);
    if (fadeOut > 0 && pos >= length - fadeOut)
        g *= fadeGain (outShape, (double) (length - pos) / (double) fadeOut);
    return g;
}

} // namespace beatmaker::engine
