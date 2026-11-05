// Regression harness: renders fixed scenarios and compares them against a stored reference.
//
//   g++ -O2 -std=c++17 -pthread -ISource tests/regression.cpp -o regression
//   ./regression record <dir>     # write reference files
//   ./regression compare <dir>    # compare current build against the reference
//
// Pass criterion per scenario: either bit-identical, or SNR >= 90 dB (re-ordered float arithmetic),
// which is far below anything audible. Any larger deviation means the sound changed.
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
using namespace rotary;

struct Scenario { const char* name; std::function<void(RotaryRoomDSP&)> setup; int channels; double seconds; };

static void playChord(RotaryRoomDSP& d) { for (int k : {24, 28, 31, 36}) d.noteOn(k, 0); for (int k : {12, 16, 19}) d.noteOn(k, 1); d.noteOn(0, 2); }

static std::vector<Scenario> scenarios() {
    auto stereo = [](RotaryRoomDSP& d) { Speakers s; s.az = {30, -30}; s.ch = {0, 1}; s.stereo = true; d.setSpeakers(s); };
    auto quad = [](RotaryRoomDSP& d) { Speakers s; s.az = {45, -45, 135, -135}; s.ch = {0, 1, 2, 3}; d.setSpeakers(s); };
    auto s51 = [](RotaryRoomDSP& d) { Speakers s; s.az = {30, -30, 0, 110, -110}; s.ch = {0, 1, 2, 4, 5}; s.lfe = 3; d.setSpeakers(s); };
    auto room = [](RotaryRoomDSP& d, int order) { Room r; r.Lx = 6; r.Ly = 8; r.Lz = 2.8; r.absorb = 0.3; r.order = order; d.setRoom(r); d.setPos(true, 4.2, 6.3, true, 2.6, 2.4, true, 1.5708, true); };
    return {
        {"stereo_eco_slow", [&](RotaryRoomDSP& d) { stereo(d); room(d, 2); d.setAmp(0, 0, 0.5); playChord(d); }, 2, 3.0},
        {"quad_hq_fast", [&](RotaryRoomDSP& d) { quad(d); room(d, 3); d.setAmp(0, 1, 0.5); d.setDrive(0.6); playChord(d); d.setRotary("fast"); }, 4, 3.0},
        {"s51_ultra_147", [&](RotaryRoomDSP& d) { s51(d); room(d, 3); d.setAmp(2, 2, 0.7); d.setDrive(0.8); playChord(d); d.setRotary("fast"); }, 6, 3.0},
        {"mics_stereo_spring", [&](RotaryRoomDSP& d) { stereo(d); room(d, 2); d.setAmp(0, 1, 0.5); MicSetup m; m.on = true; m.roomX = 3; m.roomY = 3.5; d.setMics(m); d.setSpring(true, 0.3, 2.8, 0.5, 0.3, 3); playChord(d); d.setRotary("fast"); }, 2, 3.0},
        {"mics_outs_mono", [&](RotaryRoomDSP& d) { stereo(d); room(d, 2); d.setAmp(1, 1, 0.5); MicSetup m; m.on = true; m.hornStereo = false; m.drumStereo = true; m.roomArr = 2; m.grp[0].out = 2; m.grp[1].out = 3; m.grp[2].out = 5; d.setMics(m); playChord(d); }, 8, 2.0},
        {"perc_release_brake", [&](RotaryRoomDSP& d) { stereo(d); room(d, 2); d.setAmp(0, 1, 0.5); d.setRotary("fast"); d.noteOn(36, 0); d.noteOn(40, 0); }, 2, 2.0},
    };
}

static std::string path(const std::string& dir, const char* name) { return dir + "/" + name + ".f32"; }

static std::vector<float> render(const Scenario& sc) {
    const double fs = 48000; const int N = 128;
    RotaryRoomDSP dsp(fs);
    sc.setup(dsp);
    int blocks = (int) (sc.seconds * fs / N);
    std::vector<std::vector<float>> out(16, std::vector<float>(N)); float* p[16]; for (int c = 0; c < 16; c++) p[c] = out[c].data();
    std::vector<float> all; all.reserve((size_t) blocks * N * sc.channels);
    for (int b = 0; b < blocks; b++) {
        if (std::strcmp(sc.name, "perc_release_brake") == 0) { if (b == blocks / 3) { dsp.noteOff(36, 0); dsp.noteOff(40, 0); dsp.setRotary("stop"); } if (b == blocks / 2) dsp.noteOn(43, 0); }
        dsp.process(p, 16, N);
        for (int c = 0; c < sc.channels; c++) all.insert(all.end(), out[c].begin(), out[c].end());
    }
    return all;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: regression record|compare <dir>\n"); return 2; }
    const bool record = std::strcmp(argv[1], "record") == 0; const std::string dir = argv[2];
    int failed = 0;
    for (auto& sc : scenarios()) {
        auto cur = render(sc);
        auto file = path(dir, sc.name);
        if (record) { FILE* f = std::fopen(file.c_str(), "wb"); if (!f) { std::perror(file.c_str()); return 1; } std::fwrite(cur.data(), sizeof(float), cur.size(), f); std::fclose(f); std::printf("recorded %-20s %zu samples\n", sc.name, cur.size()); continue; }
        FILE* f = std::fopen(file.c_str(), "rb"); if (!f) { std::perror(file.c_str()); return 1; }
        std::vector<float> ref(cur.size()); size_t n = std::fread(ref.data(), sizeof(float), ref.size(), f); std::fclose(f);
        if (n != cur.size()) { std::printf("FAIL %-20s length differs\n", sc.name); failed++; continue; }
        double sig = 0, err = 0, maxd = 0; int nan = 0;
        for (size_t i = 0; i < n; i++) { if (!std::isfinite(cur[i])) nan++; double d = (double) cur[i] - ref[i]; sig += (double) ref[i] * ref[i]; err += d * d; maxd = std::max(maxd, std::fabs(d)); }
        double snr = err > 0 ? 10 * std::log10(sig / err) : 999;
        bool ok = nan == 0 && (err == 0 || snr >= 90);
        std::printf("%s %-20s %s  max diff %.2e%s\n", ok ? "ok  " : "FAIL", sc.name, err == 0 ? "bit-identical    " : (std::string("SNR ") + std::to_string((int) snr) + " dB").c_str(), maxd, nan ? "  NaN!" : "");
        if (!ok) failed++;
    }
    return failed ? 1 : 0;
}
