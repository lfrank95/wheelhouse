#include "StartupLog.h"

#if JUCE_WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Frames are logged as "<module>+0x<offset>" so that they can be looked up in the linker map file
// (Wheelhouse.map, built alongside the exe) without symbol files on the user's machine.
juce::String wh::rawBacktrace() {
    void* frames[62];
    const auto n = CaptureStackBackTrace(0, 62, frames, nullptr);
    juce::String out;
    for (int i = 0; i < (int) n; i++) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = {0};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR) frames[i], &mod) &&
            mod != nullptr)
            GetModuleFileNameA(mod, name, MAX_PATH);
        const auto base = (uintptr_t) mod;
        out << i << ": " << juce::File(juce::String(name)).getFileName() << "+0x"
            << juce::String::toHexString((juce::int64)((uintptr_t) frames[i] - base)) << "\n";
    }
    return out;
}
#else
juce::String wh::rawBacktrace() {
    return juce::SystemStats::getStackBacktrace();
}
#endif
