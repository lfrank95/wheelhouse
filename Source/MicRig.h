#pragma once
// Wheelhouse – microphone rig: close mics on horn and drum, a room-mic pair, a mixer with individual outputs.
//
// Each mic has
//   * a polar pattern per band (low / mid / high): weight g = a + (1 − a)·cos θ
//     (a = 1 omni, 0.5 cardioid, ≈0.37 supercardioid, 0 figure-8; the rear of the figure-8 picks up in opposite polarity),
//   * an on-axis frequency response (approximated from the data sheet as a filter chain),
//   * a proximity effect (pressure gradient: bass boost that grows as the distance shrinks),
//   * a wind sensitivity (the horn blows air; ribbon and condenser mics react more strongly).
// The names "type 57" etc. refer to the construction, not the brand.

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace leslie {

struct MicEq { char type; double f, q, db; };   // 'h' highpass, 'l' lowpass, 'p' peak/bell, 's' low shelf
struct MicType { const char* name; double a[3]; double prox, wind, diffuse; int nEq; MicEq eq[5]; };

static const MicType MIC_TYPES[] = {
    // 0 type 57: cardioid, presence boost around 5 kHz, lean bass at a distance, noticeably duller off-axis
    {"57", {0.55, 0.50, 0.32}, 1.0, 0.15, 0.58, 4, {{'h', 90, 0.7, 0}, {'p', 5500, 1.3, 5}, {'p', 2200, 1.0, 1.5}, {'l', 14000, 0.7, 0}}},
    // 1 type 421: cardioid, full-bodied, broad presence around 4 kHz
    {"421", {0.55, 0.50, 0.38}, 0.9, 0.15, 0.58, 4, {{'h', 45, 0.7, 0}, {'p', 4000, 0.8, 3.5}, {'p', 250, 1.0, 1.5}, {'l', 16000, 0.7, 0}}},
    // 2 type D112: bass mic, bump around 90 Hz, mid scoop, boost around 4 kHz
    {"D112", {0.60, 0.50, 0.35}, 1.0, 0.12, 0.58, 4, {{'p', 90, 0.9, 5}, {'p', 650, 1.0, -5}, {'p', 4200, 1.3, 6}, {'l', 11000, 0.7, 0}}},
    // 3 type RE20: supercardioid, hardly any proximity effect (Variable-D), neutral, gentle high-frequency roll-off
    {"RE20", {0.42, 0.38, 0.36}, 0.15, 0.10, 0.52, 3, {{'h', 40, 0.7, 0}, {'p', 2500, 0.7, 1}, {'l', 12000, 0.6, 0}}},
    // 4 type 87: large-diaphragm condenser, cardioid / omni / figure-8, slight boost around 9 kHz
    {"87", {0.50, 0.50, 0.42}, 1.0, 0.45, 0.58, 3, {{'h', 30, 0.7, 0}, {'p', 9000, 0.9, 2}, {'l', 19000, 0.7, 0}}},
    // 5 type KM 84: small-diaphragm cardioid, very even off-axis as well
    {"KM84", {0.50, 0.50, 0.47}, 1.0, 0.40, 0.58, 2, {{'h', 35, 0.7, 0}, {'p', 12000, 0.8, 1}}},
    // 6 type 121: ribbon, figure-8, dark high-frequency roll-off, strong proximity effect, very wind-sensitive
    {"121", {0.0, 0.0, 0.0}, 1.3, 1.0, 0.58, 3, {{'h', 25, 0.7, 0}, {'l', 9500, 0.6, 0}, {'p', 3000, 0.8, -1}}},
};
constexpr int NUM_MIC_TYPES = 7;

/** Mixer groups: 0 horn, 1 drum, 2 room. Each group is stereo (two mics, balance + width) or mono (one mic, pan). */
constexpr int MIC_GRP = 3;
constexpr int MIC_CH = 6;   // level meters: L and R per group

struct MicSetup {
    bool on = false;   // mode: false = listener in the room, true = microphone rig
    // horn: stereo = two mics placed around the centre, mono = one in the centre
    bool hornStereo = true; double hornAngle = 180, hornCenter = 0, hornDist = 0.15, hornHeight = 0.0; int hornType = 0; bool windscreen = false;
    bool drumStereo = false; double drumAngle = 180, drumCenter = 0, drumDist = 0.15; int drumType = 2;
    // room pair: 0 AB, 1 XY, 2 ORTF
    int roomArr = 0; double roomX = 3, roomY = 3, roomZ = 2.0, roomYaw = 1.5707963, roomSpacing = 0.6; int roomType = 4, roomPattern = 1;
    int hornPattern = 0, drumPattern = 0;   // type 87 only: 0 cardioid, 1 omni, 2 figure-8
    int roomOrder = 2;
    struct Grp { double gainDb = 0, pan = 0, width = 1, delayMs = 0; bool pol = false, mute = false, solo = false; int out = -1; };   // out: −1 front, −2 rear, ≥0 individual output (stereo: out and out+1)
    Grp grp[MIC_GRP];
    MicSetup() { grp[2].gainDb = -6; }
};

struct MicPath {
    int rotor = 0, order = 0; bool exact = false;
    double sx = 1, sy = 1, sz = 1, ox = 0, oy = 0, oz = 0, g = 1, jit = 0;
    double d0 = 0, d1 = 0, g0 = 0, g1 = 0, kb = 1, kc = 1, kDrum = 1, kRefl = 1, cab = 1;
    double band[3] = {1, 1, 1};
    double lp = 0, zlo = 0, zhi = 0, zd = 0;
    bool init = false;
};

struct MicRt {
    bool active = false, close = false; int rotor = 0, type = 0, chan = 0;
    double x = 0, y = 0, z = 1, ax = 1, ay = 0, az = 0;     // position and facing direction (room coordinates)
    double a[3] = {0.5, 0.5, 0.5}, wind = 0, diffuse = 0.58, trim = 1, dMean = 1;
    Biquad eq[6]; int nEq = 0;
    std::vector<MicPath> paths;
    Biquad prox; bool hasProx = false;                     // proximity effect (low shelf, distance-dependent)
    double phi = 0, phiT = 0, R = 1, RT = 1;                 // close mic: angle to the cabinet front, distance to the cabinet centre (current / target, smoothed)
    bool want = false, inited = false;                                       // required by the current setup (fades in and out smoothly)
    double windLp = 0, windLp2 = 0, windAmt = 0, windC = 1, windS = 0;
    std::array<float, 16> fdnRow{}; std::array<int, 16> fdnOff{};
};

} // namespace leslie
