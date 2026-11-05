#include "../Source/LeslieAmp.h"
#include <cstdio>
#include <complex>
using namespace leslie::amp;
double gainAt(LeslieAmp& a, double fs, double f, double amp) {
    std::vector<float> y((size_t) (fs * 0.4));
    for (size_t i = 0; i < y.size(); i++) y[i] = (float) (amp * std::sin(2 * PI * f * i / fs));
    a.process(y.data(), (int) y.size());
    int st = (int) (fs * 0.2); std::complex<double> s(0, 0); int n = (int) y.size() - st;
    for (int i = 0; i < n; i++) s += (double) y[st + i] * std::polar(1.0, -2 * PI * f * (st + i) / fs);
    return 2 * std::abs(s) / n / amp;
}
int main() {
    const double fs = 48000;
    for (int q = 1; q <= 2; q++) {
        LeslieAmp a(fs); a.setQuality(q); a.setDrive(0.35);
        printf("Q%d frequency response (small signal): ", q);
        for (double f : {20.0, 40.0, 80.0, 200.0, 1000.0, 3000.0, 8000.0, 15000.0}) printf("%g Hz %.2f dB  ", f, 20 * std::log10(gainAt(a, fs, f, 0.01)));
        printf("\n   40 Hz loud (0.25): gain %.2f dB; 1 kHz loud: %.2f dB\n", 20 * std::log10(gainAt(a, fs, 40, 0.25)), 20 * std::log10(gainAt(a, fs, 1000, 0.25)));
        // Sag: chord burst, plate voltage (Vb) over time
        std::vector<float> y((size_t) fs);
        for (size_t i = 0; i < y.size(); i++) { double t = i / fs; y[i] = (float) ((t > 0.1 && t < 0.6) ? 0.25 * (std::sin(2 * PI * 110 * t) + std::sin(2 * PI * 138.6 * t) + std::sin(2 * PI * 164.8 * t)) : 0); }
        printf("   sag curve Vb/Vk (every 50 ms): ");
        for (int b = 0; b < 20; b++) { a.process(y.data() + b * 2400, 2400); printf("%.0f/%.1f ", a.plateSupply(), a.cathodeVoltage()); }
        printf("\n");
        // Silence: residual noise / self-oscillation
        std::vector<float> z((size_t) fs, 0.f); a.process(z.data(), (int) z.size());
        double m = 0; for (float v : z) m = std::max(m, (double) std::fabs(v)); printf("   silence afterwards: max %.2e\n", m);
    }
}
