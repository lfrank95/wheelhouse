// Loudspeaker network (Speakers.h): crossover + Jensen woofer + V21 + horn wave guide.
//   g++ -O2 -std=c++17 -ISource tests/speakers_test.cpp -o speakers_test && ./speakers_test
// 1. Small-signal response under voltage drive against the frequency-domain reference tools/acoustics/speakers.py
//    (|Z| at the terminals, radiated level of woofer and horn re the horn at 2 kHz).
// 2. Full power (25 V peak at 16 ohm): finite, second harmonic of the horn proportional to the level, woofer asymmetry.
// 3. Voice-coil heating: sustained horn power lowers the level by a fraction of a dB.
#include "../Source/Speakers.h"
#include <complex>
#include <cstdio>
#include <vector>
using namespace rotary::spk;

static int fails = 0;
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

struct Meas {
    double z, lo, hi, h2lo, h2hi;
    bool finite;
};
static Meas run(double f, double A, int os = 2) {
    const double fs = 48000;
    System s;
    s.prepare(fs, os);
    const int n = (int) (0.5 * fs);
    double sl = 0, sh = 0, sc = 0;
    std::complex<double> l1 = 0, l2 = 0, h1 = 0, h2 = 0;
    int cnt = 0;
    bool fin = true;
    for (int i = 0; i < n; i++) {
        s.beginSample();
        double uS = 0, aS = 0, iL = 0;
        for (int k = 0; k < os; k++) {
            const double t = (i * os + k) / (fs * os), v = A * std::sin(2 * PI * f * t);
            double G, Ih;
            s.companion(G, Ih);
            iL = G * v + Ih;
            s.step(v);
            uS += s.wooferU();
            aS += s.throatWave();
        }
        double lo, hi;
        s.endSample(uS / os, aS / os, lo, hi);
        fin = fin && std::isfinite(lo) && std::isfinite(hi);
        if (i > n / 2) {
            sl += lo * lo;
            sh += hi * hi;
            sc += iL * iL;
            cnt++;
            const double ph = 2 * PI * f * i / fs;
            l1 += lo * std::polar(1.0, -ph);
            l2 += lo * std::polar(1.0, -2 * ph);
            h1 += hi * std::polar(1.0, -ph);
            h2 += hi * std::polar(1.0, -2 * ph);
        }
    }
    const double ref = A * A / 2;
    return {A / std::sqrt(2.0) / std::sqrt(sc / cnt),
            10 * std::log10(sl / cnt / ref),
            10 * std::log10(sh / cnt / ref),
            std::abs(l2) / std::abs(l1),
            std::abs(h2) / std::abs(h1),
            fin};
}

int main() {
    // reference (speakers.py, voltage drive): f, |Z|, woofer dB, horn dB (re horn at 2 kHz)
    const double ref[][4] = {{80, 82.5, -3.8, -77.0},   {125, 15.4, -1.6, -61.3},  {250, 15.4, -4.9, -41.1},
                             {500, 21.6, -9.9, -25.3},  {1000, 15.8, -18.6, 1.0},  {1600, 10.6, -33.7, 1.8},
                             {3150, 12.7, -56.0, -2.5}, {6300, 18.9, -79.5, -13.8}};
    bool zOk = true, lOk = true, hOk = true;
    for (auto& r : ref) {
        Meas m = run(r[0], 1e-3);
        printf("  %5.0f Hz  |Z| %5.1f (ref %5.1f)  woofer %6.1f (%6.1f)  horn %6.1f (%6.1f) dB\n", r[0], m.z, r[1],
               m.lo, r[2], m.hi, r[3]);
        zOk &= std::abs(m.z / r[1] - 1) < 0.08;
        if (r[2] > -40) lOk &= std::abs(m.lo - r[2]) < 1.2;
        if (r[3] > -40) hOk &= std::abs(m.hi - r[3]) < 1.2;
    }
    check(zOk, "terminal impedance within 8 % of the reference");
    check(lOk, "woofer radiation within 1.2 dB of the reference");
    check(hOk, "horn radiation within 1.2 dB of the reference");

    Meas a = run(2000, 8, 4), b = run(2000, 25, 4), w = run(100, 25, 4);
    printf("  horn 2 kHz: H2 %.2f %% at 8 V, %.2f %% at 25 V; woofer 100 Hz 25 V: H2 %.2f %%\n", 100 * a.h2hi,
           100 * b.h2hi, 100 * w.h2lo);
    check(a.finite && b.finite && w.finite, "full power stays finite");
    check(b.h2hi > 2.2 * a.h2hi && b.h2hi < 4.5 * a.h2hi, "horn second harmonic grows with the level");
    check(b.h2hi > 0.02 && b.h2hi < 0.2, "horn second harmonic at full power a few percent");
    check(w.h2lo > 0.005 && w.h2lo < 0.1, "woofer second harmonic (coil off centre) at full power");

    // thermal: 20 s of 2 kHz at 18 V
    System s;
    s.prepare(48000, 1);
    double l0 = 0, l1 = 0;
    for (int i = 0; i < 20 * 48000; i++) {
        s.beginSample();
        double G, Ih;
        s.companion(G, Ih);
        s.step(18 * std::sin(2 * PI * 2000 * i / 48000.0));
        double lo, hi;
        s.endSample(s.wooferU(), s.throatWave(), lo, hi);
        if (i > 0.4 * 48000 && i < 0.5 * 48000) l0 += hi * hi;
        if (i > 19.9 * 48000) l1 += hi * hi;
    }
    const double dc = 10 * std::log10(l1 / l0);
    printf("  horn after 20 s at ~20 W: %+.2f dB, coil +%.0f K\n", dc, s.coilTempH());
    check(dc < -0.3 && dc > -3, "power compression of the V21 coil");
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
