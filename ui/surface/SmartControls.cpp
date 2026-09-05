#include "SmartControls.h"
#include <dsp/DrumKitFactory.h>

namespace beatmaker::ui
{

SmartControls::SmartControls (model::Session& s) : session (s)
{
    session.addListener (this);
    waveLabel.setColour (juce::Label::textColourId, theme::textDim);
    waveLabel.setJustificationType (juce::Justification::centred);
    waveLabel.setFont (juce::FontOptions (11.0f));
}

SmartControls::~SmartControls() { session.removeListener (this); }

void SmartControls::setTrack (int index)
{
    trackIndex = index;
    rebuild();
}

void SmartControls::sessionChanged (model::Session&)
{
    auto* track = session.getTrack (trackIndex);
    if (track == nullptr) { trackIndex = -1; rebuild(); return; }

    // Rebuild if the kind of track changed under us, else just refresh values.
    const bool kindMatches = (waveBox != nullptr) == track->isSynth();
    if (! kindMatches || knobs.empty()) rebuild();
    else syncValues();
}

void SmartControls::issue (std::unique_ptr<model::Command> cmd)
{
    if (syncing || ! onCommand) return;
    const bool replace = gestureActive && gestureChanged;
    if (gestureActive) gestureChanged = true;
    onCommand (std::move (cmd), replace);
}

//==============================================================================
// Knob construction

void SmartControls::addKnob (const juce::String& name, double min, double max, double value, double skewMidpoint,
                             const juce::String& suffix, std::function<juce::String (double)> textFromValue,
                             std::function<void (double)> apply)
{
    Knob k;
    k.slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    auto& s = *k.slider;
    s.setRange (min, max, 0.0);
    if (skewMidpoint > min && skewMidpoint < max) s.setSkewFactorFromMidPoint (skewMidpoint);
    s.setNumDecimalPlacesToDisplay (2);
    if (textFromValue) s.textFromValueFunction = [textFromValue] (double v) { return textFromValue (v); };
    s.setTextValueSuffix (suffix);
    s.setValue (value, juce::dontSendNotification);
    s.updateText();
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, theme::gridStrong);
    s.setColour (juce::Slider::textBoxTextColourId, theme::text);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setDoubleClickReturnValue (true, value);

    s.onDragStart = [this] { gestureActive = true; gestureChanged = false; };
    s.onDragEnd   = [this] { gestureActive = false; gestureChanged = false; };
    k.apply = std::move (apply);
    s.onValueChange = [this, slider = &s, fn = k.apply] { fn (slider->getValue()); };

    k.label = std::make_unique<juce::Label> (juce::String(), name);
    k.label->setJustificationType (juce::Justification::centred);
    k.label->setColour (juce::Label::textColourId, theme::textDim);
    k.label->setFont (juce::FontOptions (11.0f));

    addAndMakeVisible (*k.slider);
    addAndMakeVisible (*k.label);
    knobs.push_back (std::move (k));
}

static juce::String dbText (double gain)
{
    if (gain <= 0.0001) return "-inf dB";
    return juce::String (juce::Decibels::gainToDecibels ((float) gain), 1) + " dB";
}

static juce::String panText (double pan)
{
    const int v = juce::roundToInt (pan * 100.0);
    return v == 0 ? "C" : v < 0 ? "L" + juce::String (-v) : "R" + juce::String (v);
}

void SmartControls::bindMix (const model::Track& track)
{
    addKnob ("Volume", 0.0, 2.0, track.gain, 1.0, {}, dbText, [this] (double v)
    {
        if (auto* t = session.getTrack (trackIndex))
        {
            issue (std::make_unique<model::SetTrackMixCommand> (trackIndex, (float) v, t->pan));
            if (onParameterChanged) onParameterChanged (trackIndex, engine::ParamId::volume(), (float) v, gestureActive);
        }
    });
    knobs.back().slider->onDragEnd = [this] { gestureActive = false; gestureChanged = false; if (onGestureEnded) onGestureEnded (trackIndex, engine::ParamId::volume()); };
    addKnob ("Pan", -1.0, 1.0, track.pan, 0.0, {}, panText, [this] (double v)
    {
        if (auto* t = session.getTrack (trackIndex))
        {
            issue (std::make_unique<model::SetTrackMixCommand> (trackIndex, t->gain, (float) v));
            if (onParameterChanged) onParameterChanged (trackIndex, engine::ParamId::pan(), (float) v, gestureActive);
        }
    });
    knobs.back().slider->onDragEnd = [this] { gestureActive = false; gestureChanged = false; if (onGestureEnded) onGestureEnded (trackIndex, engine::ParamId::pan()); };
}

void SmartControls::bindSynth (const model::Track& track)
{
    const auto& p = *track.synthParams;

    // Each knob copies the current params, edits one field, issues a command.
    auto edit = [this] (std::function<void (engine::SynthParams&, double)> mutate)
    {
        return [this, mutate] (double v)
        {
            auto* t = session.getTrack (trackIndex);
            if (t == nullptr || t->synthParams == nullptr) return;
            auto updated = std::make_shared<engine::SynthParams> (*t->synthParams);
            mutate (*updated, v);
            issue (std::make_unique<model::SetSynthParamsCommand> (trackIndex, std::move (updated)));
        };
    };

    auto seconds = [] (double v) { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s"; };
    auto hz = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    auto pct = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };

    addKnob ("Cutoff", 20.0, 20000.0, p.cutoffHz, 1000.0, {}, hz, edit ([] (auto& s, double v) { s.cutoffHz = (float) v; }));
    addKnob ("Reso", 0.0, 1.0, p.resonance, 0.5, {}, pct, edit ([] (auto& s, double v) { s.resonance = (float) v; }));
    addKnob ("Filt Env", 0.0, 6.0, p.filterEnvOctaves, 3.0, " oct", nullptr, edit ([] (auto& s, double v) { s.filterEnvOctaves = (float) v; }));
    addKnob ("Attack", 0.001, 4.0, p.attackSeconds, 0.1, {}, seconds, edit ([] (auto& s, double v) { s.attackSeconds = (float) v; }));
    addKnob ("Decay", 0.01, 4.0, p.decaySeconds, 0.3, {}, seconds, edit ([] (auto& s, double v) { s.decaySeconds = (float) v; }));
    addKnob ("Sustain", 0.0, 1.0, p.sustainLevel, 0.5, {}, pct, edit ([] (auto& s, double v) { s.sustainLevel = (float) v; }));
    addKnob ("Release", 0.01, 6.0, p.releaseSeconds, 0.5, {}, seconds, edit ([] (auto& s, double v) { s.releaseSeconds = (float) v; }));
    addKnob ("Detune", 0.0, 50.0, p.detuneCents, 10.0, " ct", nullptr, edit ([] (auto& s, double v) { s.detuneCents = (float) v; }));
    addKnob ("Level", 0.0, 1.0, p.gain, 0.4, {}, dbText, edit ([] (auto& s, double v) { s.gain = (float) v; }));

    waveBox = std::make_unique<juce::ComboBox>();
    waveBox->addItem ("Saw", 1); waveBox->addItem ("Square", 2); waveBox->addItem ("Triangle", 3); waveBox->addItem ("Sine", 4);
    waveBox->setSelectedId ((int) p.wave + 1, juce::dontSendNotification);
    waveBox->onChange = [this]
    {
        auto* t = session.getTrack (trackIndex);
        if (syncing || t == nullptr || t->synthParams == nullptr || waveBox->getSelectedId() == 0) return;
        auto updated = std::make_shared<engine::SynthParams> (*t->synthParams);
        updated->wave = (engine::SynthParams::Wave) (waveBox->getSelectedId() - 1);
        if (onCommand) onCommand (std::make_unique<model::SetSynthParamsCommand> (trackIndex, std::move (updated)), false);
    };
    addAndMakeVisible (*waveBox);
    addAndMakeVisible (waveLabel);
}

void SmartControls::bindDrums (const model::Track& track)
{
    using F = engine::DrumKitFactory;
    struct Group { const char* name; std::vector<int> pads; };
    static const Group groups[] = {
        { "Kick", { F::kick } }, { "Snare", { F::snare } }, { "Clap/Rim", { F::clap, F::rim } },
        { "Hats", { F::closedHat, F::openHat } }, { "Toms", { F::lowTom, F::midTom, F::highTom } },
        { "Cymbals", { F::crash, F::ride } }, { "Perc", { F::cowbell, F::shaker, F::clave, F::conga } }, { "Sub", { F::sub } } };

    for (const auto& g : groups)
    {
        const float current = track.drumKit->pads[(size_t) g.pads[0]].gain;
        addKnob (g.name, 0.0, 2.0, current, 1.0, {}, dbText, [this, pads = g.pads] (double v)
        {
            auto* t = session.getTrack (trackIndex);
            if (t == nullptr || t->drumKit == nullptr) return;
            auto updated = std::make_shared<engine::DrumKit> (*t->drumKit);
            for (int pad : pads) updated->pads[(size_t) pad].gain = (float) v;
            issue (std::make_unique<model::ReplaceDrumKitCommand> (trackIndex, std::move (updated)));
        });
    }
}

void SmartControls::rebuild()
{
    knobs.clear();
    waveBox.reset();
    removeChildComponent (&waveLabel);

    if (auto* track = session.getTrack (trackIndex))
    {
        bindMix (*track);
        if (track->isSynth() && track->synthParams != nullptr)          bindSynth (*track);
        else if (track->isDrumMachine() && track->drumKit != nullptr)   bindDrums (*track);
    }
    resized();
    repaint();
}

void SmartControls::syncValues()
{
    auto* track = session.getTrack (trackIndex);
    if (track == nullptr) return;

    syncing = true;
    size_t i = 0;
    auto set = [&] (double v) { if (i < knobs.size()) knobs[i++].slider->setValue (v, juce::dontSendNotification); };
    set (track->gain);
    set (track->pan);

    if (track->isSynth() && track->synthParams != nullptr)
    {
        const auto& p = *track->synthParams;
        set (p.cutoffHz); set (p.resonance); set (p.filterEnvOctaves); set (p.attackSeconds); set (p.decaySeconds);
        set (p.sustainLevel); set (p.releaseSeconds); set (p.detuneCents); set (p.gain);
        if (waveBox != nullptr) waveBox->setSelectedId ((int) p.wave + 1, juce::dontSendNotification);
    }
    else if (track->isDrumMachine() && track->drumKit != nullptr)
    {
        using F = engine::DrumKitFactory;
        for (int pad : { F::kick, F::snare, F::clap, F::closedHat, F::lowTom, F::crash, F::cowbell, F::sub })
            set (track->drumKit->pads[(size_t) pad].gain);
    }
    syncing = false;
}

//==============================================================================

void SmartControls::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto* track = session.getTrack (trackIndex);
    if (track == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Smart Controls: select a track", getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto title = getLocalBounds().removeFromLeft (titleWidth).reduced (12, 10);
    g.setColour (track->colour);
    g.fillRoundedRectangle (title.removeFromLeft (6).toFloat(), 3.0f);
    title.removeFromLeft (8);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (track->name, title.removeFromTop (22), juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (track->isSynth() ? "Synth  -  " + (track->synthParams != nullptr ? track->synthParams->name : juce::String())
              : track->isDrumMachine() ? "Drum Machine" : track->isVca() ? "VCA Master" : track->isAux() ? "Aux Input" : "Audio",
                title.removeFromTop (16), juce::Justification::centredLeft, true);
    if (waveBox == nullptr)
        g.drawText ("Smart Controls", title.removeFromTop (16), juce::Justification::centredLeft, true);
}

void SmartControls::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (titleWidth).reduced (4, 6);
    const int knobWidth = knobs.empty() ? 68 : juce::jlimit (56, 72, area.getWidth() / (int) knobs.size());

    for (auto& k : knobs)
    {
        auto col = area.removeFromLeft (knobWidth);
        k.label->setBounds (col.removeFromTop (16));
        k.slider->setBounds (col);
    }

    if (waveBox != nullptr)
    {
        // Lives under the track title so it never competes with the knobs.
        auto col = getLocalBounds().removeFromLeft (titleWidth).reduced (12, 10);
        col.removeFromTop (22 + 16 + 6);
        col.removeFromLeft (14);
        auto row = col.removeFromTop (22);
        waveLabel.setBounds (row.removeFromLeft (34));
        waveLabel.setJustificationType (juce::Justification::centredLeft);
        waveBox->setBounds (row);
    }
}

} // namespace beatmaker::ui
