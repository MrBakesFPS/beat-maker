#include "LoopBrowser.h"

namespace beatmaker::ui
{

using persistence::LoopInfo;

LoopBrowser::LoopBrowser (persistence::LoopLibrary& lib) : library (lib)
{
    library.addListener (this);

    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search loops...", theme::textDim);
    search.setColour (juce::TextEditor::backgroundColourId, theme::background);
    search.onTextChange = [this] { refilter(); };
    search.onEscapeKey = [this] { search.setText ({}); };

    auto addCategory = [this] (const juce::String& name, int id)
    {
        auto* b = categoryButtons.add (new juce::TextButton (name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setToggleState (id == -1, juce::dontSendNotification);
        b->setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.5f));
        b->onClick = [this, id] { categoryFilter = id; refilter(); };
        addAndMakeVisible (b);
    };
    addCategory ("All", -1);
    for (int c = 0; c <= (int) LoopInfo::Category::other; ++c)
        addCategory (LoopInfo::categoryName ((LoopInfo::Category) c), c);

    addAndMakeVisible (list);
    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    list.setMultipleSelectionEnabled (false);

    addFolderButton.setTooltip ("Add a folder of loops to the Library");
    rescanButton.setTooltip ("Rescan the Library folders");
    addAndMakeVisible (addFolderButton);
    addFolderButton.setTooltip ("Add a folder of loops to the library");
    addFolderButton.onClick = [this] { if (onAddFolder) onAddFolder(); };

    addAndMakeVisible (rescanButton);
    rescanButton.onClick = [this] { library.rescanAsync(); statusLabel.setText ("Scanning...", juce::dontSendNotification); };

    closeButton.setTooltip ("Close the Sample Library (Escape, or L again)");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, theme::textDim);
    statusLabel.setFont (juce::FontOptions (11.0f));

    refilter();
}

LoopBrowser::~LoopBrowser()
{
    library.removeListener (this);
}

void LoopBrowser::stopPreview()
{
    if (previewing == nullptr) return;
    previewing = nullptr;
    list.deselectAllRows();
    list.repaint();
    if (onPreview) onPreview (nullptr);
}

void LoopBrowser::refilter()
{
    const auto query = search.getText().trim().toLowerCase();
    const LoopInfo* keepPreview = previewing;
    filtered.clear();

    for (const auto& loop : library.getLoops())
    {
        if (categoryFilter >= 0 && (int) loop.category != categoryFilter) continue;
        if (query.isNotEmpty() && ! (loop.name.toLowerCase().contains (query) || loop.key.toLowerCase() == query
                                     || LoopInfo::categoryName (loop.category).toLowerCase().contains (query)))
            continue;
        filtered.push_back (&loop);
    }

    // The library vector may have been rebuilt: the previewed pointer is stale.
    if (keepPreview != nullptr && std::find (filtered.begin(), filtered.end(), keepPreview) == filtered.end())
        previewing = nullptr;

    list.updateContent();
    list.repaint();

    const auto total = library.getLoops().size();
    statusLabel.setText (library.isScanning() ? "Scanning..."
                         : juce::String (filtered.size()) + " of " + juce::String (total) + " loops    "
                           + juce::String (library.getFolders().size()) + (library.getFolders().size() == 1 ? " folder" : " folders"),
                         juce::dontSendNotification);
}

//==============================================================================
// Layout & painting

void LoopBrowser::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelDark);
    g.setColour (theme::gridStrong);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Sample Library", 12, 6, getWidth() - 24, 20, juce::Justification::centredLeft);
}

bool LoopBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
    return false;
}

void LoopBrowser::resized()
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (24);

    search.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);

    // Category chips in two rows
    auto chipRow = area.removeFromTop (22);
    const int perRow = 4;
    const int chipWidth = chipRow.getWidth() / perRow;
    for (int i = 0; i < categoryButtons.size(); ++i)
    {
        if (i > 0 && i % perRow == 0) { area.removeFromTop (3); chipRow = area.removeFromTop (22); }
        categoryButtons[i]->setBounds (chipRow.removeFromLeft (chipWidth).reduced (1, 0));
    }
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (24);
    addFolderButton.setBounds (bottom.removeFromLeft (80));
    bottom.removeFromLeft (4);
    rescanButton.setBounds (bottom.removeFromLeft (64));
    closeButton.setBounds (bottom.removeFromRight (64));
    area.removeFromBottom (4);
    statusLabel.setBounds (area.removeFromBottom (16));
    area.removeFromBottom (4);

    list.setBounds (area);
}

void LoopBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) filtered.size())) return;
    const auto& loop = *filtered[(size_t) row];
    const bool isPreviewing = previewing == &loop;

    g.setColour (selected || isPreviewing ? theme::panel.brighter (0.1f) : (row % 2 ? theme::background : theme::panelDark));
    g.fillRect (0, 0, width, height);

    // Category colour bar
    static const juce::Colour categoryColours[] = {
        juce::Colour (0xffe67e22), juce::Colour (0xff9b59b6), juce::Colour (0xff3498db), juce::Colour (0xffe74c3c),
        juce::Colour (0xff2ecc71), juce::Colour (0xff1abc9c), juce::Colour (0xff7f8c8d) };
    g.setColour (categoryColours[(size_t) loop.category]);
    g.fillRect (0, 0, 4, height);

    auto r = juce::Rectangle<int> (10, 0, width - 14, height);

    // Play indicator
    if (isPreviewing)
    {
        juce::Path tri;
        tri.addTriangle (0.0f, 0.0f, 10.0f, 5.0f, 0.0f, 10.0f);
        g.setColour (theme::accent);
        g.fillPath (tri, juce::AffineTransform::translation ((float) r.getX(), (float) height / 2.0f - 5.0f));
        r.removeFromLeft (16);
    }

    auto top = r.removeFromTop (height / 2 + 2);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (loop.name, top, juce::Justification::bottomLeft, true);

    juce::String meta = LoopInfo::categoryName (loop.category);
    if (loop.bpm > 0.0)
    {
        meta += "   " + (loop.bpmEstimated ? "~" : juce::String()) + juce::String (juce::roundToInt (loop.bpm)) + " BPM";
        const double bars = loop.getBars();
        if (bars > 0.0)
        {
            const bool whole = std::abs (bars - std::round (bars)) < 1.0e-6;
            meta += "   " + juce::String (bars, whole ? 0 : 1) + (whole && juce::roundToInt (bars) == 1 ? " bar" : " bars");
        }
    }
    else
        meta += "   " + juce::String (loop.lengthSeconds, 1) + " s";
    if (loop.key.isNotEmpty()) meta += "   " + loop.key;

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (meta, r, juce::Justification::topLeft, true);

    // Tempo-conform badge when the loop's tempo differs from the session's
    if (loop.bpm > 0.0 && std::abs (loop.bpm - sessionBpm) > 0.5)
    {
        const auto badge = juce::Rectangle<int> (width - 52, 6, 44, 14);
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillRoundedRectangle (badge.toFloat(), 3.0f);
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("-> " + juce::String (juce::roundToInt (sessionBpm)), badge, juce::Justification::centred);
    }
}

//==============================================================================
// Interaction

void LoopBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, (int) filtered.size())) return;
    const auto* loop = filtered[(size_t) row];

    if (previewing == loop) { stopPreview(); return; }
    previewing = loop;
    list.repaint();
    if (onPreview) onPreview (loop);
}

void LoopBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, (int) filtered.size())) return;
    stopPreview();
    if (onAddAtPlayhead) onAddAtPlayhead (*filtered[(size_t) row]);
}

juce::var LoopBrowser::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.size() == 0) return {};
    const int row = rows[0];
    if (! juce::isPositiveAndBelow (row, (int) filtered.size())) return {};
    return juce::String (dragPrefix) + filtered[(size_t) row]->file.getFullPathName();
}

juce::String LoopBrowser::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) filtered.size())) return {};
    const auto& loop = *filtered[(size_t) row];
    return loop.file.getFullPathName() + "\n" + juce::String (loop.numChannels) + " ch, "
         + juce::String (loop.sampleRate / 1000.0, 1) + " kHz, " + juce::String (loop.lengthSeconds, 2) + " s\n"
         + "Click: audition   Drag: place on a track   Double-click: add at playhead";
}

} // namespace beatmaker::ui
