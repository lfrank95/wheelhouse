// Wheelhouse - the Hammond scanner vibrato as a circuit: the coupled-coil LC delay line of the B-3/C-3 (18 sections
// after serial 56000, 5.6 nF per section, 2.7 nF + 15 kOhm at the end), driven by the 12BH7 line driver, and the
// motor-driven capacitive scanner (16 stator plates on 9 taps, 412 rpm) that reads it. V1/V2/V3 connect the nine
// scanner taps to a third, half or the whole line; C1..C3 put a resistor in series with the line so part of the
// signal bypasses it (vibrato chorus).
//
// What is measured / documented and what is assumed:
//   known   - 18 sections, 0.0056 uF caps (one 0.0027 uF at the end), 15 kOhm termination R77, whole line about
//             1 ms (about +-1.5 % pitch at 6.87 Hz), usable to about 5 kHz, 16 plates / 9 taps / 412 rpm, plate
//             capacitance about 25 pF, V3 = whole line, V2 about half, V1 about a third, no tap attenuators on
//             the 5.6 nF line, coupled coils so that 18 sections give the delay of the older 25
//   fitted  - L = 450 mH per coil, coupling k = 0.15 between neighbours: 1.0 ms flat to 4 kHz, -3 dB at 5.8 kHz
//             (tools: the frequency-domain check in tools/acoustics/scanner.py)
//   assumed - driver source resistance 8 kOhm, coil resistance 400 Ohm, the tap-to-node tables below (V1 / V2 as
//             on the older 25-section line: taps every section / alternately 1 and 2 sections), rotor stray
//             capacitance 60 pF, rotor eccentricity 4 %, plate position scatter 2 %.
//
// Numerics: the line is a 36-state network (node voltages, coil currents scaled by Z0) integrated with the
// trapezoidal rule at the audio rate (prewarped at 2.5 kHz); the scanner is the capacitive divider of the plates
// under the rotor against its stray capacitance, so the crossfade between taps, the slight level ripple and the
// treble flutter at the crossovers all come from the circuit rather than from a crossfade law.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace rotary::scan {

constexpr int SECTIONS = 18, PLATES = 16, TAPS = 9;
constexpr double PI = 3.14159265358979323846;

struct Spec {
    double L = 0.45, k = 0.15, Rcoil = 400, C = 5.6e-9, Cend = 2.7e-9, Rterm = 15e3, Rsrc = 8e3;
    double Rchorus = 9.4e3; // in series with the line in C1..C3 (about Z0: half the signal bypasses the line)
    double Cplate = 25e-12, Cstray = 60e-12, Cleak = 0.3e-12; // scanner: plate, rotor to ground, plate leakage
    double fringe = 0.18; // the overlap area is a triangle over +-1 plate pitch; fringing rounds its corners by this
    double rpm = 412;
    double ecc = 0.04, posScatter = 0.02, partScatter = 0.03; // rotor eccentricity, plate position, L/C tolerance
};

/** Node (1..18) read by scanner tap t = 0..8 for V1/V2/V3. The 16 plates run 0..8 and back (triangle). */
inline const int* tapNodes(int depth) {
    // the spacing scheme of the older 25-section line (taps every 1 / 1.5 / 3 sections) carried over: every
    // section, alternately 1 and 2, every other section - 8, 12 and 16 of the 18 sections
    static const int T[3][TAPS] = {{1, 2, 3, 4, 5, 6, 7, 8, 9},      // V1
                                   {1, 2, 4, 5, 7, 8, 10, 11, 13},   // V2
                                   {1, 3, 5, 7, 9, 11, 13, 15, 17}}; // V3: the whole line
    return T[depth < 1 ? 0 : depth > 3 ? 2 : depth - 1];
}

class Scanner {
public:
    static constexpr int NS = 2 * SECTIONS;

    void prepare(double sampleRate, uint32_t seed = 0x5CA11E2u) {
        fs = sampleRate;
        // part tolerances and plate geometry, fixed per instrument
        uint32_t s = seed;
        auto rnd = [&s] {
            s = s * 1664525u + 1013904223u;
            return s / 4294967296.0 * 2 - 1;
        };
        for (int i = 0; i < SECTIONS; i++) {
            Lsc[i] = 1 + spec.partScatter * rnd();
            Csc[i] = 1 + spec.partScatter * rnd();
        }
        for (int p = 0; p < PLATES; p++) {
            plateOff[p] = spec.posScatter * rnd();
            plateGain[p] = 1 + 0.5 * spec.partScatter * rnd();
        }
        eccPhase = 0.5 * (rnd() + 1);
        build(chorus ? spec.Rchorus : 0.0);
        reset();
    }
    void reset() {
        x.fill(0);
        uPrev = 0;
        angle = 0;
    }
    /** "v1".."v3", "c1".."c3"; anything else = off (then process() passes the input through). */
    void setMode(const std::string& m) {
        const bool on = m.size() == 2 && (m[0] == 'v' || m[0] == 'c') && m[1] >= '1' && m[1] <= '3';
        active = on;
        if (!on) return;
        depth = m[1] - '0';
        const bool ch = m[0] == 'c';
        if (ch != chorus) {
            chorus = ch;
            if (fs > 0) build(chorus ? spec.Rchorus : 0.0);
        }
        nodes = tapNodes(depth);
    }
    bool isActive() const { return active; }
    bool isChorus() const { return chorus; }

    /** One sample: the organ signal in, what the scanner (and in chorus the bypass) delivers out. */
    double process(double u) {
        if (!active) return u;
        // ---- the line: x1 = Ad x0 + Bd (u0 + u1)
        const double uu = u + uPrev;
        uPrev = u;
        std::array<float, NS> xn;
        for (int i = 0; i < NS; i++) {
            const float* row = Ad[i].data();
            float acc = 0;
            for (int j = 0; j < NS; j++) acc += row[j] * x[j];
            xn[i] = acc + Bd[i] * (float) uu;
        }
        x = xn;
        // ---- the scanner: rotor over the plates, capacitive divider of the plates against the stray capacitance
        angle += spec.rpm / 60 / fs;
        if (angle >= 1) angle -= 1;
        const double eccF = 1 + spec.ecc * std::cos(2 * PI * (angle - eccPhase));
        double num = leakNum(), den = spec.Cstray + PLATES * spec.Cleak;
        const double pos = angle * PLATES;
        const int p0 = (int) std::floor(pos - 1 - spec.fringe), p1 = (int) std::ceil(pos + 1 + spec.fringe);
        for (int pp = p0; pp <= p1; pp++) {
            const int p = ((pp % PLATES) + PLATES) % PLATES;
            const double sh = shape(pos - (pp + plateOff[p]));
            if (sh <= 0) continue;
            const double cap = spec.Cplate * sh * eccF * plateGain[p];
            num += cap * tapVoltage(p);
            den += cap;
        }
        double out = num / den * norm;
        if (chorus) out = (out + bypass()) * CHORUS_LEVEL;
        return out;
    }
    /** Node voltage 1..18 (for tests). */
    double node(int n) const { return x[n - 1]; }
    double normGain() const { return norm; }

    Spec spec;

private:
    // plate p (0..15) -> tap 0..8 and back
    static int tapOf(int p) { return p <= 8 ? p : PLATES - p; }
    double tapVoltage(int p) const { return x[nodes[tapOf(p)] - 1]; }
    double leakNum() const { // every plate leaks a little into the rotor: the sum of its tap voltages
        double s = 0;
        for (int p = 0; p < PLATES; p++) s += tapVoltage(p);
        return s * spec.Cleak;
    }
    /** Voltage across the chorus resistor: the part of the signal that does not go through the line. */
    double bypass() const { return (double) x[SECTIONS] / z0 * rch * norm * bypassNorm; }

    void build(double rChorus) {
        rch = rChorus;
        z0 = std::sqrt(spec.L / spec.C);
        // inductance matrix (coupled neighbours) and its inverse
        std::vector<double> Lm((size_t) SECTIONS * SECTIONS, 0.0), Li;
        for (int i = 0; i < SECTIONS; i++) {
            Lm[(size_t) (i * SECTIONS + i)] = spec.L * Lsc[i];
            if (i + 1 < SECTIONS) {
                const double m = spec.k * spec.L * std::sqrt(Lsc[i] * Lsc[i + 1]);
                Lm[(size_t) (i * SECTIONS + i + 1)] = Lm[(size_t) ((i + 1) * SECTIONS + i)] = m;
            }
        }
        Li = inverse(Lm, SECTIONS);
        // states: v_1..v_18 (index 0..17), j_1..j_18 = Z0 * i (index 18..35); input u = source voltage
        std::vector<double> A((size_t) NS * NS, 0.0), B((size_t) NS, 0.0);
        auto a = [&](int r, int c) -> double& {
            return A[(size_t) (r * NS + c)];
        };
        for (int q = 1; q <= SECTIONS; q++) { // nodes
            const double Cq = (q == SECTIONS ? spec.Cend : spec.C) * Csc[q - 1];
            a(q - 1, SECTIONS + q - 1) += 1 / (z0 * Cq);
            if (q < SECTIONS) a(q - 1, SECTIONS + q) -= 1 / (z0 * Cq);
            if (q == SECTIONS) a(q - 1, q - 1) -= 1 / (spec.Rterm * Cq);
        }
        // coils: dj/dt = Z0 Linv (e - Rcoil j / Z0), e_q = v_{q-1} - v_q, v_0 = u - (Rsrc + Rch) j_1 / Z0
        for (int q = 1; q <= SECTIONS; q++) {
            for (int m = 1; m <= SECTIONS; m++) {
                const double li = Li[(size_t) ((q - 1) * SECTIONS + (m - 1))] * z0;
                if (li == 0) continue;
                // e_m terms
                if (m > 1) a(SECTIONS + q - 1, m - 2) += li;                   // + v_{m-1}
                a(SECTIONS + q - 1, m - 1) -= li;                              // - v_m
                a(SECTIONS + q - 1, SECTIONS + m - 1) -= li * spec.Rcoil / z0; // coil resistance
                if (m == 1) {
                    B[(size_t) (SECTIONS + q - 1)] += li;                         // + u
                    a(SECTIONS + q - 1, SECTIONS) -= li * (spec.Rsrc + rch) / z0; // source + chorus resistor
                }
            }
        }
        // trapezoidal rule, prewarped so 2.5 kHz lands exactly
        const double w0 = 2 * PI * 2500 / fs;
        const double h = 1 / fs * std::tan(w0 / 2) / (w0 / 2);
        std::vector<double> G((size_t) NS * NS, 0.0);
        for (int i = 0; i < NS; i++)
            for (int j = 0; j < NS; j++) G[(size_t) (i * NS + j)] = (i == j ? 1.0 : 0.0) - 0.5 * h * a(i, j);
        const std::vector<double> Gi = inverse(G, NS);
        for (int i = 0; i < NS; i++) {
            for (int j = 0; j < NS; j++) {
                double s = 0;
                for (int k = 0; k < NS; k++)
                    s += Gi[(size_t) (i * NS + k)] * ((k == j ? 1.0 : 0.0) + 0.5 * h * a(k, j));
                Ad[i][j] = (float) s;
            }
            double sb = 0;
            for (int k = 0; k < NS; k++) sb += Gi[(size_t) (i * NS + k)] * B[(size_t) k];
            Bd[i] = (float) (0.5 * h * sb);
        }
        // level: unity at low frequencies for the middle of the scanned line (the vibrato amplifier makes up the
        // divider loss in the original); in chorus the bypass is scaled so the sum stays at the same level
        const double Rtot = spec.Rsrc + rch + SECTIONS * spec.Rcoil + spec.Rterm;
        const double vMid = spec.Rterm / Rtot; // DC: every node sits at about the termination's share
        const double divider = spec.Cplate * centreSum() / (spec.Cplate * centreSum() + spec.Cstray);
        const double vLine = vMid, vByp = rch / Rtot;
        norm = 1 / (divider * (vLine + vByp));
        bypassNorm = divider; // the bypass is not divided by the scanner; bring it to the same scale as the taps
    }
    /** Plate capacitance vs rotor distance in plate pitches: the overlap area (a triangle over +-1 pitch) with the
     * corners rounded by fringing (a triangle convolved with a box of half-width `fringe`). */
    double shape(double d) const {
        d = std::fabs(d);
        const double r = spec.fringe;
        if (d >= 1 + r) return 0;
        if (d <= r) return 1 - r / 2 - d * d / (2 * r);
        if (d <= 1 - r) return 1 - d;
        const double e = 1 + r - d;
        return e * e / (4 * r);
    }
    double centreSum() const { // sum of the plate shapes with the rotor centred on one plate
        double s = 0;
        for (int d = -2; d <= 2; d++) s += shape((double) d);
        return s;
    }
    // chorus: the line's half and the bypassed half add coherently in the bass and in power higher up
    // (+3 / -3 dB re the vibrato setting); the original's "off" resistor keeps the volume about constant, so the
    // sum is scaled to sit between the two
    static constexpr double CHORUS_LEVEL = 1.1;
    static std::vector<double> inverse(std::vector<double> M, int n) { // Gauss-Jordan with partial pivoting
        std::vector<double> I((size_t) n * n, 0.0);
        for (int i = 0; i < n; i++) I[(size_t) (i * n + i)] = 1;
        for (int col = 0; col < n; col++) {
            int piv = col;
            for (int r = col + 1; r < n; r++)
                if (std::fabs(M[(size_t) (r * n + col)]) > std::fabs(M[(size_t) (piv * n + col)])) piv = r;
            if (piv != col)
                for (int k = 0; k < n; k++) {
                    std::swap(M[(size_t) (col * n + k)], M[(size_t) (piv * n + k)]);
                    std::swap(I[(size_t) (col * n + k)], I[(size_t) (piv * n + k)]);
                }
            const double d = M[(size_t) (col * n + col)];
            for (int k = 0; k < n; k++) {
                M[(size_t) (col * n + k)] /= d;
                I[(size_t) (col * n + k)] /= d;
            }
            for (int r = 0; r < n; r++) {
                if (r == col) continue;
                const double f = M[(size_t) (r * n + col)];
                if (f == 0) continue;
                for (int k = 0; k < n; k++) {
                    M[(size_t) (r * n + k)] -= f * M[(size_t) (col * n + k)];
                    I[(size_t) (r * n + k)] -= f * I[(size_t) (col * n + k)];
                }
            }
        }
        return I;
    }

    double fs = 0, z0 = 1, rch = 0, norm = 1, bypassNorm = 1;
    bool active = false, chorus = false;
    int depth = 3;
    const int* nodes = tapNodes(3);
    std::array<std::array<float, NS>, NS> Ad{};
    std::array<float, NS> Bd{}, x{};
    double uPrev = 0, angle = 0, eccPhase = 0;
    std::array<double, SECTIONS> Lsc{}, Csc{};
    std::array<double, PLATES> plateOff{}, plateGain{};
};

} // namespace rotary::scan
