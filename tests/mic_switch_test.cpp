// Umschalten Mono ↔ Stereo und Hörer ↔ Mikros: größter Sprung zwischen zwei Samples (Knackser-Suche)
#include "../Source/LeslieRoomDSP.h"
#include <cstdio>
using namespace leslie;
int main() {
    LeslieRoomDSP dsp(48000);
    Speakers s; s.az = {30, -30}; s.ch = {0, 1}; dsp.setSpeakers(s);
    Room r; r.order = 2; dsp.setRoom(r);
    dsp.setPos(true, 4.2, 6.3, true, 2.6, 2.4, true, 1.5708, true);
    MicSetup m; m.on = false; m.roomX = 3; m.roomY = 3.5; dsp.setMics(m);
    for (int k : {24, 28, 31}) dsp.noteOn(k, 0);
    const int N = 128; std::vector<std::vector<float>> out(2, std::vector<float>(N)); float* p[2] = {out[0].data(), out[1].data()};
    float last[2] = {0, 0};
    auto run = [&](int blocks, const char* what) {
        double mx = 0, rms = 0;
        for (int b = 0; b < blocks; b++) { dsp.process(p, 2, N); for (int c = 0; c < 2; c++) for (int i = 0; i < N; i++) { double d = std::fabs(out[c][i] - last[c]); last[c] = out[c][i]; mx = std::max(mx, d); rms += out[c][i] * out[c][i]; } }
        printf("%-28s größter Sprung %.4f  Pegel %.4f\n", what, mx, std::sqrt(rms / (blocks * N * 2)));
    };
    run(400, "Hörer");
    m.on = true; dsp.setMics(m); run(60, "→ Mikros (Überblendung)");
    run(200, "Mikros, Horn stereo");
    m.hornStereo = false; dsp.setMics(m); run(60, "→ Horn mono");
    run(200, "Horn mono");
    m.hornStereo = true; dsp.setMics(m); run(60, "→ Horn stereo");
    m.grp[0].width = 0.3; dsp.setMics(m); run(60, "→ Breite 30 %");
    m.on = false; dsp.setMics(m); run(60, "→ Hörer");
    run(200, "Hörer");
}
