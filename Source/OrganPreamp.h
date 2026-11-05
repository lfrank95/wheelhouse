#pragma once
// Wheelhouse – organ preamp modelled on the AO-28 (output section after the swell/expression pedal)
//
// Signal path in the original: matching transformer → input stages (12AX7, 6AU6) → vibrato scanner → swell pedal →
// voltage amplifier → 12BH7 push-pull output stage → output transformer → Rotary (balanced input).
// Only the part after the swell pedal is simulated, because that is where overdrive occurs: the further the swell
// pedal is open and the more drawbars are pulled, the more level reaches the 12AX7 and the 12BH7.
//
//   Eco   static transfer curves (12AX7 asymmetric, push-pull symmetric and soft)
//   HQ    circuit: 12AX7 solved with Newton iteration, coupling capacitor with grid current (blocking distortion),
//         12BH7 push-pull with shared cathode, transformer with core saturation and hysteresis, 2× oversampling
//   Ultra additionally Miller capacitance of the 12AX7, sagging plate supply, 12BH7 cathode bias with its own
//         time constant, eddy currents in the core, 4× oversampling
//
// The tube and core models are based on dsp/glow.c and dsp/iron.c from Amberwave (AGPL-3.0, © The Amberwave
// contributors). 12AX7 tube data per Koren; 12BH7 approximated (μ ≈ 16.5, roughly twice the current of a 12AU7).

#include "RotaryAmp.h"

namespace rotary {
namespace amp {

static const Triode T_12AX7 { 100, 1.4, 1060, 600, 300, 2000, -0.2, 0.06 };
static const Triode T_12BH7 { 16.5, 1.35, 620, 78, 300, 1500, -0.7, 0.14 };

class OrganPreamp {
public:
    explicit OrganPreamp(double sampleRate) : fs(sampleRate) { configure(); }

    /** Quality level 0..2 (Eco / HQ / Ultra), see file header. Rebuilds the circuit. */
    void setQuality(int q) { q = std::max(0, std::min(2, q)); if (q != quality) { quality = q; configure(); } }
    /** Gain 0..1: input level into the 12AX7 grid (log scale, 0.9 V .. 10.8 V per unit input); affects saturation only. */
    void setGain(double g) { gain = std::max(0.0, std::min(1.0, g)); kin = 0.9 * std::pow(12.0, gain); }   // volts at the 12AX7 grid per unit input
    /** Enable/bypass the preamp; when disabled, process() leaves the signal untouched. */
    void setEnabled(bool on) { enabled = on; }
    int getQuality() const { return quality; }
    /** True when the preamp is active. */
    bool isEnabled() const { return enabled; }
    /** Latency in samples introduced by the oversampling filters (0 without oversampling). */
    int latency() const { return ovs.delay; }

    /** Mono, in place; x is roughly ±0.3 with full organ and open swell pedal */
    void process(float* x, int n) {
        if (!enabled) return;
        if (quality == 0) { processEco(x, n); return; }
        double up[8], dn[8];
        const int OS = ovs.os;
        for (int i = 0; i < n; i++) {
            ovs.upsample((double) x[i] * kin, up);
            for (int k = 0; k < OS; k++) dn[k] = tick(up[k]);
            double y = ovs.downsample(dn);
            double w = y - dcX + dcR * dcY; dcX = y; dcY = w;
            x[i] = (float) (w * outNorm / kin);
        }
    }

private:
    // ---------- Eco ----------
    void processEco(float* x, int n) {
        for (int i = 0; i < n; i++) {
            double u = (double) x[i] * kin;
            // 12AX7: soft limiting, earlier on the positive side (grid current) than on the negative side (cutoff)
            double a = u * 0.55;
            double s1 = a > 0 ? std::tanh(a * 1.25) / 1.25 : std::tanh(a * 0.8) / 0.8;
            // 12BH7 push-pull: symmetric
            double s2 = std::tanh(s1 * 1.6) / 1.6;
            double w = s2 - dcX + dcR * dcY; dcX = s2; dcY = w;
            x[i] = (float) (w / (0.55 * kin) * 1.0);
        }
    }

    // ---------- Circuit (one step at os·fs; u = volts at the 12AX7 grid) ----------
    double tick(double u) {
        const double h = dt;
        // Ultra: Miller capacitance of the 12AX7 against the source resistance of the swell pedal → high-frequency roll-off
        if (quality == 2) { millerY += millerK * (u - millerY); u = millerY; }
        // --- 12AX7 voltage amplifier, cathode bypassed ---
        // grid current at the input: the swell pedal has a high source impedance, so positive peaks are clipped softly
        double Vgk = u - Vk1;
        for (int it = 0; it < 2; it++) { double ig0, dig0; gridCurrent(T_12AX7.rgi, T_12AX7.vth, T_12AX7.w, Vgk, ig0, dig0); double f = Vgk - (u - Vk1) + Rsrc * ig0; Vgk -= f / (1 + Rsrc * dig0); }
        double Vp = vp1, ip = 0, gp = 0, gm = 0;
        for (int it = 0; it < itA; it++) {
            triode(T_12AX7, Vgk, Vp - Vk1, ip, gp, gm);
            double f = Vp - Vb + Ra1 * ip, df = 1 + Ra1 * gp;
            double d = f / df; Vp -= d;
            if (Vp < Vk1 + 0.5) Vp = Vk1 + 0.5; else if (Vp > Vb) Vp = Vb;
            if (std::fabs(d) < NEWTON_TOL_V) break;   // quadratic convergence: a step this small means the next error is far below 1 mV
        }
        vp1 = Vp; double ip1 = ip;
        double rout1 = 1 / (1 / Ra1 + std::max(gp, 1e-8));
        Vk1 += h * (ip1 - Vk1 / Rk1) / Ck1;
        // --- Divider and phase inversion (transformer driver in the original) → two coupling capacitors ---
        double vsig = (Vp - vp1Rest) * divK;                 // AC component, attenuated
        double Vg[2], ig[2];
        for (int j = 0; j < 2; j++) {
            double src = (j == 0 ? vsig : -vsig);
            double Req = rout1 * divK * divK + Rs2 + h / Cc;
            double s = src - ccQ[j];
            double Vgj = s * Rg / (Rg + Req), igj = 0, dig = 0;
            if (Vgj - Vk2 > T_12BH7.vth - 12 * T_12BH7.w) {
                for (int it = 0; it < 6; it++) {
                    gridCurrent(T_12BH7.rgi, T_12BH7.vth, T_12BH7.w, Vgj - Vk2, igj, dig);
                    double f = (s - Vgj) / Req - Vgj / Rg - igj, df = -1 / Req - 1 / Rg - dig;
                    double d = f / df; Vgj -= d;
                    if (std::fabs(d) < NEWTON_TOL_V) break;   // quadratic convergence: a step this small means the next error is far below 1 mV
                }
                gridCurrent(T_12BH7.rgi, T_12BH7.vth, T_12BH7.w, Vgj - Vk2, igj, dig);
            }
            double I = (s - Vgj) / Req;
            ccQ[j] += h * I / Cc;
            Vg[j] = Vgj; ig[j] = igj;
        }
        // --- 12BH7 push-pull into the transformer: v = half the primary voltage ---
        double Im = magnetizing();
        double v = vPrev, ia = 0, ib = 0;
        for (int it = 0; it < itB; it++) {
            double gpa, gma, gpb, gmb;
            triode(T_12BH7, Vg[0] - Vk2, Vb - v - Vk2, ia, gpa, gma);
            triode(T_12BH7, Vg[1] - Vk2, Vb + v - Vk2, ib, gpb, gmb);
            double F = 0.5 * (ia - ib) - 2 * v / Raa - Im;
            double dF = -0.5 * (gpa + gpb) - 2 / Raa;
            double d = F / dF; v -= d;
            double vmax = Vb - Vk2 - 2;
            if (v > vmax) v = vmax; else if (v < -vmax) v = -vmax;
            if (std::fabs(d) < NEWTON_TOL_OUT_V) break;
        }
        vPrev = v;
        coreUpdate(2 * v);
        // 12BH7 cathode (shared, bypassed; Ultra: smaller capacitor → audible breathing of the bias)
        Vk2 += h * (ia + ib + ig[0] + ig[1] - Vk2 / Rk2) / Ck2;
        // plate supply (Ultra: the RC filtering sags under load)
        if (quality == 2) Vb += h * ((V0 - Vb) / Rsup - (ia + ib + ip1 + Iq)) / Csup;
        return 2 * v;
    }

    double coreH(double p, bool commit) {
        double bh = 0;
        for (int j = 0; j < PLAYS; j++) {
            double r = playR[j], y = play[j];
            if (p - y > r) y = p - r; else if (y - p > r) y = p + r;
            if (commit) play[j] = y;
            bh += playW[j] * y;
        }
        double b = (1 - hystW) * p + hystW * bh;
        double q = std::min(0.96, b * b / (bs * bs));
        return b / (1 - q);
    }
    double magnetizing() { return (coreH(phi, false) + eddy) * imScale; }
    void coreUpdate(double vPrim) {
        double dphi = dt * vPrim * fluxK;
        if (quality == 2) eddy = eddyK * dphi / dt * 1e-3;
        // winding resistance: the DC component of the flux decays (R/L ≈ 0.3 s); otherwise the slightest tube
        // mismatch would slowly push the core into saturation
        phi += dphi - phi * dt / 0.3;
        if (phi > bs * 0.98) phi = bs * 0.98; else if (phi < -bs * 0.98) phi = -bs * 0.98;
        coreH(phi, true);
    }

    void configure() {
        int os = quality == 0 ? 1 : quality == 1 ? (fs >= 75000 ? 1 : 2) : (fs >= 150000 ? 1 : fs >= 75000 ? 2 : 4);
        ovs.init(os);
        dt = 1 / (fs * os);
        dcR = 1 - 2 * PI * 4 / fs;
        itA = quality == 2 ? 6 : 3; itB = quality == 2 ? 8 : 4;
        // circuit values (from the AO-28 schematic, rounded)
        V0 = 285; Vb = 285; Ra1 = 100e3; Rk1 = 1500; Ck1 = 10e-6;
        divK = 0.14; Rs2 = 47e3; Cc = 0.05e-6; Rg = 470e3;
        Rk2 = 560; Ck2 = quality == 2 ? 25e-6 : 100e-6;
        Raa = 10e3; Rsup = 2200; Csup = 20e-6; Iq = 0.002;
        // Miller: Cgp 1.6 pF × (1 + gain ≈ 60) against about 150 kΩ source impedance → corner around 10 kHz
        double fM = 14000; millerK = 1 - std::exp(-2 * PI * fM * dt);
        // transformer: smaller core than in the Rotary, primary inductance ~60 H
        bs = 1.3; hystW = 0.4; double hyst = 0.06;
        double ws = 0; for (int j = 1; j <= PLAYS; j++) { playR[j - 1] = hyst * j / PLAYS; playW[j - 1] = j; ws += j; }
        for (int j = 0; j < PLAYS; j++) playW[j] /= ws;
        fluxK = 0.8 * bs * 2 * PI * 30 / 500;
        imScale = 1 / (fluxK * 60.0);
        eddyK = quality == 2 ? 0.02 : 0;
        settle();
        calibrate();
    }

    void reset() {
        Vb = V0; vp1 = 160; vp1Rest = 160; Vk1 = 1.4; Vk2 = 8; ccQ[0] = ccQ[1] = 0; vPrev = 0; phi = 0; eddy = 0; millerY = 0;
        std::memset(play, 0, sizeof play);
    }
    void settle() {
        reset(); dcX = dcY = 0;
        if (quality == 0) return;
        int steps = (int) (0.5 / dt);
        for (int i = 0; i < steps; i++) tick(0);
        vp1Rest = vp1;                                     // 12AX7 plate operating point (AC component is measured from here)
        for (int i = 0; i < steps / 4; i++) tick(0);
    }
    /** Normalise the small-signal gain to 1: played cleanly, the organ gets louder or quieter only via the swell pedal */
    void calibrate() {
        if (quality == 0) return;
        struct S { double a[16]; double pl[PLAYS]; } sv;
        double* p = sv.a;
        *p++ = Vb; *p++ = vp1; *p++ = Vk1; *p++ = Vk2; *p++ = ccQ[0]; *p++ = ccQ[1]; *p++ = vPrev; *p++ = phi; *p++ = eddy; *p++ = millerY;
        std::memcpy(sv.pl, play, sizeof play);
        Oversampler o2 = ovs;
        double amp = 0.02, sIn = 0, sOut = 0, x1 = 0, y1 = 0, up[8], dn[8];
        int n = (int) (fs * 0.05);
        for (int i = 0; i < n; i++) {
            double xin = amp * std::sin(2 * PI * 1000 * i / fs);
            o2.upsample(xin, up);
            for (int k = 0; k < o2.os; k++) dn[k] = tick(up[k]);
            double y = o2.downsample(dn);
            double w = y - x1 + dcR * y1; x1 = y; y1 = w;
            if (i > n / 2) { sIn += xin * xin; sOut += w * w; }
        }
        double g = std::sqrt(sOut / std::max(sIn, 1e-30));
        outNorm = 1.0 / std::max(g, 1e-6);
        p = sv.a;
        Vb = *p++; vp1 = *p++; Vk1 = *p++; Vk2 = *p++; ccQ[0] = *p++; ccQ[1] = *p++; vPrev = *p++; phi = *p++; eddy = *p++; millerY = *p++;
        std::memcpy(play, sv.pl, sizeof play);
    }

    double fs;   // not const: copy-assigned when a quality change is prepared off the audio thread
    int quality = 1; bool enabled = true;
    double gain = 0.5, kin = 0.9 * std::pow(12.0, 0.5);
    Oversampler ovs;
    double dt = 1e-5, dcX = 0, dcY = 0, dcR = 0.9995, outNorm = 1;
    int itA = 3, itB = 4;
    double V0 = 285, Vb = 285, Ra1 = 100e3, Rk1 = 1500, Ck1 = 10e-6, divK = 0.14, Rs2 = 47e3, Cc = 0.05e-6, Rg = 470e3;
    double Rk2 = 560, Ck2 = 100e-6, Rsrc = 68e3, Raa = 10e3, Rsup = 2200, Csup = 20e-6, Iq = 0.002, millerK = 0.5;
    double vp1 = 160, vp1Rest = 160, Vk1 = 1.4, Vk2 = 8, ccQ[2] = {0, 0}, vPrev = 0, millerY = 0;
    static constexpr int PLAYS = 6;
    double bs = 1.3, hystW = 0.4, playR[PLAYS] = {0}, playW[PLAYS] = {0}, play[PLAYS] = {0};
    double phi = 0, fluxK = 0.4, imScale = 0.1, eddy = 0, eddyK = 0;
};

} // namespace amp
} // namespace rotary
