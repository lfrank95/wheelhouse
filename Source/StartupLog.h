#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Start-up diagnostics for the standalone app: every stage of the start is appended to
// %APPDATA%\Wheelhouse\startup.log (~/.config/Wheelhouse on Linux), and a crash writes a backtrace to the same
// file. When the app does not come up on a user's machine, this file tells where it stopped.
namespace wh {

inline juce::File startupLogFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Wheelhouse")
        .getChildFile("startup.log");
}

inline void logStartup(const juce::String& line) {
    auto f = startupLogFile();
    f.getParentDirectory().createDirectory();
    f.appendText(juce::Time::getCurrentTime().toString(true, true, true, true) + "  " + line + "\n");
}

/** Backtrace of the current thread as module-relative addresses (resolved with the linker map file); on other
    platforms the symbolised JUCE backtrace. Defined in StartupLog.cpp. */
juce::String rawBacktrace();

inline void crashHandler(void*) {
    logStartup("CRASH\n" + rawBacktrace());
}

/** Starts a fresh log for this run and installs the crash handler. */
inline void beginStartupLog(const juce::String& version) {
    startupLogFile().deleteFile();
    logStartup("Wheelhouse " + version + " on " + juce::SystemStats::getOperatingSystemName() + ", " +
               juce::String(juce::SystemStats::getNumCpus()) + " CPUs, display scale " +
               juce::String(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                                : 0.0));
    juce::SystemStats::setApplicationCrashHandler(crashHandler);
}

} // namespace wh
