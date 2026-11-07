// Scanner vibrato (ScannerVibrato.h) against the documented figures of the B-3 line and scanner.
//   g++ -O2 -std=c++17 -ISource tests/scanner_test.cpp -o scanner_test && ./scanner_test
// 1. Line: whole-line delay about 1 ms and flat to 4 kHz, -3 dB near 5.8 kHz, level ripple of the middle taps a few dB.
// 2. Scanner: pitch deviation V3 about +-1.3 %, V2 and V1 smaller in that order, rate 6.87 Hz.
// 3. Level: vibrato and chorus within 1.5 dB of the dry signal at 440 Hz; nothing blows up at 96 kHz.
#include "../Source/ScannerVibrato.h"
#include <complex>
#include <cstdio>
#include <vector>
using namespace rotary::scan;

static int fails = 0;
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

/** Steady-state transfer u -> node n at frequency f (the scanner is not involved). */
static std::complex<double> lineTf(double f, int n, double fs, const std::string& mode = "v3") {
    Scanner s;
    s.prepare(fs);
    s.setMode(mode);
    const int N = (int) (0.4 * fs);
    std::complex<double> acc = 0;
    int cnt = 0;
    for (int i = 0; i < N; i++) {
        const double t = i / fs, u = std::sin(2 * PI * f * t);
        s.process(u);
        if (i > N / 2) {
            acc += s.node(n) * std::exp(std::complex<double>(0, -2 * PI * f * t));
            cnt++;
        }
    }
    return acc / (double) cnt * 2.0 * std::complex<double>(0, 1); // sine input: phasor of unit amplitude
}

struct Pitch {
    double lo, hi, rate, rms;
};
/** Zero-crossing pitch of a 1 kHz tone through the scanner: min / max deviation and the modulation rate. */
static Pitch pitch(const std::string& mode, double fs) {
    Scanner s;
    s.prepare(fs);
    s.setMode(mode);
    const double f = 1000;
    const int N = (int) (1.5 * fs);
    std::vector<double> y((size_t) N);
    for (int i = 0; i < N; i++) y[(size_t) i] = s.process(std::sin(2 * PI * f * i / fs));
    // periods from upward zero crossings, averaged over 5 cycles, after 0.5 s
    std::vector<double> zc;
    for (int i = (int) (0.5 * fs); i + 1 < N; i++)
        if (y[(size_t) i] <= 0 && y[(size_t) i + 1] > 0) {
            const double fr = y[(size_t) i] / (y[(size_t) i] - y[(size_t) i + 1]);
            zc.push_back((i + fr) / fs);
        }
    // the pitch of the carrier, smoothed over two plate passages (18 ms): the plate crossfades between taps of
    // slightly different level add bumps around the triangle, the smoothing shows its slopes
    std::vector<double> raw, dev;
    for (size_t i = 5; i < zc.size(); i++) raw.push_back(5 / (zc[i] - zc[i - 5]) / f - 1);
    const int W = 18;
    double lo = 1e9, hi = -1e9;
    for (size_t i = (size_t) W; i < raw.size(); i++) {
        double a = 0;
        for (int k = 0; k < W; k++) a += raw[i - (size_t) k];
        a /= W;
        dev.push_back(a);
        lo = std::min(lo, a);
        hi = std::max(hi, a);
    }
    // rate: upward crossings of the deviation through zero with hysteresis (one per vibrato cycle)
    int peaks = 0;
    bool up = false;
    for (double d : dev) {
        if (!up && d > 0.4 * hi) {
            up = true;
            peaks++;
        } else if (up && d < 0.4 * lo)
            up = false;
    }
    double rms = 0;
    for (int i = (int) (0.5 * fs); i < N; i++) rms += y[(size_t) i] * y[(size_t) i];
    rms = std::sqrt(rms / (N - 0.5 * fs));
    return {lo, hi, peaks / 1.0, rms};
}

int main() {
    const double fs = 48000;
    printf("-- line --\n");
    double gd4k = 0, gd200 = 0;
    {
        auto g = [&](double f) {
            const double df = 5;
            const auto a = lineTf(f - df, 18, fs), b = lineTf(f + df, 18, fs);
            double dph = std::arg(b) - std::arg(a);
            while (dph > PI) dph -= 2 * PI;
            while (dph < -PI) dph += 2 * PI;
            return -dph / (2 * PI * 2 * df);
        };
        gd200 = g(200);
        gd4k = g(4000);
        printf("group delay node 18: %.0f us @200 Hz, %.0f us @4 kHz\n", gd200 * 1e6, gd4k * 1e6);
        check(gd200 > 0.85e-3 && gd200 < 1.15e-3, "whole line about 1 ms");
        check(std::fabs(gd4k - gd200) < 0.2e-3, "delay flat to 4 kHz (coupled coils)");
        const double l1k = 20 * std::log10(std::abs(lineTf(1000, 18, fs)));
        double fc = 0;
        for (double f = 4000; f <= 8000; f += 200)
            if (20 * std::log10(std::abs(lineTf(f, 18, fs))) < l1k - 3) {
                fc = f;
                break;
            }
        printf("-3 dB of the whole line: %.0f Hz\n", fc);
        check(fc > 5000 && fc < 6800, "cutoff 5..6.8 kHz");
        double mn = 1e9, mx = -1e9;
        for (double f = 100; f <= 3000; f *= 1.25) {
            const double l = 20 * std::log10(std::abs(lineTf(f, 9, fs)));
            mn = std::min(mn, l);
            mx = std::max(mx, l);
        }
        printf("tap 9 ripple 100 Hz..3 kHz: %.1f dB\n", mx - mn);
        check(mx - mn > 1 && mx - mn < 6, "mid-tap ripple from the 15k termination: a few dB");
    }
    printf("-- scanner --\n");
    const Pitch p3 = pitch("v3", fs), p2 = pitch("v2", fs), p1 = pitch("v1", fs);
    printf("V3 %.2f..%.2f %%  V2 %.2f..%.2f %%  V1 %.2f..%.2f %%  peaks/s V3 = %.0f\n", p3.lo * 100, p3.hi * 100,
           p2.lo * 100, p2.hi * 100, p1.lo * 100, p1.hi * 100, p3.rate);
    check(p3.hi > 0.010 && p3.hi < 0.018 && p3.lo < -0.010 && p3.lo > -0.018, "V3 about +-1.3 %");
    check(p2.hi < p3.hi * 0.9 && p2.hi > p3.hi * 0.6, "V2 about three quarters of V3 (12 of 16 sections)");
    check(p1.hi < p2.hi * 0.8 && p1.hi > p3.hi * 0.35, "V1 about half of V3 (8 of 16 sections)");
    check(p3.rate >= 6 && p3.rate <= 8, "about 6.9 cycles per second");
    printf("-- level --\n");
    {
        auto rms = [&](const std::string& m, double f, double sr) {
            Scanner s;
            s.prepare(sr);
            s.setMode(m);
            const int N = (int) (1.0 * sr);
            double acc = 0;
            bool fin = true;
            for (int i = 0; i < N; i++) {
                const double y = s.process(std::sin(2 * PI * f * i / sr));
                fin = fin && std::isfinite(y);
                if (i > N / 2) acc += y * y;
            }
            return fin ? 20 * std::log10(std::sqrt(acc / (N / 2)) / std::sqrt(0.5)) : 1e9;
        };
        const double v = rms("v3", 440, fs), c = rms("c3", 440, fs), v1 = rms("v1", 440, fs);
        printf("440 Hz: V3 %.1f dB  V1 %.1f dB  C3 %.1f dB re dry\n", v, v1, c);
        check(std::fabs(v) < 1.5 && std::fabs(v1) < 1.5, "vibrato at the dry level");
        check(std::fabs(c) < 2.0, "chorus at about the dry level");
        // above a few kHz the taps two sections apart are far out of phase, so the crossfades cancel part of the
        // treble (V3 more than V1); above the line cutoff only the first taps still carry signal
        const double t4 = rms("v3", 4000, fs), t4v1 = rms("v1", 4000, fs), t8 = rms("v3", 8000, fs);
        printf("4 kHz: V3 %.1f dB, V1 %.1f dB;  8 kHz V3 %.1f dB\n", t4, t4v1, t8);
        check(t4 > -12 && t4 < -2 && t4v1 > t4 && t8 < -8, "treble thinned by the crossfades, gone above the cutoff");
        const double a = rms("c2", 440, 96000), b = rms("v3", 3000, 44100);
        printf("96 kHz C2 %.1f dB, 44.1 kHz V3 @3 kHz %.1f dB\n", a, b);
        check(std::fabs(a) < 2 && b > -10 && b < 1, "finite and level at 44.1 / 96 kHz");
    }
    printf("\n%s\n", fails ? "FAILURES" : "all ok");
    return fails ? 1 : 0;
}
