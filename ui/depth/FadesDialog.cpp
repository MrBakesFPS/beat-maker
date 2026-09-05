#include "FadesDialog.h"

namespace beatmaker::ui
{

FadesDialog::FadesDialog (Values initial, int numClips) : clips (numClips)
{
    for (auto* l : { &inLabel, &outLabel, &gainLabel })
    {
        addAndMakeVisible (l);
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }

    auto setupSlider = [this] (juce::Slider& s, double min, double max, double value, const juce::String& suffix)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
        s.setRange (min, max, 0.1);
        s.setValue (value, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] { repaint(); };
    };
    setupSlider (inSlider, 0.0, 5000.0, initial.fadeInMs, " ms");
    setupSlider (outSlider, 0.0, 5000.0, initial.fadeOutMs, " ms");
    setupSlider (gainSlider, -60.0, 12.0, initial.gainDb, " dB");
    inSlider.setSkewFactorFromMidPoint (500.0);
    outSlider.setSkewFactorFromMidPoint (500.0);

    for (auto* box : { &inShapeBox, &outShapeBox })
    {
        addAndMakeVisible (box);
        int id = 1;
        for (auto shape : { engine::FadeShape::linear, engine::FadeShape::equalPower, engine::FadeShape::sCurve })
            box->addItem (engine::fadeShapeName (shape), id++);
        box->onChange = [this] { repaint(); };
    }
    inShapeBox.setSelectedId ((int) initial.inShape + 1, juce::dontSendNotification);
    outShapeBox.setSelectedId ((int) initial.outShape + 1, juce::dontSendNotification);

    addAndMakeVisible (info);
    info.setColour (juce::Label::textColourId, theme::textDim);
    info.setFont (juce::FontOptions (12.0f));
    info.setText (juce::String (clips) + (clips == 1 ? " clip selected" : " clips selected (batch fades)"), juce::dontSendNotification);

    addAndMakeVisible (applyButton);
    applyButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.5f));
    applyButton.onClick = [this] { if (onApply) onApply (current()); };
    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };

    setSize (preferredWidth, preferredHeight);
}

FadesDialog::Values FadesDialog::current() const
{
    Values v;
    v.fadeInMs = inSlider.getValue();
    v.fadeOutMs = outSlider.getValue();
    v.inShape = (engine::FadeShape) juce::jmax (0, inShapeBox.getSelectedId() - 1);
    v.outShape = (engine::FadeShape) juce::jmax (0, outShapeBox.getSelectedId() - 1);
    v.gainDb = (float) gainSlider.getValue();
    return v;
}

void FadesDialog::drawCurve (juce::Graphics& g, juce::Rectangle<float> r, engine::FadeShape shape, bool fadeIn) const
{
    juce::Path p;
    for (int i = 0; i <= 40; ++i)
    {
        const double t = i / 40.0;
        const float gain = engine::fadeGain (shape, fadeIn ? t : 1.0 - t);
        const float x = r.getX() + (float) t * r.getWidth();
        const float y = r.getBottom() - gain * r.getHeight();
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (theme::accent);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

void FadesDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    const auto v = current();
    auto preview = getLocalBounds().reduced (20).removeFromTop (60).toFloat();
    auto left = preview.removeFromLeft (preview.getWidth() / 2.0f - 6.0f);
    preview.removeFromLeft (12.0f);
    g.setColour (theme::background);
    g.fillRoundedRectangle (left, 4.0f);
    g.fillRoundedRectangle (preview, 4.0f);
    drawCurve (g, left.reduced (6.0f), v.inShape, true);
    drawCurve (g, preview.reduced (6.0f), v.outShape, false);
}

void FadesDialog::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (60 + 12);   // curve previews
    const int labelWidth = 72, rowHeight = 26, gap = 8;

    auto row = [&] (juce::Label& l, juce::Component& a, juce::Component* b)
    {
        auto r = area.removeFromTop (rowHeight);
        l.setBounds (r.removeFromLeft (labelWidth));
        r.removeFromLeft (8);
        if (b != nullptr) { b->setBounds (r.removeFromRight (110)); r.removeFromRight (8); }
        a.setBounds (r);
        area.removeFromTop (gap);
    };
    row (inLabel, inSlider, &inShapeBox);
    row (outLabel, outSlider, &outShapeBox);
    row (gainLabel, gainSlider, nullptr);

    auto buttons = area.removeFromBottom (28);
    applyButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (80));
    info.setBounds (buttons);
}

} // namespace beatmaker::ui
