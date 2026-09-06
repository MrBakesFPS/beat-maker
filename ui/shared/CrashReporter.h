// CrashReporter: writes a report when Beat Maker crashes (signal, backtrace,
// version, session, audio device, recent actions) using only signal-safe
// calls, keeps a rolling log file, notices an unacknowledged report on the
// next launch, and bundles diagnostics for "Report a Problem".
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace beatmaker::ui
{

class CrashReporter
{
public:
    static CrashReporter& get();

    // Installs the signal handlers and the log file. Reports go to
    // `folder/Crash Reports`, the log to `folder/Logs/Beat Maker.log`.
    void install (const juce::File& folder, const juce::String& version, bool enabled = true);
    void setEnabled (bool) noexcept;
    bool isEnabled() const noexcept { return enabled.load(); }

    // Context copied into fixed buffers so the handler can dump it without allocating.
    void setSessionPath (const juce::String&);
    void setAudioDevice (const juce::String&);
    void addBreadcrumb (const juce::String&);      // last 32 kept
    juce::StringArray getBreadcrumbs() const;

    juce::File reportsFolder() const;
    juce::File logFile() const;
    juce::File pendingReport() const;               // newest report not yet acknowledged
    void acknowledge (const juce::File& report);   // renames crash-pending-* to crash-*
    juce::String sessionPathIn (const juce::File& report) const;   // the "Session:" line

    // A text bundle for bug reports: version, system, audio device, session,
    // breadcrumbs, the last crash report and the tail of the log.
    juce::String diagnosticsText (const juce::String& extra) const;
    juce::File writeDiagnostics (const juce::String& extra) const;

    static juce::String describeSignal (int);
    // Demangles the C++ names in a report's backtrace for reading.
    static juce::String prettify (const juce::String& reportText);
    // Signal handler body. Public so the crash flag and tests can call it.
    void writeReportNow (int signal) noexcept;
    // Deliberately crashes (for --crash and for checking the handler end to end).
    [[noreturn]] static void crashNow();

private:
    CrashReporter() = default;
    static void handler (void*);
    void copyTo (char* dest, size_t size, const juce::String&);

    std::atomic<bool> enabled { true };
    std::atomic<bool> installed { false };
    char folderPath[1024] = {}, reportPath[1024] = {}, version[64] = {}, sessionPath[1024] = {}, device[256] = {};
    std::array<std::array<char, 160>, 32> crumbs {};
    std::atomic<int> crumbCount { 0 };
    std::unique_ptr<juce::FileLogger> logger;
    mutable juce::SpinLock lock;
};

} // namespace beatmaker::ui
