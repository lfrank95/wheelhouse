// Performance benchmark: CPU share per stage (one core, 48 kHz, 128-sample blocks).
//   g++ -O2 -std=c++17 -pthread -ISource tests/bench.cpp -o bench && ./bench
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
#include <chrono>
using namespace rotary;
static double run(const char* name, int order, int quality, bool mics, bool spring, int mode) {
    RotaryRoomDSP d(48000);
    Speakers s; if (mode == 0) { s.az = {30, -30}; s.ch = {0, 1}; s.stereo = true; } else { s.az = {30, -30, 0, 110, -110}; s.ch = {0, 1, 2, 4, 5}; s.lfe = 3; }
    d.setSpeakers(s); Room r; r.Lx = 6; r.Ly = 8; r.Lz = 2.8; r.order = order; d.setRoom(r); d.setPos(true, 4.2, 6.3, true, 2.6, 2.4, true, 1.5708, true);
    d.setAmp(0, quality, 0.5); d.setSpring(spring, 0.3, 2.8, 0.5, 0.3, 3);
    if (mics) { MicSetup m; m.on = true; m.roomX = 3; m.roomY = 3.5; d.setMics(m); }
    for (int k : {24, 28, 31, 36}) d.noteOn(k, 0); for (int k : {12, 16, 19}) d.noteOn(k, 1); d.noteOn(0, 2); d.setRotary("fast");
    const int N = 128, blocks = 3 * 48000 / N; std::vector<std::vector<float>> out(16, std::vector<float>(N)); float* p[16]; for (int c = 0; c < 16; c++) p[c] = out[c].data();
    for (int b = 0; b < 40; b++) d.process(p, 16, N);
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; b++) d.process(p, 16, N);
    double pct = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 30;
    std::printf("%-34s %5.1f %%\n", name, pct); return pct;
}
int main() {
    run("stereo order2 eco", 2, 0, false, false, 0);
    run("stereo order2 hq", 2, 1, false, false, 0);
    run("5.1 order3 hq", 3, 1, false, false, 1);
    run("5.1 order3 ultra", 3, 2, false, false, 1);
    run("5.1 order5 hq", 5, 1, false, false, 1);
    run("stereo order2 hq + spring", 2, 1, false, true, 0);
    run("stereo order2 hq + mics", 2, 1, true, false, 0);
    run("5.1 order3 ultra + mics + spring", 3, 2, true, true, 1);
}
