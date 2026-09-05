#include "TrackArea.h"
#include <GroupLogic.h>
#include <Playlists.h>
#include <dsp/Fades.h>

namespace beatmaker::ui
{

TrackArea::TrackArea (model::Session& s, engine::Transport& t, juce::AudioFormatManager& fm, EditSettings& es)
    : session (s), transport (t), formatManager (fm), edit (es)
{
    session.addListener (this);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (addTrackButton);
    addTrackButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Audio Track");
        menu.addItem (2, "Drum Machine Track");
        menu.addItem (3, "Synth Track");
        menu.addItem (4, "Aux Input");
        menu.addItem (5, "VCA Master");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addTrackButton),
                            [this] (int result)
                            {
                                if (result == 5 && onAddTrack)      onAddTrack (model::Track::Type::vca, model::Track::InstrumentKind::none);
                                else if (result == 4 && onAddTrack) onAddTrack (model::Track::Type::aux, model::Track::InstrumentKind::none);
                                else if (result == 1 && onAddTrack) onAddTrack (model::Track::Type::audio, model::Track::InstrumentKind::none);
                                else if (result == 2 && onAddTrack) onAddTrack (model::Track::Type::instrument, model::Track::InstrumentKind::drumMachine);
                                else if (result == 3 && onAddTrack) onAddTrack (model::Track::Type::instrument, model::Track::InstrumentKind::synth);
                            });
    };
    rebuildTrackControls();
    startTimerHz (30);
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
    return { theme::trackHeaderWidth, 0, getWidth() - theme::trackHeaderWidth, theme::rulerHeight };
}

bool TrackArea::arePlaylistsShown (const model::Track& t) const { return playlistsShown.count (t.id) > 0; }

int TrackArea::trackHeightFor (int i) const
{
    auto* t = session.getTrack (i);
    if (t == nullptr) return theme::trackHeight;
    return theme::trackHeight + (arePlaylistsShown (*t) ? (int) t->alternates.size() * alternateLaneHeight : 0);
}

int TrackArea::trackTop (int i) const
{
    int y = theme::rulerHeight;
    for (int k = 0; k < i; ++k) y += trackHeightFor (k);
    return y;
}

juce::Rectangle<int> TrackArea::getHeaderBounds (int i) const
{
    return { 0, trackTop (i), theme::trackHeaderWidth, theme::trackHeight };
}

juce::Rectangle<int> TrackArea::getLaneBounds (int i) const
{
    return { theme::trackHeaderWidth, trackTop (i), getWidth() - theme::trackHeaderWidth, theme::trackHeight };
}

juce::Rectangle<int> TrackArea::getAlternateLaneBounds (int i, int alternate) const
{
    return { 0, trackTop (i) + theme::trackHeight + alternate * alternateLaneHeight, getWidth(), alternateLaneHeight };
}

int TrackArea::trackIndexAtY (int y) const
{
    if (y < theme::rulerHeight) return -1;
    for (int i = 0; i < session.getNumTracks(); ++i)
        if (y >= trackTop (i) && y < trackTop (i) + trackHeightFor (i)) return i;
    return -1;
}

int TrackArea::alternateAtY (int i, int y) const
{
    auto* t = session.getTrack (i);
    if (t == nullptr || ! arePlaylistsShown (*t)) return -1;
    const int rel = y - (trackTop (i) + theme::trackHeight);
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

        c.mute->setClickingTogglesState (true);
        c.solo->setClickingTogglesState (true);
        c.mute->setToggleState (track.mute, juce::dontSendNotification);
        c.solo->setToggleState (track.solo, juce::dontSendNotification);
        c.mute->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe67e22));
        c.solo->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xfff1c40f));
        c.mute->setTooltip ("Mute");
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
            c.arm->setColour (juce::TextButton::buttonOnColourId, theme::record);
            c.arm->setTooltip ("Record arm");
            c.arm->onClick = [this, i, b = c.arm.get()] { if (onArmChanged) onArmChanged (i, b->getToggleState()); };
            addAndMakeVisible (*c.arm);

            c.monitor = std::make_unique<juce::TextButton> ("I");
            c.monitor->setClickingTogglesState (true);
            c.monitor->setToggleState (track.monitor, juce::dontSendNotification);
            c.monitor->setColour (juce::TextButton::buttonOnColourId, theme::play.darker (0.2f));
            c.monitor->setTooltip ("Input monitor: hear this track's input (watch for feedback with speakers)");
            c.monitor->onClick = [this, i, b = c.monitor.get()] { if (onMonitorChanged) onMonitorChanged (i, b->getToggleState()); };
            addAndMakeVisible (*c.monitor);

            c.input = std::make_unique<juce::ComboBox>();
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
        c.autoView->setTooltip ("Track view: clips, or an automation lane");
        c.autoView->addItem ("Clips", 1);
        c.autoView->addItem ("Volume", 2);
        c.autoView->addItem ("Pan", 3);
        c.autoView->addItem ("Mute", 4);
        for (int sIdx = 0; sIdx < model::Track::numSendSlots; ++sIdx)
            c.autoView->addItem (engine::ParamId::send (sIdx).getName(), 5 + sIdx);
        const auto shown = shownLane (track);
        int viewId = 1;
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
            if (sel <= 1) automationView.erase (trackId);
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

void TrackArea::paintRuler (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::panel);
    g.fillRect (r);

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

    g.setColour (track.armed ? theme::record.brighter (0.2f) : theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    juce::String badges;
    for (const auto* grp : model::GroupLogic::groupsOf (session, track.id)) badges += (grp->active ? " [" : " (") + grp->badge() + (grp->active ? "]" : ")");
    if (track.isAudio() && ! track.alternates.empty())
        badges += "   " + (track.mainPlaylistName.isNotEmpty() ? track.mainPlaylistName : model::defaultPlaylistName (track, 1));
    g.drawText (juce::String (index + 1) + (track.isDrumMachine() ? "  Drum Machine" : track.isSynth() ? "  Synth" : track.isVca() ? "  VCA Master"
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

        g.setColour (track.colour.contrasting (0.9f).withAlpha (0.9f));
        for (int k = 0; k < iterations; ++k)
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
    g.setColour (theme::text);
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

    juce::String label = clip.name;
    if (std::abs (clip.gain - 1.0f) > 1.0e-4f)
        label += "   " + juce::String (juce::Decibels::gainToDecibels (clip.gain), 1) + " dB";
    paintClipFrame (g, clipRect, track, label);
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
        g.setColour (track.colour.contrasting (0.9f).withAlpha (0.9f));

        for (int k = 0; k < totalSteps; ++k)
        {
            const float x = dotArea.getX() + k * stepWidth;
            if (x > r.getRight() || x + stepWidth < r.getX()) continue;
            const int step = k % pattern.numSteps;

            for (int pad = 0; pad < engine::DrumKit::numPads; ++pad)
                if (pattern.get (pad, step) > 0)
                    g.fillRect (x + 0.5f, dotArea.getY() + pad * rowHeight + 0.5f,
                                juce::jmax (1.0f, stepWidth - 1.0f), juce::jmax (1.0f, rowHeight - 1.0f));
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

    if (e.x < theme::trackHeaderWidth) return;

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
    bool nearStart = false, nearEnd = false;
    const auto tool = effectiveTool (e, hit, nearStart, nearEnd);

    if (tool == EditSettings::Tool::zoomer)
    {
        if (e.mods.isAltDown()) { pixelsPerSecond = juce::jmax (5.0, pixelsPerSecond / 2.0); }
        else drag = Drag::zoomRange;
        repaint();
        return;
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
                pixelsPerSecond = juce::jlimit (5.0, 2000.0, (getWidth() - theme::trackHeaderWidth) / std::abs (b - a));
            }
            else
            {
                const double anchor = dragAnchorSeconds;
                pixelsPerSecond = juce::jmin (2000.0, pixelsPerSecond * 2.0);
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
        else
            cmd = model::GroupLogic::trimCommand (session, dragClip, toSamples (ghostStart), toSamples (ghostLength));

        selectedClips.clear();
        executeWithShuffle (std::move (cmd), isMove ? "Move Clip" : "Trim Clip", { dragClip.track, dragTargetTrack });

        // Re-select the edited clip (it may have moved lists)
        const int track = isMove ? dragTargetTrack : dragClip.track;
        if (auto ref = model::ClipEdits::clipAt (session, track, toSamples (ghostStart) + 1))
            selectedClips.push_back (*ref);
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
        case EditSettings::Tool::smart:    setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void TrackArea::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Zoom around the mouse position.
        const double anchorSeconds = xToSeconds ((float) e.x);
        pixelsPerSecond = juce::jlimit (5.0, 2000.0, pixelsPerSecond * (1.0 + wheel.deltaY));
        viewStartSeconds = juce::jmax (0.0, anchorSeconds - (e.x - theme::trackHeaderWidth) / pixelsPerSecond);
    }
    else
    {
        const double delta = (std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY) * -200.0 / pixelsPerSecond;
        viewStartSeconds = juce::jmax (0.0, viewStartSeconds + delta);
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
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectedClips.clear();
        for (int t = 0; t < session.getNumTracks(); ++t)
            for (const auto& r : model::ClipEdits::allClips (session, t)) selectedClips.push_back (r);
        repaint();
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
    if (transport.isPlaying())
        ensurePlayheadVisible();
    repaint();
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
