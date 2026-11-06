// Memory check of all operating modes: g++ -O1 -g -std=c++17 -fsanitize=address,undefined -ISource
// tests/asan_all_test.cpp
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
using namespace rotary;
int main() {
    for (double fs : {44100.0, 48000.0, 96000.0})
        for (int mode = 0; mode < 3; mode++)
            for (int q = 0; q < 3; q++) {
                RotaryRoomDSP dsp(fs);
                Speakers s;
                if (mode == 0) {
                    s.az = {30, -30};
                    s.ch = {0, 1};
                    s.stereo = true;
                } else if (mode == 1) {
                    s.az = {45, -45, 135, -135};
                    s.ch = {0, 1, 2, 3};
                } else {
                    s.az = {30, -30, 0, 110, -110};
                    s.ch = {0, 1, 2, 4, 5};
                    s.lfe = 3;
                }
                dsp.setSpeakers(s);
                Room r;
                r.order = 3;
                dsp.setRoom(r);
                dsp.setAmp(q % 3, q, 0.5);
                dsp.setPreamp(true, 0.7);
                dsp.setSpring(true, 0.3, 2.8, 0.5, 0.4, q == 1 ? 2 : 3);
                for (int k : {0, 24, 28, 31, 60}) dsp.noteOn(k, 0);
                dsp.noteOn(12, 1);
                dsp.noteOn(0, 2);
                dsp.noteOn(24, 2);
                dsp.setRotary("fast");
                const int N = 256;
                std::vector<std::vector<float>> out(16, std::vector<float>(N));
                float* p[16];
                for (int c = 0; c < 16; c++) p[c] = out[c].data();
                int nan = 0;
                auto run = [&](int b) {
                    for (int i = 0; i < b; i++) {
                        dsp.process(p, 16, N);
                        for (int c = 0; c < 16; c++)
                            for (int j = 0; j < N; j++)
                                if (!std::isfinite(out[c][j])) nan++;
                    }
                };
                run(20);
                MicSetup m;
                m.on = true;
                m.grp[0].out = 6;
                m.grp[1].out = 15;
                m.grp[2].out = -2;
                dsp.setMics(m);
                run(20);
                m.hornStereo = false;
                m.drumStereo = true;
                m.roomArr = 2;
                m.roomType = 6;
                dsp.setMics(m);
                run(20);
                m.grp[0].out = 15;
                m.grp[2].out = 14;
                m.grp[1].delayMs = 50;
                dsp.setMics(m);
                dsp.kickSpring(1);
                run(20);
                // Tent with ray-tracer taps
                r.shape = "tent";
                r.Lx = 30;
                r.Ly = 30;
                r.Lz = 8.5;
                dsp.setRoom(r);
                std::vector<Tap> taps;
                for (int i = 0; i < 300; i++) {
                    Tap t{0.01 + i * 0.001, i * 0.1, 0.1, {1, 0, 0}, 2, {0.01, 0.01, 0.01}};
                    taps.push_back(t);
                }
                dsp.setTaps(taps);
                m.on = false;
                dsp.setMics(m);
                run(20);
                m.on = true;
                dsp.setMics(m);
                run(20);
                if (nan) printf("NaN at fs %.0f mode %d q %d: %d\n", fs, mode, q, nan);
            }
    printf("done\n");
}
