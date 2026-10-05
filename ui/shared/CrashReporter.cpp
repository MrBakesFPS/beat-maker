#include "CrashReporter.h"
#include <cstring>
#if JUCE_LINUX || JUCE_MAC
 #include <cxxabi.h>
 #include <execinfo.h>
 #include <fcntl.h>
 #include <unistd.h>
#elif JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <process.h>
#endif

namespace beatmaker::ui
{

static int currentProcessId()
{
   #if JUCE_WINDOWS
    return (int) ::_getpid();
   #else
    return (int) ::getpid();
   #endif
}

// Windows exception codes arrive as negative ints; anything above the signal range is one.
static bool isWindowsExceptionCode (int code) { return (unsigned int) code > 0xffffu; }

static const char* windowsExceptionName (unsigned int code)
{
    switch (code)
    {
        case 0xC0000005u: return "EXCEPTION_ACCESS_VIOLATION";
        case 0xC00000FDu: return "EXCEPTION_STACK_OVERFLOW";
        case 0xC0000094u: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case 0xC000001Du: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case 0xC0000409u: return "STATUS_STACK_BUFFER_OVERRUN (fail-fast or abort)";
        case 0xC0000006u: return "EXCEPTION_IN_PAGE_ERROR";
        case 0xC000008Eu: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case 0x80000003u: return "EXCEPTION_BREAKPOINT";
        case 0xE06D7363u: return "unhandled C++ exception";
        default:          return nullptr;
    }
}

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
                .getChildFile ("crash-pending-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + "-" + juce::String (currentProcessId()) + ".txt").getFullPathName());
   #if JUCE_WINDOWS
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::memset (reportPathW, 0, sizeof (reportPathW));
        juce::File (juce::String::fromUTF8 (reportPath)).getFullPathName().copyToUTF16 ((juce::CharPointer_UTF16::CharType*) reportPathW, sizeof (reportPathW) - sizeof (wchar_t));
    }
   #endif
    enabled.store (on);
    logger = std::make_unique<juce::FileLogger> (logFile(), "Beat Maker " + ver + " " + juce::SystemStats::getOperatingSystemName(), 512 * 1024);
    juce::Logger::setCurrentLogger (logger.get());
    if (! installed.exchange (true)) juce::SystemStats::setApplicationCrashHandler (handler);
    juce::Logger::writeToLog ("Crash reporter installed; reports in " + reportsFolder().getFullPathName());
}

void CrashReporter::setEnabled (bool on) noexcept { enabled.store (on); }

void CrashReporter::shutdownLogging() { juce::Logger::setCurrentLogger (nullptr); logger.reset(); }

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
    if (isWindowsExceptionCode (s))
    {
        const auto code = (unsigned int) s;
        const auto hex = "0x" + juce::String::toHexString ((juce::int64) code).toUpperCase();
        if (const char* name = windowsExceptionName (code)) return juce::String (name) + " (" + hex + ")";
        return "exception " + hex;
    }
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
   #if JUCE_WINDOWS
    return text;   // MSVC symbols in the backtrace are already readable
   #else
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
   #endif
}

// Only async-signal-safe calls from here on: open/write/backtrace_symbols_fd.
// (On Windows the handler is an unhandled-exception filter, where the heap is
// still usable, so the backtrace comes from SystemStats::getStackBacktrace.)
namespace
{
   #if JUCE_WINDOWS
    using ReportFile = HANDLE;
    const ReportFile noReportFile = INVALID_HANDLE_VALUE;
    void writeBytes (ReportFile f, const char* s, size_t n) { DWORD written = 0; ::WriteFile (f, s, (DWORD) n, &written, nullptr); }
    void closeReport (ReportFile f) { ::FlushFileBuffers (f); ::CloseHandle (f); }
   #else
    using ReportFile = int;
    const ReportFile noReportFile = -1;
    void writeBytes (ReportFile f, const char* s, size_t n) { const auto r = ::write (f, s, n); juce::ignoreUnused (r); }
    void closeReport (ReportFile f) { ::close (f); }
   #endif

    void put (ReportFile f, const char* s) { if (s != nullptr) { const auto n = std::strlen (s); if (n > 0) writeBytes (f, s, n); } }
    void putInt (ReportFile f, long long v)
    {
        char buf[24]; int i = 23; buf[i] = 0;
        const bool neg = v < 0; unsigned long long u = neg ? (unsigned long long) (-v) : (unsigned long long) v;
        do { buf[--i] = (char) ('0' + (u % 10)); u /= 10; } while (u > 0 && i > 1);
        if (neg) buf[--i] = '-';
        put (f, buf + i);
    }
    void putHex (ReportFile f, unsigned int v)
    {
        char buf[11] = "0x00000000";
        for (int i = 9; i >= 2; --i) { buf[i] = "0123456789ABCDEF"[v & 0xf]; v >>= 4; }
        put (f, buf);
    }
}

void CrashReporter::writeReportNow (int signal) noexcept
{
    if (! enabled.load() || reportPath[0] == 0) return;
   #if JUCE_WINDOWS
    const ReportFile fd = ::CreateFileW (reportPathW, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
   #else
    const ReportFile fd = ::open (reportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   #endif
    if (fd == noReportFile) return;
    put (fd, "Beat Maker crash report\nVersion: "); put (fd, version);
    if (isWindowsExceptionCode (signal))
    {
        put (fd, "\nException: "); putHex (fd, (unsigned int) signal);
        if (const char* name = windowsExceptionName ((unsigned int) signal)) { put (fd, " "); put (fd, name); }
    }
    else
    {
        put (fd, "\nSignal: "); putInt (fd, signal);
        switch (signal) { case 11: put (fd, " SIGSEGV"); break; case 6: put (fd, " SIGABRT"); break; case 8: put (fd, " SIGFPE"); break; case 4: put (fd, " SIGILL"); break; case 7: put (fd, " SIGBUS"); break; default: break; }
    }
    put (fd, "\nSession: "); put (fd, sessionPath);
    put (fd, "\nAudio device: "); put (fd, device);
    put (fd, "\n\nRecent actions (oldest first):\n");
    const int n = crumbCount.load(); const int count = n < (int) crumbs.size() ? n : (int) crumbs.size();
    for (int i = n - count; i < n; ++i) { put (fd, "  "); put (fd, crumbs[(size_t) (i % (int) crumbs.size())].data()); put (fd, "\n"); }
    put (fd, "\nBacktrace:\n");
   #if JUCE_WINDOWS
    put (fd, juce::SystemStats::getStackBacktrace().toRawUTF8());
   #else
    void* frames[64];
    const int depth = ::backtrace (frames, 64);
    ::backtrace_symbols_fd (frames, depth, fd);
   #endif
    put (fd, "\nEnd of report\n");
    closeReport (fd);
}

void CrashReporter::handler (void* info)
{
   #if JUCE_WINDOWS
    // JUCE passes the EXCEPTION_POINTERS of the unhandled exception.
    const auto* ep = static_cast<const EXCEPTION_POINTERS*> (info);
    const auto code = (ep != nullptr && ep->ExceptionRecord != nullptr) ? ep->ExceptionRecord->ExceptionCode : 0xC0000005u;
    get().writeReportNow ((int) code);
   #else
    get().writeReportNow ((int) (juce::pointer_sized_int) info);   // the signal number
   #endif
}

void CrashReporter::crashNow()
{
    volatile int* bad = nullptr;
    *bad = 1;   // SIGSEGV (an access violation on Windows)
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
