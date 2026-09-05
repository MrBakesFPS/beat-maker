// LoopLibrary: finds audio loops in a set of folders and extracts metadata
// (tempo, key, category, length) from file names and headers. Scanning runs
// on a background thread; results are delivered on the message thread.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <memory>
#include <vector>

namespace beatmaker::persistence
{

struct LoopInfo
{
    enum class Category { drums, bass, synth, guitar, vocals, fx, other };

    juce::File file;
    juce::String name;          // file name without extension
    Category category = Category::other;
    double bpm = 0.0;           // 0 = unknown
    bool bpmEstimated = false;  // true when guessed from length rather than read from the name
    juce::String key;           // e.g. "Am", "F#", or empty
    double lengthSeconds = 0.0;
    int numChannels = 0;
    double sampleRate = 0.0;

    double getBars (int beatsPerBar = 4) const noexcept
    {
        return bpm > 0.0 ? lengthSeconds * bpm / (60.0 * beatsPerBar) : 0.0;
    }

    static juce::String categoryName (Category c);
};

class LoopLibrary
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void loopLibraryChanged() = 0;
    };

    explicit LoopLibrary (juce::AudioFormatManager& formatManager);
    ~LoopLibrary();

    // Folders to scan (recursively). Persisted by the owner if desired.
    void setFolders (const juce::Array<juce::File>& folders);
    void addFolder (const juce::File& folder);
    const juce::Array<juce::File>& getFolders() const noexcept { return folders; }

    // Background scan; listeners are notified on the message thread when done.
    void rescanAsync();
    // Blocking scan (tests, command line).
    void rescanNow();
    bool isScanning() const noexcept { return scanning.load(); }

    const std::vector<LoopInfo>& getLoops() const noexcept { return loops; }

    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // ---- Metadata helpers (pure, exposed for tests) ----
    static double parseTempoFromName (const juce::String& name);
    static juce::String parseKeyFromName (const juce::String& name);
    static LoopInfo::Category detectCategory (const juce::String& nameAndFolder);
    // Guess a tempo that makes the loop a whole number of bars, preferring
    // values near `preferredBpm`. Returns 0 if nothing plausible fits.
    static double estimateTempoFromLength (double lengthSeconds, double preferredBpm = 120.0, int beatsPerBar = 4);
    static LoopInfo analyse (const juce::File& file, juce::AudioFormatManager& formatManager);

private:
    class ScanThread;
    std::vector<LoopInfo> scanFolders (const juce::Array<juce::File>& toScan, const std::atomic<bool>* cancel) const;
    void deliver (std::vector<LoopInfo> result);

    juce::AudioFormatManager& formatManager;
    juce::Array<juce::File> folders;
    std::vector<LoopInfo> loops;
    juce::ListenerList<Listener> listeners;
    std::unique_ptr<ScanThread> scanThread;
    std::atomic<bool> scanning { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

} // namespace beatmaker::persistence
