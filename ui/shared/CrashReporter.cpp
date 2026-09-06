#include "CrashReporter.h"
#include <cstring>
#include <cxxabi.h>
#if JUCE_LINUX || JUCE_MAC
 #include <execinfo.h>
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace beatmaker::ui
{

CrashReporter& CrashReporter::get() { static CrashReporter r; return r; }

void CrashReporter::copyTo (char* dest, size_t size, const juce::String& s)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    std::memset (dest, 0, size);
    const auto utf8 = s.toRawUTF8();
    std::strncpy (dest, utf8, size - 1);
}

void CrashReporter::install (const juce::File& folder, const juce::String& ver, bool on)
{
    copyTo (folderPath, sizeof (folderPath), folder.getFullPathName());
    folder.getChildFile ("Crash Reports").createDirectory();
    folder.getChildFile ("Logs").createDirectory();
    copyTo (version, sizeof (version), ver);
    // The report's name is fixed now so the handler never formats anything.
    copyTo (reportPath, sizeof (reportPath), folder.getChildFile ("Crash Reports")
                .getChildFile ("crash-pending-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + "-" + juce::String ((int) ::getpid()) + ".txt").getFullPathName());
    enabled.store (on);
    logger = std::make_unique<juce::FileLogger> (logFile(), "Beat Maker " + ver + " " + juce::SystemStats::getOperatingSystemName(), 512 * 1024);
    juce::Logger::setCurrentLogger (logger.get());
    if (! installed.exchange (true)) juce::SystemStats::setApplicationCrashHandler (handler);
    juce::Logger::writeToLog ("Crash reporter installed; reports in " + reportsFolder().getFullPathName());
}

void CrashReporter::setEnabled (bool on) noexcept { enabled.store (on); }

void CrashReporter::setSessionPath (const juce::String& s) { copyTo (sessionPath, sizeof (sessionPath), s); }
void CrashReporter::setAudioDevice (const juce::String& s) { copyTo (device, sizeof (device), s); }

void CrashReporter::addBreadcrumb (const juce::String& s)
{
    const auto stamp = juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + s;
    const int n = crumbCount.fetch_add (1);
    copyTo (crumbs[(size_t) (n % (int) crumbs.size())].data(), crumbs[0].size(), stamp);
}

juce::StringArray CrashReporter::getBreadcrumbs() const
{
    juce::StringArray out;
    const int n = crumbCount.load();
    const int count = juce::jmin (n, (int) crumbs.size());
    for (int i = n - count; i < n; ++i) out.add (juce::String::fromUTF8 (crumbs[(size_t) (i % (int) crumbs.size())].data()));
    return out;
}

juce::File CrashReporter::reportsFolder() const { return juce::File (juce::String::fromUTF8 (folderPath)).getChildFile ("Crash Reports"); }
juce::File CrashReporter::logFile() const { return juce::File (juce::String::fromUTF8 (folderPath)).getChildFile ("Logs").getChildFile ("Beat Maker.log"); }

juce::File CrashReporter::pendingReport() const
{
    auto files = reportsFolder().findChildFiles (juce::File::findFiles, false, "crash-pending-*.txt");
    files.sort();
    for (int i = files.size(); --i >= 0;)
        if (files[i].getFullPathName() != juce::String::fromUTF8 (reportPath) && files[i].getSize() > 0) return files[i];
    return {};
}

void CrashReporter::acknowledge (const juce::File& report)
{
    if (report.existsAsFile()) report.moveFileTo (report.getSiblingFile (report.getFileName().replace ("crash-pending-", "crash-")));
}

juce::String CrashReporter::sessionPathIn (const juce::File& report) const
{
    juce::StringArray lines; report.readLines (lines);
    for (const auto& l : lines) if (l.startsWith ("Session: ")) return l.fromFirstOccurrenceOf ("Session: ", false, false).trim();
    return {};
}

juce::String CrashReporter::describeSignal (int s)
{
    switch (s)
    {
        case 11: return "SIGSEGV (invalid memory access)";
        case 6:  return "SIGABRT (abort)";
        case 8:  return "SIGFPE (arithmetic error)";
        case 4:  return "SIGILL (illegal instruction)";
        case 7:  return "SIGBUS (bus error)";
        default: return "signal " + juce::String (s);
    }
}

juce::String CrashReporter::prettify (const juce::String& text)
{
    juce::StringArray lines; lines.addLines (text);
    for (auto& line : lines)
    {
        const int open = line.indexOf ("(_Z");
        if (open < 0) continue;
        const int plus = line.indexOf (open, "+");
        const int close = line.indexOf (open, ")");
        if (plus < 0 || close < 0 || plus > close) continue;
        const auto mangled = line.substring (open + 1, plus);
        int status = 0;
        if (char* demangled = abi::__cxa_demangle (mangled.toRawUTF8(), nullptr, nullptr, &status); demangled != nullptr)
        {
            if (status == 0) line = line.substring (0, open + 1) + juce::String (demangled) + line.substring (plus);
            std::free (demangled);
        }
    }
    return lines.joinIntoString ("\n");
}

// Only async-signal-safe calls from here on: open/write/backtrace_symbols_fd.
static void put (int fd, const char* s) { if (s != nullptr) { const auto n = std::strlen (s); if (n > 0) { const auto r = ::write (fd, s, n); juce::ignoreUnused (r); } } }
static void putInt (int fd, long v)
{
    char buf[24]; int i = 23; buf[i] = 0;
    const bool neg = v < 0; unsigned long u = neg ? (unsigned long) (-v) : (unsigned long) v;
    do { buf[--i] = (char) ('0' + (u % 10)); u /= 10; } while (u > 0 && i > 1);
    if (neg) buf[--i] = '-';
    put (fd, buf + i);
}

void CrashReporter::writeReportNow (int signal) noexcept
{
    if (! enabled.load() || reportPath[0] == 0) return;
   #if JUCE_LINUX || JUCE_MAC
    const int fd = ::open (reportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    put (fd, "Beat Maker crash report\nVersion: "); put (fd, version);
    put (fd, "\nSignal: "); putInt (fd, signal);
    switch (signal) { case 11: put (fd, " SIGSEGV"); break; case 6: put (fd, " SIGABRT"); break; case 8: put (fd, " SIGFPE"); break; case 4: put (fd, " SIGILL"); break; case 7: put (fd, " SIGBUS"); break; default: break; }
    put (fd, "\nSession: "); put (fd, sessionPath);
    put (fd, "\nAudio device: "); put (fd, device);
    put (fd, "\n\nRecent actions (oldest first):\n");
    const int n = crumbCount.load(); const int count = n < (int) crumbs.size() ? n : (int) crumbs.size();
    for (int i = n - count; i < n; ++i) { put (fd, "  "); put (fd, crumbs[(size_t) (i % (int) crumbs.size())].data()); put (fd, "\n"); }
    put (fd, "\nBacktrace:\n");
    void* frames[64];
    const int depth = ::backtrace (frames, 64);
    ::backtrace_symbols_fd (frames, depth, fd);
    put (fd, "\nEnd of report\n");
    ::close (fd);
   #else
    juce::ignoreUnused (signal);
   #endif
}

void CrashReporter::handler (void* info)
{
    get().writeReportNow ((int) (juce::pointer_sized_int) info);
}

void CrashReporter::crashNow()
{
    volatile int* bad = nullptr;
    *bad = 1;   // SIGSEGV
    std::abort();
}

juce::String CrashReporter::diagnosticsText (const juce::String& extra) const
{
    juce::String t;
    t << "Beat Maker diagnostics\n"
      << "Version: " << juce::String::fromUTF8 (version) << "\n"
      << "Time: " << juce::Time::getCurrentTime().toString (true, true) << "\n"
      << "System: " << juce::SystemStats::getOperatingSystemName() << " " << (juce::SystemStats::isOperatingSystem64Bit() ? "64-bit" : "32-bit")
      << ", " << juce::SystemStats::getCpuModel() << " x" << juce::SystemStats::getNumCpus() << ", " << juce::SystemStats::getMemorySizeInMegabytes() << " MB\n"
      << "Audio device: " << juce::String::fromUTF8 (device) << "\n"
      << "Session: " << juce::String::fromUTF8 (sessionPath) << "\n";
    if (extra.isNotEmpty()) t << "\n" << extra << "\n";
    t << "\nRecent actions (oldest first):\n";
    for (const auto& c : getBreadcrumbs()) t << "  " << c << "\n";
    auto reports = reportsFolder().findChildFiles (juce::File::findFiles, false, "crash-*.txt");
    reports.sort();
    if (! reports.isEmpty())
    {
        const auto last = reports[reports.size() - 1];
        t << "\nLast crash report (" << last.getFileName() << "):\n" << prettify (last.loadFileAsString()) << "\n";
    }
    const auto log = logFile();
    if (log.existsAsFile())
    {
        juce::StringArray lines; log.readLines (lines);
        t << "\nLog (last " << juce::jmin (200, lines.size()) << " lines):\n";
        for (int i = juce::jmax (0, lines.size() - 200); i < lines.size(); ++i) t << lines[i] << "\n";
    }
    return t;
}

juce::File CrashReporter::writeDiagnostics (const juce::String& extra) const
{
    const auto file = reportsFolder().getChildFile ("diagnostics-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".txt");
    file.replaceWithText (diagnosticsText (extra));
    return file;
}

} // namespace beatmaker::ui
