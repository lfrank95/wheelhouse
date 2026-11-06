// Wheelhouse - DSP core: basic building blocks and data types shared by RotaryRoomDSP and its parts.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>

namespace rotary {

constexpr double TWO_PI = 6.283185307179586;
constexpr double C_SOUND = 343.0;

// ---- Room model constants ----
// Air absorption: a path of length d keeps treble up to AIR_HF_PER_M / d Hz (one-pole), never below AIR_HF_MIN_HZ.
constexpr double AIR_HF_MIN_HZ = 2500.0, AIR_HF_PER_M = 260000.0;
// Walls: each bounce keeps WALL_HF_KEEP of the treble bandwidth (times the material's HF reflection); a reflected
// horn path is limited to HORN_WALL_HF_HZ, a drum path to DRUM_WALL_HF_HZ (scaled by that per-bounce factor).
constexpr double WALL_HF_KEEP = 0.62, HORN_WALL_HF_HZ = 9000.0, DRUM_WALL_HF_HZ = 18000.0;
// Sharp (pin-point) share of a path: cos^2 of its elevation times SHARP_GAIN, clipped to 1. The rest is spread wide.
constexpr double SHARP_GAIN = 1.3;
// Closest distance used for the 1/d gain of a fast image path (avoids a blow-up next to a wall).
constexpr double MIN_PATH_DIST = 0.3;
// Wall patches (diffuse first-order reflection): edge ~PATCH_SIZE_M, at most PATCH_MAX_PER_SIDE per surface edge;
// per rotor only the strongest patches carrying PATCH_ENERGY of the diffuse energy stay active, at most
// PATCH_MAX_ACTIVE.
constexpr double PATCH_SIZE_M = 2.5, PATCH_ENERGY = 0.97;
constexpr int PATCH_MAX_PER_SIDE = 4, PATCH_MAX_ACTIVE = 24;

/** Fractional delay read for the rotating paths: 8-tap Kaiser-windowed sinc (beta 6), 1024 phases.
    A cubic's treble loss depends on the fractional position; with the Doppler sweeping that position ~100 times a
    second on Slow, the high harmonics fluttered (2.6 % at 8 kHz, 12 % at 12 kHz). This one stays flat within
    0.1 % up to 12 kHz. Taps i1-3 .. i1+4 around rp = i1 + fr, no added delay. */
struct SincInterp {
    static constexpr int PH = 1024, T = 8;
    float h[PH + 1][T];
    SincInterp() {
        auto i0 = [](double x) {
            double s = 1, t = 1;
            for (int k = 1; k < 30; k++) {
                t *= (x / (2 * k)) * (x / (2 * k));
                s += t;
            }
            return s;
        };
        const double beta = 6.0, ib = 1 / i0(beta);
        for (int p = 0; p <= PH; p++) {
            const double fr = (double) p / PH;
            double sum = 0, w[T];
            for (int k = 0; k < T; k++) {
                const double x = (k - 3) - fr, r = x / 4.0;
                const double win = std::fabs(r) >= 1 ? 0 : i0(beta * std::sqrt(1 - r * r)) * ib;
                const double sn = std::fabs(x) < 1e-12 ? 1 : std::sin(3.141592653589793 * x) / (3.141592653589793 * x);
                w[k] = sn * win;
                sum += w[k];
            }
            for (int k = 0; k < T; k++) h[p][k] = (float) (w[k] / sum);
        }
    }
    static const SincInterp& get() {
        static const SincInterp t;
        return t;
    }
    /** buf is a ring of size mask + 1; reads at i1 + fr (0 <= fr < 1). */
    /** Same, for nb in-phase bands mixed with gains g: sum_b g_b * read(bufs[b]). */
    inline double readBands(const float* const* bufs, const float* g, int nb, int mask, int i1, double fr) const {
        const float* c = h[(int) (fr * PH + 0.5)];
        const int b0 = i1 - 3;
        double v = 0;
        for (int k = 0; k < T; k++) {
            const int ix = (b0 + k) & mask;
            double s = 0;
            for (int b = 0; b < nb; b++) s += (double) g[b] * bufs[b][ix];
            v += (double) c[k] * s;
        }
        return v;
    }
    inline double read(const float* buf, int mask, int i1, double fr) const {
        const float* c = h[(int) (fr * PH + 0.5)];
        const int b = i1 - 3;
        double v = 0;
        for (int k = 0; k < T; k++) v += (double) c[k] * buf[(b + k) & mask];
        return v;
    }
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    Biquad& set(const char* type, double f, double q, double fs, double gainDb = 0) {
        double w = TWO_PI * f / fs, cw = std::cos(w), sw = std::sin(w), al = sw / (2 * q);
        double B0, B1, B2, A0 = 1 + al, A1 = -2 * cw, A2 = 1 - al;
        if (type[0] == 'l') {
            B0 = (1 - cw) / 2;
            B1 = 1 - cw;
            B2 = (1 - cw) / 2;
        } else if (type[0] == 'b') {
            B0 = al;
            B1 = 0;
            B2 = -al;
        } // band-pass, 0 dB at the centre
        else if (type[0] == 'h') {
            B0 = (1 + cw) / 2;
            B1 = -(1 + cw);
            B2 = (1 + cw) / 2;
        } else if (type[0] == 's') { // low shelf (RBJ cookbook)
            double A = std::pow(10.0, gainDb / 40), sA = 2 * std::sqrt(A) * al;
            B0 = A * ((A + 1) - (A - 1) * cw + sA);
            B1 = 2 * A * ((A - 1) - (A + 1) * cw);
            B2 = A * ((A + 1) - (A - 1) * cw - sA);
            A0 = (A + 1) + (A - 1) * cw + sA;
            A1 = -2 * ((A - 1) + (A + 1) * cw);
            A2 = (A + 1) + (A - 1) * cw - sA;
        } else {
            double A = std::pow(10.0, gainDb / 40);
            B0 = 1 + al * A;
            B1 = -2 * cw;
            B2 = 1 - al * A;
            A0 = 1 + al / A;
            A2 = 1 - al / A;
        }
        b0 = B0 / A0;
        b1 = B1 / A0;
        b2 = B2 / A0;
        a1 = A1 / A0;
        a2 = A2 / A0;
        return *this;
    }
    inline double run(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    double operator()() {
        s = s * 1664525u + 1013904223u;
        return s / 4294967296.0;
    }
};

struct Tap {
    double t;
    double az;
    double el;
    double dep[3];
    int order;
    double E[3];
};

struct Path {
    int rotor = 0, order = 0;       // 0 = horn, 1 = drum; number of wall bounces (0 = direct)
    bool trace = false;             // ray-traced tap (tent shape) instead of an image source
    bool fast = false, lin = false; // block-ramped float renderer / linear instead of sinc interpolation
    bool init = false;              // geometry computed at least once (else the start values are copied)
    double sx = 1, sy = 1, sz = 1;  // image source: mirror signs per axis ...
    double ox = 0, oy = 0, oz = 0;  // ... and offsets: image position = s * source + o
    double g = 1;                   // gain from wall reflections (times 1/distance at render time)
    // One-pole low-pass coefficients (see kOf): kOn wall treble loss, kAir air absorption, kDrum extra limit for the
    // drum, kc = min(cabinet, air, walls) at the block end, kb the coefficient actually used by the fast renderer.
    double kOn = 1, kAir = 1, kDrum = 1, kc = 1, kb = 1;
    double lp = 0, zd = 0, zlo = 0;        // filter states: low-pass, drum low-pass, low-band shelf (traced taps)
    double icx = 0, icy = 0, iz = 0;       // image of the cabinet centre for this block
    double jit = 0;                        // tiny fixed extra length (m) that breaks up exact delay coincidences
    double d0 = 0, d1 = 0, g0 = 0, g1 = 0; // distance (m) and gain at the start / end of the sub-block
    double cab0 = 0, cab1 = 0;             // cabinet directivity gain at the start / end
    double hf = 1, hfPrev = 1;             // sharp (pin-point) share now / previous block; the rest goes wide
    double bx0 = 0, bx1 = 0; // direction as a continuous bucket coordinate (0..NB) at the start/end of the sub-block
    std::array<float, 8> gPrev{}, gNext{};              // speaker gains of the sharp part (exact paths)
    float bgA[4] = {1, 1, 1, 1}, bgB[4] = {1, 1, 1, 1}; // radiation band gains at the sub-block start / end
    // Ray-traced taps (tent) and wall patches: fixed distance, departure direction, arrival azimuth/elevation,
    // gain, low-band gain and wall low-pass
    double dist = 0, dep[3] = {0, 0, 0}, azRoom = 0, el = 0, gm = 0, glr = 1, kTrace = 1;
    // Wall patch (diffuse part of a first-order reflection): rectangle on the plane axis = at, spanning [u0,u1] x
    // [v0,v1] along the other two axes. updatePatches() fills dist/dep/azRoom/el/gm from its Lambert-weighted centroid.
    bool patch = false, active = true;
    int axis = 0;
    double at = 0, u0 = 0, u1 = 0, v0 = 0, v1 = 0;
    // Specular share of the sharp fraction: (1 - scatter)^order; 1 for first-order images whose diffuse part goes to
    // the wall patches
    double hfScat = 1;
};

// dir: +1 counter-clockwise seen from above, -1 clockwise. The 122's horn and drum turn in opposite directions.
struct Rotor {
    double ang, hz, tgt;             // internal angle (rad, always increasing), speed and target speed (rev/s)
    double slow, fast;               // speeds for Slow / Fast (rev/s)
    double r, h;                     // Doppler radius (m) and height above the floor (m)
    double upTau, dnTau;             // time constants for speeding up / slowing down (s)
    double lastSin = 0, lastCos = 1; // sine / cosine of the previous sample (wobble term)
    int dir = 1;                     // +1 counter-clockwise seen from above (horn), -1 clockwise (drum)
};

/** Per-sub-block working set of one fast path (higher-order image or traced ray): everything the sample loop
    needs, in float and in one cache line, with the per-sample increments precomputed. */
/** Per-sub-block working copy of a fast path (compact, float, ramps precomputed). */
struct FastPathRT {
    double ds, dds;      // delay in samples and its increment (double: a float would quantise the fractional delay)
    float g, dg;         // gain (path gain x cabinet) and its increment
    float kb;            // low-pass coefficient
    float zd, lp, zlo;   // filter states (copied back to the Path after the block)
    float glr;           // low-band gain (traced taps)
    float hf;            // sharp share; the rest goes to the wide buckets
    float bx, dbx;       // continuous bucket coordinate and its per-sample increment
    float bg[4], dbg[4]; // radiation band gains (ramped)
    int bi, bj;          // bucket pair when the whole sub-block stays within one cell (bi < 0: generic path) ...
    float bf;            // ... and the weight between them
    uint8_t horn, trace;
};

struct Room {
    double Lx = 6, Ly = 8, Lz = 2.8, absorb = 0.25, reverb = 1, tentVolume = 0, tentSurface = 0;
    int order = 2;
    std::string material = "wood", shape = "box";
    bool hasAbsBands = false;
    double absBands[3] = {0, 0, 0};
};

struct Character {
    double cond = 0.5, leak = 0.5, contact = 0.5, motor = 0.4;
    bool gearTune = true, busComp = true;
};
struct Perc {
    bool on = true, third = true, fast = true, soft = false;
};
struct Speakers {
    std::vector<double> az;
    std::vector<int> ch;
    int lfe = -1;
    bool stereo = false;
};

struct RoomInfo {
    double rt60[3] = {0, 0, 0};
    double rc = 0;
    int paths = 0;
};

} // namespace rotary
