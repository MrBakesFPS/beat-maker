#include "PresetBuilder.h"

namespace beatmaker::ui
{

PresetBuilder::PresetBuilder (engine::AudioGraph* g, engine::InstrumentType type, const juce::String& startFrom, const engine::InstrumentParams* initial) : graph (g)
{
    for (auto* label : { &instrumentLabel, &startLabel, &nameLabel, &waveLabel, &hint })
    {
        label->setColour (juce::Label::textColourId, theme::textDim);
        label->setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (*label);
    }
    addAndMakeVisible (instrumentBox);
    int id = 1;
    juce::String lastCategory;
    for (auto t : engine::Instrument::availableTypes())
    {
        if (juce::String (engine::Instrument::typeCategory (t)) != lastCategory) { lastCategory = engine::Instrument::typeCategory (t); instrumentBox.addSectionHeading (lastCategory); }
        instrumentBox.addItem (engine::Instrument::typeName (t), id++);
        instrumentIds.push_back (t);
    }
    instrumentBox.onChange = [this]
    {
        const int i = instrumentBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (i, (int) instrumentIds.size()) || instrumentIds[(size_t) i] == currentType) return;
        currentType = instrumentIds[(size_t) i];
        editingUser = false;
        rebuildStartMenu();
        loadPreset ({});
    };
    addAndMakeVisible (startBox);
    startBox.onChange = [this]
    {
        const int i = startBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (i, (int) startNames.size())) loadPreset (startNames[(size_t) i]);
    };
    addAndMakeVisible (nameEditor);
    addAndMakeVisible (knobArea);
    playButton.setTooltip ("Play a short phrase on the sound as it stands");
    playButton.onClick = [this] { play(); };
    saveButton.onClick = [this] { save (false); };
    saveAddButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    saveAddButton.onClick = [this] { save (true); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    for (auto* b : { &playButton, &saveButton, &saveAddButton, &cancelButton }) addAndMakeVisible (*b);
    playButton.setEnabled (graph != nullptr);

    currentType = type != engine::InstrumentType::none ? type : engine::InstrumentType::subtractive;
    for (int i = 0; i < (int) instrumentIds.size(); ++i) if (instrumentIds[(size_t) i] == currentType) instrumentBox.setSelectedId (i + 1, juce::dontSendNotification);
    rebuildStartMenu();
    if (initial != nullptr)
    {
        current = *initial; current.type = currentType;
        editingUser = engine::Instrument::isUserPreset (currentType, current.presetName);
        nameEditor.setText (editingUser ? current.presetName : current.presetName + " 2");
        for (int i = 0; i < (int) startNames.size(); ++i) if (startNames[(size_t) i] == current.presetName) startBox.setSelectedId (i + 1, juce::dontSendNotification);
        rebuildKnobs();
    }
    else loadPreset (startFrom);
    setSize (preferredWidth, preferredHeight);
}

PresetBuilder::~PresetBuilder()
{
    stopTimer();
    if (graph != nullptr) graph->setAuditionInstrument (nullptr, nullptr);
}

void PresetBuilder::rebuildStartMenu()
{
    startBox.clear (juce::dontSendNotification);
    startNames.clear();
    int id = 1;
    bool headed = false;
    for (const auto& p : engine::Instrument::presets (currentType))
    {
        if (engine::Instrument::isUserPreset (currentType, p.presetName) && ! headed) { startBox.addSectionHeading ("My Presets"); headed = true; }
        startBox.addItem (p.presetName, id++);
        startNames.push_back (p.presetName);
    }
}

void PresetBuilder::loadPreset (const juce::String& name)
{
    const auto all = engine::Instrument::presets (currentType);
    size_t index = currentType == engine::InstrumentType::subtractive && all.size() > 1 ? 1 : 0;
    for (size_t i = 0; i < all.size(); ++i) if (name.isNotEmpty() && all[i].presetName == name) index = i;
    current = all.empty() ? engine::Instrument::defaultParams (currentType) : all[index];
    editingUser = engine::Instrument::isUserPreset (currentType, current.presetName);
    nameEditor.setText (editingUser ? current.presetName : current.presetName + " 2");
    startBox.setSelectedId ((int) index + 1, juce::dontSendNotification);
    rebuildKnobs();
    if (auditionLoaded) pushAudition (true);
}

void PresetBuilder::rebuildKnobs()
{
    knobs.clear(); waveBox.reset(); waveParam = -1;
    const auto& info = engine::Instrument::paramInfo (currentType);
    auto seconds = [] (double v) { return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s"; };
    auto hz = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    auto pct = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    auto onOff = [] (double v) { return juce::String (v >= 0.5 ? "On" : "Off"); };
    auto db = [] (double v) { return v <= 0.0 ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (v), 1) + " dB"; };
    auto plain = [] (double v) { return juce::String (v, 2); };
    for (int i = 0; i < (int) info.size(); ++i)
    {
        const auto& pi = info[(size_t) i];
        const juce::String name (pi.name), suffix (pi.suffix);
        const double value = current.values[(size_t) i];
        if (name == "Wave" && currentType == engine::InstrumentType::subtractive)
        {
            waveParam = i;
            waveBox = std::make_unique<juce::ComboBox>();
            const char* waveNames[] = { "Saw", "Square", "Triangle", "Sine" };
            for (int w = 0; w < 4; ++w) waveBox->addItem (waveNames[w], w + 1);
            waveBox->setSelectedId ((int) std::lround (value) + 1, juce::dontSendNotification);
            waveBox->onChange = [this] { if (waveBox->getSelectedId() > 0) setParam (waveParam, (float) (waveBox->getSelectedId() - 1)); };
            knobArea.addAndMakeVisible (*waveBox);
            continue;
        }
        Knob k;
        k.param = i;
        k.slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        auto& sl = *k.slider;
        sl.setTitle (name);
        sl.setRange (pi.min, pi.max, 0.0);
        if (pi.skewMidpoint > pi.min && pi.skewMidpoint < pi.max) sl.setSkewFactorFromMidPoint (pi.skewMidpoint);
        const bool unit = pi.min <= 0.0f && std::abs (pi.max - 1.0f) < 1.0e-6f;
        const bool toggle = unit && (name == "Osc 2" || name == "Loop");
        std::function<juce::String (double)> text;
        if (toggle) text = onOff; else if (name == "Level") text = db; else if (suffix == " s") text = seconds; else if (suffix == " Hz") text = hz;
        else if (suffix.isEmpty() && unit) text = pct; else if (suffix.isEmpty()) text = plain; else sl.setTextValueSuffix (suffix);
        if (text) sl.textFromValueFunction = text;
        if (toggle) sl.setRange (0.0, 1.0, 1.0);
        sl.setNumDecimalPlacesToDisplay (2);
        sl.setValue (value, juce::dontSendNotification);
        sl.updateText();
        sl.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
        sl.setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
        sl.setColour (juce::Slider::rotarySliderOutlineColourId, theme::gridStrong);
        sl.setColour (juce::Slider::textBoxTextColourId, theme::text);
        sl.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        sl.setDoubleClickReturnValue (true, pi.def);
        sl.onValueChange = [this, slider = &sl, index = i] { setParam (index, (float) slider->getValue()); };
        k.label = std::make_unique<juce::Label> (juce::String(), name);
        k.label->setJustificationType (juce::Justification::centred);
        k.label->setColour (juce::Label::textColourId, theme::textDim);
        k.label->setFont (juce::FontOptions (11.0f));
        knobArea.addAndMakeVisible (*k.slider);
        knobArea.addAndMakeVisible (*k.label);
        knobs.push_back (std::move (k));
    }
    resized();
    repaint();
}

void PresetBuilder::setParam (int index, float value)
{
    if (! juce::isPositiveAndBelow (index, (int) current.values.size())) return;
    current.values[(size_t) index] = value;
    if (auditionLoaded) { paramsDirty = true; if (! isTimerRunning()) startTimer (40); }
}

engine::InstrumentParams PresetBuilder::params() const
{
    auto p = current;
    p.type = currentType;
    p.presetName = nameEditor.getText().trim();
    return p;
}

void PresetBuilder::pushAudition (bool recreate)
{
    if (graph == nullptr) return;
    auto p = params();
    if (engine::Instrument::usesSample (currentType) && p.sample == nullptr)
    {
        auto tone = std::make_shared<juce::AudioBuffer<float>> (1, 24000);
        for (int i = 0; i < 24000; ++i) tone->setSample (0, i, (float) (std::sin (juce::MathConstants<double>::twoPi * 261.63 * i / 48000.0) * (1.0 - i / 24000.0)));
        p.sample = tone; p.sampleRate = 48000.0; p.rootNote = 60; p.sampleName = "Preview tone";
    }
    if (recreate || ! auditionLoaded) graph->setAuditionInstrument (engine::Instrument::create (currentType, 48000.0), std::make_shared<const engine::InstrumentParams> (p));
    else graph->updateAuditionParams (std::make_shared<const engine::InstrumentParams> (p));
    auditionLoaded = true;
    paramsDirty = false;
}

void PresetBuilder::play()
{
    if (graph == nullptr) return;
    pushAudition (! auditionLoaded);
    phraseStep = 0;
    timerCallback();
}

void PresetBuilder::timerCallback()
{
    if (paramsDirty && auditionLoaded) pushAudition (false);
    if (graph == nullptr || phraseStep < 0) { if (! paramsDirty) stopTimer(); return; }
    static constexpr int notes[3] = { 60, 64, 67 };
    if (phraseStep < 3) { graph->triggerAuditionNote (notes[phraseStep], 0.85f, 0.26); ++phraseStep; startTimer (280); return; }
    if (phraseStep == 3) { for (int n : { 60, 64, 67, 72 }) graph->triggerAuditionNote (n, 0.8f, 1.1); ++phraseStep; startTimer (1400); return; }
    phraseStep = -1;
    if (! paramsDirty) stopTimer();
}

void PresetBuilder::save (bool addTrack)
{
    const auto p = params();
    if (p.presetName.isEmpty()) { hint.setText ("Give the preset a name", juce::dontSendNotification); return; }
    for (const auto& b : engine::Instrument::bundledPresets (currentType))
        if (b.presetName == p.presetName) { hint.setText ("That is a bundled preset's name; pick another", juce::dontSendNotification); return; }
    if (onSave) onSave (currentType, p, addTrack);
}

void PresetBuilder::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (editingUser ? "Edit Preset" : "Build Your Own Preset", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Pick an instrument and a preset to start from, turn the knobs, press Play to hear it, then save it under a name of your own.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void PresetBuilder::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (36);
    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    saveAddButton.setBounds (buttons.removeFromRight (150)); buttons.removeFromRight (6);
    saveButton.setBounds (buttons.removeFromRight (110));
    playButton.setBounds (buttons.removeFromLeft (70)); buttons.removeFromLeft (10);
    hint.setBounds (buttons);
    area.removeFromBottom (8);
    auto nameRow = area.removeFromBottom (26);
    nameLabel.setBounds (nameRow.removeFromLeft (80)); nameEditor.setBounds (nameRow.removeFromLeft (260));
    area.removeFromBottom (10);
    auto top = area.removeFromTop (24);
    instrumentLabel.setBounds (top.removeFromLeft (70)); instrumentBox.setBounds (top.removeFromLeft (180)); top.removeFromLeft (16);
    startLabel.setBounds (top.removeFromLeft (66)); startBox.setBounds (top.removeFromLeft (200));
    area.removeFromTop (12);
    knobArea.setBounds (area);
    // Knobs in rows of eight
    const int perRow = 8, w = knobArea.getWidth() / perRow, h = 88;
    int x = 0, y = 0;
    if (waveBox != nullptr)
    {
        waveLabel.setBounds (knobArea.getX(), knobArea.getY(), w, 14);
        waveBox->setBounds (4, 16, w - 8, 22);
        x = 1;
    }
    for (auto& k : knobs)
    {
        k.label->setBounds (x * w, y * h, w, 14);
        k.slider->setBounds (x * w, y * h + 14, w, h - 16);
        if (++x >= perRow) { x = 0; ++y; }
    }
}

} // namespace beatmaker::ui
