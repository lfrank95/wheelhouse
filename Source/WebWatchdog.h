#pragma once
// Wheelhouse - decision logic of the WebView watchdog (see WebUiView in WebUi.h). No JUCE code, so it can be tested
// on its own (tests/webwatch_test.cpp).
#include <cstdint>

/** Decides when the WebView must be rebuilt. Times in ms (wrap-around safe: unsigned differences). */
struct WebWatchdog {
    static constexpr uint32_t TIMEOUT_MS = 5000;       // page loaded, no answer for this long: rebuild
    static constexpr uint32_t LOAD_TIMEOUT_MS = 20000; // after (re)building: time allowed for the first answer
    static constexpr uint32_t STALL_MS = 2500;         // timer gap that means the message thread was blocked
    uint32_t lastPong = 0, lastTick = 0;
    bool answered = false;
    int failedBuilds = 0; // rebuilds in a row without an answer: the wait for the first answer doubles each time

    void reset(uint32_t now) {
        lastPong = lastTick = now;
        answered = false;
    }
    void pong(uint32_t now) {
        lastPong = now;
        answered = true;
        failedBuilds = 0;
    }
    /** Called when the view is rebuilt because tick() returned true. */
    void rebuilt(uint32_t now) {
        reset(now);
        if (failedBuilds < 4) failedBuilds++;
    }
    /** Called about once a second. Returns true if the view must be rebuilt. When the view is not on screen
        (window closed or minimised) or the host blocked the message thread for a while (loading a project, a
        modal dialog), the page had no chance to answer, so counting starts again. */
    bool tick(uint32_t now, bool showing) {
        const bool stalled = now - lastTick > STALL_MS;
        lastTick = now;
        if (!showing || stalled) {
            lastPong = now;
            return false;
        }
        return now - lastPong > (answered ? TIMEOUT_MS : LOAD_TIMEOUT_MS << failedBuilds);
    }
};
