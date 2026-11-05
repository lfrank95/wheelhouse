#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
#include <chrono>
using namespace rotary;
int main() {
    for (int q = 0; q <= 2; q++) {
        RotaryRoomDSP dsp(48000);
        Speakers s; s.az = {30, -30, 0, 110, -110}; s.ch = {0, 1, 2, 4, 5}; s.lfe = 3; dsp.setSpeakers(s);
        Room r; r.Lx = 6; r.Ly = 8; r.Lz = 2.8; r.absorb = 0.3; r.order = 3; dsp.setRoom(r);
        dsp.setPos(true, 4.2, 6.3, true, 2.6, 2.4, true, 1.5708, true);
        dsp.setAmp(0, q, 0.5); dsp.setDrive(0.5);
        int pd[2] = {8, 4}; dsp.setDrawbars(pd, 2);
        const int N = 128, blocks = 5 * 48000 / N;
        std::vector<std::vector<float>> out(8, std::vector<float>(N)); float* p[8]; for (int c = 0; c < 8; c++) p[c] = out[c].data();
        double peak = 0, e[6] = {0}; int nan = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; b++) {
            if (b == 10) { for (int k : {24, 28, 31, 36}) dsp.noteOn(k, 0); for (int k : {12, 16, 19}) dsp.noteOn(k, 1); dsp.noteOn(0, 2); }
            if (b == 600) dsp.setRotary("fast");
            if (b == 1500) { for (int k : {24, 28, 31, 36}) dsp.noteOff(k, 0); dsp.noteOff(0, 2); }
            dsp.process(p, 8, N);
            for (int c = 0; c < 6; c++) for (int i = 0; i < N; i++) { float v = out[c][i]; if (!std::isfinite(v)) nan++; peak = std::max(peak, (double) std::fabs(v)); e[c] += v * v; }
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("power amp Q%d: CPU %.1f %%  peak %.3f  NaN %d  RMS", q, ms / 50, peak, nan);
        for (int c = 0; c < 6; c++) printf(" %.4f", std::sqrt(e[c] / (blocks * N)));
        printf("  Vb %.0f V\n", dsp.ampPlate());
    }
}
