#include "TrackArea.h"

namespace beatmaker::ui
{

TrackArea::TrackArea (model::Session& s, engine::Transport& t, juce::AudioFormatManager& fm)
    : session (s), transport (t), formatManager (fm)
{
    session.addListener (this);
    addAndMakeVisible (addTrackButton);
    addTrackButton.onClick = [this] { if (onAddTrack) onAddTrack(); };
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
    repaint();
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

    g.setColour (theme::gridStrong);
    g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX(), (float) r.getRight());
}

void TrackArea::paintHeader (juce::Graphics& g, const model::Track& track, int index, juce::Rectangle<int> r)
{
    g.setColour (theme::panelDark);
    g.fillRect (r);

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
    g.drawText (juce::String (index + 1) + "  Audio", content.removeFromTop (16), juce::Justification::centredLeft);

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

    // Clips
    for (const auto& clip : track.clips)
    {
        const float x1 = secondsToX (clip.getStartSeconds());
        const float x2 = secondsToX (clip.getEndSeconds());
        if (x2 < r.getX() || x1 > r.getRight()) continue;

        auto clipRect = juce::Rectangle<float> (x1, (float) r.getY() + 4.0f, x2 - x1, (float) r.getHeight() - 9.0f);
        const auto body = track.colour.withMultipliedBrightness (track.mute ? 0.45f : 0.75f);

        g.setColour (body);
        g.fillRoundedRectangle (clipRect, 4.0f);

        auto waveArea = clipRect.reduced (1.0f).withTrimmedTop (16.0f);
        g.setColour (track.colour.contrasting (0.9f).withAlpha (0.85f));
        thumbnailFor (clip).drawChannels (g, waveArea.toNearestInt(),
                                          (double) clip.sourceOffset / clip.sampleRate,
                                          (double) (clip.sourceOffset + clip.length) / clip.sampleRate,
                                          1.0f);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (clipRect.withHeight (16.0f), 4.0f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (clip.name, clipRect.withHeight (16.0f).reduced (6.0f, 0.0f).toNearestInt(),
                    juce::Justification::centredLeft, true);

        g.setColour (track.colour.brighter (0.3f));
        g.drawRoundedRectangle (clipRect, 4.0f, 1.0f);
    }
}

//==============================================================================
// Interaction

void TrackArea::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < theme::trackHeaderWidth)
        return;

    // Click in ruler or empty lane space: locate the playhead.
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

    if (onFilesDropped)
        onFilesDropped (files, trackIndexAtY (y), xToSeconds ((float) juce::jmax (x, theme::trackHeaderWidth)));
}

} // namespace beatmaker::ui
