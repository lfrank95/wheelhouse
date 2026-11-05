// Wheelhouse – Leslie power amplifier (122 / early 122 / 147) modelled as a circuit.
//
// Topology follows the 122: balanced input → 12AU7 as push-pull driver (147: phase splitter as a long-tailed pair)
// → coupling capacitors with grid-leak resistor → 2 × 6550 in push-pull, class AB, shared cathode resistor
// 150 Ω (about 25 V cathode voltage) → output transformer → crossover/speakers. Screen grids on the OC3-regulated 310 V,
// plates on about 415 V behind the rectifier and choke (early 122: 5U4 rectifier tube, otherwise diodes).
//
// Tubes per Koren (triode and pentode), grid current, cathode and power-supply dynamics; transformer core with
// H(B) = B / (1 − (B/Bs)²) and hysteresis via play operators. The numerical methods (Koren solved per sample with Newton,
// grid current, core with play operators, Kaiser-windowed oversampling) follow Amberwave Glow HQ/Ultra and Iron HQ/Ultra
// (dsp/glow.c, dsp/iron.c, AGPL-3.0, The Amberwave contributors) and are rebuilt here for a push-pull power stage.
//
// Quality levels:
//   0 Standard – static push-pull transfer curve with bias shift and sag (very cheap)
//   1 HQ       – circuit solved per sample (2× oversampling): driver, grid current/blocking, cathode bias, sag, transformer with hysteresis
//   2 Ultra    – 4×, choke-input supply (ringing), driver supply through 10 kΩ, screen current, loudspeaker load
//                (voice coil + drum resonance), tube tolerance with drift, eddy currents in the core
#pragma once
#include <cmath>
#include "FastMath.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace leslie {
namespace amp {

constexpr double PI = 3.14159265358979323846;

struct Triode { double mu, ex, kg1, kp, kvb, rgi, vth, w; };
struct Pentode { double mu, ex, kg1, kg2, kp, kvb, rgi, vth, w; };
// 12AU7 as in Amberwave Glow, 6550 per Koren
// Newton termination: once a step is this small the next one would be ~1e-6 V, so the evaluation is skipped
static constexpr double NEWTON_TOL_V = 3e-3, NEWTON_TOL_OUT_V = 3e-2;

static const Triode T_12AU7 { 21.5, 1.3, 1180, 84, 300, 2000, -0.7, 0.12 };
static const Pentode P_6550 { 7.9, 1.35, 890, 4200, 60, 24, 1000, -0.7, 0.18 };

static inline void softplus(double x, double& L, double& sig) {
    if (x > 40) { L = x; sig = 1; }
    else if (x < -40) { L = std::exp(x); sig = L; }
    else { double e = std::exp(x); L = std::log1p(e); sig = e / (1 + e); }
}

/** Triode: plate current and its derivatives with respect to Vpk and Vgk */
static inline void triode(const Triode& T, double Vgk, double Vpk, double& ip, double& gp, double& gm) {
    if (Vpk < 1e-3) Vpk = 1e-3;
    double s = std::sqrt(T.kvb + Vpk * Vpk);
    double L, sig; softplus(T.kp * (1 / T.mu + Vgk / s), L, sig);
    double E1 = (Vpk / T.kp) * L;
    if (E1 <= 1e-12) { ip = 0; gp = 0; gm = 0; return; }
    double p = std::exp((T.ex - 1) * std::log(E1));
    ip = 2 * p * E1 / T.kg1;
    double k = 2 * T.ex * p / T.kg1;
    gp = k * (L / T.kp - Vpk * sig * Vgk * Vpk / (s * s * s));
    gm = k * Vpk * sig / s;
}

/** Pentode: the part that depends only on the grids (evaluated once per sample) */
struct PentGrid { double a, da; };   // a = 2·E1^ex/kg1, da = d a / d Vg1k
static inline PentGrid pentGrid(const Pentode& P, double Vg1k, double Vg2k, double kgScale) {
    if (Vg2k < 1) Vg2k = 1;
    double L, sig; softplus(P.kp * (1 / P.mu + Vg1k / Vg2k), L, sig);
    double E1 = (Vg2k / P.kp) * L;
    if (E1 <= 1e-12) return { 0, 0 };
    double e = std::exp(P.ex * std::log(E1));
    double kg = P.kg1 * kgScale;
    return { 2 * e / kg, 2 * P.ex * e / E1 / kg * sig };
}
static inline double pentAtan(const Pentode& P, double Vpk) { return fastmath::atanFast(std::max(Vpk, 0.5) / P.kvb); }
static inline double pentAtanD(const Pentode& P, double Vpk) { double x = std::max(Vpk, 0.5) / P.kvb; return (1 / P.kvb) / (1 + x * x); }
static inline double screenCurrent(const Pentode& P, double Vg1k, double Vg2k, double kgScale) {
    double x = Vg1k + Vg2k / P.mu;
    return x > 0 ? std::exp(P.ex * std::log(x)) / (P.kg2 * kgScale) : 0;
}

/** Grid current (sets in softly from about vth) */
static inline void gridCurrent(double rgi, double vth, double w, double Vgk, double& ig, double& dig) {
    double xg = (Vgk - vth) / w;
    if (xg > 30) { ig = (Vgk - vth) / rgi; dig = 1 / rgi; return; }
    if (xg < -25) { ig = 0; dig = 0; return; }   // below this the current is < 1e-11 A: nothing to compute
    double e = std::exp(xg);
    ig = w * std::log1p(e) / rgi; dig = (e / (1 + e)) / rgi;
}

// ---------------- Oversampling (Kaiser-windowed sinc, polyphase upsampling, downsampling tapped at phase 0) ----------------
static inline double besselI0(double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }

struct Oversampler {
    int os = 1, taps = 1, ph = 1, delay = 0;
    std::vector<double> h, up, down;   // h: length ph*os
    std::vector<double> hUpRev, hDownRev;   // polyphase rows / full filter, stored reversed so the FIRs are contiguous dot products
    std::vector<double> ringUp, ringDown;
    int posUp = 0, posDown = 0;
    void init(int factor) {
        os = factor;
        if (os == 1) { taps = 1; ph = 1; delay = 0; return; }
        taps = 32 * os + 1; ph = (taps + os - 1) / os; delay = (taps - 1) / os;
        h.assign((size_t) ph * os, 0.0);
        double m = (taps - 1) / 2.0, beta = 8.0, sum = 0;
        for (int i = 0; i < taps; i++) {
            double n = (i - m) / os, sinc = n == 0 ? 1 : std::sin(PI * n) / (PI * n);
            double r = 2.0 * i / (taps - 1) - 1;
            h[(size_t) i] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / besselI0(beta);
            sum += h[(size_t) i];
        }
        for (auto& v : h) v *= os / sum;   // gain of os when upsampling (zeros are inserted)
        ringUp.assign((size_t) ph * 2, 0.0); ringDown.assign((size_t) taps * 2, 0.0); posUp = posDown = 0;
        hUpRev.assign((size_t) os * ph, 0.0); hDownRev.assign((size_t) taps, 0.0);
        for (int k = 0; k < os; k++) for (int j = 0; j < ph; j++) hUpRev[(size_t) (k * ph + j)] = h[(size_t) (k + (ph - 1 - j) * os)];
        for (int j = 0; j < taps; j++) hDownRev[(size_t) j] = h[(size_t) (taps - 1 - j)];
    }
    // one input sample → os output samples
    void upsample(double x, double* out) {
        if (os == 1) { out[0] = x; return; }
        ringUp[(size_t) posUp] = x; ringUp[(size_t) (posUp + ph)] = x;
        const double* win = ringUp.data() + posUp + 1;   // the ph most recent inputs, oldest first
        for (int k = 0; k < os; k++) {
            const double* hk = hUpRev.data() + (size_t) k * ph;
            double s = 0;
            for (int j = 0; j < ph; j++) s += hk[j] * win[j];   // output k: sum over j of h[k + j*os] * x[n - j]
            out[k] = s;
        }
        posUp = posUp == ph - 1 ? 0 : posUp + 1;
    }
    // os samples → one output (lowpass, tapped at phase 0)
    double downsample(const double* in) {
        if (os == 1) return in[0];
        double y = 0;
        for (int k = 0; k < os; k++) {
            ringDown[(size_t) posDown] = in[k]; ringDown[(size_t) (posDown + taps)] = in[k];
            if (k == 0) {
                const double* win = ringDown.data() + posDown + 1; const double* hr = hDownRev.data();
                double s = 0;
                for (int j = 0; j < taps; j++) s += hr[j] * win[j];
                y = s / os;
            }
            posDown = posDown == taps - 1 ? 0 : posDown + 1;
        }
        return y;
    }
};

// ---------------- the power amplifier ----------------
class LeslieAmp {
public:
    enum Model { M122 = 0, M122_EARLY = 1, M147 = 2 };

    explicit LeslieAmp(double sampleRate) : fs(sampleRate) { configure(); }

    /** Amp model: 0 = 122, 1 = early 122 (tube rectifier), 2 = 147 (long-tailed-pair splitter, cathode feedback). Rebuilds the circuit. */
    void setModel(int m) { if (m != model) { model = std::max(0, std::min(2, m)); updateKin(); configure(); } }
    /** Quality level 0..2 (Standard / HQ / Ultra), see file header. Rebuilds the circuit. */
    void setQuality(int q) { if (q != quality) { quality = std::max(0, std::min(2, q)); configure(); } }
    /** Drive 0..1: input level into the driver grid (log scale, 1.2 V .. 36 V); changes only saturation, not loudness. */
    void setDrive(double d) { drive = std::max(0.0, std::min(1.0, d)); updateKin(); }
    /** Recomputes the input scaling (volts at the driver grid per unit input) from drive and model. */
    void updateKin() { kin = 1.2 * std::pow(30.0, drive) * (model == M147 ? 1.8 : 1.0); }
    /** Sag 0..1: power-supply source resistance (0 = stiff supply, 1 = soft, strong compression). Rebuilds the circuit. */
    void setSag(double s) { if (std::fabs(s - sag) > 1e-6) { sag = std::max(0.0, std::min(1.0, s)); applySupply(); } }   // cheap: the supply just drifts to its new point
    /** Tube age 0..1: reduced emission of the output tubes (0 = new, 1 = worn). Rebuilds the circuit. */
    void setAge(double a) { if (std::fabs(a - age) > 1e-6) { age = std::max(0.0, std::min(1.0, a)); applyEmission(); } }   // cheap: no re-settle
    /** Current amp model (see Model). */
    int getModel() const { return model; }
    /** Current quality level 0..2. */
    int getQuality() const { return quality; }
    /** Latency in samples introduced by the oversampling filters (0 without oversampling). */
    int latency() const { return ovs.delay; }

    // State for display (volts / mA)
    /** Plate supply voltage Vb in volts (sags under load). */
    double plateSupply() const { return Vb; }
    /** Shared cathode voltage of the output tubes in volts (about 25 V at idle). */
    double cathodeVoltage() const { return Vko; }
    /** Total plate current of both output tubes in mA. */
    double plateCurrentMA() const { return 1000 * (lastIp[0] + lastIp[1]); }

    /** Mono, in place. x: signal from the organ preamp (roughly ±0.3) */
    void process(float* x, int n) {
        if (quality == 0) { processStandard(x, n); return; }
        double up[8], dn[8];
        const int OS = ovs.os;
        for (int i = 0; i < n; i++) {
            ovs.upsample((double) x[i] * kin, up);
            for (int k = 0; k < OS; k++) dn[k] = tick(up[k]);
            double y = ovs.downsample(dn);
            // highpass to remove DC (4 Hz)
            double w = y - dcX + dcR * dcY; dcX = y; dcY = w;
            x[i] = (float) (w * outNorm / kin);
        }
        driftCount += n;
        if (quality == 2 && driftCount > 4096) { driftCount = 0; drift(); }
    }

private:
    // ---------- Standard ----------
    void processStandard(float* x, int n) {
        const double atk = 1 - std::exp(-1 / (0.03 * fs)), rel = 1 - std::exp(-1 / (0.25 * fs));
        const double sagK = 0.15 + 0.6 * sag;
        for (int i = 0; i < n; i++) {
            double u = (double) x[i] * kin * stdGain;            // normalised: 1 ≈ full drive
            double a = std::fabs(u);
            stdEnv += (a > stdEnv ? atk : rel) * (a - stdEnv);
            double us = u / (1 + sagK * stdEnv);
            double b = 0.35 + 0.25 * stdEnv;                      // class AB, cathode bias drifts colder under load
            double g1 = std::tanh(us + b) - std::tanh(b), g2 = std::tanh(-us + b) - std::tanh(b);
            double y = 0.5 * (g1 - 0.96 * g2);
            double w = y - dcX + dcR * dcY; dcX = y; dcY = w;
            x[i] = (float) (w * stdNorm / kin);
        }
    }

    // ---------- Circuit: one step at os·fs; u = volts at the driver grid (balanced: +u / −u) ----------
    double tick(double u) {
        const double h = dt;
        // --- Driver (12AU7, two triode sections) ---
        double ipd[2], vpoc[2], rout[2];
        double ltpShift = 0;
        if (model == M147) {
            // long-tailed pair: the control voltage splits across the shared tail, slightly asymmetric
            ltpShift = 0.5 * u;
        }
        for (int j = 0; j < 2; j++) {
            double sgn = j == 0 ? 1.0 : -1.0;
            double ug = model == M147 ? (j == 0 ? u - ltpShift : -ltpShift * ltpImbalance) : sgn * u;
            double Vgk = ug - Vkd;
            double Vp = drvVp[j];
            double ip = 0, gp = 0, gm = 0;
            const double Ra = drvRa;
            for (int it = 0; it < itDrv; it++) {
                triode(T_12AU7, Vgk, Vp - Vkd, ip, gp, gm);
                double f = Vp - Vbd + Ra * ip, df = 1 + Ra * gp;
                double d = f / df; Vp -= d;
                if (Vp < Vkd + 1) Vp = Vkd + 1; else if (Vp > Vbd) Vp = Vbd;
                if (std::fabs(d) < NEWTON_TOL_V) break;   // quadratic convergence: a step this small means the next error is far below 1 mV
            }
            drvVp[j] = Vp; vpoc[j] = Vp; ipd[j] = ip;
            rout[j] = quality == 2 ? 1 / (1 / Ra + std::max(gp, 1e-7)) : drvRout;
        }
        // --- Coupling capacitor → 6550 grid (with grid current, blocking distortion) ---
        double Vg[2], ig[2];
        for (int j = 0; j < 2; j++) {
            double Req = rout[j] + h / Cc;
            double src = vpoc[j] - ccQ[j];                     // what is "left over" across the capacitor (plate voltage minus capacitor charge)
            double Vgj = src * Rg / (Rg + Req);
            double igj = 0, dig = 0;
            const double VkoJ = Vko + (j == 0 ? beta : -beta) * vPrev;
            if (Vgj - VkoJ > P_6550.vth - 12 * P_6550.w) {
                for (int it = 0; it < 8; it++) {
                    gridCurrent(P_6550.rgi, P_6550.vth, P_6550.w, Vgj - VkoJ, igj, dig);
                    double f = (src - Vgj) / Req - Vgj / Rg - igj;
                    double df = -1 / Req - 1 / Rg - dig;
                    double d = f / df; Vgj -= d;
                    if (std::fabs(d) < NEWTON_TOL_V) break;   // quadratic convergence: a step this small means the next error is far below 1 mV
                }
                gridCurrent(P_6550.rgi, P_6550.vth, P_6550.w, Vgj - VkoJ, igj, dig);
            }
            double I = (src - Vgj) / Req;
            ccQ[j] += h * I / Cc;
            Vg[j] = Vgj; ig[j] = igj;
        }
        // --- Output tubes in push-pull: v = half the primary voltage (Newton iteration) ---
        double Vg2k = Vscreen - Vko;
        PentGrid pg0 = pentGrid(P_6550, Vg[0] - Vko, Vg2k, kgTube[0]);
        PentGrid pg1 = pentGrid(P_6550, Vg[1] - Vko, Vg2k, kgTube[1]);
        // magnetising current from the core flux (explicit, from the previous step)
        double Im = magnetizingCurrent();
        // load: Ifull = 2v·G + Ih
        double G, Ih;
        loadCompanion(G, Ih);
        double v = vPrev;
        double ip0 = 0, ip1 = 0;
        const double be = beta;   // 147: cathode feedback from the secondary winding (centre tap)
        for (int it = 0; it < itOut; it++) {
            if (be > 0) {
                pg0 = pentGrid(P_6550, Vg[0] - Vko - be * v, Vg2k + be * v, kgTube[0]);
                pg1 = pentGrid(P_6550, Vg[1] - Vko + be * v, Vg2k - be * v, kgTube[1]);
            }
            double vp0 = Vb - v - Vko - be * v, vp1 = Vb + v - Vko + be * v;
            double a0 = pentAtan(P_6550, vp0), a1 = pentAtan(P_6550, vp1);
            ip0 = pg0.a * a0; ip1 = pg1.a * a1;
            double d0 = -pg0.da * be * a0 - pg0.a * pentAtanD(P_6550, vp0) * (1 + be);
            double d1 = pg1.da * be * a1 + pg1.a * pentAtanD(P_6550, vp1) * (1 + be);
            double F = 0.5 * (ip0 - ip1) - (2 * v * G + Ih) - Im;
            double dF = 0.5 * (d0 - d1) - 2 * G;
            double d = F / dF; v -= d;
            double vmax = Vb - Vko - 2;
            if (v > vmax) v = vmax; else if (v < -vmax) v = -vmax;
            if (std::fabs(d) < NEWTON_TOL_OUT_V) break;
        }
        vPrev = v;
        lastIp[0] = ip0; lastIp[1] = ip1;
        loadUpdate(2 * v);
        coreUpdate(2 * v);
        // --- Cathode (shared, bypassed) ---
        double is0 = screenCurrent(P_6550, Vg[0] - Vko, Vg2k, kgTube[0]), is1 = screenCurrent(P_6550, Vg[1] - Vko, Vg2k, kgTube[1]);
        double Ik = ip0 + ip1 + is0 + is1 + ig[0] + ig[1];
        Vko += h * (Ik - Vko / Rko) / Cko;
        // driver cathode (shared, bypassed) and driver supply
        Vkd += h * (ipd[0] + ipd[1] - Vkd / Rkd) / Ckd;
        if (quality == 2) Vbd += h * ((Vreg - Vbd) / 10e3 - (ipd[0] + ipd[1])) / 20e-6;
        // --- Power supply ---
        double Ip = ip0 + ip1;
        if (quality == 2) {
            // rectifier → C1 → choke → C2 (plates)
            V1 += h * ((V0 - V1) / Rs - iL) / C1;
            iL += h * (V1 - Vb - RL * iL) / Lch;
            Vb += h * (iL - Ip - is0 * 0 - Ireg) / C2;
        } else {
            Vb += h * ((V0 - Vb) / RsEff - Ip - Ireg) / C2;
        }
        return 2 * v;
    }

    // ---------- Output transformer: core with hysteresis (play operators) ----------
    double coreH(double p, bool commit) {
        double b = p;
        if (hystW > 0) {
            double bh = 0;
            for (int j = 0; j < PLAYS; j++) {
                double r = playR[j], y = play[j];
                if (p - y > r) y = p - r; else if (y - p > r) y = p + r;
                if (commit) play[j] = y;
                bh += playW[j] * y;
            }
            b = (1 - hystW) * p + hystW * bh;
        }
        double q = std::min(0.96, b * b / (bs * bs));
        return b / (1 - q);
    }
    double magnetizingCurrent() { return (coreH(phi, false) + eddy) * imScale; }
    void coreUpdate(double vPrimary) {
        double dphi = dt * vPrimary * fluxK;
        if (quality == 2) eddy = eddyK * dphi / dt * 1e-3;   // eddy current ∝ dΦ/dt
        phi += dphi;
        if (phi > bs * 0.98) phi = bs * 0.98; else if (phi < -bs * 0.98) phi = -bs * 0.98;
        coreH(phi, true);
    }

    // ---------- Loudspeaker load (referred to the primary, full winding) ----------
    void loadCompanion(double& G, double& Ih) {
        if (quality < 2) { G = 1 / Raa; Ih = 0; return; }
        const double h = dt;
        double Yt = 1 / Rm + h / Lm + Cm / h;
        double c = -L0 * li / h + (-liLm + Cm * lvm / h) / Yt;
        double Zeq = R0 + L0 / h + 1 / Yt;
        G = 1 / Zeq; Ih = -c / Zeq;
        // Ifull = (V − c)/Zeq, V = 2v → Ifull = 2v·G + Ih
    }
    void loadUpdate(double V) {
        if (quality < 2) return;
        const double h = dt;
        double G, Ih; loadCompanion(G, Ih);
        double i = V * G + Ih;
        double Yt = 1 / Rm + h / Lm + Cm / h;
        double vm = (i - liLm + Cm * lvm / h) / Yt;
        liLm += h * vm / Lm; lvm = vm; li = i;
    }

    void drift() {
        // Ultra: the tubes slowly drift apart (warming up), ±1 %
        driftPh += 0.05;
        kgTube[0] = kgBase[0] * (1 + 0.008 * std::sin(driftPh));
        kgTube[1] = kgBase[1] * (1 - 0.006 * std::sin(driftPh * 1.31 + 1));
    }

    // ---------- Setup ----------
    void configure() {
        int os = quality == 0 ? 1 : quality == 1 ? (fs >= 75000 ? 1 : 2) : (fs >= 150000 ? 1 : fs >= 75000 ? 2 : 4);
        ovs.init(os);
        dt = 1 / (fs * os);
        itDrv = quality == 2 ? 6 : 3; itOut = quality == 2 ? 8 : 4;
        dcR = 1 - 2 * PI * 4 / fs;
        // circuit values
        drvRa = 47e3; Rkd = 1500; Ckd = 25e-6; Vreg = 310; Vbd = 260;
        Cc = 0.1e-6; Rg = 100e3;
        beta = model == M147 ? 0.061 : 0;
        Vscreen = 310; Rko = 150; Cko = 50e-6;
        Raa = 4300;
        Ireg = 0.012;   // bleed current of the OC3 regulator plus the driver, fed from the same supply
        applySupply();
        C1 = 30e-6; C2 = 40e-6; Lch = 4; RL = 60;
        ltpImbalance = 0.94;
        // tubes: fixed tolerance mismatch and age (reduced emission)
        applyEmission();
        // transformer: iron core (Amberwave Iron core model), primary inductance ~25 H
        bs = 1.5; hystW = quality >= 1 ? 0.45 : 0; double hyst = 0.07 * (quality == 2 ? 1.2 : 1);
        double ws = 0; for (int j = 1; j <= PLAYS; j++) { playR[j - 1] = hyst * j / PLAYS; playW[j - 1] = j; ws += j; }
        for (int j = 0; j < PLAYS; j++) playW[j] /= ws;
        fluxK = 0.8 * bs * 2 * PI * 35 / 700;     // 700 V peak at 35 Hz → 80 % of Bs
        imScale = 1 / (fluxK * 25.0);             // Lp = 25 H
        eddyK = quality == 2 ? 0.02 : 0;
        // load (Ultra): voice coil + resonance of the 15" woofer in the drum, referred to the primary side
        // total load (crossover + 15" + horn driver): above the crossover the horn driver takes over, and the voice-coil inductance
        // of the 15" sits behind the crossover inductor – what remains is a slight rise at the very top and the drum resonance
        R0 = Raa; L0 = R0 / (2 * PI * 30000);
        double f0 = 48, Q = 1.6, w0 = 2 * PI * f0; Rm = 0.6 * Raa; Lm = Rm / (Q * w0); Cm = Q / (Rm * w0);
        // let the circuit settle to its operating point
        settle();
        calibrate();
    }

    /** Rectifier type and series resistance (sag) of the power supply; safe to call while running. */
    void applySupply() {
        bool tubeRect = model == M122_EARLY;
        V0 = tubeRect ? 455 : 438;
        Rs = (tubeRect ? 190 : 55) * (0.5 + 1.5 * sag);
        RsEff = Rs + 60;   // HQ: choke resistance folded in
    }
    /** Tube emission (age) and the fixed pair mismatch; safe to call while running. */
    void applyEmission() {
        double emis = 1 + 0.7 * age;
        kgBase[0] = 1.00 * emis; kgBase[1] = (quality >= 1 ? 1.045 : 1.0) * emis;
        kgTube[0] = kgBase[0]; kgTube[1] = kgBase[1];
    }
    void settle() {
        Vb = V0 - 0.2 * (V0 - 415); V1 = Vb; iL = 0.19; Vko = 25; Vkd = 4; Vbd = quality == 2 ? 258 : 260;
        drvVp[0] = drvVp[1] = 140; ccQ[0] = ccQ[1] = 140; vPrev = 0; phi = 0; eddy = 0;
        std::memset(play, 0, sizeof play); li = liLm = lvm = 0;
        // operating point: run a few hundred milliseconds without signal (capacitors, power supply, bias)
        if (quality == 0) return;
        int steps = (int) (0.6 / dt);
        for (int i = 0; i < steps; i++) tick(0);
        // driver output impedance (HQ): Ra ∥ rp at the operating point
        double ip, gp, gm; triode(T_12AU7, -Vkd, drvVp[0] - Vkd, ip, gp, gm);
        drvRout = 1 / (1 / drvRa + std::max(gp, 1e-7));
        dcX = dcY = 0;
    }

    /** Measure the small-signal gain (1 kHz, very quiet) so that "Drive" changes only the saturation, not the loudness */
    void calibrate() {
        if (quality == 0) {
            // Standard: normalise the slope of the transfer curve at 0 to 1
            double b = 0.35, d = 1e-4;
            double g = 0.5 * ((std::tanh(d + b) - std::tanh(b)) - 0.96 * (std::tanh(-d + b) - std::tanh(b))) / d;
            stdGain = 361.0 / 700.0; stdNorm = 2.0 / (g * stdGain); stdEnv = 0;
            return;
        }
        // save the state
        auto save = snapshot();
        Oversampler o2 = ovs;
        double amp = 0.01, sIn = 0, sOut = 0;
        int n = (int) (fs * 0.06);
        double up[8], dn[8];
        double x1 = 0, y1 = 0;
        for (int i = 0; i < n; i++) {
            double xin = amp * std::sin(2 * PI * 1000 * i / fs);
            o2.upsample(xin, up);
            for (int k = 0; k < o2.os; k++) dn[k] = tick(up[k]);
            double y = o2.downsample(dn);
            double w = y - x1 + dcR * y1; x1 = y; y1 = w;
            if (i > n / 2) { sIn += xin * xin; sOut += w * w; }
        }
        double g = std::sqrt(sOut / std::max(sIn, 1e-30));
        restore(save);
        outNorm = 2.0 / std::max(g, 1e-6);
    }

    struct Snap { double v[40]; double pl[8]; };
    Snap snapshot() const {
        Snap s{}; double* p = s.v;
        *p++ = Vb; *p++ = V1; *p++ = iL; *p++ = Vko; *p++ = Vkd; *p++ = Vbd; *p++ = drvVp[0]; *p++ = drvVp[1];
        *p++ = ccQ[0]; *p++ = ccQ[1]; *p++ = vPrev; *p++ = phi; *p++ = eddy; *p++ = li; *p++ = liLm; *p++ = lvm;
        std::memcpy(s.pl, play, sizeof play);
        return s;
    }
    void restore(const Snap& s) {
        const double* p = s.v;
        Vb = *p++; V1 = *p++; iL = *p++; Vko = *p++; Vkd = *p++; Vbd = *p++; drvVp[0] = *p++; drvVp[1] = *p++;
        ccQ[0] = *p++; ccQ[1] = *p++; vPrev = *p++; phi = *p++; eddy = *p++; li = *p++; liLm = *p++; lvm = *p++;
        std::memcpy(play, s.pl, sizeof play);
    }

    double fs;   // not const: instances are copy-assigned when a model/quality change is prepared off the audio thread
    int model = M122, quality = 1;
    double drive = 0.35, kin = 1.2 * std::pow(30.0, 0.35), sag = 0.5, age = 0.0;
    Oversampler ovs;
    double dt = 1e-5;
    int itDrv = 3, itOut = 4;
    double dcX = 0, dcY = 0, dcR = 0.9995, outNorm = 1;
    // Standard quality
    double stdGain = 0.5, stdNorm = 1, stdEnv = 0;
    // circuit values
    double drvRa = 47e3, Rkd = 1500, Ckd = 25e-6, Vreg = 310, Vbd = 260, drvRout = 8000;
    double Cc = 0.047e-6, Rg = 100e3, Vscreen = 310, Rko = 150, Cko = 50e-6, Raa = 4300, Ireg = 0.012;
    double beta = 0;
    double V0 = 450, Rs = 55, RsEff = 115, C1 = 30e-6, C2 = 40e-6, Lch = 4, RL = 60, ltpImbalance = 0.94;
    double kgBase[2] = {1, 1}, kgTube[2] = {1, 1};
    // state variables
    double Vb = 415, V1 = 420, iL = 0.19, Vko = 25, Vkd = 4;
    double drvVp[2] = {140, 140}, ccQ[2] = {140, 140}, vPrev = 0, lastIp[2] = {0, 0};
    // transformer core
    static constexpr int PLAYS = 6;
    double bs = 1.5, hystW = 0.45, playR[PLAYS] = {0}, playW[PLAYS] = {0}, play[PLAYS] = {0};
    double phi = 0, fluxK = 0.38, imScale = 0.1, eddy = 0, eddyK = 0;
    // loudspeaker load
    double R0 = 3600, L0 = 0.26, Rm = 11000, Lm = 0.016, Cm = 0.6e-6, li = 0, liLm = 0, lvm = 0;
    // tube drift
    double driftPh = 0; int driftCount = 0;
};

} // namespace amp
} // namespace leslie
