// Cabinet radiation tables (CabinetDirectivity.h) and their use in the engine.
//   g++ -O2 -std=c++17 -pthread -ISource tests/cabinet_test.cpp -o cabinet_test && ./cabinet_test
// 1. CabinetDir: grid values reproduced, continuous across the angle / azimuth wrap, elevation clamped.
// 2. Engine: the same organ heard 2 m in front of the cabinet and 2 m behind it, in a dead room (no reflections):
//    the back is quieter, more so in the horn's treble than in the drum's bass; turning the cabinet round swaps it.
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace rotary;

static int fails = 0;
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

// band energies (horn-ish > 1.5 kHz, drum-ish < 300 Hz) of the stereo sum for a listener position
static void listen(double lx, double ly, double hx, double hy, double yaw, double cabYaw, double& lo, double& hi) {
    RotaryRoomDSP dsp(48000);
    Speakers s;
    s.az = {30, -30};
    s.ch = {0, 1};
    s.lfe = -1;
    s.stereo = true;
    dsp.setSpeakers(s);
    Room r;
    r.Lx = 40;
    r.Ly = 40;
    r.Lz = 20;
    r.absorb = 0.99;
    r.order = 0;
    r.reverb = 0;
    dsp.setRoom(r);
    dsp.setPos(true, lx, ly, true, hx, hy, true, yaw, true);
    dsp.setCabYaw(cabYaw, true);
    dsp.setRotary("stop");
    int pd[9] = {8, 8, 8, 8, 8, 8, 8, 8, 8};
    dsp.setDrawbars(pd, 0);
    const int N = 128, blocks = 3 * 48000 / N;
    std::vector<std::vector<float>> out(2, std::vector<float>(N));
    float* p[2] = {out[0].data(), out[1].data()};
    // crude band split: one-pole high / low pass on the mono sum
    double lpS = 0, hpS = 0;
    const double kl = 1 - std::exp(-2 * 3.14159 * 300 / 48000.0), kh = 1 - std::exp(-2 * 3.14159 * 1500 / 48000.0);
    lo = hi = 0;
    for (int b = 0; b < blocks; b++) {
        if (b == 5)
            for (int k : {24, 36, 43, 48, 55}) dsp.noteOn(k, 0);
        dsp.process(p, 2, N);
        if (b < blocks / 3) continue; // let it settle
        for (int n = 0; n < N; n++) {
            const double x = 0.5 * (out[0][n] + out[1][n]);
            lpS += kl * (x - lpS);
            hpS += kh * (x - hpS);
            lo += lpS * lpS;
            hi += (x - hpS) * (x - hpS);
        }
    }
}

int main() {
    // 1. table lookup
    CabinetDir d;
    d.build(cabdir::HORN_DEFLECTOR, cabdir::CAB_NA, cabdir::CAB_NZ, cabdir::CAB_NE, 4);
    float a[4], b[4];
    const int ia = 5, iz = 11, ie = 6;
    d.at((float) ia, (float) iz, (float) ie, a);
    const short* row = &cabdir::HORN_DEFLECTOR[((ia * cabdir::CAB_NZ + iz) * cabdir::CAB_NE + ie) * 4];
    bool grid = true;
    for (int k = 0; k < 4; k++) grid &= std::abs(20 * std::log10(a[k]) - row[k] / 10.0) < 0.01;
    check(grid, "grid point reproduced");
    d.at((float) cabdir::CAB_NA - 0.001f, 3.f, 6.f, a);
    d.at(0.001f, 3.f, 6.f, b);
    check(std::abs(a[1] - b[1]) < 0.02f * std::max(a[1], b[1]) + 1e-6f, "continuous across the rotor-angle wrap");
    d.at(7.f, (float) cabdir::CAB_NZ - 0.001f, 6.f, a);
    d.at(7.f, 0.001f, 6.f, b);
    check(std::abs(a[1] - b[1]) < 0.02f * std::max(a[1], b[1]) + 1e-6f, "continuous across the azimuth wrap");
    d.at(7.f, 3.f, 99.f, a);
    d.at(7.f, 3.f, (float) (cabdir::CAB_NE - 1), b);
    check(a[2] == b[2], "elevation clamped");
    // mean power over everything is of order 1 (normalised to the previous model)
    double m = 0;
    for (size_t i = 0; i < d.g.size(); i += 4) m += d.g[i + 1] * d.g[i + 1];
    m /= d.g.size() / 4;
    check(m > 0.05 && m < 20, "mean band power of order one");

    // 2. engine: cabinet at (20, 20) facing +y (cabYaw = pi/2); listener in front (20, 22) and behind (20, 18)
    double loF, hiF, loB, hiB, loT, hiT;
    listen(20, 20, 20, 22, -1.5708, 1.5708, loF, hiF);
    listen(20, 20, 20, 18, 1.5708, 1.5708, loB, hiB);
    listen(20, 20, 20, 18, 1.5708, -1.5708, loT, hiT); // cabinet turned round: the listener is in front again
    const double dHi = 10 * std::log10(hiB / hiF), dLo = 10 * std::log10(loB / loF);
    printf("behind vs in front: treble %+.1f dB, bass %+.1f dB; cabinet turned: treble %+.1f dB\n", dHi, dLo,
           10 * std::log10(hiT / hiF));
    check(dHi < -3, "treble quieter behind the cabinet");
    check(dLo > dHi, "bass shadowed less than treble");
    check(std::abs(10 * std::log10(hiT / hiF)) < 1.5, "turning the cabinet round restores the front sound");
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
