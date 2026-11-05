// Switching mono <-> stereo and listener <-> mics: largest jump between two consecutive samples (click hunting)
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
        printf("%-28s largest jump %.4f  level %.4f\n", what, mx, std::sqrt(rms / (blocks * N * 2)));
    };
    run(400, "listener");
    m.on = true; dsp.setMics(m); run(60, "-> mics (crossfade)");
    run(200, "mics, horn stereo");
    m.hornStereo = false; dsp.setMics(m); run(60, "-> horn mono");
    run(200, "horn mono");
    m.hornStereo = true; dsp.setMics(m); run(60, "-> horn stereo");
    m.grp[0].width = 0.3; dsp.setMics(m); run(60, "-> width 30 %");
    m.on = false; dsp.setMics(m); run(60, "-> listener");
    run(200, "listener");
}
