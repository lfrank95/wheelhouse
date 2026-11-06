// AM depth from runout, measured on the organ signal itself (orgBuf), against the same organ with runout disabled
#define private public
#include "../Source/RotaryRoomDSP.h"
#include <cstdio>
using namespace rotary;
static std::vector<float> organ(double cond, int key, bool runout) {
    RotaryRoomDSP d(48000);
    Character c;
    c.cond = cond;
    c.leak = 0;
    c.contact = 0;
    c.motor = 0;
    d.setCharacter(c);
    if (!runout) d.runScale = 0;
    d.setAmp(0, 0, 0);
    d.setPreamp(false, 0.5);
    d.leakHum = 0;
    int db[9] = {0, 0, 8, 0, 0, 0, 0, 0, 0};
    d.setDrawbars(db, 0);
    d.noteOn(key, 0);
    std::vector<std::vector<float>> out(16, std::vector<float>(64));
    float* p[16];
    for (int k = 0; k < 16; k++) p[k] = out[k].data();
    std::vector<float> o;
    for (int b = 0; b < 2250; b++) {
        d.process(p, 16, 64);
        if (b > 750) o.insert(o.end(), d.orgBuf.begin(), d.orgBuf.begin() + 64);
    }
    return o;
}
int main() {
    for (double cond : {0.0, 0.45, 0.85})
        for (int key : {0, 24, 48, 60}) {
            auto a = organ(cond, key, true), b = organ(cond, key, false);
            double sig = 0, err = 0;
            for (size_t i = 0; i < a.size(); i++) {
                sig += b[i] * b[i];
                double e = a[i] - b[i];
                err += e * e;
            }
            // for AM with depth m (sine), residual/signal RMS ratio = m / sqrt(2)
            printf("cond %.2f key %2d: AM depth ~ %.2f %%\n", cond, key, std::sqrt(err / sig) * std::sqrt(2.0) * 100);
        }
}
