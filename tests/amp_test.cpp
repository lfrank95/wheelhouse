#include "../Source/LeslieAmp.h"
#include <cstdio>
#include <chrono>
#include <complex>
using namespace leslie::amp;
static double thd(const std::vector<float>& y, double fs, double f0, int start, double* h2r=nullptr, double* h3r=nullptr) {
    // Harmonics via DFT (Hann window), relative to the fundamental
    int n = (int) y.size() - start; double P[8] = {0};
    for (int k = 1; k <= 7; k++) {
        std::complex<double> s(0, 0);
        for (int i = 0; i < n; i++) { double w = 0.5 - 0.5 * std::cos(2 * PI * i / n); s += (double) y[start + i] * w * std::polar(1.0, -2 * PI * f0 * k * i / fs); }
        P[k] = std::norm(s);
    }
    double hsum = 0; for (int k = 2; k <= 7; k++) hsum += P[k];
    if (h2r) *h2r = std::sqrt(P[2] / P[1]); if (h3r) *h3r = std::sqrt(P[3] / P[1]);
    return std::sqrt(hsum / P[1]);
}
int main() {
    const double fs = 48000;
    for (int q = 0; q <= 2; q++) {
        for (int m = 0; m <= 2; m++) {
            if (q == 0 && m > 0) continue;
            LeslieAmp a(fs); a.setQuality(q); a.setModel(m);
            printf("Q%d model %d: idle Vb=%.0f V Vk=%.1f V Ianode=%.0f mA\n", q, m, a.plateSupply(), a.cathodeVoltage(), a.plateCurrentMA());
            for (double drv : {0.1, 0.35, 0.6, 0.85}) {
                a.setDrive(drv);
                for (double amp : {0.05, 0.2}) {
                    std::vector<float> y((size_t) (fs * 0.5));
                    for (size_t i = 0; i < y.size(); i++) y[i] = (float) (amp * std::sin(2 * PI * 220 * i / fs));
                    auto t0 = std::chrono::steady_clock::now();
                    a.process(y.data(), (int) y.size());
                    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    double h2, h3; double t = thd(y, fs, 220, (int) (fs * 0.25), &h2, &h3);
                    double pk = 0, nan = 0; for (float v : y) { if (!std::isfinite(v)) nan++; pk = std::max(pk, (double) std::fabs(v)); }
                    printf("   drive %.2f in %.2f: THD %5.1f %%  k2 %5.2f %%  k3 %5.2f %%  peak %.3f (lin %.3f)  CPU %.1f %%  %s  Vb=%.0f Vk=%.1f\n",
                           drv, amp, t * 100, h2 * 100, h3 * 100, pk, amp * 2, ms / 5, nan ? "NaN!" : "", a.plateSupply(), a.cathodeVoltage());
                }
            }
        }
    }
    return 0;
}
