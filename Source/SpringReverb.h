#pragma once
// Wheelhouse – Federhall (Hammond/Accutronics-Tank, wie in A-100 und Leslie 122RV: in der Orgel, vor dem Leslie)
//
// Modell nach Välimäki, Parker & Abel („Parametric spring reverberation effect“, 2010) und Parker („Efficient
// dispersion generation structures for spring reverb emulation“, 2011):
//   * Jede Feder ist eine Rückkopplungsschleife aus Verzögerung (Laufzeit der Feder), einer langen Kette
//     gedehnter Allpässe (Dispersion: tiefe Frequenzen kommen früher an als hohe → das typische „Zirpen“),
//     Tiefpass an der Übergangsfrequenz und Dämpfung je Umlauf.
//   * Die Laufzeit schwankt leicht (Federn sind nicht starr) – das gibt das lebendige „Tropfen“.
//   * Zwei oder drei Federn mit verschiedenen Längen laufen parallel.
//   * Treiberstufe vor dem Tank sättigt weich; der Wandler am Ausgang ist bandbegrenzt (ca. 100 Hz – 5 kHz).
//   * „Anstoßen“: ein Stoß gegen den Tank – die Federn schlagen hart an (der berühmte Krach).

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace leslie {

class SpringReverb {
public:
    explicit SpringReverb(double sampleRate) : fs(sampleRate) { configure(); }

    void setEnabled(bool on) { enabled = on; }
    void setMix(double m) { mix = std::max(0.0, std::min(1.0, m)); }
    void setDecay(double t60) { decay = std::max(0.5, std::min(8.0, t60)); updateGains(); }
    void setTone(double t) { tone = std::max(0.0, std::min(1.0, t)); toneLp.set("lp", 2200 + 3300 * tone, 0.6, fs); }
    void setDrive(double d) { drive = std::max(0.0, std::min(1.0, d)); }
    void setSprings(int n) { n = std::max(2, std::min(3, n)); if (n != nSpr) { nSpr = n; updateGains(); } }
    /** Stoß gegen den Tank */
    void kick(double strength = 1) { if (!enabled) return; kickEnv = std::max(kickEnv, std::max(0.2, std::min(1.5, strength))); kickPh = 0; }
    bool isEnabled() const { return enabled; }

    /** mono, in place: trocken bleibt, der Hall kommt dazu */
    void process(float* x, int n) {
        if (!enabled && !ringing) return;
        const double wet = mix;
        const double kDrv = 0.6 + 5.0 * drive;
        double peak = 0;
        for (int i = 0; i < n; i++) {
            double dry = x[i];
            // Treiber (Röhre auf den Wandler): weich begrenzt, Bässe schon vorher schwach (Wandler)
            double in = enabled ? inHp.run(dry) : 0;
            in = std::tanh(in * kDrv) / kDrv * 1.4;
            // Stoß: kurzer, sehr lauter Rauschburst plus tiefer Schlag
            if (kickEnv > 1e-4) {
                double nz = rnd() * 2 - 1;
                kickPh += 1 / fs;
                in += kickEnv * (nz * 2.2 + 3.0 * std::sin(TWO_PI_S * 38 * kickPh)) * std::exp(-kickPh / 0.025);
                if (kickPh > 0.15) kickEnv = 0;
            }
            double sum = 0;
            for (int s = 0; s < nSpr; s++) sum += springTick(spr[s], in) * (s == 1 ? -1 : 1);
            double y = outHp.run(outLp2.run(outLp.run(sum)));
            y = toneLp.run(y);
            peak = std::max(peak, std::fabs(y));
            x[i] = (float) (dry + y * wet * outGain);
        }
        ringing = peak > 1e-5;
    }

private:
    static constexpr double TWO_PI_S = 6.283185307179586;
    static constexpr int NAP = 90;      // Allpässe je Feder

    struct Spring {
        std::vector<float> dl; int len = 1000, idx = 0;
        std::vector<float> apX, apY; int K = 6, apIdx = 0;   // gedehnte Allpässe: gemeinsamer Ringpuffer der Länge K je Stufe
        double a = 0.6, g = 0.7, fbz = 0;
        Biquad lp1, lp2, dcb;
        double modPh = 0, modInc = 0, modDepth = 0, modZ = 0, td = 0.035;
        double tapFrac = 0.3; int tapLen = 300;
    };

    double springTick(Spring& S, double in) {
        // Laufzeit leicht schwankend (gefiltertes Rauschen + langsamer Sinus)
        S.modPh += S.modInc; if (S.modPh > 1) S.modPh -= 1;
        S.modZ += 0.0008 * ((rnd() * 2 - 1) - S.modZ);
        double off = S.len - 2 - S.modDepth * (0.5 + 0.35 * std::sin(TWO_PI_S * S.modPh) + 6 * S.modZ);
        double rp = S.idx - off; const int L = (int) S.dl.size(); while (rp < 0) rp += L;
        int i0 = (int) rp; double fr = rp - i0;
        double d = S.dl[(size_t) (i0 % L)] + (S.dl[(size_t) ((i0 + 1) % L)] - S.dl[(size_t) (i0 % L)]) * fr;
        // Dispersion: Kaskade gedehnter Allpässe  H(z) = (a + z^-K) / (1 + a z^-K)
        const int K = S.K, j = S.apIdx; const double a = S.a;
        double v = d;
        float* X = S.apX.data(); float* Y = S.apY.data();
        for (int k = 0; k < NAP; k++) {
            const int base = k * K;
            double xk = X[base + j], yk = Y[base + j];       // Werte von vor K Takten
            double y = a * v + xk - a * yk;
            X[base + j] = (float) v; Y[base + j] = (float) y;
            v = y;
        }
        S.apIdx = (j + 1) % K;
        v = S.lp2.run(S.lp1.run(v));
        v = S.dcb.run(v);
        // Rückkopplung mit Dämpfung, Eingang dazu
        double w = in + v * S.g;
        S.dl[(size_t) S.idx] = (float) w; S.idx = (S.idx + 1) % L;
        return v;
    }

    void configure() {
        // Übergangsfrequenz der Dispersion ~4,3 kHz: Dehnung K = fs / (2·fc)
        const double fc = 4300;
        int K = std::max(1, (int) std::lround(fs / (2 * fc)));
        const double TD[3] = {0.0331, 0.0397, 0.0289};    // Laufzeiten der Federn (Typ 4, lang)
        const double AA[3] = {0.62, 0.58, 0.66};
        for (int s = 0; s < 3; s++) {
            Spring& S = spr[s];
            S.td = TD[s]; S.K = K; S.a = AA[s];
            // die Allpasskette verzögert selbst: Gruppenlaufzeit bei tiefen Frequenzen abziehen
            double apDelay = NAP * K * (1 - S.a) / (1 + S.a);
            S.len = std::max(16, (int) std::lround(S.td * fs - apDelay));
            S.dl.assign((size_t) S.len + 64, 0.f); S.idx = 0;
            S.apX.assign((size_t) (NAP * K), 0.f); S.apY.assign((size_t) (NAP * K), 0.f); S.apIdx = 0;
            S.lp1.set("lp", fc * 0.92, 0.707, fs); S.lp2.set("lp", fc * 0.92, 0.9, fs);
            S.dcb.set("hp", 35, 0.7, fs);
            S.modInc = (0.37 + 0.21 * s) / fs; S.modDepth = 0.00025 * fs; S.modPh = 0.3 * s;
            S.tapLen = std::max(8, (int) std::lround(S.len * (0.45 + 0.07 * s)));
        }
        inHp.set("hp", 120, 0.7, fs);
        outLp.set("lp", 5200, 0.7, fs); outLp2.set("lp", 6000, 0.6, fs); outHp.set("hp", 90, 0.7, fs);
        setTone(tone);
        updateGains();
    }
    void updateGains() {
        for (int s = 0; s < 3; s++) spr[s].g = std::pow(10.0, -3.0 * spr[s].td * 1.3 / decay);   // 1,3: Kette und Tiefpässe verlängern die effektive Umlaufzeit
        outGain = 0.55 / std::sqrt((double) nSpr);
    }
    double rnd() { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0; }

    const double fs;
    bool enabled = false, ringing = false;
    double mix = 0.25, decay = 2.8, tone = 0.5, drive = 0.3, outGain = 0.3;
    int nSpr = 3;
    double kickEnv = 0, kickPh = 0;
    uint32_t seed = 0x5EED1234u;
    Spring spr[3];
    Biquad inHp, outLp, outLp2, outHp, toneLp;
};

} // namespace leslie
