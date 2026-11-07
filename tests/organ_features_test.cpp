// the standard headers first: the private/public swap must not touch them
#include <complex>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#define private public
#include "RotaryRoomDSP.h"
#include <cstdio>
#include <chrono>
using namespace rotary;
static RotaryRoomDSP* mk() {
    auto* d = new RotaryRoomDSP(48000);
    Speakers s;
    s.az = {30, -30};
    s.ch = {0, 1};
    s.stereo = true;
    d->setSpeakers(s);
    d->setAmp(0, 0, 0);
    d->setPreamp(false, 0.5);
    d->setRotary("stop");
    return d;
}
int main() {
    std::vector<std::vector<float>> out(16, std::vector<float>(64));
    float* p[16];
    for (int k = 0; k < 16; k++) p[k] = out[k].data();
    // 1) key click vs velocity: 8' only (pure low tone), energy above ~2 kHz in the first 12 ms
    for (int vel : {20, 64, 100, 127}) {
        double e = 0;
        int runs = 40;
        for (int r = 0; r < runs; r++) {
            RotaryRoomDSP* d = mk();
            int db[9] = {0, 0, 8, 0, 0, 0, 0, 0, 0};
            d->setDrawbars(db, 0);
            d->leakHum = 0;
            d->humLevel = 0;
            Perc pc;
            pc.on = false;
            d->setPerc(pc);
            for (int b = 0; b < 200 + r; b++) d->process(p, 16, 64);
            d->noteOn(24, 0, vel);
            Biquad hp1, hp2;
            hp1.set("hp", 2000, 0.7, 48000);
            hp2.set("hp", 2000, 0.7, 48000);
            double eOn = 0, eSt = 0;
            for (int b = 0; b < 9; b++) {
                d->process(p, 16, 64);
                for (int i = 0; i < 64; i++) {
                    double y = hp2.run(hp1.run(d->orgBuf[i]));
                    eOn += y * y;
                }
            }
            for (int b = 0; b < 150; b++) {
                d->process(p, 16, 64);
                for (int i = 0; i < 64; i++) hp2.run(hp1.run(d->orgBuf[i]));
            }
            for (int b = 0; b < 9; b++) {
                d->process(p, 16, 64);
                for (int i = 0; i < 64; i++) {
                    double y = hp2.run(hp1.run(d->orgBuf[i]));
                    eSt += y * y;
                }
            }
            e += std::max(0.0, eOn - eSt);
            delete d;
        }
        printf("velocity %3d: click energy above 2 kHz %.2f dB\n", vel, 10 * std::log10(e / runs));
    }
    // 2) CPU of the organ stage with all features (full drawbars, 10 keys, vibrato C3)
    {
        RotaryRoomDSP* d = mk();
        int db[9] = {8, 8, 8, 8, 8, 8, 8, 8, 8};
        d->setDrawbars(db, 0);
        d->setVib("c3");
        for (int k : {12, 16, 19, 24, 28, 31, 36, 40, 43, 48}) d->noteOn(k, 0);
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 750; b++) d->process(p, 16, 64);
        printf("total with organ features: %.0f ms/s\n",
               std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        delete d;
    }
    // 3) NaN / level check of the scanner across V1..C3
    for (const char* v : {"v1", "v2", "v3", "c1", "c3"}) {
        RotaryRoomDSP* d = mk();
        d->setVib(v);
        d->noteOn(36, 0);
        double e = 0;
        int nan = 0;
        for (int b = 0; b < 1500; b++) {
            d->process(p, 16, 64);
            for (int i = 0; i < 64; i++) {
                float x = d->orgBuf[i];
                if (!std::isfinite(x)) nan++;
                if (b > 500) e += x * x;
            }
        }
        printf("vibrato %s: level %.4f NaN %d\n", v, std::sqrt(e / 64000), nan);
        delete d;
    }
}
