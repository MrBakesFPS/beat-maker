#include "PreferencesWindow.h"

namespace beatmaker::ui
{

class PreferencesWindow::Row final : public juce::Component
{
public:
    Row (Preferences& p, const PrefDef& d, std::function<void (const juce::File&)>* folderPicker) : prefs (p), def (d)
    {
        name.setText (def.name, juce::dontSendNotification);
        name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        name.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (name);
        description.setText (def.description, juce::dontSendNotification);
        description.setFont (juce::FontOptions (11.0f));
        description.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (description);

        switch (def.type)
        {
            case PrefDef::Type::toggle:
                toggle = std::make_unique<juce::ToggleButton>();
                toggle->setToggleState (prefs.getBool (def.id), juce::dontSendNotification);
                toggle->onClick = [this] { prefs.set (def.id, toggle->getToggleState()); };
                addAndMakeVisible (*toggle);
                break;
            case PrefDef::Type::number:
                slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                slider->setRange (def.min, def.max, def.step);
                slider->setValue (prefs.getDouble (def.id), juce::dontSendNotification);
                slider->setTextValueSuffix (def.unit.isNotEmpty() ? " " + def.unit : juce::String());
                slider->setColour (juce::Slider::trackColourId, theme::accent);
                slider->onValueChange = [this] { prefs.set (def.id, slider->getValue()); };
                addAndMakeVisible (*slider);
                break;
            case PrefDef::Type::choice:
                box = std::make_unique<juce::ComboBox>();
                for (int i = 0; i < def.choices.size(); ++i) box->addItem (def.choices[i], i + 1);
                box->setSelectedId (prefs.getInt (def.id) + 1, juce::dontSendNotification);
                box->onChange = [this] { if (box->getSelectedId() > 0) prefs.set (def.id, box->getSelectedId() - 1); };
                addAndMakeVisible (*box);
                break;
            case PrefDef::Type::text:
            case PrefDef::Type::folder:
                text = std::make_unique<juce::TextEditor>();
                text->setText (prefs.getString (def.id), juce::dontSendNotification);
                text->onReturnKey = [this] { prefs.set (def.id, text->getText()); };
                text->onFocusLost = [this] { prefs.set (def.id, text->getText()); };
                addAndMakeVisible (*text);
                if (def.type == PrefDef::Type::folder)
                {
                    browse = std::make_unique<juce::TextButton> ("...");
                    browse->onClick = [this, folderPicker]
                    {
                        if (folderPicker != nullptr && *folderPicker)
                            (*folderPicker) (juce::File (prefs.getString (def.id)));
                    };
                    addAndMakeVisible (*browse);
                }
                break;
        }
        resetButton.setButtonText ("Default");
        resetButton.setTooltip ("Reset to " + def.defaultValue.toString());
        resetButton.onClick = [this] { prefs.resetToDefault (def.id); refresh(); };
        addAndMakeVisible (resetButton);
        setSize (600, 54);
    }

    void refresh()
    {
        if (toggle) toggle->setToggleState (prefs.getBool (def.id), juce::dontSendNotification);
        if (slider) slider->setValue (prefs.getDouble (def.id), juce::dontSendNotification);
        if (box) box->setSelectedId (prefs.getInt (def.id) + 1, juce::dontSendNotification);
        if (text) text->setText (prefs.getString (def.id), juce::dontSendNotification);
        resetButton.setVisible (! prefs.isDefault (def.id));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::gridStrong);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        if (! prefs.isDefault (def.id)) { g.setColour (theme::accent); g.fillRect (0, 6, 3, getHeight() - 12); }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 4);
        auto control = area.removeFromRight (260);
        resetButton.setBounds (control.removeFromRight (62).reduced (0, 14));
        control.removeFromRight (8);
        if (toggle) toggle->setBounds (control.removeFromLeft (40).reduced (0, 12));
        if (slider) slider->setBounds (control.reduced (0, 12));
        if (box) box->setBounds (control.reduced (0, 12));
        if (text) { if (browse) { browse->setBounds (control.removeFromRight (30).reduced (0, 12)); control.removeFromRight (4); } text->setBounds (control.reduced (0, 12)); }
        name.setBounds (area.removeFromTop (22));
        description.setBounds (area);
        refresh();
    }

    const PrefDef& getDef() const { return def; }

private:
    Preferences& prefs;
    const PrefDef& def;
    juce::Label name, description;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> box;
    std::unique_ptr<juce::TextEditor> text;
    std::unique_ptr<juce::TextButton> browse;
    juce::TextButton resetButton;
};

PreferencesWindow::PreferencesWindow (Preferences& p) : prefs (p)
{
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search all settings...", theme::textDim);
    search.addListener (this);
    addAndMakeVisible (categories);
    categories.setModel (this);
    categories.setRowHeight (28);
    categories.setColour (juce::ListBox::backgroundColourId, theme::background);
    categories.selectRow (0);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this]
    {
        const auto cats = prefs.categories();
        const int row = categories.getSelectedRow();
        if (juce::isPositiveAndBelow (row, cats.size()))
            for (const auto& d : prefs.all()) if (d.category == cats[row]) prefs.resetToDefault (d.id);
        rebuildRows();
    };
    addAndMakeVisible (countLabel);
    countLabel.setColour (juce::Label::textColourId, theme::textDim);
    countLabel.setFont (juce::FontOptions (11.0f));
    countLabel.setJustificationType (juce::Justification::centredRight);
    listenerToken = prefs.addListener ([this] (const juce::String&) { for (auto* r : rows) r->refresh(); });
    setSize (preferredWidth, preferredHeight);
    rebuildRows();
}

PreferencesWindow::~PreferencesWindow() { prefs.removeListener (listenerToken); }

void PreferencesWindow::showSetting (const juce::String& prefId)
{
    if (const auto* d = prefs.def (prefId)) search.setText (d->name, juce::sendNotification);
}

void PreferencesWindow::rebuildRows()
{
    rows.clear();
    holder.removeAllChildren();
    const auto query = search.getText().trim();
    const auto cats = prefs.categories();
    const int row = categories.getSelectedRow();
    const auto category = juce::isPositiveAndBelow (row, cats.size()) ? cats[row] : juce::String();
    std::vector<const PrefDef*> shown;
    if (query.isNotEmpty()) shown = prefs.search (query);
    else for (const auto& d : prefs.all()) if (d.category == category) shown.push_back (&d);

    int y = 0;
    for (const auto* d : shown)
    {
        auto* r = rows.add (new Row (prefs, *d, &chooseFolder));
        holder.addAndMakeVisible (r);
        r->setBounds (0, y, juce::jmax (100, viewport.getWidth() - 12), 54);
        y += 54;
    }
    holder.setSize (juce::jmax (100, viewport.getWidth() - 12), y);
    countLabel.setText (juce::String (shown.size()) + (query.isNotEmpty() ? " matching settings" : " settings"), juce::dontSendNotification);
    resetButton.setVisible (query.isEmpty());
}

void PreferencesWindow::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto cats = prefs.categories();
    if (! juce::isPositiveAndBelow (row, cats.size())) return;
    if (selected) { g.setColour (theme::accent.withAlpha (0.3f)); g.fillRect (0, 0, width, height); }
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (cats[row], 12, 0, width - 16, height, juce::Justification::centredLeft);
}

void PreferencesWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Preferences", 16, 10, 200, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Settings apply immediately and are saved in ~/.config/Beat Maker/Beat Maker.preferences. Changed settings show a blue mark.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void PreferencesWindow::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (36);
    auto top = area.removeFromTop (28);
    search.setBounds (top.removeFromLeft (300));
    countLabel.setBounds (top.removeFromRight (200));
    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (26);
    resetButton.setBounds (bottom.removeFromLeft (200));
    area.removeFromBottom (8);
    categories.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (8);
    viewport.setBounds (area);
    rebuildRows();
}

} // namespace beatmaker::ui
