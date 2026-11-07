#pragma once
// Wheelhouse – the Hammond AO-28 preamplifier (B-3 / C-3 / RT-3) as a circuit, component by component.
//
// Topology of the original (schematic AO-28, parts list values):
//   manuals → matching transformers → V1 6AU6 (non-vibrato channel) / V2 6AU6 (vibrato channel), plate 470 kΩ
//   (R8/R19), screens from +280 V through 3.9 MΩ / 2.7 MΩ (R10/R15, about 60 V), grid leak 10 MΩ; both 6AU6 and both
//   halves of the 12AX7 share one 1.2 kΩ cathode resistor (R17) with 30 µF (C9): about 1.7 V.
//   V2 → C12 (4.7 nF) → V3B 12BH7 as cathode follower → vibrato line and scanner (ScannerVibrato.h) → R30 1.8 MΩ
//   V1 → C5 (10 nF) → R27 1.8 MΩ                                                                     → mix node
//   (R35 3.3 MΩ; the 24/30 pF across the 1.8 MΩ compensate the 12AX7's Miller capacitance: flat) → V4A 12AX7, plate
//   330 kΩ (R29) → C16 (33 nF) → swell pedal → V4B 12AX7, plate 330 kΩ (R36) → C23 (33 nF) → R48 470 kΩ → V3A 12BH7
//   single-ended, cathode 1.2 kΩ (R42) with 30 µF (C26): 12 V, 10 mA → output transformer (premagnetised by those
//   10 mA) → Rotary.
//
// Two parts, because the scanner sits in the middle:
//   OrganFront    per sample at the audio rate, used inside the organ loop: the two input pentodes, the cathode
//                 follower in front of the line, the mix node and V4A. This is where a B-3 starts to growl with a
//                 full registration - before the swell pedal, so backing the pedal off does not clean it up.
//   OrganPreamp   per block, oversampled: V4B and the single-ended output stage with the transformer. Owns the
//                 OrganFront (same quality level, same gain knob, copied together when the host swaps models).
//
// Measured / documented: all resistor and capacitor values above, the operating points (plates 130 V / 145 V /
// 285 V, cathodes 1.7 V / 12 V), the topology. Assumed: the matching-transformer level (12 mV peak at the 6AU6 grid
// with a full registration, the Gain knob moves it ±11 dB), the cathode follower's bias tap, the output transformer
// (30 H, about 15 kΩ reflected load, core premagnetised to about half its saturation flux), the swell network's
// fixed attenuation (folded into the V4B drive level).
//
// Quality: Eco = linear front end + static curves (very cheap); HQ = the circuit, Newton per sample, 2x oversampled
// output stage; Ultra = additionally screen-supply sag of the pentodes, 12AX7 Miller input, sagging plate supply,
// eddy currents in the core, 4x oversampling. Tube models per Koren (dsp/glow.c of Amberwave), core with play
// operators (dsp/iron.c), rebuilt here for this circuit.

#include "RotaryAmp.h"

namespace rotary {
namespace amp {

static const Triode T_12AX7{100, 1.4, 1060, 600, 300, 2000, -0.2, 0.06};
static const Triode T_12BH7{16.5, 1.35, 620, 78, 300, 1500, -0.7, 0.14};
// 6AU6 sharp-cutoff pentode: mu(g1->g2) 36; kg1 set for 0.32 mA at 130 V / 60 V / -1.7 V (the AO-28 operating point,
// right at the knee of the softplus - the stage is starved, which is where its second harmonic comes from); kg2 only
// scales the screen share (about 18 % of the cathode current).
static const Pentode P_6AU6{36, 1.4, 2300, 12000, 100, 30, 2000, -0.3, 0.08};

/** AO-28 input section: V1/V2 6AU6, V3B cathode follower, mix node, V4A. One instance per organ. */
class OrganFront {
public:
    void prepare(double sampleRate, int q, double gainKnob) {
        fs = sampleRate;
        dt = 1 / fs;
        quality = q;
        setGain(gainKnob);
        build();
    }
    void setGain(double g) { kIn = 0.03 * std::pow(12.0, std::max(0.0, std::min(1.0, g)) - 0.5); }
    /** Input stage ch (0 = V1, non-vibrato; 1 = V2, vibrato): x in organ units, returns the volts coupled out of the
     * plate into the next stage (the mix branch for V1, the cathode follower's grid for V2). */
    double stage(int ch, double x) {
        if (quality == 0) { // linear: the coupling capacitor's high-pass only, gain folded into norm
            double& hp = ecoHp[ch];
            const double y = x - ecoX[ch] + ecoR * hp;
            ecoX[ch] = x;
            hp = y;
            return y;
        }
        Pent& p = pent[ch];
        const double u = x * kIn;
        const double Vg1k = u - VkP;
        const PentGrid g = pentGrid(P_6AU6, Vg1k, p.Vg2 - VkP, 1.0);
        // plate node: (V0 - Vp)/Rp = Ip(Vp) + coupling current (Vp - q)/Req
        double Vp = p.Vp;
        const double Req = p.Rn + dt / p.Cc;
        for (int it = 0; it < 3; it++) {
            const double Vpk = std::max(Vp - VkP, 0.5);
            const double ip = g.a * pentAtan(P_6AU6, Vpk);
            const double F = (V0 - Vp) / Rp6 - ip - (Vp - p.q) / Req;
            const double dF = -1 / Rp6 - g.a * pentAtanD(P_6AU6, Vpk) - 1 / Req;
            const double d = F / dF;
            Vp -= d;
            if (Vp < VkP + 1) Vp = VkP + 1;
            if (Vp > V0) Vp = V0;
            if (std::fabs(d) < NEWTON_TOL_V) break;
        }
        p.Vp = Vp;
        const double ip = g.a * pentAtan(P_6AU6, std::max(Vp - VkP, 0.5));
        const double ig2 = ip * p.screenShare;
        const double i = (Vp - p.q) / Req; // through the coupling capacitor into the next stage
        p.q += dt * i / p.Cc;
        kSumP += ip + ig2;
        p.Vg2 += dt * ((V0 - p.Vg2) / p.Rs - ig2) / Cs; // the screen supply (3.9 MΩ, 0.33 µF) sags under drive: 1.3 s
        return i * p.Rn;
    }
    /** V3B cathode follower: grid volts in (from V2), volts at the line's source out. */
    double lineDriver(double vg) {
        if (quality == 0) return vg;
        // cathode: Ik = Ip(Vgk, Vpk) with Vk = Ik * Rk. The grid leak returns to a tap Rbias below the cathode (DC
        // bias -Ik Rbias, about -9 V), the signal arrives ground-referenced through C12: Vgk = vg - (Vk - Vk0) - r Vk0
        const double r = RbiasCf / RkCf;
        double Vk = cfVk;
        for (int it = 0; it < 4; it++) {
            double ip, gp, gm;
            triode(T_12BH7, vg - (Vk - cfVkRest) - r * cfVkRest, std::max(V0 - Vk, 1.0), ip, gp, gm);
            const double F = ip - Vk / RkCf, dF = -gm - gp - 1 / RkCf;
            const double d = F / dF;
            Vk -= d;
            if (Vk < 0) Vk = 0;
            if (std::fabs(d) < NEWTON_TOL_V) break;
        }
        cfVk = Vk;
        // C13 (1 µF) couples the line to the cathode, which sits at +124 V: with the line's 17 kΩ a 9 Hz high-pass
        const double y = Vk - cfX + cfR * cfY;
        cfX = Vk;
        cfY = y;
        return y;
    }
    /** Mix node (R27 / R30 1.8 MΩ into R35 3.3 MΩ) and V4A: volts of the two branches in, organ units out (unity for
     * small signals, so bypass and circuit have the same loudness). */
    double mix(double vDry, double vScan) {
        if (quality == 0) return vDry + vScan;
        const double src = (vDry + vScan) * Rnode / Rbranch; // open-circuit node voltage
        double Vg = src, ig = 0, dig = 0;
        if (src - Vk > T_12AX7.vth - 10 * T_12AX7.w) { // grid current against the node's 1.1 MΩ: soft positive clip
            for (int it = 0; it < 5; it++) {
                gridCurrent(T_12AX7.rgi, T_12AX7.vth, T_12AX7.w, Vg - Vk, ig, dig);
                const double F = (src - Vg) / Rnode - ig, dF = -1 / Rnode - dig;
                const double d = F / dF;
                Vg -= d;
                if (std::fabs(d) < NEWTON_TOL_V) break;
            }
            gridCurrent(T_12AX7.rgi, T_12AX7.vth, T_12AX7.w, Vg - Vk, ig, dig);
        }
        if (quality == 2) { // Miller capacitance against the node resistance
            millerY += millerK * (Vg - millerY);
            Vg = millerY;
        }
        double Vp = v4Vp, ip = 0, gp = 0, gm = 0;
        const double Req = RswellIn + dt / C16;
        for (int it = 0; it < 4; it++) {
            triode(T_12AX7, Vg - Vk, std::max(Vp - Vk, 0.5), ip, gp, gm);
            const double F = (V0 - Vp) / Rp4 - ip - (Vp - v4q) / Req, dF = -1 / Rp4 - gp - 1 / Req;
            const double d = F / dF;
            Vp -= d;
            if (Vp < Vk + 0.5) Vp = Vk + 0.5;
            if (Vp > V0) Vp = V0;
            if (std::fabs(d) < NEWTON_TOL_V) break;
        }
        v4Vp = Vp;
        const double i = (Vp - v4q) / Req;
        v4q += dt * i / C16;
        // the shared 1.2 kΩ / 30 µF cathode: the pentodes' node carries their own current plus the triodes' quiescent
        // share, V4A/V4B see everything (so a hard-driven V4A shifts V4B's bias). The sub-audio loop V1 → V4A → cathode
        // → V1 is left out: the model's loop gain would exceed the real circuit's, which is stable.
        VkP += dt * (kSumP + I_V4A + I_V4B - VkP / Rk) / Ck;
        Vk += dt * (kSumP + ip + ig + I_V4B - Vk / Rk) / Ck;
        kSumP = 0;
        return i * RswellIn * norm;
    }
    /** Shared cathode voltage (V4B reads it). */
    double cathode() const { return quality == 0 ? 1.7 : Vk; }
    struct Op {
        double plate[2], screen[2], cathode, followerCathode, v4aPlate;
    };
    /** Operating point (tests). */
    Op op() const { return {{pent[0].Vp, pent[1].Vp}, {pent[0].Vg2, pent[1].Vg2}, Vk, cfVk, v4Vp}; }
    int qualityLevel() const { return quality; }

private:
    struct Pent {
        double Vp = 130, Vg2 = 60, q = 0, Rn = 1.8e6, Cc = 0.01e-6, Rs = 3.9e6, screenShare = 0.18;
    };
    void build() {
        // screen share set so that both screens sit at the documented 60 V from their different feed resistors
        pent[0] = Pent{130, 60, 0, Rbranch, 0.01e-6, 3.9e6, 0.18}; // V1: C5 10 nF into the mix branch
        // V2: C12 4.7 nF into the follower's 10 MΩ grid leak, bootstrapped by the cathode (the leak returns to the
        // tap just below it): the plate sees next to no load, the grid the ground-referenced plate swing
        pent[1] = Pent{130, 60, 0, 10e6 * RkCf / RbiasCf, 0.0047e-6, 2.7e6, 0.26};
        ecoR = 1 - 2 * PI * 9 / fs;
        ecoX[0] = ecoX[1] = ecoHp[0] = ecoHp[1] = 0;
        millerK = 1 - std::exp(-2 * PI * 9000 * dt);
        cfR = 1 - 2 * PI * 9.4 / fs;
        cfX = cfVk;
        cfY = 0;
        Vk = VkP = 1.7;
        cfVk = 110;
        v4Vp = 145;
        v4q = 0;
        kSumP = 0;
        millerY = 0;
        norm = 1;
        if (quality == 0) return;
        settle();
        calibrate();
    }
    /** Operating point: the slow nodes (screens 1.3 s, cathode 36 ms, coupling capacitors) with a coarse step. */
    void settle() {
        const double dtSave = dt;
        dt = 0.5e-3;
        for (int i = 0; i < 8000; i++) {
            stage(0, 0);
            const double v = stage(1, 0);
            lineDriver(v);
            mix(0, 0);
        }
        for (int k = 0; k < 6; k++) { // the follower's rest point enters its own bias: iterate to a fixed point
            cfVkRest = cfVk;
            for (int i = 0; i < 200; i++) lineDriver(0);
        }
        cfVkRest = cfVk;
        cfX = cfVk;
        cfY = 0;
        dt = dtSave;
        for (int i = 0; i < (int) (0.3 * fs); i++) {
            stage(0, 0);
            lineDriver(stage(1, 0));
            mix(0, 0);
        }
    }
    /** Small-signal gain of the dry path (1 kHz, 1 mV) → norm so that organ units come out as they went in. */
    void calibrate() {
        const OrganFront save = *this;
        const int n = (int) (0.3 * fs);
        double sIn = 0, sOut = 0;
        for (int i = 0; i < n; i++) {
            const double x = 0.01 * std::sin(2 * PI * 1000 * i / fs);
            const double y = mix(stage(0, x), lineDriver(stage(1, 0)));
            if (i > 2 * n / 3) {
                sIn += x * x;
                sOut += y * y;
            }
        }
        const double g = std::sqrt(sOut / std::max(sIn, 1e-30));
        const double nrm = 1 / std::max(g, 1e-9);
        *this = save;
        norm = nrm;
    }

    double fs = 48000, dt = 1.0 / 48000;
    int quality = 1;
    double kIn = 0.04, norm = 1;
    // circuit
    static constexpr double V0 = 280, Rp6 = 470e3, Cs = 0.33e-6, Rk = 1200, Ck = 30e-6, I_V4A = 0.3e-3, I_V4B = 0.3e-3;
    static constexpr double Rbranch = 1.8e6 + 1.165e6, Rnode = 1.165e6; // 1.8 MΩ into (3.3 MΩ || other 1.8 MΩ)
    static constexpr double Rp4 = 330e3, C16 = 0.033e-6, RswellIn = 1e6;
    static constexpr double RkCf = 22e3, RbiasCf = 1500;
    Pent pent[2];
    double Vk = 1.7, VkP = 1.7, kSumP = 0;
    double cfVk = 110, cfVkRest = 110, cfX = 110, cfY = 0, cfR = 0.999;
    double v4Vp = 145, v4q = 0, millerK = 0.5, millerY = 0;
    // eco
    double ecoR = 0.999, ecoX[2] = {0, 0}, ecoHp[2] = {0, 0};
};

/** AO-28 output section: V4B 12AX7 and the single-ended 12BH7 into the output transformer. Owns the front end. */
class OrganPreamp {
public:
    explicit OrganPreamp(double sampleRate) : fs(sampleRate) { configure(); }

    /** Quality level 0..2 (Eco / HQ / Ultra), see file header. Rebuilds the circuit. */
    void setQuality(int q) {
        q = std::max(0, std::min(2, q));
        if (q != quality) {
            quality = q;
            configure();
        }
    }
    /** Gain 0..1: the matching-transformer level into the 6AU6 grids (±11 dB around 12 mV peak at full
     * registration); affects how early the front end growls, not the loudness. */
    void setGain(double g) {
        gain = std::max(0.0, std::min(1.0, g));
        frontEnd.setGain(gain);
    }
    /** Enable/bypass the preamp; when disabled, process() leaves the signal untouched and the organ loop skips the
     * front end. */
    void setEnabled(bool on) { enabled = on; }
    int getQuality() const { return quality; }
    bool isEnabled() const { return enabled; }
    /** Latency in samples introduced by the oversampling filters (0 without oversampling). */
    int latency() const { return ovs.delay; }
    OrganFront& front() { return frontEnd; }

    /** Mono, in place; x is roughly ±0.3 with full organ and open swell pedal (organ units after V4A and the swell) */
    void process(float* x, int n) {
        if (!enabled) return;
        if (quality == 0) {
            processEco(x, n);
            return;
        }
        Vk4 = frontEnd.cathode();
        double up[8], dn[8];
        const int OS = ovs.os;
        for (int i = 0; i < n; i++) {
            ovs.upsample((double) x[i] * kin, up);
            for (int k = 0; k < OS; k++) dn[k] = tick(up[k]);
            double y = ovs.downsample(dn);
            double w = y - dcX + dcR * dcY;
            dcX = y;
            dcY = w;
            x[i] = (float) (w * outNorm / kin);
        }
    }

private:
    // ---------- Eco ----------
    void processEco(float* x, int n) {
        for (int i = 0; i < n; i++) {
            // static stand-in for V4B and the single-ended 12BH7 (about the circuit's harmonics at full registration)
            double u = (double) x[i] * kin;
            // 12AX7: soft limiting, earlier on the positive side (grid current) than on the negative side (cutoff)
            double a = u * 3.3;
            double s1 = a > 0 ? std::tanh(a * 1.25) / 1.25 : std::tanh(a * 0.8) / 0.8;
            // single-ended 12BH7: a little second harmonic, soft on one side
            double s2 = s1 > 0 ? std::tanh(s1 * 1.1) / 1.1 : std::tanh(s1 * 1.4) / 1.4;
            double w = s2 - dcX + dcR * dcY;
            dcX = s2;
            dcY = w;
            x[i] = (float) (w / (3.3 * kin));
        }
    }

    // ---------- Circuit (one step at os·fs; u = volts at the V4B grid) ----------
    double tick(double u) {
        const double h = dt;
        if (quality == 2) { // Miller capacitance of V4B against the swell network's source resistance
            millerY += millerK * (u - millerY);
            u = millerY;
        }
        // --- V4B 12AX7: grid current against the swell network, plate 330 kΩ, shared cathode ---
        double Vgk = u - Vk4;
        for (int it = 0; it < 2; it++) {
            double ig0, dig0;
            gridCurrent(T_12AX7.rgi, T_12AX7.vth, T_12AX7.w, Vgk, ig0, dig0);
            double f = Vgk - (u - Vk4) + Rsrc * ig0;
            Vgk -= f / (1 + Rsrc * dig0);
        }
        double Vp = vp1, ip = 0, gp = 0, gm = 0;
        const double Req1 = Rg + h / Cc; // C23 into R48, the follower-free grid leak of V3A
        for (int it = 0; it < itA; it++) {
            triode(T_12AX7, Vgk, Vp - Vk4, ip, gp, gm);
            double f = (Vb - Vp) / Ra1 - ip - (Vp - ccQ) / Req1, df = -1 / Ra1 - gp - 1 / Req1;
            double d = f / df;
            Vp -= d;
            if (Vp < Vk4 + 0.5)
                Vp = Vk4 + 0.5;
            else if (Vp > Vb)
                Vp = Vb;
            if (std::fabs(d) < NEWTON_TOL_V) break;
        }
        vp1 = Vp;
        const double ip1 = ip;
        // --- coupling C23 → R48: V3A grid, with grid current when the grid goes positive against its cathode ---
        double iC = (Vp - ccQ) / Req1;
        double Vg3 = iC * Rg, ig3 = 0, dig3 = 0;
        if (Vg3 - Vk2 > T_12BH7.vth - 12 * T_12BH7.w) {
            const double rout1 = 1 / (1 / Ra1 + std::max(gp, 1e-8));
            const double Req = rout1 + h / Cc;
            const double s = Vp - ccQ;
            for (int it = 0; it < 6; it++) {
                gridCurrent(T_12BH7.rgi, T_12BH7.vth, T_12BH7.w, Vg3 - Vk2, ig3, dig3);
                double f = (s - Vg3) / Req - Vg3 / Rg - ig3, df = -1 / Req - 1 / Rg - dig3;
                double d = f / df;
                Vg3 -= d;
                if (std::fabs(d) < NEWTON_TOL_V) break;
            }
            gridCurrent(T_12BH7.rgi, T_12BH7.vth, T_12BH7.w, Vg3 - Vk2, ig3, dig3);
            iC = (s - Vg3) / Req;
        }
        ccQ += h * iC / Cc;
        // --- V3A 12BH7 single-ended into the transformer: v = primary voltage (positive = plate pulled down) ---
        const double Im = magnetizing();
        double v = vPrev, ia = 0;
        for (int it = 0; it < itB; it++) {
            double gpa, gma;
            triode(T_12BH7, Vg3 - Vk2, std::max(Vb - v - Vk2, 0.5), ia, gpa, gma);
            // the AC part of the plate current flows into the reflected load and the magnetising branch
            double F = (ia - Iq) - Im - v / Raa;
            double dF = -gpa - 1 / Raa;
            double d = F / dF;
            v -= d;
            double vmax = Vb - Vk2 - 2;
            if (v > vmax)
                v = vmax;
            else if (v < -2 * Vb)
                v = -2 * Vb;
            if (std::fabs(d) < NEWTON_TOL_OUT_V) break;
        }
        vPrev = v;
        coreUpdate(v);
        // 12BH7 cathode: 1.2 kΩ with 30 µF (Ultra: the same, it is what it is - 36 ms)
        Vk2 += h * (ia + ig3 - Vk2 / Rk2) / Ck2;
        // plate supply (Ultra: the RC filtering sags under load)
        if (quality == 2) Vb += h * ((V0 - Vb) / Rsup - (ia + ip1 + IqOther)) / Csup;
        return v;
    }

    double coreH(double p, bool commit) {
        double bh = 0;
        for (int j = 0; j < PLAYS; j++) {
            double r = playR[j], y = play[j];
            if (p - y > r)
                y = p - r;
            else if (y - p > r)
                y = p + r;
            if (commit) play[j] = y;
            bh += playW[j] * y;
        }
        double b = (1 - hystW) * p + hystW * bh;
        double q = std::min(0.96, b * b / (bs * bs));
        return b / (1 - q);
    }
    /** Magnetising current re the quiescent point: the DC premagnetisation sits in phiDc, the signal in phi. */
    double magnetizing() { return (coreH(phiDc + phi, false) - iDcCore + eddy) * imScale; }
    void coreUpdate(double vPrim) {
        double dphi = dt * vPrim * fluxK;
        if (quality == 2) eddy = eddyK * dphi / dt * 1e-3;
        // winding resistance: the signal flux has no DC path of its own (R/L ≈ 0.3 s)
        phi += dphi - phi * dt / 0.3;
        const double lim = bs * 0.98;
        if (phiDc + phi > lim)
            phi = lim - phiDc;
        else if (phiDc + phi < -lim)
            phi = -lim - phiDc;
        coreH(phiDc + phi, true);
    }

    void configure() {
        int os = quality == 0 ? 1 : quality == 1 ? (fs >= 75000 ? 1 : 2) : (fs >= 150000 ? 1 : fs >= 75000 ? 2 : 4);
        ovs.init(os);
        dt = 1 / (fs * os);
        dcR = 1 - 2 * PI * 4 / fs;
        itA = quality == 2 ? 6 : 3;
        itB = quality == 2 ? 8 : 4;
        // circuit values (AO-28)
        V0 = 285;
        Vb = 285;
        Ra1 = 330e3;   // R36
        Rsrc = 1e6;    // the swell network as seen from the V4B grid (R21)
        Cc = 0.033e-6; // C23
        Rg = 470e3;    // R48
        Rk2 = 1200;    // R42
        Ck2 = 30e-6;   // C26
        Raa = 15e3;    // reflected load of the output transformer (assumed)
        Iq = 0.010;    // 12 V across 1.2 kΩ
        IqOther = 0.0015;
        Rsup = 2200;
        Csup = 30e-6;
        double fM = 12000;
        millerK = 1 - std::exp(-2 * PI * fM * dt);
        // transformer: primary ~30 H; the quiescent 10 mA premagnetise the (gapped) core to about half saturation
        bs = 1.3;
        hystW = 0.4;
        double hyst = 0.06;
        double ws = 0;
        for (int j = 1; j <= PLAYS; j++) {
            playR[j - 1] = hyst * j / PLAYS;
            playW[j - 1] = j;
            ws += j;
        }
        for (int j = 0; j < PLAYS; j++) playW[j] /= ws;
        fluxK = 0.8 * bs * 2 * PI * 30 / 500;
        imScale = 1 / (fluxK * 30.0);
        phiDc = 0.5 * bs;
        iDcCore = coreH(phiDc, false); // the premagnetisation in core units; subtracted so only the signal counts
        eddyK = quality == 2 ? 0.02 : 0;
        frontEnd.prepare(fs, quality, gain);
        settle();
        calibrate();
    }

    void reset() {
        Vb = V0;
        vp1 = 145;
        Vk4 = 1.7;
        Vk2 = 12;
        ccQ = 0;
        vPrev = 0;
        phi = 0;
        eddy = 0;
        millerY = 0;
        std::memset(play, 0, sizeof play);
        coreH(phiDc, true);
    }
    void settle() {
        reset();
        dcX = dcY = 0;
        if (quality == 0) return;
        Vk4 = frontEnd.cathode();
        int steps = (int) (0.5 / dt);
        for (int i = 0; i < steps; i++) tick(0);
    }
    /** Normalise the small-signal gain to 1: played cleanly, the organ gets louder or quieter only via the swell pedal
     */
    void calibrate() {
        if (quality == 0) return;
        struct S {
            double a[16];
            double pl[PLAYS];
        } sv;
        double* p = sv.a;
        *p++ = Vb;
        *p++ = vp1;
        *p++ = Vk2;
        *p++ = ccQ;
        *p++ = vPrev;
        *p++ = phi;
        *p++ = eddy;
        *p++ = millerY;
        std::memcpy(sv.pl, play, sizeof play);
        Oversampler o2 = ovs;
        double amp = 0.02, sIn = 0, sOut = 0, x1 = 0, y1 = 0, up[8], dn[8];
        int n = (int) (fs * 0.05);
        for (int i = 0; i < n; i++) {
            double xin = amp * std::sin(2 * PI * 1000 * i / fs);
            o2.upsample(xin * kin, up);
            for (int k = 0; k < o2.os; k++) dn[k] = tick(up[k]);
            double y = o2.downsample(dn);
            double w = y - x1 + dcR * y1;
            x1 = y;
            y1 = w;
            if (i > n / 2) {
                sIn += xin * xin;
                sOut += w * w;
            }
        }
        double g = std::sqrt(sOut / std::max(sIn, 1e-30)) / kin;
        outNorm = 1.0 / std::max(g, 1e-6);
        p = sv.a;
        Vb = *p++;
        vp1 = *p++;
        Vk2 = *p++;
        ccQ = *p++;
        vPrev = *p++;
        phi = *p++;
        eddy = *p++;
        millerY = *p++;
        std::memcpy(play, sv.pl, sizeof play);
    }

    double fs; // not const: copy-assigned when a quality change is prepared off the audio thread
    int quality = 1;
    bool enabled = true;
    double gain = 0.5;
    // volts at the V4B grid per organ unit: the swell network's fixed loss folded in (full registration, pedal
    // fully open: about 1 V peak - the 12AX7 is at 60 % of its bias, the output stage well inside class A)
    double kin = 0.55;
    OrganFront frontEnd;
    Oversampler ovs;
    double dt = 1e-5, dcX = 0, dcY = 0, dcR = 0.9995, outNorm = 1;
    int itA = 3, itB = 4;
    double V0 = 285, Vb = 285, Ra1 = 330e3, Rsrc = 1e6, Cc = 0.033e-6, Rg = 470e3;
    double Rk2 = 1200, Ck2 = 30e-6, Raa = 15e3, Rsup = 2200, Csup = 30e-6, Iq = 0.01, IqOther = 0.0015, millerK = 0.5;
    double vp1 = 145, Vk4 = 1.7, Vk2 = 12, ccQ = 0, vPrev = 0, millerY = 0;
    static constexpr int PLAYS = 6;
    double bs = 1.3, hystW = 0.4, playR[PLAYS] = {0}, playW[PLAYS] = {0}, play[PLAYS] = {0};
    double phi = 0, phiDc = 0.65, iDcCore = 0, fluxK = 0.4, imScale = 0.1, eddy = 0, eddyK = 0;
};

} // namespace amp
} // namespace rotary
