// Automation: which parameter, and a lane of breakpoints over time.
// Lanes are immutable once shared with the engine (copy-on-write).
#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

namespace beatmaker::engine
{

struct ParamId
{
    enum class Type { volume, pan, mute, sendLevel, insertParam };

    Type type = Type::volume;
    int index = 0;   // send slot or insert slot
    int sub = 0;     // insert parameter index

    bool operator== (const ParamId& o) const noexcept { return type == o.type && index == o.index && sub == o.sub; }
    bool operator!= (const ParamId& o) const noexcept { return ! (*this == o); }
    bool operator<  (const ParamId& o) const noexcept
    {
        if (type != o.type) return type < o.type;
        if (index != o.index) return index < o.index;
        return sub < o.sub;
    }

    // Stepped parameters hold their value until the next point (no ramps).
    bool isStepped() const noexcept { return type == Type::mute; }

    static ParamId volume()          { return { Type::volume, 0, 0 }; }
    static ParamId pan()             { return { Type::pan, 0, 0 }; }
    static ParamId mute()            { return { Type::mute, 0, 0 }; }
    static ParamId send (int slot)   { return { Type::sendLevel, slot, 0 }; }
    static ParamId insert (int slot, int param) { return { Type::insertParam, slot, param }; }

    juce::String getName() const
    {
        switch (type)
        {
            case Type::volume:      return "Volume";
            case Type::pan:         return "Pan";
            case Type::mute:        return "Mute";
            case Type::sendLevel:   return "Send " + juce::String::charToString ((juce::juce_wchar) ('A' + index));
            case Type::insertParam: return "Insert " + juce::String (index + 1) + " / " + juce::String (sub + 1);
        }
        return {};
    }
};

struct AutomationPoint
{
    juce::int64 time = 0;   // samples
    float value = 0.0f;
};

struct AutomationLane
{
    ParamId param;
    std::vector<AutomationPoint> points;   // sorted by time

    bool isEmpty() const noexcept { return points.empty(); }

    void sortPoints()
    {
        std::stable_sort (points.begin(), points.end(), [] (const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
    }

    // Value at `time`; RT-safe (binary search, no allocation). `fallback` is
    // returned for an empty lane.
    float valueAt (juce::int64 time, float fallback) const noexcept
    {
        if (points.empty()) return fallback;
        if (time <= points.front().time) return points.front().value;
        if (time >= points.back().time)  return points.back().value;

        const auto next = std::upper_bound (points.begin(), points.end(), time,
                                            [] (juce::int64 t, const AutomationPoint& p) { return t < p.time; });
        const auto prev = next - 1;
        if (param.isStepped() || next->time == prev->time) return prev->value;

        const double frac = (double) (time - prev->time) / (double) (next->time - prev->time);
        return prev->value + (float) frac * (next->value - prev->value);
    }
};

} // namespace beatmaker::engine
