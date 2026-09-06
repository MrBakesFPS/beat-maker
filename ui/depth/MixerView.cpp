#include "MixerView.h"
#include "../shared/UiProfiler.h"
#include <dsp/PitchCorrection.h>

namespace beatmaker::ui
{

namespace
{
    juce::String dbText (float gain) { return gain <= 0.0001f ? "-inf" : juce::String (juce::Decibels::gainToDecibels (gain), 1); }
}

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

//==============================================================================

class MixerView::ChannelStrip final : public juce::Component
{
public:
    static constexpr int width = 104;

    ChannelStrip (MixerView& owner, int trackIndex) : mixer (owner), index (trackIndex)
    {
        for (int i = 0; i < visibleInserts; ++i)
        {
            auto* b = insertButtons.add (new juce::TextButton());
            b->setColour (juce::TextButton::buttonColourId, theme::background);
            b->setColour (juce::TextButton::textColourOffId, theme::text);
            b->onClick = [this, i] { insertClicked (i); };
            addAndMakeVisible (b);
        }
        if (! isMaster())
            for (int i = 0; i < model::Track::numSendSlots; ++i)
            {
                auto* b = sendButtons.add (new juce::TextButton());
                b->setColour (juce::TextButton::buttonColourId, theme::background);
                b->setColour (juce::TextButton::textColourOffId, theme::text);
                b->onClick = [this, i] { sendClicked (i); };
                addAndMakeVisible (b);

                auto* s = sendLevels.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
                s->setRange (0.0, 2.0, 0.0);
                s->setSkewFactorFromMidPoint (1.0);
                s->setColour (juce::Slider::trackColourId, theme::accent.darker (0.4f));
                s->setColour (juce::Slider::backgroundColourId, theme::background);
                s->onDragStart = [this] { gesture = true; changed = false; };
                s->onDragEnd = [this, i] { endGesture (engine::ParamId::send (i)); };
                s->onValueChange = [this, i, s]
                {
                    if (syncing) return;
                    if (auto* t = track(); t != nullptr && t->sends[(size_t) i].isActive())
                    {
                        auto send = t->sends[(size_t) i];
                        send.gain = (float) s->getValue();
                        issue (std::make_unique<model::SetSendCommand> (index, i, send));
                        report (engine::ParamId::send (i), send.gain);
                    }
                };
                addAndMakeVisible (s);
            }

        if (! isMaster())
        {
            addAndMakeVisible (pan);
            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan.setRange (-1.0, 1.0, 0.0);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
            pan.setColour (juce::Slider::rotarySliderOutlineColourId, theme::gridStrong);
            pan.onDragStart = [this] { gesture = true; changed = false; };
            pan.onDragEnd = [this] { endGesture (engine::ParamId::pan()); };
            pan.onValueChange = [this]
            {
                if (syncing) return;
                if (auto* t = track())
                {
                    issue (model::GroupLogic::mixCommand (mixer.session, index, t->gain, (float) pan.getValue()));
                    report (engine::ParamId::pan(), (float) pan.getValue());
                }
            };
        }

        addAndMakeVisible (autoMode);
        int am = 1;
        for (auto m : { model::AutomationMode::off, model::AutomationMode::read, model::AutomationMode::touch, model::AutomationMode::latch,
                        model::AutomationMode::write, model::AutomationMode::trim })
            autoMode.addItem (model::automationModeName (m), am++);
        autoMode.setTooltip ("Automation mode (Trim adjusts existing volume automation relatively)");
        autoMode.onChange = [this]
        {
            if (! syncing && autoMode.getSelectedId() > 0)
                issue (std::make_unique<model::SetAutomationModeCommand> (index, (model::AutomationMode) (autoMode.getSelectedId() - 1)));
        };

        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 2.0, 0.0);
        fader.setSkewFactorFromMidPoint (1.0);
        fader.setDoubleClickReturnValue (true, 1.0);
        fader.setColour (juce::Slider::trackColourId, theme::gridStrong);
        fader.setColour (juce::Slider::backgroundColourId, theme::background);
        fader.setColour (juce::Slider::thumbColourId, theme::text);
        fader.onDragStart = [this]
        {
            gesture = true; changed = false;
            trimGestureStart = (float) fader.getValue();
            trimming = inTrimMode();
        };
        fader.onDragEnd = [this]
        {
            endGesture (engine::ParamId::volume());
            if (trimming)
            {
                // Trim faders spring back: the level itself never changed.
                trimming = false; currentTrimDb = 0.0f;
                syncing = true; fader.setValue (trimGestureStart, juce::dontSendNotification); syncing = false;
                repaint();
            }
        };
        fader.onValueChange = [this]
        {
            if (syncing) return;
            auto* t = track();
            if (t == nullptr) return;

            if (inTrimMode())
            {
                if (! gesture) { trimGestureStart = t->gain; }   // keyboard / double-click: instant trim
                trimming = true;
                currentTrimDb = juce::Decibels::gainToDecibels ((float) fader.getValue(), -60.0f) - juce::Decibels::gainToDecibels (trimGestureStart, -60.0f);
                if (mixer.onTrimChanged) mixer.onTrimChanged (index, currentTrimDb, gesture);
                if (! gesture) { trimming = false; syncing = true; fader.setValue (t->gain, juce::dontSendNotification); syncing = false; }
                return;
            }

            if (isMaster()) issue (std::make_unique<model::SetTrackMixCommand> (index, (float) fader.getValue(), t->pan));
            else            issue (model::GroupLogic::mixCommand (mixer.session, index, (float) fader.getValue(), t->pan));
            report (engine::ParamId::volume(), (float) fader.getValue());
        };

        if (! isMaster())
        {
            addAndMakeVisible (mute); addAndMakeVisible (solo);
            mute.setClickingTogglesState (true); solo.setClickingTogglesState (true);
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe67e22));
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff1c40f));
            mute.onClick = [this]
            {
                if (syncing) return;
                issue (model::GroupLogic::flagCommand (mixer.session, index, model::SetTrackFlagCommand::Flag::mute, mute.getToggleState()));
                report (engine::ParamId::mute(), mute.getToggleState() ? 1.0f : 0.0f);
                endGesture (engine::ParamId::mute());
            };
            solo.onClick = [this] { if (! syncing) issue (model::GroupLogic::flagCommand (mixer.session, index, model::SetTrackFlagCommand::Flag::solo, solo.getToggleState())); };

            addAndMakeVisible (vcaBox);
            vcaBox.setTooltip ("VCA master this track follows");
            rebuildVcaMenu();
            vcaBox.onChange = [this]
            {
                if (syncing || vcaBox.getSelectedId() == 0) return;
                issue (std::make_unique<model::SetTrackVcaCommand> (index, vcaBox.getSelectedId() == 1 ? -1 : vcaBox.getSelectedId() - 1000));
            };

            addAndMakeVisible (output);
            rebuildOutputMenu();
            output.onChange = [this]
            {
                if (syncing) return;
                auto* t = track();
                if (t == nullptr) return;
                const int id = output.getSelectedId();
                if (id >= 2000)       // an output path: no bus, path index
                {
                    issue (std::make_unique<model::SetTrackRoutingCommand> (index, t->inputBus, -1));
                    issue (std::make_unique<model::SetTrackPathsCommand> (index, t->inputPath, id - 2000));
                }
                else if (id >= 1000)  // a bus
                    issue (std::make_unique<model::SetTrackRoutingCommand> (index, t->inputBus, id - 1000));
            };
        }

        sync();
    }

    void rebuildVcaMenu()
    {
        vcaBox.clear (juce::dontSendNotification);
        vcaBox.addItem ("No VCA", 1);
        for (const auto& t : mixer.session.getTracks())
            if (t.isVca()) vcaBox.addItem (t.name, 1000 + t.id);
    }

    bool isVca() const { auto* t = track(); return t != nullptr && t->isVca(); }

    void rebuildOutputMenu()
    {
        output.clear (juce::dontSendNotification);
        const auto& io = mixer.session.getIO();
        for (int p = 0; p < (int) io.outputs.size(); ++p) output.addItem (io.outputs[(size_t) p].name, 2000 + p);
        for (int b = 0; b < model::Track::numBuses; ++b) output.addItem (mixer.session.busName (b), 1000 + b);
    }

    bool isMaster() const noexcept { return index < 0; }
    const model::Track* track() const { return isMaster() ? &mixer.session.getMaster() : mixer.session.getTrack (index); }

    void issue (std::unique_ptr<model::Command> cmd)
    {
        const bool replace = gesture && changed;
        if (gesture) changed = true;
        if (mixer.onCommand) mixer.onCommand (std::move (cmd), replace);
    }

    void report (const engine::ParamId& p, float value)
    {
        if (mixer.onParameterChanged) mixer.onParameterChanged (index, p, value, gesture);
    }
    void endGesture (const engine::ParamId& p)
    {
        gesture = false; changed = false;
        if (mixer.onGestureEnded) mixer.onGestureEnded (index, p);
    }

    bool inTrimMode() const { auto* t = track(); return t != nullptr && t->automationMode == model::AutomationMode::trim; }

    // In Read mode the fader/pan/sends follow the automation lane.
    void followAutomation()
    {
        if (gesture || ! mixer.automatedValue) return;
        syncing = true;
        if (auto v = mixer.automatedValue (index, engine::ParamId::volume())) fader.setValue (*v, juce::dontSendNotification);
        if (! isMaster())
        {
            if (auto v = mixer.automatedValue (index, engine::ParamId::pan()))    pan.setValue (*v, juce::dontSendNotification);
            for (int i = 0; i < sendLevels.size(); ++i)
                if (auto v = mixer.automatedValue (index, engine::ParamId::send (i))) sendLevels[i]->setValue (*v, juce::dontSendNotification);
        }
        syncing = false;
    }

    void sync()
    {
        auto* t = track();
        if (t == nullptr) return;
        syncing = true;

        for (int i = 0; i < insertButtons.size(); ++i)
        {
            const auto& ins = t->inserts[(size_t) i];
            insertButtons[i]->setButtonText (ins.isEmpty() ? "-" : ins.displayName() + (ins.keyBus >= 0 ? (ins.keyListen ? " [key!]" : " [key]") : juce::String()));
            insertButtons[i]->setColour (juce::TextButton::buttonColourId, ins.isEmpty() ? theme::background : ins.bypass ? theme::panelDark
                                         : ins.isPlugin() ? juce::Colour (0xff5b6ea8) : theme::accent.darker (0.55f));
            insertButtons[i]->setTooltip (ins.isEmpty() ? "Click to add an insert (built-in or plugin)" : ins.displayName() + (ins.bypass ? " (bypassed)" : "")
                                          + (ins.isPlugin() ? "\nClick: open the plugin window   Right-click: bypass / replace / remove"
                                                            : "\nClick: edit   Right-click: bypass / replace / remove"));
        }
        for (int i = 0; i < sendButtons.size(); ++i)
        {
            const auto& send = t->sends[(size_t) i];
            sendButtons[i]->setButtonText (send.isActive() ? (send.preFader ? "Pre " : "") + mixer.session.busName (send.bus).replace ("Bus ", "B") : "-");
            sendButtons[i]->setColour (juce::TextButton::buttonColourId, send.isActive() ? theme::accent.darker (0.6f) : theme::background);
            sendLevels[i]->setValue (send.gain, juce::dontSendNotification);
            sendLevels[i]->setEnabled (send.isActive());
        }
        if (! isMaster())
        {
            pan.setValue (t->pan, juce::dontSendNotification);
            mute.setToggleState (t->mute, juce::dontSendNotification);
            solo.setToggleState (t->solo, juce::dontSendNotification);
            if (output.getNumItems() != (int) mixer.session.getIO().outputs.size() + model::Track::numBuses) rebuildOutputMenu();
            output.setSelectedId (t->outputBus >= 0 ? 1000 + t->outputBus : 2000 + juce::jmax (0, t->outputPath), juce::dontSendNotification);
            int vcaCount = 1; for (const auto& tr : mixer.session.getTracks()) vcaCount += tr.isVca() ? 1 : 0;
            if (vcaBox.getNumItems() != vcaCount) rebuildVcaMenu();
            vcaBox.setSelectedId (t->vcaTrackId >= 0 ? 1000 + t->vcaTrackId : 1, juce::dontSendNotification);

            // VCA masters: fader, mute, solo, automation only
            const bool vca = t->isVca();
            for (auto* b : insertButtons) b->setVisible (! vca);
            for (auto* b : sendButtons) b->setVisible (! vca);
            for (auto* sl : sendLevels) sl->setVisible (! vca);
            pan.setVisible (! vca);
            output.setVisible (! vca);
            vcaBox.setVisible (! vca);
        }
        {
            autoMode.setSelectedId ((int) t->automationMode + 1, juce::dontSendNotification);
            const bool writing = ! t->writing.empty() || std::abs (t->volumeTrim - 1.0f) > 1.0e-6f;
            autoMode.setColour (juce::ComboBox::backgroundColourId, writing ? theme::record.darker (0.3f)
                                : t->automationMode == model::AutomationMode::trim ? juce::Colour (0xff8e6bbf)
                                : t->automationMode == model::AutomationMode::off ? theme::background : theme::accent.darker (0.65f));
        }
        fader.setValue (t->gain, juce::dontSendNotification);
        syncing = false;
        repaint();
    }

    void insertClicked (int slot)
    {
        auto* t = track();
        if (t == nullptr) return;
        const auto& ins = t->inserts[(size_t) slot];
        const bool rightClick = juce::ModifierKeys::getCurrentModifiers().isPopupMenu();

        if (ins.isEmpty() || rightClick)
        {
            juce::PopupMenu menu;
            int id = 1;
            for (auto type : engine::Effect::availableTypes())
                menu.addItem (id++, engine::Effect::typeName (type), true, ins.type == type);

            // Hosted plugins, grouped by format
            const auto plugins = mixer.knownPlugins ? mixer.knownPlugins() : juce::Array<juce::PluginDescription>();
            juce::PopupMenu pluginMenu;
            juce::StringArray formatsSeen;
            for (const auto& d : plugins) formatsSeen.addIfNotAlreadyThere (d.pluginFormatName);
            for (const auto& fmt : formatsSeen)
            {
                juce::PopupMenu sub;
                for (int i = 0; i < plugins.size(); ++i)
                    if (plugins[i].pluginFormatName == fmt)
                        sub.addItem (1000 + i, plugins[i].name + (plugins[i].manufacturerName.isNotEmpty() ? "  (" + plugins[i].manufacturerName + ")" : juce::String()),
                                     true, ins.isPlugin() && ins.pluginIdentifier == plugins[i].createIdentifierString());
                pluginMenu.addSubMenu (fmt, sub);
            }
            if (plugins.isEmpty()) pluginMenu.addItem (999, "No plugins found - scan first", false);
            pluginMenu.addSeparator();
            pluginMenu.addItem (998, "Scan for Plugins...");
            menu.addSubMenu ("Plugins", pluginMenu);

            if (! ins.isEmpty())
            {
                menu.addSeparator();
                menu.addItem (100, "Bypass", true, ins.bypass);
                menu.addItem (101, "Remove Insert");
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (insertButtons[slot]), [this, slot, plugins] (int result)
            {
                if (result == 0) return;
                auto* tr = track();
                if (tr == nullptr) return;
                if (result == 100)       issue (std::make_unique<model::SetInsertBypassCommand> (index, slot, ! tr->inserts[(size_t) slot].bypass));
                else if (result == 101)  issue (std::make_unique<model::SetInsertCommand> (index, slot, engine::EffectType::none, mixer.sampleRate()));
                else if (result == 998)  { if (mixer.onScanPlugins) mixer.onScanPlugins(); }
                else if (result >= 1000) { if (mixer.onInsertPlugin && result - 1000 < plugins.size()) mixer.onInsertPlugin (index, slot, plugins[result - 1000]); }
                else                     issue (std::make_unique<model::SetInsertCommand> (index, slot, engine::Effect::availableTypes()[(size_t) (result - 1)], mixer.sampleRate()));
            });
            return;
        }

        if (ins.isPlugin())
        {
            if (mixer.onOpenPluginEditor) mixer.onOpenPluginEditor (index, slot);
            return;
        }

        // Edit the effect in a callout
        auto editor = std::make_unique<EffectEditor> (ins, mixer.sampleRate(), mixer.session,
            [this, slot] (std::shared_ptr<const engine::InsertParams> p, bool replace)
            {
                if (mixer.onCommand) mixer.onCommand (std::make_unique<model::SetInsertParamsCommand> (index, slot, std::move (p)), replace);
            },
            [this, slot] (int keyBus, bool listen) { issue (std::make_unique<model::SetInsertKeyCommand> (index, slot, keyBus, listen)); },
            [this, slot] { if (mixer.onLoadImpulse) mixer.onLoadImpulse (index, slot); });
        juce::CallOutBox::launchAsynchronously (std::move (editor), insertButtons[slot]->getScreenBounds(), nullptr);
    }

    void sendClicked (int sendIndex)
    {
        auto* t = track();
        if (t == nullptr) return;
        const auto send = t->sends[(size_t) sendIndex];

        juce::PopupMenu menu;
        for (int b = 0; b < model::Track::numBuses; ++b) menu.addItem (1 + b, mixer.session.busName (b), true, send.bus == b);
        menu.addSeparator();
        menu.addItem (100, "Pre-fader", send.isActive(), send.preFader);
        menu.addItem (101, "No Send", send.isActive());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sendButtons[sendIndex]), [this, sendIndex, send] (int result)
        {
            if (result == 0) return;
            model::Send updated = send;
            if (result == 100)      updated.preFader = ! send.preFader;
            else if (result == 101) updated.bus = -1;
            else                  { updated.bus = result - 1; if (! send.isActive()) updated.gain = 1.0f; }
            issue (std::make_unique<model::SetSendCommand> (index, sendIndex, updated));
        });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (clipBounds.expanded (2).contains (e.getPosition())) { clipHeld = false; repaint(); return; }

        if (meterBounds.expanded (4).contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                int id = 1;
                for (auto m : { model::MeterType::samplePeak, model::MeterType::rms, model::MeterType::peakAndRms, model::MeterType::vu,
                                model::MeterType::k12, model::MeterType::k14, model::MeterType::k20 })
                    menu.addItem (id++, model::meterTypeName (m), true, track() != nullptr && track()->meterType == m);
                if (isMaster()) { menu.addSeparator(); menu.addItem (100, "Reset loudness / peaks"); }
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int result)
                {
                    if (result == 0) return;
                    if (result == 100) { mixer.loudness.reset(); clipHeld = false; return; }
                    issue (std::make_unique<model::SetMeterTypeCommand> (index, (model::MeterType) (result - 1)));
                });
                return;
            }
            if (isMaster()) { clipHeld = false; return; }
        }

        if (isMaster()) return;
        if (mixer.onSelectTrack) mixer.onSelectTrack (index);

        // Group badges: toggle, edit, leave, delete
        if (e.y >= 2 && e.y < 20)
            if (auto* t = track())
            {
                const auto groups = model::GroupLogic::groupsOf (mixer.session, t->id);
                const int which = (e.x - 4) / 16;
                if (juce::isPositiveAndBelow (which, (int) groups.size()))
                {
                    const auto grp = *groups[(size_t) which];
                    juce::PopupMenu menu;
                    menu.addItem (1, grp.name + "  (" + (grp.type == model::Group::Type::edit ? "Edit" : grp.type == model::Group::Type::mix ? "Mix" : "Edit+Mix") + ")", false);
                    menu.addSeparator();
                    menu.addItem (2, "Active", true, grp.active);
                    menu.addItem (3, "Edit Group...");
                    menu.addItem (4, "Remove this track from group");
                    menu.addItem (5, "Delete Group");
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, grp, trackId = t->id] (int r)
                    {
                        if (r == 2) issue (std::make_unique<model::SetGroupActiveCommand> (grp.id, ! grp.active));
                        else if (r == 3) { if (mixer.onEditGroup) mixer.onEditGroup (grp.id); }
                        else if (r == 4) { auto g = grp; std::erase (g.trackIds, trackId); issue (std::make_unique<model::ReplaceGroupCommand> (g)); }
                        else if (r == 5) issue (std::make_unique<model::RemoveGroupCommand> (grp.id));
                    });
                    return;
                }
            }

        // Alt-click the dly readout to type a user offset
        if (e.mods.isAltDown() && e.y >= 22 && e.y < 34)
        {
            auto* t = track();
            if (t == nullptr) return;
            auto* window = new juce::AlertWindow ("Delay Compensation Offset", "User offset for \"" + t->name + "\" in samples (may be negative):",
                                                  juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("offset", juce::String (t->delayOffset));
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
            {
                if (result == 1) issue (std::make_unique<model::SetTrackDelayOffsetCommand> (index, window->getTextEditorContents ("offset").getIntValue()));
            }), true);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto* t = track();
        g.fillAll (isMaster() ? theme::panel : theme::panelDark);
        g.setColour (theme::gridStrong);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        if (t == nullptr) return;

        // Name
        auto top = getLocalBounds().removeFromTop (22);
        g.setColour (t->colour);
        g.fillRect (top.removeFromTop (3));
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (t->name, top.reduced (4, 0), juce::Justification::centred, true);

        // Group badges (letters) next to the name
        if (! isMaster())
        {
            int x = 4;
            for (const auto* grp : model::GroupLogic::groupsOf (mixer.session, t->id))
            {
                auto badge = juce::Rectangle<int> (x, 4, 14, 14);
                g.setColour (grp->active ? grp->colour : theme::gridStrong);
                g.fillRoundedRectangle (badge.toFloat(), 3.0f);
                g.setColour (theme::text);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (grp->badge(), badge, juce::Justification::centred);
                x += 16;
            }
        }

        if (isVca())
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("VCA MASTER", 4, 24, getWidth() - 8, 10, juce::Justification::centredLeft);
            int members = 0;
            for (const auto& tr : mixer.session.getTracks()) members += tr.vcaTrackId == t->id ? 1 : 0;
            g.drawText (juce::String (members) + (members == 1 ? " member" : " members"), 4, 36, getWidth() - 8, 10, juce::Justification::centredLeft);
        }

        // Section labels + delay compensation readout (Pro Tools "dly")
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (9.0f));
        if (! isVca()) g.drawText ("INSERTS", 4, 24, getWidth() - 8, 10, juce::Justification::centredLeft);
        if (! isMaster() && ! isVca() && juce::isPositiveAndBelow (index, (int) mixer.delays.size()))
        {
            const auto& d = mixer.delays[(size_t) index];
            const bool active = d.total() > 0 || d.insertLatency > 0;
            g.setColour (d.userOffset != 0 ? theme::accent : active ? theme::text : theme::textDim);
            g.drawText ("dly " + juce::String (d.total()) + (d.insertLatency > 0 ? "  (" + juce::String (d.insertLatency) + ")" : juce::String()),
                        4, 24, getWidth() - 8, 10, juce::Justification::centredRight);
        }
        if (! isMaster() && ! isVca()) g.drawText ("SENDS", 4, sendsY - 12, getWidth() - 8, 10, juce::Justification::centredLeft);

        // Meters beside the fader, drawn per the track's meter type
        const auto type = t->meterType;
        struct Scale { float minDb, maxDb, zeroDb; };   // meter range and where the "0" mark sits, in the displayed unit
        const Scale scale = type == model::MeterType::vu ? Scale { -20.0f, 3.0f, 0.0f }
                          : type == model::MeterType::k12 || type == model::MeterType::k14 || type == model::MeterType::k20 ? Scale { -24.0f, 12.0f, 0.0f }
                          : Scale { -60.0f, 6.0f, 0.0f };
        const float offset = type == model::MeterType::k12 ? 12.0f : type == model::MeterType::k14 ? 14.0f : type == model::MeterType::k20 ? 20.0f
                           : type == model::MeterType::vu ? 18.0f : 0.0f;   // dB added to RMS so 0 on the meter = -offset dBFS
        const bool useRms = type != model::MeterType::samplePeak;
        const bool showPeakLine = type == model::MeterType::peakAndRms || offset > 0.0f;

        const bool clippedNow = isMaster() ? mixer.graph.getAndClearMasterClip() : mixer.graph.getAndClearStripClip (index);
        if (clippedNow) clipHeld = true;

        auto drawMeter = [&] (juce::Rectangle<int> r, int ch, float& held)
        {
            const float peak = isMaster() ? mixer.graph.getMasterPeak (ch) : mixer.graph.getStripPeak (index, ch);
            const float rms  = isMaster() ? mixer.graph.getMasterRms (ch) : mixer.graph.getStripRms (index, ch);
            const float value = useRms ? rms : peak;
            held = juce::jmax (value, held * (useRms ? 0.97f : 0.85f));

            g.setColour (theme::background);
            g.fillRect (r);
            auto yFor = [&] (float db) { return r.getBottom() - juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db - scale.minDb) / (scale.maxDb - scale.minDb)) * r.getHeight()); };

            const float db = juce::Decibels::gainToDecibels (held, -100.0f) + offset;
            auto lit = r.withTop (yFor (db));
            g.setColour (db > scale.zeroDb ? theme::record : db > scale.zeroDb - 6.0f ? juce::Colour (0xfff1c40f) : theme::play);
            g.fillRect (lit);

            if (showPeakLine)
            {
                const int py = yFor (juce::Decibels::gainToDecibels (peak, -100.0f) + (offset > 0.0f ? 0.0f : 0.0f) + (type == model::MeterType::peakAndRms ? 0.0f : offset));
                g.setColour (theme::text);
                g.drawHorizontalLine (py, (float) r.getX(), (float) r.getRight());
            }
            g.setColour (theme::gridStrong);
            g.drawHorizontalLine (yFor (scale.zeroDb), (float) r.getX(), (float) r.getRight());
        };
        drawMeter (meterBounds.withWidth (meterBounds.getWidth() / 2 - 1), 0, heldL);
        drawMeter (meterBounds.withLeft (meterBounds.getCentreX() + 1), 1, heldR);

        // Clip indicator (holds until clicked) + meter type tag
        g.setColour (clipHeld ? theme::record : theme::background);
        g.fillRect (clipBounds);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (type == model::MeterType::samplePeak ? "PK" : type == model::MeterType::rms ? "RMS" : type == model::MeterType::peakAndRms ? "P+R"
                    : type == model::MeterType::vu ? "VU" : type == model::MeterType::k12 ? "K12" : type == model::MeterType::k14 ? "K14" : "K20",
                    clipBounds.translated (0, 8).withHeight (10), juce::Justification::centred);

        // Gain-reduction bars on dynamics inserts
        for (int i = 0; i < insertButtons.size(); ++i)
        {
            const auto& ins = t->inserts[(size_t) i];
            if (ins.isEmpty()) continue;
            const float gr = ins.instance->getMeter();
            if (gr <= 0.01f) continue;
            auto bar = insertButtons[i]->getBounds().removeFromRight (5).reduced (0, 2);
            g.setColour (theme::background);
            g.fillRect (bar);
            g.setColour (juce::Colour (0xffe67e22));
            g.fillRect (bar.withHeight (juce::roundToInt (juce::jlimit (0.0f, 1.0f, gr / 20.0f) * bar.getHeight())));
        }

        // Master: loudness readout
        if (isMaster())
        {
            const auto r = mixer.loudness.getReadings();
            auto fmt = [] (float v) { return v <= -99.0f ? juce::String ("-") : juce::String (v, 1); };
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            auto area = loudnessBounds;
            auto line = [&] (const juce::String& label, const juce::String& value)
            {
                auto row = area.removeFromTop (12);
                g.setColour (theme::textDim); g.drawText (label, row, juce::Justification::centredLeft);
                g.setColour (theme::text);    g.drawText (value, row, juce::Justification::centredRight);
            };
            line ("M",   fmt (r.momentary));
            line ("S",   fmt (r.shortTerm));
            line ("I",   fmt (r.integrated) + " LUFS");
            line ("LRA", juce::String (r.range, 1) + " LU");
            line ("TP",  fmt (r.truePeakDb) + " dBTP");
        }

        // Fader dB readout (trim offset while trimming)
        g.setColour (trimming ? juce::Colour (0xffc39bd3) : theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (trimming ? (currentTrimDb >= 0.0f ? "+" : "") + juce::String (currentTrimDb, 1) + " trim" : dbText (t->gain),
                    dbBounds, juce::Justification::centred);
        if (! isMaster() && ! isVca())
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (10.0f));
            const int p = juce::roundToInt (t->pan * 100.0f);
            g.drawText (p == 0 ? "C" : p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p), panLabelBounds, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 0);
        area.removeFromTop (34);
        for (auto* b : insertButtons) { b->setBounds (area.removeFromTop (15)); area.removeFromTop (1); }
        if (! isMaster())
        {
            area.removeFromTop (12);
            sendsY = area.getY();
            for (int i = 0; i < sendButtons.size(); ++i)
            {
                auto row = area.removeFromTop (15);
                sendButtons[i]->setBounds (row.removeFromLeft (44));
                row.removeFromLeft (2);
                sendLevels[i]->setBounds (row);
                area.removeFromTop (1);
            }
            area.removeFromTop (4);
            auto panRow = area.removeFromTop (38);
            panLabelBounds = panRow.removeFromBottom (11);
            pan.setBounds (panRow.withSizeKeepingCentre (34, 27));
        }

        auto bottom = area.removeFromBottom (isMaster() ? 22 : 88);
        if (isMaster())
            autoMode.setBounds (bottom.removeFromBottom (20));
        else
        {
            output.setBounds (bottom.removeFromBottom (20));
            bottom.removeFromBottom (2);
            vcaBox.setBounds (bottom.removeFromBottom (20));
            bottom.removeFromBottom (2);
            autoMode.setBounds (bottom.removeFromBottom (20));
            bottom.removeFromBottom (2);
            auto ms = bottom.removeFromBottom (20);
            mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2 - 1));
            ms.removeFromLeft (2);
            solo.setBounds (ms);
        }
        if (isMaster())
        {
            loudnessBounds = area.removeFromBottom (62);
            area.removeFromBottom (4);
        }
        dbBounds = area.removeFromBottom (16);
        area.removeFromBottom (2);
        auto meterColumn = area.removeFromRight (isMaster() ? 22 : 14);
        clipBounds = meterColumn.removeFromTop (6).reduced (1, 0);
        meterColumn.removeFromTop (12);   // meter type tag
        meterBounds = meterColumn.reduced (0, 4);
        area.removeFromRight (4);
        fader.setBounds (area);
    }

private:
    static constexpr int visibleInserts = 5;
    MixerView& mixer;
    int index;
    juce::OwnedArray<juce::TextButton> insertButtons, sendButtons;
    juce::OwnedArray<juce::Slider> sendLevels;
    juce::Slider pan, fader;
    juce::TextButton mute { "M" }, solo { "S" };
    juce::ComboBox output, autoMode, vcaBox;
    juce::Rectangle<int> meterBounds, dbBounds, panLabelBounds, clipBounds, loudnessBounds;
    int sendsY = 0;
    float heldL = 0.0f, heldR = 0.0f;
    bool clipHeld = false;
    bool trimming = false;
    float trimGestureStart = 1.0f, currentTrimDb = 0.0f;
    bool gesture = false, changed = false, syncing = false;
};

//==============================================================================

MixerView::MixerView (model::Session& s, engine::AudioGraph& g, std::function<double()> sr)
    : session (s), graph (g), sampleRate (std::move (sr))
{
    session.addListener (this);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    delays = model::DelayCompensation::compute (session);
    loudness.setSampleRate (sampleRate());
    masterStrip = std::make_unique<ChannelStrip> (*this, -1);
    addAndMakeVisible (*masterStrip);
    rebuildStrips();
    startTimerHz (30);
}

MixerView::~MixerView() { session.removeListener (this); }

void MixerView::rebuildStrips()
{
    strips.clear();
    for (int i = 0; i < session.getNumTracks(); ++i)
        stripHolder.addAndMakeVisible (strips.add (new ChannelStrip (*this, i)));
    resized();
}

void MixerView::sessionChanged (model::Session&)
{
    delays = model::DelayCompensation::compute (session);
    if (strips.size() != session.getNumTracks()) rebuildStrips();
    else for (auto* s : strips) s->sync();
    masterStrip->sync();
}

void MixerView::timerCallback()
{
    // Always drain the loudness FIFO so the analyser keeps integrating while hidden.
    loudnessScratch.clear();
    graph.getLoudnessSource().drain (loudnessScratch);
    for (const auto& b : loudnessScratch) loudness.addBlock (b);

    if (! isShowing()) return;
    for (auto* s : strips) { s->followAutomation(); s->repaint(); }
    masterStrip->repaint();
}

void MixerView::paint (juce::Graphics& g)
{
    ui::UiProfiler::Scope profile ("paint MixerView");
    g.fillAll (theme::background);
    if (strips.isEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Mixer: add a track to see its channel strip", getLocalBounds().withTrimmedRight (ChannelStrip::width), juce::Justification::centred);
    }
}

void MixerView::resized()
{
    auto area = getLocalBounds();
    masterStrip->setBounds (area.removeFromRight (ChannelStrip::width + 8).withTrimmedLeft (8));
    viewport.setBounds (area);
    stripHolder.setSize (juce::jmax (area.getWidth(), strips.size() * ChannelStrip::width), area.getHeight() - (viewport.isHorizontalScrollBarShown() ? 8 : 0));
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStrip::width, 0, ChannelStrip::width, stripHolder.getHeight());
}

} // namespace beatmaker::ui
