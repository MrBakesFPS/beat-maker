#include "TrackArea.h"

namespace beatmaker::ui
{

TrackArea::TrackArea (model::Session& s, engine::Transport& t, juce::AudioFormatManager& fm)
    : session (s), transport (t), formatManager (fm)
{
    session.addListener (this);
    addAndMakeVisible (addTrackButton);
    addTrackButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Audio Track");
        menu.addItem (2, "Drum Machine Track");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addTrackButton),
                            [this] (int result)
                            {
                                if (result == 1 && onAddTrack) onAddTrack (model::Track::Type::audio);
                                if (result == 2 && onAddTrack) onAddTrack (model::Track::Type::instrument);
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

juce::Rectangle<int> TrackArea::getHeaderBounds (int i) const
{
    return { 0, theme::rulerHeight + i * theme::trackHeight, theme::trackHeaderWidth, theme::trackHeight };
}

juce::Rectangle<int> TrackArea::getLaneBounds (int i) const
{
    return { theme::trackHeaderWidth, theme::rulerHeight + i * theme::trackHeight,
             getWidth() - theme::trackHeaderWidth, theme::trackHeight };
}

int TrackArea::trackIndexAtY (int y) const
{
    if (y < theme::rulerHeight) return -1;
    const int idx = (y - theme::rulerHeight) / theme::trackHeight;
    return idx < session.getNumTracks() ? idx : -1;
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
        auto header = getHeaderBounds (i).reduced (10);
        auto buttons = header.removeFromBottom (24);
        trackControls[(size_t) i].mute->setBounds (buttons.removeFromLeft (30));
        buttons.removeFromLeft (4);
        trackControls[(size_t) i].solo->setBounds (buttons.removeFromLeft (30));
    }

    const int y = theme::rulerHeight + session.getNumTracks() * theme::trackHeight + 12;
    addTrackButton.setBounds (12, y, theme::trackHeaderWidth - 24, 28);
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
        trackControls.push_back (std::move (c));
    }

    resized();
}

void TrackArea::sessionChanged (model::Session&)
{
    rebuildTrackControls();
    if (selectedTrack >= session.getNumTracks())
        setSelectedTrack (session.getNumTracks() - 1);
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
    }

    paintRuler (g, getRulerBounds());

    // Header column background below the last track
    g.setColour (theme::panelDark);
    g.fillRect (0, theme::rulerHeight + (int) tracks.size() * theme::trackHeight, theme::trackHeaderWidth,
                getHeight());
    g.setColour (theme::gridStrong);
    g.drawVerticalLine (theme::trackHeaderWidth - 1, 0.0f, (float) getHeight());

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
    g.drawText (track.name, content.removeFromTop (22), juce::Justification::centredLeft, true);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (index + 1) + (track.isInstrument() ? "  Drum Machine" : "  Audio"),
                content.removeFromTop (16), juce::Justification::centredLeft);

    g.setColour (theme::grid);
    g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX(), (float) r.getRight());
}

void TrackArea::paintLane (juce::Graphics& g, const model::Track& track, juce::Rectangle<int> r)
{
    // Bar grid
    const double secondsPerBar = transport.beatsToSeconds (transport.getBeatsPerBar());
    const double viewEnd = xToSeconds ((float) getWidth());
    for (int bar = (int) (viewStartSeconds / secondsPerBar); bar * secondsPerBar <= viewEnd; ++bar)
    {
        g.setColour (theme::grid);
        g.drawVerticalLine ((int) secondsToX (bar * secondsPerBar), (float) r.getY(), (float) r.getBottom());
    }
    g.setColour (theme::grid);
    g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX(), (float) r.getRight());

    for (const auto& clip : track.clips)        paintAudioClip (g, track, clip, r);
    for (const auto& clip : track.patternClips) paintPatternClip (g, track, clip, r);
}

juce::Rectangle<float> TrackArea::clipRectFor (double startSeconds, double endSeconds, juce::Rectangle<int> r) const
{
    const float x1 = secondsToX (startSeconds);
    const float x2 = secondsToX (endSeconds);
    return { x1, (float) r.getY() + 4.0f, x2 - x1, (float) r.getHeight() - 9.0f };
}

void TrackArea::paintClipFrame (juce::Graphics& g, juce::Rectangle<float> clipRect, const model::Track& track, const juce::String& name)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (clipRect.withHeight (16.0f), 4.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (name, clipRect.withHeight (16.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
    g.setColour (track.colour.brighter (0.3f));
    g.drawRoundedRectangle (clipRect, 4.0f, 1.0f);
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
                                      (double) (clip.sourceOffset + clip.length) / clip.sampleRate, 1.0f);

    paintClipFrame (g, clipRect, track, clip.name);
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
// Interaction

void TrackArea::mouseDown (const juce::MouseEvent& e)
{
    const int track = trackIndexAtY (e.y);
    if (track >= 0)
        setSelectedTrack (track);

    if (e.x < theme::trackHeaderWidth)
        return;

    // Click in ruler or lane: locate the playhead.
    transport.setPositionSeconds (xToSeconds ((float) e.x));
    repaint();
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

} // namespace beatmaker::ui
