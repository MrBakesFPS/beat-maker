#include "Preferences.h"

namespace beatmaker::ui
{

std::vector<PrefDef> Preferences::definitions()
{
    using T = PrefDef::Type;
    auto toggle = [] (const char* id, const char* cat, const char* name, const char* desc, bool def)
    { PrefDef d; d.id = id; d.category = cat; d.name = name; d.description = desc; d.type = T::toggle; d.defaultValue = def; return d; };
    auto number = [] (const char* id, const char* cat, const char* name, const char* desc, double def, double min, double max, double step, const char* unit)
    { PrefDef d; d.id = id; d.category = cat; d.name = name; d.description = desc; d.type = T::number; d.defaultValue = def; d.min = min; d.max = max; d.step = step; d.unit = unit; return d; };
    auto choice = [] (const char* id, const char* cat, const char* name, const char* desc, int def, juce::StringArray choices)
    { PrefDef d; d.id = id; d.category = cat; d.name = name; d.description = desc; d.type = T::choice; d.defaultValue = def; d.choices = std::move (choices); return d; };
    auto folder = [] (const char* id, const char* cat, const char* name, const char* desc, const juce::String& def)
    { PrefDef d; d.id = id; d.category = cat; d.name = name; d.description = desc; d.type = T::folder; d.defaultValue = def; return d; };

    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Beat Maker");
    return {
        // Display
        choice ("display.trackHeight", "Display", "Default track height", "Height of track lanes in the edit window.", 1, { "Small", "Medium", "Large", "Extra Large" }),
        toggle ("display.showMarkerStrip", "Display", "Show memory locations strip", "The markers and sections strip above the ruler.", true),
        choice ("display.theme", "Display", "Theme", "Colour palette for every view. High Contrast meets WCAG AAA contrast.", 0, { "Dark", "High Contrast", "Light" }),
        choice ("display.uiScale", "Display", "Interface scale", "Size of the whole interface (text, controls, tracks).", 0, { "100%", "125%", "150%", "175%" }),
        toggle ("display.announceStatus", "Display", "Announce status messages", "Speak status bar messages through the screen reader.", true),
        toggle ("display.showWelcome", "Display", "Show the Welcome window at startup", "Templates, sample projects, the tour and tutorials when Beat Maker opens without a session.", true),
        toggle ("display.clipGainReadout", "Display", "Show clip gain on clips", "Append the clip gain in dB to clip names when it is not 0 dB.", true),
        number ("display.editorHeight", "Display", "Editor panel height", "Height of the note and drum editor panel; drag the bar above the panel to change it.", 300.0, 120.0, 1200.0, 10.0, "px"),
        number ("display.pianoRollRowHeight", "Display", "Note editor row height", "Vertical zoom of the piano roll: pixels per semitone (Alt+wheel over the notes, or the - and + buttons).", 14.0, 6.0, 32.0, 1.0, "px"),
        number ("display.zoomSensitivity", "Display", "Zoom sensitivity", "How much Ctrl+wheel and the R/T focus keys zoom per step.", 1.5, 1.1, 3.0, 0.1, "x"),

        // Operation
        toggle ("operation.timelineFollowsPlayback", "Operation", "Timeline insertion follows playback", "When on, stopping leaves the playhead where it stopped; when off, it returns to where playback started.", true),
        toggle ("operation.latchRecordEnable", "Operation", "Latch record enable buttons", "When off, arming a track disarms every other track.", true),
        number ("operation.autosaveMinutes", "Operation", "Auto-backup interval", "Minutes between automatic backups of a saved session (0 = off).", 3.0, 0.0, 60.0, 1.0, "min"),
        number ("operation.autosaveCount", "Operation", "Auto-backups to keep", "Newest backups kept in Session File Backups.", 5.0, 1.0, 50.0, 1.0, ""),
        choice ("operation.recordBitDepth", "Operation", "Record bit depth", "Bit depth of new audio files.", 1, { "16-bit", "24-bit", "32-bit float" }),
        folder ("operation.audioFilesFolder", "Operation", "Audio files folder", "Where new recordings are written.", music.getChildFile ("Audio Files").getFullPathName()),
        toggle ("operation.crashReports", "Operation", "Crash reports and recovery", "Write a report if Beat Maker crashes and offer the newest auto-backup on the next launch. Reports stay on this computer.", true),
        toggle ("operation.newSessionOnStart", "Operation", "Show New Session on startup", "Offer the templates dialog when Beat Maker starts.", false),

        // Editing
        number ("editing.defaultFadeMs", "Editing", "Default fade length", "Length used by the focus keys D/G/F and new fades.", 10.0, 1.0, 5000.0, 1.0, "ms"),
        choice ("editing.defaultFadeShape", "Editing", "Default fade shape", "Shape of new fades.", 1, { "Linear", "Equal Power", "S-Curve" }),
        toggle ("editing.clipsFollowTempo", "Editing", "Clips follow tempo changes", "When the tempo changes, clips, memory locations and automation keep their bar positions (tick-based). Off: they keep their time in seconds.", true),
        toggle ("editing.loopsFollowTempo", "Editing", "Loops re-conform on tempo change", "Audio clips with a known source tempo (library loops) are re-stretched with Elastic Audio to the new tempo.", true),
        toggle ("editing.autoSelectAfterPaste", "Editing", "Select clips after paste", "Pasted clips become the selection.", true),
        toggle ("editing.nudgeFollowsGrid", "Editing", "Nudge amount follows grid", "The , and . keys move clips by the grid value.", true),
        choice ("editing.separateOnDelete", "Editing", "Delete with a time selection", "What Delete does when a time range is selected.", 0, { "Delete clips in range", "Clear the range (separate first)" }),

        // Mixing
        choice ("mixing.panDepth", "Mixing", "Pan depth", "Centre attenuation of the pan law.", 1, { "-2.5 dB", "-3 dB", "-4.5 dB", "-6 dB" }),
        choice ("mixing.defaultMeterType", "Mixing", "Default meter type", "Meter type for new tracks.", 0, { "Sample Peak", "RMS", "Peak + RMS", "VU", "K-12", "K-14", "K-20" }),
        choice ("mixing.defaultAutomationMode", "Mixing", "Default automation mode", "Automation mode for new tracks.", 1, { "Off", "Read", "Touch", "Latch", "Write", "Trim" }),
        toggle ("mixing.soloLatch", "Mixing", "Solo latch", "When off, soloing a track un-solos the others.", true),
        number ("mixing.meterPeakHoldSeconds", "Mixing", "Peak hold", "How long meters hold their peak readout.", 2.0, 0.0, 10.0, 0.5, "s"),

        // Processing
        choice ("processing.loopConformMode", "Processing", "Loop conform mode", "Elastic mode used when a library loop is conformed to the session tempo.", 0, { "Auto (Rhythmic for drums)", "Polyphonic", "Rhythmic", "Monophonic", "Varispeed" }),
        number ("processing.backgroundRenderSeconds", "Processing", "Background render threshold", "Clips longer than this render Elastic changes on a background thread.", 8.0, 0.0, 600.0, 1.0, "s"),
        choice ("processing.defaultElasticMode", "Processing", "Default Elastic mode", "Mode used by TCE trims and warp markers on clips with Elastic off.", 0, { "Polyphonic", "Rhythmic", "Monophonic" }),
        number ("processing.audioCacheMb", "Processing", "Audio cache size", "Decoded audio kept in memory for reuse (loops, samples, session files). Audio in use is never dropped.", 2048.0, 128.0, 16384.0, 128.0, "MB"),
        number ("processing.transientSensitivity", "Processing", "Transient sensitivity", "Default sensitivity for tab-to-transient and Separate at Transients.", 50.0, 0.0, 100.0, 5.0, "%"),

        // MIDI
        number ("midi.defaultVelocity", "MIDI", "Default note velocity", "Velocity of notes drawn in the piano roll.", 100.0, 1.0, 127.0, 1.0, ""),
        number ("midi.softVelocity", "MIDI", "Soft note velocity", "Velocity of notes drawn with Shift held.", 70.0, 1.0, 127.0, 1.0, ""),
        toggle ("midi.previewNotes", "MIDI", "Audition notes when drawing", "Play a note through the track's instrument when it is added or moved.", true),
    };
}

Preferences::Preferences (juce::File f) : defs (definitions())
{
    if (f != juce::File())
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Beat Maker";
        o.filenameSuffix = "preferences";
        o.folderName = "Beat Maker";
        o.osxLibrarySubFolder = "Application Support";
        f.getParentDirectory().createDirectory();
        file = std::make_unique<juce::PropertiesFile> (f, o);
    }
    for (const auto& d : defs)
    {
        juce::var v = d.defaultValue;
        if (file != nullptr && file->containsKey (d.id))
        {
            const auto stored = file->getValue (d.id);
            switch (d.type)
            {
                case PrefDef::Type::toggle: v = stored.getIntValue() != 0; break;
                case PrefDef::Type::number: v = stored.getDoubleValue(); break;
                case PrefDef::Type::choice: v = stored.getIntValue(); break;
                case PrefDef::Type::text:
                case PrefDef::Type::folder:
                default:                    v = stored; break;
            }
        }
        values.set (d.id, v);
    }
}

Preferences::~Preferences() { save(); }

juce::StringArray Preferences::categories() const
{
    juce::StringArray out;
    for (const auto& d : defs) out.addIfNotAlreadyThere (d.category);
    return out;
}

const PrefDef* Preferences::def (const juce::String& id) const
{
    for (const auto& d : defs) if (d.id == id) return &d;
    return nullptr;
}

juce::var Preferences::get (const juce::String& id) const
{
    if (const auto* v = values.getVarPointer (id)) return *v;
    return def (id) != nullptr ? def (id)->defaultValue : juce::var();
}

juce::String Preferences::choiceName (const juce::String& id) const
{
    const auto* d = def (id);
    if (d == nullptr || d->type != PrefDef::Type::choice) return get (id).toString();
    const int i = getInt (id);
    return juce::isPositiveAndBelow (i, d->choices.size()) ? d->choices[i] : juce::String();
}

void Preferences::set (const juce::String& id, const juce::var& value)
{
    const auto* d = def (id);
    if (d == nullptr) return;
    juce::var v = value;
    if (d->type == PrefDef::Type::number) v = juce::jlimit (d->min, d->max, (double) value);
    if (d->type == PrefDef::Type::choice) v = juce::jlimit (0, juce::jmax (0, d->choices.size() - 1), (int) value);
    if (d->type == PrefDef::Type::toggle) v = (bool) value;
    if (values.getVarPointer (id) != nullptr && *values.getVarPointer (id) == v) return;
    values.set (id, v);
    if (file != nullptr)
    {
        switch (d->type)
        {
            case PrefDef::Type::toggle: file->setValue (id, (bool) v ? 1 : 0); break;
            case PrefDef::Type::number: file->setValue (id, (double) v); break;
            case PrefDef::Type::choice: file->setValue (id, (int) v); break;
            case PrefDef::Type::text:
            case PrefDef::Type::folder:
            default:                    file->setValue (id, v.toString()); break;
        }
    }
    notify (id);
}

void Preferences::resetToDefault (const juce::String& id) { if (const auto* d = def (id)) set (id, d->defaultValue); }
void Preferences::resetAll() { for (const auto& d : defs) set (d.id, d.defaultValue); }

std::vector<const PrefDef*> Preferences::search (const juce::String& query) const
{
    std::vector<const PrefDef*> out;
    const auto words = juce::StringArray::fromTokens (query.toLowerCase(), " ", {});
    for (const auto& d : defs)
    {
        const auto haystack = (d.category + " " + d.name + " " + d.description + " " + d.id).toLowerCase();
        bool ok = true;
        for (const auto& w : words) if (w.isNotEmpty() && ! haystack.contains (w)) { ok = false; break; }
        if (ok) out.push_back (&d);
    }
    return out;
}

int Preferences::addListener (std::function<void (const juce::String&)> fn)
{
    listeners.emplace_back (nextToken, std::move (fn));
    return nextToken++;
}

void Preferences::removeListener (int token)
{
    std::erase_if (listeners, [token] (const auto& l) { return l.first == token; });
}

void Preferences::notify (const juce::String& id)
{
    auto copy = listeners;
    for (auto& l : copy) l.second (id);
}

void Preferences::save() { if (file != nullptr) file->saveIfNeeded(); }

} // namespace beatmaker::ui
