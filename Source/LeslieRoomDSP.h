// Leslie im Raum – DSP-Kern (C++-Port von processor7.js)
// Tonradorgel -> Scanner-Vibrato -> Schweller -> Leslie-Verstärker -> Weiche -> rotierende Quellen im Raum
// (Spiegelquellen / Strahlen-Taps, Doppler, Gehäuse-Richtwirkung) -> 16-Leitungen-FDN -> Panning.
// Kein JUCE in dieser Datei, damit der Kern auch ohne Framework getestet werden kann.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include "LeslieAmp.h"

namespace leslie {

constexpr double TWO_PI = 6.283185307179586;
constexpr double C_SOUND = 343.0;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    Biquad& set(const char* type, double f, double q, double fs, double gainDb = 0) {
        double w = TWO_PI * f / fs, cw = std::cos(w), sw = std::sin(w), al = sw / (2 * q);
        double B0, B1, B2, A0 = 1 + al, A1 = -2 * cw, A2 = 1 - al;
        if (type[0] == 'l') { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; }
        else if (type[0] == 'h') { B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; }
        else { double A = std::pow(10.0, gainDb / 40); B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A2 = 1 - al / A; }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        return *this;
    }
    inline double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

struct Lcg { uint32_t s; explicit Lcg(uint32_t seed) : s(seed) {} double operator()() { s = s * 1664525u + 1013904223u; return s / 4294967296.0; } };

struct Tap { double t; double az; double el; double dep[3]; int order; double E[3]; };

struct Path {
    int rotor = 0, order = 0;
    bool trace = false, fast = false, lin = false, init = false;
    double sx = 1, sy = 1, sz = 1, ox = 0, oy = 0, oz = 0, g = 1;
    double kOn = 1, kOff = 1, kAir = 1, kDrum = 1, kc = 1, kb = 1, lp = 0, zd = 0, zlo = 0;
    double icx = 0, icy = 0, iz = 0, jit = 0;
    double d0 = 0, d1 = 0, g0 = 0, g1 = 0, dl0 = 0, dl1 = 0, cab0 = 0, cab1 = 0, hf = 1;
    int bP = 0, bN = 0;
    std::array<float, 8> gPrev{}, gNext{};
    // Strahlen-Taps
    double dist = 0, dep[3] = {0, 0, 0}, azRoom = 0, el = 0, gm = 0, glr = 1, kTrace = 1;
};

struct Rotor { double ang, hz, tgt, slow, fast, r, h, upTau, dnTau; };

struct Room {
    double Lx = 6, Ly = 8, Lz = 2.8, absorb = 0.25, reverb = 1, tentV = 0, tentS = 0;
    int order = 2;
    std::string material = "holz", shape = "quader";
    bool hasAbsBands = false; double absBands[3] = {0, 0, 0};
};

struct Character { double cond = 0.5, leak = 0.5, contact = 0.5, motor = 0.4; bool gearTune = true, busComp = true; };
struct Perc { bool on = true, third = true, fast = true, soft = false; };
struct Speakers { std::vector<double> az; std::vector<int> ch; int lfe = -1; bool stereo = false; };

struct RoomInfo { double rt60[3] = {0, 0, 0}; double rc = 0; int paths = 0; };

class LeslieRoomDSP {
public:
    explicit LeslieRoomDSP(double sampleRate) : fs(sampleRate), amp(sampleRate) { init(); }

    // ---------------- Steuerung (entspricht den Worklet-Nachrichten) ----------------
    // Manuale: 0 = Ober, 1 = Unter, 2 = Pedal
    void noteOn(int k, int man = 0);
    void noteOff(int k, int man = 0);
    void panic() {
        for (int m = 0; m < 3; m++) { std::fill(keys[m].begin(), keys[m].end(), 0); std::fill(target[m].begin(), target[m].end(), 0.f); }
        numKeysUpper = 0; events.clear(); std::fill(percG.begin(), percG.end(), 0.f);
    }
    void setDrawbars(const int* v, int man = 0) { int n = man == 2 ? 2 : 9; for (int i = 0; i < n; i++) drawbars[man][i] = v[i]; retarget(); }
    void setPerc(const Perc& p) { perc = p; retarget(); }
    void setVibRouting(bool upper, bool lower) { vibUpper = upper; vibLower = lower; }
    void setVib(const std::string& v) { vibMode = v; }
    void setDrive(double v) { drive = v; amp.setDrive(v); }
    void setAmp(int model, int quality, double sagAmount) { amp.setModel(model); amp.setQuality(quality); amp.setSag(sagAmount); }
    void setRotor(int r, double slowHz, double fastHz, double upTau, double dnTau) {
        auto& R = rot[r]; R.slow = slowHz; R.fast = fastHz; R.upTau = upTau; R.dnTau = dnTau; setLeslie(leslieMode);
    }
    void setBrake(bool on) { brake = on; }
    void setCabinetDetail(double hornfach, double wobble) { hornfachLevel = hornfach; wobbleOverride = wobble; applyCharacter(); }
    void setOrganDetail(double percLevel, double percShort, double percLong, bool complexWheels) { percLevelUser = percLevel; percShortS = percShort; percLongS = percLong; complexLow = complexWheels; }
    void setOutput(double masterDb, double lfeHz) {
        masterGain = std::pow(10.0, masterDb / 20); lfeLp1.set("lp", lfeHz, 0.70710678, fs); lfeLp2.set("lp", lfeHz, 0.70710678, fs);
    }
    double ampPlate() const { return amp.plateSupply(); }
    double ampCathode() const { return amp.cathodeVoltage(); }
    int ampQuality() const { return amp.getQuality(); }
    void setClick(double v) { click = v; }
    void setVolume(double v) { volume = v; }
    void setBalance(double v) { hornLevel = std::min(1.0, 2 * (1 - v)); drumLevel = std::min(1.0, 2 * v); }
    void setLeslie(const std::string& mode) { leslieMode = mode; for (auto& r : rot) r.tgt = mode == "fast" ? r.fast : mode == "slow" ? r.slow : 0; }
    void setRoom(const Room& r) { room = r; rebuildRoom(); }
    Room& roomRef() { return room; }
    void roomChanged() { rebuildRoom(); }
    void setCharacter(const Character& c) { chr = c; applyCharacter(); }
    void setPos(bool hasLes, double lx, double ly, bool hasLis, double hx, double hy, bool hasYaw, double yaw, bool jump);
    void setSpeakers(const Speakers& s);
    void setLfe(double v) { lfeLevel = v; }
    void setCabYaw(double v, bool jump) { cabTyaw = v; if (jump) cabYaw = v; }
    void setTaps(std::vector<Tap> taps) { tapsData = std::move(taps); if (room.shape == "zelt") rebuildRoom(); }

    RoomInfo info() const { return roomInfo; }
    double hornAngle() const { return rot[0].ang; }
    double drumAngle() const { return rot[1].ang; }
    double hornHz() const { return rot[0].hz; }
    double drumHz() const { return rot[1].hz; }

    // out[c][n], nCh <= 8
    void process(float* const* out, int nCh, int N);

    const double fs;

private:
    void init();
    void applyCharacter();
    void rebuildRoom();
    void rebuildFdnOut();
    void prepSpeakers();
    void panGains(double az, double hfrac, float* out) const;
    void cabinet(double dx, double dy, double dz, double& g, double& fc) const;
    void schedule(int m, int w, double d, double baseDelay);
    void retarget();
    void applyDelta(int m, int w, double d);
    int wheelFor(int key, int i) const { int w = key + 13 + DB_OFF[i]; if (w < 1) w += 12; while (w > 91) w -= 12; return w; }
    int pedalWheel(int key, int i) const { int w = key + 1 + (i == 1 ? 12 : 0); return std::max(1, std::min(91, w)); }
    void drawbarAmps(int man, double* amps) const;
    double kOf(double f) const { return 1 - std::exp(-TWO_PI * std::min(f, fs * 0.45) / fs); }

    static constexpr int DB_OFF[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
    static constexpr int GEARS[12][2] = {{85, 104}, {71, 82}, {67, 73}, {105, 108}, {103, 100}, {84, 77}, {74, 64}, {98, 80}, {96, 74}, {88, 64}, {67, 46}, {108, 70}};

    // Tonräder
    static constexpr int TAB = 4096;
    std::vector<float> sine, cplx;
    std::array<double, 92> phase{}, inc{}, incNew{}, incGear{};
    std::array<float, 92> percG{}, wheelLvl{}, wheelVar{}, wheelG{};
    std::array<float, 92> gain[3]{}, target[3]{};
    std::array<uint8_t, 61> keys[3]{};
    int numKeysUpper = 0;
    int drawbars[3][9] = {{8, 8, 8, 0, 0, 0, 0, 0, 0}, {0, 0, 8, 4, 0, 0, 0, 0, 0}, {6, 3, 0, 0, 0, 0, 0, 0, 0}};
    bool vibUpper = true, vibLower = false;
    double percLevelUser = 1.0, percShortS = 0.2, percLongS = 1.0; bool complexLow = true;
    bool brake = true; double hornfachLevel = 0.45, wobbleOverride = -1, masterGain = 1.0;
    amp::LeslieAmp amp;
    std::vector<float> orgBuf;
    Perc perc;
    double percEnv = 0, click = 0.5, clickEnv = 0, organGain = 0.05;
    struct Event { double t; int m; int w; double d; };
    std::vector<Event> events;
    double sampleClock = 0;
    Lcg rndFast{777};

    Character chr;
    double leakAdj = 0, leakAdj2 = 0, leakBank = 0, leakHum = 0, staggerS = 0, bounceP = 0, gainStep = 0, busK = 0, motorLevel = 0, rotorWobble = 0, sagAmt = 0;

    std::string vibMode = "c3";
    std::vector<float> vibBuf; int vibW = 0; double vibPh = 0;

    double drive = 0.35, volume = 0.8, sagEnv = 0;
    Biquad expLp, lp1, lp2, hp1, hp2, cabLp, hornRes, hornRes2, drumRes, hornHi, lfeLp1, lfeLp2, motorLp, windBp1, windBp2;
    int cabDelay = 0;
    double cabYaw = -1.5707963, cabTyaw = -1.5707963;

    Rotor rot[2];
    std::string leslieMode = "slow";
    double hornLevel = 1, drumLevel = 1;
    static constexpr int DL = 32768, DM = DL - 1;
    std::vector<float> bufH, bufD, bufC;
    int wIdx = 0;

    Room room;
    std::vector<Tap> tapsData;
    struct Pos { double x, y, tx, ty; } les{3, 6, 3, 6};
    struct Lis { double x, y, tx, ty, yaw, tyaw, z; } lis{3, 2.5, 3, 2.5, 1.5707963, 1.5707963, 1.2};
    std::vector<Path> paths;
    int nRefl = 0;
    RoomInfo roomInfo;
    double rt60[3] = {0, 0, 0};

    Speakers spk;
    double lfeLevel = 0.5;
    struct SpkS { double a; int ch; };
    std::vector<SpkS> spkSorted;
    double uniG = 1;
    static constexpr int NB = 24;
    std::vector<std::array<float, 8>> bucketG;
    std::array<float, NB + 1> bucket{};

    // FDN
    static constexpr int FM = 65535;
    std::vector<std::vector<float>> fdnBuf, apBuf, lapBuf;
    std::array<int, 16> fdnLen{}, fdnIdx{}, lapLen{}, lapIdx{};
    std::array<int, 6> apLen{}, apIdx{};
    std::array<double, 16> fdnGl{}, fdnGm{}, fdnGh{}, fdnZlo{}, fdnZhi{}, fdnModPh{}, fdnModInc{};
    double fdnModDepth = 4, fdnInGain = 0, kLoBand = 0, kHiBand = 0;
    std::vector<float> preBuf; int preIdx = 0, preLen = 480;
    double revInZ = 0, revInK = 0, revHpZ = 0, revHpK = 0;
    std::vector<std::array<float, 16>> fdnOutRows;
    std::vector<std::array<int, 16>> fdnOutOff;
    std::vector<int> fdnOutChs;
    std::array<double, 16> rv{};
    std::array<double, 8> acc{};
};

// =====================================================================================
inline void LeslieRoomDSP::init() {
    Lcg rnd(20261104u);
    sine.resize(TAB + 1); cplx.resize(TAB + 1);
    for (int i = 0; i <= TAB; i++) { double x = TWO_PI * i / TAB; sine[i] = (float) std::sin(x); cplx[i] = (float) ((std::sin(x) + 0.28 * std::sin(2 * x) + 0.14 * std::sin(3 * x) + 0.06 * std::sin(5 * x)) / 1.2); }
    for (int w = 1; w <= 91; w++) {
        int n = (w - 1) % 12, oct = (w - 1) / 12;
        int teeth = 2 << oct; const int* g = GEARS[n];
        if (w >= 85) { teeth = 192; g = GEARS[(n + 5) % 12]; }
        double fGear = 20.0 * g[0] / g[1] * teeth, fNew = 32.7032 * std::pow(2.0, (w - 1) / 12.0);
        incGear[w] = fGear / fs; incNew[w] = fNew / fs; phase[w] = rnd();
        wheelLvl[w] = (float) (w < 13 ? 1.15 : w < 49 ? 1.0 : 1 - (w - 48) / 43.0 * 0.42);
        wheelVar[w] = (float) (rnd() * 2 - 1);
    }
    applyCharacter();
    vibBuf.assign(2048, 0.f);
    expLp.set("lp", 180, 0.7, fs);
    lp1.set("lp", 800, 0.6, fs); lp2.set("lp", 4500, 0.5, fs);
    hp1.set("hp", 800, 0.6, fs); hp2.set("hp", 560, 0.7, fs);
    cabDelay = (int) std::lround(0.0017 * fs); cabLp.set("lp", 3200, 0.7, fs);
    hornRes.set("peak", 1700, 1.4, fs, 3.5); hornRes2.set("peak", 4200, 2.0, fs, 2.0); drumRes.set("peak", 120, 1.1, fs, 3.0);
    hornHi.set("lp", 9500, 0.6, fs);
    lfeLp1.set("lp", 100, 0.70710678, fs); lfeLp2.set("lp", 100, 0.70710678, fs);
    motorLp.set("lp", 90, 0.9, fs); windBp1.set("hp", 1500, 0.7, fs); windBp2.set("lp", 5000, 0.7, fs);
    rot[0] = {0, 0.75, 0.75, 0.75, 6.8, 0.19, 0.95, 0.8, 1.4};
    rot[1] = {1.3, 0.62, 0.62, 0.62, 5.7, 0.19, 0.40, 4.5, 5.5};
    bufH.assign(DL, 0.f); bufD.assign(DL, 0.f); bufC.assign(DL, 0.f);
    spk.az = {0, 45, 90, 135, 180, -135, -90, -45}; for (auto& a : spk.az) a *= 3.14159265358979 / 180; spk.ch = {0, 1, 2, 3, 4, 5, 6, 7};
    fdnBuf.assign(16, std::vector<float>(65536, 0.f)); apBuf.assign(6, std::vector<float>(4096, 0.f)); lapBuf.assign(16, std::vector<float>(2048, 0.f));
    for (int i = 0; i < 16; i++) { fdnModPh[i] = rnd(); fdnModInc[i] = (0.1 + 0.4 * rnd()) / fs; }
    kLoBand = 1 - std::exp(-TWO_PI * 250 / fs); kHiBand = 1 - std::exp(-TWO_PI * 3500 / fs);
    preBuf.assign(8192, 0.f);
    revInK = 1 - std::exp(-TWO_PI * 6000 / fs); revHpK = 1 - std::exp(-TWO_PI * 45 / fs);
    prepSpeakers();
    rebuildRoom();
}

inline void LeslieRoomDSP::applyCharacter() {
    const auto& c = chr;
    for (int w = 1; w <= 91; w++) inc[w] = c.gearTune ? incGear[w] : incNew[w];
    for (int w = 1; w <= 91; w++) wheelG[w] = (float) (wheelLvl[w] * std::pow(10.0, wheelVar[w] * 1.5 * c.cond / 20));
    leakAdj = std::pow(10.0, -(52 - 14 * c.leak) / 20); leakAdj2 = leakAdj * 0.45;
    leakBank = std::pow(10.0, -(50 - 12 * c.leak) / 20); leakHum = std::pow(10.0, -(78 - 16 * c.leak) / 20);
    staggerS = (0.0008 + 0.0045 * c.contact) * fs; bounceP = 0.15 + 0.7 * c.contact;
    gainStep = 1 / ((0.0003 + 0.0004 * (1 - c.contact)) * fs);
    busK = c.busComp ? 0.035 + 0.05 * c.cond : 0;
    motorLevel = 0.008 * c.motor; rotorWobble = wobbleOverride >= 0 ? 0.004 + 0.05 * wobbleOverride : 0.012 + 0.03 * c.cond; sagAmt = 0.25 + 0.35 * c.cond;
    amp.setAge(0.6 * c.cond);
}

inline void LeslieRoomDSP::cabinet(double dx, double dy, double dz, double& g, double& fc) const {
    double c = std::cos(cabYaw), s = std::sin(cabYaw);
    double hd = std::hypot(dx, dy); if (hd == 0) hd = 1e-6;
    double fwd = (dx * c + dy * s) / hd;
    double vert = std::fabs(dz) / std::hypot(hd, dz);
    double b = std::max(0.0, -fwd);
    g = (fwd > 0 ? 1 : 1 - 0.72 * b) * (1 - 0.35 * vert * vert);
    fc = (fwd > 0 ? 16000 : 16000 - 14000 * b * b) * (1 - 0.7 * vert * vert);
}

inline void LeslieRoomDSP::drawbarAmps(int man, double* amps) const {
    // Percussion wirkt nur auf das Obermanual: 1' stumm, „normal“ dämpft die Zugriegel etwas (wie im Original)
    double percAtt = man == 0 && perc.on && !perc.soft ? 0.72 : 1;
    int n = man == 2 ? 2 : 9;
    for (int i = 0; i < 9; i++) {
        if (i >= n || (man == 0 && i == 8 && perc.on)) { amps[i] = 0; continue; }
        double lvl = man == 2 ? 1.15 : 1.0;   // Pedal etwas kräftiger (eigene Pegelkurve im Original)
        amps[i] = drawbars[man][i] <= 0 ? 0 : std::pow(10.0, -(8 - drawbars[man][i]) * 3 / 20.0) * percAtt * lvl;
    }
}

inline void LeslieRoomDSP::schedule(int m, int w, double d, double baseDelay) {
    double t0 = sampleClock + baseDelay + rndFast() * staggerS * (m == 2 ? 1.6 : 1.0);
    events.push_back({t0, m, w, d});
    if (rndFast() < bounceP) {
        int n = rndFast() < 0.4 ? 2 : 1; double t = t0;
        for (int b = 0; b < n; b++) {
            double dt = (0.00015 + 0.0006 * rndFast()) * fs;
            events.push_back({t + dt, m, w, -d});
            events.push_back({t + dt + (0.0001 + 0.0004 * rndFast()) * fs, m, w, d});
            t += dt * 2;
        }
    }
}

inline void LeslieRoomDSP::noteOn(int k, int man) {
    int maxK = man == 2 ? 24 : 60;
    if (man < 0 || man > 2 || k < 0 || k > maxK || keys[man][k]) return;
    keys[man][k] = 1;
    if (man == 0) { bool wasEmpty = numKeysUpper == 0; numKeysUpper++; if (perc.on && wasEmpty) percEnv = perc.soft ? 0.55 : 1.0; }
    clickEnv = std::max(clickEnv, man == 2 ? 0.6 : 1.0);
    double amps[9]; drawbarAmps(man, amps);
    for (int i = 0; i < 9; i++) if (amps[i] > 0) schedule(man, man == 2 ? pedalWheel(k, i) : wheelFor(k, i), amps[i], 0);
    if (man == 0 && perc.on) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
}
inline void LeslieRoomDSP::noteOff(int k, int man) {
    int maxK = man == 2 ? 24 : 60;
    if (man < 0 || man > 2 || k < 0 || k > maxK || !keys[man][k]) return;
    keys[man][k] = 0;
    if (man == 0) numKeysUpper--;
    clickEnv = std::max(clickEnv, 0.35);
    double amps[9]; drawbarAmps(man, amps);
    for (int i = 0; i < 9; i++) if (amps[i] > 0) schedule(man, man == 2 ? pedalWheel(k, i) : wheelFor(k, i), -amps[i], 0);
    if (man == 0 && perc.on) { int w = wheelFor(k, perc.third ? 4 : 3); percG[w] = std::max(0.f, percG[w] - 1); }
}
inline void LeslieRoomDSP::retarget() {
    events.clear(); std::fill(percG.begin(), percG.end(), 0.f);
    for (int m = 0; m < 3; m++) {
        std::fill(target[m].begin(), target[m].end(), 0.f);
        double amps[9]; drawbarAmps(m, amps);
        int maxK = m == 2 ? 24 : 60;
        for (int k = 0; k <= maxK; k++) {
            if (!keys[m][k]) continue;
            for (int i = 0; i < 9; i++) if (amps[i] > 0) applyDelta(m, m == 2 ? pedalWheel(k, i) : wheelFor(k, i), amps[i]);
            if (m == 0 && perc.on) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
        }
    }
}
inline void LeslieRoomDSP::applyDelta(int m, int w, double d) {
    auto& t = target[m];
    t[w] += (float) d;
    float la = (float) (d * leakAdj), lb = (float) (d * leakAdj2), lc = (float) (d * leakBank);
    if (w > 1) t[w - 1] += la;
    if (w < 91) t[w + 1] += la;
    if (w > 2) t[w - 2] += lb;
    if (w < 90) t[w + 2] += lb;
    if (w > 48) t[w - 48] += lc;
    if (w <= 43) t[w + 48] += lc;
}

inline void LeslieRoomDSP::setPos(bool hasLes, double lx, double ly, bool hasLis, double hx, double hy, bool hasYaw, double yaw, bool jump) {
    if (hasLes) { les.tx = lx; les.ty = ly; }
    if (hasLis) { lis.tx = hx; lis.ty = hy; if (hasYaw) lis.tyaw = yaw; }
    if (jump) { les.x = les.tx; les.y = les.ty; lis.x = lis.tx; lis.y = lis.ty; lis.yaw = lis.tyaw; }
}
inline void LeslieRoomDSP::setSpeakers(const Speakers& s) {
    spk = s; for (auto& a : spk.az) a *= 3.14159265358979 / 180;
    prepSpeakers(); rebuildFdnOut();
    for (auto& p : paths) { p.gPrev.fill(0); p.gNext.fill(0); p.init = false; }
}

inline void LeslieRoomDSP::rebuildRoom() {
    auto clampd = [](double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); };
    auto& R = room;
    les.tx = clampd(les.tx, 0.3, R.Lx - 0.3); les.ty = clampd(les.ty, 0.3, R.Ly - 0.3); les.x = clampd(les.x, 0.3, R.Lx - 0.3); les.y = clampd(les.y, 0.3, R.Ly - 0.3);
    lis.tx = clampd(lis.tx, 0.3, R.Lx - 0.3); lis.ty = clampd(lis.ty, 0.3, R.Ly - 0.3); lis.x = clampd(lis.x, 0.3, R.Lx - 0.3); lis.y = clampd(lis.y, 0.3, R.Ly - 0.3);
    double mat[3] = {1.35, 1.0, 0.8};
    if (R.material == "zelt") { mat[0] = 1.9; mat[2] = 0.75; } else if (R.material == "beton") { mat[0] = 0.55; mat[2] = 1.1; }
    else if (R.material == "stein") { mat[0] = 0.6; mat[2] = 1.35; } else if (R.material == "glas") { mat[0] = 0.9; mat[2] = 1.6; }
    double ab[3];
    for (int i = 0; i < 3; i++) ab[i] = R.hasAbsBands ? clampd(R.absBands[i], 0.03, 0.97) : clampd(R.absorb * mat[i], 0.03, 0.95);
    double reflG = std::sqrt(1 - ab[1]), hfRefl = std::sqrt((1 - ab[2]) / (1 - ab[1]));
    std::vector<Path> old = std::move(paths);
    std::vector<Path> np;
    bool tent = R.shape == "zelt";
    if (tent) {
        for (int r = 0; r < 2; r++) {
            Path d; d.rotor = r; d.order = 0; d.kOn = kOf(14000); d.kOff = kOf(1500); d.kDrum = kOf(18000); np.push_back(d);
            std::vector<Tap> sorted = tapsData;
            std::sort(sorted.begin(), sorted.end(), [](const Tap& x, const Tap& y) { return y.E[1] < x.E[1]; });
            size_t cap = r == 0 ? 190 : 70; if (sorted.size() > cap) sorted.resize(cap);
            for (auto& t : sorted) {
                double gm = std::sqrt(std::max(t.E[1], 0.0)), gl = std::sqrt(std::max(t.E[0], 0.0)), gh = std::sqrt(std::max(t.E[2], 0.0));
                if (gm < 1e-5 && gl < 1e-5) continue;
                double hfRatio = gm > 0 ? std::min(1.0, gh / gm) : 0;
                Path p; p.rotor = r; p.order = t.order; p.trace = true; p.fast = true; p.dist = t.t * C_SOUND;
                p.dep[0] = t.dep[0]; p.dep[1] = t.dep[1]; p.dep[2] = t.dep[2]; p.azRoom = t.az; p.el = t.el;
                p.gm = gm; p.glr = gm > 0 ? gl / gm : 1; p.kTrace = kOf(1500 + 14000 * hfRatio * hfRatio);
                np.push_back(p);
            }
        }
    } else {
        for (int r = 0; r < 2; r++)
            for (int nx = -3; nx <= 3; nx++) for (int px = 0; px <= 1; px++) {
                int ox = std::abs(2 * nx - px);
                for (int ny = -3; ny <= 3; ny++) for (int py = 0; py <= 1; py++) {
                    int oy = std::abs(2 * ny - py);
                    for (int nz = -3; nz <= 3; nz++) for (int pz = 0; pz <= 1; pz++) {
                        int oz = std::abs(2 * nz - pz);
                        int order = ox + oy + oz;
                        if (order > R.order) continue;
                        double hf = std::pow(0.62, order) * std::pow(hfRefl, order);
                        Path p; p.rotor = r; p.order = order;
                        p.sx = 1 - 2 * px; p.sy = 1 - 2 * py; p.sz = 1 - 2 * pz;
                        p.ox = 2 * nx * R.Lx; p.oy = 2 * ny * R.Ly; p.oz = 2 * nz * R.Lz;
                        p.g = std::pow(reflG, order) * (order > 0 ? 0.9 : 1);
                        p.kOn = kOf(14000 * hf); p.kOff = kOf(1500 * hf); p.kDrum = kOf(std::min(20000.0, 6000 * hf * 3));
                        p.lin = order > 1; p.fast = order > 1;
                        p.jit = order > 0 ? (((r * 977 + (int) np.size() * 131) % 97) / 97.0) * 0.9 * order : 0;
                        np.push_back(p);
                    }
                }
            }
    }
    if (old.size() == np.size()) for (size_t i = 0; i < np.size(); i++) { np[i].gPrev = old[i].gPrev; np[i].gNext = old[i].gNext; np[i].init = old[i].init; np[i].lp = old[i].lp; }
    paths = std::move(np);
    nRefl = 0; for (auto& p : paths) if (p.order > 0) nRefl++; nRefl /= 2;

    double V = tent && R.tentV > 0 ? R.tentV : R.Lx * R.Ly * R.Lz;
    double S = tent && R.tentS > 0 ? R.tentS : 2 * (R.Lx * R.Ly + R.Lx * R.Lz + R.Ly * R.Lz);
    double rt[3];
    for (int i = 0; i < 3; i++) rt[i] = clampd(0.161 * V / (-S * std::log(1 - ab[i]) + (i == 2 ? 4 * 0.004 * V : 0)), 0.12, 12);
    rt[0] = std::min(rt[0], rt[1] * 1.25); rt[2] = std::min(rt[2], rt[1]);
    for (int i = 0; i < 3; i++) rt60[i] = rt[i];
    double A = -S * std::log(1 - ab[1]);
    double rc2 = A / (16 * 3.14159265358979);
    double mfp = 4 * V / S / C_SOUND;
    double tCover = tent ? 0.09 : clampd((R.order + 0.5) * mfp, 0.012, 0.25);
    preLen = std::max(32, std::min(8191, (int) std::lround(tCover * 0.55 * fs)));
    if (preIdx >= preLen) preIdx = 0;
    double base = clampd(mfp, 0.026, 0.08);
    auto isPrime = [](int n) { if (n < 2) return false; for (int d = 2; d * d <= n; d++) if (n % d == 0) return false; return true; };
    auto toPrime = [&](int n) { while (!isPrime(n)) n++; return n; };
    double lenSum = 0;
    for (int i = 0; i < 16; i++) {
        double frac = 0.6 + 1.6 * i / 15.0;
        int len = toPrime(std::min(60000, (int) std::lround(base * frac * fs * (1 + 0.017 * ((i * 7) % 5)))));
        fdnLen[i] = len; lenSum += len;
        auto gOf = [&](double t) { return std::pow(10.0, -3.0 * len / (fs * t)); };
        fdnGl[i] = gOf(rt[0]); fdnGm[i] = gOf(rt[1]); fdnGh[i] = gOf(rt[2]);
        if (fdnIdx[i] >= len) fdnIdx[i] = 0;
    }
    double lenAvg = lenSum / 16;
    double gBar2 = std::pow(10.0, -6 * lenAvg / (fs * rt[1]));
    double frac = std::exp(-13.8 * tCover * 0.55 / rt[1]);
    double Etarget = 0.232 / rc2 * frac;
    fdnInGain = std::sqrt(Etarget * (1 - gBar2) / 16);
    fdnModDepth = 5 + 9 * clampd((rt[1] - 0.4) / 3, 0, 1);
    const double apMs[6] = {1.3, 2.1, 3.2, 4.7, 6.3, 8.1}; double apScale = clampd(rt[1] / 0.8, 0.5, 4);
    for (int i = 0; i < 6; i++) { apLen[i] = toPrime(std::min(4090, (int) std::lround(apMs[i] * apScale * fs / 1000))); if (apIdx[i] >= apLen[i]) apIdx[i] = 0; }
    const double lapMs[16] = {2.1, 2.9, 3.7, 4.6, 5.3, 6.1, 7.0, 7.9, 8.7, 9.6, 10.4, 11.3, 12.1, 13.0, 13.8, 14.7};
    for (int i = 0; i < 16; i++) { lapLen[i] = toPrime(std::min(2040, (int) std::lround(lapMs[(i * 5) % 16] * 0.6 * clampd(apScale, 0.6, 2) * fs / 1000))); if (lapIdx[i] >= lapLen[i]) lapIdx[i] = 0; }
    rebuildFdnOut();
    roomInfo.rt60[0] = rt[0]; roomInfo.rt60[1] = rt[1]; roomInfo.rt60[2] = rt[2]; roomInfo.rc = std::sqrt(rc2); roomInfo.paths = (int) paths.size();
}

inline void LeslieRoomDSP::rebuildFdnOut() {
    int n = (int) spk.az.size();
    fdnOutRows.assign(n, {}); fdnOutOff.assign(n, {}); fdnOutChs = spk.ch;
    static const int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599};
    const int NP = sizeof(primes) / sizeof(int);
    double sc = fs / 48000;
    for (int c = 0; c < n; c++) {
        int r = c + 1;
        for (int i = 0; i < 16; i++) { int bits = r & i, par = 0; while (bits) { par ^= bits & 1; bits >>= 1; } fdnOutRows[c][i] = par ? -0.25f : 0.25f; fdnOutOff[c][i] = (int) std::lround(primes[(c * 37 + i * 11 + 5) % NP] * sc); }
    }
}

inline void LeslieRoomDSP::prepSpeakers() {
    int n = (int) spk.az.size();
    spkSorted.clear();
    for (int i = 0; i < n; i++) { double a = spk.az[i]; while (a < 0) a += TWO_PI; while (a >= TWO_PI) a -= TWO_PI; spkSorted.push_back({a, spk.ch[i]}); }
    std::sort(spkSorted.begin(), spkSorted.end(), [](const SpkS& p, const SpkS& q) { return p.a < q.a; });
    uniG = 1 / std::sqrt((double) n);
    bucketG.assign(NB + 1, {});
    for (int k = 0; k <= NB; k++) { if (k < NB) panGains(k * TWO_PI / NB, 1, bucketG[k].data()); else panGains(0, 0, bucketG[k].data()); }
}

inline void LeslieRoomDSP::panGains(double az, double hfrac, float* out) const {
    for (int i = 0; i < 8; i++) out[i] = 0;
    double uni = (1 - hfrac) * uniG;
    if (spk.stereo) {
        double p = std::max(-1.0, std::min(1.0, std::sin(az) / std::sin(3.14159265358979 / 6) * 0.5));
        double t = (1 - p) / 2 * 3.14159265358979 / 2;
        out[spk.ch[0]] = (float) (hfrac * std::cos(t) + uni); out[spk.ch[1]] = (float) (hfrac * std::sin(t) + uni);
        return;
    }
    int n = (int) spkSorted.size();
    for (int i = 0; i < n; i++) out[spkSorted[i].ch] += (float) uni;
    while (az < 0) az += TWO_PI; while (az >= TWO_PI) az -= TWO_PI;
    int i = 0; while (i < n && spkSorted[i].a <= az) i++;
    const SpkS& A = spkSorted[(i - 1 + n) % n]; const SpkS& B = spkSorted[i % n];
    double span = B.a - A.a; while (span <= 0) span += TWO_PI;
    if (span >= 3.14159265358979) {
        double dA = az - A.a; while (dA < 0) dA += TWO_PI; double f = dA / span, w = std::sin(f * 3.14159265358979);
        out[A.ch] += (float) (hfrac * std::cos(f * 3.14159265358979 / 2) * (1 - 0.3 * w)); out[B.ch] += (float) (hfrac * std::sin(f * 3.14159265358979 / 2) * (1 - 0.3 * w));
        return;
    }
    double d = az - A.a; while (d < 0) d += TWO_PI;
    double t = d / span * 3.14159265358979 / 2;
    out[A.ch] += (float) (hfrac * std::cos(t)); out[B.ch] += (float) (hfrac * std::sin(t));
}

inline void LeslieRoomDSP::process(float* const* out, int nCh, int N) {
    const double PI = 3.14159265358979;
    double a = 1 - std::exp(-N / (0.12 * fs));
    auto& L = les; auto& H = lis;
    L.x += (L.tx - L.x) * a; L.y += (L.ty - L.y) * a; H.x += (H.tx - H.x) * a; H.y += (H.ty - H.y) * a;
    double dy = H.tyaw - H.yaw; while (dy > PI) dy -= TWO_PI; while (dy < -PI) dy += TWO_PI; H.yaw += dy * a;
    double dcy = cabTyaw - cabYaw; while (dcy > PI) dcy -= TWO_PI; while (dcy < -PI) dcy += TWO_PI; cabYaw += dcy * a;

    double bt = N / fs;
    for (auto& r : rot) { double tau = r.tgt > r.hz ? r.upTau : r.dnTau; if (r.tgt == 0) tau *= brake ? 0.45 : 1.6; r.hz += (r.tgt - r.hz) * (1 - std::exp(-bt / tau)); if (r.tgt == 0 && r.hz < 0.02) r.hz = 0; }

    float tmp[8];
    double midAng[2] = {rot[0].ang + PI * rot[0].hz * bt, rot[1].ang + PI * rot[1].hz * bt};
    for (auto& p : paths) {
        const Rotor& r = rot[p.rotor];
        if (p.trace) {
            bool horn = p.rotor == 0;
            double a0 = r.ang, a1 = r.ang + TWO_PI * r.hz * bt;
            for (int e = 0; e < 2; e++) {
                double an = e ? a1 : a0, c = std::cos(an), sn = std::sin(an);
                double cosE = p.dep[0] * c + p.dep[1] * sn;
                double d = p.dist - r.r * cosE, u = 0.5 + 0.5 * cosE;
                double g = (horn ? 0.1 + 0.9 * u * u : 0.4 + 0.6 * u) * p.gm;
                if (e) { p.d1 = d; p.g1 = g; p.kb = std::min(horn ? kOf(1500) + (p.kTrace - kOf(1500)) * u * u : p.kTrace, kOf(std::max(2500.0, 260000 / std::max(d, 1.0)))); }
                else { p.d0 = d; p.g0 = g; }
            }
            int k = (int) std::lround((p.azRoom - H.yaw) / TWO_PI * NB) % NB; if (k < 0) k += NB;
            p.bP = p.init ? p.bN : k; p.bN = k; p.hf = std::min(1.0, std::sqrt(std::max(0.0, 1 - p.el * p.el)) * 1.15); p.init = true;
            continue;
        }
        p.icx = p.sx * L.x + p.ox; p.icy = p.sy * L.y + p.oy; p.iz = p.sz * r.h + p.oz;
        p.gPrev = p.gNext;
        double ca = std::cos(midAng[p.rotor]), sa = std::sin(midAng[p.rotor]);
        double vx = p.icx + p.sx * r.r * ca - H.x, vy = p.icy + p.sy * r.r * sa - H.y, vz = p.iz - H.z;
        double hd = std::hypot(vx, vy), dist = std::hypot(hd, vz);
        p.kAir = kOf(std::max(2500.0, 260000 / std::max(dist, 1.0)));
        double cg, cfc; cabinet(-p.sx * vx, -p.sy * vy, -p.sz * vz, cg, cfc);
        p.cab0 = p.cab1 != 0 ? p.cab1 : cg; p.cab1 = cg; p.kc = std::min(kOf(cfc), p.kAir);
        if (p.fast) {
            double a0 = r.ang, a1 = r.ang + TWO_PI * r.hz * bt; bool horn = p.rotor == 0;
            for (int e = 0; e < 2; e++) {
                double an = e ? a1 : a0, c = std::cos(an), sn = std::sin(an);
                double dxp = p.sx * c, dyp = p.sy * sn;
                double wx = H.x - (p.icx + dxp * r.r), wy = H.y - (p.icy + dyp * r.r), wz = H.z - p.iz;
                double d = std::sqrt(wx * wx + wy * wy + wz * wz) + p.jit;
                double cosE = (dxp * wx + dyp * wy) / d, u = 0.5 + 0.5 * cosE;
                double g = (horn ? 0.3 + 0.7 * u : 1) * p.g / (d > 0.3 ? d : 0.3);
                if (e) { p.d1 = d; p.g1 = g; p.dl1 = u; } else { p.d0 = d; p.g0 = g; p.dl0 = u; }
                if (e) {
                    double k;
                    if (horn) { double th = std::acos(std::max(-1.0, std::min(1.0, cosE))); double s2 = th <= PI / 2 ? std::sin(th) : 1 + (th - PI / 2) / (PI / 2); k = kOf(std::min(16000.0, C_SOUND / (PI * 0.045 * std::max(s2, 0.06)))); }
                    else k = p.kDrum;
                    p.kb = std::min(k, p.kc);
                }
            }
        }
        double az = std::atan2(vy, vx) - H.yaw;
        double hfr = std::min(1.0, hd / std::max(dist, 1e-6) * 1.15);
        if (p.fast) { int k = (int) std::lround(az / TWO_PI * NB) % NB; if (k < 0) k += NB; p.bP = p.init ? p.bN : k; p.bN = k; p.hf = hfr; p.init = true; }
        else { panGains(az, hfr, tmp); for (int c = 0; c < 8; c++) p.gNext[c] = tmp[c]; if (!p.init) { p.gPrev = p.gNext; p.init = true; } }
    }
    for (int c = 0; c < nCh; c++) std::fill(out[c], out[c] + N, 0.f);

    const double gs = gainStep;
    const double percDecay = std::exp(-1 / ((perc.fast ? percShortS : percLongS) * fs / 6.9));
    const double clickDecay = std::exp(-1 / (0.002 * fs));
    const double percLevel = (perc.soft ? 0.6 : 1.0) * percLevelUser;
    double vibDepth = 0; if (vibMode == "v1" || vibMode == "c1") vibDepth = 0.00022; else if (vibMode == "v2" || vibMode == "c2") vibDepth = 0.00045; else if (vibMode == "v3" || vibMode == "c3") vibDepth = 0.0008;
    vibDepth *= fs;
    const bool vibChorus = vibMode[0] == 'c';
    const double vibInc = 6.87 / fs;
    const double vol = volume, expBass = (1 - vol) * 0.9 * vol * 2, expGain = 0.08 + 0.92 * vol * vol;
    const double fsc = fs / C_SOUND;
    Rotor& rh = rot[0]; Rotor& rd = rot[1];
    const double wob = rotorWobble;
    const double revGain = fdnInGain * room.reverb;
    const int nSpk = (int) fdnOutRows.size();
    const int lfeIdx = spk.lfe;
    const double master = 6.5 * masterGain;
    const double kDrumLo = 1 - std::exp(-TWO_PI * 200 / fs), kScale = TWO_PI / fs;
    const double cabC = std::cos(cabYaw), cabS = std::sin(cabYaw);
    const size_t np = paths.size();

    // ---------- 1) Orgel: Tonräder, Manuale, Vibrato, Schweller → orgBuf ----------
    if ((int) orgBuf.size() < N) orgBuf.assign((size_t) N, 0.f);
    for (int n = 0; n < N; n++) {
        if (!events.empty()) {
            double now = sampleClock;
            for (size_t i = 0; i < events.size();) { if (events[i].t <= now) { applyDelta(events[i].m, events[i].w, events[i].d); events[i] = events.back(); events.pop_back(); } else i++; }
        }
        sampleClock++;

        double sU = 0, sL = 0, sP = 0, ps = 0, load = 0, hum = 0;
        for (int w = 1; w <= 91; w++) {
            double ph = phase[w] + inc[w]; if (ph >= 1) ph -= 1; phase[w] = ph;
            float g0 = gain[0][w], g1 = gain[1][w], g2 = gain[2][w];
            const float t0 = target[0][w], t1 = target[1][w], t2 = target[2][w];
            if (g0 != t0) { g0 = g0 < t0 ? std::min(t0, (float) (g0 + gs)) : std::max(t0, (float) (g0 - gs)); gain[0][w] = g0; }
            if (g1 != t1) { g1 = g1 < t1 ? std::min(t1, (float) (g1 + gs)) : std::max(t1, (float) (g1 - gs)); gain[1][w] = g1; }
            if (g2 != t2) { g2 = g2 < t2 ? std::min(t2, (float) (g2 + gs * 0.6)) : std::max(t2, (float) (g2 - gs * 0.6)); gain[2][w] = g2; }
            const float pg = percG[w];
            const double gsum = (double) g0 + g1 + g2;
            if (gsum + leakHum > 1e-6 || pg > 0) {
                double x = ph * TAB; int i = (int) x; double f = x - i;
                const float* tab = (w <= 12 && complexLow) ? cplx.data() : sine.data();
                double v = (tab[i] + (tab[i + 1] - tab[i]) * f) * wheelG[w];
                sU += v * g0; sL += v * g1; sP += v * g2; ps += v * pg; hum += v; load += gsum;
            }
        }
        double comp = 1 / (1 + busK * load);
        double sVib = ((vibUpper ? sU : 0) + (vibLower ? sL : 0)) * comp;
        double sDry = ((vibUpper ? 0 : sU) + (vibLower ? 0 : sL) + sP) * comp + hum * leakHum;
        // Percussion läuft am Vibrato vorbei
        sDry += ps * percEnv * percLevel * 0.9;
        percEnv *= percDecay;
        if (clickEnv > 1e-4) { sDry += (rndFast() * 2 - 1) * clickEnv * click * 0.35; clickEnv *= clickDecay; }
        sVib *= organGain; sDry *= organGain;

        double s = sDry;
        if (vibDepth > 0) {
            vibBuf[vibW] = (float) sVib;
            vibPh += vibInc; if (vibPh >= 1) vibPh -= 1;
            double tri = 1 - std::fabs(2 * vibPh - 1), stepped = std::round(tri * 16) / 16;
            double d = 20 + vibDepth * 2 * (0.75 * tri + 0.25 * stepped);
            double rp = vibW - d; while (rp < 0) rp += 2048;
            int ri = (int) rp; double rf = rp - ri;
            double wet = vibBuf[ri] + (vibBuf[(ri + 1) & 2047] - vibBuf[ri]) * rf;
            vibW = (vibW + 1) & 2047;
            s += vibChorus ? 0.6 * sVib + 0.6 * wet : wet;
        } else s += sVib;
        s = (s + expLp.run(s) * expBass) * expGain;
        orgBuf[(size_t) n] = (float) s;
    }

    // ---------- 2) Leslie-Endstufe (Schaltung, eigenes Oversampling) ----------
    amp.process(orgBuf.data(), N);

    // ---------- 3) Weiche, Rotoren, Raum ----------
    for (int n = 0; n < N; n++) {
        double s = orgBuf[(size_t) n];
        double lo = drumRes.run(lp2.run(lp1.run(s))) * drumLevel;
        double hi = hornHi.run(hornRes2.run(hornRes.run(hp2.run(hp1.run(s))))) * hornLevel;
        if (motorLevel > 0) {
            double nz = rndFast() * 2 - 1;
            lo += motorLp.run(nz) * motorLevel * (0.3 + rd.hz / rd.fast) * 3;
            hi += windBp2.run(windBp1.run(nz)) * motorLevel * (rh.hz / rh.fast) * 0.25;
        }
        const int wi = wIdx;
        bufH[wi] = (float) hi; bufD[wi] = (float) lo; bufC[wi] = (float) cabLp.run(hi);

        rh.ang += rh.hz / fs * TWO_PI * (1 + wob * std::sin(rh.ang)); if (rh.ang > TWO_PI) rh.ang -= TWO_PI;
        rd.ang += rd.hz / fs * TWO_PI * (1 + wob * std::sin(rd.ang + 1)); if (rd.ang > TWO_PI) rd.ang -= TWO_PI;
        const double chc = std::cos(rh.ang), chs = std::sin(rh.ang), cdc = std::cos(rd.ang), cds = std::sin(rd.ang);
        const double tt = (n + 1.0) / N;

        acc.fill(0); bucket.fill(0);
        double revIn = 0, lfeIn = 0;

        for (size_t q = 0; q < np; q++) {
            Path& p = paths[q];
            if (p.fast) {
                double d = p.d0 + (p.d1 - p.d0) * tt, g = (p.g0 + (p.g1 - p.g0) * tt) * (p.cab0 + (p.cab1 - p.cab0) * tt);
                double rp = wi - d * fsc; if (rp < 0) rp += DM + 1;
                int i1 = (int) rp; double fr = rp - i1;
                bool hornP = p.rotor == 0;
                const float* buf = hornP ? bufH.data() : bufD.data();
                double y1 = buf[i1 & DM];
                double xin = y1 + (buf[(i1 + 1) & DM] - y1) * fr;
                if (!hornP) { double u = p.dl0 + (p.dl1 - p.dl0) * tt; p.zd += kDrumLo * (xin - p.zd); xin = p.zd * (0.85 + 0.15 * u) + (xin - p.zd) * (0.3 + 0.7 * u); }
                if (p.trace) { p.zlo += kLoBand * (xin - p.zlo); xin += (p.glr - 1) * p.zlo; }
                p.lp += p.kb * (xin - p.lp);
                double v = p.lp * g;
                revIn += v * d;
                if (p.rotor) lfeIn += v;
                double vh = v * p.hf;
                bucket[p.bP] += (float) (vh * (1 - tt)); bucket[p.bN] += (float) (vh * tt); bucket[NB] += (float) (v - vh);
                continue;
            }
            bool horn = p.rotor == 0;
            double ca = horn ? chc : cdc, sa = horn ? chs : cds, rr = horn ? rh.r : rd.r;
            double dxp = p.sx * ca, dyp = p.sy * sa;
            double vx = H.x - (p.icx + dxp * rr), vy = H.y - (p.icy + dyp * rr), vz = H.z - p.iz;
            double dist = std::sqrt(vx * vx + vy * vy + vz * vz) + p.jit;
            double cosE = (dxp * vx + dyp * vy) / dist, u = 0.5 + 0.5 * cosE;
            double rp = wi - dist * fsc; if (rp < 0) rp += DM + 1;
            int i1 = (int) rp; double fr = rp - i1;
            const float* buf = horn ? bufH.data() : bufD.data();
            double v;
            if (p.lin) { double y1 = buf[i1 & DM]; v = y1 + (buf[(i1 + 1) & DM] - y1) * fr; }
            else {
                double y0 = buf[(i1 - 1) & DM], y1 = buf[i1 & DM], y2 = buf[(i1 + 1) & DM], y3 = buf[(i1 + 2) & DM];
                double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
                v = ((c3 * fr + c2) * fr + c1) * fr + y1;
            }
            double dirG, k;
            if (horn) {
                dirG = 0.3 + 0.7 * u;
                double th = std::acos(std::max(-1.0, std::min(1.0, cosE)));
                double s2 = th <= PI / 2 ? std::sin(th) : 1 + (th - PI / 2) / (PI / 2);
                double fc = std::min(16000.0, C_SOUND / (PI * 0.045 * std::max(s2, 0.06)));
                k = 1 - std::exp(-fc * kScale);
                if (p.order == 0 || p.order == 1) {
                    double backness = std::max(0.0, -(dxp * cabC + dyp * cabS));
                    if (backness > 0) { double rp2 = rp - cabDelay; if (rp2 < 0) rp2 += DM + 1; v += bufC[((int) rp2) & DM] * backness * hornfachLevel / std::max(0.05, dirG); }
                }
            } else {
                p.zd += kDrumLo * (v - p.zd);
                v = p.zd * (0.85 + 0.15 * u) + (v - p.zd) * (0.3 + 0.7 * u);
                dirG = 1; k = p.kDrum;
            }
            if (p.kc < k) k = p.kc;
            p.lp += k * (v - p.lp); v = p.lp;
            v *= dirG * p.g * (p.cab0 + (p.cab1 - p.cab0) * tt);
            if (p.order > 0) revIn += v;
            v /= (dist > 0.3 ? dist : 0.3);
            if (!horn) lfeIn += v;
            for (int c = 0; c < nCh; c++) { double gg = p.gPrev[c] + (p.gNext[c] - p.gPrev[c]) * tt; if (gg != 0) acc[c] += v * gg; }
        }
        for (int k = 0; k <= NB; k++) { double bv = bucket[k]; if (bv == 0) continue; const auto& bg = bucketG[k]; for (int c = 0; c < nCh; c++) acc[c] += bv * bg[c]; }

        revInZ += revInK * ((hi + lo) - revInZ);
        revHpZ += revHpK * (revInZ - revHpZ);
        preBuf[preIdx] = (float) (revInZ - revHpZ);
        int pi = preIdx + 1; if (pi >= preLen) pi = 0; preIdx = pi;
        double inj = preBuf[pi] * revGain;
        for (int i = 0; i < 6; i++) { auto& b = apBuf[i]; int ix = apIdx[i]; double dl = b[ix]; double x = inj - 0.68 * dl; b[ix] = (float) x; inj = dl + 0.68 * x; int ni = ix + 1; if (ni >= apLen[i]) ni = 0; apIdx[i] = ni; }
        for (int i = 0; i < 16; i++) {
            double ph = fdnModPh[i] + fdnModInc[i]; if (ph >= 1) ph -= 1; fdnModPh[i] = ph;
            double off = fdnLen[i] - 2 - fdnModDepth * (0.5 + 0.5 * std::sin(TWO_PI * ph));
            double rp = fdnIdx[i] - off; if (rp < 0) rp += 65536;
            int ri = (int) rp; double rf = rp - ri;
            const auto& b = fdnBuf[i];
            double y = b[ri & FM] + (b[(ri + 1) & FM] - b[ri & FM]) * rf;
            { auto& lb = lapBuf[i]; int lx = lapIdx[i]; double dl = lb[lx]; double x = y - 0.55 * dl; lb[lx] = (float) x; y = dl + 0.55 * x; int ni = lx + 1; if (ni >= lapLen[i]) ni = 0; lapIdx[i] = ni; }
            fdnZlo[i] += kLoBand * (y - fdnZlo[i]);
            double mid = y - fdnZlo[i];
            fdnZhi[i] += kHiBand * (mid - fdnZhi[i]);
            rv[i] = fdnZlo[i] * fdnGl[i] + fdnZhi[i] * fdnGm[i] + (mid - fdnZhi[i]) * fdnGh[i];
        }
        for (int h = 1; h < 16; h <<= 1) for (int i = 0; i < 16; i += h << 1) for (int j = i; j < i + h; j++) { double x = rv[j], y = rv[j + h]; rv[j] = x + y; rv[j + h] = x - y; }
        for (int c = 0; c < nSpk; c++) {
            const auto& row = fdnOutRows[c]; const auto& offs = fdnOutOff[c]; double o = 0;
            for (int i = 0; i < 16; i++) o += fdnBuf[i][(fdnIdx[i] - fdnLen[i] + 2 + offs[i]) & FM] * row[i];
            int ch = fdnOutChs[c]; if (ch < 8) acc[ch] += o;
        }
        for (int i = 0; i < 16; i++) { fdnBuf[i][fdnIdx[i]] = (float) (rv[i] * 0.25 + inj * (0.8 + 0.2 * ((i * 7) % 5) / 4.0)); fdnIdx[i] = (fdnIdx[i] + 1) & FM; }

        if (lfeIdx >= 0 && lfeIdx < 8) acc[lfeIdx] = lfeLp2.run(lfeLp1.run(lfeIn)) * lfeLevel * 2;

        for (int c = 0; c < nCh; c++) {
            double x = acc[c] * master;
            out[c][n] = (float) (x > 0.9 ? 0.9 + 0.1 * std::tanh((x - 0.9) * 10) : x < -0.9 ? -0.9 + 0.1 * std::tanh((x + 0.9) * 10) : x);
        }
        wIdx = (wi + 1) & DM;
    }
}

} // namespace leslie
