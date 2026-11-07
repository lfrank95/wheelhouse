#pragma once
// Wheelhouse UI - the UI's state: a JSON tree with the same keys as the former web UI, so saved states (standalone
// file, plugin projects) keep loading. Defaults, migrations of old saves, and the preset tables live here too.
#include <juce_core/juce_core.h>
#include <functional>

namespace wh {

using juce::String;
using juce::var;

/** Deep copy of a JSON value (var copies share objects and arrays). */
inline var deepCopy(const var& v) {
    if (!v.isObject() && !v.isArray()) return v; // scalars are values already (and JSON::parse only reads containers)
    return juce::JSON::parse(juce::JSON::toString(v, true));
}

/** JSON helpers on dotted paths: "room.Lx", "rotor.1.slow", "mics.grp.2.gain". */
struct Json {
    static var* walk(var& root, const String& path, bool create) {
        var* cur = &root;
        for (auto part : juce::StringArray::fromTokens(path, ".", "")) {
            if (auto* arr = cur->getArray()) {
                const int i = part.getIntValue();
                if (i < 0 || i >= arr->size()) return nullptr;
                cur = &arr->getReference(i);
            } else if (auto* obj = cur->getDynamicObject()) {
                const juce::Identifier id(part);
                if (!obj->hasProperty(id)) {
                    if (!create) return nullptr;
                    obj->setProperty(id, var());
                }
                cur = obj->getProperties().getVarPointer(id);
            } else
                return nullptr;
        }
        return cur;
    }
    static var get(const var& root, const String& path) {
        if (auto* p = walk(const_cast<var&>(root), path, false)) return *p;
        return {};
    }
    static void set(var& root, const String& path, const var& v) {
        if (auto* p = walk(root, path, true)) *p = v;
    }
};

/** Fills dst with the values of src for the keys dst already has (same rules as the web UI's deepMerge). */
inline void deepMerge(var& dst, const var& src) {
    auto* d = dst.getDynamicObject();
    auto* s = src.getDynamicObject();
    if (!d || !s) return;
    for (auto& p : s->getProperties()) {
        if (!d->hasProperty(p.name)) continue;
        var& a = *d->getProperties().getVarPointer(p.name);
        const var& b = p.value;
        if (a.isObject() && b.isObject() && !a.isArray() && !b.isArray())
            deepMerge(a, b);
        else if (a.isArray() && b.isArray()) {
            auto* aa = a.getArray();
            auto* ba = b.getArray();
            if (aa->size() == ba->size()) {
                if (aa->size() > 0 && aa->getReference(0).isObject()) {
                    for (int i = 0; i < aa->size(); i++)
                        if (ba->getReference(i).isObject()) deepMerge(aa->getReference(i), ba->getReference(i));
                } else
                    a = deepCopy(b);
            }
        } else {
            const bool sameKind =
                (a.isString() && b.isString()) || (a.isBool() && b.isBool()) ||
                ((a.isDouble() || a.isInt() || a.isInt64()) && (b.isDouble() || b.isInt() || b.isInt64()));
            if (b.isVoid() || sameKind || a.isVoid()) a = b;
        }
    }
}

// ---------------- Defaults (the web UI's DEF, without the removed circus tent) ----------------
inline const char* defaultStateJson() {
    return R"JSON({
 "room": {"Lx": 6, "Ly": 8, "Lz": 2.8, "absorb": 0.3, "order": 2, "reverb": 1, "material": "wood"},
 "exact": false, "threads": 0,
 "chr": {"cond": 0.5, "leak": 0.5, "contact": 0.5, "motor": 0.4, "gearTune": true, "busComp": true},
 "rotaryPos": {"x": 4.2, "y": 6.3},
 "listener": {"x": 2.6, "y": 2.4, "yaw": 1.5707963267948966},
 "cabYaw": -1.5707963267948966,
 "db": {"upper": [8, 8, 8, 0, 0, 0, 0, 0, 0], "lower": [0, 0, 8, 4, 0, 0, 0, 0, 0], "pedal": [6, 3],
        "upperA": [8, 8, 8, 8, 0, 0, 0, 0, 0], "lowerA": [0, 0, 8, 8, 4, 0, 0, 0, 0]},
 "presetKey": {"upper": 11, "lower": 11}, "dbEdit": {"upper": 1, "lower": 1},
 "perc": {"on": true, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": false},
 "vib": "c3", "drive": 0.35, "click": 0.5, "volume": 0.8, "balance": 0.5, "lfe": 0.5,
 "organ": {"percLevel": 1, "percShort": 0.2, "percLong": 1.0, "complex": true, "keyNoise": 0.5, "pedSus": false,
           "pedSusLen": 3},
 "mains": 50, "velClick": true, "offlineMax": true,
 "rotor": [{"slow": 0.75, "fast": 6.8, "up": 0.8, "down": 1.4}, {"slow": 0.62, "fast": 5.7, "up": 4.5, "down": 5.5}],
 "brake": true,
 "cab": {"hornBox": 0.45, "wobble": -1, "board": "ply", "res": 0.5, "mech": -1, "deflector": true},
 "amp": {"model": 0, "quality": 1, "sag": 0.5},
 "pre": {"on": true, "gain": 0.5},
 "spring": {"mix": 0.2, "decay": 2.8, "tone": 0.5, "drive": 0.3, "springs": 3},
 "mics": {"on": false, "hornStereo": true, "hornAngle": 180, "hornCenter": 0, "hornDist": 0.15, "hornHeight": 0,
   "hornType": 0, "hornPattern": 0, "windscreen": false, "drumStereo": false, "drumAngle": 180, "drumCenter": 0,
   "drumDist": 0.15, "drumType": 2, "drumPattern": 0, "roomArr": 0, "roomX": 3, "roomY": 3.5, "roomZ": 2,
   "roomYaw": 1.5707963267948966, "roomSpacing": 0.6, "roomType": 4, "roomPattern": 1, "roomOrder": 2,
   "placed": false,
   "grp": [{"gain": 0, "pan": 0, "width": 1, "delay": 0, "pol": false, "mute": false, "solo": false, "out": -1},
           {"gain": 0, "pan": 0, "width": 1, "delay": 0, "pol": false, "mute": false, "solo": false, "out": -1},
           {"gain": -6, "pan": 0, "width": 1, "delay": 0, "pol": false, "mute": false, "solo": false, "out": -1}]},
 "out": {"masterDb": 0, "lfeHz": 100, "rearDb": 0, "centerDb": 0, "centerDiv": 0},
 "chmap": {"stereo": null, "quad": null, "s51": null},
 "midi": {"upper": 1, "lower": 2, "pedal": 3, "rotaryCC": 1, "swellCC": 11},
 "split": {"mode": 0, "hi": 60, "lo": 48, "oct": [0, 0, 0], "kb": 61},
 "rotary": "slow", "mode": "stereo", "octave": 0, "pcMan": 0, "halfMoon": "stop"
})JSON";
}

/** Older saves: German value names, renamed keys, the Leslie -> Rotary rename, the removed tent. */
inline var migrateState(var o) {
    auto* obj = o.getDynamicObject();
    if (!obj) return o;
    static const std::map<String, String> V{
        {"holz", "wood"}, {"beton", "concrete"}, {"stein", "stone"}, {"glas", "glass"}, {"zelt", "tent"}};
    auto fix = [&](const String& path) {
        auto s = Json::get(o, path).toString();
        if (auto it = V.find(s); it != V.end()) Json::set(o, path, it->second);
    };
    fix("room.material");
    if (Json::get(o, "room.material").toString() == "tent" || Json::get(o, "room.shape").toString() == "tent" ||
        Json::get(o, "room.shape").toString() == "zelt") {
        Json::set(o, "room", juce::JSON::parse(R"({"Lx": 12, "Ly": 18, "Lz": 4.5, "absorb": 0.27, "reverb": 1,
                                                    "material": "concrete", "order": 2})"));
    }
    if (auto cab = Json::get(o, "cab"); cab.isObject() && cab.hasProperty("hornfach") && !cab.hasProperty("hornBox"))
        Json::set(o, "cab.hornBox", cab["hornfach"]);
    if (!obj->hasProperty("rotaryPos") && obj->hasProperty("lesliePos")) obj->setProperty("rotaryPos", o["lesliePos"]);
    if (!obj->hasProperty("rotary") && o["leslie"].isString()) obj->setProperty("rotary", o["leslie"]);
    if (auto m = o["midi"]; m.isObject() && !m.hasProperty("rotaryCC") && m.hasProperty("leslieCC"))
        m.getDynamicObject()->setProperty("rotaryCC", m["leslieCC"]);
    obj->removeProperty("lesliePos");
    obj->removeProperty("leslie");
    if (auto m = o["midi"]; m.isObject()) m.getDynamicObject()->removeProperty("leslieCC");
    if (!obj->hasProperty("rotaryPos") && obj->hasProperty("les")) obj->setProperty("rotaryPos", o["les"]);
    if (!obj->hasProperty("listener") && obj->hasProperty("lis")) obj->setProperty("listener", o["lis"]);
    return o;
}

// ---------------- Preset tables (same content as the web UI) ----------------
inline const var& tables() {
    static const var t = juce::JSON::parse(juce::String::fromUTF8(R"JSON({
 "modes": {
  "stereo": {"ch": 2, "names": ["L", "R"], "long": ["Left", "Right"],
             "spk": {"az": [30, -30], "ch": [0, 1], "lfe": -1, "stereo": true}},
  "quad": {"ch": 4, "names": ["FL", "FR", "RL", "RR"], "long": ["Front left", "Front right", "Rear left", "Rear right"],
           "spk": {"az": [45, -45, 135, -135], "ch": [0, 1, 2, 3], "lfe": -1}},
  "s51": {"ch": 6, "names": ["L", "R", "C", "LFE", "SL", "SR"],
          "long": ["Front left", "Front right", "Center", "LFE", "Surround left", "Surround right"],
          "spk": {"az": [30, -30, 0, 110, -110], "ch": [0, 1, 2, 4, 5], "lfe": 3}}},
 "rooms": [
  {"n": "Living room", "s": "Living", "Lx": 5, "Ly": 6, "Lz": 2.6, "absorb": 0.38, "reverb": 1, "material": "wood"},
  {"n": "Rehearsal room", "s": "Jam", "Lx": 6, "Ly": 8, "Lz": 2.8, "absorb": 0.3, "reverb": 1, "material": "wood"},
  {"n": "Club", "s": "Club", "Lx": 12, "Ly": 18, "Lz": 4.5, "absorb": 0.27, "reverb": 1, "material": "concrete"},
  {"n": "Hall", "s": "Hall", "Lx": 25, "Ly": 40, "Lz": 10, "absorb": 0.12, "reverb": 1, "material": "concrete"},
  {"n": "Church", "s": "Church", "Lx": 16, "Ly": 36, "Lz": 14, "absorb": 0.09, "reverb": 1.1, "material": "stone"}],
 "materials": [["wood", "Wood / drywall"], ["concrete", "Concrete"], ["stone", "Stone"], ["glass", "Glass / tiles"]],
 "upperPresets": [["Jazz", [8, 8, 8, 0, 0, 0, 0, 0, 0]], ["Gospel", [8, 8, 8, 8, 8, 8, 8, 8, 8]],
                  ["Rock", [8, 8, 8, 8, 0, 0, 0, 0, 0]], ["Ballad", [8, 3, 8, 0, 0, 0, 0, 0, 0]],
                  ["Flute", [0, 0, 8, 0, 0, 0, 0, 0, 0]], ["Theatre", [8, 7, 6, 5, 4, 3, 2, 1, 1]],
                  ["Perc 2nd", [8, 4, 8, 0, 0, 0, 0, 0, 0]], ["Smith", [8, 8, 8, 8, 0, 0, 0, 0, 8]]],
 "lowerPresets": [["Comping", [0, 0, 8, 4, 0, 0, 0, 0, 0]], ["Mellow", [0, 0, 6, 6, 0, 0, 0, 0, 0]],
                  ["Jazz", [8, 8, 8, 0, 0, 0, 0, 0, 0]], ["Flute", [0, 0, 8, 0, 0, 0, 0, 0, 0]],
                  ["Strings", [0, 0, 4, 5, 4, 4, 2, 0, 0]], ["Full", [8, 8, 8, 8, 8, 8, 8, 8, 8]]],
 "presets": [
  {"n": "Jazz Combo", "u": [8, 8, 8, 0, 0, 0, 0, 0, 0], "l": [0, 0, 8, 4, 0, 0, 0, 0, 0], "p": [6, 3],
   "perc": {"on": true, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": false}, "vib": "c3", "drive": 0.3},
  {"n": "Full Gospel", "u": [8, 8, 8, 8, 8, 8, 8, 8, 8], "l": [8, 8, 8, 8, 0, 0, 0, 0, 0], "p": [8, 5],
   "perc": {"on": false, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": true}, "vib": "c3", "drive": 0.45},
  {"n": "Rock", "u": [8, 8, 8, 8, 0, 0, 0, 0, 0], "l": [8, 8, 8, 0, 0, 0, 0, 0, 0], "p": [8, 4],
   "perc": {"on": false, "third": true, "fast": true, "soft": false, "vibU": false, "vibL": false}, "vib": "c3", "drive": 0.75},
  {"n": "Ballad", "u": [8, 3, 8, 0, 0, 0, 0, 0, 0], "l": [0, 0, 6, 6, 0, 0, 0, 0, 0], "p": [5, 2],
   "perc": {"on": false, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": true}, "vib": "c3", "drive": 0.15},
  {"n": "Flute", "u": [0, 0, 8, 0, 0, 0, 0, 0, 0], "l": [0, 0, 8, 0, 0, 0, 0, 0, 0], "p": [4, 2],
   "perc": {"on": false, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": true}, "vib": "v2", "drive": 0.1},
  {"n": "Theatre", "u": [8, 7, 6, 5, 4, 3, 2, 1, 1], "l": [0, 0, 4, 5, 4, 4, 2, 0, 0], "p": [7, 4],
   "perc": {"on": false, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": true}, "vib": "v3", "drive": 0.2},
  {"n": "Perc 2nd", "u": [8, 4, 8, 0, 0, 0, 0, 0, 0], "l": [0, 0, 8, 4, 0, 0, 0, 0, 0], "p": [6, 3],
   "perc": {"on": true, "third": false, "fast": false, "soft": false, "vibU": true, "vibL": false}, "vib": "c2", "drive": 0.3}],
 "instruments": [
  {"n": "B3 + 122 (Stock)", "note": "well-kept 60s B3, stock 122 without stop, no reverb",
   "chr": {"cond": 0.45, "leak": 0.45, "contact": 0.45, "motor": 0.46, "gearTune": true, "busComp": true},
   "organ": {"percLevel": 1, "percShort": 0.2, "percLong": 1.0, "complex": true}, "click": 0.5,
   "db": {"upper": [8, 8, 8, 0, 0, 0, 0, 0, 0], "lower": [0, 0, 8, 4, 0, 0, 0, 0, 0], "pedal": [6, 3]},
   "perc": {"on": true, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": false}, "vib": "c3",
   "pre": {"on": true, "gain": 0.5}, "amp": {"model": 0, "quality": 1, "sag": 0.5}, "drive": 0.35, "balance": 0.5,
   "rotor": [{"slow": 0.8, "fast": 6.666666666666667, "up": 0.5, "down": 0.85},
             {"slow": 0.6666666666666666, "fast": 5.7, "up": 1.7, "down": 2.5}],
   "brake": true, "cab": {"hornBox": 0.45, "wobble": -1, "board": "ply", "res": 0.5, "mech": -1, "deflector": true},
   "halfMoon": "two", "spring": {"mix": 0}},
  {"n": "B3 + 147 (Rock)", "note": "147 with negative feedback, hot preamp, vibrato off",
   "chr": {"cond": 0.55, "leak": 0.55, "contact": 0.55, "motor": 0.54, "gearTune": true, "busComp": true},
   "organ": {"percLevel": 1, "percShort": 0.2, "percLong": 1.0, "complex": true}, "click": 0.6,
   "db": {"upper": [8, 8, 8, 8, 0, 0, 0, 0, 0], "lower": [8, 8, 8, 0, 0, 0, 0, 0, 0], "pedal": [8, 4]},
   "perc": {"on": false, "third": true, "fast": true, "soft": false, "vibU": false, "vibL": false}, "vib": "c3",
   "pre": {"on": true, "gain": 0.75}, "amp": {"model": 2, "quality": 1, "sag": 0.6}, "drive": 0.75, "balance": 0.5,
   "rotor": [{"slow": 0.8, "fast": 6.666666666666667, "up": 0.5, "down": 0.85},
             {"slow": 0.6666666666666666, "fast": 5.7, "up": 1.7, "down": 2.5}],
   "brake": true, "cab": {"hornBox": 0.45, "wobble": -1, "board": "ply", "res": 0.5, "mech": -1, "deflector": true},
   "halfMoon": "two", "spring": {"mix": 0}},
  {"n": "A-100 + Spring Reverb", "note": "same tone generator as the B3, built-in organ spring tank",
   "chr": {"cond": 0.45, "leak": 0.45, "contact": 0.45, "motor": 0.46, "gearTune": true, "busComp": true},
   "organ": {"percLevel": 1, "percShort": 0.2, "percLong": 1.0, "complex": true}, "click": 0.45,
   "db": {"upper": [8, 3, 8, 0, 0, 0, 0, 0, 0], "lower": [0, 0, 6, 6, 0, 0, 0, 0, 0], "pedal": [5, 2]},
   "perc": {"on": false, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": true}, "vib": "c3",
   "pre": {"on": true, "gain": 0.5}, "amp": {"model": 0, "quality": 1, "sag": 0.5}, "drive": 0.3, "balance": 0.5,
   "rotor": [{"slow": 0.8, "fast": 6.666666666666667, "up": 0.5, "down": 0.85},
             {"slow": 0.6666666666666666, "fast": 5.7, "up": 1.7, "down": 2.5}],
   "brake": true, "cab": {"hornBox": 0.45, "wobble": -1, "board": "ply", "res": 0.5, "mech": -1, "deflector": true},
   "halfMoon": "two", "spring": {"mix": 0.3, "decay": 2.8, "tone": 0.5, "drive": 0.3, "springs": 3}},
  {"n": "Road-worn B3", "note": "40 years on the road: bouncy contacts, slack belts, tired tubes",
   "chr": {"cond": 0.85, "leak": 0.85, "contact": 0.9, "motor": 0.8, "gearTune": true, "busComp": true},
   "organ": {"percLevel": 0.85, "percShort": 0.17, "percLong": 0.9, "complex": true}, "click": 0.75,
   "db": {"upper": [8, 8, 8, 8, 0, 0, 0, 0, 0], "lower": [8, 4, 8, 0, 0, 0, 0, 0, 0], "pedal": [8, 5]},
   "perc": {"on": true, "third": true, "fast": true, "soft": false, "vibU": true, "vibL": false}, "vib": "c3",
   "pre": {"on": true, "gain": 0.7}, "amp": {"model": 1, "quality": 1, "sag": 0.8}, "drive": 0.6, "balance": 0.5,
   "rotor": [{"slow": 0.7333333333333333, "fast": 6.333333333333333, "up": 0.75, "down": 1.1},
             {"slow": 0.6, "fast": 5.416666666666667, "up": 2.3, "down": 3.2}],
   "brake": true, "cab": {"hornBox": 0.5, "wobble": 0.65, "board": "chip", "res": 0.65, "mech": -1, "deflector": true},
   "halfMoon": "two", "spring": {"mix": 0}}],
 "vib": [["v1", "V1"], ["v2", "V2"], ["v3", "V3"], ["c1", "C1"], ["c2", "C2"], ["c3", "C3"]],
 "ampModels": [[0, "122 · 2 × 6550"], [1, "122 early · 5U4"], [2, "147 · neg. feedback"]]
})JSON"));
    return t;
}

/** The live UI state plus the bits that are not saved (telemetry, audio info). */
struct UiState {
    var S;   // saved state (keys of defaultStateJson)
    var def; // defaults, for "Reset"
    UiState() : S(juce::JSON::parse(defaultStateJson())), def(juce::JSON::parse(defaultStateJson())) {}

    var get(const String& path) const { return Json::get(S, path); }
    double num(const String& path, double fallback = 0) const {
        auto v = get(path);
        return v.isVoid() ? fallback : (double) v;
    }
    bool flag(const String& path) const { return (bool) get(path); }
    String str(const String& path) const { return get(path).toString(); }
    void set(const String& path, const var& v) { Json::set(S, path, v); }
    var defaultOf(const String& path) const { return Json::get(def, path); }

    /** Loads a saved state (JSON text). Returns false if it was empty or unreadable. */
    bool load(const String& json) {
        if (json.trim().isEmpty()) return false;
        var o = juce::JSON::parse(json);
        if (!o.isObject()) return false;
        deepMerge(S, migrateState(o));
        return true;
    }
    String save() const { return juce::JSON::toString(S, true); }

    // not saved: telemetry from the engine
    double hornAngle = 0, drumAngle = 0, hornHz = 0.75, drumHz = -0.62;
    double rotT = 0; // time (ms) of the last rotor update
    juce::Array<var> meters, micMeters;
    String cpuText = L"–", ampText = L"–";
    int procLatency = 0, droppedBlocks = 0;
    var roomInfo, audioInfo = juce::JSON::parse(R"({"outputs": 0, "device": "", "type": "", "sampleRate": 0,
        "bufferSize": 0, "outLatencySamples": 0, "driverLatencySamples": 0, "channelNames": [], "midiDevices": []})");
};

} // namespace wh
