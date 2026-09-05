#include "StepSequencer.h"

namespace beatmaker::ui
{

StepSequencer::StepSequencer (model::Session& s, engine::Transport& t, engine::AudioGraph& g)
    : session (s), transport (t), graph (g)
{
    startTimerHz (30);
}

void StepSequencer::setTarget (int newTrackIndex, int newClipIndex)
{
    trackIndex = newTrackIndex;
    clipIndex = newClipIndex;
    repaint();
}

//==============================================================================
// Model access

const model::Track* StepSequencer::getTrack() const { return session.getTrack (trackIndex); }

const model::PatternClip* StepSequencer::getClip() const
{
    if (auto* track = getTrack())
        if (juce::isPositiveAndBelow (clipIndex, (int) track->patternClips.size()))
            return &track->patternClips[(size_t) clipIndex];
    return nullptr;
}

int StepSequencer::getNumSteps() const
{
    auto* clip = getClip();
    return clip != nullptr && clip->pattern != nullptr ? clip->pattern->numSteps : 16;
}

int StepSequencer::currentStep() const
{
    auto* clip = getClip();
    if (clip == nullptr || clip->pattern == nullptr) return -1;

    const double stepSeconds = 60.0 / (transport.getBpm() * clip->pattern->stepsPerBeat);
    const double rel = transport.getPositionSeconds() - clip->getStartSeconds();
    if (rel < 0.0 || rel >= clip->getLengthSeconds()) return -1;
    return (int) (rel / stepSeconds) % clip->pattern->numSteps;
}

//==============================================================================
// Geometry

juce::Rectangle<int> StepSequencer::gridBounds() const
{
    return getLocalBounds().withTrimmedLeft (padColumnWidth).withTrimmedTop (headerHeight).reduced (0, 0);
}

int StepSequencer::padAt (int y) const
{
    const auto g = gridBounds();
    if (y < g.getY() || y >= g.getBottom()) return -1;
    return juce::jlimit (0, engine::DrumKit::numPads - 1, (y - g.getY()) * engine::DrumKit::numPads / juce::jmax (1, g.getHeight()));
}

int StepSequencer::stepAt (int x) const
{
    const auto g = gridBounds();
    if (x < g.getX() || x >= g.getRight()) return -1;
    return juce::jlimit (0, getNumSteps() - 1, (x - g.getX()) * getNumSteps() / juce::jmax (1, g.getWidth()));
}

juce::Rectangle<float> StepSequencer::cellBounds (int pad, int step) const
{
    const auto g = gridBounds().toFloat();
    const float cw = g.getWidth() / (float) getNumSteps();
    const float rh = g.getHeight() / (float) engine::DrumKit::numPads;
    return { g.getX() + step * cw, g.getY() + pad * rh, cw, rh };
}

//==============================================================================
// Painting

void StepSequencer::paint (juce::Graphics& g)
{
    g.fillAll (theme::panelDark);

    auto* track = getTrack();
    auto* clip = getClip();

    if (track == nullptr || clip == nullptr || clip->pattern == nullptr || track->drumKit == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Select a Drum Machine or Synth track to edit it", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& pattern = *clip->pattern;
    const auto& kit = *track->drumKit;
    const int numSteps = pattern.numSteps;
    const int playStep = currentStep();
    const auto grid = gridBounds();

    // Header: step numbers, beat shading
    g.setFont (juce::FontOptions (11.0f));
    for (int s = 0; s < numSteps; ++s)
    {
        auto col = cellBounds (0, s).withY (0.0f).withHeight ((float) getHeight());
        const bool beatStart = s % pattern.stepsPerBeat == 0;
        const bool oddBeat = (s / pattern.stepsPerBeat) % 2 == 1;

        g.setColour (oddBeat ? theme::background.brighter (0.03f) : theme::background);
        g.fillRect (col.withTrimmedTop ((float) headerHeight));

        if (beatStart)
        {
            g.setColour (theme::textDim);
            g.drawText (juce::String (s / pattern.stepsPerBeat + 1), col.withHeight ((float) headerHeight).toNearestInt(),
                        juce::Justification::centred);
        }
    }

    // Pad column + rows
    for (int pad = 0; pad < engine::DrumKit::numPads; ++pad)
    {
        auto row = cellBounds (pad, 0).withX (0.0f).withWidth ((float) padColumnWidth);
        const bool isDropTarget = pad == dragPad;

        g.setColour (isDropTarget ? theme::accent.withAlpha (0.35f) : (pad % 2 == 0 ? theme::panel : theme::panelDark));
        g.fillRect (row);

        g.setColour (track->colour);
        g.fillRect (row.removeFromLeft (4.0f));

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (kit.pads[(size_t) pad].name, row.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);

        // Cells
        for (int s = 0; s < numSteps; ++s)
        {
            const auto cell = cellBounds (pad, s).reduced (1.5f);
            const auto v = pattern.get (pad, s);

            if (v > 0)
            {
                const float alpha = 0.35f + 0.65f * (float) v / 127.0f;
                g.setColour (track->colour.withAlpha (alpha));
                g.fillRoundedRectangle (cell, 3.0f);
            }
            else
            {
                g.setColour (theme::grid);
                g.drawRoundedRectangle (cell, 3.0f, 1.0f);
            }
        }
    }

    // Playhead column
    if (playStep >= 0)
    {
        g.setColour (theme::playhead.withAlpha (0.14f));
        g.fillRect (cellBounds (0, playStep).withY ((float) grid.getY()).withHeight ((float) grid.getHeight()));
        g.setColour (theme::accent);
        g.fillRect (cellBounds (0, playStep).withY (0.0f).withHeight (3.0f));
    }

    // Grid lines at beats
    g.setColour (theme::gridStrong);
    for (int s = 0; s <= numSteps; s += pattern.stepsPerBeat)
        g.drawVerticalLine ((int) cellBounds (0, juce::jmin (s, numSteps - 1)).getX() + (s == numSteps ? (int) cellBounds (0, 0).getWidth() : 0),
                            0.0f, (float) getHeight());
    g.drawVerticalLine (padColumnWidth - 1, 0.0f, (float) getHeight());
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
}

//==============================================================================
// Interaction

void StepSequencer::applyPaint (int pad, int step)
{
    if (pad < 0 || step < 0 || (pad == lastPaintPad && step == lastPaintStep)) return;
    lastPaintPad = pad; lastPaintStep = step;

    if (auto* clip = getClip())
        if (clip->pattern != nullptr && clip->pattern->get (pad, step) != paintValue && onStepChanged)
            onStepChanged (trackIndex, clipIndex, pad, step, paintValue);
}

void StepSequencer::mouseDown (const juce::MouseEvent& e)
{
    auto* track = getTrack();
    auto* clip = getClip();
    if (track == nullptr || clip == nullptr || clip->pattern == nullptr) return;

    const int pad = padAt (e.y);
    if (pad < 0) return;

    if (e.x < padColumnWidth)
    {
        // Audition
        if (track->drumKit != nullptr)
            graph.triggerPadPreview (track->drumKit.get(), pad, 1.0f);
        return;
    }

    const int step = stepAt (e.x);
    if (step < 0) return;

    const auto current = clip->pattern->get (pad, step);
    paintValue = current > 0 ? (std::uint8_t) 0 : (e.mods.isShiftDown() ? softVelocity : fullVelocity);
    painting = true;
    lastPaintPad = lastPaintStep = -1;
    applyPaint (pad, step);
}

void StepSequencer::mouseDrag (const juce::MouseEvent& e)
{
    if (painting)
        applyPaint (padAt (e.y), stepAt (e.x));
}

void StepSequencer::mouseUp (const juce::MouseEvent&)
{
    painting = false;
}

void StepSequencer::timerCallback()
{
    const int step = currentStep();
    if (step != lastPlayheadStep)
    {
        lastPlayheadStep = step;
        repaint();
    }
}

bool StepSequencer::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (getTrack() == nullptr) return false;
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg" || ext == ".mp3")
            return true;
    }
    return false;
}

void StepSequencer::filesDropped (const juce::StringArray& files, int, int y)
{
    const int pad = padAt (y);
    dragPad = -1;
    repaint();

    if (pad >= 0 && files.size() > 0 && onPadSampleDropped)
        onPadSampleDropped (trackIndex, pad, juce::File (files[0]));
}

} // namespace beatmaker::ui
