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
    // 4b) floor plan snapshot: what the room model renders (direct, images, wall patches), horn level follows the turn
    {
        WheelhouseEngine e(false);
        e.prepare(48000, 256);
        rotary::PathViz v;
        CHECK(!e.readPathViz(v), "no path snapshot before the first block");
        e.handleMessage(msg("{\"type\":\"rotary\",\"v\":\"fast\"}"));
        rms(e, 200);
        double lo = 1e9, hi = 0;
        bool direct[2] = {false, false}, ok = true;
        int patches = 0, images = 0;
        for (int b = 0; b < 400 && ok; b++) {
            rms(e, 1);
            ok = e.readPathViz(v);
            for (int i = 0; i < v.n; i++) {
                const auto& it = v.item[i];
                if (it.patch)
                    patches++;
                else if (it.order > 0)
                    images++;
                else {
                    direct[it.rotor] = true;
                    if (it.rotor == 0) lo = std::min(lo, (double) it.level), hi = std::max(hi, (double) it.level);
                }
            }
        }
        printf("      horn direct level %.3f .. %.3f, %d image and %d patch entries\n", lo, hi, images, patches);
        CHECK(ok && direct[0] && direct[1] && images > 0 && patches > 0,
              "snapshot holds the direct paths of both rotors, image sources and wall patches");
        CHECK(hi > lo * 1.5, "horn direct level changes as the horn turns (radiation model)");
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
    // 9) preset keys: the octave below a manual latches its preset key; Cancel silences it; sets A / B; pedal sustain
    {
        WheelhouseEngine e(false);
        e.prepare(48000, 256);
        auto hold = [&](int note, int ch, int blocks) {
            MidiBuffer mb;
            mb.addEvent(MidiMessage::noteOn(ch, note, (uint8) 100), 0);
            return rms(e, blocks, &mb);
        };
        auto release = [&](int note, int ch) {
            MidiBuffer mb;
            mb.addEvent(MidiMessage::noteOff(ch, note), 0);
            rms(e, 1, &mb);
        };
        const double b = hold(60, 1, 120);
        release(60, 1);
        rms(e, 40);
        MidiBuffer pk;
        pk.addEvent(MidiMessage::noteOn(1, 24, (uint8) 100), 0); // C1 = C of the upper preset octave: Cancel
        rms(e, 4, &pk);
        var ev;
        int presetEv = -1;
        while (e.popUiEvent(ev))
            if (ev.getProperty("type", "") == "presetKey") presetEv = (int) ev["k"];
        const double c = hold(60, 1, 120);
        release(60, 1);
        rms(e, 40);
        CHECK(presetEv == 0, "MIDI note below the upper manual latches its preset key (C = Cancel) and tells the UI");
        CHECK(b > 1e-3 && c < b * 0.02, "Cancel silences the manual");
        e.handleMessage(msg("{\"type\":\"drawbars\",\"man\":0,\"set\":0,\"v\":[0,0,0,0,0,0,0,0,0]}"));
        e.handleMessage(msg("{\"type\":\"presetKey\",\"man\":0,\"k\":9}")); // A: Full Swell
        const double fs = hold(60, 1, 120);
        release(60, 1);
        rms(e, 40);
        e.handleMessage(msg("{\"type\":\"presetKey\",\"man\":0,\"k\":10}")); // A#: set A (all drawbars in)
        const double sa = hold(60, 1, 120);
        release(60, 1);
        rms(e, 40);
        printf("    levels: B %.4f, Full Swell %.4f, set A (all in) %.4f\n", b, fs, sa);
        CHECK(fs > b * 0.2, "factory preset A (Full Swell) sounds although set B is untouched");
        CHECK(sa < b * 0.02, "A# plays drawbar set A (all drawbars in: silent)");
        // percussion only on the upper B key: all drawbars in, percussion on -> only the percussion sounds
        e.handleMessage(msg("{\"type\":\"perc\",\"v\":{\"on\":true,\"third\":true,\"fast\":false,\"soft\":false,"
                            "\"vibU\":false,\"vibL\":false}}"));
        e.handleMessage(msg("{\"type\":\"drawbars\",\"man\":0,\"set\":1,\"v\":[0,0,0,0,0,0,0,0,0]}"));
        e.handleMessage(msg("{\"type\":\"presetKey\",\"man\":0,\"k\":11}"));
        const double pB = hold(60, 1, 40);
        release(60, 1);
        rms(e, 80);
        e.handleMessage(msg("{\"type\":\"presetKey\",\"man\":0,\"k\":10}"));
        const double pA = hold(60, 1, 40);
        release(60, 1);
        rms(e, 80);
        printf("    percussion alone: key B %.4f, key A# %.4f\n", pB, pA);
        CHECK(pB > 5e-3 && pA < pB * 0.1, "percussion sounds only with the upper manual's B preset key");
        // pedal: without sustain it stops at once, with sustain it is still there half a second after release
        auto pedalTail = [&](bool sus) {
            e.handleMessage(
                msg(String("{\"type\":\"organDetail\",\"pedSus\":") + (sus ? "true" : "false") + ",\"pedSusLen\":4}"));
            const double on = hold(36, 3, 100);
            release(36, 3);
            rms(e, 90); // ~0.5 s
            return rms(e, 20) / on;
        };
        const double t0 = pedalTail(false), t1 = pedalTail(true);
        printf("    pedal tail after 0.5 s: B-3 %.4f, sustain %.4f\n", t0, t1);
        CHECK(t0 < 0.05 && t1 > 0.15, "pedal sustain: the released note fades instead of stopping");
        rms(e, 1500); // 8 s: completely gone
        const double tail = rms(e, 20);
        printf("    after 8 s: %.6f (idle reference %.6f)\n", tail, t0);
        CHECK(tail < 0.15 * t0 + 1e-4, "sustained pedal decays to silence (generator noise floor)");
    }
    printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails;
}
