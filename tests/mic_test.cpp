#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
#include <chrono>
using namespace rotary;
int main() {
    for (int mode = 0; mode < 2; mode++) {
        RotaryRoomDSP dsp(48000);
        Speakers s;
        s.az = {30, -30};
        s.ch = {0, 1};
        dsp.setSpeakers(s);
        Room r;
        r.Lx = 6;
        r.Ly = 8;
        r.Lz = 2.8;
        r.absorb = 0.3;
        r.order = 3;
        dsp.setRoom(r);
        dsp.setPos(true, 4.2, 6.3, true, 2.6, 2.4, true, 1.5708, true);
        dsp.setAmp(0, 1, 0.5);
        MicSetup m;
        m.on = mode == 1;
        m.roomX = 3;
        m.roomY = 3.5;
        // Separate outputs for measuring: horn L->2, horn R->3, drum->4, room L->5, room R->6; the listener mix stays
        // off
        m.grp[0].out = 2;
        m.grp[1].out = 4;
        m.grp[2].out = 5; // horn -> 3/4, drum (mono) -> 5, room -> 6/7
        dsp.setMics(m);
        for (int k : {24, 28, 31, 36}) dsp.noteOn(k, 0);
        dsp.noteOn(0, 2);
        dsp.setRotary("fast");
        const int N = 128, blocks = 4 * 48000 / N;
        std::vector<std::vector<float>> out(8, std::vector<float>(N));
        float* p[8];
        for (int c = 0; c < 8; c++) p[c] = out[c].data();
        double e[8] = {0}, mn[8], mx[8];
        for (int c = 0; c < 8; c++) {
            mn[c] = 1e9;
            mx[c] = 0;
        }
        int nan = 0;
        double win[8] = {0};
        int wn = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; b++) {
            dsp.process(p, 8, N);
            for (int c = 0; c < 8; c++)
                for (int i = 0; i < N; i++) {
                    float v = out[c][i];
                    if (!std::isfinite(v)) nan++;
                    if (b > blocks / 2) {
                        e[c] += v * v;
                        win[c] += v * v;
                    }
                }
            if (b > blocks / 2 && ++wn == 4) {
                for (int c = 0; c < 8; c++) {
                    double db = 10 * std::log10(win[c] / (4 * N) + 1e-20);
                    mn[c] = std::min(mn[c], db);
                    mx[c] = std::max(mx[c], db);
                    win[c] = 0;
                }
                wn = 0;
            }
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("mics %s: CPU %.1f %%  NaN %d\n", mode ? "on" : "off", ms / 40, nan);
        const char* nm[8] = {"L", "R", "HornL", "HornR", "Drum", "RoomL", "RoomR", "-"};
        for (int c = 0; c < 7; c++)
            printf("  %-8s RMS %6.1f dB  fluctuation %4.1f dB  meter %.4f\n", nm[c],
                   10 * std::log10(e[c] / (blocks / 2 * N) + 1e-20), mx[c] - mn[c],
                   c < 7 ? dsp.micMeter(c < 2 ? 0 : c - 2) : 0);
    }
}
