#include "Engine.h"
#include <thread>
#include <cstring>

using namespace juce;

static double numProp(const var& v, const char* name, double def) { auto x = v.getProperty(name, var()); return x.isVoid() ? def : (double) x; }
static bool hasProp(const var& v, const char* name) { return v.hasProperty(name); }

// ---------------- Parameters ----------------
// Only what one really automates in a song: drawbars, Rotary switch, swell, drive, vibrato and percussion.
// Everything else is part of the stored settings.
const std::vector<WheelhouseEngine::ParamDef>& WheelhouseEngine::params() {
    static const std::vector<ParamDef> P = [] {
        std::vector<ParamDef> v;
        static const char* ft[9] = {"16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"};
        static const float du[9] = {8, 8, 8, 0, 0, 0, 0, 0, 0}, dl[9] = {0, 0, 8, 4, 0, 0, 0, 0, 0};
        for (int i = 0; i < 9; i++) v.push_back({ "db_u_" + std::to_string(i), std::string("Upper ") + ft[i], 1, 0, 8, du[i] });
        for (int i = 0; i < 9; i++) v.push_back({ "db_l_" + std::to_string(i), std::string("Lower ") + ft[i], 1, 0, 8, dl[i] });
        v.push_back({ "db_p_0", "Pedal 16'", 1, 0, 8, 6 });
        v.push_back({ "db_p_1", "Pedal 8'", 1, 0, 8, 3 });
        v.push_back({ "leslie", "Rotary", 3, 0, 2, 1 });   // ID kept from earlier versions: existing host automation stays valid
        v.push_back({ "swell", "Swell", 0, 0, 1, 0.8f });
        v.push_back({ "drive", "Drive", 0, 0, 1, 0.35f });
        v.push_back({ "vib", "Vibrato/Chorus", 3, 0, 5, 5 });
        v.push_back({ "vib_upper", "Vibrato Upper", 2, 0, 1, 1 });
        v.push_back({ "vib_lower", "Vibrato Lower", 2, 0, 1, 0 });
        v.push_back({ "perc_on", "Percussion", 2, 0, 1, 1 });
        v.push_back({ "perc_soft", "Perc Soft", 2, 0, 1, 0 });
        v.push_back({ "perc_fast", "Perc Fast", 2, 0, 1, 1 });
        v.push_back({ "perc_third", "Perc 3rd", 2, 0, 1, 1 });
        return v;
    }();
    return P;
}
enum { P_DB_U = 0, P_DB_L = 9, P_DB_P = 18, P_ROTARY = 20, P_SWELL, P_DRIVE, P_VIB, P_VIB_U, P_VIB_L, P_PERC_ON, P_PERC_SOFT, P_PERC_FAST, P_PERC_THIRD, P_COUNT };
static const char* VIB_IDS[6] = {"v1", "v2", "v3", "c1", "c2", "c3"};
static const char* ROTARY_IDS[3] = {"stop", "slow", "fast"};

const StringArray& WheelhouseEngine::choices(int index) {
    static const StringArray rotary { "Stop", "Slow", "Fast" }, vib { "V1", "V2", "V3", "C1", "C2", "C3" }, none;
    return index == P_ROTARY ? rotary : index == P_VIB ? vib : none;
}

int WheelhouseEngine::autoThreads(bool plugin) {
    int hw = (int) std::thread::hardware_concurrency();
    // the DAW needs its own cores for other tracks: a plugin takes fewer by default
    return plugin ? jlimit(1, 4, hw / 4) : jlimit(1, 8, hw / 2);
}

WheelhouseEngine::WheelhouseEngine(bool plugin) : isPlugin(plugin), applied(params().size()) {
    std::memset(heldMan, -1, sizeof(heldMan)); std::memset(heldKey, 0, sizeof(heldKey));
    dsp = std::make_unique<rotary::RotaryRoomDSP>(fs);
    dsp->setThreads(autoThreads(plugin));
    for (size_t i = 0; i < applied.size(); i++) applied[i] = params()[i].def;
}

bool WheelhouseEngine::prepare(double sampleRate, int) {
    {
        std::lock_guard<std::mutex> lock(dspLock);
        if (std::abs(dsp->fs - sampleRate) <= 0.5) { fs = sampleRate; return false; }
        fs = sampleRate;
        int threads = dsp->getThreads();
        dsp = std::make_unique<rotary::RotaryRoomDSP>(sampleRate);
        dsp->setThreads(threads);
        paramsInit = false;   // the host parameters are applied again in the next block
    }
    // the new core starts from defaults: replay the recorded settings (works without an open UI, e.g. in a plugin)
    std::vector<var> msgs;
    { std::lock_guard<std::mutex> g(recLock); for (auto& k : recKeys) if (recMsgs.count(k)) msgs.push_back(recMsgs[k]); }
    for (auto& m : msgs) handleMessage(m, false);
    return true;
}

// ---------------- Play queue ----------------
void WheelhouseEngine::pushPlay(uint8_t type, int man, int key, int value) {
    std::lock_guard<std::mutex> g(playWriteLock);
    const auto w = playFifo.write(1);
    PlayCmd c { type, (int8_t) man, (int16_t) key, (int16_t) value };
    if (w.blockSize1 > 0) playBuf[(size_t) w.startIndex1] = c;
    else if (w.blockSize2 > 0) playBuf[(size_t) w.startIndex2] = c;
}
void WheelhouseEngine::drainPlay() {
    const auto r = playFifo.read(playFifo.getNumReady());
    auto apply = [this](const PlayCmd& c) {
        switch (c.type) {
            case NOTE_ON: dsp->noteOn(c.key, c.man, c.value); break;
            case NOTE_OFF: dsp->noteOff(c.key, c.man, c.value); break;
            case ROTARY: dsp->setRotary(ROTARY_IDS[jlimit(0, 2, (int) c.value)]); break;
            case SWELL: dsp->setVolume(c.value / 127.0); break;
            case PANIC: dsp->panic(); break;
            default: break;
        }
    };
    for (int i = 0; i < r.blockSize1; i++) apply(playBuf[(size_t) (r.startIndex1 + i)]);
    for (int i = 0; i < r.blockSize2; i++) apply(playBuf[(size_t) (r.startIndex2 + i)]);
}
void WheelhouseEngine::pushUi(UiEv e) {
    const auto w = uiFifo.write(1);
    if (w.blockSize1 > 0) uiBuf[(size_t) w.startIndex1] = e; else if (w.blockSize2 > 0) uiBuf[(size_t) w.startIndex2] = e;
}

// MIDI note -> manual and key. Pedal and lower manual by channel; the upper manual on its channel or omni (0).
// On the upper channel the split divides the keyboard: below splitLo pedal (3-way), below splitHi lower, else upper.
// Key numbers: manuals start at MIDI note 36 (C2), the pedal at 24; each manual is shifted by its octave setting.
bool WheelhouseEngine::mapNote(int path, int ch, int note, bool on, int& man, int& key) {
    if (ch < 1 || ch > 16 || note < 0 || note > 127) return false;
    int8_t& hm = heldMan[path][ch - 1][note]; int8_t& hk = heldKey[path][ch - 1][note];
    if (!on && hm >= 0) { man = hm; key = hk; hm = -1; return true; }   // goes where its note-on went
    if (!on) return false;
    man = 0;
    if (ch == midiPedal.load()) man = 2;
    else if (ch == midiLower.load()) man = 1;
    else if (midiUpper.load() != 0 && ch != midiUpper.load()) return false;
    if (man == 0) {
        const int mode = splitMode.load();
        if (mode == 2 && note < splitLo.load()) man = 2;
        else if (mode >= 1 && note < splitHi.load()) man = 1;
    }
    key = note - (man == 2 ? 24 : 36) + 12 * octShift[man].load();
    if (key < 0 || key > (man == 2 ? 24 : 60)) return false;
    hm = (int8_t) man; hk = (int8_t) key;
    return true;
}

var WheelhouseEngine::midiFromDevice(const MidiMessage& m) {
    auto ev = [&](const char* type, int key, int manual) { auto* o = new DynamicObject(); o->setProperty("type", type); o->setProperty("key", key); o->setProperty("man", manual); o->setProperty("note", m.getNoteNumber()); return var(o); };
    if (m.isNoteOn() || m.isNoteOff()) {
        int man = 0, key = 0; bool ok;
        { std::lock_guard<std::mutex> g(playWriteLock); ok = mapNote(0, m.getChannel(), m.getNoteNumber(), m.isNoteOn(), man, key); }
        if (!ok) { if (m.isNoteOn()) { auto* o = new DynamicObject(); o->setProperty("type", "noteOut"); o->setProperty("note", m.getNoteNumber()); return var(o); } return {}; }
        pushPlay(m.isNoteOn() ? NOTE_ON : NOTE_OFF, man, key, m.getVelocity());
        return ev(m.isNoteOn() ? "noteOn" : "noteOff", key, man);
    }
    if (m.isController()) {
        int cc = m.getControllerNumber(), v = m.getControllerValue();
        auto* o = new DynamicObject();
        if (cc == rotaryCC.load()) { pushPlay(ROTARY, 0, 0, v >= 64 ? 2 : 1); o->setProperty("type", "rotary"); o->setProperty("v", v >= 64 ? "fast" : "slow"); return var(o); }
        if (cc == swellCC.load()) { pushPlay(SWELL, 0, 0, v); o->setProperty("type", "volume"); o->setProperty("v", v / 127.0); return var(o); }
        if (cc == 123) { pushPlay(PANIC, 0, 0, 0); o->setProperty("type", "panic"); return var(o); }
        delete o;
    }
    return {};
}

// ---------------- Audio ----------------
void WheelhouseEngine::render(float* const* out, int n, const MidiBuffer* midi) {
    std::unique_lock<std::mutex> lock(dspLock, std::defer_lock);
    const double t0 = Time::getMillisecondCounterHiRes(), waitMs = 300.0 * n / jmax(8000.0, fs);
    // offline (bounce): the host waits for us, so wait for the lock as long as it takes
    if (offlineActive) lock.lock();
    else while (!lock.try_lock()) { if (Time::getMillisecondCounterHiRes() - t0 > waitMs) break; std::this_thread::yield(); }
    if (!lock.owns_lock()) { for (int c = 0; c < 16; c++) FloatVectorOperations::clear(out[c], n); droppedBlocks++; return; }

    drainPlay();
    if (hostParam) applyHostParams();
    if (midi == nullptr || midi->isEmpty()) dsp->process(out, 16, n);
    else {
        // host MIDI with sample positions: render up to each event, apply it, continue
        int pos = 0; float* seg[16];
        for (const auto meta : *midi) {
            const int at = jlimit(0, n, meta.samplePosition);
            if (at > pos) { for (int c = 0; c < 16; c++) seg[c] = out[c] + pos; dsp->process(seg, 16, at - pos); pos = at; }
            const auto m = meta.getMessage();
            if (m.isNoteOn() || m.isNoteOff()) {
                int man = 0, key = 0;
                if (mapNote(1, m.getChannel(), m.getNoteNumber(), m.isNoteOn(), man, key)) {
                    if (m.isNoteOn()) dsp->noteOn(key, man, m.getVelocity()); else dsp->noteOff(key, man, m.getVelocity());
                    pushUi({ (uint8_t) (m.isNoteOn() ? 1 : 2), (int8_t) man, (int16_t) key, (int16_t) m.getNoteNumber() });
                } else if (m.isNoteOn()) pushUi({ 4, 0, 0, (int16_t) m.getNoteNumber() });
            } else if (m.isController()) {
                int cc = m.getControllerNumber(), v = m.getControllerValue();
                if (cc == rotaryCC.load()) dsp->setRotary(v >= 64 ? "fast" : "slow");
                else if (cc == swellCC.load()) dsp->setVolume(v / 127.0);
                else if (cc == 123 || cc == 120) dsp->panic();
            }
        }
        if (pos < n) { for (int c = 0; c < 16; c++) seg[c] = out[c] + pos; dsp->process(seg, 16, n - pos); }
    }
    teleHorn = dsp->hornAngle(); teleDrum = dsp->drumAngle(); teleHornHz = dsp->hornHz() * dsp->rotorDir(0); teleDrumHz = dsp->drumHz() * dsp->rotorDir(1);
    telePlate = dsp->ampPlate(); teleCathode = dsp->ampCathode(); teleLatency = dsp->processingLatency();
    teleMics = dsp->mics().on;
    for (int c = 0; c < 6; c++) teleMicMeter[(size_t) c] = dsp->micMeter(c);
}

void WheelhouseEngine::applyHostParams() {
    const auto& P = params();
    bool db[3] = {false, false, false}, perc = false, first = !paramsInit.load();
    for (int i = 0; i < (int) P.size(); i++) {
        const float v = hostParam[i].load();
        if (!first && v == applied[(size_t) i].load()) continue;
        applied[(size_t) i] = v;
        if (!first) pushUi({ 3, 0, (int16_t) i });
        if (i < P_DB_L) db[0] = true; else if (i < P_DB_P) db[1] = true; else if (i < P_ROTARY) db[2] = true;
        else if (i == P_ROTARY) dsp->setRotary(ROTARY_IDS[jlimit(0, 2, (int) std::lround(v))]);
        else if (i == P_SWELL) dsp->setVolume(v);
        else if (i == P_DRIVE) dsp->setDrive(v);
        else if (i == P_VIB) dsp->setVib(VIB_IDS[jlimit(0, 5, (int) std::lround(v))]);
        else perc = true;
    }
    for (int m = 0; m < 3; m++) if (db[m]) {
        int v[9] = {0}; const int base = m == 0 ? P_DB_U : m == 1 ? P_DB_L : P_DB_P, cnt = m == 2 ? 2 : 9;
        for (int k = 0; k < cnt; k++) v[k] = (int) std::lround(applied[(size_t) (base + k)].load());
        dsp->setDrawbars(v, m);
    }
    if (perc) {
        rotary::Perc p; p.on = applied[P_PERC_ON] > 0.5f; p.soft = applied[P_PERC_SOFT] > 0.5f; p.fast = applied[P_PERC_FAST] > 0.5f; p.third = applied[P_PERC_THIRD] > 0.5f;
        dsp->setPerc(p); dsp->setVibRouting(applied[P_VIB_U] > 0.5f, applied[P_VIB_L] > 0.5f);
    }
    paramsInit = true;
}

// The UI changed something that is also a host parameter: remember it as applied and tell the host
void WheelhouseEngine::uiParamsFromMessage(const String& type, const var& m) {
    if (!onUiParam) return;
    auto set = [this](int i, float v) { if (applied[(size_t) i].load() == v) return; applied[(size_t) i] = v; onUiParam(i, v); };
    if (type == "drawbars") {
        const int man = (int) m.getProperty("man", 0), base = man == 0 ? P_DB_U : man == 1 ? P_DB_L : P_DB_P, cnt = man == 2 ? 2 : 9;
        if (auto* a = m.getProperty("v", var()).getArray()) for (int k = 0; k < cnt && k < a->size(); k++) set(base + k, (float) (int) (*a)[k]);
    } else if (type == "perc") {
        auto v = m.getProperty("v", var());
        set(P_PERC_ON, (bool) v.getProperty("on", true) ? 1.f : 0.f); set(P_PERC_SOFT, (bool) v.getProperty("soft", false) ? 1.f : 0.f);
        set(P_PERC_FAST, (bool) v.getProperty("fast", true) ? 1.f : 0.f); set(P_PERC_THIRD, (bool) v.getProperty("third", true) ? 1.f : 0.f);
        set(P_VIB_U, (bool) v.getProperty("vibU", true) ? 1.f : 0.f); set(P_VIB_L, (bool) v.getProperty("vibL", false) ? 1.f : 0.f);
    } else if (type == "vib") {
        const String s = m.getProperty("v", "c3").toString();
        for (int k = 0; k < 6; k++) if (s == VIB_IDS[k]) set(P_VIB, (float) k);
    } else if (type == "rotary") {
        const String s = m.getProperty("v", "slow").toString();
        for (int k = 0; k < 3; k++) if (s == ROTARY_IDS[k]) set(P_ROTARY, (float) k);
    } else if (type == "drive") set(P_DRIVE, (float) numProp(m, "v", 0.35));
    else if (type == "volume") set(P_SWELL, (float) numProp(m, "v", 0.8));
}

void WheelhouseEngine::startTestTone(int channel, bool lfe) { testIsLfe = lfe; testSamplesLeft = (int) (fs * 0.5); testPhase = 0; testChannel = channel; }
// 440 Hz triangle (55 Hz on the LFE), 0.5 s with 20 ms fades
void WheelhouseEngine::addTestTone(float* const* out, int nCh, int n) {
    int tc = testChannel.load();
    if (tc < 0 || testSamplesLeft <= 0 || tc >= nCh || out[tc] == nullptr) return;
    double f = testIsLfe.load() ? 55.0 : 440.0;
    int left = testSamplesLeft.load();
    for (int i = 0; i < n && left > 0; i++, left--) {
        double env = jmin(1.0, left / (0.02 * fs), (fs * 0.5 - left) / (0.02 * fs));
        testPhase += f / fs; if (testPhase >= 1) testPhase -= 1;
        out[tc][i] += (float) ((1 - 4 * std::fabs(testPhase - 0.5)) * 0.18 * env);
    }
    testSamplesLeft = left;
    if (left <= 0) testChannel = -1;
}

// ---------------- Offline render ----------------
void WheelhouseEngine::setNonRealtime(bool offline) {
    if (offline == offlineActive) return;
    offlineActive = offline;
    if (!offline || !offlineMax.load()) return;
    // audio thread, the host is not waiting in real time: switch everything to maximum on the spot
    std::lock_guard<std::mutex> lock(dspLock);
    dsp->roomRef().order = 5; dsp->roomChanged();
    dsp->setExactPaths(true);
    dsp->setAmp(ampRequest.model, 2, ampRequest.sag);
}
void WheelhouseEngine::restoreRealtimeQuality() {
    // back to the settings the UI chose (replayed from the recorded messages)
    std::vector<var> msgs;
    { std::lock_guard<std::mutex> g(recLock); for (auto k : { "room", "precision", "amp" }) if (recMsgs.count(k)) msgs.push_back(recMsgs[k]); }
    for (auto& m : msgs) handleMessage(m, false);
}

// ---------------- Telemetry ----------------
var WheelhouseEngine::makeRotEvent(double cpu) {
    auto* o = new DynamicObject();
    o->setProperty("horn", teleHorn.load()); o->setProperty("drum", teleDrum.load());
    o->setProperty("hornHz", teleHornHz.load()); o->setProperty("drumHz", teleDrumHz.load());
    o->setProperty("cpu", cpu);
    o->setProperty("vb", telePlate.load()); o->setProperty("vk", teleCathode.load());
    o->setProperty("procLatency", teleLatency.load());
    o->setProperty("dropped", droppedBlocks.load());
    if (teleMics.load()) { Array<var> mm; for (int c = 0; c < 6; c++) mm.add(20.0 * std::log10(teleMicMeter[(size_t) c].load() + 1e-6)); o->setProperty("mm", mm); }
    return var(o);
}
var WheelhouseEngine::makeRoomInfoEvent() {
    rotary::RoomInfo ri; { std::lock_guard<std::mutex> lock(dspLock); ri = dsp->info(); }
    auto* o = new DynamicObject();
    Array<var> rt; rt.add(ri.rt60[0]); rt.add(ri.rt60[1]); rt.add(ri.rt60[2]);
    o->setProperty("rt60", rt); o->setProperty("rc", ri.rc); o->setProperty("paths", ri.paths);
    return var(o);
}
bool WheelhouseEngine::popUiEvent(var& ev) {
    if (uiFifo.getNumReady() == 0) return false;
    UiEv e; const auto r = uiFifo.read(1);
    e = r.blockSize1 > 0 ? uiBuf[(size_t) r.startIndex1] : uiBuf[(size_t) r.startIndex2];
    auto* o = new DynamicObject();
    if (e.kind == 3) {
        const auto& p = params()[(size_t) e.key];
        o->setProperty("type", "param"); o->setProperty("id", String(p.id)); o->setProperty("v", applied[(size_t) e.key].load());
    } else if (e.kind == 4) { o->setProperty("type", "noteOut"); o->setProperty("note", (int) e.note); }
    else { o->setProperty("type", e.kind == 1 ? "noteOn" : "noteOff"); o->setProperty("key", (int) e.key); o->setProperty("man", (int) e.man); o->setProperty("note", (int) e.note); }
    ev = var(o);
    return true;
}

// ---------------- State ----------------
// Last message per setting. "room" and "pos" carry partial updates, so their fields are merged.
void WheelhouseEngine::record(const String& type, const var& m) {
    static const StringArray transient { "noteOn", "noteOff", "panic", "springKick" };
    if (transient.contains(type)) return;
    String key = type;
    if (type == "drawbars") key << ":" << (int) m.getProperty("man", 0);
    else if (type == "rotor") key << ":" << (int) numProp(m, "r", 0);
    std::lock_guard<std::mutex> g(recLock);
    if (!recMsgs.count(key)) recKeys.add(key);
    if ((type == "room" || type == "pos") && recMsgs.count(key)) {
        auto* dst = recMsgs[key].getDynamicObject(); auto* src = m.getDynamicObject();
        if (dst && src) {
            for (auto& p : src->getProperties()) {
                if (type == "room" && p.name.toString() == "v" && dst->getProperty("v").getDynamicObject() && p.value.getDynamicObject()) {
                    auto* dv = dst->getProperty("v").getDynamicObject();
                    for (auto& q : p.value.getDynamicObject()->getProperties()) dv->setProperty(q.name, q.value);
                } else dst->setProperty(p.name, p.value);
            }
            return;
        }
    }
    recMsgs[key] = JSON::parse(JSON::toString(m));   // deep copy
}
var WheelhouseEngine::saveState() const {
    auto* o = new DynamicObject();
    Array<var> msgs;
    { std::lock_guard<std::mutex> g(recLock); for (auto& k : recKeys) if (recMsgs.count(k)) msgs.add(recMsgs.at(k)); }
    o->setProperty("version", 1); o->setProperty("messages", msgs); o->setProperty("ui", uiState);
    return var(o);
}
void WheelhouseEngine::restoreState(const var& state) {
    uiState = state.getProperty("ui", "").toString();
    if (auto* a = state.getProperty("messages", var()).getArray()) for (auto& m : *a) handleMessage(m);
}

// ---------------- Messages from the UI (same JSON format as the browser prototype) ----------------
// Settings saved by earlier versions used "leslie" in message types and keys: translate them on the way in.
static var upgradeLegacy(const var& in) {
    auto* src = in.getDynamicObject(); if (!src) return in;
    bool old = in.getProperty("type", "").toString() == "leslie";
    for (auto& p : src->getProperties()) if (p.name.toString().startsWith("leslie")) old = true;
    if (!old) return in;
    auto* o = new DynamicObject();
    for (auto& p : src->getProperties()) {
        String k = p.name.toString(); if (k.startsWith("leslie")) k = "rotary" + k.substring(6);
        o->setProperty(Identifier(k), p.value);
    }
    if (o->getProperty("type").toString() == "leslie") o->setProperty("type", "rotary");
    return var(o);
}
bool WheelhouseEngine::handleMessage(const var& mIn, bool rec) {
    const var m = upgradeLegacy(mIn);
    const String type = m.getProperty("type", "").toString();
    const int man = (int) m.getProperty("man", 0);
    if (type == "noteOn") { pushPlay(NOTE_ON, man, (int) m.getProperty("key", 0), (int) numProp(m, "vel", 100)); return false; }
    if (type == "noteOff") { pushPlay(NOTE_OFF, man, (int) m.getProperty("key", 0), (int) numProp(m, "vel", 64)); return false; }
    if (type == "panic") { pushPlay(PANIC, 0, 0, 0); return false; }
    if (rec) record(type, m);
    uiParamsFromMessage(type, m);
    bool roomInfoChanged = false;
    {
        std::lock_guard<std::mutex> lock(dspLock);
        auto& d = *dsp;
        if (type == "drawbars") { int v[9] = {0}; if (auto* a = m.getProperty("v", var()).getArray()) for (int i = 0; i < 9 && i < a->size(); i++) v[i] = (int) (*a)[i]; d.setDrawbars(v, man); }
        else if (type == "perc") {
            rotary::Perc p; auto v = m.getProperty("v", var());
            p.on = (bool) v.getProperty("on", true); p.third = (bool) v.getProperty("third", true); p.fast = (bool) v.getProperty("fast", true); p.soft = (bool) v.getProperty("soft", false);
            d.setPerc(p); d.setVibRouting((bool) v.getProperty("vibU", true), (bool) v.getProperty("vibL", false));
        }
        else if (type == "amp") { ampRequest = { (int) numProp(m, "model", 0), (int) numProp(m, "quality", 1), numProp(m, "sag", 0.5) }; d.setAmpSag(ampRequest.sag); }
        else if (type == "rotor") d.setRotor((int) numProp(m, "r", 0), numProp(m, "slow", 0.75), numProp(m, "fast", 6.8), numProp(m, "up", 0.8), numProp(m, "down", 1.4));
        else if (type == "spring") d.setSpring(numProp(m, "mix", 0) > 0.001, numProp(m, "mix", 0.2), numProp(m, "decay", 2.8), numProp(m, "tone", 0.5), numProp(m, "drive", 0.3), (int) numProp(m, "springs", 3));
        else if (type == "threads") { int n = (int) numProp(m, "n", 0); if (n <= 0) n = autoThreads(isPlugin); d.setThreads(n); }
        else if (type == "precision") d.setExactPaths((bool) m.getProperty("exact", false));
        else if (type == "springKick") d.kickSpring(numProp(m, "v", 1));
        else if (type == "preamp") d.setPreamp((bool) m.getProperty("on", true), numProp(m, "gain", 0.5));
        else if (type == "offlineMax") offlineMax = (bool) m.getProperty("v", true);
        else if (type == "mics") {
            rotary::MicSetup ms; auto v = m.getProperty("v", var());
            auto b = [&](const char* k, bool def) { auto x = v.getProperty(k, var()); return x.isVoid() ? def : (bool) x; };
            auto n = [&](const char* k, double def) { return numProp(v, k, def); };
            ms.on = b("on", false);
            ms.hornStereo = b("hornStereo", true); ms.hornAngle = n("hornAngle", 180); ms.hornCenter = n("hornCenter", 0); ms.hornDist = n("hornDist", 0.15); ms.hornHeight = n("hornHeight", 0);
            ms.hornType = (int) n("hornType", 0); ms.hornPattern = (int) n("hornPattern", 0); ms.windscreen = b("windscreen", false);
            ms.drumStereo = b("drumStereo", false); ms.drumAngle = n("drumAngle", 180); ms.drumCenter = n("drumCenter", 0); ms.drumDist = n("drumDist", 0.15);
            ms.drumType = (int) n("drumType", 2); ms.drumPattern = (int) n("drumPattern", 0);
            ms.roomArr = (int) n("roomArr", 0); ms.roomX = n("roomX", 3); ms.roomY = n("roomY", 3); ms.roomZ = n("roomZ", 2); ms.roomYaw = n("roomYaw", 1.5708);
            ms.roomSpacing = n("roomSpacing", 0.6); ms.roomType = (int) n("roomType", 4); ms.roomPattern = (int) n("roomPattern", 1); ms.roomOrder = (int) n("roomOrder", 2);
            if (auto* a = v.getProperty("grp", var()).getArray())
                for (int g = 0; g < rotary::MIC_GRP && g < a->size(); g++) {
                    auto& G = ms.grp[g]; auto x = (*a)[g];
                    G.gainDb = numProp(x, "gain", G.gainDb); G.pan = numProp(x, "pan", 0); G.width = numProp(x, "width", 1); G.delayMs = numProp(x, "delay", 0);
                    G.pol = (bool) x.getProperty("pol", false); G.mute = (bool) x.getProperty("mute", false); G.solo = (bool) x.getProperty("solo", false);
                    G.out = (int) numProp(x, "out", -1);
                }
            d.setMics(ms);
        }
        else if (type == "brake") d.setBrake((bool) m.getProperty("v", true));
        else if (type == "cabDetail") {
            d.setCabinetDetail(numProp(m, "hornBox", 0.45), numProp(m, "wobble", -1));
            d.setCabinetBody(m.getProperty("board", "ply").toString() == "chip" ? 1 : 0, numProp(m, "res", 0.5), numProp(m, "mech", -1));
            d.setDeflector(m.hasProperty("deflector") ? (bool) m.getProperty("deflector", true) : true);
        }
        else if (type == "organDetail") {
            d.setOrganDetail(numProp(m, "percLevel", 1), numProp(m, "percShort", 0.2), numProp(m, "percLong", 1.0), (bool) m.getProperty("complex", true));
            d.setKeyNoise(numProp(m, "keyNoise", 0.5));
        }
        else if (type == "output") { d.setOutput(numProp(m, "masterDb", 0), numProp(m, "lfeHz", 100)); d.setOutputTrims(numProp(m, "rearDb", 0), numProp(m, "centerDb", 0), numProp(m, "centerDiv", 0)); }
        else if (type == "keyMap") {
            splitMode = jlimit(0, 2, (int) numProp(m, "mode", 0)); splitHi = jlimit(1, 127, (int) numProp(m, "hi", 60)); splitLo = jlimit(0, 126, (int) numProp(m, "lo", 48));
            if (auto* a = m.getProperty("oct", var()).getArray()) for (int i = 0; i < 3 && i < a->size(); i++) octShift[i] = jlimit(-3, 3, (int) (*a)[i]);
        }
        else if (type == "midiChannels") { midiUpper = (int) numProp(m, "upper", 1); midiLower = (int) numProp(m, "lower", 2); midiPedal = (int) numProp(m, "pedal", 3); rotaryCC = (int) numProp(m, "rotaryCC", 1); swellCC = (int) numProp(m, "swellCC", 11); }
        else if (type == "vib") d.setVib(m.getProperty("v", "c3").toString().toStdString());
        else if (type == "drive") d.setDrive(numProp(m, "v", 0.35));
        else if (type == "click") d.setClick(numProp(m, "v", 0.5));
        else if (type == "volume") d.setVolume(numProp(m, "v", 0.8));
        else if (type == "balance") d.setBalance(numProp(m, "v", 0.5));
        else if (type == "rotary") d.setRotary(m.getProperty("v", "slow").toString().toStdString());
        else if (type == "lfe") d.setLfe(numProp(m, "v", 0.5));
        else if (type == "cabYaw") d.setCabYaw(numProp(m, "v", 0), (bool) m.getProperty("jump", false));
        else if (type == "room") {
            auto v = m.getProperty("v", var());
            auto& r = d.roomRef();
            if (hasProp(v, "Lx")) r.Lx = numProp(v, "Lx", r.Lx);
            if (hasProp(v, "Ly")) r.Ly = numProp(v, "Ly", r.Ly);
            if (hasProp(v, "Lz")) r.Lz = numProp(v, "Lz", r.Lz);
            if (hasProp(v, "absorb")) r.absorb = numProp(v, "absorb", r.absorb);
            if (hasProp(v, "reverb")) r.reverb = numProp(v, "reverb", r.reverb);
            if (hasProp(v, "order")) r.order = (int) numProp(v, "order", r.order);
            if (hasProp(v, "material")) r.material = v.getProperty("material", "wood").toString().toStdString();
            if (hasProp(v, "shape")) r.shape = v.getProperty("shape", "box").toString().toStdString();
            if (hasProp(v, "tentVolume")) r.tentVolume = numProp(v, "tentVolume", 0);
            if (hasProp(v, "tentSurface")) r.tentSurface = numProp(v, "tentSurface", 0);
            if (hasProp(v, "absBands")) { auto ab = v.getProperty("absBands", var()); if (auto* a = ab.getArray(); a && a->size() >= 3) { r.hasAbsBands = true; for (int i = 0; i < 3; i++) r.absBands[i] = (double) (*a)[i]; } else r.hasAbsBands = false; }
            d.roomChanged(); roomInfoChanged = true;
        }
        else if (type == "mains") d.setMains(numProp(m, "hz", 50));
        else if (type == "velocityClick") d.setVelocityClick((bool) m.getProperty("v", true));
        else if (type == "character") {
            rotary::Character c; auto v = m.getProperty("v", var());
            c.cond = numProp(v, "cond", 0.5); c.leak = numProp(v, "leak", 0.5); c.contact = numProp(v, "contact", 0.5); c.motor = numProp(v, "motor", 0.4);
            c.gearTune = (bool) v.getProperty("gearTune", true); c.busComp = (bool) v.getProperty("busComp", true);
            d.setCharacter(c);
        }
        else if (type == "pos") {
            auto rotaryPos = m.getProperty("rotaryPos", var()), listener = m.getProperty("listener", var());
            d.setPos(!rotaryPos.isVoid(), numProp(rotaryPos, "x", 0), numProp(rotaryPos, "y", 0), !listener.isVoid(), numProp(listener, "x", 0), numProp(listener, "y", 0), hasProp(listener, "yaw"), numProp(listener, "yaw", 0), (bool) m.getProperty("jump", false));
        }
        else if (type == "speakers") {
            rotary::Speakers s;
            if (auto* a = m.getProperty("az", var()).getArray()) for (auto& x : *a) s.az.push_back((double) x);
            if (auto* a = m.getProperty("ch", var()).getArray()) for (auto& x : *a) s.ch.push_back((int) x);
            s.lfe = (int) m.getProperty("lfe", -1); s.stereo = (bool) m.getProperty("stereo", false);
            // the room/listener mix has 8 speaker slots; reject anything outside (a stale saved mapping must not crash)
            bool valid = !s.az.empty() && s.az.size() == s.ch.size() && s.lfe < 8;
            for (int c : s.ch) if (c < 0 || c >= 8) valid = false;
            if (valid) d.setSpeakers(s);
        }
        else if (type == "taps") {
            std::vector<rotary::Tap> taps;
            if (auto* a = m.getProperty("taps", var()).getArray()) for (auto& t : *a) {
                rotary::Tap tp; tp.t = numProp(t, "t", 0); tp.az = numProp(t, "az", 0); tp.el = numProp(t, "el", 0); tp.order = (int) numProp(t, "order", 1);
                if (auto* dp = t.getProperty("dep", var()).getArray(); dp && dp->size() >= 3) for (int i = 0; i < 3; i++) tp.dep[i] = (double) (*dp)[i];
                if (auto* e = t.getProperty("E", var()).getArray(); e && e->size() >= 3) for (int i = 0; i < 3; i++) tp.E[i] = (double) (*e)[i];
                taps.push_back(tp);
            }
            d.setTaps(std::move(taps)); roomInfoChanged = true;
        }
    }
    if (type == "amp") applyAmpModelOffLock();
    return roomInfoChanged;
}

// A model or quality change re-settles the whole tube circuit (hundreds of ms of simulation). Doing that under
// dspLock would stall the audio thread, so the new amplifier is prepared on a copy and swapped in afterwards.
void WheelhouseEngine::applyAmpModelOffLock() {
    const AmpRequest req = ampRequest;
    std::unique_ptr<rotary::amp::RotaryAmp> ampCopy; std::unique_ptr<rotary::amp::OrganPreamp> preCopy;
    {
        std::lock_guard<std::mutex> lock(dspLock);
        if (dsp->ampModel().getModel() != req.model || dsp->ampModel().getQuality() != req.quality) ampCopy = std::make_unique<rotary::amp::RotaryAmp>(dsp->ampModel());
        if (dsp->preampModel().getQuality() != req.quality) preCopy = std::make_unique<rotary::amp::OrganPreamp>(dsp->preampModel());
    }
    if (!ampCopy && !preCopy) return;
    if (ampCopy) { ampCopy->setModel(req.model); ampCopy->setQuality(req.quality); ampCopy->setSag(req.sag); }
    if (preCopy) preCopy->setQuality(req.quality);
    std::lock_guard<std::mutex> lock(dspLock);
    if (ampCopy) dsp->ampModel() = *ampCopy;
    if (preCopy) dsp->preampModel() = *preCopy;
}
