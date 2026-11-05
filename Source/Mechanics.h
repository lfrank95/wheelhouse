#pragma once
// Wheelhouse - the parts of the instrument that are not loudspeakers: the Rotary cabinet as a wooden box, and the
// mechanical noises of Rotary and organ. Everything here produces *static* sound sources (they do not rotate);
// RotaryRoomDSP places them in the room and at the microphones.
//
// Sources for the numbers (see README, "Gehäuse und Mechanik"):
//   * Rotary 122: 41" x 29" x 20.5" cabinet, three compartments; the middle one holds the drivers and works as a
//     vented box for the 15" woofer; the louvres in the top and bottom compartments let the sound out. Early
//     cabinets are plywood, later ones particle board, which flexes more and buzzes more easily.
//   * Rotary motors are AC induction motors (one fast and one slow per rotor); the slow motor's armature drops onto
//     a rubber rim on the fast motor pulley when it is switched on ("clunk"), speed changes go through a relay
//     ("definite click"), belts, idler and rotor bearings rumble, the drum's scrim cloth rustles at fast speed.
//   * Organ generator: synchronous run motor (two torque pulses per mains cycle), main drive shaft at 20 rev/s
//     with 24 bakelite driving gears (two each of 12 sizes), the whole generator under a felt blanket.
//   * Keys and pedals: the key hits its felt at the bottom of the stroke and again at the top when released.
// Levels are estimates: they are set so that the noise sits where it does on recordings of real instruments
// (audible in pauses and close up, covered as soon as you play), not measured.
#include <cmath>
#include <vector>
#include <array>
#include <cstdint>

namespace rotary {

// ---------------------------------------------------------------------------------------------------------------
// Cabinet body: woofer vented box, air modes of the lower compartment, plate modes of the wooden panels
// ---------------------------------------------------------------------------------------------------------------
struct CabinetBody {
    enum Board { PLYWOOD = 0, PARTICLE = 1 };

    void prepare(double sampleRate) { fs = sampleRate; configure(board, amount); }

    /** board: plywood (early cabinets) or particle board (from about 1969); amount 0..1 scales the panel radiation. */
    void configure(int b, double amt) {
        board = b; amount = amt;
        // Vented box (middle compartment): 4th-order high-pass, -3 dB near 40 Hz (Butterworth sections Q 0.54 / 1.31)
        boxHp1.set("hp", 40, 0.541, fs); boxHp2.set("hp", 40, 1.307, fs);
        // Axial air modes of the drum compartment (about 0.70 m wide, 0.48 m deep): f = c / 2L
        cav1.set("peak", 343.0 / (2 * 0.70), 6, fs, 1.5); cav2.set("peak", 343.0 / (2 * 0.48), 6, fs, 1.2);
        // Plate modes. f_mn = k * pi/2 * sqrt(D / (rho h)) * ((m/a)^2 + (n/b)^2), D = E h^3 / (12 (1 - nu^2));
        // k = 1.4 for edges that are glued/screwed (between simply supported and clamped).
        const double E = board == PARTICLE ? 3.2e9 : 8.0e9, rho = board == PARTICLE ? 700 : 600, nu = 0.3;
        const double Q = board == PARTICLE ? 14 : 28;              // particle board is far more damped ...
        const double level = (board == PARTICLE ? 1.25 : 1.0) * amount;   // ... but much softer, so it moves more
        struct Panel { double a, b, h; uint8_t drive; double g; };
        static const Panel P[] = {
            {1.04, 0.52, 0.019, 1, 1.0},    // side panels (full height), mostly pushed by the woofer box
            {0.74, 0.52, 0.019, 0, 0.7},    // top, above the horn
            {0.70, 0.36, 0.0127, 0, 0.8},   // upper back panel (removable)
            {0.70, 0.48, 0.0127, 1, 0.9},   // lower back panel (removable)
            {0.70, 0.25, 0.019, 1, 0.8},    // front board of the woofer compartment
            {0.74, 0.52, 0.019, 1, 0.5},    // bottom
        };
        modes.clear();
        for (const auto& p : P) {
            const double D = E * p.h * p.h * p.h / (12 * (1 - nu * nu));
            const double c0 = 1.4 * 1.5707963 * std::sqrt(D / (rho * p.h));
            const int MN[3][2] = {{1, 1}, {2, 1}, {1, 2}};
            for (int i = 0; i < 3; i++) {
                double f = c0 * (MN[i][0] * MN[i][0] / (p.a * p.a) + MN[i][1] * MN[i][1] / (p.b * p.b));
                if (f > fs * 0.4) continue;
                Mode m; m.bp.set("bp", f, Q, fs); m.drive = p.drive;
                // higher modes radiate less (more cancellation between the moving zones)
                m.g = (float) (0.5 * level * p.g * (i == 0 ? 1.0 : 0.45));
                modes.push_back(m);
            }
        }
    }

    /** Woofer through vented box and compartment. */
    inline double woofer(double lo) { return cav2.run(cav1.run(boxHp2.run(boxHp1.run(lo)))); }

    /** Panels: driven by the pressure in the horn compartment (hi) and in the woofer box (lo); adds to up / low. */
    inline void tick(double hi, double lo, double& up, double& low) {
        for (auto& m : modes) { double y = m.bp.run(m.drive ? lo : hi) * m.g; if (m.drive) low += y; else up += y; }
    }

    struct Mode { Biquad bp; float g = 0; uint8_t drive = 0; };
    std::vector<Mode> modes;
    Biquad boxHp1, boxHp2, cav1, cav2;
    double fs = 48000, amount = 0.5; int board = PLYWOOD;
};

// ---------------------------------------------------------------------------------------------------------------
// Rotary mechanics: motors, belts and bearings, drum scrim, relay and slow-motor engagement
// ---------------------------------------------------------------------------------------------------------------
struct RotaryMechanics {
    void prepare(double sampleRate) {
        fs = sampleRate;
        rumble[0].set("lp", 180, 0.8, fs); rumble[1].set("lp", 140, 0.8, fs);
        scrim1.set("hp", 220, 0.7, fs); scrim2.set("lp", 1400, 0.7, fs);
        relayBp.set("bp", 2600, 1.6, fs); relayLow.set("bp", 900, 2.0, fs);
        clunkBp.set("bp", 95, 1.3, fs); clunkHi.set("bp", 420, 2.0, fs);
        buzzBp.set("bp", 600, 1.0, fs);
        kEnv = 1 - std::exp(-1.0 / (0.05 * fs));
        relayDec = std::exp(-1.0 / (0.0025 * fs)); clunkDec = std::exp(-1.0 / (0.03 * fs));
    }
    /** level 0..1 (0 = a freshly serviced cabinet), cond 0..1 (organ condition: worn springs clunk harder). */
    void setLevel(double lvl, double cond) { level = lvl; wear = cond; }
    void setMains(double hz) { mainsHz = hz; }
    /** Speed switch: 0 = stop, 1 = slow (chorale), 2 = fast (tremolo). */
    void speedChange(int from, int to) {
        if (from == to) return;
        pendRelay = (int) (0.004 * fs);                                // relay pulls in a few ms after the switch
        relayBounce = (int) (0.011 * fs);
        if (to == 1) { pendClunk[0] = (int) (0.06 * fs); pendClunk[1] = (int) (0.085 * fs); clunkAmp = 1.0; }   // slow motors drop onto the rim
        else if (from == 1) { pendClunk[0] = (int) (0.03 * fs); pendClunk[1] = (int) (0.045 * fs); clunkAmp = 0.35 + 0.65 * wear; }  // ... and lift off
        state = to;
    }

    /** One sample: adds the upper (horn) and lower (drum, amp chassis) compartment sound to up / low. */
    inline void tick(const Rotor& rh, const Rotor& rd, double& up, double& low) {
        if (level <= 0) return;
        motPh += mainsHz / fs; if (motPh >= 1) motPh -= 1;
        const double a = 6.283185307179586 * motPh;
        // Induction motor hum: magnetic forces at twice the mains frequency (+ harmonics). Fast motors: a smooth hum;
        // slow motors are small shaded-pole motors: weaker but buzzier.
        const double s2 = std::sin(2 * a), s4 = std::sin(4 * a), s6 = std::sin(6 * a);
        const double humFast = s2 + 0.35 * s4 + 0.12 * s6, humSlow = 0.6 * s2 + 0.5 * s4 + 0.4 * s6;
        fastEnv += kEnv * ((state == 2 ? 1.0 : 0.0) - fastEnv); slowEnv += kEnv * ((state == 1 ? 1.0 : 0.0) - slowEnv);
        const double nz1 = rnd() * 2 - 1, nz2 = rnd() * 2 - 1, nz3 = rnd() * 2 - 1;
        double buzz = buzzBp.run(nz3) * slowEnv * (0.4 + 0.6 * wear) * (0.5 + 0.5 * std::fabs(s2));
        const double motor = humFast * fastEnv * 0.9 + humSlow * slowEnv * 0.6 + buzz * 0.5;
        // Belts, idler and rotor bearings: low rumble growing with speed, a little stronger once per revolution
        const double vh = rh.fast > 0 ? rh.hz / rh.fast : 0, vd = rd.fast > 0 ? rd.hz / rd.fast : 0;
        const double rumH = rumble[0].run(nz1) * vh * (1 + 0.5 * rh.lastSin) * (0.5 + wear);
        const double rumD = rumble[1].run(nz2) * vd * (1 + 0.6 * rd.lastSin) * (0.6 + wear) * 1.6;
        // Drum scrim cloth and air around the drum: rustle that grows with the square of the speed
        const double scr = scrim2.run(scrim1.run(nz2 - nz1)) * vd * vd * (1 + 0.3 * rd.lastCos) * 0.7;
        // Relay (amp chassis, lower compartment) and slow-motor engagement
        double ev = 0, evUp = 0;
        if (pendRelay > 0 && --pendRelay == 0) relayEnv = 1;
        if (relayBounce > 0 && --relayBounce == 0) relayEnv += 0.35;
        if (relayEnv > 1e-4) { ev += (relayBp.run(nz3) + 0.6 * relayLow.run(nz1)) * relayEnv * 6; relayEnv *= relayDec; }
        for (int r = 0; r < 2; r++) if (pendClunk[r] > 0 && --pendClunk[r] == 0) clunkEnv[r] = clunkAmp;
        if (clunkEnv[0] > 1e-4 || clunkEnv[1] > 1e-4) {
            const double c = (clunkBp.run(nz1) * 3 + clunkHi.run(nz2)) * (0.5 + wear);
            evUp += c * clunkEnv[0]; ev += c * clunkEnv[1];
            clunkEnv[0] *= clunkDec; clunkEnv[1] *= clunkDec;
        }
        const double g = level * 0.0011;
        up += g * (motor * 0.5 + rumH + evUp);
        low += g * (motor * 0.6 + rumD + scr + ev);
    }

    double fs = 48000, level = 0.5, wear = 0.5, mainsHz = 50, motPh = 0;
    double fastEnv = 0, slowEnv = 1, kEnv = 0.001, relayEnv = 0, relayDec = 0.99, clunkEnv[2] = {0, 0}, clunkDec = 0.99, clunkAmp = 1;
    int state = 1, pendRelay = 0, relayBounce = 0, pendClunk[2] = {0, 0};
    Biquad rumble[2], scrim1, scrim2, relayBp, relayLow, clunkBp, clunkHi, buzzBp;
    Lcg rnd{0x1E5Eu};
};

// ---------------------------------------------------------------------------------------------------------------
// Organ mechanics: tone generator (gear mesh, whir), run motor, key and pedal action
// ---------------------------------------------------------------------------------------------------------------
struct OrganMechanics {
    /** gears: the 12 driving gear tooth counts on the 20 rev/s main shaft. */
    void prepare(double sampleRate, const int (*gears)[2]) {
        fs = sampleRate;
        Lcg r(0x6EA5u);
        for (int i = 0; i < 12; i++) {
            gearInc[i] = 20.0 * gears[i][0] / fs;                     // mesh frequency = shaft speed x teeth (1.3-2.2 kHz)
            gearAmp[i] = 0.3 + 0.7 * r();                              // fixed per organ
            gearPh[i] = r();
            gearMod[i] = 0.002 + 0.004 * r();                          // small eccentricity (frequency wobble at shaft rate)
        }
        felt.set("lp", 2600, 0.6, fs);
        whir1.set("hp", 250, 0.6, fs); whir2.set("lp", 1600, 0.6, fs);
        keyHp.set("hp", 1400, 0.7, fs); keyLp.set("lp", 6500, 0.7, fs); keyThock.set("bp", 340, 2.2, fs);
        pedThump.set("bp", 85, 1.4, fs); pedKnock.set("bp", 520, 2.0, fs);
        keyDec = std::exp(-1.0 / (0.005 * fs)); thockDec = std::exp(-1.0 / (0.013 * fs));
        thumpDec = std::exp(-1.0 / (0.05 * fs)); knockDec = std::exp(-1.0 / (0.016 * fs));
    }
    void setLevels(double motor, double keys) { motorLevel = motor; keyLevel = keys; }
    void setMains(double hz) { mainsHz = hz; }
    /** Key or pedal action. v = velocity 0..1; the key reaches its felt a few ms after the contacts closed. */
    void key(int man, double v, bool down) {
        if (keyLevel <= 0) return;
        Voice& k = voice[next]; next = (next + 1) % (int) voice.size();
        const double amp = keyLevel * (0.15 + 0.85 * std::pow(v, 1.5)) * (down ? 1.0 : 0.35);
        k.pedal = man == 2; k.amp = amp;
        k.wait = (int) ((down ? 0.002 + 0.010 * (1 - v) : 0.006) * fs) + 1;
        k.e1 = k.e2 = 0;
    }

    /** One sample of the console sound. */
    inline double tick() {
        double y = 0;
        if (motorLevel > 0) {
            shaftPh += 20.0 / fs; if (shaftPh >= 1) shaftPh -= 1;
            const double sh = std::sin(6.283185307179586 * shaftPh);
            double g = 0;
            for (int i = 0; i < 12; i++) {
                gearPh[i] += gearInc[i] * (1 + gearMod[i] * sh); if (gearPh[i] >= 1) gearPh[i] -= 1;
                g += gearAmp[i] * sinFast(gearPh[i]);
            }
            const double nz = rnd() * 2 - 1;
            const double whir = whir2.run(whir1.run(nz)) * (1 + 0.25 * sh);
            motPh += mainsHz / fs; if (motPh >= 1) motPh -= 1;
            const double a = 6.283185307179586 * motPh;
            const double pulse = std::sin(2 * a) + 0.4 * std::sin(4 * a) + 0.2 * std::sin(6 * a);   // run motor: two torque pulses per cycle
            y += motorLevel * 0.0003 * (felt.run(g * 0.08 + whir * 0.9) + pulse * 0.35);
        }
        // Key voices: the envelopes are summed first and shape one noise source, so every filter runs once per sample
        double kc = 0, kt = 0, pt = 0, pk = 0;
        for (auto& k : voice) {
            if (k.wait > 0) { if (--k.wait == 0) { k.e1 = k.amp; k.e2 = k.amp; } continue; }
            if (k.e1 < 1e-5 && k.e2 < 1e-5) continue;
            if (k.pedal) { pt += k.e1; pk += k.e2; k.e1 *= thumpDec; k.e2 *= knockDec; }
            else { kc += k.e1; kt += k.e2; k.e1 *= keyDec; k.e2 *= thockDec; }
        }
        if (kc + kt + pt + pk > 0) ringOut = (int) (0.15 * fs);
        if (ringOut > 0) {   // keep the filters running a little after the envelopes ended so they ring out
            ringOut--;
            const double nz = rnd() * 2 - 1;
            y += (keyLp.run(keyHp.run(nz * kc)) + keyThock.run(nz * kt) * 1.5) * 0.0012;
            y += (pedThump.run(nz * pt) * 2.5 + pedKnock.run(nz * pk)) * 0.006;
        }
        return y;
    }

    static inline double sinFast(double ph) { const double x = ph * 6.283185307179586 - 3.141592653589793; return -x * (1.2732395 - 0.4052847 * std::fabs(x)); }

    struct Voice { int wait = 0; double amp = 0, e1 = 0, e2 = 0; bool pedal = false; };
    std::array<Voice, 12> voice{}; int next = 0, ringOut = 0;
    double fs = 48000, motorLevel = 0.4, keyLevel = 0.5, mainsHz = 50, shaftPh = 0, motPh = 0;
    double gearPh[12] = {}, gearInc[12] = {}, gearAmp[12] = {}, gearMod[12] = {};
    double keyDec = 0.99, thockDec = 0.99, thumpDec = 0.99, knockDec = 0.99;
    Biquad felt, whir1, whir2, keyHp, keyLp, keyThock, pedThump, pedKnock;
    Lcg rnd{0x0B3Bu};
};

} // namespace rotary
