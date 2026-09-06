#include <catch2/catch_test_macros.hpp>
#include "../ui/shared/CrashReporter.h"

using namespace beatmaker;

TEST_CASE ("CrashReporter writes a report with context and breadcrumbs, and notices it next time")
{
    auto& r = ui::CrashReporter::get();
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_crash_test");
    folder.deleteRecursively();
    r.install (folder, "9.9-test");
    CHECK (r.logFile().existsAsFile());
    CHECK (r.reportsFolder().isDirectory());
    CHECK (r.pendingReport() == juce::File());
    r.setSessionPath ("/tmp/My Song.bmk");
    r.setAudioDevice ("Test Device 48000 Hz 256");
    for (int i = 0; i < 40; ++i) r.addBreadcrumb ("Action " + juce::String (i));
    const auto crumbs = r.getBreadcrumbs();
    CHECK (crumbs.size() == 32);
    CHECK (crumbs[0].endsWith ("Action 8"));
    CHECK (crumbs[31].endsWith ("Action 39"));

    r.writeReportNow (11);
    // The report this run wrote is not "pending" for this run; another run sees it.
    CHECK (r.pendingReport() == juce::File());
    auto files = r.reportsFolder().findChildFiles (juce::File::findFiles, false, "crash-pending-*.txt");
    REQUIRE (files.size() == 1);
    const auto text = files[0].loadFileAsString();
    CHECK (text.contains ("Version: 9.9-test"));
    CHECK (text.contains ("Signal: 11 SIGSEGV"));
    CHECK (text.contains ("Session: /tmp/My Song.bmk"));
    CHECK (text.contains ("Audio device: Test Device 48000 Hz 256"));
    CHECK (text.contains ("Action 39"));
    CHECK (! text.contains ("Action 7\n"));
    CHECK (text.contains ("Backtrace:"));
    CHECK (text.contains ("End of report"));
    CHECK (r.sessionPathIn (files[0]) == "/tmp/My Song.bmk");

    // Simulate the next launch: a differently named pending file is found, acknowledged, and archived.
    const auto other = files[0].getSiblingFile ("crash-pending-20200101-000000-1.txt");
    files[0].copyFileTo (other);
    CHECK (r.pendingReport() == other);
    r.acknowledge (other);
    CHECK (r.pendingReport() == juce::File());
    CHECK (other.getSiblingFile ("crash-20200101-000000-1.txt").existsAsFile());

    const auto diag = r.diagnosticsText ("Plugins: none");
    CHECK (diag.contains ("Version: 9.9-test"));
    CHECK (diag.contains ("Plugins: none"));
    CHECK (diag.contains ("Action 39"));
    CHECK (diag.contains ("Last crash report"));
    CHECK (diag.contains ("Log (last"));
    const auto file = r.writeDiagnostics ("");
    CHECK (file.existsAsFile());
    CHECK (ui::CrashReporter::describeSignal (11).startsWith ("SIGSEGV"));
    CHECK (ui::CrashReporter::prettify ("app(_ZN9beatmaker2ui13CrashReporter14writeReportNowEi+0x41e) [0x1]").contains ("beatmaker::ui::CrashReporter::writeReportNow(int)"));

    r.setEnabled (false);
    files[0].deleteFile();
    r.writeReportNow (6);
    CHECK (r.reportsFolder().findChildFiles (juce::File::findFiles, false, "crash-pending-*.txt").isEmpty());
    juce::Logger::setCurrentLogger (nullptr);
    folder.deleteRecursively();
}
