// Wheelhouse - DSP core (C++ port of processor7.js)
// Signal chain: tonewheel organ -> scanner vibrato -> expression (swell) pedal -> Leslie amplifier
// -> crossover -> rotating sources placed in a room (image sources / ray-traced taps, Doppler,
// cabinet directivity) -> 16-line feedback delay network (FDN) reverb -> speaker panning.
// This file deliberately contains no JUCE code so the core can be built and tested without the framework.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include "LeslieAmp.h"
#include "OrganPreamp.h"

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
        else if (type[0] == 's') {   // low shelf (RBJ cookbook)
            double A = std::pow(10.0, gainDb / 40), sA = 2 * std::sqrt(A) * al;
            B0 = A * ((A + 1) - (A - 1) * cw + sA); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sA);
            A0 = (A + 1) + (A - 1) * cw + sA; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sA;
        }
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
    // Fields used only by ray-traced taps (tent shape): fixed distance, departure direction, arrival azimuth/elevation
    double dist = 0, dep[3] = {0, 0, 0}, azRoom = 0, el = 0, gm = 0, glr = 1, kTrace = 1;
};

struct Rotor { double ang, hz, tgt, slow, fast, r, h, upTau, dnTau; };

struct Room {
    double Lx = 6, Ly = 8, Lz = 2.8, absorb = 0.25, reverb = 1, tentVolume = 0, tentSurface = 0;
    int order = 2;
    std::string material = "wood", shape = "box";
    bool hasAbsBands = false; double absBands[3] = {0, 0, 0};
};

struct Character { double cond = 0.5, leak = 0.5, contact = 0.5, motor = 0.4; bool gearTune = true, busComp = true; };
struct Perc { bool on = true, third = true, fast = true, soft = false; };
struct Speakers { std::vector<double> az; std::vector<int> ch; int lfe = -1; bool stereo = false; };

struct RoomInfo { double rt60[3] = {0, 0, 0}; double rc = 0; int paths = 0; };

} // namespace leslie
#include "MicRig.h"
#include "SpringReverb.h"
namespace leslie {

class LeslieRoomDSP {
public:
    explicit LeslieRoomDSP(double sampleRate) : fs(sampleRate), amp(sampleRate), pre(sampleRate), spring(sampleRate) { init(); }

    // ---------------- Control API (mirrors the messages of the original audio worklet) ----------------
    // Manuals: 0 = upper (swell), 1 = lower (great), 2 = pedal
    /** Key down. k = key index (0..60 for manuals, 0..24 for pedals), man = manual (0/1/2). */
    void noteOn(int k, int man = 0);
    /** Key up. Same arguments as noteOn(). */
    void noteOff(int k, int man = 0);
    /** All notes off: clears held keys, pending contact events, tonewheel targets and percussion. */
    void panic() {
        for (int m = 0; m < 3; m++) { std::fill(keys[m].begin(), keys[m].end(), 0); std::fill(target[m].begin(), target[m].end(), 0.f); }
        numKeysUpper = 0; events.clear(); std::fill(percG.begin(), percG.end(), 0.f);
    }
    /** Drawbar positions 0..8 for one manual (9 values; the pedal uses only the first 2). */
    void setDrawbars(const int* v, int man = 0) { int n = man == 2 ? 2 : 9; for (int i = 0; i < n; i++) drawbars[man][i] = v[i]; retarget(); }
    /** Percussion switches: on/off, harmonic (third = 4' / second = 2 2/3'), decay fast/slow, volume soft/normal. */
    void setPerc(const Perc& p) { perc = p; retarget(); }
    /** Routes the scanner vibrato to the upper and/or lower manual (the vibrato tabs on the console). */
    void setVibRouting(bool upper, bool lower) { vibUpper = upper; vibLower = lower; }
    /** Vibrato/chorus knob position: "v1".."v3" (vibrato) or "c1".."c3" (chorus); anything else = off. */
    void setVib(const std::string& v) { vibMode = v; }
    /** Leslie amplifier drive, 0..1 (also forwarded to the amp model). */
    void setDrive(double v) { drive = v; amp.setDrive(v); }
    /** Amplifier model index, simulation quality level, and power-supply sag amount 0..1. */
    void setAmp(int model, int quality, double sagAmount) { amp.setModel(model); amp.setQuality(quality); amp.setSag(sagAmount); pre.setQuality(quality); }
    /** Organ preamp stage on/off and its gain 0..1 (see OrganPreamp). */
    void setPreamp(bool on, double gain) { pre.setEnabled(on); pre.setGain(gain); }
    /** Spring reverb: on/off, wet mix 0..1, decay time T60 in seconds, tone 0..1, drive 0..1, number of springs. */
    void setSpring(bool on, double mixAmt, double t60, double toneAmt, double driveAmt, int springs) { spring.setEnabled(on); spring.setMix(mixAmt); spring.setDecay(t60); spring.setTone(toneAmt); spring.setDrive(driveAmt); spring.setSprings(springs); }
    /** Physically "kicks" the spring tank (the classic crash); strength 0..1. */
    void kickSpring(double strength) { spring.kick(strength); }
    /** Rotor speeds: r = 0 horn, 1 drum; slow/fast speed in Hz, acceleration/deceleration time constants in seconds. */
    void setRotor(int r, double slowHz, double fastHz, double upTau, double dnTau) {
        auto& R = rot[r]; R.slow = slowHz; R.fast = fastHz; R.upTau = upTau; R.dnTau = dnTau; setLeslie(leslieMode);
    }
    /** Motor brake: true = rotors stop quickly in "stop" mode, false = they coast down slowly. */
    void setBrake(bool on) { brake = on; }
    /** Cabinet detail: level of the horn-compartment reflection 0..1, rotor speed wobble 0..1 (negative = derive from Character). */
    void setCabinetDetail(double hornBox, double wobble) { hornBoxLevel = hornBox; wobbleOverride = wobble; applyCharacter(); }
    /** Organ detail: percussion level (linear), short/long percussion decay in seconds, use harmonic-rich waveforms for the lowest 12 wheels. */
    void setOrganDetail(double percLevel, double percShort, double percLong, bool complexWheels) { percLevelUser = percLevel; percShortS = percShort; percLongS = percLong; complexLow = complexWheels; }
    /** Output stage: master gain in dB and LFE low-pass cutoff in Hz. */
    void setOutput(double masterDb, double lfeHz) {
        masterGain = std::pow(10.0, masterDb / 20); lfeLp1.set("lp", lfeHz, 0.70710678, fs); lfeLp2.set("lp", lfeHz, 0.70710678, fs);
    }
    /** Current plate (B+) supply voltage of the amp model, for the UI. */
    double ampPlate() const { return amp.plateSupply(); }
    /** Current cathode voltage of the amp model, for the UI. */
    double ampCathode() const { return amp.cathodeVoltage(); }
    /** Active amp simulation quality level. */
    int ampQuality() const { return amp.getQuality(); }
    /** Key click level 0..1 (contact noise on key press/release). */
    void setClick(double v) { click = v; }
    /** Expression (swell) pedal position 0..1; also shapes the bass response like the original pedal. */
    void setVolume(double v) { volume = v; }
    /** Horn/drum balance 0..1: 0 = horn only, 0.5 = both full, 1 = drum only. */
    void setBalance(double v) { hornLevel = std::min(1.0, 2 * (1 - v)); drumLevel = std::min(1.0, 2 * v); }
    /** Leslie speed switch: "slow", "fast" or anything else for "stop". */
    void setLeslie(const std::string& mode) { leslieMode = mode; for (auto& r : rot) r.tgt = mode == "fast" ? r.fast : mode == "slow" ? r.slow : 0; }
    /** Replaces the room description (dimensions in metres, absorption, material, shape) and rebuilds all paths. */
    void setRoom(const Room& r) { room = r; rebuildRoom(); }
    /** Direct access to the room for in-place edits; call roomChanged() afterwards. */
    Room& roomRef() { return room; }
    /** Rebuilds image sources and reverb after the room was edited via roomRef(). */
    void roomChanged() { rebuildRoom(); }
    /** Instrument "character": tonewheel condition, crosstalk/leakage, contact quality, motor noise (all 0..1), plus gear-tuning and bus-bar compression switches. */
    void setCharacter(const Character& c) { chr = c; applyCharacter(); }
    /** Positions in metres: Leslie (lx, ly) and listener (hx, hy) with listener yaw in radians; jump = snap instead of glide. The has* flags say which parts are valid. */
    void setPos(bool hasLes, double lx, double ly, bool hasLis, double hx, double hy, bool hasYaw, double yaw, bool jump);
    /** Speaker layout: azimuths in degrees, output channel per speaker, LFE channel index (-1 = none), stereo flag. */
    void setSpeakers(const Speakers& s);
    /** LFE send level (linear). */
    void setLfe(double v) { lfeLevel = v; }
    /** Cabinet orientation (yaw) in radians; jump = snap instead of glide. */
    void setCabYaw(double v, bool jump) { cabTyaw = v; if (jump) cabYaw = v; }
    /** Ray-traced reflection taps for the "tent" room shape (delay in s, direction, per-band energy). */
    void setTaps(std::vector<Tap> taps) { tapsData = std::move(taps); if (room.shape == "tent") rebuildRoom(); }

    /** Derived room data for the UI: RT60 per band in seconds, critical distance in metres, number of paths. */
    RoomInfo info() const { return roomInfo; }
    // Microphone rig
    /** Microphone setup (types, patterns, positions, mixer groups) - see MicRig.h. */
    void setMics(const MicSetup& m) { micSetup = m; micConfigure(); }
    /** Current microphone setup. */
    const MicSetup& mics() const { return micSetup; }
    /** Level per mixer channel (RMS, linear) for the meter display. */
    float micMeter(int c) const { return c >= 0 && c < MIC_CH ? micMeterVal[c] : 0.f; }
    /** Current horn rotor angle in radians. */
    double hornAngle() const { return rot[0].ang; }
    /** Current drum rotor angle in radians. */
    double drumAngle() const { return rot[1].ang; }
    /** Current horn rotor speed in Hz. */
    double hornHz() const { return rot[0].hz; }
    /** Current drum rotor speed in Hz. */
    double drumHz() const { return rot[1].hz; }

    /** Renders N samples into out[c][n]. Channels 0..7 carry the speaker/room mix; 8..15 are mic direct outs only. */
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
    // Calls f(wheel, amplitude) for every tonewheel a key contributes to on manual m.
    // Pedal: on the original console the 16' and 8' pedal drawbars are mixtures of the fundamental and two
    // overtones (16' ~ 16' + 8' + 5 1/3', 8' ~ 8' + 4' + 2 2/3'); the mix ratios are approximations.
    template <class F> void forParts(int m, int k, const double* amps, F f) const {
        static const int OFF[2][3] = {{0, 12, 19}, {12, 24, 31}};
        static const double MIX[2][3] = {{1.0, 0.38, 0.22}, {1.0, 0.32, 0.18}};
        for (int i = 0; i < 9; i++) {
            if (amps[i] <= 0) continue;
            if (m == 2) { if (i < 2) for (int j = 0; j < 3; j++) f(std::max(1, std::min(91, k + 1 + OFF[i][j])), amps[i] * MIX[i][j]); }
            else f(wheelFor(k, i), amps[i]);
        }
    }
    void drawbarAmps(int man, double* amps) const;
    // One-pole low-pass coefficient for cutoff f (Hz), clamped below Nyquist
    double kOf(double f) const { return 1 - std::exp(-TWO_PI * std::min(f, fs * 0.45) / fs); }

    // Semitone offset of each drawbar relative to the 8' fundamental (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1')
    static constexpr int DB_OFF[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
    // Driving/driven gear tooth counts of the original generator, one pair per semitone (gives the slightly detuned "gear tuning")
    static constexpr int GEARS[12][2] = {{85, 104}, {71, 82}, {67, 73}, {105, 108}, {103, 100}, {84, 77}, {74, 64}, {98, 80}, {96, 74}, {88, 64}, {67, 46}, {108, 70}};

    // ---------------- Tonewheel generator (91 wheels, index 1..91) ----------------
    static constexpr int TAB = 4096;
    std::vector<float> sine, complexWave;
    std::array<double, 92> phase{}, inc{}, incNew{}, incGear{};
    std::array<float, 92> percG{}, wheelLvl{}, wheelVar{}, wheelG{};
    std::array<float, 92> gain[3]{}, target[3]{};
    std::array<uint8_t, 61> keys[3]{};
    int numKeysUpper = 0;
    int drawbars[3][9] = {{8, 8, 8, 0, 0, 0, 0, 0, 0}, {0, 0, 8, 4, 0, 0, 0, 0, 0}, {6, 3, 0, 0, 0, 0, 0, 0, 0}};
    bool vibUpper = true, vibLower = false;
    double percLevelUser = 1.0, percShortS = 0.2, percLongS = 1.0; bool complexLow = true;
    bool brake = true; double hornBoxLevel = 0.45, wobbleOverride = -1, masterGain = 1.0;
    amp::LeslieAmp amp;
    amp::OrganPreamp pre;
    SpringReverb spring;
    std::vector<float> orgBuf;
    Perc perc;
    double percEnv = 0, click = 0.5, clickEnv = 0, organGain = 0.05;
    struct Event { double t; int m; int w; double d; };
    std::vector<Event> events;
    double sampleClock = 0;
    Lcg fastRand{777};

    // ---------------- Instrument character: crosstalk/leakage, key-contact behaviour, bus-bar compression, motor ----------------
    Character chr;
    double leakAdj = 0, leakAdj2 = 0, leakBank = 0, leakHum = 0, staggerS = 0, bounceP = 0, gainStep = 0, busK = 0, motorLevel = 0, rotorWobble = 0, sagAmt = 0;

    // ---------------- Scanner vibrato (delay-line based, 2048-sample buffer) ----------------
    std::string vibMode = "c3";
    std::vector<float> vibBuf; int vibW = 0; double vibPh = 0;

    // ---------------- Expression pedal, crossover and cabinet filters ----------------
    // expLp: bass boost of the expression pedal; lp1/lp2 + hp1/hp2: horn/drum crossover; cabLp: horn-compartment colouration;
    // hornRes/hornRes2/drumRes/hornHi: resonances of horn and drum; lfeLp1/2: LFE low-pass; motorLp/windBp1/2: motor and wind noise
    double drive = 0.35, volume = 0.8, sagEnv = 0;
    Biquad expLp, lp1, lp2, hp1, hp2, cabLp, hornRes, hornRes2, drumRes, hornHi, lfeLp1, lfeLp2, motorLp, windBp1, windBp2;
    int cabDelay = 0;
    double cabYaw = -1.5707963, cabTyaw = -1.5707963;   // current / target cabinet orientation (radians)

    // ---------------- Rotors and rotor delay lines ----------------
    // rot[0] = horn, rot[1] = drum. bufH/bufD hold the horn/drum signals, bufC the horn signal coloured by the cabinet;
    // every acoustic path reads from these ring buffers with a fractional (Doppler-shifting) delay.
    Rotor rot[2];
    std::string leslieMode = "slow";
    double hornLevel = 1, drumLevel = 1;
    static constexpr int DL = 32768, DM = DL - 1;
    std::vector<float> bufH, bufD, bufC;
    int wIdx = 0;

    // ---------------- Room: Leslie/listener positions and acoustic paths (image sources or ray taps) ----------------
    Room room;
    std::vector<Tap> tapsData;
    struct Pos { double x, y, tx, ty; } lesliePos{3, 6, 3, 6};
    struct ListenerPos { double x, y, tx, ty, yaw, tyaw, z; } listener{3, 2.5, 3, 2.5, 1.5707963, 1.5707963, 1.2};
    std::vector<Path> paths;
    int nRefl = 0;
    RoomInfo roomInfo;
    double rt60[3] = {0, 0, 0};

    // ---------------- Speaker layout and panning ----------------
    // Direct paths are panned with pairwise (VBAP-style) gains between the two nearest speakers; higher-order paths are
    // summed into NB azimuth buckets (plus one omnidirectional bucket) that are panned once per sample.
    Speakers spk;
    double lfeLevel = 0.5;
    struct SpkS { double a; int ch; };
    std::vector<SpkS> spkSorted;
    double uniG = 1;
    static constexpr int NB = 24;
    std::vector<std::array<float, 8>> bucketG;
    std::array<float, NB + 1> bucket{};

    // ---------------- FDN reverb (16 modulated delay lines, 6 input allpasses, 16 per-line allpasses, 3-band decay) ----------------
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

    // ---------------- Microphone rig and mixer ----------------
    // mic[0..1] = horn close mics (A/B), mic[2..3] = drum close mics (A/B), mic[4..5] = room pair (L/R)
    MicSetup micSetup;
    MicRt mic[6];
    std::vector<float> chDelay[6]; int chDelayIdx = 0; static constexpr int CHD = 8192;
    double mixT[MIC_GRP][4] = {}, mixC[MIC_GRP][4] = {};   // per group: A->L, A->R, B->L, B->R (mixT = target, mixC = smoothed current)
    int grpDelayS[MIC_GRP] = {0}, grpOutL[MIC_GRP] = {-1, -1, -1}, grpOutR[MIC_GRP] = {-1, -1, -1};
    double modeMix = 0;                                     // 0 = listener mode, 1 = microphone mode (crossfade)
    double micMeterAcc[MIC_CH] = {0}; float micMeterVal[MIC_CH] = {0}; int micMeterN = 0;
    std::array<double, 16> micOut{};
    double kBandLo = 0.05, kBandHi = 0.3, kWind = 0.02, micRevScale = 1, roomReflG = 0.8, roomHfRefl = 0.9;
    double micSig[6] = {0};

    void micConfigure();
    void micRebuildPaths();
    void micPlaceClose(double a);
    bool micNeed(int i) const { return mic[i].want || (i % 2 == 1 && std::fabs(mixC[i / 2][2]) + std::fabs(mixC[i / 2][3]) > 1e-5); }
    void micBlock(double bt);
    void micFdnRows();
};

// =====================================================================================
// Initialisation: wavetables, tonewheel frequencies (gear-derived and equal-tempered), per-wheel level
// and tolerance, fixed filters, rotor parameters, default 8-channel speaker ring, reverb buffers.
inline void LeslieRoomDSP::init() {
    Lcg rnd(20261104u);
    sine.resize(TAB + 1); complexWave.resize(TAB + 1);
    for (int i = 0; i <= TAB; i++) { double x = TWO_PI * i / TAB; sine[i] = (float) std::sin(x); complexWave[i] = (float) ((std::sin(x) + 0.28 * std::sin(2 * x) + 0.14 * std::sin(3 * x) + 0.06 * std::sin(5 * x)) / 1.2); }
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

// Derives all "character" dependent coefficients: wheel tuning/levels, crosstalk gains, key-contact timing,
// bus-bar compression, motor noise, rotor wobble and amp ageing.
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

// Cabinet directivity: gain g and low-pass cutoff fc (Hz) for a path leaving the cabinet in direction (dx, dy, dz).
// The louvred front (facing cabYaw) is open; towards the back and vertically the sound is attenuated and dulled.
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
    // Linear amplitude per drawbar (3 dB per step). Percussion only affects the upper manual: the 1' drawbar is
    // muted and the "normal" (loud) percussion setting slightly attenuates the drawbars, as on the original console.
    double percAtt = man == 0 && perc.on && !perc.soft ? 0.72 : 1;
    int n = man == 2 ? 2 : 9;
    for (int i = 0; i < 9; i++) {
        if (i >= n || (man == 0 && i == 8 && perc.on)) { amps[i] = 0; continue; }
        double lvl = man == 2 ? 1.15 : 1.0;   // pedal a little louder (it has its own level curve on the original)
        amps[i] = drawbars[man][i] <= 0 ? 0 : std::pow(10.0, -(8 - drawbars[man][i]) * 3 / 20.0) * percAtt * lvl;
    }
}

// Schedules a gain change (key contact closing/opening) for tonewheel w on manual m. The contacts of the nine
// bus bars close at slightly different times (stagger) and may bounce, producing the characteristic key click.
inline void LeslieRoomDSP::schedule(int m, int w, double d, double baseDelay) {
    double t0 = sampleClock + baseDelay + fastRand() * staggerS * (m == 2 ? 1.6 : 1.0);
    events.push_back({t0, m, w, d});
    if (fastRand() < bounceP) {
        int n = fastRand() < 0.4 ? 2 : 1; double t = t0;
        for (int b = 0; b < n; b++) {
            double dt = (0.00015 + 0.0006 * fastRand()) * fs;
            events.push_back({t + dt, m, w, -d});
            events.push_back({t + dt + (0.0001 + 0.0004 * fastRand()) * fs, m, w, d});
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
    forParts(man, k, amps, [&](int w, double a) { schedule(man, w, a, 0); });
    if (man == 0 && perc.on) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
}
inline void LeslieRoomDSP::noteOff(int k, int man) {
    int maxK = man == 2 ? 24 : 60;
    if (man < 0 || man > 2 || k < 0 || k > maxK || !keys[man][k]) return;
    keys[man][k] = 0;
    if (man == 0) numKeysUpper--;
    clickEnv = std::max(clickEnv, 0.35);
    double amps[9]; drawbarAmps(man, amps);
    forParts(man, k, amps, [&](int w, double a) { schedule(man, w, -a, 0); });
    if (man == 0 && perc.on) { int w = wheelFor(k, perc.third ? 4 : 3); percG[w] = std::max(0.f, percG[w] - 1); }
}
// Recomputes all tonewheel target gains from the currently held keys (after drawbar or percussion changes).
inline void LeslieRoomDSP::retarget() {
    events.clear(); std::fill(percG.begin(), percG.end(), 0.f);
    for (int m = 0; m < 3; m++) {
        std::fill(target[m].begin(), target[m].end(), 0.f);
        double amps[9]; drawbarAmps(m, amps);
        int maxK = m == 2 ? 24 : 60;
        for (int k = 0; k <= maxK; k++) {
            if (!keys[m][k]) continue;
            forParts(m, k, amps, [&](int w, double a) { applyDelta(m, w, a); });
            if (m == 0 && perc.on) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
        }
    }
}
// Adds d to the target gain of wheel w and spreads crosstalk to the neighbouring wheels (adjacent on the
// generator shaft, +-1 and +-2) and to the wheel 48 positions away that shares the same magnet/bank wiring.
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
    if (hasLes) { lesliePos.tx = lx; lesliePos.ty = ly; }
    if (hasLis) { listener.tx = hx; listener.ty = hy; if (hasYaw) listener.tyaw = yaw; }
    if (jump) { lesliePos.x = lesliePos.tx; lesliePos.y = lesliePos.ty; listener.x = listener.tx; listener.y = listener.ty; listener.yaw = listener.tyaw; }
}
inline void LeslieRoomDSP::setSpeakers(const Speakers& s) {
    spk = s; for (auto& a : spk.az) a *= 3.14159265358979 / 180;
    prepSpeakers(); rebuildFdnOut();
    for (auto& p : paths) { p.gPrev.fill(0); p.gNext.fill(0); p.init = false; }
    if (mic[0].inited) micConfigure();   // the mixer's front/rear output targets depend on the output format
}

// Rebuilds the acoustic model after a room change: clamps positions, derives per-band absorption from the material,
// builds the image-source paths (box) or ray taps (tent) for both rotors, computes RT60 (Eyring), the
// pre-delay and the FDN delay lengths/decay gains, then refreshes the output rows and microphone paths.
inline void LeslieRoomDSP::rebuildRoom() {
    auto clampd = [](double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); };
    auto& R = room;
    lesliePos.tx = clampd(lesliePos.tx, 0.3, R.Lx - 0.3); lesliePos.ty = clampd(lesliePos.ty, 0.3, R.Ly - 0.3); lesliePos.x = clampd(lesliePos.x, 0.3, R.Lx - 0.3); lesliePos.y = clampd(lesliePos.y, 0.3, R.Ly - 0.3);
    listener.tx = clampd(listener.tx, 0.3, R.Lx - 0.3); listener.ty = clampd(listener.ty, 0.3, R.Ly - 0.3); listener.x = clampd(listener.x, 0.3, R.Lx - 0.3); listener.y = clampd(listener.y, 0.3, R.Ly - 0.3);
    double mat[3] = {1.35, 1.0, 0.8};
    if (R.material == "tent") { mat[0] = 1.9; mat[2] = 0.75; } else if (R.material == "concrete") { mat[0] = 0.55; mat[2] = 1.1; }
    else if (R.material == "stone") { mat[0] = 0.6; mat[2] = 1.35; } else if (R.material == "glass") { mat[0] = 0.9; mat[2] = 1.6; }
    double ab[3];
    for (int i = 0; i < 3; i++) ab[i] = R.hasAbsBands ? clampd(R.absBands[i], 0.03, 0.97) : clampd(R.absorb * mat[i], 0.03, 0.95);
    double reflG = std::sqrt(1 - ab[1]), hfRefl = std::sqrt((1 - ab[2]) / (1 - ab[1]));
    roomReflG = reflG; roomHfRefl = hfRefl;
    std::vector<Path> old = std::move(paths);
    std::vector<Path> np;
    bool tent = R.shape == "tent";
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

    double V = tent && R.tentVolume > 0 ? R.tentVolume : R.Lx * R.Ly * R.Lz;
    double S = tent && R.tentSurface > 0 ? R.tentSurface : 2 * (R.Lx * R.Ly + R.Lx * R.Lz + R.Ly * R.Lz);
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
    micRebuildPaths(); micFdnRows();
    roomInfo.rt60[0] = rt[0]; roomInfo.rt60[1] = rt[1]; roomInfo.rt60[2] = rt[2]; roomInfo.rc = std::sqrt(rc2); roomInfo.paths = (int) paths.size();
}

// Per-speaker FDN output taps: a Hadamard-like +-0.25 row per speaker plus prime-number read offsets so that
// every output gets a decorrelated mix of the 16 lines.
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

// Sorts the speakers by azimuth and precomputes the panning gains for the NB azimuth buckets (+ omni bucket).
inline void LeslieRoomDSP::prepSpeakers() {
    int n = (int) spk.az.size();
    spkSorted.clear();
    for (int i = 0; i < n; i++) { double a = spk.az[i]; while (a < 0) a += TWO_PI; while (a >= TWO_PI) a -= TWO_PI; spkSorted.push_back({a, spk.ch[i]}); }
    std::sort(spkSorted.begin(), spkSorted.end(), [](const SpkS& p, const SpkS& q) { return p.a < q.a; });
    uniG = 1 / std::sqrt((double) n);
    bucketG.assign(NB + 1, {});
    for (int k = 0; k <= NB; k++) { if (k < NB) panGains(k * TWO_PI / NB, 1, bucketG[k].data()); else panGains(0, 0, bucketG[k].data()); }
}

// Panning gains for a source at azimuth az (radians, listener-relative). hfrac = horizontal fraction: that part is
// panned between the two nearest speakers (constant power), the rest (elevated sound) is spread uniformly.
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


// ---------------- Microphone rig ----------------
// Applies a MicSetup: mic types/patterns, close-mic placement around the cabinet, room pair geometry,
// proximity effect, wind sensitivity and the mixer matrix per group.
inline void LeslieRoomDSP::micConfigure() {
    const MicSetup& M = micSetup;
    kBandLo = kOf(400); kBandHi = kOf(3500); kWind = kOf(160);
    for (int c = 0; c < 6; c++) if ((int) chDelay[c].size() != CHD) chDelay[c].assign(CHD, 0.f);
    struct Def { bool want, close; int rotor, type, pattern; };
    const Def d[6] = {
        {true, true, 0, M.hornType, M.hornPattern}, {M.hornStereo, true, 0, M.hornType, M.hornPattern},
        {true, true, 1, M.drumType, M.drumPattern}, {M.drumStereo, true, 1, M.drumType, M.drumPattern},
        {true, false, -1, M.roomType, M.roomPattern}, {true, false, -1, M.roomType, M.roomPattern}};
    const double PI = 3.14159265358979, D2R = PI / 180;
    for (int i = 0; i < 6; i++) {
        MicRt& m = mic[i];
        const bool first = !m.inited, wasWant = m.want;
        m.want = d[i].want; m.active = true; m.close = d[i].close; m.rotor = d[i].rotor; m.chan = i;
        int t = std::max(0, std::min(NUM_MIC_TYPES - 1, d[i].type)); m.type = t;
        const MicType& T = MIC_TYPES[t];
        for (int b = 0; b < 3; b++) m.a[b] = T.a[b];
        if (t == 4) { if (d[i].pattern == 1) { m.a[0] = 1; m.a[1] = 0.95; m.a[2] = 0.72; } else if (d[i].pattern == 2) { m.a[0] = m.a[1] = 0; m.a[2] = 0.05; } }
        double aAvg = (m.a[0] + m.a[1] + m.a[2]) / 3;
        m.diffuse = std::sqrt(aAvg * aAvg + (1 - aAvg) * (1 - aAvg) / 3);      // diffuse-field sensitivity (random-energy efficiency)
        auto setBq = [&](Biquad& q, const char* ty, double f, double qq, double db) {
            Biquad nb; nb.set(ty, std::min(f, fs * 0.45), qq, fs, db);
            if (first) q = nb; else { q.b0 = nb.b0; q.b1 = nb.b1; q.b2 = nb.b2; q.a1 = nb.a1; q.a2 = nb.a2; }   // keep the filter state so there is no click
        };
        m.nEq = 0;
        for (int k = 0; k < T.nEq; k++) {
            const MicEq& e = T.eq[k];
            setBq(m.eq[m.nEq++], e.type == 'h' ? "hp" : e.type == 'l' ? "lp" : e.type == 's' ? "shelf" : "peak", e.f, e.q, e.db);
        }
        if (m.close) {
            bool horn = m.rotor == 0, st = horn ? M.hornStereo : M.drumStereo;
            double center = (horn ? M.hornCenter : M.drumCenter) * D2R, ang = (horn ? M.hornAngle : M.drumAngle) * D2R;
            m.phiT = st ? center + (i % 2 == 0 ? -0.5 : 0.5) * ang : center;   // L = left as seen by the engineer facing the cabinet
            const double Dh = 0.275, Wh = 0.37;                                  // half depth / half width of a Leslie 122 cabinet (m)
            double cf = std::fabs(std::cos(m.phiT)), sf = std::fabs(std::sin(m.phiT));
            double edge = std::min(cf > 1e-6 ? Dh / cf : 1e9, sf > 1e-6 ? Wh / sf : 1e9);
            m.RT = edge + std::max(0.02, horn ? M.hornDist : M.drumDist);
            if (first) { m.phi = m.phiT; m.R = m.RT; }
            else if (i % 2 == 1 && m.want && !wasWant) {   // the second (B) mic starts from the centre position with fresh filter state
                m.phi = center; m.R = mic[i - 1].R;
                for (auto& q : m.eq) q.reset(); m.prox.reset();
                for (auto& p : m.paths) { p.lp = p.zlo = p.zhi = p.zd = 0; }
            }
            double r = rot[m.rotor].r;
            m.dMean = std::sqrt(m.RT * m.RT + r * r);
            m.trim = m.dMean / 2.5;                                             // preamp trim: 0 dB on the mixer ~ listener at 2.5 m
            m.windAmt = horn ? T.wind * (M.windscreen ? 0.2 : 1) * std::pow(0.2 / std::max(0.05, m.RT - 0.2), 2) : 0;
        } else {
            m.trim = 1; m.windAmt = 0; m.dMean = 2;
        }
        // Proximity effect: the pressure-gradient part (1 - a) boosts the bass below c / (2 pi d)
        double grad = 1 - m.a[0];
        double fP = C_SOUND / (TWO_PI * std::max(0.05, m.dMean));
        double boost = std::min(15.0, 20 * std::log10(1 + grad * T.prox * std::max(0.0, fP / 60 - 1) * 0.5));
        m.hasProx = boost > 0.3;
        if (m.hasProx) setBq(m.prox, "shelf", std::min(450.0, fP), 0.6, boost);
        m.inited = true;
    }
    // Room pair
    {
        double yaw = M.roomYaw, px = -std::sin(yaw), py = std::cos(yaw);   // unit vector pointing left of the viewing direction
        double sp = M.roomArr == 0 ? std::max(0.05, M.roomSpacing) : M.roomArr == 2 ? 0.17 : 0.02;
        double aimOff = M.roomArr == 0 ? 0 : M.roomArr == 2 ? 55 * D2R : 45 * D2R;
        for (int j = 0; j < 2; j++) {
            MicRt& m = mic[4 + j]; double sgn = j == 0 ? 1 : -1;        // 4 = left, 5 = right
            m.x = M.roomX + px * sp * 0.5 * sgn; m.y = M.roomY + py * sp * 0.5 * sgn; m.z = M.roomZ;
            double a = yaw + sgn * aimOff; m.ax = std::cos(a); m.ay = std::sin(a); m.az = 0;
        }
    }
    // Mixer: routing matrix per group (A->L, A->R, B->L, B->R)
    bool anySolo = false; for (int g = 0; g < MIC_GRP; g++) if (M.grp[g].solo) anySolo = true;
    const size_t nsp = spk.ch.size();
    const int fl = nsp > 0 ? spk.ch[0] : 0, fr = nsp > 1 ? spk.ch[1] : 1;
    const int rl = nsp >= 5 ? spk.ch[3] : nsp >= 4 ? spk.ch[2] : fl, rr = nsp >= 5 ? spk.ch[4] : nsp >= 4 ? spk.ch[3] : fr;
    for (int g = 0; g < MIC_GRP; g++) {
        const auto& G = M.grp[g];
        const bool stereo = g == 2 || (g == 0 ? M.hornStereo : M.drumStereo);
        double gain = (!G.mute && (!anySolo || G.solo)) ? std::pow(10.0, G.gainDb / 20) * (G.pol ? -1 : 1) : 0;
        double* T = mixT[g];
        if (stereo) {
            double w = std::max(0.0, std::min(1.0, G.width)), bal = std::max(-1.0, std::min(1.0, G.pan));
            double gl = bal > 0 ? 1 - bal : 1, gr = bal < 0 ? 1 + bal : 1;
            T[0] = gain * gl * (0.5 + 0.5 * w); T[1] = gain * gr * (0.5 - 0.5 * w);
            T[2] = gain * gl * (0.5 - 0.5 * w); T[3] = gain * gr * (0.5 + 0.5 * w);
        } else {
            double th = (std::max(-1.0, std::min(1.0, G.pan)) + 1) * 0.25 * PI;
            T[0] = gain * std::cos(th); T[1] = gain * std::sin(th); T[2] = T[3] = 0;
        }
        if (G.out >= 0) {
            grpOutL[g] = std::min(15, G.out); grpOutR[g] = stereo ? std::min(15, G.out + 1) : grpOutL[g];
            if (!stereo) { T[0] = gain; T[1] = 0; }   // mono direct out: no panning
        } else { grpOutL[g] = G.out == -2 ? rl : fl; grpOutR[g] = G.out == -2 ? rr : fr; }
        grpDelayS[g] = std::max(0, std::min(CHD - 2, (int) std::lround(G.delayMs * 1e-3 * fs)));
    }
    micPlaceClose(1); micRebuildPaths(); micFdnRows();
}

// Places the four close mics around the cabinet; a = smoothing factor (1 = jump to target).
inline void LeslieRoomDSP::micPlaceClose(double a) {
    for (int i = 0; i < 4; i++) {
        MicRt& m = mic[i];
        m.phi += (m.phiT - m.phi) * a; m.R += (m.RT - m.R) * a;        // mono <-> stereo: mics glide to the new position instead of jumping
        double th = cabYaw + m.phi, c = std::cos(th), s = std::sin(th);
        m.x = lesliePos.x + m.R * c; m.y = lesliePos.y + m.R * s;
        m.z = m.rotor == 0 ? rot[0].h + micSetup.hornHeight : rot[1].h;
        m.ax = -c; m.ay = -s; m.az = 0;
        m.windC = c; m.windS = s;
    }
}

// Builds the acoustic paths from the rotors to each microphone.
inline void LeslieRoomDSP::micRebuildPaths() {
    const Room& R = room;
    for (int i = 0; i < 6; i++) {
        MicRt& m = mic[i];
        m.paths.clear();
        if (m.close) {
            // Close mic: its own rotor directly and via the floor reflection (computed exactly per sample); the other rotor as crosstalk
            for (int r = 0; r < 2; r++) {
                MicPath p; p.rotor = r; p.exact = r == m.rotor; p.kDrum = kOf(18000); m.paths.push_back(p);
                if (r == m.rotor) { MicPath f = p; f.sz = -1; f.order = 1; f.g = 0.75 * roomReflG; f.kDrum = kOf(9000); m.paths.push_back(f); }
            }
            continue;
        }
        int maxO = std::max(0, std::min(R.shape == "tent" ? 1 : R.order, micSetup.roomOrder));
        for (int r = 0; r < 2; r++)
            for (int nx = -3; nx <= 3; nx++) for (int px = 0; px <= 1; px++) {
                int ox = std::abs(2 * nx - px);
                for (int ny = -3; ny <= 3; ny++) for (int py = 0; py <= 1; py++) {
                    int oy = std::abs(2 * ny - py);
                    for (int nz = -3; nz <= 3; nz++) for (int pz = 0; pz <= 1; pz++) {
                        int oz = std::abs(2 * nz - pz), order = ox + oy + oz;
                        if (order > maxO) continue;
                        double hf = std::pow(0.62, order) * std::pow(roomHfRefl, order);
                        MicPath p; p.rotor = r; p.order = order;
                        p.sx = 1 - 2 * px; p.sy = 1 - 2 * py; p.sz = 1 - 2 * pz;
                        p.ox = 2 * nx * R.Lx; p.oy = 2 * ny * R.Ly; p.oz = 2 * nz * R.Lz;
                        p.g = std::pow(roomReflG, order) * (order > 0 ? 0.9 : 1);
                        p.kDrum = kOf(std::min(20000.0, 18000 * hf));
                        p.jit = order > 0 ? (((r * 577 + (int) m.paths.size() * 97 + i * 31) % 89) / 89.0) * 0.9 * order : 0;
                        m.paths.push_back(p);
                    }
                }
            }
    }
}

// FDN output rows/offsets for the six microphones (same scheme as rebuildFdnOut(), different seeds).
inline void LeslieRoomDSP::micFdnRows() {
    static const int primes[] = {7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331};
    const int NP = sizeof(primes) / sizeof(int);
    double sc = fs / 48000;
    for (int i = 0; i < 6; i++) {
        int r = 8 + i;
        for (int j = 0; j < 16; j++) { int bits = r & j, par = 0; while (bits) { par ^= bits & 1; bits >>= 1; } mic[i].fdnRow[j] = par ? -0.25f : 0.25f; mic[i].fdnOff[j] = (int) std::lround(primes[(i * 23 + j * 13 + 3) % NP] * sc); }
    }
}

/** Per-block update: close-mic positions (they follow the Leslie), delays / gains / directivity of the room paths */
inline void LeslieRoomDSP::micBlock(double bt) {
    const double PI = 3.14159265358979;
    micPlaceClose(1 - std::exp(-bt / 0.12));
    for (int i = 0; i < 6; i++) {
        MicRt& m = mic[i]; if (!micNeed(i)) continue;
        for (auto& p : m.paths) {
            const Rotor& r = rot[p.rotor];
            double icx = p.sx * lesliePos.x + p.ox, icy = p.sy * lesliePos.y + p.oy, iz = p.sz * r.h + p.oz;
            double vx = m.x - icx, vy = m.y - icy, vz = m.z - iz, dist = std::sqrt(vx * vx + vy * vy + vz * vz);
            double cg, cfc; cabinet(-p.sx * vx, -p.sy * vy, -p.sz * vz, cg, cfc);
            p.cab = cg; p.kc = std::min(kOf(cfc), kOf(std::max(2500.0, 260000 / std::max(dist, 1.0))));
            double cth = -(vx * m.ax + vy * m.ay + vz * m.az) / std::max(dist, 1e-6);
            for (int b = 0; b < 3; b++) p.band[b] = m.a[b] + (1 - m.a[b]) * cth;
            if (p.exact) continue;
            bool horn = p.rotor == 0;
            double a0 = r.ang, a1 = r.ang + TWO_PI * r.hz * bt;
            for (int e = 0; e < 2; e++) {
                double an = e ? a1 : a0, c = std::cos(an), sn = std::sin(an);
                double dxp = p.sx * c, dyp = p.sy * sn;
                double wx = m.x - (icx + dxp * r.r), wy = m.y - (icy + dyp * r.r), wz = m.z - iz;
                double d = std::sqrt(wx * wx + wy * wy + wz * wz) + p.jit;
                double cosE = (dxp * wx + dyp * wy) / d, u = 0.5 + 0.5 * cosE;
                double g = (horn ? 0.3 + 0.7 * u : 0.75 + 0.25 * u) * p.g * p.cab / std::max(d, 0.1);
                if (e) { p.d1 = d; p.g1 = g; } else { p.d0 = d; p.g0 = g; }
                if (e) {
                    double k;
                    if (horn) { double th = std::acos(std::max(-1.0, std::min(1.0, cosE))); double s2 = th <= PI / 2 ? std::sin(th) : 1 + (th - PI / 2) / (PI / 2); k = kOf(std::min(16000.0, C_SOUND / (PI * 0.045 * std::max(s2, 0.06)))); }
                    else k = p.kDrum;
                    p.kb = std::min(k, p.kc);
                }
            }
            if (!p.init) { p.init = true; }
        }
    }
}

// ---------------- Main render ----------------
inline void LeslieRoomDSP::process(float* const* out, int nCh, int N) {
    const double PI = 3.14159265358979;
    double a = 1 - std::exp(-N / (0.12 * fs));
    auto& L = lesliePos; auto& H = listener;
    L.x += (L.tx - L.x) * a; L.y += (L.ty - L.y) * a; H.x += (H.tx - H.x) * a; H.y += (H.ty - H.y) * a;
    double dy = H.tyaw - H.yaw; while (dy > PI) dy -= TWO_PI; while (dy < -PI) dy += TWO_PI; H.yaw += dy * a;
    double dcy = cabTyaw - cabYaw; while (dcy > PI) dcy -= TWO_PI; while (dcy < -PI) dcy += TWO_PI; cabYaw += dcy * a;

    // Per-block smoothing of positions/orientations, then rotor speed ramps (acceleration/deceleration, brake)
    double bt = N / fs;
    for (auto& r : rot) { double tau = r.tgt > r.hz ? r.upTau : r.dnTau; if (r.tgt == 0) tau *= brake ? 0.45 : 1.6; r.hz += (r.tgt - r.hz) * (1 - std::exp(-bt / tau)); if (r.tgt == 0 && r.hz < 0.02) r.hz = 0; }

    // Per-block geometry for every room path: distances, cabinet directivity, panning (interpolated within the block)
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
    // Listener <-> microphone mode: crossfade smoothly; whatever is fully faded out is not computed (the reverb runs for both)
    const double modeT = micSetup.on ? 1 : 0;
    const bool micsOn = micSetup.on || modeMix > 1e-5, lisOn = !micSetup.on || modeMix < 1 - 1e-5;
    if (micsOn) micBlock(bt);
    const double chK = 1 - std::exp(-1 / (0.03 * fs)), modeK = 1 - std::exp(-1 / (0.04 * fs));
    const int nOut = std::min(nCh, 16);
    const int nSp = std::min(nCh, 8);   // room/listener: at most 8 speakers (acc, panning tables); channels 9-16 are mic direct outs only

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

    // ---------- 1) Organ: tonewheels, manuals, key contacts, percussion, scanner vibrato, expression pedal -> orgBuf ----------
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
                const float* tab = (w <= 12 && complexLow) ? complexWave.data() : sine.data();
                double v = (tab[i] + (tab[i + 1] - tab[i]) * f) * wheelG[w];
                sU += v * g0; sL += v * g1; sP += v * g2; ps += v * pg; hum += v; load += gsum;
            }
        }
        double comp = 1 / (1 + busK * load);
        double sVib = ((vibUpper ? sU : 0) + (vibLower ? sL : 0)) * comp;
        double sDry = ((vibUpper ? 0 : sU) + (vibLower ? 0 : sL) + sP) * comp + hum * leakHum;
        // Percussion bypasses the vibrato (as in the original wiring)
        sDry += ps * percEnv * percLevel * 0.9;
        percEnv *= percDecay;
        if (clickEnv > 1e-4) { sDry += (fastRand() * 2 - 1) * clickEnv * click * 0.35; clickEnv *= clickDecay; }
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

    // ---------- 2) Organ preamp, spring reverb and Leslie power amp (circuit models with their own oversampling) ----------
    pre.process(orgBuf.data(), N); spring.process(orgBuf.data(), N); amp.process(orgBuf.data(), N);

    // ---------- 3) Crossover, rotors, room paths, microphones, FDN reverb and mixer (per sample) ----------
    for (int n = 0; n < N; n++) {
        double s = orgBuf[(size_t) n];
        double lo = drumRes.run(lp2.run(lp1.run(s))) * drumLevel;
        double hi = hornHi.run(hornRes2.run(hornRes.run(hp2.run(hp1.run(s))))) * hornLevel;
        if (motorLevel > 0) {
            double nz = fastRand() * 2 - 1;
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

        // Listener rendering: every room path reads the rotor ring buffers with its own (Doppler) delay and directivity

        for (size_t q = 0; q < (lisOn ? np : 0); q++) {
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
                    if (backness > 0) { double rp2 = rp - cabDelay; if (rp2 < 0) rp2 += DM + 1; v += bufC[((int) rp2) & DM] * backness * hornBoxLevel / std::max(0.05, dirG); }
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
            for (int c = 0; c < nSp; c++) { double gg = p.gPrev[c] + (p.gNext[c] - p.gPrev[c]) * tt; if (gg != 0) acc[c] += v * gg; }
        }
        if (lisOn) for (int k = 0; k <= NB; k++) { double bv = bucket[k]; if (bv == 0) continue; const auto& bg = bucketG[k]; for (int c = 0; c < nSp; c++) acc[c] += bv * bg[c]; }

        // ---------- Microphones ----------
        if (micsOn) {
            for (int i = 0; i < 6; i++) {
                MicRt& m = mic[i]; if (!micNeed(i)) { micSig[i] = 0; continue; }
                double sum = 0;
                for (auto& p : m.paths) {
                    const bool horn = p.rotor == 0;
                    const float* buf = horn ? bufH.data() : bufD.data();
                    double v, b0, b1, b2;
                    if (p.exact) {
                        double ca = horn ? chc : cdc, sa = horn ? chs : cds, rr = horn ? rh.r : rd.r, hh = horn ? rh.h : rd.h;
                        double dxp = p.sx * ca, dyp = p.sy * sa;
                        double wx = m.x - (p.sx * L.x + p.ox + dxp * rr), wy = m.y - (p.sy * L.y + p.oy + dyp * rr), wz = m.z - (p.sz * hh + p.oz);
                        double d = std::sqrt(wx * wx + wy * wy + wz * wz); if (d < 0.06) d = 0.06;
                        double cosE = (dxp * wx + dyp * wy) / d, u = 0.5 + 0.5 * cosE;
                        double rp = wi - d * fsc; if (rp < 0) rp += DM + 1;
                        int i1 = (int) rp; double fr = rp - i1;
                        double y0 = buf[(i1 - 1) & DM], y1 = buf[i1 & DM], y2 = buf[(i1 + 1) & DM], y3 = buf[(i1 + 2) & DM];
                        double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
                        v = ((c3 * fr + c2) * fr + c1) * fr + y1;
                        double dirG, k;
                        if (horn) {
                            dirG = 0.3 + 0.7 * u;
                            double th = std::acos(std::max(-1.0, std::min(1.0, cosE)));
                            double s2 = th <= PI / 2 ? std::sin(th) : 1 + (th - PI / 2) / (PI / 2);
                            k = 1 - std::exp(-std::min(16000.0, C_SOUND / (PI * 0.045 * std::max(s2, 0.06))) * kScale);
                            // when the horn points away, sound reaches the mic via the horn compartment (reflection inside the cabinet)
                            double back = std::max(0.0, -cosE);
                            if (back > 0) { double rp2 = rp - cabDelay; if (rp2 < 0) rp2 += DM + 1; v += bufC[((int) rp2) & DM] * back * hornBoxLevel * 0.6 / std::max(0.05, dirG); }
                        } else {
                            p.zd += kDrumLo * (v - p.zd);
                            v = p.zd * (0.85 + 0.15 * u) + (v - p.zd) * (0.3 + 0.7 * u);
                            dirG = 1; k = p.kDrum;
                        }
                        if (p.kc < k) k = p.kc;
                        p.lp += k * (v - p.lp);
                        v = p.lp * dirG * p.g * p.cab / d;
                        double cth = -(wx * m.ax + wy * m.ay + wz * m.az) / d;
                        b0 = m.a[0] + (1 - m.a[0]) * cth; b1 = m.a[1] + (1 - m.a[1]) * cth; b2 = m.a[2] + (1 - m.a[2]) * cth;
                    } else {
                        double d = p.d0 + (p.d1 - p.d0) * tt, g = p.g0 + (p.g1 - p.g0) * tt;
                        double rp = wi - d * fsc; if (rp < 0) rp += DM + 1;
                        int i1 = (int) rp; double fr = rp - i1;
                        double y1 = buf[i1 & DM];
                        v = y1 + (buf[(i1 + 1) & DM] - y1) * fr;
                        p.lp += p.kb * (v - p.lp);
                        v = p.lp * g;
                        b0 = p.band[0]; b1 = p.band[1]; b2 = p.band[2];
                    }
                    // Polar pattern per band (low < 400 Hz, mid, high > 3.5 kHz)
                    p.zlo += kBandLo * (v - p.zlo); p.zhi += kBandHi * (v - p.zhi);
                    sum += p.zlo * b0 + (p.zhi - p.zlo) * b1 + (v - p.zhi) * b2;
                }
                // Wind: the horn blows past the mic (proportional to speed squared, only while the horn mouth faces the mic)
                if (m.windAmt > 0 && rh.hz > 0.5) {
                    double c = chc * m.windC + chs * m.windS;
                    if (c > 0.3) {
                        double e = (c - 0.3) / 0.7; e *= e; e *= e;
                        double nz = fastRand() * 2 - 1;
                        m.windLp += kWind * (nz - m.windLp); m.windLp2 += kWind * (m.windLp - m.windLp2);
                        double sp = rh.hz / 6.8;
                        sum += m.windLp2 * e * sp * sp * m.windAmt * 0.9;
                    }
                }
                micSig[i] = sum;
            }
        }

        // ---------- FDN reverb: pre-delay, input diffusion, 16 modulated lines with 3-band decay, Hadamard mixing ----------
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
        if (micsOn) for (int i = 0; i < 6; i++) {
            MicRt& m = mic[i]; if (!micNeed(i)) continue;
            double o = 0;
            for (int j = 0; j < 16; j++) o += fdnBuf[j][(fdnIdx[j] - fdnLen[j] + 2 + m.fdnOff[j]) & FM] * m.fdnRow[j];
            // Mic sound: diffuse field (weighted by the polar pattern), proximity effect, frequency response, preamp trim
            double x = (micSig[i] + o * m.diffuse * micRevScale);
            if (m.hasProx) x = m.prox.run(x);
            for (int k = 0; k < m.nEq; k++) x = m.eq[k].run(x);
            micSig[i] = x * m.trim;
        }
        for (int i = 0; i < 16; i++) { fdnBuf[i][fdnIdx[i]] = (float) (rv[i] * 0.25 + inj * (0.8 + 0.2 * ((i * 7) % 5) / 4.0)); fdnIdx[i] = (fdnIdx[i] + 1) & FM; }

        if (lfeIdx >= 0 && lfeIdx < 8) acc[lfeIdx] = lfeLp2.run(lfeLp1.run(lfeIn)) * lfeLevel * 2;

        // ---------- Mixer: listener/mic crossfade, group matrix with delays, direct outs, soft clipping ----------
        modeMix += modeK * (modeT - modeMix);
        if (std::fabs(modeT - modeMix) < 1e-6) modeMix = modeT;
        const double lisG = std::cos(modeMix * 0.5 * PI), micG = std::sin(modeMix * 0.5 * PI);
        if (micsOn) {
            micOut.fill(0);
            const int di = chDelayIdx;
            for (int g = 0; g < MIC_GRP; g++) {
                double* C = mixC[g]; const double* T = mixT[g];
                for (int k = 0; k < 4; k++) C[k] += chK * (T[k] - C[k]);
                double xa = micNeed(2 * g) ? micSig[2 * g] : 0, xb = micNeed(2 * g + 1) ? micSig[2 * g + 1] : 0;
                auto& da = chDelay[2 * g]; auto& db = chDelay[2 * g + 1];
                da[(size_t) di] = (float) xa; db[(size_t) di] = (float) xb;
                if (grpDelayS[g] > 0) { int ri = (di - grpDelayS[g]) & (CHD - 1); xa = da[(size_t) ri]; xb = db[(size_t) ri]; }
                double l = (xa * C[0] + xb * C[2]) * micG, r = (xa * C[1] + xb * C[3]) * micG;
                micMeterAcc[2 * g] += l * l; micMeterAcc[2 * g + 1] += r * r;
                const int ol = grpOutL[g], orr = grpOutR[g];
                if (ol >= 0 && ol < 16) micOut[(size_t) ol] += l;
                if (orr >= 0 && orr < 16) micOut[(size_t) orr] += r;
            }
            chDelayIdx = (di + 1) & (CHD - 1);
        }
        for (int c = 0; c < nOut; c++) {
            double x = ((c < 8 ? acc[c] * lisG : 0) + (micsOn ? micOut[(size_t) c] : 0)) * master;
            out[c][n] = (float) (x > 0.9 ? 0.9 + 0.1 * std::tanh((x - 0.9) * 10) : x < -0.9 ? -0.9 + 0.1 * std::tanh((x + 0.9) * 10) : x);
        }
        wIdx = (wi + 1) & DM;
    }
    if (micsOn) {
        micMeterN += N;
        if (micMeterN >= 2048) { for (int c = 0; c < MIC_CH; c++) { micMeterVal[c] = (float) std::sqrt(micMeterAcc[c] / micMeterN) * (float) master; micMeterAcc[c] = 0; } micMeterN = 0; }
    }
}

} // namespace leslie
