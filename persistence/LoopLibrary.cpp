#include "LoopLibrary.h"
#include <regex>

namespace beatmaker::persistence
{

juce::String LoopInfo::categoryName (Category c)
{
    switch (c)
    {
        case Category::drums:  return "Drums";
        case Category::bass:   return "Bass";
        case Category::synth:  return "Synth";
        case Category::guitar: return "Guitar";
        case Category::vocals: return "Vocals";
        case Category::fx:     return "FX";
        case Category::other:  return "Other";
    }
    return "Other";
}

//==============================================================================
// Metadata

double LoopLibrary::parseTempoFromName (const juce::String& name)
{
    const std::string text = name.toLowerCase().toStdString();
    std::smatch m;

    // "120bpm", "120 bpm", "bpm120"
    static const std::regex withBpm (R"((\d{2,3})\s*bpm|bpm\s*(\d{2,3}))");
    if (std::regex_search (text, m, withBpm))
    {
        const auto v = std::stoi (m[1].matched ? m[1].str() : m[2].str());
        if (v >= 40 && v <= 300) return v;
    }

    // A standalone 2-3 digit token in a plausible range, e.g. "Drum Loop 120", "bass_90_Am"
    static const std::regex standalone (R"((?:^|[^a-z0-9#])(\d{2,3})(?:$|[^a-z0-9]))");
    for (auto it = std::sregex_iterator (text.begin(), text.end(), standalone); it != std::sregex_iterator(); ++it)
    {
        const auto v = std::stoi ((*it)[1].str());
        if (v >= 55 && v <= 220) return v;
    }
    return 0.0;
}

juce::String LoopLibrary::parseKeyFromName (const juce::String& name)
{
    // Tokens like "Am", "F#", "Bbmin", "Cmaj", "Dmajor", "E minor" (single token only)
    const std::string text = name.toStdString();
    static const std::regex keyToken (R"((?:^|[\s_\-\.])([A-G])([#b]?)(?:\s?(m|min|minor|maj|major))?(?=$|[\s_\-\.]))");
    std::smatch m;
    for (auto it = std::sregex_iterator (text.begin(), text.end(), keyToken); it != std::sregex_iterator(); ++it)
    {
        const auto& match = *it;
        const std::string root = match[1].str(), acc = match[2].str(), mode = match[3].str();
        // A bare letter with no accidental and no mode is too ambiguous ("A", "E"...)
        if (acc.empty() && mode.empty()) continue;
        juce::String result = juce::String (root) + acc;
        if (mode == "m" || mode == "min" || mode == "minor") result += "m";
        return result;
    }
    return {};
}

LoopInfo::Category LoopLibrary::detectCategory (const juce::String& s)
{
    const auto t = s.toLowerCase();
    auto has = [&] (std::initializer_list<const char*> words)
    {
        for (auto* w : words) if (t.contains (w)) return true;
        return false;
    };

    if (has ({ "vocal", "vox", "voice", "choir", "acapella" })) return LoopInfo::Category::vocals;
    if (has ({ "guitar", "gtr", "strum", "riff" }))              return LoopInfo::Category::guitar;
    if (has ({ "bass", "sub ", "808" }))                         return LoopInfo::Category::bass;
    if (has ({ "fx", "riser", "sweep", "impact", "noise", "sfx", "downlifter", "uplifter" })) return LoopInfo::Category::fx;
    if (has ({ "drum", "beat", "kick", "snare", "hat", "perc", "break", "groove", "shaker", "clap" })) return LoopInfo::Category::drums;
    if (has ({ "synth", "pad", "lead", "arp", "keys", "piano", "chord", "pluck", "organ", "string" })) return LoopInfo::Category::synth;
    return LoopInfo::Category::other;
}

double LoopLibrary::estimateTempoFromLength (double lengthSeconds, double preferredBpm, int beatsPerBar)
{
    if (lengthSeconds <= 0.0) return 0.0;
    double best = 0.0, bestDistance = 1.0e9;

    for (int bars : { 1, 2, 4, 8, 16 })
    {
        const double bpm = bars * beatsPerBar * 60.0 / lengthSeconds;
        if (bpm < 70.0 || bpm > 180.0) continue;
        const double distance = std::abs (bpm - preferredBpm);
        if (distance < bestDistance) { bestDistance = distance; best = bpm; }
    }
    return best;
}

LoopInfo LoopLibrary::analyse (const juce::File& file, juce::AudioFormatManager& fm)
{
    LoopInfo info;
    info.file = file;
    info.name = file.getFileNameWithoutExtension();
    // The file name is the best evidence; fall back to the folder name.
    info.category = detectCategory (info.name);
    if (info.category == LoopInfo::Category::other)
        info.category = detectCategory (file.getParentDirectory().getFileName());
    info.key = parseKeyFromName (info.name);

    if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) })
    {
        info.numChannels = (int) reader->numChannels;
        info.sampleRate = reader->sampleRate;
        info.lengthSeconds = reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
    }

    info.bpm = parseTempoFromName (info.name);
    if (info.bpm <= 0.0 && info.lengthSeconds > 0.0)
    {
        info.bpm = estimateTempoFromLength (info.lengthSeconds);
        info.bpmEstimated = info.bpm > 0.0;
    }
    return info;
}

//==============================================================================
// Scanning

class LoopLibrary::ScanThread final : public juce::Thread
{
public:
    ScanThread (LoopLibrary& owner, juce::Array<juce::File> toScan, std::shared_ptr<std::atomic<bool>> aliveToken)
        : Thread ("Loop Library Scan"), library (owner), folders (std::move (toScan)), alive (std::move (aliveToken)) {}

    void run() override
    {
        std::atomic<bool> cancel { false };
        auto result = library.scanFolders (folders, &cancel);
        if (threadShouldExit()) return;

        auto shared = std::make_shared<std::vector<LoopInfo>> (std::move (result));
        auto* lib = &library;
        auto token = alive;
        juce::MessageManager::callAsync ([lib, shared, token]
        {
            if (token->load())
                lib->deliver (std::move (*shared));
        });
    }

private:
    LoopLibrary& library;
    juce::Array<juce::File> folders;
    std::shared_ptr<std::atomic<bool>> alive;
};

LoopLibrary::LoopLibrary (juce::AudioFormatManager& fm) : formatManager (fm) {}

LoopLibrary::~LoopLibrary()
{
    alive->store (false);
    if (scanThread != nullptr)
        scanThread->stopThread (5000);
}

void LoopLibrary::setFolders (const juce::Array<juce::File>& newFolders)
{
    folders.clear();
    for (const auto& f : newFolders)
        if (f.isDirectory() && ! folders.contains (f))
            folders.add (f);
}

void LoopLibrary::addFolder (const juce::File& folder)
{
    if (folder.isDirectory() && ! folders.contains (folder))
        folders.add (folder);
}

std::vector<LoopInfo> LoopLibrary::scanFolders (const juce::Array<juce::File>& toScan, const std::atomic<bool>* cancel) const
{
    std::vector<LoopInfo> result;
    const auto wildcard = formatManager.getWildcardForAllFormats();

    for (const auto& folder : toScan)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, wildcard, juce::File::findFiles))
        {
            if (cancel != nullptr && cancel->load()) return result;
            const auto& file = entry.getFile();
            if (file.getFileName().startsWithChar ('.')) continue;
            result.push_back (analyse (file, formatManager));
        }
    }

    std::sort (result.begin(), result.end(), [] (const LoopInfo& a, const LoopInfo& b)
    {
        if (a.category != b.category) return a.category < b.category;
        return a.name.compareNatural (b.name) < 0;
    });
    return result;
}

void LoopLibrary::rescanAsync()
{
    if (scanThread != nullptr)
        scanThread->stopThread (5000);

    scanning.store (true);
    scanThread = std::make_unique<ScanThread> (*this, folders, alive);
    scanThread->startThread();
}

void LoopLibrary::rescanNow()
{
    deliver (scanFolders (folders, nullptr));
}

void LoopLibrary::deliver (std::vector<LoopInfo> result)
{
    loops = std::move (result);
    scanning.store (false);
    listeners.call ([] (Listener& l) { l.loopLibraryChanged(); });
}

} // namespace beatmaker::persistence
