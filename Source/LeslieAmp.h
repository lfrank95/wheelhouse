// Wheelhouse – Leslie-Endstufe (122 / 122 früh / 147) als Schaltung.
//
// Aufbau nach dem 122: symmetrischer Eingang → 12AU7 als Gegentakt-Treiber (beim 147: Phasendreher als Long-Tailed-Pair)
// → Koppelkondensatoren mit Gitterableitwiderstand → 2 × 6550 im Gegentakt, Klasse AB, gemeinsamer Kathodenwiderstand
// 150 Ω (ca. 25 V Kathodenspannung) → Ausgangsübertrager → Weiche/Lautsprecher. Schirmgitter an der OC3-stabilisierten 310 V,
// Anoden an ca. 415 V hinter Gleichrichter und Drossel (122 früh: Gleichrichterröhre 5U4, sonst Dioden).
//
// Röhren nach Koren (Triode und Pentode), Gitterstrom, Kathoden- und Netzteil-Dynamik; Kern des Übertragers mit
// H(B) = B / (1 − (B/Bs)²) und Hysterese über Play-Operatoren. Die Rechenwege (Koren je Takt mit Newton, Gitterstrom,
// Kern mit Play-Operatoren, Kaiser-Oversampling) folgen Amberwave Glow HQ/Ultra und Iron HQ/Ultra (dsp/glow.c, dsp/iron.c,
// AGPL-3.0, The Amberwave contributors) und sind hier für eine Gegentakt-Endstufe neu aufgebaut.
//
// Qualität:
//   0 Standard – statische Gegentakt-Kennlinie mit Bias-Verschiebung und Sag (sehr sparsam)
//   1 HQ       – Schaltung je Takt (2× Oversampling): Treiber, Gitterstrom/Blocking, Kathoden-Bias, Sag, Übertrager mit Hysterese
//   2 Ultra    – 4×, Drossel-Netzteil (Nachschwingen), Treiber-Versorgung über 10 kΩ, Schirmstrom, Lautsprecherlast
//                (Schwingspule + Resonanz der Trommel), Röhrenstreuung mit Drift, Wirbelstrom im Kern
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace leslie {
namespace amp {

constexpr double PI = 3.14159265358979323846;

struct Triode { double mu, ex, kg1, kp, kvb, rgi, vth, w; };
struct Pentode { double mu, ex, kg1, kg2, kp, kvb, rgi, vth, w; };
// 12AU7 wie in Amberwave Glow, 6550 nach Koren
static const Triode T_12AU7 { 21.5, 1.3, 1180, 84, 300, 2000, -0.7, 0.12 };
static const Pentode P_6550 { 7.9, 1.35, 890, 4200, 60, 24, 1000, -0.7, 0.18 };

static inline void softplus(double x, double& L, double& sig) {
    if (x > 40) { L = x; sig = 1; }
    else if (x < -40) { L = std::exp(x); sig = L; }
    else { double e = std::exp(x); L = std::log1p(e); sig = e / (1 + e); }
}

/** Triode: Anodenstrom und Ableitungen nach Vpk und Vgk */
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

/** Pentode: Teil, der nur von den Gittern abhängt (einmal je Takt) */
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
static inline double pentAtan(const Pentode& P, double Vpk) { return std::atan(std::max(Vpk, 0.5) / P.kvb); }
static inline double pentAtanD(const Pentode& P, double Vpk) { double x = std::max(Vpk, 0.5) / P.kvb; return (1 / P.kvb) / (1 + x * x); }
static inline double screenCurrent(const Pentode& P, double Vg1k, double Vg2k, double kgScale) {
    double x = Vg1k + Vg2k / P.mu;
    return x > 0 ? std::exp(P.ex * std::log(x)) / (P.kg2 * kgScale) : 0;
}

/** Gitterstrom (weich einsetzend ab etwa vth) */
static inline void gridCurrent(double rgi, double vth, double w, double Vgk, double& ig, double& dig) {
    double xg = (Vgk - vth) / w;
    if (xg > 30) { ig = (Vgk - vth) / rgi; dig = 1 / rgi; return; }
    if (xg < -40) { ig = 0; dig = 0; return; }
    double e = std::exp(xg);
    ig = w * std::log1p(e) / rgi; dig = (e / (1 + e)) / rgi;
}

// ---------------- Oversampling (Kaiser-gefensterter Sinc, polyphas hoch, an Phase 0 runter) ----------------
static inline double besselI0(double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }

struct Oversampler {
    int os = 1, taps = 1, ph = 1, delay = 0;
    std::vector<double> h, up, down;   // h: Länge ph*os
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
        for (auto& v : h) v *= os / sum;   // Verstärkung os beim Hochtasten (Nullen eingefügt)
        ringUp.assign((size_t) ph * 2, 0.0); ringDown.assign((size_t) taps * 2, 0.0); posUp = posDown = 0;
    }
    // ein Eingangswert → os Werte
    void upsample(double x, double* out) {
        if (os == 1) { out[0] = x; return; }
        ringUp[(size_t) posUp] = x; ringUp[(size_t) (posUp + ph)] = x;
        for (int k = 0; k < os; k++) {
            double s = 0;
            // Ausgang k: Summe über j von h[k + j*os] * x[n - j]
            for (int j = 0; j < ph; j++) s += h[(size_t) (k + j * os)] * ringUp[(size_t) (posUp + ph - j)];
            out[k] = s;
        }
        posUp = posUp == ph - 1 ? 0 : posUp + 1;
    }
    // os Werte → ein Ausgang (Tiefpass, an Phase 0 abgegriffen)
    double downsample(const double* in) {
        if (os == 1) return in[0];
        double y = 0;
        for (int k = 0; k < os; k++) {
            ringDown[(size_t) posDown] = in[k]; ringDown[(size_t) (posDown + taps)] = in[k];
            if (k == 0) {
                double s = 0;
                for (int j = 0; j < taps; j++) s += h[(size_t) j] * ringDown[(size_t) (posDown + taps - j)];
                y = s / os;
            }
            posDown = posDown == taps - 1 ? 0 : posDown + 1;
        }
        return y;
    }
};

// ---------------- die Endstufe ----------------
class LeslieAmp {
public:
    enum Model { M122 = 0, M122_EARLY = 1, M147 = 2 };

    explicit LeslieAmp(double sampleRate) : fs(sampleRate) { configure(); }

    void setModel(int m) { if (m != model) { model = std::max(0, std::min(2, m)); updateKin(); configure(); } }
    void setQuality(int q) { if (q != quality) { quality = std::max(0, std::min(2, q)); configure(); } }
    void setDrive(double d) { drive = std::max(0.0, std::min(1.0, d)); updateKin(); }
    void updateKin() { kin = 1.2 * std::pow(30.0, drive) * (model == M147 ? 1.8 : 1.0); }
    void setSag(double s) { if (std::fabs(s - sag) > 1e-6) { sag = std::max(0.0, std::min(1.0, s)); configure(); } }
    void setAge(double a) { if (std::fabs(a - age) > 1e-6) { age = std::max(0.0, std::min(1.0, a)); configure(); } }
    int getModel() const { return model; }
    int getQuality() const { return quality; }
    int latency() const { return ovs.delay; }

    // Zustand zur Anzeige (Volt / mA)
    double plateSupply() const { return Vb; }
    double cathodeVoltage() const { return Vko; }
    double plateCurrentMA() const { return 1000 * (lastIp[0] + lastIp[1]); }

    /** mono, in place. x: Signal des Orgel-Vorverstärkers (etwa ±0,3) */
    void process(float* x, int n) {
        if (quality == 0) { processStandard(x, n); return; }
        double up[8], dn[8];
        const int OS = ovs.os;
        for (int i = 0; i < n; i++) {
            ovs.upsample((double) x[i] * kin, up);
            for (int k = 0; k < OS; k++) dn[k] = tick(up[k]);
            double y = ovs.downsample(dn);
            // Hochpass gegen Gleichanteil (4 Hz)
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
            double u = (double) x[i] * kin * stdGain;            // normiert: 1 ≈ Vollaussteuerung
            double a = std::fabs(u);
            stdEnv += (a > stdEnv ? atk : rel) * (a - stdEnv);
            double us = u / (1 + sagK * stdEnv);
            double b = 0.35 + 0.25 * stdEnv;                      // Klasse AB, Kathoden-Bias wandert kälter
            double g1 = std::tanh(us + b) - std::tanh(b), g2 = std::tanh(-us + b) - std::tanh(b);
            double y = 0.5 * (g1 - 0.96 * g2);
            double w = y - dcX + dcR * dcY; dcX = y; dcY = w;
            x[i] = (float) (w * stdNorm / kin);
        }
    }

    // ---------- Schaltung: ein Takt bei os·fs; u = Volt am Treibergitter (symmetrisch: +u / −u) ----------
    double tick(double u) {
        const double h = dt;
        // --- Treiber (12AU7, zwei Systeme) ---
        double ipd[2], vpoc[2], rout[2];
        double ltpShift = 0;
        if (model == M147) {
            // Long-Tailed-Pair: Steuerspannung teilt sich über den gemeinsamen Schweif, leicht unsymmetrisch
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
                if (std::fabs(d) < 1e-4) break;
            }
            drvVp[j] = Vp; vpoc[j] = Vp; ipd[j] = ip;
            rout[j] = quality == 2 ? 1 / (1 / Ra + std::max(gp, 1e-7)) : drvRout;
        }
        // --- Koppelkondensator → Gitter der 6550 (mit Gitterstrom, Blocking) ---
        double Vg[2], ig[2];
        for (int j = 0; j < 2; j++) {
            double Req = rout[j] + h / Cc;
            double src = vpoc[j] - ccQ[j];                     // was über dem Kondensator „übrig“ bleibt
            double Vgj = src * Rg / (Rg + Req);
            double igj = 0, dig = 0;
            const double VkoJ = Vko + (j == 0 ? beta : -beta) * vPrev;
            if (Vgj - VkoJ > P_6550.vth - 12 * P_6550.w) {
                for (int it = 0; it < 8; it++) {
                    gridCurrent(P_6550.rgi, P_6550.vth, P_6550.w, Vgj - VkoJ, igj, dig);
                    double f = (src - Vgj) / Req - Vgj / Rg - igj;
                    double df = -1 / Req - 1 / Rg - dig;
                    double d = f / df; Vgj -= d;
                    if (std::fabs(d) < 1e-4) break;
                }
                gridCurrent(P_6550.rgi, P_6550.vth, P_6550.w, Vgj - VkoJ, igj, dig);
            }
            double I = (src - Vgj) / Req;
            ccQ[j] += h * I / Cc;
            Vg[j] = Vgj; ig[j] = igj;
        }
        // --- Endröhren im Gegentakt: v = halbe Primärspannung (Newton) ---
        double Vg2k = Vscreen - Vko;
        PentGrid pg0 = pentGrid(P_6550, Vg[0] - Vko, Vg2k, kgTube[0]);
        PentGrid pg1 = pentGrid(P_6550, Vg[1] - Vko, Vg2k, kgTube[1]);
        // Magnetisierungsstrom aus dem Kernfluss (explizit, Takt davor)
        double Im = magnetizingCurrent();
        // Last: Ifull = 2v·G + Ih
        double G, Ih;
        loadCompanion(G, Ih);
        double v = vPrev;
        double ip0 = 0, ip1 = 0;
        const double be = beta;   // 147: Kathoden-Gegenkopplung aus der Sekundärwicklung (Mittelanzapfung)
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
            if (std::fabs(d) < 1e-3) break;
        }
        vPrev = v;
        lastIp[0] = ip0; lastIp[1] = ip1;
        loadUpdate(2 * v);
        coreUpdate(2 * v);
        // --- Kathode (gemeinsam, überbrückt) ---
        double is0 = screenCurrent(P_6550, Vg[0] - Vko, Vg2k, kgTube[0]), is1 = screenCurrent(P_6550, Vg[1] - Vko, Vg2k, kgTube[1]);
        double Ik = ip0 + ip1 + is0 + is1 + ig[0] + ig[1];
        Vko += h * (Ik - Vko / Rko) / Cko;
        // Treiber-Kathode (gemeinsam, überbrückt) und -Versorgung
        Vkd += h * (ipd[0] + ipd[1] - Vkd / Rkd) / Ckd;
        if (quality == 2) Vbd += h * ((Vreg - Vbd) / 10e3 - (ipd[0] + ipd[1])) / 20e-6;
        // --- Netzteil ---
        double Ip = ip0 + ip1;
        if (quality == 2) {
            // Gleichrichter → C1 → Drossel → C2 (Anoden)
            V1 += h * ((V0 - V1) / Rs - iL) / C1;
            iL += h * (V1 - Vb - RL * iL) / Lch;
            Vb += h * (iL - Ip - is0 * 0 - Ireg) / C2;
        } else {
            Vb += h * ((V0 - Vb) / RsEff - Ip - Ireg) / C2;
        }
        return 2 * v;
    }

    // ---------- Ausgangsübertrager: Kern mit Hysterese (Play-Operatoren) ----------
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
        if (quality == 2) eddy = eddyK * dphi / dt * 1e-3;   // Wirbelstrom ∝ dΦ/dt
        phi += dphi;
        if (phi > bs * 0.98) phi = bs * 0.98; else if (phi < -bs * 0.98) phi = -bs * 0.98;
        coreH(phi, true);
    }

    // ---------- Lautsprecherlast (Primärseite, ganze Wicklung) ----------
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
        // Ultra: Röhren laufen langsam auseinander (Erwärmung), ±1 %
        driftPh += 0.05;
        kgTube[0] = kgBase[0] * (1 + 0.008 * std::sin(driftPh));
        kgTube[1] = kgBase[1] * (1 - 0.006 * std::sin(driftPh * 1.31 + 1));
    }

    // ---------- Aufbau ----------
    void configure() {
        int os = quality == 0 ? 1 : quality == 1 ? (fs >= 75000 ? 1 : 2) : (fs >= 150000 ? 1 : fs >= 75000 ? 2 : 4);
        ovs.init(os);
        dt = 1 / (fs * os);
        itDrv = quality == 2 ? 6 : 3; itOut = quality == 2 ? 8 : 4;
        dcR = 1 - 2 * PI * 4 / fs;
        // Schaltung
        drvRa = 47e3; Rkd = 1500; Ckd = 25e-6; Vreg = 310; Vbd = 260;
        Cc = 0.1e-6; Rg = 100e3;
        beta = model == M147 ? 0.061 : 0;
        Vscreen = 310; Rko = 150; Cko = 50e-6;
        Raa = 4300;
        Ireg = 0.012;   // Querstrom der OC3-Stabilisierung und Treiber aus derselben Versorgung
        bool tubeRect = model == M122_EARLY;
        V0 = tubeRect ? 455 : 438;
        Rs = (tubeRect ? 190 : 55) * (0.5 + 1.5 * sag);
        RsEff = Rs + 60;   // HQ: Drossel-Widerstand mit eingerechnet
        C1 = 30e-6; C2 = 40e-6; Lch = 4; RL = 60;
        ltpImbalance = 0.94;
        // Röhren: Streuung (fest) und Alter (weniger Emission)
        double emis = 1 + 0.7 * age;
        kgBase[0] = 1.00 * emis; kgBase[1] = (quality >= 1 ? 1.045 : 1.0) * emis;
        kgTube[0] = kgBase[0]; kgTube[1] = kgBase[1];
        // Übertrager: Eisenkern (Iron „eisen“), Primärinduktivität ~25 H
        bs = 1.5; hystW = quality >= 1 ? 0.45 : 0; double hyst = 0.07 * (quality == 2 ? 1.2 : 1);
        double ws = 0; for (int j = 1; j <= PLAYS; j++) { playR[j - 1] = hyst * j / PLAYS; playW[j - 1] = j; ws += j; }
        for (int j = 0; j < PLAYS; j++) playW[j] /= ws;
        fluxK = 0.8 * bs * 2 * PI * 35 / 700;     // 700 V Spitze bei 35 Hz → 80 % von Bs
        imScale = 1 / (fluxK * 25.0);             // Lp = 25 H
        eddyK = quality == 2 ? 0.02 : 0;
        // Last (Ultra): Schwingspule + Resonanz des 15″ in der Trommel, auf die Primärseite übersetzt
        // Gesamtlast (Weiche + 15″ + Horntreiber): oberhalb der Weiche nimmt der Horntreiber über, die Schwingspulen-Induktivität
        // des 15″ liegt hinter der Weichendrossel – übrig bleibt ein leichter Anstieg ganz oben und die Resonanz der Trommel
        R0 = Raa; L0 = R0 / (2 * PI * 30000);
        double f0 = 48, Q = 1.6, w0 = 2 * PI * f0; Rm = 0.6 * Raa; Lm = Rm / (Q * w0); Cm = Q / (Rm * w0);
        // Ruhezustand einschwingen lassen
        settle();
        calibrate();
    }

    void settle() {
        Vb = V0 - 0.2 * (V0 - 415); V1 = Vb; iL = 0.19; Vko = 25; Vkd = 4; Vbd = quality == 2 ? 258 : 260;
        drvVp[0] = drvVp[1] = 140; ccQ[0] = ccQ[1] = 140; vPrev = 0; phi = 0; eddy = 0;
        std::memset(play, 0, sizeof play); li = liLm = lvm = 0;
        // Ruhepunkt: einige hundert Millisekunden ohne Signal rechnen (Kondensatoren, Netzteil, Bias)
        if (quality == 0) return;
        int steps = (int) (0.6 / dt);
        for (int i = 0; i < steps; i++) tick(0);
        // Ausgangsimpedanz des Treibers (HQ): Ra ∥ rp am Ruhepunkt
        double ip, gp, gm; triode(T_12AU7, -Vkd, drvVp[0] - Vkd, ip, gp, gm);
        drvRout = 1 / (1 / drvRa + std::max(gp, 1e-7));
        dcX = dcY = 0;
    }

    /** Kleinsignal-Verstärkung messen (1 kHz, sehr leise), damit „Drive“ nur die Sättigung ändert, nicht die Lautstärke */
    void calibrate() {
        if (quality == 0) {
            // Standard: Steigung der Kennlinie bei 0 auf 1 normieren
            double b = 0.35, d = 1e-4;
            double g = 0.5 * ((std::tanh(d + b) - std::tanh(b)) - 0.96 * (std::tanh(-d + b) - std::tanh(b))) / d;
            stdGain = 361.0 / 700.0; stdNorm = 2.0 / (g * stdGain); stdEnv = 0;
            return;
        }
        // Zustand sichern
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

    const double fs;
    int model = M122, quality = 1;
    double drive = 0.35, kin = 1.2 * std::pow(30.0, 0.35), sag = 0.5, age = 0.0;
    Oversampler ovs;
    double dt = 1e-5;
    int itDrv = 3, itOut = 4;
    double dcX = 0, dcY = 0, dcR = 0.9995, outNorm = 1;
    // Standard
    double stdGain = 0.5, stdNorm = 1, stdEnv = 0;
    // Schaltung
    double drvRa = 47e3, Rkd = 1500, Ckd = 25e-6, Vreg = 310, Vbd = 260, drvRout = 8000;
    double Cc = 0.047e-6, Rg = 100e3, Vscreen = 310, Rko = 150, Cko = 50e-6, Raa = 4300, Ireg = 0.012;
    double beta = 0;
    double V0 = 450, Rs = 55, RsEff = 115, C1 = 30e-6, C2 = 40e-6, Lch = 4, RL = 60, ltpImbalance = 0.94;
    double kgBase[2] = {1, 1}, kgTube[2] = {1, 1};
    // Zustände
    double Vb = 415, V1 = 420, iL = 0.19, Vko = 25, Vkd = 4;
    double drvVp[2] = {140, 140}, ccQ[2] = {140, 140}, vPrev = 0, lastIp[2] = {0, 0};
    // Kern
    static constexpr int PLAYS = 6;
    double bs = 1.5, hystW = 0.45, playR[PLAYS] = {0}, playW[PLAYS] = {0}, play[PLAYS] = {0};
    double phi = 0, fluxK = 0.38, imScale = 0.1, eddy = 0, eddyK = 0;
    // Last
    double R0 = 3600, L0 = 0.26, Rm = 11000, Lm = 0.016, Cm = 0.6e-6, li = 0, liLm = 0, lvm = 0;
    // Drift
    double driftPh = 0; int driftCount = 0;
};

} // namespace amp
} // namespace leslie
