// Engine (shared by app and plugin): settings recorded and replayed, project state round trip, host parameters
// applied on the audio thread, UI changes reported to the host, host MIDI with sample positions, offline switch.
// Build (needs the JUCE modules): see README, "Tests".
#include "../Source/Engine.h"
#include <cstdio>
using namespace juce;

static var msg(const String& json) {
    return JSON::parse(json);
}
static double rms(WheelhouseEngine& e, int blocks, const MidiBuffer* midi = nullptr) {
    std::vector<std::vector<float>> o(16, std::vector<float>(256));
    float* p[16];
    for (int i = 0; i < 16; i++) p[i] = o[i].data();
    double s = 0;
    long n = 0;
    for (int b = 0; b < blocks; b++) {
        e.render(p, 256, b == 0 ? midi : nullptr);
        for (int i = 0; i < 256; i++) {
            s += o[0][i] * o[0][i];
            n++;
        }
    }
    return std::sqrt(s / n);
}
int fails = 0;
#define CHECK(c, t)                                                                                                    \
    do {                                                                                                               \
        bool ok_ = (c);                                                                                                \
        printf("%s  %s\n", ok_ ? "ok  " : "FAIL", t);                                                                  \
        if (!ok_) fails++;                                                                                             \
    } while (0)

int main() {
    // 1) host parameters drive the DSP; with all drawbars at 0 a held note is silent
    {
        WheelhouseEngine e(true);
        std::vector<std::atomic<float>> hv(WheelhouseEngine::params().size());
        for (size_t i = 0; i < hv.size(); i++) hv[i] = WheelhouseEngine::params()[i].def;
        e.hostParam = hv.data();
        e.prepare(48000, 256);
        MidiBuffer mb;
        mb.addEvent(MidiMessage::noteOn(1, 60, (uint8) 100), 10);
        double on = rms(e, 200, &mb);
        for (int i = 0; i < 9; i++) hv[(size_t) i] = 0; // pull all upper drawbars in
        rms(e, 50);
        double off = rms(e, 100);
        CHECK(on > 1e-3 && off < on * 0.05, "host drawbar parameters reach the DSP (note silent with all drawbars in)");
        var ev;
        int params = 0;
        while (e.popUiEvent(ev))
            if (ev.getProperty("type", "") == "param") params++;
        CHECK(params == 3, "automation produces one UI event per changed parameter (3 of 9 were not 0)");
    }
    // 2) UI change -> host is told
    {
        WheelhouseEngine e(true);
        std::vector<std::pair<int, float>> told;
        e.onUiParam = [&](int i, float v) {
            told.push_back({i, v});
        };
        e.handleMessage(msg("{\"type\":\"drawbars\",\"man\":0,\"v\":[8,8,8,8,0,0,0,0,0]}"));
        e.handleMessage(msg("{\"type\":\"rotary\",\"v\":\"fast\"}"));
        bool db3 = false, les = false;
        for (auto& t : told) {
            if (t.first == 3 && t.second == 8) db3 = true;
            if (t.first == 20 && t.second == 2) les = true;
        }
        CHECK(db3 && les, "UI drawbar and Rotary changes are reported as parameter changes");
    }
    // 2b) projects saved before the rename ("leslie" in message types and keys) still load
    {
        WheelhouseEngine e(true);
        std::vector<std::pair<int, float>> told;
        e.onUiParam = [&](int i, float v) {
            told.push_back({i, v});
        };
        e.handleMessage(msg("{\"type\":\"leslie\",\"v\":\"fast\"}"));
        e.handleMessage(msg("{\"type\":\"pos\",\"lesliePos\":{\"x\":2,\"y\":3}}"));
        bool les = false;
        for (auto& t : told)
            if (t.first == 20 && t.second == 2) les = true;
        var st = e.saveState();
        bool renamed = false, pos = false, legacy = false;
        if (auto* m = st.getProperty("messages", var()).getArray())
            for (auto& x : *m) {
                const String t = x.getProperty("type", "").toString();
                if (t == "rotary") renamed = true;
                if (t == "leslie" || x.hasProperty("lesliePos")) legacy = true;
                if (t == "pos" && x.hasProperty("rotaryPos")) pos = true;
            }
        CHECK(les && renamed && pos && !legacy,
              "old 'leslie' messages are translated to 'rotary' and stored under the new names");
        CHECK(WheelhouseEngine::params()[20].id == std::string("leslie"),
              "host parameter ID unchanged (existing automation keeps working)");
    }
    // 3) project state round trip: settings recorded, partial room updates merged, replayed into a fresh engine
    {
        WheelhouseEngine a(false);
        a.handleMessage(msg("{\"type\":\"room\",\"v\":{\"Lx\":9,\"Ly\":12}}"));
        a.handleMessage(msg("{\"type\":\"room\",\"v\":{\"order\":3}}"));
        a.handleMessage(msg("{\"type\":\"drive\",\"v\":0.9}"));
        a.handleMessage(msg("{\"type\":\"noteOn\",\"key\":20,\"man\":0}"));
        a.uiState = "{\"x\":1}";
        var st = a.saveState();
        auto* m = st.getProperty("messages", var()).getArray();
        bool room = false, note = false;
        if (m)
            for (auto& x : *m) {
                if (x.getProperty("type", "") == "room")
                    room = (double) x["v"]["Lx"] == 9 && (int) x["v"]["order"] == 3;
                if (x.getProperty("type", "") == "noteOn") note = true;
            }
        CHECK(room && !note, "state keeps merged room settings and no transient notes");
        WheelhouseEngine b(false);
        b.restoreState(JSON::parse(JSON::toString(st)));
        CHECK(b.uiState == a.uiState, "UI state restored");
        CHECK(JSON::toString(b.saveState()) == JSON::toString(st), "replayed engine records the same state");
    }
    // 4) a sample-rate change rebuilds the core and replays the settings
    {
        WheelhouseEngine e(false), ref(false);
        e.handleMessage(msg("{\"type\":\"drawbars\",\"man\":0,\"v\":[0,0,0,0,0,0,0,0,0]}"));
        bool rebuilt = e.prepare(96000, 256);
        ref.prepare(96000, 256);
        e.handleMessage(msg("{\"type\":\"noteOn\",\"key\":24,\"man\":0}"));
        ref.handleMessage(msg("{\"type\":\"noteOn\",\"key\":24,\"man\":0}"));
        double a = rms(e, 300), r = rms(ref, 300);
        printf("      rebuilt with drawbars in: %.5f, default drawbars: %.5f\n", a, r);
        CHECK(rebuilt && a < r * 0.3,
              "after a rebuild the replayed drawbars still apply (only percussion and noise left)");
    }
    // 5) offline (bounce) switch and back
    {
        WheelhouseEngine e(true);
        std::vector<std::atomic<float>> hv(WheelhouseEngine::params().size());
        for (size_t i = 0; i < hv.size(); i++) hv[i] = WheelhouseEngine::params()[i].def;
        e.hostParam = hv.data();
        e.handleMessage(msg("{\"type\":\"room\",\"v\":{\"order\":2}}"));
        e.setNonRealtime(true);
        double a = rms(e, 50);
        e.setNonRealtime(false);
        e.restoreRealtimeQuality();
        double b = rms(e, 50);
        CHECK(std::isfinite(a) && std::isfinite(b), "bounce quality switch runs");
    }
    // 6) keyboard split and octave shift; a note-off goes where its note-on went
    {
        WheelhouseEngine e(false);
        e.handleMessage(msg("{\"type\":\"keyMap\",\"mode\":2,\"hi\":60,\"lo\":48,\"oct\":[0,1,-1]}"));
        auto on = [&](int note) {
            return e.midiFromDevice(MidiMessage::noteOn(1, note, (uint8) 100));
        };
        var a = on(72), b = on(50), c = on(40);
        CHECK((int) a["man"] == 0 && (int) a["key"] == 36, "split: C5 plays the upper manual");
        CHECK((int) b["man"] == 1 && (int) b["key"] == 26, "split: D3 plays the lower manual, one octave up");
        CHECK((int) c["man"] == 2 && (int) c["key"] == 4, "split: E2 plays the pedal, one octave down");
        e.handleMessage(msg("{\"type\":\"keyMap\",\"mode\":0,\"hi\":60,\"lo\":48,\"oct\":[0,0,0]}"));
        var off = e.midiFromDevice(MidiMessage::noteOff(1, 50));
        CHECK((int) off["man"] == 1 && (int) off["key"] == 26, "note-off after a split change releases the same key");
        var d = on(50);
        CHECK((int) d["man"] == 0 && (int) d["key"] == 14, "split off: D3 plays the upper manual again");
    }
    printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails;
}
