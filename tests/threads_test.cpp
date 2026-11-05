// Multi-threaded room rendering: output must match the single-threaded result, and it should be faster.
//   g++ -O2 -std=c++17 -pthread -ISource tests/threads_test.cpp
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
#include <chrono>
using namespace rotary;
static std::vector<float> render(int threads, int order, bool exact, bool mics, double* ms) {
    RotaryRoomDSP d(48000); d.setThreads(threads);
    Speakers s; s.az = {30, -30, 0, 110, -110}; s.ch = {0, 1, 2, 4, 5}; s.lfe = 3; d.setSpeakers(s);
    Room r; r.order = order; d.setRoom(r); d.setExactPaths(exact); d.setAmp(0, 1, 0.5); d.setPos(true, 4.2, 6.3, true, 2.6, 2.4, true, 1.5708, true);
    Character c; c.motor = 0; d.setCharacter(c);
    if (mics) { MicSetup m; m.on = true; m.roomX = 3; m.roomY = 3.5; m.grp[0].out = 6; d.setMics(m); }
    for (int k : {24, 28, 31, 36}) d.noteOn(k, 0); d.noteOn(0, 2); d.setRotary("fast");
    std::vector<std::vector<float>> out(16, std::vector<float>(128)); float* p[16]; for (int c = 0; c < 16; c++) p[c] = out[c].data();
    std::vector<float> all;
    for (int b = 0; b < 50; b++) d.process(p, 16, 128);
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < 375; b++) { d.process(p, 16, 128); for (int c = 0; c < 8; c++) all.insert(all.end(), p[c], p[c] + 128); }
    *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return all;
}
int main() {
    int hw = (int) std::thread::hardware_concurrency(); std::printf("hardware threads: %d\n", hw);
    for (auto cfg : { std::pair<int, bool>{3, false}, {3, true}, {5, true} }) for (int mics = 0; mics < 2; mics++) {
        double t1, tn; auto a = render(1, cfg.first, cfg.second, mics, &t1); auto b = render(std::max(2, hw), cfg.first, cfg.second, mics, &tn);
        double sig = 0, err = 0; for (size_t i = 0; i < a.size(); i++) { sig += a[i] * a[i]; double e = a[i] - b[i]; err += e * e; }
        std::printf("order %d %-5s mics %d: 1 thread %5.0f ms/s, %d threads %5.0f ms/s, SNR %s\n", cfg.first, cfg.second ? "exact" : "fast", mics, t1, std::max(2, hw), tn, err == 0 ? "identical" : (std::to_string((int) (10 * std::log10(sig / err))) + " dB").c_str());
    }
}
