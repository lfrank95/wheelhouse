#include "../Source/OrganPreamp.h"
#include <cstdio>
#include <chrono>
#include <vector>
using namespace rotary::amp;
double thd(OrganPreamp& p, double fs, double a, double f0, double* gainOut) {
    int n = (int) (fs * 0.5); std::vector<float> x(n);
    for (int i = 0; i < n; i++) x[i] = (float) (a * std::sin(2 * M_PI * f0 * i / fs));
    p.process(x.data(), n);
    int s0 = n / 2; double h[6] = {0};
    for (int k = 1; k <= 5; k++) { double re = 0, im = 0; for (int i = s0; i < n; i++) { double ph = 2 * M_PI * f0 * k * i / fs; re += x[i] * std::cos(ph); im += x[i] * std::sin(ph); } h[k] = std::hypot(re, im) * 2 / (n - s0); }
    *gainOut = h[1] / a;
    return std::sqrt(h[2] * h[2] + h[3] * h[3] + h[4] * h[4] + h[5] * h[5]) / h[1] * 100;
}
int main() {
    double fs = 48000;
    for (int q = 0; q <= 2; q++) {
        printf("Q%d:", q);
        for (double a : {0.02, 0.1, 0.2, 0.3, 0.5}) { OrganPreamp p(fs); p.setQuality(q); p.setGain(0.5); double g; double t = thd(p, fs, a, 220, &g); printf("  a=%.2f THD %.1f%% g %.2f", a, t, g); }
        printf("\n");
        // Frequency response
        printf("   FR:");
        for (double f : {40.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0}) { OrganPreamp p(fs); p.setQuality(q); double g; thd(p, fs, 0.02, f, &g); printf(" %g Hz %.1f dB", f, 20 * std::log10(g)); }
        printf("\n");
        OrganPreamp p(fs); p.setQuality(q); std::vector<float> x(48000); for (int i = 0; i < 48000; i++) x[i] = 0.3f * std::sin(i * 0.03f);
        auto t0 = std::chrono::steady_clock::now(); p.process(x.data(), 48000);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool nan = false; for (float v : x) if (!std::isfinite(v)) nan = true;
        printf("   CPU %.1f %%  NaN %d\n", ms / 10, nan);
    }
}
