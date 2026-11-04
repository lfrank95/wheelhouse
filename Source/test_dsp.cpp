#include "LeslieRoomDSP.h"
#include <cstdio>
#include <chrono>
int main() {
    using namespace leslie;
    LeslieRoomDSP dsp(48000);
    Speakers s; s.az = {30, -30, 0, 110, -110}; s.ch = {0, 1, 2, 4, 5}; s.lfe = 3; dsp.setSpeakers(s);
    Room r; r.Lx = 6; r.Ly = 8; r.Lz = 2.8; r.absorb = 0.3; r.material = "wood"; r.order = 2; r.reverb = 1; dsp.setRoom(r);
    dsp.setPos(true, 4.2, 6.3, true, 2.6, 2.4, true, 3.14159265358979 / 2, true);
    dsp.setCabYaw(std::atan2(2.4 - 6.3, 2.6 - 4.2), true);
    const int N = 128, blocks = 4 * 48000 / N;
    std::vector<std::vector<float>> out(8, std::vector<float>(N));
    std::vector<float> all((size_t) blocks * N * 6);
    float* ptrs[8]; for (int c = 0; c < 8; c++) ptrs[c] = out[c].data();
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; b++) {
        if (b == 10) for (int k : {24, 28, 31, 36}) dsp.noteOn(k);
        if (b == 400) dsp.setLeslie("fast");
        if (b == 1100) for (int k : {24, 28, 31, 36}) dsp.noteOff(k);
        dsp.process(ptrs, 8, N);
        for (int i = 0; i < N; i++) for (int c = 0; c < 6; c++) all[((size_t) b * N + i) * 6 + c] = out[c][i];
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("C++: %.1f%% CPU (4 s in %.0f ms)\n", ms / 40, ms);
    FILE* f = fopen("ref.f32", "rb"); if (!f) { printf("keine Referenz\n"); return 0; }
    std::vector<float> ref(all.size()); size_t got = fread(ref.data(), 4, ref.size(), f); fclose(f);
    if (got != ref.size()) printf("Referenz hat %zu statt %zu Werte\n", got, ref.size());
    for (int c = 0; c < 6; c++) {
        double ea = 0, eb = 0, ab = 0, maxd = 0, nan = 0;
        for (size_t i = 0; i < (size_t) blocks * N; i++) { double x = all[i * 6 + c], y = ref[i * 6 + c]; if (!std::isfinite(x)) nan++; ea += x * x; eb += y * y; ab += x * y; maxd = std::max(maxd, std::fabs(x - y)); }
        printf("Kanal %d: RMS C++ %.4f  JS %.4f  Korrelation %.4f  max|diff| %.5f  NaN %.0f\n", c, std::sqrt(ea / (blocks * N)), std::sqrt(eb / (blocks * N)), ab / std::sqrt(ea * eb + 1e-30), maxd, nan);
    }
    return 0;
}
