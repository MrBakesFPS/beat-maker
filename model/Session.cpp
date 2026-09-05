#include "Session.h"

namespace beatmaker::model
{

IOSetup IOSetup::createDefault (int numInputChannels, int numOutputChannels)
{
    IOSetup io;
    for (int ch = 0; ch < numInputChannels; ++ch)
        io.inputs.push_back ({ "In " + juce::String (ch + 1), ch, 1 });
    for (int ch = 0; ch + 1 < numInputChannels; ch += 2)
        io.inputs.push_back ({ "In " + juce::String (ch + 1) + "-" + juce::String (ch + 2), ch, 2 });

    io.outputs.push_back ({ "Main", 0, 2 });
    for (int ch = 2; ch + 1 < numOutputChannels; ch += 2)
        io.outputs.push_back ({ "Out " + juce::String (ch + 1) + "-" + juce::String (ch + 2), ch, 2 });

    io.busNames.resize ((size_t) numBuses);
    return io;
}

} // namespace beatmaker::model

namespace beatmaker::model
{

int Session::indexOfTrackId (int id) const noexcept
{
    for (int i = 0; i < (int) tracks.size(); ++i)
        if (tracks[(size_t) i].id == id) return i;
    return -1;
}

double Session::getLengthSeconds() const
{
    double end = 0.0;
    for (const auto& t : tracks)
    {
        for (const auto& c : t.clips)        end = juce::jmax (end, c.getEndSeconds());
        for (const auto& c : t.patternClips) end = juce::jmax (end, c.getEndSeconds());
        for (const auto& c : t.midiClips)    end = juce::jmax (end, c.getEndSeconds());
    }
    return end;
}

juce::Colour Session::colourForTrackIndex (int index)
{
    static const juce::Colour palette[] = {
        juce::Colour (0xff3498db), juce::Colour (0xff2ecc71), juce::Colour (0xffe67e22),
        juce::Colour (0xff9b59b6), juce::Colour (0xffe74c3c), juce::Colour (0xff1abc9c),
        juce::Colour (0xfff1c40f), juce::Colour (0xffe84393),
    };
    return palette[(size_t) index % std::size (palette)];
}

} // namespace beatmaker::model
