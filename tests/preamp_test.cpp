#include "../Source/OrganPreamp.h"
#include <cstdio>
#include <chrono>
#include <vector>
using namespace rotary::amp;
double thd(OrganPreamp& p, double fs, double a, double f0, double* gainOut) {
    int n = (int) (fs * 0.5);
    std::vector<float> x(n);
    for (int i = 0; i < n; i++) x[i] = (float) (a * std::sin(2 * M_PI * f0 * i / fs));
    p.process(x.data(), n);
    int s0 = n / 2;
    double h[6] = {0};
    for (int k = 1; k <= 5; k++) {
        double re = 0, im = 0;
        for (int i = s0; i < n; i++) {
            double ph = 2 * M_PI * f0 * k * i / fs;
            re += x[i] * std::cos(ph);
            im += x[i] * std::sin(ph);
        }
        h[k] = std::hypot(re, im) * 2 / (n - s0);
    }
    *gainOut = h[1] / a;
    return std::sqrt(h[2] * h[2] + h[3] * h[3] + h[4] * h[4] + h[5] * h[5]) / h[1] * 100;
}
static int fails = 0;
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
/** The AO-28 input section: operating points, gain, harmonics per channel, drift. */
static void frontEnd(double fs) {
    printf("-- front end (V1/V2 6AU6, follower, mix, V4A) --\n");
    for (int q = 1; q <= 2; q++) {
        OrganPreamp p(fs);
        p.setQuality(q);
        OrganFront& f = p.front();
        const OrganFront::Op o = f.op();
        printf("Q%d plates %.0f / %.0f V, screens %.0f / %.0f V, cathode %.2f V, follower %.0f V, V4A %.0f V\n", q,
               o.plate[0], o.plate[1], o.screen[0], o.screen[1], o.cathode, o.followerCathode, o.v4aPlate);
        check(o.plate[0] > 120 && o.plate[0] < 150 && o.screen[0] > 50 && o.screen[0] < 70 && o.cathode > 1.4 &&
                  o.cathode < 2.0,
              "6AU6 at the schematic's operating point (130 V / 60 V / 1.7 V)");
        check(o.followerCathode > 80 && o.followerCathode < 160, "cathode follower biased mid-way");
        double thdDry[3] = {}, thdVib[3] = {}, gDry[3] = {}, gVib[3] = {};
        const double levels[3] = {0.1, 0.3, 0.6};
        for (int li = 0; li < 3; li++)
            for (int ch = 0; ch < 2; ch++) {
                const int n = (int) (0.4 * fs);
                std::vector<double> y((size_t) n);
                for (int i = 0; i < n; i++) {
                    const double x = levels[li] * std::sin(2 * M_PI * 220 * i / fs);
                    const double vd = f.stage(0, ch == 0 ? x : 0), vv = f.stage(1, ch == 1 ? x : 0);
                    y[(size_t) i] = f.mix(vd, f.lineDriver(vv));
                }
                double h[6] = {0};
                for (int k = 1; k <= 5; k++) {
                    double re = 0, im = 0;
                    for (int i = n / 2; i < n; i++) {
                        const double ph = 2 * M_PI * 220 * k * i / fs;
                        re += y[(size_t) i] * std::cos(ph);
                        im += y[(size_t) i] * std::sin(ph);
                    }
                    h[k] = std::hypot(re, im) * 2 / (n - n / 2);
                }
                const double t = std::sqrt(h[2] * h[2] + h[3] * h[3] + h[4] * h[4] + h[5] * h[5]) / h[1] * 100;
                (ch ? thdVib : thdDry)[li] = t;
                (ch ? gVib : gDry)[li] = h[1] / levels[li];
                for (int i = 0; i < n; i++) f.mix(f.stage(0, 0), f.lineDriver(f.stage(1, 0)));
            }
        printf("   dry: gain %.2f / %.2f / %.2f  THD %.1f / %.1f / %.1f %%  (0.1 / 0.3 / 0.6)\n", gDry[0], gDry[1],
               gDry[2], thdDry[0], thdDry[1], thdDry[2]);
        printf("   vib: gain %.2f / %.2f / %.2f  THD %.1f / %.1f / %.1f %%\n", gVib[0], gVib[1], gVib[2], thdVib[0],
               thdVib[1], thdVib[2]);
        check(std::fabs(gDry[0] - 1) < 0.05 && std::fabs(gVib[0] - 1) < 0.08, "unity gain for small signals");
        check(thdDry[0] < 3 && thdDry[1] > thdDry[0] && thdDry[1] < 10 && thdDry[2] > thdDry[1],
              "second harmonic growing with level: a few percent at full registration");
        check(std::fabs(thdVib[1] - thdDry[1]) < 1.5, "vibrato channel like the dry channel");
        // idle: no drift, no DC
        double mx = 0;
        for (int i = 0; i < (int) (3 * fs); i++) {
            const double y = f.mix(f.stage(0, 0), f.lineDriver(f.stage(1, 0)));
            if (i > fs) mx = std::max(mx, std::fabs(y));
        }
        printf("   idle output after 1 s: %.2e\n", mx);
        check(mx < 1e-4, "no drift at idle");
    }
}
int main() {
    double fs = 48000;
    frontEnd(fs);
    printf("-- output section (V4B, single-ended 12BH7, transformer) --\n");
    for (int q = 0; q <= 2; q++) {
        printf("Q%d:", q);
        for (double a : {0.02, 0.1, 0.2, 0.3, 0.5}) {
            OrganPreamp p(fs);
            p.setQuality(q);
            p.setGain(0.5);
            double g;
            double t = thd(p, fs, a, 220, &g);
            printf("  a=%.2f THD %.1f%% g %.2f", a, t, g);
        }
        printf("\n");
        // Frequency response
        printf("   FR:");
        for (double f : {40.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0}) {
            OrganPreamp p(fs);
            p.setQuality(q);
            double g;
            thd(p, fs, 0.02, f, &g);
            printf(" %g Hz %.1f dB", f, 20 * std::log10(g));
        }
        printf("\n");
        OrganPreamp p(fs);
        p.setQuality(q);
        std::vector<float> x(48000);
        for (int i = 0; i < 48000; i++) x[i] = 0.3f * std::sin(i * 0.03f);
        auto t0 = std::chrono::steady_clock::now();
        p.process(x.data(), 48000);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool nan = false;
        for (float v : x)
            if (!std::isfinite(v)) nan = true;
        printf("   CPU %.1f %%  NaN %d\n", ms / 10, nan);
        check(!nan, "finite");
    }
    printf("\n%s\n", fails ? "FAILURES" : "all ok");
    return fails ? 1 : 0;
}
