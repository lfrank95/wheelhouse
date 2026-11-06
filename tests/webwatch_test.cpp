// Wheelhouse - tests of the WebView watchdog logic (WebWatchdog.h).
// Build: g++ -O2 -std=c++17 -ISource tests/webwatch_test.cpp
#include <cstdint>
#include <cstdio>
#include "../Source/WebWatchdog.h"

static int fails = 0;
static void CHECK(bool c, const char* msg) {
    printf("%s  %s\n", c ? "ok  " : "FAIL", msg);
    if (!c) fails++;
}

// Runs the watchdog with a 1 s timer; pongAt(t) says whether the page answers at time t.
template <class F, class S>
static int run(uint32_t until, F pongAt, S showingAt, uint32_t stallFrom = 0, uint32_t stallLen = 0) {
    WebWatchdog w;
    w.reset(0);
    int rebuilds = 0;
    for (uint32_t t = 1000; t <= until; t += 1000) {
        if (stallLen && t == stallFrom) t += stallLen; // message thread blocked: the timer fires late
        if (pongAt(t)) w.pong(t);
        if (w.tick(t, showingAt(t))) {
            rebuilds++;
            w.rebuilt(t);
        }
    }
    return rebuilds;
}

int main() {
    auto always = [](uint32_t) {
        return true;
    };
    auto never = [](uint32_t) {
        return false;
    };
    CHECK(run(120000, always, always) == 0, "page answers: never rebuilt");
    CHECK(run(19000, never, always) == 0, "page still loading (no answer yet): waits up to 20 s");
    CHECK(run(21000, never, always) == 1, "page never answers: rebuilt after 20 s");
    CHECK(run(
              60000, [](uint32_t t) { return t < 30000; }, always) == 1,
          "page stops answering (white): rebuilt once after ~5 s");
    { // after the rebuild the new page answers again: exactly one rebuild
        WebWatchdog w;
        w.reset(0);
        int r = 0;
        uint32_t dead = 0;
        for (uint32_t t = 1000; t <= 60000; t += 1000) {
            const bool ok = t < 10000 || (dead && t > dead + 3000);
            if (ok) w.pong(t);
            if (w.tick(t, true)) {
                r++;
                dead = t;
                w.rebuilt(t);
            }
        }
        CHECK(r == 1 && dead >= 15000 && dead <= 16000, "white page: rebuilt 5-6 s after the last answer, then quiet");
    }
    CHECK(run(
              600000, [](uint32_t t) { return t < 30000; }, always) <= 5,
          "page stays dead: rebuilds back off (20, 40, 80, 160, 320 s), at most 5 in 10 minutes");
    CHECK(run(120000, never, never) == 0, "window closed or minimised: no rebuild");
    CHECK(run(
              60000, [](uint32_t t) { return t < 10000 || t > 18000; }, always, 12000, 8000) == 0,
          "host blocks the message thread for 8 s: no rebuild");
    printf(fails ? "%d FAILED\n" : "all checks passed\n", fails);
    return fails ? 1 : 0;
}
