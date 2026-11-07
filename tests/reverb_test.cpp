// Reverb decay per octave (OctaveDecay in core/Basics.h, the FDN in core/Render.h).
//   g++ -O2 -std=c++17 -pthread -ISource tests/reverb_test.cpp -o reverb_test && ./reverb_test
// 1. Filter design: the fitted band gains hit the targets at the octave centres within 0.15 dB, the response never
//    exceeds the largest target (stability), equal targets give a flat response.
// 2. The reverb loop itself (RotaryRoomDSP::reverbImpulse): T60 per octave of its impulse response in the rehearsal
//    room and in the church against the Eyring figures the model reports (within 12 %) - the church's 8 kHz octave
//    much shorter than its 500 Hz octave (air), the rehearsal room's bass shorter than its treble (drywall absorbs
//    bass).
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
#include <vector>
using namespace rotary;

static int fails = 0;
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static void designChecks() {
    printf("-- filter design --\n");
    const double fs = 48000;
    const double tgt[OCT_BANDS] = {0.97, 0.96, 0.95, 0.93, 0.9, 0.8, 0.55}; // a long room with air loss
    OctaveDecay d;
    d.design(tgt, fs);
    double worst = 0;
    for (int b = 0; b < OCT_BANDS; b++) {
        const double h = std::abs(OctaveDecay::response(d.k, d.g, OCT_HZ[b], fs));
        worst = std::max(worst, std::fabs(20 * std::log10(h / tgt[b])));
    }
    printf("largest error at the centres: %.3f dB\n", worst);
    check(worst < 0.15, "band targets met at the octave centres");
    double mx = 0;
    for (double f = 20; f < 20000; f *= 1.02) mx = std::max(mx, std::abs(OctaveDecay::response(d.k, d.g, f, fs)));
    printf("largest magnitude anywhere: %.4f (largest target %.2f)\n", mx, tgt[0]);
    check(mx <= tgt[0] + 1e-6, "never above the largest target");
    const double flat[OCT_BANDS] = {0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9};
    d.design(flat, fs);
    double dev = 0;
    for (double f = 20; f < 20000; f *= 1.05)
        dev = std::max(dev, std::fabs(std::abs(OctaveDecay::response(d.k, d.g, f, fs)) - 0.9));
    check(dev < 1e-6, "equal targets: flat");
    // steep bass case (bare concrete: long bass, short treble) at 44.1 and 96 kHz
    const double bass[OCT_BANDS] = {0.99, 0.985, 0.97, 0.95, 0.93, 0.9, 0.85};
    for (double sr : {44100.0, 96000.0}) {
        d.design(bass, sr);
        double w = 0, m = 0;
        for (int b = 0; b < OCT_BANDS; b++)
            w = std::max(
                w, std::fabs(20 * std::log10(std::abs(OctaveDecay::response(d.k, d.g, OCT_HZ[b], sr)) / bass[b])));
        for (double f = 20; f < sr * 0.49; f *= 1.02) m = std::max(m, std::abs(OctaveDecay::response(d.k, d.g, f, sr)));
        printf("%.0f Hz: error %.3f dB, max %.4f\n", sr, w, m);
        check(w < 0.15 && m <= bass[0] + 1e-6, "other sample rates");
    }
}

/** Octave-band levels over time by STFT (Hann 4096, hop 480 = 10 ms): no leakage between bands worth mentioning,
 * unlike a band-pass cascade (the 8 kHz octave of a reverb tail is 20 dB under 4 kHz and would show its decay). */
static std::vector<std::vector<double>> octaveLevels(const std::vector<float>& x, size_t from, double fs) {
    const size_t NF = 4096, hop = (size_t) (0.01 * fs);
    std::vector<double> win(NF);
    for (size_t i = 0; i < NF; i++) win[i] = 0.5 - 0.5 * std::cos(TWO_PI * i / NF);
    std::vector<std::vector<double>> lv(OCT_BANDS);
    std::vector<std::complex<double>> X(NF);
    for (size_t s0 = from; s0 + NF <= x.size(); s0 += hop) {
        for (size_t i = 0; i < NF; i++) X[i] = x[s0 + i] * win[i];
        // radix-2 FFT
        for (size_t i = 1, j = 0; i < NF; i++) {
            size_t bit = NF >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(X[i], X[j]);
        }
        for (size_t len = 2; len <= NF; len <<= 1) {
            const std::complex<double> wl = std::exp(std::complex<double>(0, -TWO_PI / (double) len));
            for (size_t i = 0; i < NF; i += len) {
                std::complex<double> w = 1;
                for (size_t j = 0; j < len / 2; j++) {
                    const std::complex<double> u = X[i + j], v = X[i + j + len / 2] * w;
                    X[i + j] = u + v;
                    X[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        for (int b = 0; b < OCT_BANDS; b++) {
            const double lo = OCT_HZ[b] / 1.41421356, hi = OCT_HZ[b] * 1.41421356;
            double e = 0;
            for (size_t k = (size_t) (lo * NF / fs); k <= (size_t) (hi * NF / fs) && k < NF / 2; k++)
                e += std::norm(X[k]);
            lv[b].push_back(10 * std::log10(e + 1e-30));
        }
    }
    return lv;
}

/** Impulse response of the reverb loop in the given room: T60 per octave (line fitted to the STFT band levels
 * between -10 and -50 dB) and the Eyring figures the model reports. */
static void measure(const Room& r, double t60[OCT_BANDS], double eyring[OCT_BANDS]) {
    const double fs = 48000;
    RotaryRoomDSP dsp(fs);
    Speakers s;
    s.az = {30, -30};
    s.ch = {0, 1};
    s.stereo = true;
    dsp.setSpeakers(s);
    dsp.setRoom(r);
    const auto& info = dsp.info();
    for (int b = 0; b < OCT_BANDS; b++) eyring[b] = info.rt60[b];
    const std::vector<float> ir = dsp.reverbImpulse((int) (std::min(12.0, 2.0 * eyring[1] + 1.0) * fs));
    const auto levels = octaveLevels(ir, 0, fs);
    for (int b = 0; b < OCT_BANDS; b++) {
        const auto& lv = levels[(size_t) b];
        const double top = lv[5] - 10, bottom = top - 40;
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for (size_t w = 5; w < lv.size(); w++) {
            if (lv[w] > top) continue;
            if (lv[w] < bottom) break;
            const double x = w * 0.01;
            sx += x;
            sy += lv[w];
            sxx += x * x;
            sxy += x * lv[w];
            n++;
        }
        const double slope = n > 3 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0; // dB/s
        t60[b] = slope < 0 ? -60 / slope : 0;
    }
}

int main() {
    designChecks();
    printf("-- reverb loop --\n");
    Room jam;
    jam.Lx = 6;
    jam.Ly = 8;
    jam.Lz = 2.8;
    jam.absorb = 0.3;
    jam.order = 2;
    jam.material = "wood";
    Room church;
    church.Lx = 16;
    church.Ly = 36;
    church.Lz = 14;
    church.absorb = 0.09;
    church.reverb = 1.1;
    church.order = 2;
    church.material = "stone";
    for (int which = 0; which < 2; which++) {
        double t[OCT_BANDS], ey[OCT_BANDS];
        measure(which ? church : jam, t, ey);
        printf("%s\n   octave   ", which ? "church (stone)" : "rehearsal room (wood)");
        for (int b = 0; b < OCT_BANDS; b++) printf("%7.0f", OCT_HZ[b]);
        printf("\n   Eyring   ");
        for (int b = 0; b < OCT_BANDS; b++) printf("%7.2f", ey[b]);
        printf("\n   measured ");
        for (int b = 0; b < OCT_BANDS; b++) printf("%7.2f", t[b]);
        printf("\n");
        bool within = true;
        for (int b = 0; b < OCT_BANDS; b++) within = within && t[b] > 0.88 * ey[b] && t[b] < 1.12 * ey[b];
        check(within, "T60 per octave within 12 % of Eyring");
        if (which == 0)
            check(t[5] > t[1] * 1.15, "wood room: drywall swallows the bass, the treble rings longer");
        else
            check(t[6] < 0.5 * t[2] && t[2] > 3, "church: 8 kHz dies in the air, 500 Hz stays long");
    }
    printf("\n%s\n", fails ? "FAILURES" : "all ok");
    return fails ? 1 : 0;
}
