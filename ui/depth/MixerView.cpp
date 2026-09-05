#include "MixerView.h"

namespace beatmaker::ui
{

namespace
{
    juce::String dbText (float gain) { return gain <= 0.0001f ? "-inf" : juce::String (juce::Decibels::gainToDecibels (gain), 1); }
}

//==============================================================================
// Effect editor shown in a callout when an insert is clicked

class EffectEditor final : public juce::Component
{
public:
    EffectEditor (const model::Insert& insert, std::function<void (std::shared_ptr<const engine::InsertParams>, bool replace)> onChange)
        : type (insert.type), params (insert.params != nullptr ? *insert.params : engine::Effect::defaultParams (insert.type)), apply (std::move (onChange))
    {
        const auto& info = engine::Effect::paramInfo (type);
        for (size_t i = 0; i < info.size(); ++i)
        {
            auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            s->setRange (info[i].min, info[i].max, 0.0);
            if (info[i].skewMidpoint > info[i].min && info[i].skewMidpoint < info[i].max) s->setSkewFactorFromMidPoint (info[i].skewMidpoint);
            s->setNumDecimalPlacesToDisplay (info[i].max - info[i].min > 100.0f ? 0 : 1);
            s->setTextValueSuffix (info[i].suffix);
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
            };
            addAndMakeVisible (s);

            auto* l = labels.add (new juce::Label ({}, info[i].name));
            l->setJustificationType (juce::Justification::centred);
            l->setColour (juce::Label::textColourId, theme::textDim);
            l->setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (l);
        }
        setSize (juce::jmax (1, sliders.size()) * 76 + 16, 110);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (engine::Effect::typeName (type), 8, 4, getWidth() - 16, 14, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8).withTrimmedTop (18);
        for (int i = 0; i < sliders.size(); ++i)
        {
            auto col = area.removeFromLeft (76);
            labels[i]->setBounds (col.removeFromTop (14));
            sliders[i]->setBounds (col);
        }
    }

private:
    engine::EffectType type;
    engine::InsertParams params;
    std::function<void (std::shared_ptr<const engine::InsertParams>, bool)> apply;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
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
                    issue (std::make_unique<model::SetTrackMixCommand> (index, t->gain, (float) pan.getValue()));
                    report (engine::ParamId::pan(), (float) pan.getValue());
                }
            };
        }

        addAndMakeVisible (fader);
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 2.0, 0.0);
        fader.setSkewFactorFromMidPoint (1.0);
        fader.setDoubleClickReturnValue (true, 1.0);
        fader.setColour (juce::Slider::trackColourId, theme::gridStrong);
        fader.setColour (juce::Slider::backgroundColourId, theme::background);
        fader.setColour (juce::Slider::thumbColourId, theme::text);
        fader.onDragStart = [this] { gesture = true; changed = false; };
        fader.onDragEnd = [this] { endGesture (engine::ParamId::volume()); };
        fader.onValueChange = [this]
        {
            if (syncing) return;
            if (auto* t = track())
            {
                issue (std::make_unique<model::SetTrackMixCommand> (index, (float) fader.getValue(), t->pan));
                report (engine::ParamId::volume(), (float) fader.getValue());
            }
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
                issue (std::make_unique<model::SetTrackFlagCommand> (index, model::SetTrackFlagCommand::Flag::mute, mute.getToggleState()));
                report (engine::ParamId::mute(), mute.getToggleState() ? 1.0f : 0.0f);
                endGesture (engine::ParamId::mute());
            };
            solo.onClick = [this] { if (! syncing) issue (std::make_unique<model::SetTrackFlagCommand> (index, model::SetTrackFlagCommand::Flag::solo, solo.getToggleState())); };

            addAndMakeVisible (autoMode);
            int am = 1;
            for (auto m : { model::AutomationMode::off, model::AutomationMode::read, model::AutomationMode::touch, model::AutomationMode::latch, model::AutomationMode::write })
                autoMode.addItem (model::automationModeName (m), am++);
            autoMode.setTooltip ("Automation mode");
            autoMode.onChange = [this]
            {
                if (! syncing && autoMode.getSelectedId() > 0)
                    issue (std::make_unique<model::SetAutomationModeCommand> (index, (model::AutomationMode) (autoMode.getSelectedId() - 1)));
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
        if (! isMaster() && mixer.onParameterChanged) mixer.onParameterChanged (index, p, value, gesture);
    }
    void endGesture (const engine::ParamId& p)
    {
        gesture = false; changed = false;
        if (! isMaster() && mixer.onGestureEnded) mixer.onGestureEnded (index, p);
    }

    // In Read mode the fader/pan/sends follow the automation lane.
    void followAutomation()
    {
        if (isMaster() || gesture || ! mixer.automatedValue) return;
        syncing = true;
        if (auto v = mixer.automatedValue (index, engine::ParamId::volume())) fader.setValue (*v, juce::dontSendNotification);
        if (auto v = mixer.automatedValue (index, engine::ParamId::pan()))    pan.setValue (*v, juce::dontSendNotification);
        for (int i = 0; i < sendLevels.size(); ++i)
            if (auto v = mixer.automatedValue (index, engine::ParamId::send (i))) sendLevels[i]->setValue (*v, juce::dontSendNotification);
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
            insertButtons[i]->setButtonText (ins.isEmpty() ? "-" : engine::Effect::typeName (ins.type));
            insertButtons[i]->setColour (juce::TextButton::buttonColourId, ins.isEmpty() ? theme::background : ins.bypass ? theme::panelDark : theme::accent.darker (0.55f));
            insertButtons[i]->setTooltip (ins.isEmpty() ? "Click to add an insert" : juce::String (engine::Effect::typeName (ins.type)) + (ins.bypass ? " (bypassed)" : "") + "\nClick: edit   Right-click: bypass / replace / remove");
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
            autoMode.setSelectedId ((int) t->automationMode + 1, juce::dontSendNotification);
            const bool writing = ! t->writing.empty();
            autoMode.setColour (juce::ComboBox::backgroundColourId, writing ? theme::record.darker (0.3f)
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
            if (! ins.isEmpty())
            {
                menu.addSeparator();
                menu.addItem (100, "Bypass", true, ins.bypass);
                menu.addItem (101, "Remove Insert");
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (insertButtons[slot]), [this, slot] (int result)
            {
                if (result == 0) return;
                auto* tr = track();
                if (tr == nullptr) return;
                if (result == 100)      issue (std::make_unique<model::SetInsertBypassCommand> (index, slot, ! tr->inserts[(size_t) slot].bypass));
                else if (result == 101) issue (std::make_unique<model::SetInsertCommand> (index, slot, engine::EffectType::none, mixer.sampleRate()));
                else                    issue (std::make_unique<model::SetInsertCommand> (index, slot, engine::Effect::availableTypes()[(size_t) (result - 1)], mixer.sampleRate()));
            });
            return;
        }

        // Edit the effect in a callout
        auto editor = std::make_unique<EffectEditor> (ins, [this, slot] (std::shared_ptr<const engine::InsertParams> p, bool replace)
        {
            if (mixer.onCommand) mixer.onCommand (std::make_unique<model::SetInsertParamsCommand> (index, slot, std::move (p)), replace);
        });
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

        // Section labels + delay compensation readout (Pro Tools "dly")
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("INSERTS", 4, 24, getWidth() - 8, 10, juce::Justification::centredLeft);
        if (! isMaster() && juce::isPositiveAndBelow (index, (int) mixer.delays.size()))
        {
            const auto& d = mixer.delays[(size_t) index];
            const bool active = d.total() > 0 || d.insertLatency > 0;
            g.setColour (d.userOffset != 0 ? theme::accent : active ? theme::text : theme::textDim);
            g.drawText ("dly " + juce::String (d.total()) + (d.insertLatency > 0 ? "  (" + juce::String (d.insertLatency) + ")" : juce::String()),
                        4, 24, getWidth() - 8, 10, juce::Justification::centredRight);
        }
        if (! isMaster()) g.drawText ("SENDS", 4, sendsY - 12, getWidth() - 8, 10, juce::Justification::centredLeft);

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

        // Fader dB readout
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (dbText (t->gain), dbBounds, juce::Justification::centred);
        if (! isMaster())
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

        auto bottom = area.removeFromBottom (isMaster() ? 22 : 66);
        if (! isMaster())
        {
            output.setBounds (bottom.removeFromBottom (20));
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
    juce::ComboBox output, autoMode;
    juce::Rectangle<int> meterBounds, dbBounds, panLabelBounds, clipBounds, loudnessBounds;
    int sendsY = 0;
    float heldL = 0.0f, heldR = 0.0f;
    bool clipHeld = false;
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
