#include "IOSetupDialog.h"

namespace beatmaker::ui
{

// One tab: a scrollable list of path rows (name, first channel, width).
class IOSetupDialog::PathList final : public juce::Component
{
public:
    enum class Kind { input, output, bus };

    PathList (Kind k, model::IOSetup& setup, int deviceChannels) : kind (k), io (setup), channels (deviceChannels)
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (addButton);
        addButton.onClick = [this]
        {
            if (kind == Kind::input)       io.inputs.push_back ({ "In " + juce::String (io.inputs.size() + 1), 0, 1 });
            else if (kind == Kind::output) io.outputs.push_back ({ "Out " + juce::String (io.outputs.size() + 1), 0, 2 });
            rebuild();
        };
        addButton.setVisible (kind != Kind::bus);
        rebuild();
    }

    void rebuild()
    {
        rows.clear();
        const int count = kind == Kind::input ? (int) io.inputs.size() : kind == Kind::output ? (int) io.outputs.size() : (int) io.busNames.size();
        for (int i = 0; i < count; ++i)
        {
            auto* row = rows.add (new Row());
            holder.addAndMakeVisible (row->name);
            row->name.setText (kind == Kind::bus ? io.busName (i) : (kind == Kind::input ? io.inputs[(size_t) i] : io.outputs[(size_t) i]).name, juce::dontSendNotification);
            row->name.onTextChange = [this, i, row]
            {
                if (kind == Kind::input)       io.inputs[(size_t) i].name = row->name.getText();
                else if (kind == Kind::output) io.outputs[(size_t) i].name = row->name.getText();
                else                           io.busNames[(size_t) i] = row->name.getText();
            };

            if (kind != Kind::bus)
            {
                auto& path = kind == Kind::input ? io.inputs[(size_t) i] : io.outputs[(size_t) i];
                holder.addAndMakeVisible (row->channel);
                for (int ch = 0; ch < juce::jmax (channels, path.firstChannel + 1); ++ch)
                    row->channel.addItem ("Ch " + juce::String (ch + 1), ch + 1);
                row->channel.setSelectedId (path.firstChannel + 1, juce::dontSendNotification);
                row->channel.onChange = [this, i, row]
                {
                    auto& p = kind == Kind::input ? io.inputs[(size_t) i] : io.outputs[(size_t) i];
                    p.firstChannel = juce::jmax (0, row->channel.getSelectedId() - 1);
                };

                holder.addAndMakeVisible (row->width);
                row->width.addItem ("Mono", 1);
                row->width.addItem ("Stereo", 2);
                row->width.setSelectedId (path.numChannels, juce::dontSendNotification);
                row->width.onChange = [this, i, row]
                {
                    auto& p = kind == Kind::input ? io.inputs[(size_t) i] : io.outputs[(size_t) i];
                    p.numChannels = juce::jmax (1, row->width.getSelectedId());
                };

                holder.addAndMakeVisible (row->remove);
                row->remove.setEnabled (! (kind == Kind::output && i == 0));   // Main can't be removed
                row->remove.onClick = [this, i]
                {
                    if (kind == Kind::input)       io.inputs.erase (io.inputs.begin() + i);
                    else if (kind == Kind::output) io.outputs.erase (io.outputs.begin() + i);
                    rebuild();
                };
            }
        }
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        addButton.setBounds (area.removeFromBottom (24).removeFromLeft (100));
        area.removeFromBottom (6);
        viewport.setBounds (area);
        const int rowHeight = 28;
        holder.setSize (area.getWidth() - 10, juce::jmax (area.getHeight(), rows.size() * rowHeight));
        for (int i = 0; i < rows.size(); ++i)
        {
            auto r = juce::Rectangle<int> (0, i * rowHeight, holder.getWidth(), rowHeight).reduced (0, 3);
            auto* row = rows[i];
            if (kind != Kind::bus)
            {
                row->remove.setBounds (r.removeFromRight (28)); r.removeFromRight (6);
                row->width.setBounds (r.removeFromRight (90)); r.removeFromRight (6);
                row->channel.setBounds (r.removeFromRight (90)); r.removeFromRight (6);
            }
            row->name.setBounds (r);
        }
    }

private:
    struct Row { juce::TextEditor name; juce::ComboBox channel, width; juce::TextButton remove { "-" }; };
    Kind kind;
    model::IOSetup& io;
    int channels;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Row> rows;
    juce::TextButton addButton { "+ Path" };
};

IOSetupDialog::IOSetupDialog (model::IOSetup s, int ins, int outs, bool adc)
    : setup (std::move (s)), deviceInputs (ins), deviceOutputs (outs)
{
    addAndMakeVisible (tabs);
    tabs.addTab ("Input",  theme::panelDark, new PathList (PathList::Kind::input,  setup, deviceInputs), true);
    tabs.addTab ("Output", theme::panelDark, new PathList (PathList::Kind::output, setup, deviceOutputs), true);
    tabs.addTab ("Bus",    theme::panelDark, new PathList (PathList::Kind::bus,    setup, 0), true);

    addAndMakeVisible (adcToggle);
    adcToggle.setToggleState (adc, juce::dontSendNotification);
    adcToggle.setTooltip ("Delay every track so insert latency never shifts timing between tracks");

    addAndMakeVisible (info);
    info.setColour (juce::Label::textColourId, theme::textDim);
    info.setFont (juce::FontOptions (11.0f));
    info.setText ("Device: " + juce::String (deviceInputs) + " in / " + juce::String (deviceOutputs) + " out.  Paths map names to device channels.",
                  juce::dontSendNotification);

    addAndMakeVisible (applyButton);
    applyButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.5f));
    applyButton.onClick = [this] { if (onApply) onApply (setup, adcToggle.getToggleState()); };
    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };

    setSize (preferredWidth, preferredHeight);
}

void IOSetupDialog::paint (juce::Graphics& g) { g.fillAll (theme::panel); }

void IOSetupDialog::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto buttons = area.removeFromBottom (28);
    applyButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (80));
    info.setBounds (buttons);
    area.removeFromBottom (8);
    adcToggle.setBounds (area.removeFromBottom (24));
    area.removeFromBottom (8);
    tabs.setBounds (area);
}

} // namespace beatmaker::ui
