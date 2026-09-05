// Beat Maker — Phase 0 entry point.
//
// Opens a single window with a GarageBand-style transport bar and plays a
// sine test tone through the default audio device when Play is pressed.
// This proves the JUCE build, audio device I/O, and the UI toolkit all work.
// The transport and tone generator will move into engine/ once the real
// audio graph exists.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace beatmaker
{

//==============================================================================
// Minimal audio source: a sine tone that is silent until "playing" is true.
class TestToneSource final : public juce::AudioSource
{
public:
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay); }
    bool isPlaying() const noexcept { return playing.load(); }

    void prepareToPlay (int, double sampleRate) override
    {
        currentSampleRate = sampleRate;
        phaseDelta = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;
    }

    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();

        if (! playing.load())
            return;

        for (int i = 0; i < info.numSamples; ++i)
        {
            const auto sample = static_cast<float> (std::sin (phase) * gain);
            phase += phaseDelta;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                info.buffer->setSample (ch, info.startSample + i, sample);
        }
    }

private:
    std::atomic<bool> playing { false };
    double currentSampleRate = 44100.0;
    double frequencyHz = 440.0;
    double gain = 0.2;
    double phase = 0.0;
    double phaseDelta = 0.0;
};

//==============================================================================
// GarageBand-style transport strip: Record / Play / Stop and a position LCD.
class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (TestToneSource& source) : tone (source)
    {
        for (auto* b : { &recordButton, &playButton, &stopButton })
            addAndMakeVisible (b);

        // Transport icons are drawn as vector paths so they never depend on
        // which fonts happen to be installed.
        juce::Path circle, triangle, square;
        circle.addEllipse (0.0f, 0.0f, 1.0f, 1.0f);
        triangle.addTriangle (0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 1.0f);
        square.addRectangle (0.0f, 0.0f, 1.0f, 1.0f);

        recordButton.setShape (circle,   true, true, false);
        playButton.setShape   (triangle, true, true, false);
        stopButton.setShape   (square,   true, true, false);

        for (auto* b : { &recordButton, &playButton, &stopButton })
            b->setOutline (juce::Colours::transparentBlack, 0.0f);

        recordButton.setEnabled (false); // Phase 1
        playButton.onClick = [this] { tone.setPlaying (true);  startTimerHz (30); };
        stopButton.onClick = [this] { tone.setPlaying (false); stopTimer(); positionSeconds = 0.0; repaint(); };

        addAndMakeVisible (lcd);
        lcd.setJustificationType (juce::Justification::centred);
        lcd.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        lcd.setColour (juce::Label::backgroundColourId, juce::Colour (0xff101418));
        lcd.setColour (juce::Label::textColourId, juce::Colour (0xff9fe1cb));
        updateLcd();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff2b2f36));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        const int buttonSize = area.getHeight();

        recordButton.setBounds (area.removeFromLeft (buttonSize).reduced (8)); area.removeFromLeft (10);
        playButton.setBounds   (area.removeFromLeft (buttonSize).reduced (6)); area.removeFromLeft (10);
        stopButton.setBounds   (area.removeFromLeft (buttonSize).reduced (8)); area.removeFromLeft (16);

        lcd.setBounds (area.removeFromLeft (juce::jmin (360, area.getWidth())));
    }

private:
    void timerCallback() override
    {
        positionSeconds += 1.0 / 30.0;
        updateLcd();
    }

    void updateLcd()
    {
        const double bpm = 120.0;
        const double beats = positionSeconds * bpm / 60.0;
        const int bar  = static_cast<int> (beats / 4.0) + 1;
        const int beat = static_cast<int> (std::fmod (beats, 4.0)) + 1;

        lcd.setText (juce::String::formatted ("%03d | %d      %.0f BPM   4/4", bar, beat, bpm),
                     juce::dontSendNotification);
    }

    TestToneSource& tone;
    juce::ShapeButton recordButton { "Record", juce::Colour (0xffc0392b), juce::Colour (0xffe74c3c), juce::Colour (0xff96281b) };
    juce::ShapeButton playButton   { "Play",   juce::Colour (0xff27ae60), juce::Colour (0xff2ecc71), juce::Colour (0xff1e8449) };
    juce::ShapeButton stopButton   { "Stop",   juce::Colour (0xff7f8c8d), juce::Colour (0xff95a5a6), juce::Colour (0xff626f70) };
    juce::Label lcd;
    double positionSeconds = 0.0;
};

//==============================================================================
// Main content: transport on top, empty track area below.
class MainComponent final : public juce::Component
{
public:
    MainComponent()
    {
        deviceManager.initialiseWithDefaultDevices (0, 2);
        player.setSource (&tone);
        deviceManager.addAudioCallback (&player);

        addAndMakeVisible (transport);
        addAndMakeVisible (trackAreaHint);
        trackAreaHint.setJustificationType (juce::Justification::centred);
        trackAreaHint.setColour (juce::Label::textColourId, juce::Colours::grey);
        trackAreaHint.setText ("Track area - press Play to hear a 440 Hz test tone",
                               juce::dontSendNotification);

        setSize (1100, 680);
    }

    ~MainComponent() override
    {
        deviceManager.removeAudioCallback (&player);
        player.setSource (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1c1f24));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        transport.setBounds (area.removeFromTop (56));
        trackAreaHint.setBounds (area);
    }

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer player;
    TestToneSource tone;
    TransportBar transport { tone };
    juce::Label trackAreaHint;
};

//==============================================================================
class BeatMakerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace beatmaker

START_JUCE_APPLICATION (beatmaker::BeatMakerApplication)
