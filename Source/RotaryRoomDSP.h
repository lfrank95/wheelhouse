// Wheelhouse - DSP core (C++ port of processor7.js)
// Signal chain: tonewheel organ -> scanner vibrato -> expression (swell) pedal -> Rotary amplifier
// -> crossover -> rotating sources placed in a room (image sources and wall patches, Doppler,
// cabinet directivity) -> 16-line feedback delay network (FDN) reverb -> speaker panning.
// This file deliberately contains no JUCE code so the core can be built and tested without the framework.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <algorithm>
#include "RotaryAmp.h"
#include "OrganPresets.h"
#include "OrganPreamp.h"
#include "Workers.h"
#include "RotorRadiation.h"

#include "core/Basics.h"
#include "MicRig.h"
#include "SpringReverb.h"
#include "Mechanics.h"
#include "ScannerVibrato.h"
namespace rotary {

class RotaryRoomDSP {
public:
    explicit RotaryRoomDSP(double sampleRate) : fs(sampleRate), amp(sampleRate), pre(sampleRate), spring(sampleRate) {
        init();
    }

    // ---------------- Control API (mirrors the messages of the original audio worklet) ----------------
    // Manuals: 0 = upper (swell), 1 = lower (great), 2 = pedal
    /** Key down. k = key index (0..60 for manuals, 0..24 for pedals), man = manual (0/1/2). */
    void noteOn(int k, int man = 0, int velocity = 100);
    /** Key up. Same arguments as noteOn(). */
    void noteOff(int k, int man = 0, int velocity = 64);
    /** All notes off: clears held keys, pending contact events, tonewheel targets and percussion. */
    void panic() {
        for (int m = 0; m < 3; m++) {
            std::fill(keys[m].begin(), keys[m].end(), 0);
            std::fill(target[m].begin(), target[m].end(), 0.f);
        }
        numKeysUpper = 0;
        events.clear();
        std::fill(percG.begin(), percG.end(), 0.f);
        std::fill(pedSus.begin(), pedSus.end(), 0.f);
        pedSusAny = false;
    }
    /** Drawbar positions 0..8 for one manual (9 values; the pedal uses only the first 2). On the manuals this is the
        drawbar set last chosen with the A# / B preset key (what the host's drawbar parameters move). */
    void setDrawbars(const int* v, int man = 0) {
        if (man == 2) {
            for (int i = 0; i < 2; i++) drawbars[2][i] = v[i];
            retarget();
            return;
        }
        setDrawbarSet(man, dbEdit[man], v);
    }
    /** One of the two drawbar sets of a manual: set 0 = A (left, preset key A#), 1 = B (right, preset key B). */
    void setDrawbarSet(int man, int set, const int* v) {
        if (man < 0 || man > 1 || set < 0 || set > 1) return;
        for (int i = 0; i < 9; i++) dbSets[man][set][i] = std::max(0, std::min(8, v[i]));
        if (presetK[man] == 10 + set) applyRegistration(man);
    }
    /** Reverse-coloured preset keys of a manual, as on the B-3 / C-3: 0 = C (cancel: the manual is silent),
        1..9 = C#..A (factory registrations), 10 = A# (drawbar set A), 11 = B (drawbar set B). Percussion works
        only on the upper manual's B key - its trigger is the B set's 1' contact. */
    void setPresetKey(int man, int k) {
        if (man < 0 || man > 1 || k < 0 || k > 11) return;
        presetK[man] = k;
        if (k >= 10) dbEdit[man] = k - 10;
        applyRegistration(man);
    }
    int presetKey(int man) const { return man >= 0 && man <= 1 ? presetK[man] : 11; }
    int drawbarEditSet(int man) const { return man >= 0 && man <= 1 ? dbEdit[man] : 1; }
    /** Factory registration of preset key k (1..9) of a manual (B-3 / C-3 as shipped, OrganPresets.h). */
    static const int* factoryPreset(int man, int k) { return presets::registration(man, k); }
    /** Pedal sustain: after a pedal is released its tone fades out (length 1 short .. 5 long) instead of stopping -
        not on the B-3 / C-3 / A-100 (their pedal is keyed like the manuals), but on the spinets, H-100 / X-66, a
        European A-100 variant and the later Hammond clones. */
    void setPedalSustain(bool on, int length) {
        pedSusOn = on;
        static const double T60[5] = {0.35, 0.7, 1.3, 2.2, 3.5};
        pedSusK = (float) std::pow(10.0, -3.0 / (T60[std::max(1, std::min(5, length)) - 1] * fs));
    }
    /** Percussion switches: on/off, harmonic (third = 4' / second = 2 2/3'), decay fast/slow, volume soft/normal. */
    void setPerc(const Perc& p) {
        perc = p;
        retarget();
    }
    /** Routes the scanner vibrato to the upper and/or lower manual (the vibrato tabs on the console). */
    void setVibRouting(bool upper, bool lower) {
        vibUpper = upper;
        vibLower = lower;
    }
    /** Vibrato/chorus knob position: "v1".."v3" (vibrato) or "c1".."c3" (chorus); anything else = off. */
    void setVib(const std::string& v) {
        vibMode = v;
        scanner.setMode(v);
    }
    /** Mains frequency (50 or 60 Hz): only the hum changes; the synchronous motor is geared for 20 Hz main-shaft speed
     * either way. */
    void setMains(double hz) {
        mainsHz = hz >= 55 ? 60 : 50;
        rotaryMech.setMains(mainsHz);
        orgMech.setMains(mainsHz);
    }
    /** Rotary cabinet body: board 0 = plywood (early), 1 = particle board (late); resonance 0..1 = panel radiation;
        mechanics 0..1 = motor/belt/relay noise (negative = derive from the organ condition). */
    void setCabinetBody(int board, double resonance, double mechanics) {
        cabBoard = board;
        cabRes = resonance;
        mechUser = mechanics;
        body.configure(board, resonance);
        applyCharacter();
    }
    /** Acoustic noise of keys and pedals hitting their felts, 0..1. */
    void setKeyNoise(double v) {
        keyNoise = v;
        applyCharacter();
    }
    /** Key click follows the key velocity (how fast the key closes the nine contacts). Off = fixed medium velocity. */
    void setVelocityClick(bool on) { velocityClick = on; }
    /** Rotary amplifier drive, 0..1 (also forwarded to the amp model). */
    void setDrive(double v) {
        drive = v;
        amp.setDrive(v);
    }
    /** Amplifier model index, simulation quality level, and power-supply sag amount 0..1. */
    void setAmp(int model, int quality, double sagAmount) {
        amp.setModel(model);
        amp.setQuality(quality);
        amp.setSag(sagAmount);
        pre.setQuality(quality);
    }
    /** Organ preamp stage on/off and its gain 0..1 (see OrganPreamp). */
    /** Supply sag only (cheap); model/quality switches go through ampModel() so the host can prepare them off the audio
     * thread. */
    void setAmpSag(double sagAmount) { amp.setSag(sagAmount); }
    /** Delay added by the circuit models (oversampling filters of preamp and power amp), in samples. */
    int processingLatency() const { return amp.latency() + (pre.isEnabled() ? pre.latency() : 0); }
    amp::RotaryAmp& ampModel() { return amp; }
    amp::OrganPreamp& preampModel() { return pre; }
    void setPreamp(bool on, double gain) {
        pre.setEnabled(on);
        pre.setGain(gain);
    }
    /** Spring reverb: on/off, wet mix 0..1, decay time T60 in seconds, tone 0..1, drive 0..1, number of springs. */
    void setSpring(bool on, double mixAmt, double t60, double toneAmt, double driveAmt, int springs) {
        spring.setEnabled(on);
        spring.setMix(mixAmt);
        spring.setDecay(t60);
        spring.setTone(toneAmt);
        spring.setDrive(driveAmt);
        spring.setSprings(springs);
    }
    /** Physically "kicks" the spring tank (the classic crash); strength 0..1. */
    void kickSpring(double strength) { spring.kick(strength); }
    /** Rotor speeds: r = 0 horn, 1 drum; slow/fast speed in Hz, acceleration/deceleration time constants in seconds. */
    void setRotor(int r, double slowHz, double fastHz, double upTau, double dnTau) {
        auto& R = rot[r];
        R.slow = slowHz;
        R.fast = fastHz;
        R.upTau = upTau;
        R.dnTau = dnTau;
        setRotary(rotaryMode);
    }
    /** Motor brake: true = rotors stop quickly in "stop" mode, false = they coast down slowly. */
    void setBrake(bool on) { brake = on; }
    /** Horn deflectors fitted (stock) or removed. */
    void setDeflector(bool on) {
        deflector = on;
        dirHorn = on ? &dirHornDefl : &dirHornOpen;
        cabHorn = on ? &cabHornDefl : &cabHornOpen;
    }
    bool hasDeflector() const { return deflector; }
    /** Cabinet detail: level of the horn-compartment reflection 0..1, rotor speed wobble 0..1 (negative = derive from
     * Character). */
    void setCabinetDetail(double hornBox, double wobble) {
        hornBoxLevel = hornBox;
        wobbleOverride = wobble;
        applyCharacter();
    }
    /** Organ detail: percussion level (linear), short/long percussion decay in seconds, use harmonic-rich waveforms for
     * the lowest 12 wheels. */
    void setOrganDetail(double percLevel, double percShort, double percLong, bool complexWheels) {
        percLevelUser = percLevel;
        percShortS = percShort;
        percLongS = percLong;
        complexLow = complexWheels;
    }
    /** Output stage: master gain in dB and LFE low-pass cutoff in Hz. */
    void setOutput(double masterDb, double lfeHz) {
        masterGain = std::pow(10.0, masterDb / 20);
        lfeLp1.set("lp", lfeHz, 0.70710678, fs);
        lfeLp2.set("lp", lfeHz, 0.70710678, fs);
    }
    /** Monitoring trims: rear speakers (|azimuth| > 90 deg) and the centre speaker in dB; centre divergence 0..1
       spreads a frontal source from the centre speaker into L/R (0 = all centre, 1 = phantom centre only). */
    void setOutputTrims(double rearDb, double centerDb, double divergence) {
        trimRear = rearDb;
        trimCenter = centerDb;
        centerDiv = std::max(0.0, std::min(1.0, divergence));
        prepSpeakers();
    }
    /** Current plate (B+) supply voltage of the amp model, for the UI. */
    double ampPlate() const { return amp.plateSupply(); }
    /** Current cathode voltage of the amp model, for the UI. */
    double ampCathode() const { return amp.cathodeVoltage(); }
    /** Active amp simulation quality level. */
    /** Key click level 0..1 (contact noise on key press/release): 0.5 about as loud as one drawbar, 1 = +12 dB. */
    void setClick(double v) { click = v; }
    /** Expression (swell) pedal position 0..1; also shapes the bass response like the original pedal. */
    void setVolume(double v) { volume = v; }
    /** Horn/drum balance 0..1: 0 = horn only, 0.5 = both full, 1 = drum only. */
    void setBalance(double v) {
        hornLevel = std::min(1.0, 2 * (1 - v));
        drumLevel = std::min(1.0, 2 * v);
    }
    /** Rotary speed switch: "slow", "fast" or anything else for "stop". */
    void setRotary(const std::string& mode) {
        auto code = [](const std::string& m) {
            return m == "fast" ? 2 : m == "slow" ? 1 : 0;
        };
        rotaryMech.speedChange(code(rotaryMode), code(mode));
        rotaryMode = mode;
        for (auto& r : rot) r.tgt = mode == "fast" ? r.fast : mode == "slow" ? r.slow : 0;
    }
    /** Replaces the room description (dimensions in metres, absorption, material) and rebuilds all paths. */
    void setRoom(const Room& r) {
        room = r;
        rebuildRoom();
    }
    /** Precision of the reflections: false = fast (higher orders ramped per sub-block, direction buckets),
        true = exact per-sample rendering of every image up to room.order. Exact at order >= 4 is for offline rendering.
     */
    /** Number of CPU threads used for the room rendering (1 = single-threaded). */
    void setThreads(int n) {
        pool.setThreads(n);
        partials.resize((size_t) std::max(1, pool.getThreads()));
    }
    int getThreads() const { return pool.getThreads(); }
    void setExactPaths(bool exact) {
        if (exact != exactPaths) {
            exactPaths = exact;
            rebuildRoom();
        }
    }
    /** Direct access to the room for in-place edits; call roomChanged() afterwards. */
    Room& roomRef() { return room; }
    /** Rebuilds image sources and reverb after the room was edited via roomRef(). */
    void roomChanged() { rebuildRoom(); }
    /** Instrument "character": tonewheel condition, crosstalk/leakage, contact quality, motor noise (all 0..1), plus
     * gear-tuning and bus-bar compression switches. */
    void setCharacter(const Character& c) {
        chr = c;
        applyCharacter();
    }
    /** Positions in metres: Rotary (lx, ly) and listener (hx, hy) with listener yaw in radians; jump = snap instead of
     * glide. The has* flags say which parts are valid. */
    void setPos(bool hasLes, double lx, double ly, bool hasLis, double hx, double hy, bool hasYaw, double yaw,
                bool jump);
    /** Speaker layout: azimuths in degrees, output channel per speaker, LFE channel index (-1 = none), stereo flag. */
    void setSpeakers(const Speakers& s);
    /** LFE send level (linear). */
    void setLfe(double v) { lfeLevel = v; }
    /** Cabinet orientation (yaw) in radians; jump = snap instead of glide. */
    void setCabYaw(double v, bool jump) {
        cabTyaw = v;
        if (jump) cabYaw = v;
    }

    /** Derived room data for the UI: RT60 per band in seconds, critical distance in metres, number of paths. */
    RoomInfo info() const { return roomInfo; }
    /** For tests: impulse response of the late-reverb loop alone (n samples, first speaker's output row). Clears the
     * reverb. */
    std::vector<float> reverbImpulse(int n);
    /** Snapshot of the rendered paths (direct, image sources up to second order, active wall patches) for display.
        Call on the audio thread between blocks; cheap (about 100 directivity lookups). */
    void fillPathViz(PathViz& v) const;
    // Microphone rig
    /** Microphone setup (types, patterns, positions, mixer groups) - see MicRig.h. */
    void setMics(const MicSetup& m) {
        micSetup = m;
        micConfigure();
    }
    /** Current microphone setup. */
    const MicSetup& mics() const { return micSetup; }
    /** Level per mixer channel (RMS, linear) for the meter display. */
    float micMeter(int c) const { return c >= 0 && c < MIC_CH ? micMeterVal[c] : 0.f; }
    /** Current horn rotor angle in radians. */
    double hornAngle() const { return rot[0].dir > 0 ? rot[0].ang : TWO_PI - rot[0].ang; }
    /** Current drum rotor angle in radians. */
    double drumAngle() const { return rot[1].dir > 0 ? rot[1].ang : TWO_PI - rot[1].ang; }
    /** +1 counter-clockwise, -1 clockwise (seen from above). */
    int rotorDir(int r) const { return rot[r].dir; }
    /** Current horn rotor speed in Hz. */
    double hornHz() const { return rot[0].hz; }
    /** Current drum rotor speed in Hz. */
    double drumHz() const { return rot[1].hz; }

    /** Renders N samples into out[c][n]. Channels 0..7 carry the speaker/room mix; 8..15 are mic direct outs only. */
    void process(float* const* out, int nCh, int N);

    const double fs;

private:
    void init();
    void processBlock(float* const* out, int nCh, int N); // one sub-block (<= 64 samples), called by process()
    // Steps of processBlock, in this order (see there)
    struct MixParams {
        int N = 0, nSp = 0, nOut = 0, nSpk = 0, lfeIdx = -1;
        bool lisOn = true, micsOn = false;
        double chK = 0, modeK = 0, modeT = 0, revGain = 0, master = 1;
    };
    double smoothMotion(int N);
    void updatePathGeometry(double bt);
    void buildFastWorkingSet(int N);
    void prepareFdnModulation(int N);
    void prepareRunout(int N);
    void renderOrgan(int N);
    void renderRotors(int N);
    void mixListenerSample(int n, const MixParams& mp, double& lfeIn);
    void micInputSample(int n, const MixParams& mp);
    void reverbSample(int n, const MixParams& mp);
    void fdnRead(double tt, double* rv);
    void outputSample(float* const* out, int n, const MixParams& mp, double lfeIn);
    void applyCharacter();
    void rebuildRoom();
    void rebuildFdnOut();
    void prepSpeakers();
    void panGains(double az, double hfrac, float* out) const;
    void cabinet(double dx, double dy, double dz, double& g, double& fc) const;
    // Cabinet radiation tables (CabinetDirectivity.h): table coordinates of a departure direction (real cabinet
    // frame, not normalised) and of a rotor's mouth angle (cos, dir * sin), and the band gains for them
    void cabDir(double dx, double dy, double dz, float& zi, float& ei) const;
    float mouthIdx(double c, double s) const;
    void radiate(bool horn, float ai, float zi, float ei, float* gb) const;
    void schedule(int m, int w, double d, double baseDelay);
    void retarget();
    void applyDelta(int m, int w, double d);
    int wheelFor(int key, int i) const {
        int w = key + 13 + DB_OFF[i];
        if (w < 13) w += 12;
        while (w > 91) w -= 12;
        return w;
    }
    // Calls f(wheel, amplitude) for every tonewheel a key contributes to on manual m.
    // Pedal (B-3/C-3 wiring, patent 2,508,514): every pedal has 8 contacts on 4 bus bars, two harmonics each -
    // brown: fundamental + 3rd, red: 2nd + 4th, orange: 6th + 8th, yellow: 10th + 12th. The fundamental is a complex
    // tonewheel (odd harmonics) for the lowest octave. The resistance wires R81..R86 on the resistor panel mix the
    // four bars into the two drawbars: 16' takes all four with falling level (brown direct, then 10, 47, 470 ohm),
    // 8' only red/orange/yellow (5, 5, 20 ohm), so pulling 8' adds the upper even harmonics. The levels below assume
    // the drawbar circuit loads the wires with about 5 ohm; the level ratio inside each pair is an estimate.
    static constexpr int PED_OFF[8] = {0, 19, 12, 24, 31, 36, 40, 43}; // semitones above the pedal fundamental
    static constexpr double PED_PAIR[8] = {1.0, 0.5, 1.0, 0.7, 1.0, 0.7, 1.0, 0.7};
    static constexpr int PED_BUS[8] = {0, 0, 1, 1, 2, 2, 3, 3};
    static constexpr double PED_MIX[2][4] = {{1.0, 0.33, 0.096, 0.0105}, {0.0, 0.58, 0.58, 0.23}};
    template <class F> void forParts(int m, int k, const double* amps, F f) const {
        if (m == 2) {
            for (int c = 0; c < 8; c++) {
                const double a = amps[0] * PED_MIX[0][PED_BUS[c]] + amps[1] * PED_MIX[1][PED_BUS[c]];
                if (a > 0) f(std::min(91, k + 1 + PED_OFF[c]), a * PED_PAIR[c]);
            }
            return;
        }
        for (int i = 0; i < 9; i++)
            if (amps[i] > 0) f(wheelFor(k, i), amps[i]);
    }
    void drawbarAmps(int man, double* amps) const;
    // One-pole low-pass coefficient for cutoff f (Hz), clamped below Nyquist
    double kOf(double f) const { return 1 - std::exp(-TWO_PI * std::min(f, fs * 0.45) / fs); }
    // Air absorption low-pass for a path of length d metres (see AIR_DB_PER_M_8K)
    double airK(double d) const {
        const double a = std::pow(10.0, 0.1 * AIR_DB_PER_M_8K * std::max(d, 0.5)) - 1; // (8 kHz / fc)^2
        return kOf(std::min(20000.0, std::max(AIR_HF_MIN_HZ, 8000.0 / std::sqrt(std::max(a, 1e-9)))));
    }

    // Semitone offset of each drawbar relative to the 8' fundamental (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3',
    // 1')
    static constexpr int DB_OFF[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
    // Driving/driven gear tooth counts of the original generator, one pair per semitone (gives the slightly detuned
    // "gear tuning")
    static constexpr int GEARS[12][2] = {{85, 104}, {71, 82}, {67, 73}, {105, 108}, {103, 100}, {84, 77},
                                         {74, 64},  {98, 80}, {96, 74}, {88, 64},   {67, 46},   {108, 70}};

    // ---------------- Tonewheel generator (91 wheels, index 1..91) ----------------
    static constexpr int TAB = 4096;
    std::vector<float> sine, complexWave;
    std::array<double, 92> phase{}, inc{}, incNew{}, incGear{};
    std::array<float, 92> percG{}, wheelLvl{}, wheelVar{}, wheelG{};
    // Tonewheel runout: every disc is slightly bent or sits slightly off-centre on its shaft, so the air gap to the
    // pickup changes once per shaft revolution -> amplitude modulation at the shaft speed (13-33 Hz). Amount and
    // direction (phase) are random per wheel and fixed for this organ; fine-toothed upper wheels react more.
    std::array<double, 92> runPh{}, runInc{}, runA1{}, runA2{}, runP1{}, runP2{}, runF{}, runDF{};
    double runScale = 0;
    // Crosstalk partners per wheel (generator bins, filter panels / loom), recomputed by applyCharacter()
    struct Xtalk {
        int w;
        float g;
    };
    std::array<std::array<Xtalk, 8>, 92> xtalk{};
    std::array<int, 92> xtalkN{};
    std::array<float, 92> xtVar{}; // per-organ scatter of the loom/filter-panel crosstalk
    // Harmonics of the raw tonewheel signal (pickup gap, tooth profile) after the tonewheel filters
    std::array<float, 92> harm2{}, harm3{}, harm2Raw{}, harm3Raw{};
    // Mains hum
    double mainsHz = 50, mainsPh = 0, humLevel = 0;
    // Key velocity
    bool velocityClick = true;
    float curRamp = 1; // ramp speed of the events being scheduled right now (set by noteOn/noteOff)
    std::array<float, 92> gain[3]{}, target[3]{};
    // what the key contact actually passes: the ramped gain smoothed once more (the spring wipes onto the bus bar
    // and the contact resistance falls gradually - no kink at the end of the ramp, hence a softer click)
    std::array<float, 92> cgain[3]{};
    std::array<uint8_t, 61> keys[3]{};
    int numKeysUpper = 0;
    int drawbars[3][9] = {{8, 8, 8, 0, 0, 0, 0, 0, 0}, {0, 0, 8, 4, 0, 0, 0, 0, 0}, {6, 3, 0, 0, 0, 0, 0, 0, 0}};
    // the two drawbar sets per manual (A, B), the latched preset key and the set the drawbar controls move
    int dbSets[2][2][9] = {{{8, 8, 8, 8, 0, 0, 0, 0, 0}, {8, 8, 8, 0, 0, 0, 0, 0, 0}},
                           {{0, 0, 8, 8, 4, 0, 0, 0, 0}, {0, 0, 8, 4, 0, 0, 0, 0, 0}}};
    int presetK[2] = {11, 11}, dbEdit[2] = {1, 1};
    inline bool percActive() const { return perc.on && presetK[0] == 11; }
    void applyRegistration(int man) {
        const int k = presetK[man];
        const int* src = k == 0 ? nullptr : k >= 10 ? dbSets[man][k - 10] : factoryPreset(man, k);
        for (int i = 0; i < 9; i++) drawbars[man][i] = src ? src[i] : 0;
        retarget();
    }
    // pedal sustain: released pedal notes move here and fade out with pedSusK per sample
    bool pedSusOn = false, pedSusAny = false;
    float pedSusK = 0.9999f;
    std::array<float, 92> pedSus{};
    void transferSus(int w, double d); // contact event of a sustained pedal release
    bool vibUpper = true, vibLower = false;
    double percLevelUser = 1.0, percShortS = 0.2, percLongS = 1.0;
    bool complexLow = true;
    bool brake = true;
    double hornBoxLevel = 0.45, wobbleOverride = -1, masterGain = 1.0;
    amp::RotaryAmp amp;
    amp::OrganPreamp pre;
    SpringReverb spring;
    std::vector<float> orgBuf, loBuf, hiBuf; // organ signal; what woofer and horn radiate (from the amp's network)
    Perc perc;
    double percEnv = 0, click = 0.5, clickEnv = 0, clickDecay = 0.99, organGain = 0.05;
    double clickHp = 0, clickLp = 0; // contact scrape noise: band-limited (one-pole high and low pass states)
    struct Event {
        double t;
        int m;
        int w;
        double d;
        float ramp;
        bool sus = false;
    }; // ramp: contact closing speed (1 = nominal); sus: pedal release handed over to the sustain decay
    std::array<float, 92> rampMul[3]{}; // per manual and wheel: speed of the current gain transition
    std::vector<Event> events;
    double sampleClock = 0;
    Lcg fastRand{777};

    // ---------------- Instrument character: crosstalk/leakage, key-contact behaviour, bus-bar compression, motor
    // ----------------
    Character chr;
    double leakHum = 0, staggerS = 0, bounceP = 0, gainStep = 0, busK = 0, motorLevel = 0, rotorWobble = 0, sagAmt = 0;

    // ---------------- Scanner vibrato: the LC line and the capacitive scanner as a circuit (ScannerVibrato.h) ----
    std::string vibMode = "c3";
    scan::Scanner scanner;

    // ---------------- Expression pedal, crossover and cabinet filters ----------------
    // expLp: bass boost of the expression pedal; cabLp: horn-compartment colouration (crossover, drivers and horn
    // are a circuit in Speakers.h, driven by the amp); lfeLp1/2: LFE low-pass; windBp1/2:
    // horn wind
    double drive = 0.35, volume = 0.8;
    Biquad expLp, cabLp, lfeLp1, lfeLp2, windBp1, windBp2;
    int cabDelay = 0;
    RotorDir dirHornDefl, dirHornOpen, dirDrum;
    const RotorDir* dirHorn = &dirHornDefl;
    // the same rotors as heard outside the louvred cabinet (box shadowing, diffraction, beaming of the openings)
    CabinetDir cabHornDefl, cabHornOpen, cabDrum;
    const CabinetDir* cabHorn = &cabHornDefl;
    bool deflector = true;
    LRBandSplit splitH, splitD;            // in-phase bands of the horn / drum signal (radiation model)
    std::vector<float> bandH[4], bandD[3]; // ring buffers of those bands, same indexing as hornRing / drumRing
    const float* bandHp[4] = {};
    const float* bandDp[3] = {};
    double cabYaw = -1.5707963, cabTyaw = -1.5707963; // current / target cabinet orientation (radians)

    // ---------------- Cabinet body and mechanical noise (static sources, see Mechanics.h) ----------------
    // upperCabRing / lowerCabRing: sound radiated by the upper / lower part of the cabinet itself (panels, motors,
    // relay); consoleRing: the organ console (generator, run motor, keys). All three are placed in the room without
    // rotation.
    CabinetBody body;
    CompartmentReverb hornBox; // reverberant field of the upper compartment (feeds hornCabRing)
    RotaryMechanics rotaryMech;
    OrganMechanics orgMech;
    int cabBoard = 0;
    double cabRes = 0.5, mechUser = -1, keyNoise = 0.5;
    std::vector<float> upperCabRing, lowerCabRing, consoleRing;
    struct StaticPath {
        double d0 = 0, d1 = 0;
        float g0[8] = {}, g1[8] = {};
        bool init = false;
    };
    StaticPath stCab, stKey; // listener: cabinet and console
    struct MicStatic {
        double d0[3] = {}, d1[3] = {}, g0[3] = {}, g1[3] = {};
        bool init = false;
    };
    MicStatic micSt[6]; // per mic: upper cabinet, lower cabinet, console
    double consoleX = 0, consoleY = 0;
    // Reverb feed: what six directions around the cabinet receive from the rotating horn and drum (Doppler, horn
    // directivity, drum AM, cabinet). The late reverb is built from these, so it carries the rotation's chorus and,
    // per delay line, a little of its movement - instead of a static copy of the amplifier signal.
    static constexpr int RDIR = 6;
    float rdZi[RDIR] = {}, rdEi[RDIR] = {}; // reverb feed directions as cabinet-table coordinates
    double rdX[RDIR] = {}, rdY[RDIR] = {}, rdCab[RDIR] = {}, rdLpH[RDIR] = {}, rdLpD[RDIR] = {}, rdZd[RDIR] = {},
           rdSig[RDIR] = {};
    void staticBlock(bool lisOn, bool micsOn);
    inline double readStatic(const std::vector<float>& b, int wi, double ds) const {
        double rp = wi - ds;
        if (rp < 0) rp += DM + 1;
        int i1 = (int) rp;
        double fr = rp - i1, y1 = b[(size_t) (i1 & DM)];
        return y1 + (b[(size_t) ((i1 + 1) & DM)] - y1) * fr;
    }

    // ---------------- Rotors and rotor delay lines ----------------
    // rot[0] = horn, rot[1] = drum. hornRing/drumRing hold the horn/drum signals, hornCabRing the horn signal coloured
    // by the cabinet; every acoustic path reads from these ring buffers with a fractional (Doppler-shifting) delay.
    Rotor rot[2];
    std::string rotaryMode = "slow";
    double hornLevel = 1, drumLevel = 1;
    int DL = 65536, DM = DL - 1; // rotor delay lines: power of two >= 1.2 s so even 5th-order images of a 40 m room fit
    std::vector<float> hornRing, drumRing, hornCabRing;
    int wIdx = 0;

    // ---------------- Room: Rotary/listener positions and acoustic paths (image sources and wall patches)
    // ----------------
    Room room;
    struct Pos {
        double x, y, tx, ty;
    } rotaryPos{3, 6, 3, 6};
    struct ListenerPos {
        double x, y, tx, ty, yaw, tyaw, z;
    } listener{3, 2.5, 3, 2.5, 1.5707963, 1.5707963, 1.2};
    std::vector<Path> paths;
    std::vector<FastPathRT> fastRT;
    std::vector<int> fastIdx, exactIdx; // rebuilt every sub-block from `paths`
    // ---- parallel room rendering: per-sample values of the sub-block and one partial sum per job ----
    static constexpr int SUB = 64;
    const SincInterp& sinc = SincInterp::get();
    WorkerPool pool;
    int blockN = 0, listenerJobs = 0;
    bool blockLisOn = true, blockMicsOn = false;
    std::array<int, SUB> writeIdxBuf{};
    std::array<double, SUB> hornCosBuf{}, hornSinBuf{}, drumCosBuf{}, drumSinBuf{}, hornSigBuf{}, drumSigBuf{},
        staticRevFeed{};
    std::array<float, SUB> hornAiBuf{}, drumAiBuf{}; // mouth angles as cabinet-table coordinates, per sample
    double micRaw[6][SUB] = {};
    void renderListenerJob(int job);
    void renderMicJob(int micIndex);
    void renderJob(int job) {
        if (job < listenerJobs)
            renderListenerJob(job);
        else
            renderMicJob(job - listenerJobs);
    }
    int nRefl = 0;
    bool exactPaths = false;
    RoomInfo roomInfo;
    double rt60[OCT_BANDS] = {};

    // ---------------- Speaker layout and panning ----------------
    // Direct paths are panned with pairwise (VBAP-style) gains between the two nearest speakers; higher-order paths are
    // summed into NB azimuth buckets (plus one omnidirectional bucket) that are panned once per sample.
    Speakers spk;
    double lfeLevel = 0.5;
    struct SpkS {
        double a;
        int ch;
    };
    std::vector<SpkS> spkSorted;
    double trimRear = 0, trimCenter = 0, centerDiv = 0;
    std::array<float, 8> spkTrim{}; // per speaker slot, applied at the output
    int centerCh = -1, centerL = -1, centerR = -1;
    static constexpr int NB = 24;
    /** Direction -> continuous bucket coordinate in [0, NB). A path is spread over the two nearest buckets with a
        sliding weight, so a moving image glides from one bucket to the next instead of being switched over (that
        switch, within one sub-block and into a different diffuser, was audible as a jag on Slow, close up, dry). */
    static double bucketX(double az) {
        double x = az / TWO_PI * NB;
        x -= std::floor(x / NB) * NB;
        return x >= NB ? 0 : x;
    }
    static void setBx(Path& p, double x) {
        if (!p.init)
            p.bx0 = p.bx1 = x;
        else {
            p.bx0 = p.bx1;
            double d = x - p.bx0;
            if (d > NB / 2.0)
                d -= NB;
            else if (d < -NB / 2.0)
                d += NB;
            p.bx1 = p.bx0 + d;
        }
    }
    /** Adds a sharp part vh and a wide part ve at coordinate x (linear weights: both are panned coherently; the wide
        part is decorrelated afterwards, per speaker channel). */
    static inline void addBuckets(float* bk, double x, float vh, float ve) {
        while (x >= NB) x -= NB;
        while (x < 0) x += NB;
        const int i = (int) x;
        const int j = i + 1 == NB ? 0 : i + 1;
        const float f = (float) (x - i);
        if (vh != 0) {
            bk[i] += vh * (1 - f);
            bk[j] += vh * f;
        }
        if (ve != 0) {
            bk[NB + i] += ve * (1 - f);
            bk[NB + j] += ve * f;
        }
    }
    struct Partial {
        double acc[SUB][8];
        float bucket[SUB][2 * NB];
        double lfe[SUB];
    }; // one per rendering job; buckets 0..NB-1 horizontal, NB..2NB-1 elevated (wide)
    std::vector<Partial> partials;
    std::vector<std::array<float, 8>> bucketG;
    std::array<float, 2 * NB> bucket{};
    // Diffuser for the wide (scattered / elevated) sound: two allpasses per azimuth bucket. Real floor and ceiling
    // bounces reach the ears from other elevations than the direct sound and do not comb-filter it; mixed into the
    // same loudspeaker channel they would, so they are decorrelated here first (energy preserved).
    static constexpr int DIFF_LEN = 512, DIFF_ST = 4;
    std::vector<float> diffBuf; // one diffuser per speaker channel (indices 0..7 used) x DIFF_ST stages x DIFF_LEN
    std::array<int, DIFF_ST * NB> diffLen{};
    int diffIdx = 0;
    std::array<int, NB> diffActive{};      // samples left until a silent channel diffuser can be skipped
    inline float diffuse(int k, float x) { // four allpasses in series (g 0.6): the coherent remainder is 0.6^4
        int& act = diffActive[(size_t) k];
        if (x != 0)
            act = 24 * DIFF_LEN;
        else if (act > 0) {
            if (--act == 0) std::fill_n(diffBuf.data() + (size_t) (DIFF_ST * k) * DIFF_LEN, DIFF_ST * DIFF_LEN, 0.f);
        } else
            return 0; // bucket has been silent for a while: nothing rings any more
        float* b = diffBuf.data() + (size_t) (DIFF_ST * k) * DIFF_LEN;
        for (int st = 0; st < DIFF_ST; st++, b += DIFF_LEN) {
            const float d = b[(diffIdx - diffLen[(size_t) (DIFF_ST * k + st)]) & (DIFF_LEN - 1)], y = d - 0.6f * x;
            b[diffIdx] = x + 0.6f * y;
            x = y;
        }
        return x;
    }

    // ---------------- FDN reverb (16 modulated delay lines, 6 input allpasses, 16 per-line allpasses, 3-band decay)
    // ----------------
    static constexpr int FM = 65535;
    std::vector<std::vector<float>> fdnBuf, apBuf, lapBuf;
    std::array<int, 16> fdnLen{}, fdnIdx{}, lapLen{}, lapIdx{};
    std::array<int, 6> apLen{}, apIdx{};
    std::array<double, 16> fdnModPh{}, fdnModInc{}, fdnOff0{}, fdnOffD{};
    std::array<OctaveDecay, 16> fdnDecay{};
    std::array<std::array<double, 2 * (OCT_BANDS - 1)>, 16> fdnZ{};
    double fdnModDepth = 4, fdnInGain = 0;
    std::vector<float> preBuf;
    int preIdx = 0, preLen = 480;
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
    std::vector<float> chDelay[6];
    int chDelayIdx = 0;
    static constexpr int CHD = 8192;
    double mixT[MIC_GRP][4] = {},
           mixC[MIC_GRP][4] = {}; // per group: A->L, A->R, B->L, B->R (mixT = target, mixC = smoothed current)
    int grpDelayS[MIC_GRP] = {0}, grpOutL[MIC_GRP] = {-1, -1, -1}, grpOutR[MIC_GRP] = {-1, -1, -1};
    double modeMix = 0; // 0 = listener mode, 1 = microphone mode (crossfade)
    double micMeterAcc[MIC_CH] = {0};
    float micMeterVal[MIC_CH] = {0};
    int micMeterN = 0;
    std::array<double, 16> micOut{};
    double kBandLo = 0.05, kBandHi = 0.3, kWind = 0.02, micRevScale = 1, roomReflG = 0.8, roomHfRefl = 0.9,
           roomScatter = 0.35;
    double roomScatterG = 0; // sqrt(scatter) x wall reflection: amplitude of the diffuse share
    bool patchesDirty = true;
    double patchAt[4] = {-1e9, -1e9, -1e9, -1e9}; // cabinet / listener position the patch weights were computed for
    void updatePatches();
    double micSig[6] = {0};
    bool micWasConfigured = false;

    void micConfigure();
    void micRebuildPaths();
    void micPlaceClose(double a);
    bool micNeed(int i) const {
        return mic[i].want || (i % 2 == 1 && std::fabs(mixC[i / 2][2]) + std::fabs(mixC[i / 2][3]) > 1e-5);
    }
    void micBlock(double bt);
    void micFdnRows();
    void schedule(int m, int w, double d, double baseDelay, double spreadScale, double bounceScale);
};

} // namespace rotary

// Member definitions, grouped by topic
#include "core/Setup.h"
#include "core/Organ.h"
#include "core/Room.h"
#include "core/Mics.h"
#include "core/Render.h"
