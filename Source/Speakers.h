// Wheelhouse – the Rotary's loudspeaker system as a circuit: passive 800 Hz crossover, Jensen 15" woofer in the vented
// middle compartment, Jensen V21 compression driver on the horn. The power amplifier (RotaryAmp) sees this network as
// its load at every (oversampled) step, so the impedance of the drivers shapes what the tubes deliver - the 122 has
// no feedback and drives the crossover almost from a current source, the 147 with its cathode feedback much stiffer.
//
//   crossover   woofer: series L1 5.2 mH + shunt C1 12.5 uF; horn: series C2 7.8 uF + shunt L3 3.2 mH (16 ohm)
//   woofer      voice coil Re + Le + semi-inductance (L2 || R2), motor Bl, cone mass / stiffness / loss; the box behind
//               the cone (compliance) with its port (mass + loss); radiation: half-space piston resistance
//   V21         voice coil as above, diaphragm mass / suspension, rear chamber (stiffness), compression chamber
//               (compliance) in front, then the throat: a wave guide of the elbow and the horn (tube sections from the
//               horn geometry, wall loss, mouth = radiation resistance || end-correction mass); the wave coming back
//               from the horn acts on the diaphragm
//
// Nonlinear (explicit, from the previous step): woofer Bl(x) and suspension stiffening, V21 suspension, the air in the
// compression chamber (P V^gamma with the chamber volume changing with the diaphragm), nonlinear propagation in the
// horn throat (second harmonic growing with level and frequency), voice-coil heating of both drivers (two thermal
// time constants each: power compression and a crossover that drifts as the coils warm up).
//
// Values: crossover from the Leslie service schematics (145/122 share it); drivers estimated from period data, see
// tools/acoustics/speakers.py, which computes the same network in the frequency domain (reference for the tests).
#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>

namespace rotary {
namespace spk {

constexpr double RHO = 1.18, C = 343.0, GAMMA = 1.4, P0 = 101325.0, PI = 3.14159265358979323846;

struct Params {
    // crossover
    double L1 = 5.2e-3, r1 = 0.55, C1 = 12.5e-6;
    double C2 = 7.8e-6, L3 = 3.2e-3, r3 = 0.45;
    // woofer
    double Rew = 13.6, Lew = 1.6e-3, L2w = 2.2e-3, R2w = 9.0;
    double Blw = 24.0, Mmw = 0.072, Kmw = 5700.0, Rmw = 5.7, Sdw = 0.0855;
    double Vb = 0.100, fb = 44.0, Qp = 4.0, aw = 0.15; // leaky compartment (gaps, cable and motor openings)
    double xbW = 0.006,
           xkW = 0.005; // Bl(x) = Bl0 / (1 + ((x - xo)/xb)^2) (normalised at rest), K(x) = K0 (1 + (x/xk)^2)
    double xoW = 0.0006, k1W = 60.0; // voice coil 0.6 mm off the gap centre; suspension asymmetry K0 k1 x (per m)
    // V21
    double Reh = 13.0, Leh = 0.28e-3, L2h = 0.35e-3, R2h = 12.0;
    double Blh = 14.5, Mmh = 0.0036, Ksh = 7000.0, Rmh = 3.0;
    double Sdh = 4.6e-3, Vbc = 320e-6, Vfc = 2.8e-6, rt = 0.0105;
    double xbH = 0.0008, xkH = 0.0005;
    // thermal: coil (K/W, s) and magnet (K/W, s)
    double thW[4] = {3.0, 12.0, 1.0, 1200.0}, thH[4] = {6.0, 1.5, 2.0, 200.0};
};

// ---------------- horn geometry (as tools/acoustics/horn.py, plus the elbow from the driver) ----------------
inline double hornRadius(double z) {
    const double mm = 1e-3, A0 = 9.5 * mm, L = 150 * mm, LB = 25 * mm, R1 = 28 * mm, AM = 45 * mm, ELBOW = 0.12;
    if (z < ELBOW) return 0.0105;
    z -= ELBOW;
    const double zc = L - LB;
    if (z <= zc) return A0 + (R1 - A0) * z / zc;
    const double u = std::min(1.0, (z - zc) / LB), slope = (R1 - A0) / zc * LB;
    return R1 + slope * u + (AM - R1 - slope) * u * u;
}
constexpr double HORN_PATH = 0.12 + 0.150, HORN_MOUTH = 0.045;

// ---------------- linear subsystem: x' = A x + B v + E w, port current i = c . x ----------------
struct Subsystem {
    static constexpr int MS = 8, MW = 4;
    int ns = 0, nw = 0;
    double A[MS][MS] = {}, B[MS] = {}, E[MS][MW] = {}, c[MS] = {};
    double Ad[MS][MS] = {}, Bd[MS] = {}, Ed[MS][MW] = {}, cAd[MS] = {}, cEd[MW] = {}, g = 0;
    double x[MS] = {};

    /** Trapezoidal rule with w held over the step:
        (I - hA/2) x1 = (I + hA/2) x0 + h/2 B (v0 + v1) + h E w. */
    void discretize(double h) {
        double M[MS][2 * MS] = {};
        for (int i = 0; i < ns; i++) {
            for (int j = 0; j < ns; j++) M[i][j] = (i == j ? 1.0 : 0.0) - 0.5 * h * A[i][j];
            M[i][ns + i] = 1;
        }
        for (int col = 0; col < ns; col++) { // Gauss-Jordan, partial pivoting
            int piv = col;
            for (int r = col + 1; r < ns; r++)
                if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
            if (piv != col)
                for (int k = 0; k < 2 * ns; k++) std::swap(M[col][k], M[piv][k]);
            const double d = M[col][col];
            for (int k = 0; k < 2 * ns; k++) M[col][k] /= d;
            for (int r = 0; r < ns; r++) {
                if (r == col) continue;
                const double f = M[r][col];
                if (f != 0)
                    for (int k = 0; k < 2 * ns; k++) M[r][k] -= f * M[col][k];
            }
        }
        for (int i = 0; i < ns; i++) {
            for (int j = 0; j < ns; j++) {
                double s = 0;
                for (int k = 0; k < ns; k++) s += M[i][ns + k] * ((k == j ? 1.0 : 0.0) + 0.5 * h * A[k][j]);
                Ad[i][j] = s;
            }
            double sb = 0;
            for (int k = 0; k < ns; k++) sb += M[i][ns + k] * B[k];
            Bd[i] = 0.5 * h * sb;
            for (int w = 0; w < nw; w++) {
                double se = 0;
                for (int k = 0; k < ns; k++) se += M[i][ns + k] * E[k][w];
                Ed[i][w] = h * se;
            }
        }
        g = 0;
        for (int i = 0; i < ns; i++) g += c[i] * Bd[i];
        for (int j = 0; j < ns; j++) {
            double s = 0;
            for (int i = 0; i < ns; i++) s += c[i] * Ad[i][j];
            cAd[j] = s;
        }
        for (int w = 0; w < nw; w++) {
            double s = 0;
            for (int i = 0; i < ns; i++) s += c[i] * Ed[i][w];
            cEd[w] = s;
        }
    }
    /** History part of the port current at the next step: i1 = g v1 + hist. */
    inline double hist(double v0, const double* w) const {
        double s = g * v0;
        for (int j = 0; j < ns; j++) s += cAd[j] * x[j];
        for (int k = 0; k < nw; k++) s += cEd[k] * w[k];
        return s;
    }
    inline void step(double v0, double v1, const double* w) {
        double y[MS];
        const double vs = v0 + v1;
        for (int i = 0; i < ns; i++) {
            double s = Bd[i] * vs;
            for (int j = 0; j < ns; j++) s += Ad[i][j] * x[j];
            for (int k = 0; k < nw; k++) s += Ed[i][k] * w[k];
            y[i] = s;
        }
        std::memcpy(x, y, sizeof(double) * (size_t) ns);
    }
};

// ---------------- horn wave guide (base rate) ----------------
class HornLine {
public:
    static constexpr int MAXSEG = 640;
    void prepare(double fs, double nonlinear) {
        const double dz = C / fs;
        n = std::max(4, std::min(MAXSEG, (int) std::lround(HORN_PATH / dz)));
        const double dzr = HORN_PATH / n;
        for (int i = 0; i < n; i++) {
            const double r = hornRadius((i + 0.5) * dzr);
            S[i] = PI * r * r;
        }
        // visco-thermal wall loss at 2 kHz, per segment
        for (int i = 0; i < n; i++) {
            const double r = std::sqrt(S[i] / PI);
            loss[i] = std::exp(-3.0e-5 * std::sqrt(2000.0) / r * dzr);
        }
        Smouth = S[n - 1];
        Z0t = RHO * C / S[0];
        const double tau = 2 * 0.61 * std::sqrt(Smouth / PI) / C, k = 2 * fs * tau;
        mA = 1 / (1 + k);
        mB = (1 - k) / (1 + k);
        radK = std::sqrt(Smouth / (RHO * C));
        // nonlinear propagation: second harmonic grows with distance x as (gamma+1)/2 x / (2 rho c^3) d(p^2)/dt; in the
        // horn the pressure falls as 1/sqrt(S): effective distance = integral of sqrt(S0 / S)
        double leff = 0;
        for (int i = 0; i < n; i++) leff += std::sqrt(S[0] / S[i]) * dzr;
        nlK = nonlinear * (GAMMA + 1) / 2 * leff / (2 * RHO * C * C * C) * fs;
        reset();
    }
    void reset() {
        std::memset(R, 0, sizeof R);
        std::memset(L, 0, sizeof L);
        mx1 = my1 = a1sq = 0;
    }
    /** Wave arriving at the throat at this sample (pressure, travelling towards the driver). */
    inline double back() const { return L[0]; }
    /** Throat impedance of the first tube section. */
    double throatZ0() const { return Z0t; }
    /** Advances one sample with the wave leaving the throat; returns the radiated signal (sqrt(W)). */
    inline double step(double a) {
        // nonlinear steepening of the outgoing wave (lumped at the throat)
        const double asq = a * a;
        const double aNl = a + nlK * (asq - a1sq);
        a1sq = asq;
        double Rn[MAXSEG], Ln[MAXSEG];
        Rn[0] = aNl * loss[0];
        for (int k = 1; k < n; k++) {              // junction between sections k-1 and k
            const double y1 = S[k - 1], y2 = S[k]; // admittances are proportional to the areas
            const double pj = 2 * (R[k - 1] * y1 + L[k] * y2) / (y1 + y2);
            Rn[k] = (pj - L[k]) * loss[k];
            Ln[k - 1] = (pj - R[k - 1]) * loss[k - 1];
        }
        // mouth: radiation resistance || end-correction mass -> R = -1 / (1 + s tau)
        const double pin = R[n - 1];
        const double refl = -mA * (pin + mx1) - mB * my1;
        mx1 = pin;
        my1 = refl;
        Ln[n - 1] = refl * loss[n - 1];
        std::memcpy(R, Rn, sizeof(double) * (size_t) n);
        std::memcpy(L, Ln, sizeof(double) * (size_t) n);
        return (pin + refl) * radK; // mouth pressure into the radiation resistance rho c / S
    }

private:
    int n = 4;
    double S[MAXSEG] = {}, loss[MAXSEG] = {}, R[MAXSEG] = {}, L[MAXSEG] = {};
    double Smouth = 1, Z0t = 1, mA = 0.5, mB = 0, mx1 = 0, my1 = 0, radK = 1, nlK = 0, a1sq = 0;
};

// ---------------- the whole system ----------------
class System {
public:
    /** fsBase: sample rate of the engine; os: oversampling factor of the amp loop (network rate = fsBase * os). */
    void prepare(double fsBase, int osFactor) {
        fs = fsBase;
        os = std::max(1, osFactor);
        h = 1 / (fs * os);
        line.prepare(fs, 1.0);
        Z0 = line.throatZ0();
        build();
        wf.discretize(h);
        hn.discretize(h);
        // woofer radiation: half-space piston, |H|^2 = rho c / S * (ka)^2 / (2 + (ka)^2) -> first-order high-pass
        {
            const double wc = std::sqrt(2.0) * C / P.aw, k = 2 * fs / wc;
            radA = k / (1 + k);
            radB = (1 - k) / (1 + k);
            radG = std::sqrt(RHO * C / (PI * P.aw * P.aw));
        }
        reset();
        calibrate();
        reset();
    }
    void reset() {
        std::memset(wf.x, 0, sizeof wf.x);
        std::memset(hn.x, 0, sizeof hn.x);
        line.reset();
        vPrev = 0;
        b = 0;
        rx1 = ry1 = 0;
        Tw[0] = Tw[1] = Th[0] = Th[1] = 0;
        pw = ph = 0;
        nPow = 0;
        dReW = dReH = 0;
        std::memset(ww, 0, sizeof ww);
        std::memset(wh, 0, sizeof wh);
        uOut = aOut = 0;
    }

    // ---- per base sample ----
    /** Takes the wave coming back from the horn for the OS steps of this sample. */
    inline void beginSample() { b = line.back(); }
    /** Port (16 ohm terminals) as a Norton equivalent for the next step: i = G v + Ih. Also fixes the nonlinear
        correction inputs of this step (explicit, from the current state). */
    inline void companion(double& G, double& Ih) {
        corrections();
        G = wf.g + hn.g;
        Ih = wf.hist(vPrev, ww) + hn.hist(vPrev, wh);
    }
    /** Advances the network with the terminal voltage of this step; uOut / aOut: woofer volume velocity (cone +
        port) and the wave leaving the throat. */
    inline void step(double v) {
        wf.step(vPrev, v, ww);
        hn.step(vPrev, v, wh);
        vPrev = v;
        uOut = P.Sdw * wf.x[4] + wf.x[7];
        aOut = hn.x[6] - b;
        pw += wf.x[2] * wf.x[2];
        ph += hn.x[2] * hn.x[2];
        nPow++;
    }
    double wooferU() const { return uOut; }
    double throatWave() const { return aOut; }
    /** Ends the base sample with the decimated network outputs; lo / hi: radiated signals of woofer and horn,
        calibrated (2 kHz, horn, voltage drive: 1 V at the terminals -> 1). */
    inline void endSample(double uw, double a, double& lo, double& hi) {
        // woofer: radiated pressure from the volume velocity (high-pass of the radiation resistance)
        const double y = radA * (uw - rx1) - radB * ry1;
        rx1 = uw;
        ry1 = y;
        lo = y * radG * kCal;
        hi = line.step(a) * kCal;
        // voice-coil heating: two thermal nodes per driver (explicit, per base sample)
        if (nPow > 0) {
            const double iw2 = pw / nPow, ih2 = ph / nPow;
            pw = ph = 0;
            nPow = 0;
            thermal(Tw, P.thW, iw2 * P.Rew * (1 + 0.00393 * Tw[0]));
            thermal(Th, P.thH, ih2 * P.Reh * (1 + 0.00393 * Th[0]));
            dReW = P.Rew * 0.00393 * Tw[0];
            dReH = P.Reh * 0.00393 * Th[0];
        }
    }
    /** Coil temperature rise (K) of woofer and horn driver. */
    double coilTempW() const { return Tw[0]; }
    double coilTempH() const { return Th[0]; }
    /** Small-signal |Z| at the terminals at 1 kHz (for the Thevenin drive of the Standard amp). */
    double z1k() const { return zRef; }
    Params P;

private:
    void build() {
        const double Cab = P.Vb / (RHO * C * C), Map = 1 / std::pow(2 * PI * P.fb, 2) / Cab;
        const double Rap = 2 * PI * P.fb * Map / P.Qp;
        // woofer: x = [iL1, vC1, iw, iL2, u, x, pbox, Uport], w = [Enl, Fnl]
        Subsystem& s = wf;
        s = Subsystem{};
        s.ns = 8;
        s.nw = 2;
        auto& A = s.A;
        A[0][0] = -P.r1 / P.L1;
        A[0][1] = -1 / P.L1;
        s.B[0] = 1 / P.L1;
        A[1][0] = 1 / P.C1;
        A[1][2] = -1 / P.C1;
        A[2][1] = 1 / P.Lew;
        A[2][2] = -(P.Rew + P.R2w) / P.Lew;
        A[2][3] = P.R2w / P.Lew;
        A[2][4] = -P.Blw / P.Lew;
        s.E[2][0] = 1 / P.Lew;
        A[3][2] = P.R2w / P.L2w;
        A[3][3] = -P.R2w / P.L2w;
        A[4][2] = P.Blw / P.Mmw;
        A[4][4] = -P.Rmw / P.Mmw;
        A[4][5] = -P.Kmw / P.Mmw;
        A[4][6] = P.Sdw / P.Mmw;
        s.E[4][1] = 1 / P.Mmw;
        A[5][4] = 1;
        A[6][4] = -P.Sdw / Cab;
        A[6][7] = -1 / Cab;
        A[7][6] = 1 / Map;
        A[7][7] = -Rap / Map;
        s.c[0] = 1;
        // V21: x = [vC2, iL3, ih, iL2, u, x, pfront], w = [Enl, Fnl, b, Qnl]
        Subsystem& t = hn;
        t = Subsystem{};
        t.ns = 7;
        t.nw = 4;
        auto& H = t.A;
        const double Cfc = P.Vfc / (RHO * C * C), Kbc = RHO * C * C * P.Sdh * P.Sdh / P.Vbc;
        H[0][1] = 1 / P.C2;
        H[0][2] = 1 / P.C2;
        H[1][0] = -1 / P.L3;
        H[1][1] = -P.r3 / P.L3;
        t.B[1] = 1 / P.L3;
        H[2][0] = -1 / P.Leh;
        H[2][2] = -(P.Reh + P.R2h) / P.Leh;
        H[2][3] = P.R2h / P.Leh;
        H[2][4] = -P.Blh / P.Leh;
        t.B[2] = 1 / P.Leh;
        t.E[2][0] = 1 / P.Leh;
        H[3][2] = P.R2h / P.L2h;
        H[3][3] = -P.R2h / P.L2h;
        H[4][2] = P.Blh / P.Mmh;
        H[4][4] = -P.Rmh / P.Mmh;
        H[4][5] = -(P.Ksh + Kbc) / P.Mmh;
        H[4][6] = -P.Sdh / P.Mmh;
        t.E[4][1] = 1 / P.Mmh;
        H[5][4] = 1;
        H[6][4] = P.Sdh / Cfc;
        H[6][6] = -1 / (Z0 * Cfc);
        t.E[6][2] = 2 / (Z0 * Cfc); // throat flow (p - 2b) / Z0
        t.E[6][3] = 1 / Cfc;        // nonlinear compliance flow
        t.c[1] = 1;
        t.c[2] = 1;
        cfcK = Cfc;
        v0h = P.Vfc;
    }
    /** Explicit nonlinear terms from the current state (held over the next step). */
    inline void corrections() {
        // woofer
        {
            const double x = wf.x[5], u = wf.x[4], i = wf.x[2];
            const double q = (x - P.xoW) / P.xbW, q0 = P.xoW / P.xbW;
            const double bl = P.Blw * (1 + q0 * q0) / (1 + q * q), dBl = bl - P.Blw;
            const double r = x / P.xkW, dK = P.Kmw * (r * r + P.k1W * x);
            ww[0] = -dBl * u - dReW * i;
            ww[1] = dBl * i - dK * x;
        }
        // V21
        {
            const double x = hn.x[5], u = hn.x[4], i = hn.x[2], pf = hn.x[6];
            const double q = x / P.xbH, bl = P.Blh / (1 + q * q), dBl = bl - P.Blh;
            const double r = x / P.xkH, dK = P.Ksh * r * r;
            wh[0] = -dBl * u - dReH * i;
            wh[1] = dBl * i - dK * x;
            wh[2] = b;
            // compression chamber: dp/dt = gamma (P0 + p) / (V0 - Sd x) * (Sd u - Ut) instead of the linear
            // gamma P0 / V0 * (...); the excess as a volume-velocity source into the chamber compliance
            const double flow = P.Sdh * u - (pf - 2 * b) / Z0;
            const double eps = std::max(-0.5, std::min(0.5, P.Sdh * x / v0h));
            wh[3] = flow * ((1 + pf / P0) / (1 - eps) - 1);
        }
    }
    void thermal(double* T, const double* th, double P_) {
        // node 1 (coil): C1 dT1/dt = P - (T1 - T2)/R1; node 2 (magnet): C2 dT2/dt = (T1 - T2)/R1 - T2/R2
        const double R1 = th[0], C1 = th[1] / th[0], R2 = th[2], C2 = th[3] / th[2], dt = 1 / fs;
        const double q12 = (T[0] - T[1]) / R1;
        T[0] += dt * (P_ - q12) / C1;
        T[1] += dt * (q12 - T[1] / R2) / C2;
    }
    /** Ideal voltage drive at the terminals: horn output at 2 kHz -> kCal; |Z| at 1 kHz -> zRef. */
    void calibrate() {
        kCal = 1;
        const double saveTw0 = Tw[0], saveTh0 = Th[0];
        auto run = [&](double f, double& outRms, double& curRms) {
            reset();
            const int n = (int) (0.06 * fs);
            double so = 0, sc = 0;
            int cnt = 0;
            for (int i = 0; i < n; i++) {
                beginSample();
                double uSum = 0, aSum = 0, iLast = 0;
                for (int k = 0; k < os; k++) {
                    const double t = (i * os + k) * h, v = 1e-3 * std::sin(2 * PI * f * t);
                    double G, Ih;
                    companion(G, Ih);
                    iLast = G * v + Ih;
                    step(v);
                    uSum += uOut;
                    aSum += aOut;
                }
                double lo, hi;
                endSample(uSum / os, aSum / os, lo, hi);
                if (i > n / 2) {
                    so += hi * hi;
                    sc += iLast * iLast;
                    cnt++;
                }
            }
            outRms = std::sqrt(so / std::max(1, cnt));
            curRms = std::sqrt(sc / std::max(1, cnt));
        };
        double o, c;
        run(2000, o, c);
        kCal = (1e-3 / std::sqrt(2.0)) / std::max(o, 1e-30);
        run(1000, o, c);
        zRef = (1e-3 / std::sqrt(2.0)) / std::max(c, 1e-30);
        Tw[0] = saveTw0;
        Th[0] = saveTh0;
    }

    Subsystem wf, hn;
    HornLine line;
    double fs = 48000, h = 1 / 96000.0, Z0 = 1, cfcK = 1, v0h = 1;
    int os = 1;
    double vPrev = 0, b = 0, uOut = 0, aOut = 0;
    double ww[Subsystem::MW] = {}, wh[Subsystem::MW] = {};
    double radA = 1, radB = 0, radG = 1, rx1 = 0, ry1 = 0, kCal = 1, zRef = 16;
    double Tw[2] = {}, Th[2] = {}, pw = 0, ph = 0, dReW = 0, dReH = 0;
    int nPow = 0;
};

} // namespace spk
} // namespace rotary
