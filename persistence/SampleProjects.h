// Sample projects: finished-sounding sessions built from the bundled loops
// and instruments, so a new user can open something and start editing.
#pragma once

#include <Session.h>
#include "SessionFile.h"
#include <functional>

namespace beatmaker::persistence
{

class SampleProjects
{
public:
    struct Info { juce::String name, description; };
    static std::vector<Info> list();

    // Builds `name` into `session` (which should be empty) and fills the transport state.
    // `loopsFolder` holds the bundled loops; `loadAudio` loads a file at the engine rate.
    static bool create (const juce::String& name, model::Session& session, TransportState& transport, double sampleRate,
                        const juce::File& loopsFolder, const std::function<std::shared_ptr<const juce::AudioBuffer<float>> (const juce::File&)>& loadAudio,
                        juce::String& error);
};

} // namespace beatmaker::persistence
