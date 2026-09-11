#include "TrackArea.h"
#include <dsp/DrumKitFactory.h>
#include <ClipLoop.h>
#include <ClipJoin.h>
#include "../shared/UiProfiler.h"
#include <GroupLogic.h>
#include <Playlists.h>
#include <dsp/Fades.h>
#include <Elastic.h>
#include <Arrangement.h>
#include <map>
#include "../shared/ElasticJob.h"

namespace beatmaker::ui
{

TrackArea::TrackArea (model::Session& s, engine::Transport& t, engine::AudioGraph& g, juce::AudioFormatManager& fm, EditSettings& es)
    : session (s), transport (t), formatManager (fm), graph (g), edit (es)
{
    session.addListener (this);
    setWantsKeyboardFocus (true);
    setTitle ("Edit window");
    setDescription ("Tracks and clips. Up and Down select a track, Left and Right move the playhead by the grid, Shift with arrows extends the selection, Shift+Return selects the track's clips.");
    addTrackButton.setTitle ("Add track");
    addAndMakeVisible (addTrackButton);
    addTrackButton.setTooltip ("Add a track: audio, Drum Machine, any bundled instrument, aux input or VCA master");
    addTrackButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Audio Track");
        juce::PopupMenu kits;
        const auto& kitList = engine::DrumKitFactory::availableKits();
        for (const auto& cat : engine::DrumKitFactory::categories())
        {
            juce::PopupMenu inCategory;
            for (int i = 0; i < (int) kitList.size(); ++i) if (kitList[(size_t) i].category == cat) inCategory.addItem (200 + i, kitList[(size_t) i].name);
            kits.addSubMenu (cat, inCategory);
        }
        kits.addSeparator();
        kits.addItem (199, "Other...");           // the chooser: every kit by category, with descriptions and previews
        kits.addItem (198, "Build Your Own...");  // the kit builder
        menu.addSubMenu ("Drum Machine Track", kits);
        // One instrument per category (the first of each); the rest are in the chooser, so the menu stays short
        juce::PopupMenu instruments;
        const auto& types = engine::Instrument::availableTypes();
        for (const auto& category : engine::Instrument::categories())
            for (int i = 0; i < (int) types.size(); ++i)
                if (category == engine::Instrument::typeCategory (types[(size_t) i])) { instruments.addItem (100 + i, engine::Instrument::typeName (types[(size_t) i])); break; }
        instruments.addSeparator();
        instruments.addItem (99, "Other...");   // the chooser: every instrument by category, with descriptions and presets
        menu.addSubMenu ("Instrument Track", instruments);
        menu.addItem (4, "Aux Input");
        menu.addItem (5, "VCA Master");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addTrackButton),
                            [this] (int result)
                            {
                                using K = model::Track::InstrumentKind;
                                using T = model::Track::Type;
                                if (! onAddTrack) return;
                                const auto& all = engine::Instrument::availableTypes();
                                if (result == 5)      onAddTrack (T::vca, K::none, engine::InstrumentType::none);
                                else if (result == 4) onAddTrack (T::aux, K::none, engine::InstrumentType::none);
                                else if (result == 1) onAddTrack (T::audio, K::none, engine::InstrumentType::none);
                                else if (result == 2) onAddTrack (T::instrument, K::drumMachine, engine::InstrumentType::none);
                                else if (result == 199) { if (onChooseKit) onChooseKit(); }
                                else if (result == 198) { if (onBuildKit) onBuildKit(); }
                                else if (result >= 200 && result < 200 + (int) engine::DrumKitFactory::availableKits().size())
                                {
                                    if (onAddDrumTrack) onAddDrumTrack (engine::DrumKitFactory::availableKits()[(size_t) (result - 200)].name);
                                    else onAddTrack (T::instrument, K::drumMachine, engine::InstrumentType::none);
                                }
                                else if (result == 99) { if (onChooseInstrument) onChooseInstrument(); }
                                else if (result >= 100 && result < 100 + (int) all.size())
                                    onAddTrack (T::instrument, K::synth, all[(size_t) (result - 100)]);
                            });
    };
    rebuildTrackControls();
    addAndMakeVisible (vScroll);
    vScroll.setAutoHide (true);
    vScroll.addListener (this);
    vScroll.setTitle ("Track list scroll");
    addAndMakeVisible (rulerOverlay);
    rulerOverlay.toFront (false);
    startTimerHz (30);
}

int TrackArea::contentHeight() const
{
    int h = 0;
    for (int k = 0; k < session.getNumTracks(); ++k) h += trackHeightFor (k);
    return h + 52;   // room for the + Track button
}

void TrackArea::updateScrollRange()
{
    const int visible = juce::jmax (1, getHeight() - theme::rulerHeight);
    scrollY = juce::jlimit (0, juce::jmax (0, contentHeight() - visible), scrollY);
    vScroll.setRangeLimits (0.0, (double) juce::jmax (contentHeight(), visible), juce::dontSendNotification);
    vScroll.setCurrentRange ((double) scrollY, (double) visible, juce::dontSendNotification);
}

void TrackArea::setScrollY (int pixels)
{
    const int visible = juce::jmax (1, getHeight() - theme::rulerHeight);
    const int clamped = juce::jlimit (0, juce::jmax (0, contentHeight() - visible), pixels);
    if (clamped == scrollY) return;
    scrollY = clamped;
    resized();
    repaint();
}

void TrackArea::scrollBarMoved (juce::ScrollBar*, double newRangeStart) { setScrollY (juce::roundToInt (newRangeStart)); }

void TrackArea::ensureTrackVisible (int i)
{
    if (! juce::isPositiveAndBelow (i, session.getNumTracks())) return;
    const int top = trackTop (i), bottom = top + trackHeightFor (i);
    if (top < theme::rulerHeight) setScrollY (scrollY - (theme::rulerHeight - top));
    else if (bottom > getHeight()) setScrollY (scrollY + (bottom - getHeight()));
}

TrackArea::~TrackArea()
{
    session.removeListener (this);
    for (auto& [_, thumb] : thumbnails)
        thumb->removeChangeListener (this);
}

//==============================================================================
// Geometry

juce::Rectangle<int> TrackArea::getRulerBounds() const
{
    return { theme::trackHeaderWidth, 0, getWidth() - theme::trackHeaderWidth - scrollBarWidth, theme::rulerHeight };
}

bool TrackArea::arePlaylistsShown (const model::Track& t) const { return playlistsShown.count (t.id) > 0; }

int TrackArea::trackHeightFor (int i) const
{
    auto* t = session.getTrack (i);
    if (t == nullptr) return trackHeight;
    return trackHeight + (arePlaylistsShown (*t) ? (int) t->alternates.size() * alternateLaneHeight : 0);
}

int TrackArea::trackTop (int i) const
{
    int y = theme::rulerHeight - scrollY;
    for (int k = 0; k < i; ++k) y += trackHeightFor (k);
    return y;
}

juce::Rectangle<int> TrackArea::getHeaderBounds (int i) const
{
    return { 0, trackTop (i), theme::trackHeaderWidth, trackHeight };
}

juce::Rectangle<int> TrackArea::getLaneBounds (int i) const
{
    return { theme::trackHeaderWidth, trackTop (i), getLaneWidth(), trackHeight };
}

juce::Rectangle<int> TrackArea::getAlternateLaneBounds (int i, int alternate) const
{
    return { 0, trackTop (i) + trackHeight + alternate * alternateLaneHeight, getWidth() - scrollBarWidth, alternateLaneHeight };
}

int TrackArea::trackIndexAtY (int y) const
{
    if (y < theme::rulerHeight || y >= getHeight()) return -1;
    for (int i = 0; i < session.getNumTracks(); ++i)
        if (y >= trackTop (i) && y < trackTop (i) + trackHeightFor (i)) return i;
    return -1;
}

int TrackArea::alternateAtY (int i, int y) const
{
    auto* t = session.getTrack (i);
    if (t == nullptr || ! arePlaylistsShown (*t)) return -1;
    const int rel = y - (trackTop (i) + trackHeight);
    if (rel < 0) return -1;
    const int alt = rel / alternateLaneHeight;
    return alt < (int) t->alternates.size() ? alt : -1;
}

float TrackArea::secondsToX (double seconds) const
{
    return (float) (theme::trackHeaderWidth + (seconds - viewStartSeconds) * pixelsPerSecond);
}

double TrackArea::xToSeconds (float x) const
{
    return juce::jmax (0.0, viewStartSeconds + (x - theme::trackHeaderWidth) / pixelsPerSecond);
}

void TrackArea::ensurePlayheadVisible()
{
    const double pos = transport.getPositionSeconds();
    const double viewWidthSeconds = (getWidth() - theme::trackHeaderWidth) / pixelsPerSecond;

    if (pos < viewStartSeconds || pos > viewStartSeconds + viewWidthSeconds)
        viewStartSeconds = juce::jmax (0.0, pos - viewWidthSeconds * 0.1);
}

//==============================================================================
// Layout & controls

void TrackArea::resized()
{
    updateScrollRange();   // clamp the scroll to the new size before anything is placed
    for (int i = 0; i < (int) trackControls.size(); ++i)
    {
        auto& c = trackControls[(size_t) i];
        auto header = getHeaderBounds (i).reduced (10);
        header.removeFromLeft (14); // colour chip

        auto buttons = header.removeFromBottom (22);
        c.mute->setBounds (buttons.removeFromLeft (28)); buttons.removeFromLeft (4);
        c.solo->setBounds (buttons.removeFromLeft (28)); buttons.removeFromLeft (4);
        if (c.arm)     { c.arm->setBounds (buttons.removeFromLeft (28));     buttons.removeFromLeft (4); }
        if (c.monitor) { c.monitor->setBounds (buttons.removeFromLeft (28)); buttons.removeFromLeft (4); }
        if (c.playlists) c.playlists->setBounds (buttons.removeFromLeft (28));

        header.removeFromBottom (3);
        auto autoRow = header.removeFromBottom (19);
        if (c.autoMode) { c.autoMode->setBounds (autoRow.removeFromLeft (64)); autoRow.removeFromLeft (3); }
        if (c.autoView) c.autoView->setBounds (autoRow);

        header.removeFromBottom (3);
        if (c.input)
            c.input->setBounds (header.removeFromBottom (19));
    }

    const int y = trackTop (session.getNumTracks()) + 12;
    addTrackButton.setBounds (12, y, theme::trackHeaderWidth - 24, 28);
    vScroll.setBounds (getWidth() - scrollBarWidth, theme::rulerHeight, scrollBarWidth, juce::jmax (0, getHeight() - theme::rulerHeight));
    rulerOverlay.setBounds (0, 0, getWidth(), theme::rulerHeight);
    rulerOverlay.toFront (false);
    // Controls that slid under the ruler must not take clicks through it
    for (int i = 0; i < (int) trackControls.size(); ++i)
    {
        const bool shown = trackTop (i) + trackHeightFor (i) > theme::rulerHeight && trackTop (i) < getHeight();
        auto& c = trackControls[(size_t) i];
        for (juce::Component* comp : { (juce::Component*) c.mute.get(), (juce::Component*) c.solo.get(), (juce::Component*) c.arm.get(), (juce::Component*) c.monitor.get(),
                                       (juce::Component*) c.playlists.get(), (juce::Component*) c.autoMode.get(), (juce::Component*) c.autoView.get(), (juce::Component*) c.input.get() })
            if (comp != nullptr) comp->setVisible (shown);
    }
    addTrackButton.setVisible (addTrackButton.getBottom() > theme::rulerHeight);

    // Alternate lane buttons
    for (int i = 0; i < (int) trackControls.size(); ++i)
    {
        auto& c = trackControls[(size_t) i];
        for (int a = 0; a < (int) c.laneMain.size(); ++a)
        {
            auto lane = getAlternateLaneBounds (i, a).withWidth (theme::trackHeaderWidth).reduced (10, 6);
            lane.removeFromTop (16);
            auto row = lane.removeFromTop (20);
            c.laneMain[(size_t) a]->setBounds (row.removeFromLeft (52));
            row.removeFromLeft (4);
            c.laneComp[(size_t) a]->setBounds (row.removeFromLeft (52));
        }
    }
}

void TrackArea::rebuildTrackControls()
{
    trackControls.clear();

    for (int i = 0; i < session.getNumTracks(); ++i)
    {
        const auto& track = session.getTracks()[(size_t) i];
        TrackControls c;
        c.mute = std::make_unique<juce::TextButton> ("M");
        c.solo = std::make_unique<juce::TextButton> ("S");
        c.solo->setTooltip ("Solo: hear only soloed tracks");
        c.mute->setTitle (track.name + " mute"); c.solo->setTitle (track.name + " solo");

        c.mute->setClickingTogglesState (true);
        c.solo->setClickingTogglesState (true);
        c.mute->setToggleState (track.mute, juce::dontSendNotification);
        c.solo->setToggleState (track.solo, juce::dontSendNotification);
        c.mute->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe67e22));
        c.solo->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff1c40f));
        c.mute->setTooltip ("Mute (Solo on another track also silences this one)");
        c.solo->setTooltip ("Solo");

        c.mute->onClick = [this, i, b = c.mute.get()] { if (onMuteChanged) onMuteChanged (i, b->getToggleState()); };
        c.solo->onClick = [this, i, b = c.solo.get()] { if (onSoloChanged) onSoloChanged (i, b->getToggleState()); };

        addAndMakeVisible (*c.mute);
        addAndMakeVisible (*c.solo);

        if (track.isAudio())
        {
            c.arm = std::make_unique<juce::TextButton> ("R");
            c.arm->setClickingTogglesState (true);
            c.arm->setToggleState (track.armed, juce::dontSendNotification);
            // TrackPunch: armed-but-waiting tracks show amber, punched-in tracks bright red.
            const bool trackPunch = session.getRecordSettings().mode == model::RecordMode::trackPunch;
            c.arm->setColour (juce::TextButton::buttonOnColourId, trackPunch ? (track.punched ? theme::record.brighter (0.4f) : juce::Colour (0xfff0a030)) : theme::record);
            c.arm->setTooltip (trackPunch ? "Record arm (TrackPunch: while rolling, click to punch this track in or out)" : "Record arm");
            c.arm->onClick = [this, i, b = c.arm.get()] { if (onArmChanged) onArmChanged (i, b->getToggleState()); };
            addAndMakeVisible (*c.arm);

            c.monitor = std::make_unique<juce::TextButton> ("I");
            c.monitor->setTooltip ("Input monitoring: hear the input while stopped or recording");
            c.monitor->setClickingTogglesState (true);
            c.monitor->setToggleState (track.monitor, juce::dontSendNotification);
            c.monitor->setColour (juce::TextButton::buttonOnColourId, theme::play.darker (0.2f));
            c.monitor->setTooltip ("Input monitor: hear this track's input (watch for feedback with speakers)");
            c.monitor->onClick = [this, i, b = c.monitor.get()] { if (onMonitorChanged) onMonitorChanged (i, b->getToggleState()); };
            addAndMakeVisible (*c.monitor);

            c.input = std::make_unique<juce::ComboBox>();
            c.input->setTooltip ("Record input (device channels or I/O Setup paths)");
            c.input->setTitle (track.name + " input");
            c.input->setTooltip ("Input path (I/O Setup)");
            const auto& io = session.getIO();
            if (io.inputs.empty())
            {
                c.input->addItem ("No inputs", 1);
                c.input->setSelectedId (1, juce::dontSendNotification);
                c.input->setEnabled (false);
            }
            else
            {
                for (int pathIndex = 0; pathIndex < (int) io.inputs.size(); ++pathIndex)
                    c.input->addItem (io.inputs[(size_t) pathIndex].name, 1000 + pathIndex);

                int selected = -1;
                if (track.inputPath >= 0) selected = track.inputPath;
                else
                    for (int pathIndex = 0; pathIndex < (int) io.inputs.size(); ++pathIndex)
                        if (io.inputs[(size_t) pathIndex].firstChannel == track.firstInput && io.inputs[(size_t) pathIndex].numChannels == track.numInputs)
                        { selected = pathIndex; break; }
                if (selected >= 0) c.input->setSelectedId (1000 + selected, juce::dontSendNotification);

                c.input->onChange = [this, i, box = c.input.get()]
                {
                    const int id = box->getSelectedId();
                    if (id >= 1000 && onInputPathChanged) onInputPathChanged (i, id - 1000);
                };
            }
            addAndMakeVisible (*c.input);

            c.playlists = std::make_unique<juce::TextButton> ("P");
            c.playlists->setTooltip ("Playlists: alternate takes for comping");
            c.playlists->setTooltip ("Playlists: new, duplicate, switch, show take lanes");
            c.playlists->setColour (juce::TextButton::buttonColourId, arePlaylistsShown (track) ? theme::accent.darker (0.5f) : theme::panel);
            c.playlists->onClick = [this, i] { showPlaylistMenu (i); };
            addAndMakeVisible (*c.playlists);

            if (arePlaylistsShown (track))
                for (int a = 0; a < (int) track.alternates.size(); ++a)
                {
                    auto mainBtn = std::make_unique<juce::TextButton> ("Main");
                    mainBtn->setTooltip ("Make this take the main playlist");
                    mainBtn->onClick = [this, i, a] { session.execute (std::make_unique<model::SwitchPlaylistCommand> (i, a)); };
                    addAndMakeVisible (*mainBtn);
                    c.laneMain.push_back (std::move (mainBtn));

                    auto compBtn = std::make_unique<juce::TextButton> ("Comp");
                    compBtn->setTooltip ("Copy the time selection on this lane into the main playlist (Ctrl+Alt+V)");
                    compBtn->setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.55f));
                    compBtn->onClick = [this, i, a]
                    {
                        if (timeSelection.isValid()) { timeSelection.playlistTrack = i; timeSelection.playlistIndex = a; compSelectionToMain(); }
                    };
                    addAndMakeVisible (*compBtn);
                    c.laneComp.push_back (std::move (compBtn));
                }
        }

        // Automation mode + which lane the track shows
        c.autoMode = std::make_unique<juce::ComboBox>();
        c.autoMode->setTooltip ("Automation mode");
        int id = 1;
        for (auto m : { model::AutomationMode::off, model::AutomationMode::read, model::AutomationMode::touch, model::AutomationMode::latch,
                        model::AutomationMode::write, model::AutomationMode::trim })
            c.autoMode->addItem (model::automationModeName (m), id++);
        c.autoMode->setSelectedId ((int) track.automationMode + 1, juce::dontSendNotification);
        c.autoMode->onChange = [this, i, box = c.autoMode.get()]
        {
            if (box->getSelectedId() > 0 && onAutomationModeChanged)
                onAutomationModeChanged (i, (model::AutomationMode) (box->getSelectedId() - 1));
        };
        addAndMakeVisible (*c.autoMode);

        c.autoView = std::make_unique<juce::ComboBox>();
        c.autoView->setTooltip ("What the lane shows: clips, clip gain, or an automation parameter");
        c.autoView->setTitle (track.name + " lane view"); c.autoMode->setTitle (track.name + " automation mode");
        c.autoView->setTooltip ("Track view: clips, or an automation lane");
        c.autoView->addItem ("Clips", 1);
        if (track.isAudio()) c.autoView->addItem ("Clip Gain", 100);
        c.autoView->addItem ("Volume", 2);
        c.autoView->addItem ("Pan", 3);
        c.autoView->addItem ("Mute", 4);
        for (int sIdx = 0; sIdx < model::Track::numSendSlots; ++sIdx)
            c.autoView->addItem (engine::ParamId::send (sIdx).getName(), 5 + sIdx);
        const auto shown = shownLane (track);
        int viewId = showsClipGain (track) ? 100 : 1;
        if (shown)
        {
            switch (shown->type)
            {
                case engine::ParamId::Type::volume:    viewId = 2; break;
                case engine::ParamId::Type::pan:       viewId = 3; break;
                case engine::ParamId::Type::mute:      viewId = 4; break;
                case engine::ParamId::Type::sendLevel: viewId = 5 + shown->index; break;
                case engine::ParamId::Type::insertParam: viewId = 1; break;
            }
        }
        c.autoView->setSelectedId (viewId, juce::dontSendNotification);
        c.autoView->onChange = [this, trackId = track.id, box = c.autoView.get()]
        {
            const int sel = box->getSelectedId();
            clipGainView.erase (trackId);
            if (sel == 100) { automationView.erase (trackId); clipGainView.insert (trackId); }
            else if (sel <= 1) automationView.erase (trackId);
            else if (sel == 2) automationView[trackId] = engine::ParamId::volume();
            else if (sel == 3) automationView[trackId] = engine::ParamId::pan();
            else if (sel == 4) automationView[trackId] = engine::ParamId::mute();
            else automationView[trackId] = engine::ParamId::send (sel - 5);
            repaint();
        };
        addAndMakeVisible (*c.autoView);

        trackControls.push_back (std::move (c));
    }

    resized();
}

void TrackArea::setInputChannelNames (const juce::StringArray& names)
{
    inputNames = names;
    rebuildTrackControls();
}

juce::AudioThumbnail& TrackArea::createLiveThumbnail (int trackId)
{
    auto thumb = std::make_unique<juce::AudioThumbnail> (256, formatManager, thumbnailCache);
    auto& ref = *thumb;
    liveThumbnails[trackId] = std::move (thumb);
    return ref;
}

void TrackArea::clearLiveThumbnails()
{
    liveThumbnails.clear();
    repaint();
}

void TrackArea::sessionChanged (model::Session&)
{
    rebuildTrackControls();
    updateScrollRange();
    if (selectedTrack >= session.getNumTracks())
        setSelectedTrack (session.getNumTracks() - 1);

    std::erase_if (selectedClips, [this] (const model::ClipRef& r) { return ! model::ClipEdits::timing (session, r).has_value(); });
    repaint();
}

void TrackArea::setSelectedTrack (int index)
{
    index = juce::isPositiveAndBelow (index, session.getNumTracks()) ? index : -1;
    if (index == selectedTrack) return;
    selectedTrack = index;
    ensureTrackVisible (index);
    repaint();
    if (onSelectionChanged) onSelectionChanged (selectedTrack);
}

juce::AudioThumbnail& TrackArea::thumbnailFor (const model::AudioClip& clip)
{
    const auto key = clip.sourceFile.getFullPathName();
    auto it = thumbnails.find (key);

    if (it == thumbnails.end())
    {
        auto thumb = std::make_unique<juce::AudioThumbnail> (256, formatManager, thumbnailCache);
        thumb->setSource (new juce::FileInputSource (clip.sourceFile));
        thumb->addChangeListener (this);
        it = thumbnails.emplace (key, std::move (thumb)).first;
    }

    return *it->second;
}

//==============================================================================
// Painting

void TrackArea::paint (juce::Graphics& g)
{
    ui::UiProfiler::Scope profile ("paint TrackArea");
    paintBody (g);
    if (hasKeyboardFocus (false))
    {
        g.setColour (theme::accent.withAlpha (0.9f));
        g.drawRect (getLocalBounds(), 2);
    }
}

void TrackArea::paintBody (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const auto& tracks = session.getTracks();

    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        paintLane (g, tracks[(size_t) i], getLaneBounds (i));
        paintHeader (g, tracks[(size_t) i], i, getHeaderBounds (i));
        paintAlternateLanes (g, tracks[(size_t) i], i);
    }

    paintRuler (g, getRulerBounds());

    // Header column background below the last track
    g.setColour (theme::panelDark);
    g.fillRect (0, trackTop ((int) tracks.size()), theme::trackHeaderWidth, getHeight());
    g.setColour (theme::gridStrong);
    g.drawVerticalLine (theme::trackHeaderWidth - 1, 0.0f, (float) getHeight());

    paintEditOverlays (g);

    // Playhead
    const float px = secondsToX (transport.getPositionSeconds());
    if (px >= theme::trackHeaderWidth && px <= getWidth())
    {
        g.setColour (theme::playhead);
        g.drawLine (px, 0.0f, px, (float) getHeight(), 1.5f);
        juce::Path head;
        head.addTriangle (px - 6.0f, 0.0f, px + 6.0f, 0.0f, px, 8.0f);
        g.fillPath (head);
    }

    if (tracks.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("Drop an audio file here, or use Open Audio File...", getLaneBounds (0).withHeight (getHeight()),
                    juce::Justification::centred);
    }

    if (dragHover)
    {
        g.setColour (theme::accent.withAlpha (0.12f));
        g.fillRect (getLocalBounds());
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds(), 2);
    }
}

void TrackArea::paintRuler (juce::Graphics& g, juce::Rectangle<int> fullRuler)
{
    g.setColour (theme::panel);
    g.fillRect (fullRuler);
    paintMarkerStrip (g, fullRuler.removeFromTop (theme::markerStripHeight));
    const auto r = fullRuler;

    const double secondsPerBar = transport.beatsToSeconds (transport.getBeatsPerBar());
    const double secondsPerBeat = transport.beatsToSeconds (1.0);
    const double viewEnd = xToSeconds ((float) getWidth());
    const bool showBeats = secondsPerBeat * pixelsPerSecond > 18.0;

    g.setFont (juce::FontOptions (12.0f));
    const int firstBar = (int) (viewStartSeconds / secondsPerBar);

    for (int bar = firstBar; ; ++bar)
    {
        const double barStart = bar * secondsPerBar;
        if (barStart > viewEnd) break;

        const float x = secondsToX (barStart);
        g.setColour (theme::gridStrong);
        g.drawVerticalLine ((int) x, (float) r.getY(), (float) r.getBottom());
        g.setColour (theme::text);
        g.drawText (juce::String (bar + 1), (int) x + 4, r.getY(), 40, r.getHeight(), juce::Justification::centredLeft);

        if (showBeats)
            for (int beat = 1; beat < transport.getBeatsPerBar(); ++beat)
            {
                const float bx = secondsToX (barStart + beat * secondsPerBeat);
                g.setColour (theme::grid);
                g.drawVerticalLine ((int) bx, (float) r.getCentreY(), (float) r.getBottom());
            }
    }

    if (transport.hasValidLoop())
    {
        const float x1 = juce::jmax ((float) r.getX(), secondsToX ((double) transport.getLoopStart() / transport.getSampleRate()));
        const float x2 = juce::jmin ((float) r.getRight(), secondsToX ((double) transport.getLoopEnd() / transport.getSampleRate()));
        if (x2 > x1)
        {
            g.setColour (theme::accent.withAlpha (0.85f));
            g.fillRoundedRectangle (x1, (float) r.getY() + 2.0f, x2 - x1, 5.0f, 2.0f);
        }
    }

    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX(), (float) r.getRight());
}

void TrackArea::paintHeader (juce::Graphics& g, const model::Track& track, int index, juce::Rectangle<int> r)
{
    const bool selected = index == selectedTrack;
    g.setColour (selected ? theme::panel.brighter (0.08f) : theme::panelDark);
    g.fillRect (r);
    if (selected)
    {
        g.setColour (theme::accent);
        g.fillRect (r.removeFromLeft (3));
    }

    // Colour chip
    auto chip = r.reduced (10).removeFromLeft (6).toFloat();
    g.setColour (track.colour);
    g.fillRoundedRectangle (chip, 3.0f);

    auto content = r.reduced (10);
    content.removeFromLeft (14);

    g.setColour (theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (track.name, content.removeFromTop (20), juce::Justification::centredLeft, true);

    if (track.isFrozen())
    {
        g.setColour (juce::Colour (0xff7ec8e3));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("FROZEN", r.getRight() - 58, r.getY() + 6, 50, 14, juce::Justification::centredRight);
    }
    g.setColour (track.armed ? theme::record.brighter (0.2f) : theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    juce::String badges;
    for (const auto* grp : model::GroupLogic::groupsOf (session, track.id)) badges += (grp->active ? " [" : " (") + grp->badge() + (grp->active ? "]" : ")");
    if (track.isAudio() && ! track.alternates.empty())
        badges += "   " + (track.mainPlaylistName.isNotEmpty() ? track.mainPlaylistName : model::defaultPlaylistName (track, 1));
    g.drawText (juce::String (index + 1) + (track.isDrumMachine() ? "  Drum Machine" : track.isSynth() ? "  " + juce::String (engine::Instrument::typeName (track.instrumentType())) : track.isVca() ? "  VCA Master"
                                            : track.isAux() ? "  Aux  <- " + (track.inputBus >= 0 ? session.busName (track.inputBus) : juce::String ("no input"))
                                            : track.armed ? "  Audio  REC" : "  Audio") + badges,
                content.removeFromTop (14), juce::Justification::centredLeft);

    g.setColour (theme::grid);
    g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX(), (float) r.getRight());
}

void TrackArea::paintLane (juce::Graphics& g, const model::Track& track, juce::Rectangle<int> r)
{
    // Bar grid, plus the edit grid when it is coarse enough to read
    const double secondsPerBar = transport.beatsToSeconds (transport.getBeatsPerBar());
    const double viewEnd = xToSeconds ((float) getWidth());
    const double gridSec = gridSeconds();
    if (edit.mode == EditSettings::Mode::grid && gridSec * pixelsPerSecond >= 8.0)
    {
        g.setColour (theme::grid.withAlpha (0.45f));
        for (double t = std::floor (viewStartSeconds / gridSec) * gridSec; t <= viewEnd; t += gridSec)
            g.drawVerticalLine ((int) secondsToX (t), (float) r.getY(), (float) r.getBottom());
    }
    for (int bar = (int) (viewStartSeconds / secondsPerBar); bar * secondsPerBar <= viewEnd; ++bar)
    {
        g.setColour (theme::grid);
        g.drawVerticalLine ((int) secondsToX (bar * secondsPerBar), (float) r.getY(), (float) r.getBottom());
    }
    g.setColour (theme::grid);
    g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX(), (float) r.getRight());

    if (track.isFrozen())
    {
        // The rendered audio as one read-only clip
        model::AudioClip shown; shown.name = track.name + " (frozen)"; shown.audio = track.freeze.audio; shown.sampleRate = track.freeze.sampleRate; shown.length = track.freeze.audio->getNumSamples();
        paintAudioClip (g, track, shown, r);
        g.setColour (juce::Colour (0xff7ec8e3).withAlpha (0.18f));
        g.fillRect (r);
        return;
    }
    for (const auto& clip : track.clips)        paintAudioClip (g, track, clip, r);
    for (const auto& clip : track.patternClips) paintPatternClip (g, track, clip, r);
    for (const auto& clip : track.midiClips)    paintMidiClip (g, track, clip, r);
    paintLiveRecording (g, track, r);

    for (int i = 0; i < session.getNumTracks(); ++i)
        if (&session.getTracks()[(size_t) i] == &track) { paintAutomationLane (g, track, i, r); break; }
}

//==============================================================================
// Playlists

void TrackArea::setPlaylistsShown (int trackIndex, bool shown)
{
    if (auto* t = session.getTrack (trackIndex))
    {
        if (shown) playlistsShown.insert (t->id); else playlistsShown.erase (t->id);
        rebuildTrackControls();
        repaint();
    }
}

void TrackArea::paintAlternateLanes (juce::Graphics& g, const model::Track& track, int trackIndex)
{
    if (! arePlaylistsShown (track)) return;
    const double sr = transport.getSampleRate();

    for (int a = 0; a < (int) track.alternates.size(); ++a)
    {
        const auto& alt = track.alternates[(size_t) a];
        auto lane = getAlternateLaneBounds (trackIndex, a);
        auto header = lane.removeFromLeft (theme::trackHeaderWidth);

        g.setColour (theme::panelDark.darker (0.15f));
        g.fillRect (header);
        g.setColour (theme::background.darker (0.2f));
        g.fillRect (lane);

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (alt.name, header.reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft, true);

        for (const auto& clip : alt.clips)
        {
            auto r = clipRectFor (clip.getStartSeconds(), clip.getEndSeconds(), lane);
            if (r.getRight() < lane.getX() || r.getX() > lane.getRight()) continue;
            g.setColour (track.colour.withMultipliedBrightness (0.5f).withAlpha (0.8f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (track.colour.contrasting (0.8f).withAlpha (0.7f));
            thumbnailFor (clip).drawChannels (g, r.reduced (1.0f).withTrimmedTop (2.0f).toNearestInt(),
                                              (double) clip.sourceOffset / clip.sampleRate,
                                              (double) (clip.sourceOffset + clip.length) / clip.sampleRate,
                                              juce::jlimit (0.05f, 4.0f, clip.gain));
            g.setColour (track.colour.brighter (0.2f));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
        }

        // Selection made on this lane
        if (timeSelection.isValid() && timeSelection.playlistTrack == trackIndex && timeSelection.playlistIndex == a)
        {
            const float x1 = secondsToX (timeSelection.start), x2 = secondsToX (timeSelection.end);
            g.setColour (theme::accent.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (x1, (float) lane.getY(), x2 - x1, (float) lane.getHeight()));
            g.setColour (theme::accent);
            g.drawRect (juce::Rectangle<float> (x1, (float) lane.getY(), x2 - x1, (float) lane.getHeight()), 1.0f);
        }

        g.setColour (theme::grid);
        g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());
        juce::ignoreUnused (sr);
    }
}

void TrackArea::showPlaylistMenu (int trackIndex)
{
    auto* t = session.getTrack (trackIndex);
    if (t == nullptr) return;

    juce::PopupMenu menu;
    menu.addItem (1, "New Playlist");
    menu.addItem (2, "Duplicate Playlist");
    menu.addItem (3, arePlaylistsShown (*t) ? "Hide Take Lanes" : "Show Take Lanes", ! t->alternates.empty() || arePlaylistsShown (*t));
    if (! t->alternates.empty())
    {
        menu.addSeparator();
        juce::PopupMenu switchMenu, deleteMenu;
        for (int a = 0; a < (int) t->alternates.size(); ++a)
        {
            switchMenu.addItem (100 + a, t->alternates[(size_t) a].name);
            deleteMenu.addItem (200 + a, t->alternates[(size_t) a].name);
        }
        menu.addSubMenu ("Switch To", switchMenu);
        menu.addSubMenu ("Delete Playlist", deleteMenu);
    }

    auto* button = trackControls[(size_t) trackIndex].playlists.get();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button), [this, trackIndex] (int result)
    {
        if (result == 1)      session.execute (std::make_unique<model::NewPlaylistCommand> (trackIndex));
        else if (result == 2) session.execute (std::make_unique<model::DuplicatePlaylistCommand> (trackIndex));
        else if (result == 3) { if (auto* tr = session.getTrack (trackIndex)) setPlaylistsShown (trackIndex, ! arePlaylistsShown (*tr)); }
        else if (result >= 200) session.execute (std::make_unique<model::DeletePlaylistCommand> (trackIndex, result - 200));
        else if (result >= 100) session.execute (std::make_unique<model::SwitchPlaylistCommand> (trackIndex, result - 100));
    });
}

void TrackArea::compSelectionToMain()
{
    if (! timeSelection.isValid() || ! timeSelection.isOnAlternate()) return;
    auto* t = session.getTrack (timeSelection.playlistTrack);
    if (t == nullptr || ! juce::isPositiveAndBelow (timeSelection.playlistIndex, (int) t->alternates.size())) return;

    const auto s0 = toSamples (timeSelection.start), s1 = toSamples (timeSelection.end);
    auto comped = model::Playlists::comp (t->clips, t->alternates[(size_t) timeSelection.playlistIndex].clips, s0, s1);
    session.execute (std::make_unique<model::ReplaceMainClipsCommand> (timeSelection.playlistTrack, std::move (comped),
                                                                       "Comp from " + t->alternates[(size_t) timeSelection.playlistIndex].name));
}

//==============================================================================
// Automation lanes

void TrackArea::showAutomationLane (int trackIndex, const engine::ParamId& param)
{
    if (auto* t = session.getTrack (trackIndex))
    {
        automationView[t->id] = param;
        rebuildTrackControls();
        repaint();
    }
}

void TrackArea::showClipGainView (int trackIndex)
{
    if (auto* t = session.getTrack (trackIndex))
    {
        automationView.erase (t->id);
        clipGainView.insert (t->id);
        rebuildTrackControls();
        repaint();
    }
}

std::optional<engine::ParamId> TrackArea::shownLane (const model::Track& track) const
{
    auto it = automationView.find (track.id);
    return it != automationView.end() ? std::optional<engine::ParamId> (it->second) : std::nullopt;
}

float TrackArea::valueToY (const engine::ParamId& p, float value, juce::Rectangle<int> lane) const
{
    const float top = (float) lane.getY() + 6.0f, bottom = (float) lane.getBottom() - 6.0f;
    float norm = 0.0f;
    switch (p.type)
    {
        case engine::ParamId::Type::volume:
        case engine::ParamId::Type::sendLevel:
            norm = (juce::Decibels::gainToDecibels (value, -60.0f) + 60.0f) / 66.0f; break;   // -60..+6 dB
        case engine::ParamId::Type::pan:  norm = (value + 1.0f) * 0.5f; break;
        case engine::ParamId::Type::mute: norm = value >= 0.5f ? 1.0f : 0.0f; break;
        case engine::ParamId::Type::insertParam: norm = value; break;
    }
    return bottom - juce::jlimit (0.0f, 1.0f, norm) * (bottom - top);
}

float TrackArea::yToValue (const engine::ParamId& p, int y, juce::Rectangle<int> lane) const
{
    const float top = (float) lane.getY() + 6.0f, bottom = (float) lane.getBottom() - 6.0f;
    const float norm = juce::jlimit (0.0f, 1.0f, (bottom - (float) y) / (bottom - top));
    switch (p.type)
    {
        case engine::ParamId::Type::volume:
        case engine::ParamId::Type::sendLevel: return norm <= 0.001f ? 0.0f : juce::Decibels::decibelsToGain (norm * 66.0f - 60.0f);
        case engine::ParamId::Type::pan:       return norm * 2.0f - 1.0f;
        case engine::ParamId::Type::mute:      return norm >= 0.5f ? 1.0f : 0.0f;
        case engine::ParamId::Type::insertParam: return norm;
    }
    return norm;
}

void TrackArea::paintAutomationLane (juce::Graphics& g, const model::Track& track, int trackIndex, juce::Rectangle<int> r)
{
    const auto shown = shownLane (track);
    if (! shown) return;

    // Dim the clips beneath
    g.setColour (theme::background.withAlpha (0.55f));
    g.fillRect (r);

    const auto* lane = track.laneFor (*shown);
    const float fallback = [&]
    {
        switch (shown->type)
        {
            case engine::ParamId::Type::volume: return track.gain;
            case engine::ParamId::Type::pan: return track.pan;
            case engine::ParamId::Type::mute: return track.mute ? 1.0f : 0.0f;
            case engine::ParamId::Type::sendLevel: return juce::isPositiveAndBelow (shown->index, model::Track::numSendSlots) ? track.sends[(size_t) shown->index].gain : 0.0f;
            case engine::ParamId::Type::insertParam: return 0.0f;
        }
        return 0.0f;
    }();

    const auto colour = track.automationMode == model::AutomationMode::off ? theme::textDim : theme::accent;
    const double viewEnd = xToSeconds ((float) getWidth());
    const double sr = transport.getSampleRate();

    // Curve
    juce::Path path;
    const int steps = juce::jmax (2, r.getWidth() / 3);
    for (int i = 0; i <= steps; ++i)
    {
        const double seconds = viewStartSeconds + (viewEnd - viewStartSeconds) * i / steps;
        float v = fallback;
        if (lane != nullptr && ! lane->isEmpty()) v = lane->valueAt ((juce::int64) std::llround (seconds * sr), fallback);
        const float x = secondsToX (seconds), y = valueToY (*shown, v, r);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (colour.withAlpha (lane != nullptr && ! lane->isEmpty() ? 0.95f : 0.45f));
    g.strokePath (path, juce::PathStrokeType (lane != nullptr && ! lane->isEmpty() ? 1.6f : 1.0f));

    // Points
    if (lane != nullptr)
        for (int i = 0; i < (int) lane->points.size(); ++i)
        {
            const auto& p = lane->points[(size_t) i];
            const float x = secondsToX ((double) p.time / sr), y = valueToY (*shown, p.value, r);
            if (x < r.getX() - 4 || x > r.getRight() + 4) continue;
            const bool dragging = drag == Drag::automationPoint && dragClip.track == trackIndex && dragPointIndex == i;
            g.setColour (dragging ? theme::text : colour);
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        }

    // Ghost point while dragging
    if (drag == Drag::automationPoint && dragClip.track == trackIndex && dragMoved)
    {
        const float x = secondsToX ((double) ghostPoint.time / sr), y = valueToY (*shown, ghostPoint.value, r);
        g.setColour (theme::text);
        g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.5f);
        juce::String text;
        switch (shown->type)
        {
            case engine::ParamId::Type::volume:
            case engine::ParamId::Type::sendLevel: text = juce::String (juce::Decibels::gainToDecibels (ghostPoint.value, -60.0f), 1) + " dB"; break;
            case engine::ParamId::Type::pan: { const int pc = juce::roundToInt (ghostPoint.value * 100.0f); text = pc == 0 ? "C" : pc < 0 ? "L" + juce::String (-pc) : "R" + juce::String (pc); break; }
            case engine::ParamId::Type::mute: text = ghostPoint.value >= 0.5f ? "Muted" : "Unmuted"; break;
            case engine::ParamId::Type::insertParam: text = juce::String (ghostPoint.value, 2); break;
        }
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, (int) x + 8, (int) y - 8, 90, 16, juce::Justification::centredLeft);
    }

    // Label
    g.setColour (colour);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (shown->getName() + "  (" + model::automationModeName (track.automationMode) + ")",
                r.getRight() - 206, r.getY() + 2, 200, 14, juce::Justification::centredRight);
}

int TrackArea::automationPointAt (int trackIndex, juce::Point<int> pt) const
{
    auto* track = session.getTrack (trackIndex);
    if (track == nullptr) return -1;
    const auto shown = shownLane (*track);
    if (! shown) return -1;
    const auto* lane = track->laneFor (*shown);
    if (lane == nullptr) return -1;
    const auto r = getLaneBounds (trackIndex);
    const double sr = transport.getSampleRate();
    for (int i = 0; i < (int) lane->points.size(); ++i)
    {
        const auto& p = lane->points[(size_t) i];
        const float x = secondsToX ((double) p.time / sr), y = valueToY (*shown, p.value, r);
        if (std::abs (x - pt.x) <= 6.0f && std::abs (y - pt.y) <= 6.0f) return i;
    }
    return -1;
}

void TrackArea::commitAutomationDrag()
{
    auto* track = session.getTrack (dragClip.track);
    if (track == nullptr) return;
    const auto shown = shownLane (*track);
    if (! shown) return;

    auto lane = std::make_shared<engine::AutomationLane>();
    lane->param = *shown;
    if (const auto* existing = track->laneFor (*shown)) lane->points = existing->points;
    if (juce::isPositiveAndBelow (dragPointIndex, (int) lane->points.size()))
        lane->points[(size_t) dragPointIndex] = ghostPoint;
    lane->sortPoints();
    session.execute (std::make_unique<model::ReplaceAutomationLaneCommand> (dragClip.track, lane, "Move Breakpoint"));
}

void TrackArea::paintMidiClip (juce::Graphics& g, const model::Track& track, const model::MidiClip& clip, juce::Rectangle<int> r)
{
    auto clipRect = clipRectFor (clip.getStartSeconds(), clip.getEndSeconds(), r);
    if (clipRect.getRight() < r.getX() || clipRect.getX() > r.getRight() || clip.sequence == nullptr) return;

    g.setColour (track.colour.withMultipliedBrightness (track.mute ? 0.35f : 0.55f));
    g.fillRoundedRectangle (clipRect, 4.0f);

    const auto& seq = *clip.sequence;
    if (! seq.notes.empty() && seq.lengthBeats > 0.0)
    {
        int lo = 127, hi = 0;
        for (const auto& n : seq.notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
        lo = juce::jmax (0, lo - 2); hi = juce::jmin (127, hi + 2);

        auto area = clipRect.reduced (2.0f).withTrimmedTop (17.0f);
        const float rowH = area.getHeight() / (float) (hi - lo + 1);
        const double periodSeconds = transport.beatsToSeconds (seq.lengthBeats);
        const float periodWidth = (float) (periodSeconds * pixelsPerSecond);
        const int iterations = (int) std::ceil (clip.getLengthSeconds() / periodSeconds);
        const int shownIterations = clip.loop ? iterations : juce::jmin (iterations, 1);

        g.setColour (track.colour.contrasting (0.9f).withAlpha (0.9f));
        for (int k = 0; k < shownIterations; ++k)
        {
            const float x0 = area.getX() + k * periodWidth;
            if (x0 > r.getRight()) break;
            for (const auto& n : seq.notes)
            {
                const float x = x0 + (float) (n.startBeat / seq.lengthBeats) * periodWidth;
                const float w = juce::jmax (1.5f, (float) (n.lengthBeats / seq.lengthBeats) * periodWidth - 1.0f);
                if (x + w > clipRect.getRight() - 2.0f) continue;   // clip trims the last iteration
                g.fillRect (x, area.getBottom() - (n.pitch - lo + 1) * rowH + 0.5f, w, juce::jmax (1.0f, rowH - 1.0f));
            }
        }
    }

    paintClipFrame (g, clipRect, track, clip.name);
}

void TrackArea::paintLiveRecording (juce::Graphics& g, const model::Track& track, juce::Rectangle<int> r)
{
    auto it = liveThumbnails.find (track.id);
    if (it == liveThumbnails.end() || ! getRecordStartSeconds) return;

    const double start = getRecordStartSeconds();
    const double end = transport.getPositionSeconds();
    if (start < 0.0 || end <= start) return;

    auto clipRect = clipRectFor (start, end, r);
    if (clipRect.getRight() < r.getX() || clipRect.getX() > r.getRight()) return;

    g.setColour (theme::record.withAlpha (0.55f));
    g.fillRoundedRectangle (clipRect, 4.0f);

    auto& thumb = *it->second;
    if (thumb.getTotalLength() > 0.0)
    {
        g.setColour (theme::text.withAlpha (0.9f));
        thumb.drawChannels (g, clipRect.reduced (1.0f).withTrimmedTop (16.0f).toNearestInt(), 0.0, thumb.getTotalLength(), 1.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (clipRect.withHeight (16.0f), 4.0f);
    g.setColour (juce::Colours::white);   // the name bar is always darkened, whatever the theme
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("Recording...", clipRect.withHeight (16.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
    g.setColour (theme::record.brighter (0.4f));
    g.drawRoundedRectangle (clipRect, 4.0f, 1.0f);
}

juce::Rectangle<float> TrackArea::clipRectFor (double startSeconds, double endSeconds, juce::Rectangle<int> r) const
{
    const float x1 = secondsToX (startSeconds);
    const float x2 = secondsToX (endSeconds);
    return { x1, (float) r.getY() + 4.0f, x2 - x1, (float) r.getHeight() - 9.0f };
}

void TrackArea::paintClipFrame (juce::Graphics& g, juce::Rectangle<float> clipRect, const model::Track& track, const juce::String& name)
{
    // Selected clips get a bright frame (drawn after the body below).
    bool selected = false;
    for (const auto& ref : selectedClips)
        if (rectForClip (ref).toNearestInt() == clipRect.toNearestInt()) { selected = true; break; }

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (clipRect.withHeight (16.0f), 4.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (name, clipRect.withHeight (16.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
    g.setColour (selected ? theme::text : track.colour.brighter (0.3f));
    g.drawRoundedRectangle (clipRect, 4.0f, selected ? 2.0f : 1.0f);
}

void TrackArea::paintAudioClip (juce::Graphics& g, const model::Track& track, const model::AudioClip& clip, juce::Rectangle<int> r)
{
    auto clipRect = clipRectFor (clip.getStartSeconds(), clip.getEndSeconds(), r);
    if (clipRect.getRight() < r.getX() || clipRect.getX() > r.getRight()) return;

    g.setColour (track.colour.withMultipliedBrightness (track.mute ? 0.45f : 0.75f));
    g.fillRoundedRectangle (clipRect, 4.0f);

    auto waveArea = clipRect.reduced (1.0f).withTrimmedTop (16.0f);
    g.setColour (track.colour.contrasting (0.9f).withAlpha (0.85f));
    if (pixelsPerSecond / clip.sampleRate >= 0.25 || clip.audioModified)
        paintSamples (g, clip, waveArea);
    else
        thumbnailFor (clip).drawChannels (g, waveArea.toNearestInt(),
                                          (double) clip.sourceOffset / clip.sampleRate,
                                          (double) (clip.sourceOffset + clip.length) / clip.sampleRate,
                                          juce::jlimit (0.05f, 4.0f, clip.gain));

    // Fade curves: shaded region above the curve, like Pro Tools.
    auto drawFade = [&] (juce::int64 samples, engine::FadeShape shape, bool in)
    {
        if (samples <= 0) return;
        const float w = juce::jmin (waveArea.getWidth(), (float) (samples / clip.sampleRate * pixelsPerSecond));
        auto fr = in ? waveArea.withWidth (w) : waveArea.withLeft (waveArea.getRight() - w);
        juce::Path curve;
        curve.startNewSubPath (fr.getX(), fr.getY());
        for (int i = 0; i <= 24; ++i)
        {
            const double t = i / 24.0;
            const float gain = engine::fadeGain (shape, in ? t : 1.0 - t);
            curve.lineTo (fr.getX() + (float) t * fr.getWidth(), fr.getBottom() - gain * fr.getHeight());
        }
        curve.lineTo (fr.getRight(), fr.getY());
        curve.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillPath (curve);
        g.setColour (theme::text.withAlpha (0.9f));
        g.strokePath (curve, juce::PathStrokeType (1.0f));
    };
    drawFade (clip.fadeIn, clip.fadeInShape, true);
    drawFade (clip.fadeOut, clip.fadeOutShape, false);

    if (showsClipGain (track)) paintClipGainLine (g, track, clip, clipRect);

    // Warp markers: a line with a handle at the top of the waveform.
    if (clip.isElastic())
    {
        const bool draggingThis = drag == Drag::warpMarker && &session.getTrack (dragClip.track)->clips[(size_t) dragClip.index] == &clip;
        for (int i = 0; i < (int) clip.elastic.markers.size(); ++i)
        {
            const juce::int64 rendered = draggingThis && i == dragMarkerIndex ? ghostMarkerSample : clip.elastic.markers[(size_t) i].output;
            if (rendered < clip.sourceOffset || rendered > clip.sourceOffset + clip.length) continue;
            const float x = secondsToX (clip.getStartSeconds() + (double) (rendered - clip.sourceOffset) / clip.sampleRate);
            if (x < waveArea.getX() || x > waveArea.getRight()) continue;
            g.setColour (juce::Colours::orange.withAlpha (draggingThis && i == dragMarkerIndex ? 1.0f : 0.85f));
            g.drawLine (x, waveArea.getY(), x, waveArea.getBottom(), 1.5f);
            juce::Path handle;
            handle.addTriangle (x - 5.0f, waveArea.getY(), x + 5.0f, waveArea.getY(), x, waveArea.getY() + 7.0f);
            g.fillPath (handle);
        }
    }

    juce::String label = clip.name;
    if (std::abs (clip.gain - 1.0f) > 1.0e-4f)
        label += "   " + juce::String (juce::Decibels::gainToDecibels (clip.gain), 1) + " dB";
    if (clip.audioModified) label += "   (edited)";
    if (clip.isElastic())
    {
        label += "   [" + juce::String (engine::TimeStretch::modeName (clip.elastic.mode));
        if (std::abs (clip.elastic.ratio - 1.0) > 1.0e-6) label += " " + juce::String (juce::roundToInt (100.0 / clip.elastic.ratio)) + "%";
        if (std::abs (clip.elastic.pitchSemitones) > 1.0e-6) label += " " + juce::String (clip.elastic.pitchSemitones > 0 ? "+" : "") + juce::String (clip.elastic.pitchSemitones, 0) + " st";
        label += "]";
    }
    paintClipFrame (g, clipRect, track, label);
}

// Direct sample rendering for high zoom (and for pencil-edited audio, which
// has no thumbnail on disk).
void TrackArea::paintSamples (juce::Graphics& g, const model::AudioClip& clip, juce::Rectangle<float> area)
{
    if (clip.audio == nullptr || area.getWidth() <= 1.0f) return;
    const double sr = clip.sampleRate;
    const double viewStart = juce::jmax (viewStartSeconds, clip.getStartSeconds());
    const double viewEnd = juce::jmin (xToSeconds ((float) getWidth()), clip.getEndSeconds());
    if (viewEnd <= viewStart) return;

    const int channels = clip.audio->getNumChannels();
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
    const double pxPerSample = pixelsPerSecond / sr;
    const auto* data = clip.audio->getReadPointer (0);
    const int total = clip.audio->getNumSamples();

    if (pxPerSample >= 1.0)
    {
        // One point per sample (dots when very zoomed)
        juce::Path path;
        const juce::int64 first = clip.sourceOffset + (juce::int64) ((viewStart - clip.getStartSeconds()) * sr);
        const juce::int64 last = clip.sourceOffset + (juce::int64) ((viewEnd - clip.getStartSeconds()) * sr) + 1;
        for (juce::int64 sIdx = first; sIdx <= last && sIdx < total; ++sIdx)
        {
            if (sIdx < 0) continue;
            float v = 0.0f;
            for (int ch = 0; ch < channels; ++ch) v += clip.audio->getSample (ch, (int) sIdx);
            v = juce::jlimit (-1.0f, 1.0f, v / (float) channels * clip.gain);
            const float x = secondsToX (clip.getStartSeconds() + (double) (sIdx - clip.sourceOffset) / sr);
            const float y = mid - v * half;
            if (path.isEmpty()) path.startNewSubPath (x, y); else path.lineTo (x, y);
            if (pxPerSample >= 4.0) g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
        }
        g.strokePath (path, juce::PathStrokeType (1.0f));
        g.setColour (theme::grid);
        g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());
    }
    else
    {
        // Min/max per pixel column
        for (int px = (int) secondsToX (viewStart); px <= (int) secondsToX (viewEnd); ++px)
        {
            const double t0 = xToSeconds ((float) px), t1 = xToSeconds ((float) px + 1.0f);
            const juce::int64 a = clip.sourceOffset + (juce::int64) ((t0 - clip.getStartSeconds()) * sr);
            const juce::int64 b = juce::jmax (a + 1, clip.sourceOffset + (juce::int64) ((t1 - clip.getStartSeconds()) * sr));
            float lo = 1.0f, hi = -1.0f;
            for (juce::int64 sIdx = juce::jmax<juce::int64> (0, a); sIdx < juce::jmin<juce::int64> (b, total); ++sIdx)
            { const float v = data[sIdx] * clip.gain; lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
            if (hi >= lo) g.drawVerticalLine (px, mid - juce::jlimit (-1.0f, 1.0f, hi) * half, mid - juce::jlimit (-1.0f, 1.0f, lo) * half);
        }
    }
}

bool TrackArea::pencilZoomOk() const { return pixelsPerSecond / transport.getSampleRate() >= 0.5; }

juce::Rectangle<float> TrackArea::waveAreaFor (const model::ClipRef& ref) const
{
    return rectForClip (ref).reduced (1.0f).withTrimmedTop (16.0f);
}

//==============================================================================
// Clip gain line

void TrackArea::paintClipGainLine (juce::Graphics& g, const model::Track& track, const model::AudioClip& clip, juce::Rectangle<float> clipRect)
{
    auto area = clipRect.reduced (1.0f).withTrimmedTop (16.0f);
    const double sr = clip.sampleRate;
    const engine::AutomationLane* lane = clip.gainLane.get();

    juce::Path path;
    const int steps = juce::jmax (2, (int) area.getWidth() / 3);
    for (int i = 0; i <= steps; ++i)
    {
        const double rel = clip.getLengthSeconds() * i / steps;
        const auto src = clip.sourceOffset + (juce::int64) std::llround (rel * sr);
        const float v = lane != nullptr && ! lane->isEmpty() ? lane->valueAt (src, 1.0f) : 1.0f;
        const float x = secondsToX (clip.getStartSeconds() + rel);
        const float y = valueToY (engine::ParamId::volume(), v, area.toNearestInt());
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xfff1c40f).withAlpha (lane != nullptr ? 0.95f : 0.5f));
    g.strokePath (path, juce::PathStrokeType (lane != nullptr ? 1.6f : 1.0f));

    if (lane != nullptr)
        for (const auto& p : lane->points)
        {
            if (p.time < clip.sourceOffset || p.time > clip.sourceOffset + clip.length) continue;
            const float x = secondsToX (clip.getStartSeconds() + (double) (p.time - clip.sourceOffset) / sr);
            const float y = valueToY (engine::ParamId::volume(), p.value, area.toNearestInt());
            g.setColour (juce::Colour (0xfff1c40f));
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        }

    if (drag == Drag::clipGainPoint && dragMoved && &session.getTrack (dragClip.track)->clips[(size_t) dragClip.index] == &clip)
    {
        const float x = secondsToX (clip.getStartSeconds() + (double) (ghostPoint.time - clip.sourceOffset) / sr);
        const float y = valueToY (engine::ParamId::volume(), ghostPoint.value, area.toNearestInt());
        g.setColour (theme::text);
        g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.5f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (juce::Decibels::gainToDecibels (ghostPoint.value, -60.0f), 1) + " dB", (int) x + 8, (int) y - 8, 80, 16, juce::Justification::centredLeft);
    }
    juce::ignoreUnused (track);
}

int TrackArea::clipGainPointAt (const model::ClipRef& ref, juce::Point<int> pt) const
{
    auto* t = session.getTrack (ref.track);
    if (t == nullptr || ref.kind != model::ClipRef::Kind::audio || ref.index >= (int) t->clips.size()) return -1;
    const auto& clip = t->clips[(size_t) ref.index];
    if (clip.gainLane == nullptr) return -1;
    const auto area = waveAreaFor (ref).toNearestInt();
    for (int i = 0; i < (int) clip.gainLane->points.size(); ++i)
    {
        const auto& p = clip.gainLane->points[(size_t) i];
        const float x = secondsToX (clip.getStartSeconds() + (double) (p.time - clip.sourceOffset) / clip.sampleRate);
        const float y = valueToY (engine::ParamId::volume(), p.value, area);
        if (std::abs (x - pt.x) <= 6.0f && std::abs (y - pt.y) <= 6.0f) return i;
    }
    return -1;
}

void TrackArea::paintPatternClip (juce::Graphics& g, const model::Track& track, const model::PatternClip& clip, juce::Rectangle<int> r)
{
    auto clipRect = clipRectFor (clip.getStartSeconds(), clip.getEndSeconds(), r);
    if (clipRect.getRight() < r.getX() || clipRect.getX() > r.getRight() || clip.pattern == nullptr) return;

    g.setColour (track.colour.withMultipliedBrightness (track.mute ? 0.35f : 0.55f));
    g.fillRoundedRectangle (clipRect, 4.0f);

    // Step dots: pads as rows, steps across, repeating for each pattern iteration.
    const auto& pattern = *clip.pattern;
    const double stepSeconds = 60.0 / (transport.getBpm() * pattern.stepsPerBeat);
    const float stepWidth = (float) (stepSeconds * pixelsPerSecond);
    auto dotArea = clipRect.reduced (2.0f).withTrimmedTop (17.0f);
    const float rowHeight = dotArea.getHeight() / (float) engine::DrumKit::numPads;

    if (stepWidth >= 2.0f)
    {
        const int totalSteps = (int) std::ceil (clip.getLengthSeconds() / stepSeconds);
    const int shownSteps = clip.loop ? totalSteps : juce::jmin (totalSteps, pattern.numSteps);
        g.setColour (track.colour.contrasting (0.9f).withAlpha (0.9f));

        for (int k = 0; k < shownSteps; ++k)
        {
            const float x = dotArea.getX() + k * stepWidth;
            if (x > r.getRight() || x + stepWidth < r.getX()) continue;
            const int step = k % pattern.numSteps;

            for (int pad = 0; pad < engine::DrumKit::numPads; ++pad)
                if (pattern.get (pad, step) > 0)
                {
                    const int held = juce::jmin (pattern.getLength (pad, step), pattern.numSteps - step, shownSteps - k);   // a held hit spans its steps
                    g.fillRect (x + 0.5f, dotArea.getY() + pad * rowHeight + 0.5f,
                                juce::jmax (1.0f, held * stepWidth - 1.0f), juce::jmax (1.0f, rowHeight - 1.0f));
                }
        }
    }

    paintClipFrame (g, clipRect, track, clip.name);
}

//==============================================================================
// Edit helpers

double TrackArea::gridSeconds() const { return transport.beatsToSeconds (edit.gridBeats); }

double TrackArea::snapSeconds (double seconds) const
{
    if (edit.mode != EditSettings::Mode::grid) return juce::jmax (0.0, seconds);
    const double g = gridSeconds();
    return juce::jmax (0.0, std::round (seconds / g) * g);
}

double TrackArea::snapDelta (double delta) const
{
    if (edit.mode != EditSettings::Mode::grid) return delta;
    const double g = gridSeconds();
    return std::round (delta / g) * g;
}

juce::int64 TrackArea::toSamples (double seconds) const
{
    return (juce::int64) std::llround (seconds * transport.getSampleRate());
}

juce::Rectangle<float> TrackArea::rectForClip (const model::ClipRef& ref) const
{
    auto t = model::ClipEdits::timing (session, ref);
    if (! t) return {};
    return clipRectFor ((double) t->start / t->sampleRate, (double) (t->start + t->length) / t->sampleRate, getLaneBounds (ref.track));
}

std::optional<model::ClipRef> TrackArea::clipAtPoint (juce::Point<int> p) const
{
    const int track = trackIndexAtY (p.y);
    if (track < 0 || p.x < theme::trackHeaderWidth) return std::nullopt;
    if (session.getTracks()[(size_t) track].isFrozen()) return std::nullopt;   // frozen: nothing to grab
    const auto sample = toSamples (xToSeconds ((float) p.x));
    return model::ClipEdits::clipAt (session, track, sample);
}

bool TrackArea::isSelected (const model::ClipRef& ref) const
{
    return std::find (selectedClips.begin(), selectedClips.end(), ref) != selectedClips.end();
}

void TrackArea::selectClip (const model::ClipRef& ref, bool add)
{
    if (! add) selectedClips.clear();
    if (isSelected (ref)) { if (add) std::erase (selectedClips, ref); }
    else selectedClips.push_back (ref);
    repaint();
}

void TrackArea::clearSelection()
{
    selectedClips.clear();
    setTimeSelection ({});
    repaint();
}

void TrackArea::setTimeSelection (TimeSelection sel)
{
    timeSelection = sel;
    if (onTimeSelectionChanged) onTimeSelectionChanged();
    repaint();
}

EditSettings::Tool TrackArea::effectiveTool (const juce::MouseEvent& e, const std::optional<model::ClipRef>& hit,
                                             bool& nearStart, bool& nearEnd) const
{
    nearStart = nearEnd = false;
    if (hit)
    {
        const auto r = rectForClip (*hit);
        nearStart = std::abs (e.x - r.getX()) < 7.0f;
        nearEnd   = std::abs (e.x - r.getRight()) < 7.0f;
    }

    if (edit.tool != EditSettings::Tool::smart) return edit.tool;

    // Smart Tool: edges trim, lower half grabs, upper half selects.
    if (! hit) return EditSettings::Tool::selector;
    if (nearStart || nearEnd) return EditSettings::Tool::trimmer;
    const auto r = rectForClip (*hit);
    return e.y < r.getCentreY() ? EditSettings::Tool::selector : EditSettings::Tool::grabber;
}

void TrackArea::executeWithShuffle (std::unique_ptr<model::Command> cmd, const juce::String& name, std::initializer_list<int> tracks)
{
    if (edit.mode != EditSettings::Mode::shuffle)
    {
        session.execute (std::move (cmd));
        return;
    }
    auto compound = std::make_unique<model::CompoundCommand> (name);
    compound->add (std::move (cmd));
    std::vector<int> done;
    for (int t : tracks)
        if (t >= 0 && std::find (done.begin(), done.end(), t) == done.end())
        {
            compound->add (std::make_unique<model::RepackTrackCommand> (t));
            done.push_back (t);
        }
    session.execute (std::move (compound));
}

//==============================================================================
// Mouse

void TrackArea::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int track = trackIndexAtY (e.y);
    if (track >= 0) setSelectedTrack (track);

    if (e.x < theme::trackHeaderWidth)
    {
        if (e.mods.isPopupMenu() && track >= 0)
        {
            const auto& t = session.getTracks()[(size_t) track];
            juce::PopupMenu menu;
            menu.addItem (1, "Rename Track...");
            // Colour: the palette new tracks are dealt from, Automatic (its place in that palette), or any colour
            juce::PopupMenu colours;
            const auto& palette = model::Session::trackPalette();
            static const char* colourNames[] = { "Blue", "Green", "Orange", "Purple", "Red", "Teal", "Yellow", "Pink" };
            for (int i = 0; i < (int) palette.size(); ++i)
                colours.addColouredItem (100 + i, i < 8 ? colourNames[i] : "Colour " + juce::String (i + 1), palette[(size_t) i], true, t.colour == palette[(size_t) i]);
            colours.addSeparator();
            colours.addItem (99, "Automatic  (its place in the palette)");
            colours.addItem (98, "Custom...");
            menu.addSubMenu ("Colour", colours);
            if (t.isAudio() || t.isInstrument())
            {
                menu.addSeparator();
                menu.addItem (2, t.isFrozen() ? "Unfreeze Track" : "Freeze Track  (render inserts, keep the fader live)");
                menu.addItem (3, "Commit Track...  (new audio track from the render)");
            }
            menu.addSeparator();
            menu.addItem (4, "Delete Track");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, track] (int r)
            {
                if (r == 0 || ! onTrackAction) return;
                const auto* tt = session.getTrack (track);
                if (tt == nullptr) return;
                if (r == 1) onTrackAction (track, "rename");
                else if (r >= 100 && r < 100 + (int) model::Session::trackPalette().size()) { if (onTrackColour) onTrackColour (track, model::Session::trackPalette()[(size_t) (r - 100)]); }
                else if (r == 99) { if (onTrackColour) onTrackColour (track, model::Session::colourForTrackIndex (track)); }
                else if (r == 98) { if (onTrackCustomColour) onTrackCustomColour (track); }
                else if (r == 2) onTrackAction (track, tt->isFrozen() ? "unfreeze" : "freeze");
                else if (r == 3) onTrackAction (track, "commit");
                else if (r == 4) onTrackAction (track, "delete");
            });
        }
        return;
    }

    // Marker strip: click a marker to recall it, right-click for the menu; empty strip locates.
    if (e.y < theme::markerStripHeight)
    {
        const int hit = markerAtX (e.x);
        if (e.mods.isPopupMenu()) { showMarkerMenu (hit, xToSeconds ((float) e.x), e.getScreenPosition()); return; }
        if (hit >= 0) { recallMarker (hit); return; }
        transport.setPositionSeconds (snapSeconds (xToSeconds ((float) e.x)));
        repaint();
        return;
    }
    // Ruler: always locates
    if (e.y < theme::rulerHeight)
    {
        transport.setPositionSeconds (snapSeconds (xToSeconds ((float) e.x)));
        repaint();
        return;
    }

    dragStartPoint = e.getPosition();
    dragMoved = false;
    dragAnchorSeconds = xToSeconds ((float) e.x);

    // Alternate playlist lane: only time selection (for comping) and locate
    if (const int alt = alternateAtY (track, e.y); alt >= 0)
    {
        drag = Drag::select;
        timeSelection = {};
        timeSelection.start = timeSelection.end = snapSeconds (dragAnchorSeconds);
        timeSelection.firstTrack = timeSelection.lastTrack = track;
        timeSelection.playlistTrack = track;
        timeSelection.playlistIndex = alt;
        selectedClips.clear();
        repaint();
        return;
    }

    // Automation lane view: breakpoints instead of clips (Grabber/Smart/Trimmer tools)
    if (auto* t = session.getTrack (track); t != nullptr && shownLane (*t) && edit.tool != EditSettings::Tool::selector
        && edit.tool != EditSettings::Tool::zoomer)
    {
        const auto param = *shownLane (*t);
        const auto lane = getLaneBounds (track);
        const int hitPoint = automationPointAt (track, e.getPosition());

        if (e.mods.isPopupMenu() || (e.mods.isAltDown() && hitPoint >= 0))
        {
            if (hitPoint >= 0)
            {
                auto updated = std::make_shared<engine::AutomationLane> (*t->laneFor (param));
                updated->points.erase (updated->points.begin() + hitPoint);
                session.execute (std::make_unique<model::ReplaceAutomationLaneCommand> (track, updated, "Delete Breakpoint"));
            }
            return;
        }

        if (hitPoint >= 0)
        {
            drag = Drag::automationPoint;
            dragClip = { track, model::ClipRef::Kind::audio, -1 };
            dragPointIndex = hitPoint;
            ghostPoint = t->laneFor (param)->points[(size_t) hitPoint];
            return;
        }

        // Add a point where clicked, then keep dragging it
        auto updated = std::make_shared<engine::AutomationLane>();
        updated->param = param;
        if (const auto* existing = t->laneFor (param)) updated->points = existing->points;
        engine::AutomationPoint np { toSamples (snapSeconds (dragAnchorSeconds)), yToValue (param, e.y, lane) };
        updated->points.push_back (np);
        updated->sortPoints();
        session.execute (std::make_unique<model::ReplaceAutomationLaneCommand> (track, updated, "Add Breakpoint"));

        if (const auto* fresh = session.getTrack (track)->laneFor (param))
            for (int i = 0; i < (int) fresh->points.size(); ++i)
                if (fresh->points[(size_t) i].time == np.time) { dragPointIndex = i; break; }
        drag = Drag::automationPoint;
        dragClip = { track, model::ClipRef::Kind::audio, -1 };
        ghostPoint = np;
        return;
    }

    const auto hit = clipAtPoint (e.getPosition());

    // Scrubber: drag audio under the cursor (transport must be stopped)
    if (edit.tool == EditSettings::Tool::scrubber)
    {
        if (transport.isPlaying()) transport.stop();
        transport.setPositionSeconds (dragAnchorSeconds);
        graph.startScrub (track, toSamples (dragAnchorSeconds));
        drag = Drag::scrub;
        return;
    }

    // Pencil: redraw samples of the clip under the cursor
    if (edit.tool == EditSettings::Tool::pencil)
    {
        if (! hit || hit->kind != model::ClipRef::Kind::audio || ! pencilZoomOk()) return;
        const auto& clip = session.getTrack (hit->track)->clips[(size_t) hit->index];
        if (clip.audio == nullptr) return;
        pencilBuffer = std::make_shared<juce::AudioBuffer<float>> (*clip.audio);   // working copy
        dragClip = *hit;
        pencilLastSample = -1;
        drag = Drag::pencil;
        mouseDrag (e);
        return;
    }

    // Clip gain view: breakpoints on the clip's gain line
    if (hit && hit->kind == model::ClipRef::Kind::audio && showsClipGain (*session.getTrack (track))
        && edit.tool != EditSettings::Tool::selector && edit.tool != EditSettings::Tool::zoomer)
    {
        const auto& clip = session.getTrack (hit->track)->clips[(size_t) hit->index];
        const int hitPoint = clipGainPointAt (*hit, e.getPosition());
        const auto area = waveAreaFor (*hit).toNearestInt();

        if (e.mods.isPopupMenu() || (e.mods.isAltDown() && hitPoint >= 0))
        {
            if (hitPoint >= 0)
            {
                auto updated = std::make_shared<engine::AutomationLane> (*clip.gainLane);
                updated->points.erase (updated->points.begin() + hitPoint);
                session.execute (std::make_unique<model::SetClipGainLaneCommand> (*hit, updated, "Delete Clip Gain Point"));
            }
            return;
        }

        auto updated = std::make_shared<engine::AutomationLane>();
        updated->param = engine::ParamId::volume();
        if (clip.gainLane != nullptr) updated->points = clip.gainLane->points;
        dragClip = *hit;
        if (hitPoint >= 0)
        {
            dragPointIndex = hitPoint;
            ghostPoint = updated->points[(size_t) hitPoint];
        }
        else
        {
            engine::AutomationPoint np { clip.sourceOffset + toSamples (snapSeconds (dragAnchorSeconds) - clip.getStartSeconds()),
                                         yToValue (engine::ParamId::volume(), e.y, area) };
            np.time = juce::jlimit (clip.sourceOffset, clip.sourceOffset + clip.length, np.time);
            updated->points.push_back (np);
            updated->sortPoints();
            session.execute (std::make_unique<model::SetClipGainLaneCommand> (*hit, updated, "Add Clip Gain Point"));
            ghostPoint = np;
            dragPointIndex = -1;
            if (const auto* fresh = session.getTrack (hit->track)->clips[(size_t) hit->index].gainLane.get())
                for (int i = 0; i < (int) fresh->points.size(); ++i)
                    if (fresh->points[(size_t) i].time == np.time) { dragPointIndex = i; break; }
        }
        drag = Drag::clipGainPoint;
        return;
    }

    bool nearStart = false, nearEnd = false;
    const auto tool = effectiveTool (e, hit, nearStart, nearEnd);

    if (tool == EditSettings::Tool::zoomer)
    {
        if (e.mods.isAltDown()) { pixelsPerSecond = juce::jmax (5.0, pixelsPerSecond / 2.0); }
        else drag = Drag::zoomRange;
        repaint();
        return;
    }

    // Right-click on empty lane space: a clip for the selection (instrument tracks)
    if (! hit && e.mods.isPopupMenu() && track >= 0)
    {
        const auto* t = session.getTrack (track);
        if (t != nullptr && t->isInstrument())
        {
            const bool useSelection = timeSelection.isValid() && track >= timeSelection.firstTrack && track <= timeSelection.lastTrack;
            const double at = snapSeconds (xToSeconds ((float) e.x));
            juce::PopupMenu menu;
            menu.addItem (1, useSelection ? "Add Clip for Selection  (" + juce::String (timeSelection.end - timeSelection.start, 2) + " s)" : "Add Clip Here  (one bar)");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, at] (int r) { if (r == 1) addClipForSelection (at); });
            return;
        }
    }

    if (tool == EditSettings::Tool::selector || ! hit)
    {
        drag = Drag::select;
        timeSelection = {};
        timeSelection.start = timeSelection.end = snapSeconds (dragAnchorSeconds);
        timeSelection.firstTrack = timeSelection.lastTrack = track;
        if (! e.mods.isShiftDown()) selectedClips.clear();
        repaint();
        return;
    }

    // Right-click on an audio clip: the Elastic / clip menu.
    if (hit->kind == model::ClipRef::Kind::audio && e.mods.isPopupMenu())
    {
        if (! isSelected (*hit)) selectClip (*hit, false);
        dragAnchorSeconds = xToSeconds ((float) e.x);
        showClipMenu (*hit, e.getScreenPosition());
        return;
    }
    // Right-click on a pattern or MIDI clip: looping.
    if (hit->kind != model::ClipRef::Kind::audio && e.mods.isPopupMenu())
    {
        if (! isSelected (*hit)) selectClip (*hit, false);
        showLoopMenu (*hit, e.getScreenPosition());
        return;
    }

    // Warp marker handles (Grabber / Smart): drag to move, Alt-click to delete.
    if (hit->kind == model::ClipRef::Kind::audio && (tool == EditSettings::Tool::grabber || tool == EditSettings::Tool::smart))
    {
        const int marker = warpMarkerAt (*hit, e.getPosition());
        if (marker >= 0)
        {
            const auto& clip = session.getTrack (hit->track)->clips[(size_t) hit->index];
            if (e.mods.isAltDown()) { applyElastic (*hit, model::Elastic::withoutMarker (clip, marker), "Delete Warp Marker"); return; }
            dragClip = *hit;
            dragMarkerIndex = marker;
            ghostMarkerSample = clip.elastic.markers[(size_t) marker].output;
            drag = Drag::warpMarker;
            return;
        }
    }

    // Grabber / Trimmer on a clip
    if (edit.mode == EditSettings::Mode::spot && tool == EditSettings::Tool::grabber)
    {
        selectClip (*hit, false);
        showSpotDialog (*hit);
        return;
    }

    if (! isSelected (*hit) || e.mods.isShiftDown()) selectClip (*hit, e.mods.isShiftDown());
    dragClip = *hit;
    dragOriginal = *model::ClipEdits::timing (session, *hit);
    dragTargetTrack = hit->track;
    ghostStart = (double) dragOriginal.start / dragOriginal.sampleRate;
    ghostLength = (double) dragOriginal.length / dragOriginal.sampleRate;
    timeSelection = {};

    if (hit->kind == model::ClipRef::Kind::audio)
    {
        const auto r = rectForClip (*hit);
        const bool topZone = e.y < r.getY() + 16.0f + 14.0f && e.y >= r.getY() + 16.0f;   // just under the name bar
        const auto& clip = session.getTrack (hit->track)->clips[(size_t) hit->index];

        // Smart Tool corners: fade handles.
        if (edit.tool == EditSettings::Tool::smart && topZone && (nearStart || nearEnd))
        {
            drag = nearStart ? Drag::fadeIn : Drag::fadeOut;
            ghostFadeSeconds = (double) (nearStart ? clip.fadeIn : clip.fadeOut) / clip.sampleRate;
            return;
        }
        // Ctrl+drag vertically: clip gain.
        if (e.mods.isCtrlDown() && tool == EditSettings::Tool::grabber)
        {
            drag = Drag::clipGain;
            ghostGainDb = juce::Decibels::gainToDecibels (clip.gain, -60.0f);
            return;
        }
    }

    if (tool == EditSettings::Tool::trimmer)
        drag = nearStart || (! nearEnd && e.x < rectForClip (*hit).getCentreX()) ? Drag::trimStart : Drag::trimEnd;
    else
        drag = Drag::move;
}

void TrackArea::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none) return;
    if (e.getDistanceFromDragStart() > 2) dragMoved = true;
    const double now = xToSeconds ((float) e.x);
    const double origStart = (double) dragOriginal.start / dragOriginal.sampleRate;
    const double origLen = (double) dragOriginal.length / dragOriginal.sampleRate;

    switch (drag)
    {
        case Drag::select:
        {
            const double a = dragAnchorSeconds, b = now;
            timeSelection.start = snapSeconds (juce::jmin (a, b));
            timeSelection.end = snapSeconds (juce::jmax (a, b));
            if (timeSelection.end <= timeSelection.start && edit.mode == EditSettings::Mode::grid)
                timeSelection.end = timeSelection.start + gridSeconds();
            if (! timeSelection.isOnAlternate())
            {
                const int t = trackIndexAtY (e.y);
                if (t >= 0) { timeSelection.firstTrack = juce::jmin (timeSelection.firstTrack, t); timeSelection.lastTrack = juce::jmax (timeSelection.lastTrack, t); }
                for (int m : model::GroupLogic::editMembers (session, timeSelection.firstTrack))
                { timeSelection.firstTrack = juce::jmin (timeSelection.firstTrack, m); timeSelection.lastTrack = juce::jmax (timeSelection.lastTrack, m); }
            }
            break;
        }
        case Drag::warpMarker:
        {
            if (auto* t = session.getTrack (dragClip.track); t != nullptr && dragClip.index < (int) t->clips.size())
            {
                const auto& clip = t->clips[(size_t) dragClip.index];
                const double seconds = edit.mode == EditSettings::Mode::grid ? snapSeconds (now) : now;
                const juce::int64 rendered = clip.sourceOffset + toSamples (seconds - clip.getStartSeconds());
                ghostMarkerSample = model::Elastic::withMarkerMoved (clip, dragMarkerIndex, rendered).markers[(size_t) dragMarkerIndex].output;
            }
            break;
        }
        case Drag::move:
        {
            const double delta = now - dragAnchorSeconds;
            ghostStart = edit.mode == EditSettings::Mode::grid && ! edit.relativeGrid ? snapSeconds (origStart + delta)
                                                                                      : juce::jmax (0.0, origStart + snapDelta (delta));
            const int t = trackIndexAtY (e.y);
            if (t >= 0 && model::ClipEdits::canPlaceOn (session, dragClip, t)) dragTargetTrack = t;
            break;
        }
        case Drag::trimStart:
        {
            const double delta = now - dragAnchorSeconds;
            double newStart = edit.mode == EditSettings::Mode::grid && ! edit.relativeGrid ? snapSeconds (origStart + delta)
                                                                                           : juce::jmax (0.0, origStart + snapDelta (delta));
            newStart = juce::jmin (newStart, origStart + origLen - 0.01);
            if (dragClip.kind == model::ClipRef::Kind::audio)   // can't reveal audio before the file start
                newStart = juce::jmax (newStart, origStart - (double) dragOriginal.offset / dragOriginal.sampleRate);
            ghostStart = newStart;
            ghostLength = origStart + origLen - newStart;
            break;
        }
        case Drag::trimEnd:
        {
            const double delta = now - dragAnchorSeconds;
            double newEnd = edit.mode == EditSettings::Mode::grid && ! edit.relativeGrid ? snapSeconds (origStart + origLen + delta)
                                                                                         : origStart + origLen + snapDelta (delta);
            newEnd = juce::jmax (newEnd, origStart + 0.01);
            if (dragOriginal.maxLength > 0)
                newEnd = juce::jmin (newEnd, origStart + (double) dragOriginal.maxLength / dragOriginal.sampleRate);
            ghostLength = newEnd - origStart;
            break;
        }
        case Drag::fadeIn:
        {
            const double len = juce::jlimit (0.0, origLen, now - origStart);
            ghostFadeSeconds = edit.mode == EditSettings::Mode::grid ? snapDelta (len) : len;
            break;
        }
        case Drag::fadeOut:
        {
            const double len = juce::jlimit (0.0, origLen, origStart + origLen - now);
            ghostFadeSeconds = edit.mode == EditSettings::Mode::grid ? snapDelta (len) : len;
            break;
        }
        case Drag::scrub:
            graph.setScrubTarget (toSamples (now));
            break;

        case Drag::pencil:
        {
            auto* t = session.getTrack (dragClip.track);
            if (t == nullptr || pencilBuffer == nullptr || dragClip.index >= (int) t->clips.size()) break;
            const auto& clip = t->clips[(size_t) dragClip.index];
            const auto area = waveAreaFor (dragClip);
            const int sample = (int) juce::jlimit<juce::int64> (0, pencilBuffer->getNumSamples() - 1,
                                   clip.sourceOffset + toSamples (now - clip.getStartSeconds()));
            const float value = juce::jlimit (-1.0f, 1.0f, (area.getCentreY() - (float) e.y) / (area.getHeight() * 0.5f)) / juce::jmax (0.01f, clip.gain);
            if (pencilLastSample < 0) { pencilLastSample = sample; pencilLastValue = value; }
            const int from = juce::jmin (pencilLastSample, sample), to = juce::jmax (pencilLastSample, sample);
            for (int i = from; i <= to; ++i)
            {
                const float frac = to == from ? 1.0f : (float) (i - from) / (float) (to - from);
                const float v = pencilLastSample <= sample ? pencilLastValue + (value - pencilLastValue) * frac
                                                           : value + (pencilLastValue - value) * frac;
                for (int ch = 0; ch < pencilBuffer->getNumChannels(); ++ch) pencilBuffer->setSample (ch, i, v);
            }
            pencilLastSample = sample; pencilLastValue = value;
            break;
        }

        case Drag::clipGainPoint:
        {
            auto* t = session.getTrack (dragClip.track);
            if (t == nullptr || dragClip.index >= (int) t->clips.size()) break;
            const auto& clip = t->clips[(size_t) dragClip.index];
            ghostPoint.time = juce::jlimit (clip.sourceOffset, clip.sourceOffset + clip.length,
                                            clip.sourceOffset + toSamples (snapSeconds (now) - clip.getStartSeconds()));
            ghostPoint.value = yToValue (engine::ParamId::volume(), e.y, waveAreaFor (dragClip).toNearestInt());
            break;
        }

        case Drag::automationPoint:
        {
            if (auto* t = session.getTrack (dragClip.track); t != nullptr && shownLane (*t))
            {
                const auto param = *shownLane (*t);
                ghostPoint.time = toSamples (snapSeconds (now));
                ghostPoint.value = yToValue (param, e.y, getLaneBounds (dragClip.track));
            }
            break;
        }
        case Drag::clipGain:
        {
            const auto& clip = session.getTrack (dragClip.track)->clips[(size_t) dragClip.index];
            const float startDb = juce::Decibels::gainToDecibels (clip.gain, -60.0f);
            ghostGainDb = juce::jlimit (-60.0f, 12.0f, startDb + (float) (dragStartPoint.y - e.y) * 0.1f);   // 10 px per dB
            break;
        }
        case Drag::zoomRange:
        case Drag::none:
            break;
    }
    repaint();
}

void TrackArea::mouseUp (const juce::MouseEvent& e)
{
    const auto finished = drag;
    drag = Drag::none;

    switch (finished)
    {
        case Drag::select:
            if (! dragMoved || ! timeSelection.isValid())
            {
                // Plain click: locate, no range
                timeSelection = {};
                transport.setPositionSeconds (snapSeconds (dragAnchorSeconds));
                if (onTimeSelectionChanged) onTimeSelectionChanged();
            }
            else
            {
                if (! transport.isPlaying()) transport.setPositionSeconds (timeSelection.start);
                setTimeSelection (timeSelection);
            }
            break;

        case Drag::zoomRange:
        {
            const double a = dragAnchorSeconds, b = xToSeconds ((float) e.x);
            if (dragMoved && std::abs (b - a) > 0.01)
            {
                viewStartSeconds = juce::jmin (a, b);
                pixelsPerSecond = juce::jlimit (5.0, 400000.0, (getWidth() - theme::trackHeaderWidth) / std::abs (b - a));
            }
            else
            {
                const double anchor = dragAnchorSeconds;
                pixelsPerSecond = juce::jmin (400000.0, pixelsPerSecond * 2.0);
                viewStartSeconds = juce::jmax (0.0, anchor - (e.x - theme::trackHeaderWidth) / pixelsPerSecond);
            }
            break;
        }

        case Drag::move:
        case Drag::trimStart:
        case Drag::trimEnd:
            if (dragMoved) commitDrag();
            break;

        case Drag::fadeIn:
        case Drag::fadeOut:
            if (dragMoved)
                if (auto* t = session.getTrack (dragClip.track); t != nullptr && dragClip.index < (int) t->clips.size())
                {
                    const auto& c = t->clips[(size_t) dragClip.index];
                    const auto samples = (juce::int64) std::llround (ghostFadeSeconds * c.sampleRate);
                    session.execute (std::make_unique<model::SetClipFadesCommand> (dragClip,
                        finished == Drag::fadeIn ? samples : c.fadeIn, c.fadeInShape,
                        finished == Drag::fadeOut ? samples : c.fadeOut, c.fadeOutShape));
                }
            break;

        case Drag::clipGain:
            if (dragMoved)
                session.execute (std::make_unique<model::SetClipGainCommand> (dragClip, juce::Decibels::decibelsToGain (ghostGainDb, -60.0f)));
            break;

        case Drag::automationPoint:
            if (dragMoved) commitAutomationDrag();
            dragPointIndex = -1;
            break;

        case Drag::scrub:
            graph.stopScrub();
            break;

        case Drag::pencil:
            if (pencilBuffer != nullptr && pencilLastSample >= 0)
                session.execute (std::make_unique<model::ReplaceClipAudioCommand> (dragClip, std::shared_ptr<const juce::AudioBuffer<float>> (pencilBuffer)));
            pencilBuffer.reset();
            break;

        case Drag::warpMarker:
            if (dragMoved)
                if (auto* t = session.getTrack (dragClip.track); t != nullptr && dragClip.index < (int) t->clips.size())
                    applyElastic (dragClip, model::Elastic::withMarkerMoved (t->clips[(size_t) dragClip.index], dragMarkerIndex, ghostMarkerSample), "Move Warp Marker");
            dragMarkerIndex = -1;
            break;

        case Drag::clipGainPoint:
            if (dragMoved)
                if (auto* t = session.getTrack (dragClip.track); t != nullptr && dragClip.index < (int) t->clips.size())
                {
                    const auto& clip = t->clips[(size_t) dragClip.index];
                    auto updated = std::make_shared<engine::AutomationLane>();
                    updated->param = engine::ParamId::volume();
                    if (clip.gainLane != nullptr) updated->points = clip.gainLane->points;
                    if (juce::isPositiveAndBelow (dragPointIndex, (int) updated->points.size())) updated->points[(size_t) dragPointIndex] = ghostPoint;
                    updated->sortPoints();
                    session.execute (std::make_unique<model::SetClipGainLaneCommand> (dragClip, updated, "Move Clip Gain Point"));
                }
            dragPointIndex = -1;
            break;

        case Drag::none:
            break;
    }
    repaint();
}

void TrackArea::commitDrag()
{
    auto t = model::ClipEdits::timing (session, dragClip);
    if (! t) return;
    const double sr = t->sampleRate;

    if (drag == Drag::none) {}   // (state already cleared by mouseUp)

    if (ghostLength <= 0.0) return;

    if (dragTargetTrack != dragClip.track || std::abs (ghostStart - (double) t->start / sr) > 1e-9 || std::abs (ghostLength - (double) t->length / sr) > 1e-9)
    {
        const bool isMove = std::abs (ghostLength - (double) t->length / sr) < 1e-9;
        std::unique_ptr<model::Command> cmd;
        if (isMove)
            cmd = model::GroupLogic::moveCommand (session, dragClip, dragTargetTrack, toSamples (ghostStart));
        else if (edit.tceTrim && dragClip.kind == model::ClipRef::Kind::audio)
        {
            // TCE: the clip is stretched to the new length; a start trim then moves it so the right edge stays put.
            const auto& clip = session.getTrack (dragClip.track)->clips[(size_t) dragClip.index];
            auto elastic = std::make_unique<model::SetClipElasticCommand> (session, dragClip,
                               model::Elastic::forVisibleLength (clip, toSamples (ghostLength)), "TCE Trim", /*deferRender*/ true);
            if (elastic->wasCancelled()) return;
            const juce::int64 moveTo = std::abs (ghostStart - (double) t->start / sr) > 1e-9 ? toSamples (ghostStart) : -1;
            const auto ref = dragClip;
            const double seconds = ghostLength;
            selectedClips.clear();
            ElasticJob::run (session, std::move (elastic), [this, ref, moveTo, seconds] (bool applied)
            {
                if (! applied) { if (onStatus) onStatus ("TCE Trim: cancelled"); return; }
                if (moveTo >= 0) session.execute (std::make_unique<model::MoveClipCommand> (ref, ref.track, moveTo));
                if (auto sel = model::ClipEdits::clipAt (session, ref.track, (moveTo >= 0 ? moveTo : session.getTrack (ref.track)->clips[(size_t) ref.index].timelineStart) + 1))
                    selectedClips.push_back (*sel);
                if (onStatus) onStatus ("TCE: stretched to " + juce::String (seconds, 2) + " s");
                repaint();
            });
            return;
        }
        else
            cmd = model::GroupLogic::trimCommand (session, dragClip, toSamples (ghostStart), toSamples (ghostLength));

        selectedClips.clear();
        executeWithShuffle (std::move (cmd), isMove ? "Move Clip" : "Trim Clip", { dragClip.track, dragTargetTrack });

        // Re-select the edited clip (it may have moved lists)
        const int track = isMove ? dragTargetTrack : dragClip.track;
        if (auto ref = model::ClipEdits::clipAt (session, track, toSamples (ghostStart) + 1))
        {
            selectedClips.push_back (*ref);
            // A looping clip trimmed back to its base stops looping (choose Loop again to loop); a clip shrunk
            // below content that grew with nothing in it folds the content back to the clip
            if (! isMove)
            {
                const bool wasLooping = model::ClipLoop::isLooping (session, *ref);
                if (auto fix = model::ClipLoop::afterTrim (session, *ref)) { session.execute (std::move (fix)); if (wasLooping && onStatus) onStatus ("Clip is back to its own length: loop off"); }
            }
        }
    }
}

void TrackArea::mouseMove (const juce::MouseEvent& e) { updateCursor (e); }

void TrackArea::updateCursor (const juce::MouseEvent& e)
{
    if (e.x < theme::trackHeaderWidth || e.y < theme::rulerHeight) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    const auto hit = clipAtPoint (e.getPosition());
    bool ns = false, ne = false;
    const auto tool = effectiveTool (e, hit, ns, ne);
    if (hit && hit->kind == model::ClipRef::Kind::audio && edit.tool == EditSettings::Tool::smart && (ns || ne))
    {
        const auto r = rectForClip (*hit);
        if (e.y >= r.getY() + 16.0f && e.y < r.getY() + 30.0f) { setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor); return; }
    }
    switch (tool)
    {
        case EditSettings::Tool::zoomer:   setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
        case EditSettings::Tool::trimmer:  setMouseCursor (hit ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); break;
        case EditSettings::Tool::selector: setMouseCursor (juce::MouseCursor::IBeamCursor); break;
        case EditSettings::Tool::grabber:  setMouseCursor (hit ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); break;
        case EditSettings::Tool::scrubber: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case EditSettings::Tool::pencil:   setMouseCursor (hit && pencilZoomOk() ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor); break;
        case EditSettings::Tool::smart:    setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void TrackArea::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Zoom around the mouse position.
        const double anchorSeconds = xToSeconds ((float) e.x);
        pixelsPerSecond = juce::jlimit (5.0, 400000.0, pixelsPerSecond * (1.0 + wheel.deltaY));
        viewStartSeconds = juce::jmax (0.0, anchorSeconds - (e.x - theme::trackHeaderWidth) / pixelsPerSecond);
    }
    else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY) || contentHeight() <= getHeight() - theme::rulerHeight)
    {
        // Shift+wheel (or a sideways wheel, or nothing to scroll vertically): time
        const double delta = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY) * -200.0 / pixelsPerSecond;
        viewStartSeconds = juce::jmax (0.0, viewStartSeconds + delta);
    }
    else
    {
        setScrollY (scrollY - juce::roundToInt (wheel.deltaY * 120.0));   // the track list
        return;
    }
    repaint();
}

//==============================================================================
// Spot dialog

void TrackArea::showSpotDialog (const model::ClipRef& ref)
{
    auto t = model::ClipEdits::timing (session, ref);
    if (! t) return;
    const auto bb = transport.barBeatForSeconds ((double) t->start / t->sampleRate);

    auto* window = new juce::AlertWindow ("Spot Clip", "New start for \"" + t->name + "\" (bar|beat, e.g. 3|2, or seconds with an s, e.g. 4.5s):",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("pos", juce::String (bb.bar) + "|" + juce::String (bb.beat), "Start");
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, ref, window] (int result)
    {
        if (result == 1)
        {
            const auto text = window->getTextEditorContents ("pos").trim();
            double seconds = -1.0;
            if (text.endsWithIgnoreCase ("s"))
                seconds = text.dropLastCharacters (1).getDoubleValue();
            else
            {
                const int bar = text.upToFirstOccurrenceOf ("|", false, false).getIntValue();
                const int beat = text.contains ("|") ? text.fromFirstOccurrenceOf ("|", false, false).getIntValue() : 1;
                if (bar >= 1) seconds = transport.beatsToSeconds ((bar - 1) * transport.getBeatsPerBar() + (juce::jmax (1, beat) - 1));
            }
            if (seconds >= 0.0)
                executeWithShuffle (std::make_unique<model::MoveClipCommand> (ref, ref.track, toSamples (seconds)), "Spot Clip", { ref.track });
        }
    }), true);
}

//==============================================================================
// Selection operations

void TrackArea::deleteSelection()
{
    if (! selectedClips.empty())
    {
        auto compound = std::make_unique<model::CompoundCommand> ("Delete Clips");
        // Edit groups: same-start clips on member tracks go too.
        auto refs = selectedClips;
        for (const auto& ref : selectedClips)
            for (const auto& sib : model::GroupLogic::siblingsOf (session, ref))
                if (std::find (refs.begin(), refs.end(), sib) == refs.end()) refs.push_back (sib);
        // Remove highest indices first within each track so indices stay valid.
        std::sort (refs.begin(), refs.end(), [] (const model::ClipRef& a, const model::ClipRef& b)
                   { return a.track != b.track ? a.track > b.track : a.index > b.index; });
        std::vector<int> tracks;
        for (const auto& r : refs) { compound->add (std::make_unique<model::RemoveAnyClipCommand> (r)); tracks.push_back (r.track); }
        if (edit.mode == EditSettings::Mode::shuffle)
            for (int t : tracks) compound->add (std::make_unique<model::RepackTrackCommand> (t));
        selectedClips.clear();
        session.execute (std::move (compound));
        return;
    }

    if (timeSelection.isValid())
    {
        // Clear: split at both boundaries, then remove what lies inside.
        auto compound = std::make_unique<model::CompoundCommand> ("Clear Selection");
        const auto s0 = toSamples (timeSelection.start), s1 = toSamples (timeSelection.end);
        for (int track = timeSelection.firstTrack; track <= timeSelection.lastTrack; ++track)
        {
            // Work on a scratch copy of the session state is not possible; instead
            // execute step by step through the compound in a deterministic order.
            for (const auto& ref : model::ClipEdits::allClips (session, track))
            {
                auto t = model::ClipEdits::timing (session, ref);
                if (! t) continue;
                const auto cStart = t->start, cEnd = t->start + t->length;
                if (cEnd <= s0 || cStart >= s1) continue;

                if (cStart < s0 && cEnd > s1)
                {
                    // Clip spans the whole range: shorten it and add the tail after the range.
                    compound->add (std::make_unique<model::SplitClipCommand> (ref, s1));   // tail appended
                    compound->add (std::make_unique<model::TrimClipCommand> (ref, cStart, s0 - cStart));
                }
                else if (cStart < s0)
                    compound->add (std::make_unique<model::TrimClipCommand> (ref, cStart, s0 - cStart));
                else if (cEnd > s1)
                    compound->add (std::make_unique<model::TrimClipCommand> (ref, s1, cEnd - s1));
                else
                    compound->add (std::make_unique<model::RemoveAnyClipCommand> (ref));
            }
        }
        // Removing by index inside a compound is only safe if removals come last
        // and in descending order; the loop above visits refs in ascending order,
        // so rebuild: execute non-removals first, then removals descending.
        if (! compound->isEmpty())
        {
            if (edit.mode == EditSettings::Mode::shuffle)
                for (int t = timeSelection.firstTrack; t <= timeSelection.lastTrack; ++t)
                    compound->add (std::make_unique<model::RepackTrackCommand> (t));
            session.execute (std::move (compound));
        }
        setTimeSelection ({});
    }
}

void TrackArea::separateAtPlayhead()
{
    const auto at = toSamples (transport.getPositionSeconds());
    auto compound = std::make_unique<model::CompoundCommand> ("Separate Clip");

    if (timeSelection.isValid())
    {
        const auto s0 = toSamples (timeSelection.start), s1 = toSamples (timeSelection.end);
        for (int track = timeSelection.firstTrack; track <= timeSelection.lastTrack; ++track)
        {
            if (auto ref = model::ClipEdits::clipAt (session, track, s1)) compound->add (std::make_unique<model::SplitClipCommand> (*ref, s1));
            if (auto ref = model::ClipEdits::clipAt (session, track, s0)) compound->add (std::make_unique<model::SplitClipCommand> (*ref, s0));
        }
    }
    else if (! selectedClips.empty())
    {
        for (const auto& ref : selectedClips) compound->add (std::make_unique<model::SplitClipCommand> (ref, at));
    }
    else
    {
        for (int track = 0; track < session.getNumTracks(); ++track)
            if (auto ref = model::ClipEdits::clipAt (session, track, at))
                compound->add (std::make_unique<model::SplitClipCommand> (*ref, at));
    }

    if (! compound->isEmpty()) { selectedClips.clear(); session.execute (std::move (compound)); }
}

void TrackArea::duplicateSelectedClips()
{
    if (selectedClips.empty()) return;
    auto compound = std::make_unique<model::CompoundCommand> ("Duplicate");
    std::vector<int> tracks;
    for (const auto& ref : selectedClips) { compound->add (std::make_unique<model::DuplicateClipCommand> (ref)); tracks.push_back (ref.track); }
    if (edit.mode == EditSettings::Mode::shuffle) for (int t : tracks) compound->add (std::make_unique<model::RepackTrackCommand> (t));
    session.execute (std::move (compound));
}

void TrackArea::nudgeSelectedClips (int direction)
{
    if (selectedClips.empty()) return;
    const double delta = gridSeconds() * direction;
    auto compound = std::make_unique<model::CompoundCommand> ("Nudge");
    for (const auto& ref : selectedClips)
        if (auto t = model::ClipEdits::timing (session, ref))
            compound->add (std::make_unique<model::MoveClipCommand> (ref, ref.track, t->start + toSamples (delta)));
    session.execute (std::move (compound));
}

void TrackArea::nudgeClipGain (float deltaDb)
{
    auto compound = std::make_unique<model::CompoundCommand> ("Clip Gain");
    for (const auto& ref : selectedClips)
        if (ref.kind == model::ClipRef::Kind::audio)
            if (auto* t = session.getTrack (ref.track); t != nullptr && ref.index < (int) t->clips.size())
            {
                const float db = juce::Decibels::gainToDecibels (t->clips[(size_t) ref.index].gain, -60.0f) + deltaDb;
                compound->add (std::make_unique<model::SetClipGainCommand> (ref, juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 12.0f, db), -60.0f)));
            }
    if (! compound->isEmpty()) session.execute (std::move (compound));
}

int TrackArea::numSelectedAudioClips() const
{
    int n = 0;
    for (const auto& r : selectedClips) n += r.kind == model::ClipRef::Kind::audio ? 1 : 0;
    return n;
}

std::optional<TrackArea::FadeValues> TrackArea::currentFadeValues() const
{
    for (const auto& ref : selectedClips)
        if (ref.kind == model::ClipRef::Kind::audio)
            if (auto* t = session.getTrack (ref.track); t != nullptr && ref.index < (int) t->clips.size())
            {
                const auto& c = t->clips[(size_t) ref.index];
                FadeValues v;
                v.fadeInMs = c.fadeIn * 1000.0 / c.sampleRate;
                v.fadeOutMs = c.fadeOut * 1000.0 / c.sampleRate;
                v.inShape = c.fadeInShape; v.outShape = c.fadeOutShape;
                v.gainDb = juce::Decibels::gainToDecibels (c.gain, -60.0f);
                return v;
            }
    return std::nullopt;
}

void TrackArea::applyFadesToSelection (const FadeValues& v)
{
    auto compound = std::make_unique<model::CompoundCommand> (numSelectedAudioClips() > 1 ? "Batch Fades" : "Fades");
    for (const auto& ref : selectedClips)
        if (ref.kind == model::ClipRef::Kind::audio)
            if (auto* t = session.getTrack (ref.track); t != nullptr && ref.index < (int) t->clips.size())
            {
                const double sr = t->clips[(size_t) ref.index].sampleRate;
                compound->add (std::make_unique<model::SetClipFadesCommand> (ref, (juce::int64) std::llround (v.fadeInMs * sr / 1000.0), v.inShape,
                                                                           (juce::int64) std::llround (v.fadeOutMs * sr / 1000.0), v.outShape));
                compound->add (std::make_unique<model::SetClipGainCommand> (ref, juce::Decibels::decibelsToGain (v.gainDb, -60.0f)));
            }
    if (! compound->isEmpty()) session.execute (std::move (compound));
}

void TrackArea::zoomToFit()
{
    const double len = juce::jmax (4.0, session.getLengthSeconds() * 1.05);
    viewStartSeconds = 0.0;
    pixelsPerSecond = juce::jlimit (5.0, 2000.0, (getWidth() - theme::trackHeaderWidth) / len);
    repaint();
}

bool TrackArea::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (key == juce::KeyPress::escapeKey)                                        { clearSelection(); return true; }
    if (key == juce::KeyPress ('e', juce::ModifierKeys::commandModifier, 0))    { separateAtPlayhead(); return true; }
    if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0)) { compSelectionToMain(); return true; }
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))    { duplicateSelectedClips(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::upKey,   juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { nudgeClipGain (0.5f); return true; }
    if (key == juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { nudgeClipGain (-0.5f); return true; }
    if (key == juce::KeyPress (','))                                             { nudgeSelectedClips (-1); return true; }
    if (key == juce::KeyPress ('.'))                                             { nudgeSelectedClips (1); return true; }
    if (key == juce::KeyPress (juce::KeyPress::tabKey, 0, 0))                    { tabToTransient (true); return true; }
    if (key == juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)) { tabToTransient (false); return true; }
    if (key == juce::KeyPress ('q', juce::ModifierKeys::altModifier, 0))         { quantizeSelection(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::upKey, 0, 0))                     { selectTrackByOffset (-1); return true; }
    if (key == juce::KeyPress (juce::KeyPress::downKey, 0, 0))                   { selectTrackByOffset (1); return true; }
    if (key == juce::KeyPress (juce::KeyPress::leftKey, 0, 0))                   { stepPlayhead (-1, false); return true; }
    if (key == juce::KeyPress (juce::KeyPress::rightKey, 0, 0))                  { stepPlayhead (1, false); return true; }
    if (key == juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0))  { stepPlayhead (-1, true); return true; }
    if (key == juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)) { stepPlayhead (1, true); return true; }
    if (key == juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0))
    {
        // Select every clip of the selected track
        selectedClips.clear();
        for (const auto& r : model::ClipEdits::allClips (session, selectedTrack)) selectedClips.push_back (r);
        juce::AccessibilityHandler::postAnnouncement (describeSelection(), juce::AccessibilityHandler::AnnouncementPriority::medium);
        repaint();
        return true;
    }
    if (key == juce::KeyPress ('m', 0, 0))                                        { addMarkerAtPlayhead (false); return true; }
    if (key == juce::KeyPress ('m', juce::ModifierKeys::shiftModifier, 0))        { addMarkerAtPlayhead (true); return true; }
    for (int n = 1; n <= 9; ++n)
        if (key == juce::KeyPress ((juce::juce_wchar) ('0' + n), juce::ModifierKeys::altModifier, 0)) { recallMarker (n); return true; }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAllClips();
        return true;
    }
    return false;
}

//==============================================================================
// Overlays

void TrackArea::paintEditOverlays (juce::Graphics& g)
{
    // Time selection (alternate-lane selections are drawn inside their lane)
    if ((timeSelection.isValid() || drag == Drag::select) && ! timeSelection.isOnAlternate())
    {
        const float x1 = secondsToX (timeSelection.start), x2 = secondsToX (timeSelection.end);
        const int t0 = juce::jmax (0, timeSelection.firstTrack), t1 = juce::jmax (t0, timeSelection.lastTrack);
        const int y0 = getLaneBounds (t0).getY(), y1 = getLaneBounds (t1).getBottom();
        if (x2 > x1)
        {
            g.setColour (theme::accent.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (x1, (float) y0, x2 - x1, (float) (y1 - y0)));
            g.setColour (theme::accent.withAlpha (0.7f));
            g.drawRect (juce::Rectangle<float> (x1, (float) y0, x2 - x1, (float) (y1 - y0)), 1.0f);
            // Ruler marker
            g.fillRect (juce::Rectangle<float> (x1, 2.0f, x2 - x1, 5.0f));
        }
    }

    // Ghost of the clip being moved/trimmed
    if ((drag == Drag::move || drag == Drag::trimStart || drag == Drag::trimEnd) && dragMoved)
    {
        auto lane = getLaneBounds (dragTargetTrack >= 0 ? dragTargetTrack : dragClip.track);
        auto r = clipRectFor (ghostStart, ghostStart + ghostLength, lane);
        g.setColour (theme::text.withAlpha (0.15f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::text.withAlpha (0.9f));
        g.drawRoundedRectangle (r, 4.0f, 1.5f);

        const auto bb = transport.barBeatForSeconds (ghostStart);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (bb.bar) + "|" + juce::String (bb.beat) + "|" + juce::String (bb.tick).paddedLeft ('0', 3),
                    r.withHeight (16.0f).translated (0.0f, -18.0f).toNearestInt(), juce::Justification::centredLeft);
    }

    // Fade handle ghost
    if ((drag == Drag::fadeIn || drag == Drag::fadeOut) && dragMoved)
    {
        auto r = rectForClip (dragClip);
        const float w = (float) (ghostFadeSeconds * pixelsPerSecond);
        auto fr = drag == Drag::fadeIn ? r.withWidth (w) : r.withLeft (r.getRight() - w);
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillRect (fr);
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (juce::roundToInt (ghostFadeSeconds * 1000.0)) + " ms",
                    r.withHeight (16.0f).translated (0.0f, -18.0f).toNearestInt(),
                    drag == Drag::fadeIn ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }

    // Clip gain ghost
    if (drag == Drag::clipGain && dragMoved)
    {
        auto r = rectForClip (dragClip);
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Clip gain " + juce::String (ghostGainDb, 1) + " dB", r.withHeight (16.0f).translated (0.0f, -18.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }

    // Zoom range
    if (drag == Drag::zoomRange && dragMoved)
    {
        const float x1 = secondsToX (dragAnchorSeconds), x2 = (float) getMouseXYRelative().x;
        g.setColour (theme::accent.withAlpha (0.2f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (x1, x2), (float) theme::rulerHeight, std::abs (x2 - x1), (float) getHeight()));
    }
}

void TrackArea::timerCallback()
{
    // Repaint only what changed: the playhead's columns, or everything when the
    // view scrolled or a recording is drawing new audio.
    const double before = viewStartSeconds;
    if (transport.isPlaying()) ensurePlayheadVisible();
    const bool recording = getRecordStartSeconds != nullptr && ! liveThumbnails.empty();
    if (std::abs (viewStartSeconds - before) > 1.0e-12 || recording) { repaint(); lastPlayheadPaintX = -1; return; }
    const int x = (int) secondsToX (transport.getPositionSeconds());
    if (x != lastPlayheadPaintX)
    {
        if (lastPlayheadPaintX >= 0) repaint (lastPlayheadPaintX - 8, 0, 17, getHeight());
        repaint (x - 8, 0, 17, getHeight());
        lastPlayheadPaintX = x;
    }
}

bool TrackArea::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (formatManager.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void TrackArea::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragHover = false;
    repaint();

    int trackIndex = trackIndexAtY (y);
    if (auto* t = session.getTrack (trackIndex); t != nullptr && t->isInstrument())
        trackIndex = -1;

    if (onFilesDropped)
        onFilesDropped (files, trackIndex, xToSeconds ((float) juce::jmax (x, theme::trackHeaderWidth)));
}

bool TrackArea::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith ("loop:");
}

void TrackArea::itemDropped (const SourceDetails& details)
{
    dragHover = false;
    repaint();

    const juce::File file (details.description.toString().fromFirstOccurrenceOf ("loop:", false, false));
    int trackIndex = trackIndexAtY (details.localPosition.y);
    if (auto* t = session.getTrack (trackIndex); t != nullptr && t->isInstrument())
        trackIndex = -1;

    if (onLoopDropped)
        onLoopDropped (file, trackIndex, xToSeconds ((float) juce::jmax (details.localPosition.x, theme::trackHeaderWidth)));
}

} // namespace beatmaker::ui

//==============================================================================
// Elastic audio

namespace beatmaker::ui
{

void TrackArea::applyElastic (const model::ClipRef& ref, engine::StretchSpec spec, const juce::String& name)
{
    auto cmd = std::make_unique<model::SetClipElasticCommand> (session, ref, std::move (spec), name, /*deferRender*/ true);
    if (cmd->wasCancelled()) { if (onStatus) onStatus (name + ": nothing to render"); return; }
    const bool background = cmd->getSampleRate() > 0.0 && (double) cmd->getSourceLength() / cmd->getSampleRate() > ElasticJob::asyncThresholdSeconds;
    ElasticJob::run (session, std::move (cmd), [this, ref, name, background] (bool applied)
    {
        if (! applied) { if (onStatus) onStatus (name + ": cancelled"); return; }
        if (const auto* t = session.getTrack (ref.track); t != nullptr && ref.index < (int) t->clips.size())
        {
            const auto& c = t->clips[(size_t) ref.index];
            if (onStatus)
                onStatus (name + ": " + c.name + "  " + engine::TimeStretch::modeName (c.elastic.mode)
                          + (c.isElastic() ? "  " + juce::String (juce::roundToInt (100.0 / c.elastic.ratio)) + "%"
                                             + (std::abs (c.elastic.pitchSemitones) > 1e-6 ? "  " + juce::String (c.elastic.pitchSemitones, 0) + " st" : juce::String())
                                             + "  " + juce::String (c.elastic.markers.size()) + " warp markers"
                                           : juce::String())
                          + (background ? "  (rendered in the background)" : juce::String()));
        }
        repaint();
    });
}

int TrackArea::warpMarkerAt (const model::ClipRef& ref, juce::Point<int> p) const
{
    const auto* t = session.getTrack (ref.track);
    if (t == nullptr || ref.kind != model::ClipRef::Kind::audio || ref.index >= (int) t->clips.size()) return -1;
    const auto& clip = t->clips[(size_t) ref.index];
    if (! clip.isElastic()) return -1;
    const auto wave = waveAreaFor (ref);
    if (p.y < wave.getY() || p.y > wave.getY() + 14.0f) return -1;   // handles live along the top of the waveform
    for (int i = 0; i < (int) clip.elastic.markers.size(); ++i)
    {
        const juce::int64 rendered = clip.elastic.markers[(size_t) i].output;
        if (rendered < clip.sourceOffset || rendered > clip.sourceOffset + clip.length) continue;
        const float x = secondsToX (clip.getStartSeconds() + (double) (rendered - clip.sourceOffset) / clip.sampleRate);
        if (std::abs (x - (float) p.x) <= 6.0f) return i;
    }
    return -1;
}

void TrackArea::joinSelectedClips()
{
    const auto groups = model::ClipJoin::groups (session, selectedClips);
    auto compound = std::make_unique<model::CompoundCommand> ("Join Clips");
    int joined = 0; juce::String reason;
    for (const auto& g : groups)
    {
        if (g.size() < 2) continue;
        if (! model::ClipJoin::canJoin (session, g, reason)) continue;
        auto cmd = std::make_unique<model::JoinClipsCommand> (session, g);
        if (! cmd->wasCancelled()) { compound->add (std::move (cmd)); ++joined; }
    }
    if (joined == 0) { if (onStatus) onStatus (reason.isNotEmpty() ? "Join: " + reason : "Join: select two or more clips on one track"); return; }
    selectedClips.clear();
    session.execute (std::move (compound));
    if (onStatus) onStatus ("Joined " + juce::String (joined) + " group(s) of clips into one clip each");
}

int TrackArea::addClipForSelection (double fallbackSeconds)
{
    const bool useSelection = timeSelection.isValid();
    int first = timeSelection.firstTrack, last = timeSelection.lastTrack;
    if (! useSelection) { first = last = selectedTrack; }
    if (first < 0) { if (onStatus) onStatus ("Select an instrument track (or a time range on one) to add a clip"); return 0; }
    const double bpb = (double) juce::jmax (1, transport.getBeatsPerBar());
    double start = useSelection ? timeSelection.start : (fallbackSeconds >= 0.0 ? fallbackSeconds : transport.getPositionSeconds());
    double end = useSelection ? timeSelection.end : transport.beatsToSeconds (std::floor (transport.secondsToBeats (start) / bpb) * bpb + bpb);
    if (! useSelection) start = transport.beatsToSeconds (std::floor (transport.secondsToBeats (start) / bpb) * bpb);
    if (end <= start) return 0;
    auto compound = std::make_unique<model::CompoundCommand> ("Add Clip");
    int made = 0;
    for (int i = juce::jmax (0, first); i <= juce::jmin (session.getNumTracks() - 1, last); ++i)
    {
        const auto& t = session.getTracks()[(size_t) i];
        if (! t.isInstrument()) continue;
        const double sr = (double) toSamples (1.0);   // the engine rate
        if (t.isDrumMachine())
        {
            model::PatternClip c; c.name = "Beat " + juce::String (t.patternClips.size() + 1); c.sampleRate = sr;
            c.timelineStart = (juce::int64) std::llround (start * sr); c.length = juce::jmax<juce::int64> (1, (juce::int64) std::llround ((end - start) * sr));
            auto p = std::make_shared<engine::StepPattern>(); p->numSteps = juce::jlimit (1, engine::StepPattern::maxSteps, (int) std::round (transport.secondsToBeats (end - start) * p->stepsPerBeat));
            c.pattern = p; c.loop = false;
            compound->add (std::make_unique<model::AddPatternClipCommand> (i, std::move (c)));
        }
        else
        {
            model::MidiClip c; c.name = "Clip " + juce::String (t.midiClips.size() + 1); c.sampleRate = sr;
            c.timelineStart = (juce::int64) std::llround (start * sr); c.length = juce::jmax<juce::int64> (1, (juce::int64) std::llround ((end - start) * sr));
            auto seq = std::make_shared<engine::MidiSequence>(); seq->lengthBeats = juce::jmax (0.25, transport.secondsToBeats (end - start));
            c.sequence = seq; c.loop = false;
            compound->add (std::make_unique<model::AddMidiClipCommand> (i, std::move (c)));
        }
        ++made;
    }
    if (made == 0) { if (onStatus) onStatus ("Add Clip: the selection covers no instrument track"); return 0; }
    session.execute (std::move (compound));
    if (onStatus) onStatus ("Added " + juce::String (made) + " clip(s) from " + juce::String (start, 2) + " s to " + juce::String (end, 2) + " s");
    return made;
}

// Loop settings of a pattern or MIDI clip: on/off, and how far the loop goes (the clip's length).
void TrackArea::showLoopMenu (const model::ClipRef& ref, juce::Point<int> screenPos)
{
    const auto* t = session.getTrack (ref.track);
    if (t == nullptr) return;
    const bool midi = ref.kind == model::ClipRef::Kind::midi;
    if (midi ? ref.index >= (int) t->midiClips.size() : ref.index >= (int) t->patternClips.size()) return;
    const bool loop = midi ? t->midiClips[(size_t) ref.index].loop : t->patternClips[(size_t) ref.index].loop;
    const double contentBeats = loop ? model::ClipLoop::baseBeats (session, ref) : model::ClipLoop::clipBeats (session, ref);   // what Loop returns the clip to
    const double sr = midi ? t->midiClips[(size_t) ref.index].sampleRate : t->patternClips[(size_t) ref.index].sampleRate;
    const juce::int64 start = midi ? t->midiClips[(size_t) ref.index].timelineStart : t->patternClips[(size_t) ref.index].timelineStart;
    const juce::int64 length = midi ? t->midiClips[(size_t) ref.index].length : t->patternClips[(size_t) ref.index].length;
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const double lengthBars = transport.secondsToBeats ((double) length / sr) / bpb;

    juce::PopupMenu menu, bars;
    menu.addItem (1, loop ? "Loop Clip  (on: switch off to return it to " + juce::String (contentBeats / bpb, 1) + " bars)" : "Loop Clip  (adds one pass, up to the next clip)", true, loop);
    menu.addItem (2, "Play Once (clip as long as its " + juce::String (contentBeats / bpb, 1) + " bars)", contentBeats > 0.0 && loop);
    for (int n : { 1, 2, 4, 8, 16, 32 }) bars.addItem (10 + n, juce::String (n) + (n == 1 ? " bar" : " bars"), true, std::abs (lengthBars - n) < 0.01);
    bars.addItem (60, "Other...");
    menu.addSubMenu ("Loop Length", bars);
    menu.addSeparator();
    menu.addItem (3, "Split at Playhead  (Ctrl+E)");
    menu.addItem (4, "Join Selected Clips  (Ctrl+J)", selectedClips.size() >= 2);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }), [this, ref, midi, loop, contentBeats, sr, start, bpb] (int result)
    {
        if (result == 0) return;
        auto setLength = [this, ref, midi, sr, start, bpb] (double barsWanted)
        {
            const auto newLength = (juce::int64) std::llround (transport.beatsToSeconds (barsWanted * bpb) * sr);
            auto compound = std::make_unique<model::CompoundCommand> ("Loop Length");
            if (! model::ClipLoop::isLooping (session, ref)) compound->add (model::ClipLoop::setLoop (session, ref, true));   // remembers the clip's length first
            compound->add (std::make_unique<model::TrimClipCommand> (ref, start, juce::jmax<juce::int64> (1, newLength)));
            session.execute (std::move (compound));
            if (auto fix = model::ClipLoop::afterTrim (session, ref)) { session.execute (std::move (fix)); if (onStatus) onStatus ("Loop length is the clip's own length: loop off"); }
            else if (onStatus) onStatus ("Loops for " + juce::String (barsWanted, 1) + " bars");
        };
        if (result == 1)
        {
            if (auto cmd = model::ClipLoop::setLoop (session, ref, ! loop)) session.execute (std::move (cmd));
            if (onStatus) onStatus (! loop ? "Looping: one extra pass (drag the clip's right edge for more)" : "Clip back to its own length");
        }
        else if (result == 2) { if (auto cmd = model::ClipLoop::setLoop (session, ref, false)) session.execute (std::move (cmd)); }
        else if (result == 3) { selectedClips = { ref }; separateAtPlayhead(); }
        else if (result == 4) joinSelectedClips();
        else if (result >= 11 && result <= 42) setLength (result - 10);
        else if (result == 60)
        {
            auto* w = new juce::AlertWindow ("Loop Length", "How many bars should the clip loop for?", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("bars", "8", "Bars");
            w->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([w, setLength] (int r) { if (r == 1) { const double b = w->getTextEditorContents ("bars").getDoubleValue(); if (b > 0.0) setLength (b); } }), true);
        }
    });
}

void TrackArea::showClipMenu (const model::ClipRef& ref, juce::Point<int> screenPos)
{
    const auto* t = session.getTrack (ref.track);
    if (t == nullptr || ref.index >= (int) t->clips.size()) return;
    const auto& clip = t->clips[(size_t) ref.index];
    const bool elastic = clip.isElastic();

    juce::PopupMenu menu, modes, pitch;
    int id = 100;
    for (auto m : { engine::StretchMode::off, engine::StretchMode::polyphonic, engine::StretchMode::rhythmic,
                    engine::StretchMode::monophonic, engine::StretchMode::varispeed })
        modes.addItem (id++, engine::TimeStretch::modeName (m), true, clip.elastic.mode == m);
    menu.addSubMenu ("Elastic Audio", modes);

    for (int st = 12; st >= -12; --st)
        pitch.addItem (300 + st, (st > 0 ? "+" : "") + juce::String (st) + (st == 0 ? "  (none)" : st == 12 || st == -12 ? "  (octave)" : ""),
                       true, std::abs (clip.elastic.pitchSemitones - st) < 0.5 && elastic);
    menu.addSubMenu ("Pitch Shift", pitch, clip.elastic.mode != engine::StretchMode::varispeed);

    menu.addSeparator();
    menu.addItem (1, "Conform to Session Tempo" + (clip.sourceBpm > 0.0 ? "  (" + juce::String (juce::roundToInt (clip.sourceBpm)) + " -> "
                                                                             + juce::String (juce::roundToInt (transport.getBpm())) + " BPM)" : juce::String ("  (source tempo unknown)")),
                  clip.sourceBpm > 0.0);
    menu.addItem (2, "Quantize to Grid  (Alt+Q)");
    menu.addItem (3, "Add Warp Marker Here", true);
    menu.addItem (4, "Clear Warp Markers", elastic && ! clip.elastic.markers.empty());
    menu.addSeparator();
    menu.addItem (5, "Separate at Transients");
    menu.addItem (7, "Beat Detective...  (Ctrl+8)");
    menu.addItem (6, "Reset Elastic (original audio)", elastic);
    menu.addSeparator();
    menu.addItem (8, "Split at Playhead  (Ctrl+E)");
    menu.addItem (9, "Join Selected Clips  (Ctrl+J)", selectedClips.size() >= 2);

    const double anchor = dragAnchorSeconds;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }), [this, ref, anchor] (int result)
    {
        const auto* track = session.getTrack (ref.track);
        if (result == 0 || track == nullptr || ref.index >= (int) track->clips.size()) return;
        const auto& c = track->clips[(size_t) ref.index];

        if (result >= 100 && result < 105)
        {
            const engine::StretchMode m[] = { engine::StretchMode::off, engine::StretchMode::polyphonic, engine::StretchMode::rhythmic,
                                              engine::StretchMode::monophonic, engine::StretchMode::varispeed };
            applyElastic (ref, model::Elastic::withMode (c, m[result - 100]), "Elastic Mode");
        }
        else if (result >= 288 && result <= 312)
            applyElastic (ref, model::Elastic::withPitch (c, (double) (result - 300)), "Pitch Shift");
        else if (result == 8) { selectedClips = { ref }; separateAtPlayhead(); }
        else if (result == 9) joinSelectedClips();
        else if (result == 1)
            applyElastic (ref, model::Elastic::forTempo (c, transport.getBpm(), c.elastic.isActive() ? c.elastic.mode : engine::StretchMode::polyphonic), "Conform to Tempo");
        else if (result == 2)
        {
            selectedClips = { ref };
            quantizeSelection();
        }
        else if (result == 3)
        {
            const juce::int64 rendered = c.sourceOffset + toSamples ((edit.mode == EditSettings::Mode::grid ? snapSeconds (anchor) : anchor) - c.getStartSeconds());
            if (rendered > c.sourceOffset && rendered < c.sourceOffset + c.length)
                applyElastic (ref, model::Elastic::withMarkerAt (c, rendered), "Add Warp Marker");
        }
        else if (result == 4)
            applyElastic (ref, model::Elastic::withoutMarkers (c), "Clear Warp Markers");
        else if (result == 5)
        {
            if (auto cmd = model::Elastic::separateAtTransients (session, ref))
            {
                selectedClips.clear();
                session.execute (std::move (cmd));
                if (onStatus) onStatus ("Separated at transients");
            }
            else if (onStatus) onStatus ("No transients found in " + c.name);
        }
        else if (result == 6)
            applyElastic (ref, model::Elastic::withMode (c, engine::StretchMode::off), "Reset Elastic");
        else if (result == 7)
        {
            if (onOpenBeatDetective) onOpenBeatDetective();
        }
    });
}

void TrackArea::setTimeSelectionSeconds (double start, double end, int trackIndex)
{
    TimeSelection sel;
    sel.start = start; sel.end = end;
    sel.firstTrack = sel.lastTrack = trackIndex;
    setTimeSelection (sel);
    repaint();
}

std::vector<model::ClipRef> TrackArea::clipsForRhythmEditing() const
{
    std::vector<model::ClipRef> out;
    for (const auto& r : selectedClips) if (r.kind == model::ClipRef::Kind::audio) out.push_back (r);
    if (! out.empty()) return out;

    const auto* t = session.getTrack (selectedTrack);
    if (t == nullptr || ! t->isAudio()) return out;
    for (int i = 0; i < (int) t->clips.size(); ++i)
    {
        const auto& c = t->clips[(size_t) i];
        if (timeSelection.isValid() && (c.getEndSeconds() <= timeSelection.start || c.getStartSeconds() >= timeSelection.end)) continue;
        out.push_back ({ selectedTrack, model::ClipRef::Kind::audio, i });
    }
    return out;
}

void TrackArea::quantizeSelection()
{
    int done = 0;
    for (const auto& ref : std::vector<model::ClipRef> (selectedClips))
    {
        const auto* t = session.getTrack (ref.track);
        if (t == nullptr || ref.kind != model::ClipRef::Kind::audio || ref.index >= (int) t->clips.size()) continue;
        if (auto spec = model::Elastic::quantizeToGrid (t->clips[(size_t) ref.index], transport.getBpm(), edit.gridBeats))
        {
            auto cmd = std::make_unique<model::SetClipElasticCommand> (session, ref, *spec, "Quantize Audio", /*deferRender*/ true);
            if (! cmd->wasCancelled()) { ElasticJob::run (session, std::move (cmd)); ++done; }
        }
    }
    if (onStatus) onStatus (done > 0 ? "Quantized " + juce::String (done) + " clip(s) to the " + juce::String (edit.gridBeats, 2) + "-beat grid"
                                     : "Quantize: select an audio clip with transients");
    repaint();
}

void TrackArea::tabToTransient (bool forward)
{
    const juce::int64 here = toSamples (transport.getPositionSeconds());
    const int track = selectedTrack >= 0 && session.getTrack (selectedTrack) != nullptr && session.getTrack (selectedTrack)->isAudio() ? selectedTrack : -1;
    if (auto next = model::Elastic::nextTransient (session, track, here + (forward ? 1 : -1), forward))
    {
        const double seconds = (double) *next / transport.getSampleRate();
        transport.setPositionSeconds (seconds);
        timeSelection = {};
        if (onTimeSelectionChanged) onTimeSelectionChanged();
        ensurePlayheadVisible();
        if (onStatus) onStatus ("Transient at " + juce::String (seconds, 3) + " s");
        repaint();
    }
    else if (onStatus) onStatus (forward ? "No transient after the playhead" : "No transient before the playhead");
}

} // namespace beatmaker::ui

//==============================================================================
// Memory locations and arrangement sections

namespace beatmaker::ui
{

void TrackArea::paintMarkerStrip (juce::Graphics& g, juce::Rectangle<int> strip)
{
    g.setColour (theme::background.brighter (0.06f));
    g.fillRect (strip);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));

    // Sections first (blocks), then point markers (flags) on top
    for (const auto& m : session.getMarkers())
    {
        if (! m.isSection || ! m.isRange()) continue;
        const float x1 = juce::jmax ((float) strip.getX(), secondsToX (m.seconds));
        const float x2 = juce::jmin ((float) strip.getRight(), secondsToX (m.endSeconds));
        if (x2 <= x1) continue;
        juce::Rectangle<float> r (x1, (float) strip.getY() + 2.0f, x2 - x1, (float) strip.getHeight() - 4.0f);
        g.setColour (m.colour.withAlpha (0.75f));
        g.fillRoundedRectangle (r.reduced (1.0f, 0.0f), 3.0f);
        g.setColour (m.colour.contrasting (0.9f));
        g.drawText (m.name, r.reduced (5.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
    }
    for (const auto& m : session.getMarkers())
    {
        if (m.isSection) continue;
        const float x = secondsToX (m.seconds);
        if (x < strip.getX() - 2.0f || x > strip.getRight()) continue;
        juce::Path flag;
        flag.addTriangle (x, (float) strip.getY() + 2.0f, x + 8.0f, (float) strip.getY() + 7.0f, x, (float) strip.getY() + 12.0f);
        g.setColour (m.colour);
        g.fillPath (flag);
        g.drawVerticalLine ((int) x, (float) strip.getY() + 2.0f, (float) strip.getBottom());
        g.setColour (theme::text);
        g.drawText (juce::String (m.id) + " " + m.name, (int) x + 10, strip.getY() + 1, 160, strip.getHeight() - 2, juce::Justification::centredLeft, true);
    }
    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (strip.getBottom() - 1, (float) strip.getX(), (float) strip.getRight());
}

int TrackArea::markerAtX (int x) const
{
    // Point markers win over sections (their flags sit on top)
    for (const auto& m : session.getMarkers())
        if (! m.isSection && std::abs (secondsToX (m.seconds) - (float) x) <= 10.0f) return m.id;
    for (const auto& m : session.getMarkers())
        if (m.isSection && m.isRange() && x >= secondsToX (m.seconds) && x < secondsToX (m.endSeconds)) return m.id;
    return -1;
}

void TrackArea::recallMarker (int markerId)
{
    const auto* m = session.getMarker (markerId);
    if (m == nullptr) return;
    transport.setPositionSeconds (m->seconds);
    if (m->recallSelection)
    {
        timeSelection = {};
        timeSelection.start = m->selectionStart; timeSelection.end = m->selectionEnd;
        timeSelection.firstTrack = 0; timeSelection.lastTrack = juce::jmax (0, session.getNumTracks() - 1);
        setTimeSelection (timeSelection);
    }
    else if (m->isSection && m->isRange())
    {
        timeSelection = {};
        timeSelection.start = m->seconds; timeSelection.end = m->endSeconds;
        timeSelection.firstTrack = 0; timeSelection.lastTrack = juce::jmax (0, session.getNumTracks() - 1);
        setTimeSelection (timeSelection);
    }
    if (m->recallZoom) setView (m->viewStartSeconds, m->pixelsPerSecond);
    if (onTimeSelectionChanged) onTimeSelectionChanged();
    ensurePlayheadVisible();
    if (onStatus) onStatus ("Memory location " + juce::String (m->id) + ": " + m->name);
    repaint();
}

void TrackArea::addMarkerAtPlayhead (bool asSectionFromSelection)
{
    model::Marker m;
    static const juce::Colour palette[] = { juce::Colour (0xffe6b422), juce::Colour (0xff3498db), juce::Colour (0xff2ecc71), juce::Colour (0xffe67e22), juce::Colour (0xff9b59b6), juce::Colour (0xff1abc9c) };
    m.colour = palette[session.getMarkers().size() % 6];
    if (asSectionFromSelection)
    {
        if (! timeSelection.isValid()) { if (onStatus) onStatus ("Select a time range first, then Shift+M adds a section"); return; }
        m.isSection = true; m.seconds = timeSelection.start; m.endSeconds = timeSelection.end;
        m.name = "Section " + juce::String (session.getSections().size() + 1);
    }
    else
    {
        m.seconds = transport.getPositionSeconds();
        m.name = "Marker " + juce::String (session.getMarkers().size() + 1);
        m.recallSelection = timeSelection.isValid();
        m.selectionStart = timeSelection.start; m.selectionEnd = timeSelection.end;
    }
    auto cmd = std::make_unique<model::AddMarkerCommand> (m);
    auto* raw = cmd.get();
    session.execute (std::move (cmd));
    promptMarkerName (raw->getMarkerId());
}

void TrackArea::promptMarkerName (int markerId)
{
    const auto* m = session.getMarker (markerId);
    if (m == nullptr) return;
    auto* window = new juce::AlertWindow (m->isSection ? "Section" : "Memory Location " + juce::String (m->id), "Name:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", m->name);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->getTextEditor ("name")->selectAll();
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, markerId, window] (int result)
    {
        if (result == 1)
            if (const auto* current = session.getMarker (markerId))
            {
                auto updated = *current;
                updated.name = window->getTextEditorContents ("name").trim();
                if (updated.name.isNotEmpty() && updated.name != current->name) session.execute (std::make_unique<model::ReplaceMarkerCommand> (updated, "Rename Memory Location"));
            }
        grabKeyboardFocus();
    }), true);
}

void TrackArea::showMarkerMenu (int markerId, double seconds, juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    const auto* m = session.getMarker (markerId);
    if (m != nullptr)
    {
        menu.addItem (1, "Recall  (" + juce::String (m->name) + ")");
        menu.addItem (2, "Rename...");
        if (m->isSection)
        {
            menu.addSeparator();
            menu.addItem (10, "Move Section Earlier");
            menu.addItem (11, "Move Section Later");
            menu.addItem (12, "Duplicate Section");
            menu.addItem (13, "Delete Section and its Time");
        }
        else
        {
            menu.addItem (3, "Recall selection", true, m->recallSelection);
            menu.addItem (4, "Recall zoom", true, m->recallZoom);
            menu.addItem (5, "Store current selection and zoom");
        }
        menu.addSeparator();
        menu.addItem (6, "Delete");
    }
    else
    {
        menu.addItem (20, "Add Memory Location here  (M at playhead)");
        menu.addItem (21, "Add Section from selection  (Shift+M)", timeSelection.isValid());
    }
    menu.addSeparator();
    menu.addItem (30, "Memory Locations window...  (Ctrl+5)");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }), [this, markerId, seconds] (int result)
    {
        if (result == 0) return;
        const auto* cur = session.getMarker (markerId);
        if (result == 1 && cur) recallMarker (markerId);
        else if (result == 2 && cur) promptMarkerName (markerId);
        else if ((result == 3 || result == 4 || result == 5) && cur)
        {
            auto updated = *cur;
            if (result == 3) updated.recallSelection = ! updated.recallSelection;
            if (result == 4) updated.recallZoom = ! updated.recallZoom;
            if (result == 5)
            {
                updated.recallSelection = timeSelection.isValid(); updated.selectionStart = timeSelection.start; updated.selectionEnd = timeSelection.end;
                updated.recallZoom = true; updated.viewStartSeconds = viewStartSeconds; updated.pixelsPerSecond = pixelsPerSecond;
            }
            session.execute (std::make_unique<model::ReplaceMarkerCommand> (updated));
        }
        else if (result == 6 && cur) session.execute (std::make_unique<model::RemoveMarkerCommand> (markerId));
        else if (result == 10 || result == 11)
        {
            if (auto cmd = model::Arrangement::moveSection (session, markerId, result == 11)) { selectedClips.clear(); session.execute (std::move (cmd)); }
            else if (onStatus) onStatus ("No section to swap with in that direction");
        }
        else if (result == 12) { if (auto cmd = model::Arrangement::duplicateSection (session, markerId)) { selectedClips.clear(); session.execute (std::move (cmd)); } }
        else if (result == 13) { if (auto cmd = model::Arrangement::deleteSectionTime (session, markerId)) { selectedClips.clear(); session.execute (std::move (cmd)); } }
        else if (result == 20)
        {
            transport.setPositionSeconds (snapSeconds (seconds));
            addMarkerAtPlayhead (false);
        }
        else if (result == 21) addMarkerAtPlayhead (true);
        else if (result == 30) { if (onOpenMemoryLocations) onOpenMemoryLocations(); }
        repaint();
    });
}

void TrackArea::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x >= theme::trackHeaderWidth && e.y < theme::markerStripHeight && markerAtX (e.x) < 0)
    {
        transport.setPositionSeconds (snapSeconds (xToSeconds ((float) e.x)));
        addMarkerAtPlayhead (false);
    }
}

} // namespace beatmaker::ui

//==============================================================================
// Commands Keyboard Focus edits, clipboard, zoom, display preferences

namespace beatmaker::ui
{

void TrackArea::setTrackHeight (int pixels)
{
    trackHeight = juce::jlimit (60, 320, pixels);
    rebuildTrackControls();
    resized();
    repaint();
}

void TrackArea::selectAllClips()
{
    selectedClips.clear();
    for (int t = 0; t < session.getNumTracks(); ++t)
        for (const auto& r : model::ClipEdits::allClips (session, t)) selectedClips.push_back (r);
    repaint();
}

void TrackArea::zoomBy (double factor)
{
    const double anchor = transport.getPositionSeconds();
    const double x = secondsToX (anchor);
    pixelsPerSecond = juce::jlimit (5.0, 400000.0, pixelsPerSecond * factor);
    viewStartSeconds = juce::jmax (0.0, anchor - (x - theme::trackHeaderWidth) / pixelsPerSecond);
    repaint();
}

void TrackArea::zoomToSelection()
{
    if (! timeSelection.isValid()) { zoomToFit(); return; }
    const double width = juce::jmax (50, getWidth() - theme::trackHeaderWidth - 20);
    viewStartSeconds = juce::jmax (0.0, timeSelection.start - 0.02 * (timeSelection.end - timeSelection.start));
    pixelsPerSecond = juce::jlimit (5.0, 400000.0, width / juce::jmax (0.01, (timeSelection.end - timeSelection.start) * 1.04));
    repaint();
}

void TrackArea::trimSelectionToPlayhead (bool start)
{
    const juce::int64 p = toSamples (transport.getPositionSeconds());
    auto compound = std::make_unique<model::CompoundCommand> (start ? "Trim Start to Insertion" : "Trim End to Insertion");
    for (const auto& ref : std::vector<model::ClipRef> (selectedClips))
    {
        const auto t = model::ClipEdits::timing (session, ref);
        if (! t) continue;
        const juce::int64 end = t->start + t->length;
        if (start) { if (p >= end || p == t->start) continue; compound->add (model::GroupLogic::trimCommand (session, ref, p, end - p)); }
        else       { if (p <= t->start || p == end) continue; compound->add (model::GroupLogic::trimCommand (session, ref, t->start, p - t->start)); }
    }
    if (compound->isEmpty()) { if (onStatus) onStatus ("Trim to insertion: put the playhead inside a selected clip"); return; }
    session.execute (std::move (compound));
    repaint();
}

void TrackArea::fadeSelectionToPlayhead (bool fadeIn, engine::FadeShape shape)
{
    const juce::int64 p = toSamples (transport.getPositionSeconds());
    auto compound = std::make_unique<model::CompoundCommand> (fadeIn ? "Fade In to Insertion" : "Fade Out from Insertion");
    for (const auto& ref : selectedClips)
    {
        if (ref.kind != model::ClipRef::Kind::audio) continue;
        const auto* t = session.getTrack (ref.track);
        if (t == nullptr || ref.index >= (int) t->clips.size()) continue;
        const auto& c = t->clips[(size_t) ref.index];
        const juce::int64 end = c.timelineStart + c.length;
        if (p <= c.timelineStart || p >= end) continue;
        if (fadeIn) compound->add (std::make_unique<model::SetClipFadesCommand> (ref, p - c.timelineStart, shape, c.fadeOut, c.fadeOutShape));
        else        compound->add (std::make_unique<model::SetClipFadesCommand> (ref, c.fadeIn, c.fadeInShape, end - p, shape));
    }
    if (compound->isEmpty()) { if (onStatus) onStatus ("Fade to insertion: put the playhead inside a selected audio clip"); return; }
    session.execute (std::move (compound));
    repaint();
}

void TrackArea::applyDefaultFadesToSelection (double ms, engine::FadeShape shape)
{
    auto compound = std::make_unique<model::CompoundCommand> ("Fades");
    for (const auto& ref : selectedClips)
        if (ref.kind == model::ClipRef::Kind::audio)
            if (const auto* t = session.getTrack (ref.track); t != nullptr && ref.index < (int) t->clips.size())
            {
                const auto samples = (juce::int64) std::llround (ms * t->clips[(size_t) ref.index].sampleRate / 1000.0);
                compound->add (std::make_unique<model::SetClipFadesCommand> (ref, samples, shape, samples, shape));
            }
    if (compound->isEmpty()) { if (onStatus) onStatus ("Fades: select audio clips first"); return; }
    session.execute (std::move (compound));
    repaint();
}

void TrackArea::copySelection()
{
    clipboard.clear();
    double first = 1.0e18; int firstTrack = 1 << 30;
    for (const auto& ref : selectedClips)
        if (const auto t = model::ClipEdits::timing (session, ref)) { first = juce::jmin (first, (double) t->start / t->sampleRate); firstTrack = juce::jmin (firstTrack, ref.track); }
    for (const auto& ref : selectedClips)
    {
        const auto* t = session.getTrack (ref.track);
        if (t == nullptr) continue;
        ClipboardItem item; item.kind = ref.kind; item.trackOffset = ref.track - firstTrack;
        if (ref.kind == model::ClipRef::Kind::audio && ref.index < (int) t->clips.size())            { item.audio = t->clips[(size_t) ref.index]; item.relativeStart = item.audio.getStartSeconds() - first; }
        else if (ref.kind == model::ClipRef::Kind::pattern && ref.index < (int) t->patternClips.size()) { item.pattern = t->patternClips[(size_t) ref.index]; item.relativeStart = item.pattern.getStartSeconds() - first; }
        else if (ref.kind == model::ClipRef::Kind::midi && ref.index < (int) t->midiClips.size())    { item.midi = t->midiClips[(size_t) ref.index]; item.relativeStart = item.midi.getStartSeconds() - first; }
        else continue;
        clipboard.push_back (std::move (item));
    }
    if (onStatus) onStatus (clipboard.empty() ? "Copy: select clips first" : "Copied " + juce::String (clipboard.size()) + (clipboard.size() == 1 ? " clip" : " clips"));
}

void TrackArea::cutSelection()
{
    copySelection();
    if (! clipboard.empty()) deleteSelection();
}

void TrackArea::pasteAtPlayhead (bool selectPasted)
{
    if (clipboard.empty()) { if (onStatus) onStatus ("Paste: nothing copied"); return; }
    const int baseTrack = selectedTrack >= 0 ? selectedTrack : 0;
    const double at = transport.getPositionSeconds();
    auto compound = std::make_unique<model::CompoundCommand> ("Paste");
    std::vector<model::ClipRef> pasted;
    std::map<std::pair<int, int>, int> appended;
    for (const auto& item : clipboard)
    {
        const int trackIndex = juce::jlimit (0, juce::jmax (0, session.getNumTracks() - 1), baseTrack + item.trackOffset);
        const auto* t = session.getTrack (trackIndex);
        if (t == nullptr || model::ClipEdits::kindForTrack (*t) != item.kind) continue;
        const int newIndex = model::ClipEdits::numClips (*t, item.kind) + appended[{ trackIndex, (int) item.kind }]++;
        if (item.kind == model::ClipRef::Kind::audio)
        {
            auto c = item.audio; c.timelineStart = (juce::int64) std::llround ((at + item.relativeStart) * c.sampleRate);
            compound->add (std::make_unique<model::AddClipCommand> (trackIndex, std::move (c)));
        }
        else if (item.kind == model::ClipRef::Kind::pattern)
        {
            auto c = item.pattern; c.timelineStart = (juce::int64) std::llround ((at + item.relativeStart) * c.sampleRate);
            compound->add (std::make_unique<model::AddPatternClipCommand> (trackIndex, std::move (c)));
        }
        else
        {
            auto c = item.midi; c.timelineStart = (juce::int64) std::llround ((at + item.relativeStart) * c.sampleRate);
            compound->add (std::make_unique<model::AddMidiClipCommand> (trackIndex, std::move (c)));
        }
        pasted.push_back ({ trackIndex, item.kind, newIndex });
    }
    if (compound->isEmpty()) { if (onStatus) onStatus ("Paste: the target track holds a different kind of clip"); return; }
    session.execute (std::move (compound));
    if (selectPasted) selectedClips = pasted;
    if (onStatus) onStatus ("Pasted " + juce::String (pasted.size()) + (pasted.size() == 1 ? " clip" : " clips") + " at " + juce::String (at, 2) + " s");
    repaint();
}

} // namespace beatmaker::ui

//==============================================================================
// Keyboard navigation and accessibility

namespace beatmaker::ui
{

void TrackArea::selectTrackByOffset (int delta)
{
    if (session.getNumTracks() == 0) return;
    const int next = juce::jlimit (0, session.getNumTracks() - 1, (selectedTrack < 0 ? (delta > 0 ? -1 : session.getNumTracks()) : selectedTrack) + delta);
    setSelectedTrack (next);
    // Keep the selected track in view
    const int top = trackTop (next), bottom = top + trackHeightFor (next);
    juce::ignoreUnused (top, bottom);
    juce::AccessibilityHandler::postAnnouncement (describeSelection(), juce::AccessibilityHandler::AnnouncementPriority::medium);
    repaint();
}

void TrackArea::stepPlayhead (int direction, bool extendSelection)
{
    const double step = gridSeconds() > 0.0 ? gridSeconds() : transport.beatsToSeconds (1.0);
    if (extendSelection)
    {
        if (! timeSelection.isValid())
        {
            timeSelection = {};
            timeSelection.start = timeSelection.end = snapSeconds (transport.getPositionSeconds());
            timeSelection.firstTrack = timeSelection.lastTrack = juce::jmax (0, selectedTrack);
        }
        timeSelection.end = juce::jmax (timeSelection.start, timeSelection.end + direction * step);
        setTimeSelection (timeSelection);
        transport.setPositionSeconds (timeSelection.end);
    }
    else
        transport.setPositionSeconds (juce::jmax (0.0, snapSeconds (transport.getPositionSeconds()) + direction * step));
    ensurePlayheadVisible();
    if (onTimeSelectionChanged) onTimeSelectionChanged();
    repaint();
}

juce::String TrackArea::describeSelection() const
{
    juce::String text;
    const auto bb = transport.barBeatForSeconds (transport.getPositionSeconds());
    text << "Bar " << bb.bar << " beat " << bb.beat << ". ";
    if (const auto* t = session.getTrack (selectedTrack))
    {
        text << "Track " << (selectedTrack + 1) << " " << t->name;
        if (t->mute) text << ", muted";
        if (t->solo) text << ", soloed";
        if (t->armed) text << ", armed";
        if (t->isFrozen()) text << ", frozen";
        text << ". ";
    }
    else text << "No track selected. ";
    if (! selectedClips.empty())
    {
        text << selectedClips.size() << (selectedClips.size() == 1 ? " clip selected" : " clips selected");
        if (const auto t = model::ClipEdits::timing (session, selectedClips[0])) text << ": " << t->name << " at " << juce::String ((double) t->start / t->sampleRate, 2) << " seconds";
        text << ". ";
    }
    if (timeSelection.isValid()) text << "Selection " << juce::String (timeSelection.start, 2) << " to " << juce::String (timeSelection.end, 2) << " seconds. ";
    return text;
}

std::unique_ptr<juce::AccessibilityHandler> TrackArea::createAccessibilityHandler()
{
    struct SelectionValue final : juce::AccessibilityValueInterface
    {
        explicit SelectionValue (TrackArea& a) : area (a) {}
        bool isReadOnly() const override { return true; }
        double getCurrentValue() const override { return area.selectedTrack + 1; }
        juce::String getCurrentValueAsString() const override { return area.describeSelection(); }
        void setValue (double) override {}
        void setValueAsString (const juce::String&) override {}
        AccessibleValueRange getRange() const override { return { { 0.0, (double) area.session.getNumTracks() }, 1.0 }; }
        TrackArea& area;
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group, juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<SelectionValue> (*this) });
}

} // namespace beatmaker::ui
