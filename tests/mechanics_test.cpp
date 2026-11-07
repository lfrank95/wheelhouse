// Cabinet body and mechanical noise: levels relative to playing, key/pedal action, relay and slow-motor clunk,
#include <complex>
#include <cstdio>
// panel resonances (plywood vs particle board), pedal harmonics (complex tonewheels + B-3 pedal wiring).
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
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>

using namespace rotary;

struct Run {
    RotaryRoomDSP d{48000};
    std::vector<std::vector<float>> out = std::vector<std::vector<float>>(8, std::vector<float>(64));
    float* p[8];
    Run(double motor, double keys, double mech, int board = 0, double res = 0.5) {
        for (int k = 0; k < 8; k++) p[k] = out[k].data();
        Character c;
        c.cond = 0.5;
        c.leak = 0.5;
        c.contact = 0.5;
        c.motor = motor;
        d.setCharacter(c);
        d.setKeyNoise(keys);
        d.setCabinetBody(board, res, mech);
        d.setSpring(false, 0, 2, 0.5, 0.3, 3);
    }
    // RMS (dBFS, front L) over nb blocks, optional peak
    double block(int nb, double* peak = nullptr, std::vector<float>* rec = nullptr) {
        double e = 0, pk = 0;
        long n = 0;
        for (int b = 0; b < nb; b++) {
            d.process(p, 8, 64);
            for (int i = 0; i < 64; i++) {
                double x = out[0][i];
                e += x * x;
                pk = std::max(pk, std::fabs(x));
                n++;
                if (rec) rec->push_back(out[0][i]);
            }
        }
        if (peak) *peak = 20 * std::log10(pk + 1e-12);
        return 10 * std::log10(e / n + 1e-24);
    }
};
static double db(double x) {
    return 20 * std::log10(x + 1e-12);
}

int main() {
    // 1) Reference: a full chord with 888000000 at moderate swell
    {
        Run r(0.4, 0.5, -1);
        int dbu[9] = {8, 8, 8, 0, 0, 0, 0, 0, 0};
        r.d.setDrawbars(dbu, 0);
        r.block(400);
        for (int k : {24, 28, 31, 36}) r.d.noteOn(k, 0, 100);
        r.block(800);
        printf("chord 888 (4 keys), slow:         %6.1f dBFS\n", r.block(1500));
    }
    // 2) Idle noise, organ silent, per source
    for (const char* sp : {"slow", "fast"}) {
        Run a(0.0, 0.0, 0.0);
        a.d.setRotary(sp);
        a.block(3000);
        double base = a.block(1500);
        Run b(0.0, 0.0, 0.5);
        b.d.setRotary(sp);
        b.block(3000);
        Run c(0.5, 0.0, 0.0);
        c.d.setRotary(sp);
        c.block(3000);
        printf("idle %-4s: base (hum/leak) %6.1f  Rotary mech 0.5 %6.1f  organ motor 0.5 %6.1f dBFS\n", sp, base,
               b.block(1500), c.block(1500));
    }
    // 3) Key action without sound: drawbars all in
    {
        Run r(0.0, 0.5, 0.0);
        int z[9] = {0};
        r.d.setDrawbars(z, 0);
        r.d.setDrawbars(z, 2);
        r.d.setClick(0.5);
        {
            Perc pc;
            pc.on = false;
            r.d.setPerc(pc);
        }
        r.block(500);
        for (int v : {30, 127}) {
            double pk;
            r.d.noteOn(30, 0, v);
            r.block(40, &pk);
            r.d.noteOff(30, 0, 64);
            r.block(200);
            printf("key clack v%3d: peak %6.1f dBFS\n", v, pk);
        }
        double pk;
        r.d.noteOn(5, 2, 110);
        r.block(60, &pk);
        r.d.noteOff(5, 2, 64);
        r.block(200);
        printf("pedal thump v110: peak %6.1f dBFS\n", pk);
    }
    // 4) Speed change: relay click and slow-motor clunk
    {
        Run r(0.0, 0.0, 0.6);
        r.d.setRotary("fast");
        r.block(4000);
        double pk;
        r.d.setRotary("slow");
        r.block(60, &pk);
        printf("fast->slow (relay + clunk): peak %6.1f dBFS\n", pk);
    }
    // 5) Panel resonances: energy of the cabinet body on a pedal + lower chord, compared with resonance 0
    for (int board : {0, 1}) {
        double lv[2];
        for (int i = 0; i < 2; i++) {
            Run r(0, 0, 0, board, i ? 1.0 : 0.0);
            int dbl[9] = {0, 0, 8, 6, 0, 0, 0, 0, 0};
            r.d.setDrawbars(dbl, 1);
            int dbp[2] = {6, 4};
            r.d.setDrawbars(dbp, 2);
            r.block(200);
            r.d.noteOn(7, 2, 100);
            for (int k : {12, 16, 19}) r.d.noteOn(k, 1, 100);
            r.block(400);
            lv[i] = r.block(1500);
        }
        printf("%s resonance 1 vs 0: %+.2f dB total\n", board ? "particle board" : "plywood       ", lv[1] - lv[0]);
    }
    // 6) Pedal spectrum on the organ signal (orgBuf): low C with 16' only, then 8' only
    for (int bar = 0; bar < 2; bar++) {
        Run r(0, 0, 0);
        r.d.leakHum = 0;
        r.d.humLevel = 0;
        r.d.setPreamp(false, 0.5);
        int z[9] = {0};
        r.d.setDrawbars(z, 0);
        int dbp[2] = {bar == 0 ? 8 : 0, bar == 1 ? 8 : 0};
        r.d.setDrawbars(dbp, 2);
        r.d.noteOn(0, 2, 100);
        std::vector<float> o;
        for (int b = 0; b < 3000; b++) {
            r.d.process(r.p, 8, 64);
            if (b > 1000) o.insert(o.end(), r.d.orgBuf.begin(), r.d.orgBuf.begin() + 64);
        }
        double f0 = 20.0 * 85 / 104 * 2; // wheel 1 (gear tuning)
        printf("pedal C %s:", bar ? "8' " : "16'");
        double ref = 0;
        for (int h = 1; h <= 12; h++) {
            double a = 0; // tonewheel partials are not exact multiples (gear ratios): search +-1.2 %
            for (int st = -24; st <= 24; st++) {
                double w = TWO_PI * f0 * h * (1 + st * 0.0005) / 48000, c = 0, s2 = 0;
                for (size_t i = 0; i < o.size(); i += 2) {
                    c += o[i] * std::cos(w * (double) i);
                    s2 += o[i] * std::sin(w * (double) i);
                }
                a = std::max(a, std::hypot(c, s2) / (o.size() / 2));
            }
            if (h == 1 || (bar == 1 && h == 2)) ref = std::max(ref, a);
            printf(" h%d %5.1f", h, db(a / (ref > 0 ? ref : a)));
        }
        printf("  (dB re %s)\n", bar ? "2nd" : "fundamental");
    }
}
