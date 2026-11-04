#include "../Source/LeslieRoomDSP.h"
#include <cstdio>
#include <chrono>
using namespace leslie;
int main() {
    double fs = 48000; SpringReverb sp(fs); sp.setEnabled(true); sp.setMix(1); sp.setDecay(2.8);
    int n = (int) (fs * 4); std::vector<float> x(n, 0.f); x[0] = 1;
    auto t0 = std::chrono::steady_clock::now(); sp.process(x.data(), n);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    x[0] -= 1; // trocken raus
    bool nan = false; for (float v : x) if (!std::isfinite(v)) nan = true;
    // Energieabfall (Schroeder)
    std::vector<double> E(n); double acc = 0; for (int i = n - 1; i >= 0; i--) { acc += x[i] * x[i]; E[i] = acc; }
    auto t = [&](double db) { for (int i = 0; i < n; i++) if (10 * std::log10(E[i] / E[0]) < db) return i / fs; return -1.0; };
    printf("CPU %.2f %%  NaN %d  T(-5)=%.2f s  T(-25)=%.2f s  → T60 ≈ %.2f s\n", ms / 40, nan, t(-5), t(-25), (t(-25) - t(-5)) * 3);
    // Dispersion: Ankunft der Energie in Bändern (erste 60 ms), Bandpass per Biquad
    for (double f : {300.0, 800.0, 1500.0, 2500.0, 3500.0}) {
        Biquad b; b.set("peak", f, 8, fs, 0); Biquad bp1, bp2; bp1.set("hp", f / 1.3, 2, fs); bp2.set("lp", f * 1.3, 2, fs);
        double best = 0; int bi = 0; for (int i = 0; i < (int) (0.075 * fs); i++) { double y = bp2.run(bp1.run(x[i])); if (y * y > best) { best = y * y; bi = i; } }
        printf("  %5.0f Hz: erste Spitze bei %.1f ms\n", f, bi / fs * 1000);
    }
    // Stoß
    SpringReverb k(fs); k.setEnabled(true); k.setMix(1); std::vector<float> z(48000, 0.f); k.kick(1); k.process(z.data(), 48000);
    double pk = 0; for (float v : z) pk = std::max(pk, (double) std::fabs(v)); printf("Stoß: Spitze %.2f\n", pk);
}
