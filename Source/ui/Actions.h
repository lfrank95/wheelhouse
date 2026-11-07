#pragma once
// Wheelhouse UI - actions shared by the header, the play surface and the cards: the senders per DSP section,
// positions in the room, room and instrument presets, output format and channel mapping.
#include "Controls.h"
#include <cmath>

namespace wh {

constexpr double PI = 3.14159265358979323846;

inline String nf(double x, int d = 1) {
    return String(x, d);
}
inline String pct(double v) {
    return String(juce::roundToInt(v * 100)) + "%";
}
inline String offPct(double v) {
    return v < 0.005 ? String("off") : pct(v);
}
inline String dbText(double v) {
    return String(v > 0 ? L"+" : v < 0 ? L"−" : L"") + nf(std::abs(v), 1) + " dB";
}
inline double clampd(double v, double a, double b) {
    return juce::jlimit(a, b, v);
}
inline int wrapDeg(double rad, double offset) {
    int d = juce::roundToInt((rad - offset) * 180 / PI);
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    return d;
}
/** Element i of a JSON array, or void (var's operator[] must not be used out of range). */
inline var at(const var& arr, int i) {
    if (auto* a = arr.getArray(); a && i >= 0 && i < a->size()) return a->getReference(i);
    return {};
}
inline juce::Array<var> intArray(const var& v) {
    juce::Array<var> out;
    if (auto* a = v.getArray())
        for (auto& x : *a) out.add((int) x);
    return out;
}

class Actions {
public:
    explicit Actions(Ctx& c) : ctx(c), st(c.st) {}
    Ctx& ctx;
    UiState& st;

    // ---------------- Senders per DSP section ----------------
    // Two drawbar sets per manual, as on the B-3: "db.upper" / "db.lower" = set B (the right one, the playing set,
    // with percussion - the one set older saves knew), "db.upperA" / "db.lowerA" = set A. The pedal has one.
    static String dbPath(int man, int set) {
        if (man == 2) return "db.pedal";
        return String(man == 0 ? "db.upper" : "db.lower") + (set == 0 ? "A" : "");
    }
    static String manName(int man) { return man == 0 ? "upper" : "lower"; }
    /** The set the manual's drawbars show and move: the last one chosen with A# / B. */
    int editSet(int man) const { return man == 2 ? 1 : (int) st.num("dbEdit." + manName(man)); }
    int presetKey(int man) const { return man == 2 ? 11 : (int) st.num("presetKey." + manName(man)); }
    String dbEditPath(int man) const { return dbPath(man, editSet(man)); }
    void sendDbSet(int man, int set) {
        if (man == 2) {
            ctx.send(Ctx::obj({{"type", "drawbars"}, {"man", 2}, {"v", st.get("db.pedal")}}));
            return;
        }
        ctx.send(Ctx::obj({{"type", "drawbars"}, {"man", man}, {"set", set}, {"v", st.get(dbPath(man, set))}}));
    }
    /** The drawbars of a manual changed (the set they show). */
    void sendDb(int man) { sendDbSet(man, editSet(man)); }
    void sendPresetKey(int man) { ctx.send(Ctx::obj({{"type", "presetKey"}, {"man", man}, {"k", presetKey(man)}})); }
    /** Presses one of the reverse-coloured preset keys: 0 cancel, 1..9 factory presets, 10 = A#, 11 = B. */
    void setPresetKey(int man, int k, bool send = true) {
        if (man < 0 || man > 1) return;
        k = juce::jlimit(0, 11, k);
        st.set("presetKey." + manName(man), k);
        if (k >= 10) st.set("dbEdit." + manName(man), k - 10);
        if (send) sendPresetKey(man);
        ctx.syncAll();
    }
    void sendPerc() { ctx.sendV("perc", st.get("perc")); }
    void sendChr() { ctx.sendV("character", st.get("chr")); }
    void sendOrgan() { ctx.sendSpread("organDetail", "organ"); }
    void sendRotor(int r) { ctx.sendSpread("rotor", "rotor." + String(r), {{"r", r}}); }
    void sendAmp() { ctx.sendSpread("amp", "amp"); }
    void sendCab() { ctx.sendSpread("cabDetail", "cab"); }
    void sendOffline() { ctx.sendV("offlineMax", st.get("offlineMax")); }
    void sendOut() { ctx.sendSpread("output", "out"); }
    void sendMidi() { ctx.sendSpread("midiChannels", "midi"); }
    void sendKeyMap() {
        ctx.send(Ctx::obj({{"type", "keyMap"},
                           {"mode", st.get("split.mode")},
                           {"hi", st.get("split.hi")},
                           {"lo", st.get("split.lo")},
                           {"oct", st.get("split.oct")}}));
    }
    void sendPre() { ctx.sendSpread("preamp", "pre"); }
    void sendMains() {
        ctx.send(Ctx::obj({{"type", "mains"}, {"hz", st.get("mains")}}));
        ctx.sendV("velocityClick", st.get("velClick"));
    }
    void sendSpring() { ctx.sendSpread("spring", "spring"); }
    void sendPrecision() { ctx.send(Ctx::obj({{"type", "precision"}, {"exact", st.get("exact")}})); }
    void sendThreads() { ctx.send(Ctx::obj({{"type", "threads"}, {"n", st.get("threads")}})); }
    void sendMics() { ctx.sendV("mics", st.get("mics")); }
    void sendRoomAll() { ctx.sendV("room", st.get("room")); }

    // ---------------- Rotary speed ----------------
    void setRotary(String v) {
        if (v == "stop" && st.str("halfMoon") != "stop") v = "slow";
        st.set("rotary", v);
        ctx.sendV("rotary", v);
        ctx.syncAll();
    }
    void toggleRotary() { setRotary(st.str("rotary") == "fast" ? "slow" : "fast"); }

    // ---------------- Positions ----------------
    void clampPos() {
        const double Lx = st.num("room.Lx"), Ly = st.num("room.Ly");
        auto c = [](double v, double hi) {
            return clampd(v, 0.35, hi - 0.35);
        };
        st.set("rotaryPos.x", c(st.num("rotaryPos.x"), Lx));
        st.set("rotaryPos.y", c(st.num("rotaryPos.y"), Ly));
        st.set("listener.x", c(st.num("listener.x"), Lx));
        st.set("listener.y", c(st.num("listener.y"), Ly));
    }
    void sendPos(bool jump = false) {
        ctx.send(Ctx::obj(
            {{"type", "pos"}, {"rotaryPos", st.get("rotaryPos")}, {"listener", st.get("listener")}, {"jump", jump}}));
    }
    int yawDeg() const { return wrapDeg(st.num("listener.yaw"), PI / 2); }
    void setYaw(double rad, bool jump = false) {
        st.set("listener.yaw", rad);
        sendPos(jump);
        ctx.syncAll();
    }
    int cabDeg() const { return wrapDeg(st.num("cabYaw"), PI / 2); }
    void setCabYaw(double rad, bool jump = false) {
        st.set("cabYaw", rad);
        ctx.send(Ctx::obj({{"type", "cabYaw"}, {"v", rad}, {"jump", jump}}));
        ctx.syncAll();
    }
    void faceListener() {
        setCabYaw(
            std::atan2(st.num("listener.y") - st.num("rotaryPos.y"), st.num("listener.x") - st.num("rotaryPos.x")));
    }
    void backToWall() {
        const double x = st.num("rotaryPos.x"), y = st.num("rotaryPos.y"), Lx = st.num("room.Lx"),
                     Ly = st.num("room.Ly");
        std::vector<std::pair<double, double>> d{{x, PI}, {Lx - x, 0}, {y, -PI / 2}, {Ly - y, PI / 2}};
        std::sort(d.begin(), d.end());
        setCabYaw(d[0].second + PI);
    }
    void lookAtRotary() {
        setYaw(std::atan2(st.num("rotaryPos.y") - st.num("listener.y"), st.num("rotaryPos.x") - st.num("listener.x")));
    }

    // ---------------- Room presets ----------------
    int matchRoom() const {
        auto* rooms = tables()["rooms"].getArray();
        for (int i = 0; i < rooms->size(); i++) {
            const var r = rooms->getReference(i);
            bool same = true;
            for (auto k : {"Lx", "Ly", "Lz", "absorb", "reverb"})
                same = same && std::abs((double) r[k] - st.num("room." + String(k))) < 1e-9;
            if (same && r["material"].toString() == st.str("room.material")) return i;
        }
        return -1;
    }
    void loadRoom(int i) {
        auto* rooms = tables()["rooms"].getArray();
        if (i < 0 || i >= rooms->size()) return;
        const var r = rooms->getReference(i);
        const double fx = st.num("rotaryPos.x") / st.num("room.Lx"), fy = st.num("rotaryPos.y") / st.num("room.Ly"),
                     gx = st.num("listener.x") / st.num("room.Lx");
        for (auto k : {"Lx", "Ly", "Lz", "absorb", "reverb", "material"}) st.set("room." + String(k), r[k]);
        const double Lx = r["Lx"], Ly = r["Ly"];
        st.set("rotaryPos.x", fx * Lx);
        st.set("rotaryPos.y", std::min(Ly - 1, fy * Ly));
        st.set("listener.x", gx * Lx);
        st.set("listener.y", std::max(0.6, st.num("rotaryPos.y") - std::min(4 + Ly * 0.15, Ly * 0.6)));
        clampPos();
        sendRoomAll();
        sendPos(true);
        if (st.flag("mics.placed")) {
            st.set("mics.roomX", clampd(st.num("mics.roomX"), 0.5, Lx - 0.5));
            st.set("mics.roomY", clampd(st.num("mics.roomY"), 0.5, Ly - 0.5));
            sendMics();
        }
        ctx.syncAll();
    }
    String roomLine() const {
        static const std::map<String, String> shortName{
            {"wood", "Wood"}, {"concrete", "Concrete"}, {"stone", "Stone"}, {"glass", "Glass"}};
        const String mat = st.str("room.material");
        auto it = shortName.find(mat);
        String s = nf(st.num("room.Lx")) + L" × " + nf(st.num("room.Ly")) + L" × " + nf(st.num("room.Lz")) + L" m · " +
                   (it != shortName.end() ? it->second : mat);
        if (st.roomInfo.isObject()) s += L" · RT60 " + nf((double) at(st.roomInfo["rt60"], 3)) + " s";
        return s;
    }

    // ---------------- Mics ----------------
    void placeRoomPair() {
        const double f = st.num("cabYaw"), Lx = st.num("room.Lx"), Ly = st.num("room.Ly");
        const double d = std::min(3.0, std::max(1.5, std::min(Lx, Ly) * 0.35));
        const double x = clampd(st.num("rotaryPos.x") + std::cos(f) * d, 0.5, Lx - 0.5);
        const double y = clampd(st.num("rotaryPos.y") + std::sin(f) * d, 0.5, Ly - 0.5);
        st.set("mics.roomX", x);
        st.set("mics.roomY", y);
        st.set("mics.roomYaw", std::atan2(st.num("rotaryPos.y") - y, st.num("rotaryPos.x") - x));
        st.set("mics.placed", true);
    }
    void setMicsOn(bool v) {
        st.set("mics.on", v);
        if (v && !st.flag("mics.placed")) placeRoomPair();
        sendMics();
        ctx.syncAll();
    }

    // ---------------- Instruments and registrations ----------------
    int curInst = -1;
    String instSig;
    String instState() const {
        juce::Array<var> a;
        for (auto k : {"chr", "organ", "click", "db", "perc", "vib", "pre", "amp", "drive", "balance", "rotor", "brake",
                       "cab", "halfMoon", "spring", "presetKey"})
            a.add(st.get(k));
        return juce::JSON::toString(var(a), true);
    }
    static void assignInto(var& dst, const var& src) {
        if (auto* s = src.getDynamicObject())
            for (auto& p : s->getProperties()) dst.getDynamicObject()->setProperty(p.name, deepCopy(p.value));
    }
    void applyInstrument(int i) {
        auto* list = tables()["instruments"].getArray();
        if (i < 0 || i >= list->size()) return;
        const var P = deepCopy(list->getReference(i));
        for (auto k : {"chr", "organ", "pre", "amp", "cab", "spring"}) {
            var dst = st.get(k);
            assignInto(dst, P[k]);
        }
        const var keepA[2] = {deepCopy(st.get("db.upperA")), deepCopy(st.get("db.lowerA"))};
        for (auto k : {"click", "db", "perc", "vib", "drive", "balance", "rotor", "brake", "halfMoon"})
            st.set(k, deepCopy(P[k]));
        // an instrument's registration is the playing set B; set A stays as it was
        st.set("db.upperA", keepA[0]);
        st.set("db.lowerA", keepA[1]);
        for (int m = 0; m < 2; m++) {
            st.set("presetKey." + manName(m), 11);
            st.set("dbEdit." + manName(m), 1);
        }
        if (st.str("halfMoon") != "stop" && st.str("rotary") == "stop") st.set("rotary", "slow");
        sendChr();
        sendOrgan();
        ctx.sendV("click", st.get("click"));
        for (int m = 0; m < 3; m++) sendDb(m);
        sendPresetKey(0);
        sendPresetKey(1);
        sendPerc();
        ctx.sendV("vib", st.get("vib"));
        sendPre();
        sendAmp();
        ctx.sendV("drive", st.get("drive"));
        ctx.sendV("balance", st.get("balance"));
        sendRotor(0);
        sendRotor(1);
        ctx.sendV("brake", st.get("brake"));
        sendCab();
        sendSpring();
        ctx.sendV("rotary", st.get("rotary"));
        curInst = i;
        instSig = instState();
        ctx.syncAll();
    }
    void applyRegistration(int i) {
        auto* list = tables()["presets"].getArray();
        if (i < 0 || i >= list->size()) return;
        const var p = list->getReference(i);
        st.set("db.upper", deepCopy(p["u"]));
        st.set("db.lower", deepCopy(p["l"]));
        st.set("db.pedal", deepCopy(p["p"]));
        st.set("perc", deepCopy(p["perc"]));
        st.set("vib", p["vib"]);
        st.set("drive", p["drive"]);
        for (int m = 0; m < 2; m++) {
            st.set("presetKey." + manName(m), 11);
            st.set("dbEdit." + manName(m), 1);
            sendPresetKey(m);
        }
        for (int m = 0; m < 3; m++) sendDb(m);
        sendPerc();
        ctx.sendV("vib", st.get("vib"));
        ctx.sendV("drive", st.get("drive"));
        ctx.syncAll();
    }
    /** Which preset the header shows: "i<n>" (instrument, unchanged), "<n>" (registration) or "" (custom). */
    String presetValue() {
        if (curInst >= 0 && instState() == instSig) return "i" + String(curInst);
        curInst = -1;
        auto* list = tables()["presets"].getArray();
        for (int i = 0; i < list->size(); i++) {
            const var p = list->getReference(i);
            auto same = [&](const char* a, const char* b) {
                return juce::JSON::toString(p[a], true) == juce::JSON::toString(st.get(b), true);
            };
            if (presetKey(0) == 11 && presetKey(1) == 11 && same("u", "db.upper") && same("l", "db.lower") &&
                same("p", "db.pedal") && p["vib"].toString() == st.str("vib") &&
                (bool) p["perc"]["on"] == st.flag("perc.on"))
                return String(i);
        }
        return {};
    }

    // ---------------- Output format and channel mapping ----------------
    var mode() const { return tables()["modes"][juce::Identifier(st.str("mode"))]; }
    int modeChannels() const { return (int) mode()["ch"]; }
    juce::Array<int> curMap() const {
        const int n = modeChannels();
        juce::Array<int> out;
        const var m = st.get("chmap." + st.str("mode"));
        if (auto* a = m.getArray(); a && a->size() == n) {
            bool ok = true;
            for (auto& c : *a)
                ok = ok && (c.isInt() || c.isDouble() || c.isInt64()) && (int) c >= 0 && (int) c < 8 &&
                     (double) (int) c == (double) c;
            if (ok) {
                for (auto& c : *a) out.add((int) c);
                return out;
            }
        }
        for (int i = 0; i < n; i++) out.add(i);
        return out;
    }
    int physCh(int i) const { return curMap()[i]; }
    String warnText;
    void applyMode() {
        const var M = mode();
        const auto map = curMap();
        var spk = deepCopy(M["spk"]);
        juce::Array<var> ch;
        if (st.str("mode") == "s51") {
            for (int i : {0, 1, 2, 4, 5}) ch.add(map[i]);
            spk.getDynamicObject()->setProperty("lfe", map[3]);
        } else
            for (int c : map) ch.add(c);
        spk.getDynamicObject()->setProperty("ch", ch);
        spk.getDynamicObject()->setProperty("type", "speakers");
        ctx.send(spk);
        updateChanInfo();
    }
    void updateChanInfo() {
        const int have = (int) st.audioInfo["outputs"];
        int need = 0;
        for (int c : curMap()) need = std::max(need, c + 1);
        const String m = st.str("mode");
        warnText = have && have < need ? "The audio device only has " + String(have) + L" active outputs – " +
                                             (m == "s51"    ? "5.1"
                                              : m == "quad" ? "Quad"
                                                            : "Stereo") +
                                             " needs " + String(need) + L" (Audio tab → Audio Settings)."
                                       : String();
    }
    String outName(int i) const {
        const var n = at(st.audioInfo["channelNames"], i);
        return "Out " + String(i + 1) + (n.toString().isNotEmpty() ? L" · " + n.toString() : String());
    }

    // ---------------- Audio / latency figures ----------------
    bool isPlugin() const { return st.audioInfo["type"].toString() == "Plugin"; }
    double sampleRate() const { return (double) st.audioInfo["sampleRate"]; }
    double outMs() const {
        double s = (double) st.audioInfo["outLatencySamples"];
        if (s <= 0) s = (double) st.audioInfo["bufferSize"];
        const double sr = sampleRate() > 0 ? sampleRate() : 48000;
        return s / sr * 1000;
    }
    double soundPathMs() const {
        if (st.flag("mics.on"))
            return std::min(0.37 + st.num("mics.hornDist"), std::hypot(st.num("mics.roomX") - st.num("rotaryPos.x"),
                                                                       st.num("mics.roomY") - st.num("rotaryPos.y"))) /
                   343 * 1000;
        return std::hypot(st.num("rotaryPos.x") - st.num("listener.x"), st.num("rotaryPos.y") - st.num("listener.y")) /
               343 * 1000;
    }

    // ---------------- Keyboard split ----------------
    String learnTarget; // "hi" / "lo" while a split point waits for the next key
    void setSplit(const String& which, int note) {
        int hi = (int) st.num("split.hi"), lo = (int) st.num("split.lo");
        note = juce::jlimit(1, 126, note);
        if (which == "hi")
            hi = note;
        else
            lo = note;
        if (lo >= hi) {
            if (which == "hi")
                lo = std::max(0, hi - 1);
            else
                hi = std::min(127, lo + 1);
        }
        st.set("split.hi", hi);
        st.set("split.lo", lo);
        sendKeyMap();
        ctx.syncAll();
    }
    void setManOct(int man, int d) {
        const String k = "split.oct." + String(man);
        st.set(k, juce::jlimit(-3, 3, (int) st.num(k) + d));
        sendKeyMap();
        ctx.syncAll();
    }
    static String noteName(int n) {
        static const char* N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return String(N[((n % 12) + 12) % 12]) + String((int) std::floor(n / 12.0) - 1);
    }
    static bool isBlack(int k) {
        const int m = ((k % 12) + 12) % 12;
        return m == 1 || m == 3 || m == 6 || m == 8 || m == 10;
    }
    /** Keys of a manual the split keyboard reaches: {first, last}, or {-1, -2} when nothing is dimmed. */
    std::pair<int, int> reach(int man) const {
        static const std::map<int, std::pair<int, int>> range{
            {49, {36, 84}}, {61, {36, 96}}, {73, {28, 100}}, {76, {28, 103}}, {88, {21, 108}}};
        auto it = range.find((int) st.num("split.kb"));
        const auto [a, b] = it != range.end() ? it->second : range.at(61);
        const int mode = (int) st.num("split.mode"), shi = (int) st.num("split.hi"), slo = (int) st.num("split.lo");
        const int sh = 12 * (int) st.num("split.oct." + String(man));
        int lo, hi;
        if (man == 0) {
            lo = mode >= 1 ? shi : a;
            hi = b;
        } else if (man == 1) {
            if (mode >= 1) {
                lo = mode == 2 ? slo : a;
                hi = shi - 1;
            } else if (sh) {
                lo = a;
                hi = b;
            } else
                return {-1, -2};
        } else {
            if (mode != 2) return {-1, -2};
            lo = a;
            hi = slo - 1;
        }
        const int base = man == 2 ? 24 : 36, max = man == 2 ? 24 : 60;
        return {juce::jlimit(0, max + 1, lo - base + sh), juce::jlimit(-1, max, hi - base + sh)};
    }

    // ---------------- Full state push (startup, after a core rebuild) ----------------
    void pushAll() {
        sendRoomAll();
        ctx.send(Ctx::obj(
            {{"type", "pos"}, {"rotaryPos", st.get("rotaryPos")}, {"listener", st.get("listener")}, {"jump", true}}));
        for (int m = 0; m < 2; m++) {
            sendDbSet(m, 0);
            sendDbSet(m, 1);
            sendPresetKey(m);
        }
        sendDb(2);
        sendPerc();
        for (auto k : {"vib", "drive", "click", "volume", "balance", "lfe", "rotary"}) ctx.sendV(k, st.get(k));
        sendChr();
        ctx.send(Ctx::obj({{"type", "cabYaw"}, {"v", st.get("cabYaw")}, {"jump", true}}));
        sendOrgan();
        sendRotor(0);
        sendRotor(1);
        ctx.sendV("brake", st.get("brake"));
        sendCab();
        sendAmp();
        sendOut();
        sendMidi();
        sendPre();
        sendMains();
        sendSpring();
        sendPrecision();
        sendThreads();
        sendMics();
        sendOffline();
        sendKeyMap();
        applyMode();
    }

    /** Host automated a plugin parameter (the engine has already applied it). */
    void applyParam(const String& id, double v) {
        if (id.startsWith("db_") && id.length() == 6) {
            const juce::juce_wchar m = id[3];
            const int i = id.substring(5).getIntValue();
            const int man = m == 'u' ? 0 : m == 'l' ? 1 : 2;
            st.set(dbEditPath(man) + "." + String(i), juce::roundToInt(v));
            return;
        }
        if (id == "preset_u" || id == "preset_l") {
            setPresetKey(id == "preset_u" ? 0 : 1, juce::roundToInt(v), false);
            return;
        }
        const bool b = v > 0.5;
        static const juce::StringArray vibIds{"v1", "v2", "v3", "c1", "c2", "c3"}, lesIds{"stop", "slow", "fast"};
        if (id == "leslie")
            st.set("rotary", lesIds[juce::jlimit(0, 2, juce::roundToInt(v))]); // ID from earlier versions
        else if (id == "swell")
            st.set("volume", v);
        else if (id == "drive")
            st.set("drive", v);
        else if (id == "vib")
            st.set("vib", vibIds[juce::jlimit(0, 5, juce::roundToInt(v))]);
        else if (id == "vib_upper")
            st.set("perc.vibU", b);
        else if (id == "vib_lower")
            st.set("perc.vibL", b);
        else if (id == "perc_on")
            st.set("perc.on", b);
        else if (id == "perc_soft")
            st.set("perc.soft", b);
        else if (id == "perc_fast")
            st.set("perc.fast", b);
        else if (id == "perc_third")
            st.set("perc.third", b);
    }
};

} // namespace wh
