// EffectEditor: the knobs of a built-in insert effect, shown in a callout from
// a mixer strip's slot or from a track header's Effects menu. Dynamics
// effects get a sidechain key menu, the convolution its impulse menu, the EQ
// its response curve, the pitch corrector a readout.
#pragma once

#include "../shared/Theme.h"
#include <MixerCommands.h>
#include <Session.h>
#include <dsp/Effects.h>
#include <dsp/PitchCorrection.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

//==============================================================================
// Effect editor shown in a callout when an insert is clicked

class EffectEditor final : public juce::Component,
                           private juce::Timer
{
public:
    EffectEditor (const model::Insert& insert, double sr, const model::Session& session,
                  std::function<void (std::shared_ptr<const engine::InsertParams>, bool replace)> onChange,
                  std::function<void (int keyBus, bool listen)> onKey = {}, std::function<void()> onLoadIR = {})
        : type (insert.type), sampleRate (sr), params (insert.params != nullptr ? *insert.params : engine::Effect::defaultParams (insert.type)),
          instance (insert.instance), apply (std::move (onChange)), applyKey (std::move (onKey)), loadImpulse (std::move (onLoadIR))
    {
        // Dynamics effects: key input selector + key listen, in the header row.
        if (insert.instance != nullptr && insert.instance->acceptsSidechain())
        {
            keyBox = std::make_unique<juce::ComboBox>();
            keyBox->addItem ("Key: Internal", 1);
            for (int b = 0; b < model::Track::numBuses; ++b) keyBox->addItem ("Key: " + session.busName (b), 100 + b);
            keyBox->setSelectedId (insert.keyBus >= 0 ? 100 + insert.keyBus : 1, juce::dontSendNotification);
            keyBox->setTooltip ("Sidechain key input: the detector listens to this bus instead of the audio (strips sending to the bus are processed first)");
            keyBox->onChange = [this]
            {
                const int id = keyBox->getSelectedId();
                if (id > 0 && applyKey) applyKey (id >= 100 ? id - 100 : -1, listenButton->getToggleState());
            };
            addAndMakeVisible (*keyBox);
            listenButton = std::make_unique<juce::TextButton> ("Listen");
            listenButton->setClickingTogglesState (true);
            listenButton->setToggleState (insert.keyListen, juce::dontSendNotification);
            listenButton->setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.45f));
            listenButton->setTooltip ("Key Listen: hear the key signal instead of the effect output");
            listenButton->onClick = [this]
            {
                const int id = keyBox->getSelectedId();
                if (applyKey) applyKey (id >= 100 ? id - 100 : -1, listenButton->getToggleState());
            };
            addAndMakeVisible (*listenButton);
        }

        // Convolution: the impulse is a menu (bundled spaces + the loaded file) and a Load button.
        if (type == engine::EffectType::convolution)
        {
            impulseBox = std::make_unique<juce::ComboBox>();
            for (int i = 0; i < engine::ConvolutionEffect::numImpulses; ++i)
            {
                juce::String name = engine::ConvolutionEffect::impulseName (i);
                if (i == engine::ConvolutionEffect::custom)
                    if (auto* conv = dynamic_cast<engine::ConvolutionEffect*> (insert.instance.get()); conv != nullptr && conv->getCustomImpulseName().isNotEmpty())
                        name = "File: " + conv->getCustomImpulseName();
                impulseBox->addItem (name, i + 1);
            }
            impulseBox->setSelectedId ((int) std::lround (params.values[engine::ConvolutionEffect::impulse]) + 1, juce::dontSendNotification);
            impulseBox->onChange = [this]
            {
                if (impulseBox->getSelectedId() <= 0) return;
                params.values[engine::ConvolutionEffect::impulse] = (float) (impulseBox->getSelectedId() - 1);
                apply (std::make_shared<const engine::InsertParams> (params), false);
            };
            addAndMakeVisible (*impulseBox);
            loadButton = std::make_unique<juce::TextButton> ("Load IR...");
            loadButton->setTooltip ("Load an impulse response from an audio file");
            loadButton->onClick = [this] { if (loadImpulse) loadImpulse(); };
            addAndMakeVisible (*loadButton);
        }

        const auto& info = engine::Effect::paramInfo (type);
        for (size_t i = 0; i < info.size(); ++i)
        {
            auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            s->setTitle (juce::String (info[i].name));
            s->setRange (info[i].min, info[i].max, 0.0);
            if (info[i].skewMidpoint > info[i].min && info[i].skewMidpoint < info[i].max) s->setSkewFactorFromMidPoint (info[i].skewMidpoint);
            s->setNumDecimalPlacesToDisplay (info[i].max - info[i].min > 100.0f ? 0 : 1);
            s->setTextValueSuffix (info[i].suffix);
            // Enumerated parameters (Type, Mode, Voices, Stages, on/off) step in whole numbers
            const bool stepped = info[i].max - info[i].min <= 8.0f && info[i].suffix[0] == '\0';
            if (stepped) s->setRange (info[i].min, info[i].max, 1.0);
            s->setValue (params.values[i], juce::dontSendNotification);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
            s->setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
            s->setColour (juce::Slider::rotarySliderOutlineColourId, theme::gridStrong);
            s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s->setDoubleClickReturnValue (true, info[i].def);
            s->onDragStart = [this] { gesture = true; changed = false; };
            s->onDragEnd = [this] { gesture = false; changed = false; };
            s->onValueChange = [this, i, s]
            {
                params.values[i] = (float) s->getValue();
                const bool replace = gesture && changed;
                if (gesture) changed = true;
                apply (std::make_shared<const engine::InsertParams> (params), replace);
                repaint();
            };
            addAndMakeVisible (s);

            auto* l = labels.add (new juce::Label ({}, info[i].name));
            l->setJustificationType (juce::Justification::centred);
            l->setColour (juce::Label::textColourId, theme::textDim);
            l->setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (l);

            // The convolution's Impulse knob is replaced by the menu above.
            if (type == engine::EffectType::convolution && i == engine::ConvolutionEffect::impulse) { s->setVisible (false); l->setVisible (false); }

            // Enumerated parameters with names get a menu instead of a knob.
            if (const auto* names = engine::Effect::choices (type, (int) i))
            {
                s->setVisible (false); l->setVisible (false);
                auto* box = choiceBoxes.add (new juce::ComboBox());
                for (int c = 0; c < (int) names->size(); ++c) box->addItem ((*names)[(size_t) c], c + 1);
                box->setSelectedId ((int) std::lround (params.values[i]) - (int) info[i].min + 1, juce::dontSendNotification);
                box->setTooltip (info[i].name);
                box->onChange = [this, i, box, minValue = info[i].min]
                {
                    if (box->getSelectedId() <= 0) return;
                    params.values[i] = minValue + (float) (box->getSelectedId() - 1);
                    apply (std::make_shared<const engine::InsertParams> (params), false);
                };
                addAndMakeVisible (box);
            }
        }
        if (type == engine::EffectType::pitchCorrection)
        {
            addAndMakeVisible (readout);
            readout.setColour (juce::Label::textColourId, theme::accent);
            readout.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            readout.setJustificationType (juce::Justification::centredRight);
            startTimerHz (15);
        }
        const bool hasHeader = keyBox != nullptr || impulseBox != nullptr || ! choiceBoxes.isEmpty();
        const int visibleKnobs = sliders.size() - (type == engine::EffectType::convolution ? 1 : 0) - choiceBoxes.size();
        const int cols = juce::jmin (knobsPerRow, juce::jmax (hasHeader ? 4 : 1, visibleKnobs));
        const int rows = (visibleKnobs + knobsPerRow - 1) / knobsPerRow;
        setSize (juce::jmax (cols * 76 + 16, hasHeader ? 16 + 150 + 60 + choiceBoxes.size() * 130 : 0),
                 18 + rows * 92 + (type == engine::EffectType::eq ? responseHeight + 6 : 0) + (hasHeader ? 26 : 0) + 8);
    }

    void timerCallback() override
    {
        auto* pc = dynamic_cast<engine::PitchCorrectionEffect*> (instance.get());
        if (pc == nullptr) return;
        const float detected = pc->getDetectedMidi(), target = pc->getTargetMidi();
        if (detected < 0.0f) { readout.setText ("no pitch", juce::dontSendNotification); return; }
        const int nearest = juce::roundToInt (detected);
        const int cents = juce::roundToInt ((detected - (float) nearest) * 100.0f);
        readout.setText (juce::MidiMessage::getMidiNoteName (nearest, true, true, 3) + " " + (cents >= 0 ? "+" : "") + juce::String (cents) + " ct  ->  "
                             + juce::MidiMessage::getMidiNoteName (juce::roundToInt (target), true, true, 3),
                         juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (engine::Effect::typeName (type), 8, 4, getWidth() - 16, 14, juce::Justification::centredLeft);

        if (type == engine::EffectType::eq)
        {
            // Frequency response of the current settings, 20 Hz .. 20 kHz, +-18 dB
            auto r = juce::Rectangle<int> (8, 18, getWidth() - 16, responseHeight).toFloat();
            g.setColour (theme::background);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (theme::grid);
            for (double f : { 100.0, 1000.0, 10000.0 })
            {
                const float x = r.getX() + (float) (std::log10 (f / 20.0) / 3.0) * r.getWidth();
                g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            }
            g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
            for (int db : { -12, -6, 6, 12 })
                g.drawHorizontalLine ((int) (r.getCentreY() - db / 18.0f * r.getHeight() * 0.5f), r.getX(), r.getRight());

            const auto bands = engine::EqEffect::coefficientsFor (params, sampleRate);
            const auto active = engine::EqEffect::activeBands (params);
            juce::Path curve;
            const int steps = (int) r.getWidth();
            for (int i = 0; i <= steps; ++i)
            {
                const double f = 20.0 * std::pow (10.0, 3.0 * i / (double) steps);
                double db = 0.0;
                for (int b = 0; b < engine::EqEffect::numBands; ++b)
                    if (active[(size_t) b]) db += engine::Biquad::magnitudeDb (bands[(size_t) b], sampleRate, f);
                const float x = r.getX() + (float) i, y = r.getCentreY() - (float) juce::jlimit (-18.0, 18.0, db) / 18.0f * r.getHeight() * 0.5f;
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            g.setColour (theme::accent);
            g.strokePath (curve, juce::PathStrokeType (1.6f));
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("100", (int) (r.getX() + (float) (std::log10 (5.0) / 3.0) * r.getWidth()) + 2, (int) r.getBottom() - 11, 30, 10, juce::Justification::left);
            g.drawText ("1k",  (int) (r.getX() + (float) (std::log10 (50.0) / 3.0) * r.getWidth()) + 2, (int) r.getBottom() - 11, 30, 10, juce::Justification::left);
            g.drawText ("10k", (int) (r.getX() + (float) (std::log10 (500.0) / 3.0) * r.getWidth()) + 2, (int) r.getBottom() - 11, 30, 10, juce::Justification::left);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8).withTrimmedTop (18);
        if (type == engine::EffectType::eq) area.removeFromTop (responseHeight + 6);
        if (keyBox != nullptr || impulseBox != nullptr || ! choiceBoxes.isEmpty())
        {
            auto header = area.removeFromTop (22);
            if (keyBox != nullptr)     { keyBox->setBounds (header.removeFromLeft (150)); header.removeFromLeft (4); listenButton->setBounds (header.removeFromLeft (56)); }
            if (impulseBox != nullptr) { impulseBox->setBounds (header.removeFromLeft (170)); header.removeFromLeft (4); loadButton->setBounds (header.removeFromLeft (76)); }
            for (auto* box : choiceBoxes) { box->setBounds (header.removeFromLeft (126)); header.removeFromLeft (4); }
            if (type == engine::EffectType::pitchCorrection) readout.setBounds (header);
            area.removeFromTop (4);
        }
        juce::Rectangle<int> row;
        int placed = 0;
        for (int i = 0; i < sliders.size(); ++i)
        {
            if (! sliders[i]->isVisible()) continue;
            if (placed % knobsPerRow == 0) row = area.removeFromTop (92);
            auto col = row.removeFromLeft (76);
            labels[i]->setBounds (col.removeFromTop (14));
            sliders[i]->setBounds (col);
            ++placed;
        }
    }

private:
    static constexpr int knobsPerRow = 8, responseHeight = 90;
    engine::EffectType type;
    double sampleRate;
    engine::InsertParams params;
    std::shared_ptr<engine::Effect> instance;
    std::function<void (std::shared_ptr<const engine::InsertParams>, bool)> apply;
    std::function<void (int, bool)> applyKey;
    std::function<void()> loadImpulse;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    std::unique_ptr<juce::ComboBox> keyBox, impulseBox;
    juce::OwnedArray<juce::ComboBox> choiceBoxes;
    juce::Label readout;
    std::unique_ptr<juce::TextButton> listenButton, loadButton;
    bool gesture = false, changed = false;
};

} // namespace beatmaker::ui
