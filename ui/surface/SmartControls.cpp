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

    // Rebuild if the kind of track (or its instrument) changed under us, else just refresh values.
    const bool kindMatches = boundType == (track->hasInstrument() ? track->instrumentType() : engine::InstrumentType::none);
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
    s.setTitle (name);
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

void SmartControls::bindInstrument (const model::Track& track)
{
    const auto type = track.instrumentType();
    const auto& info = engine::Instrument::paramInfo (type);
    const auto& p = *track.instrumentParams;
    boundType = type;

    // Each knob copies the current params, edits one value, issues a command.
    auto edit = [this] (int paramIndex)
    {
        return [this, paramIndex] (double v)
        {
            auto* t = session.getTrack (trackIndex);
            if (t == nullptr || ! t->hasInstrument()) return;
            auto updated = std::make_shared<engine::InstrumentParams> (*t->instrumentParams);
            updated->values[(size_t) paramIndex] = (float) v;
            issue (std::make_unique<model::SetInstrumentParamsCommand> (trackIndex, std::move (updated)));
        };
    };

    auto seconds = [] (double v) { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s"; };
    auto hz = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    auto pct = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    auto onOff = [] (double v) { return juce::String (v >= 0.5 ? "On" : "Off"); };
    auto plain = [] (double v) { return juce::String (v, 2); };

    for (int i = 0; i < (int) info.size(); ++i)
    {
        const auto& pi = info[(size_t) i];
        const juce::String name (pi.name), suffix (pi.suffix);
        const double value = p.values[(size_t) i];

        if (name == "Wave")
        {
            waveParamIndex = i;
            waveBox = std::make_unique<juce::ComboBox>();
            const int numWaves = (int) std::lround (pi.max) + 1;
            const char* waveNames[] = { "Saw", "Square", "Triangle", "Sine" };
            for (int w = 0; w < juce::jmin (numWaves, 4); ++w) waveBox->addItem (waveNames[w], w + 1);
            waveBox->setSelectedId ((int) std::lround (value) + 1, juce::dontSendNotification);
            waveBox->onChange = [this]
            {
                auto* t = session.getTrack (trackIndex);
                if (syncing || t == nullptr || ! t->hasInstrument() || waveBox->getSelectedId() == 0) return;
                auto updated = std::make_shared<engine::InstrumentParams> (*t->instrumentParams);
                updated->values[(size_t) waveParamIndex] = (float) (waveBox->getSelectedId() - 1);
                if (onCommand) onCommand (std::make_unique<model::SetInstrumentParamsCommand> (trackIndex, std::move (updated)), false);
            };
            addAndMakeVisible (*waveBox);
            addAndMakeVisible (waveLabel);
            continue;
        }

        std::function<juce::String (double)> text;
        juce::String knobSuffix;
        const bool unit = pi.min <= 0.0f && std::abs (pi.max - 1.0f) < 1.0e-6f;   // 0..1 range
        const bool toggle = unit && (name == "Osc 2" || name == "Loop");
        if (toggle)                      text = onOff;
        else if (name == "Level")        text = dbText;
        else if (suffix == " s")         text = seconds;
        else if (suffix == " Hz")        text = hz;
        else if (suffix.isEmpty() && unit) text = pct;
        else if (suffix.isEmpty())       text = plain;
        else                             knobSuffix = suffix;

        knobParamIndices.push_back (i);
        addKnob (name, pi.min, pi.max, value, pi.skewMidpoint, knobSuffix, text, edit (i));
        if (toggle) knobs.back().slider->setRange (0.0, 1.0, 1.0);
    }
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
    knobParamIndices.clear();
    waveBox.reset();
    waveParamIndex = -1;
    boundType = engine::InstrumentType::none;
    removeChildComponent (&waveLabel);

    if (auto* track = session.getTrack (trackIndex))
    {
        bindMix (*track);
        if (track->hasInstrument())                                       bindInstrument (*track);
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

    if (track->hasInstrument())
    {
        const auto& p = *track->instrumentParams;
        for (int paramIndex : knobParamIndices) set (p.values[(size_t) paramIndex]);
        if (waveBox != nullptr && waveParamIndex >= 0)
            waveBox->setSelectedId ((int) std::lround (p.values[(size_t) waveParamIndex]) + 1, juce::dontSendNotification);
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
    g.drawText (track->hasInstrument() ? juce::String (engine::Instrument::typeName (track->instrumentType())) + "  -  " + track->instrumentParams->presetName
              : track->isDrumMachine() ? "Drum Machine" : track->isVca() ? "VCA Master" : track->isAux() ? "Aux Input" : "Audio",
                title.removeFromTop (16), juce::Justification::centredLeft, true);
    if (waveBox == nullptr)
        g.drawText (track->instrumentType() == engine::InstrumentType::sampler
                        ? (track->instrumentParams->sample != nullptr ? track->instrumentParams->sampleName : juce::String ("Drop an audio file here"))
                        : juce::String ("Smart Controls"),
                    title.removeFromTop (16), juce::Justification::centredLeft, true);
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
