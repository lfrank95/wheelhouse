// Wheelhouse - DSP core, part of RotaryRoomDSP (member definitions; included at the end of RotaryRoomDSP.h).
// Setup: wavetables, tonewheel tuning and tolerances, fixed filters, rotor parameters, instrument character.
#pragma once
#include "../RotaryRoomDSP.h"

namespace rotary {

// Initialisation: wavetables, tonewheel frequencies (gear-derived and equal-tempered), per-wheel level
// and tolerance, fixed filters, rotor parameters, default 8-channel speaker ring, reverb buffers.
inline void RotaryRoomDSP::init() {
    Lcg rnd(20261104u);
    sine.resize(TAB + 1);
    complexWave.resize(TAB + 1);
    for (int i = 0; i <= TAB; i++) {
        double x = TWO_PI * i / TAB;
        sine[i] = (float) std::sin(x);
        complexWave[i] = (float) (0.83 * (std::sin(x) + 0.30 * std::sin(3 * x) + 0.14 * std::sin(5 * x) +
                                          0.06 * std::sin(7 * x) + 0.025 * std::sin(9 * x)));
    }
    for (int w = 1; w <= 91; w++) {
        int n = (w - 1) % 12, oct = (w - 1) / 12;
        int teeth = 2 << oct;
        const int* g = GEARS[n];
        if (w >= 85) {
            teeth = 192;
            g = GEARS[(n + 5) % 12];
        }
        double fGear = 20.0 * g[0] / g[1] * teeth, fNew = 32.7032 * std::pow(2.0, (w - 1) / 12.0);
        incGear[w] = fGear / fs;
        incNew[w] = fNew / fs;
        phase[w] = rnd();
        wheelLvl[w] = (float) (w < 13 ? 1.15 : w < 49 ? 1.0 : 1 - (w - 48) / 43.0 * 0.42);
        wheelVar[w] = (float) (rnd() * 2 - 1);
    }
    {
        Lcg rr(0x5C4A7u);
        for (int w = 1; w <= 91; w++) {
            int n = (w - 1) % 12, oct = (w - 1) / 12;
            int teeth = 2 << oct;
            const int* g = GEARS[n];
            if (w >= 85) {
                teeth = 192;
                g = GEARS[(n + 5) % 12];
            }
            runInc[w] = 20.0 * g[0] / g[1] / fs;         // shaft revolutions per sample (main shaft 20 Hz x gear ratio)
            double sens = std::sqrt(teeth / 192.0);      // relative gap sensitivity: finer teeth, stronger effect
            double amt = 0.3 + 0.7 * rr();               // how badly this particular disc runs out
            runA1[w] = sens * amt;                       // once per revolution (eccentricity / bend)
            runA2[w] = sens * amt * (0.15 + 0.3 * rr()); // twice per revolution (a bent disc wobbles unevenly)
            runP1[w] = rr();
            runP2[w] = rr();
            runPh[w] = rr();
            runF[w] = 1;
        }
        Lcg rh(0xA7B3u);
        for (int w = 1; w <= 91; w++) {
            // raw harmonic content of each pickup: 2nd -45..-30 dB, 3rd -50..-36 dB (gap and tooth shape vary per
            // wheel)
            harm2Raw[w] = (float) std::pow(10.0, -(30 + 15 * rh()) / 20);
            harm3Raw[w] = (float) std::pow(10.0, -(36 + 14 * rh()) / 20);
            xtVar[w] = (float) std::pow(10.0, (rh() * 2 - 1) * 6 / 20); // +-6 dB per organ
        }
    }
    configureScanner();
    applyCharacter();
    vibBuf.assign(2048, 0.f);
    expLp.set("lp", 180, 0.7, fs);
    lp1.set("lp", 800, 0.6, fs);
    lp2.set("lp", 4500, 0.5, fs);
    hp1.set("hp", 800, 0.6, fs);
    hp2.set("hp", 560, 0.7, fs);
    cabDelay = (int) std::lround(0.0005 * fs); // first slat reflection; the compartment's own decay follows
    cabLp.set("lp", 3200, 0.7, fs);
    hornRes.set("peak", 1700, 1.4, fs, 3.5);
    hornRes2.set("peak", 4200, 2.0, fs, 2.0);
    drumRes.set("peak", 120, 1.1, fs, 3.0);
    hornHi.set("lp", 9500, 0.6, fs);
    lfeLp1.set("lp", 100, 0.70710678, fs);
    lfeLp2.set("lp", 100, 0.70710678, fs);
    windBp1.set("hp", 1500, 0.7, fs);
    windBp2.set("lp", 5000, 0.7, fs);
    // radii: acoustic centres from the BEM simulations (horn mouth with deflector 178 mm, drum opening 11-15 cm);
    // the drum turns against the horn as in the 122
    rot[0] = {0, 0.75, 0.75, 0.75, 6.8, 0.178, 0.95, 0.8, 1.4};
    rot[1] = {1.3, 0.62, 0.62, 0.62, 5.7, 0.13, 0.40, 4.5, 5.5};
    rot[1].dir = -1;
    DL = 1;
    while (DL < fs * 1.2) DL <<= 1;
    DM = DL - 1;
    hornRing.assign(DL, 0.f);
    drumRing.assign(DL, 0.f);
    hornCabRing.assign(DL, 0.f);
    upperCabRing.assign(DL, 0.f);
    lowerCabRing.assign(DL, 0.f);
    consoleRing.assign(DL, 0.f);
    body.prepare(fs);
    hornBox.prepare(fs);
    rotaryMech.prepare(fs);
    orgMech.prepare(fs, GEARS);
    spk.az = {0, 45, 90, 135, 180, -135, -90, -45};
    for (auto& a : spk.az) a *= 3.14159265358979 / 180;
    spk.ch = {0, 1, 2, 3, 4, 5, 6, 7};
    fdnBuf.assign(16, std::vector<float>(65536, 0.f));
    apBuf.assign(6, std::vector<float>(4096, 0.f));
    lapBuf.assign(16, std::vector<float>(2048, 0.f));
    for (int i = 0; i < 16; i++) {
        fdnModPh[i] = rnd();
        fdnModInc[i] = (0.1 + 0.4 * rnd()) / fs;
    }
    orgBuf.assign(4096, 0.f);
    events.reserve(4096);
    for (auto& r : rampMul) r.fill(1.f);
    pool.setJob([this](int job) { renderJob(job); });
    partials.resize(1);
    dirHornDefl.build(rotordir::HORN_DEFLECTOR);
    dirHornOpen.build(rotordir::HORN_OPEN);
    dirDrum.build(rotordir::DRUM);
    dirHorn = deflector ? &dirHornDefl : &dirHornOpen;
    cabHornDefl.build(cabdir::HORN_DEFLECTOR, cabdir::CAB_NA, cabdir::CAB_NZ, cabdir::CAB_NE, 4);
    cabHornOpen.build(cabdir::HORN_OPEN, cabdir::CAB_NA, cabdir::CAB_NZ, cabdir::CAB_NE, 4);
    cabDrum.build(cabdir::DRUM, cabdir::CAB_NA, cabdir::CAB_NZ, cabdir::CAB_NE, 3);
    cabHorn = deflector ? &cabHornDefl : &cabHornOpen;
    splitH.prepare(rotordir::HORN_EDGES, 3, fs);
    splitD.prepare(rotordir::DRUM_EDGES, 2, fs);
    for (auto& b : bandH) b.assign(DL, 0.f);
    for (auto& b : bandD) b.assign(DL, 0.f);
    for (int b = 0; b < 4; b++) bandHp[b] = bandH[b].data();
    for (int b = 0; b < 3; b++) bandDp[b] = bandD[b].data();
    kLoBand = 1 - std::exp(-TWO_PI * 250 / fs);
    kHiBand = 1 - std::exp(-TWO_PI * 3500 / fs);
    preBuf.assign(8192, 0.f);
    revInK = 1 - std::exp(-TWO_PI * 6000 / fs);
    revHpK = 1 - std::exp(-TWO_PI * 45 / fs);
    prepSpeakers();
    rebuildRoom();
}

// Derives all "character" dependent coefficients: wheel tuning/levels, crosstalk gains, key-contact timing,
// bus-bar compression, motor noise, rotor wobble and amp ageing.
inline void RotaryRoomDSP::applyCharacter() {
    const auto& c = chr;
    for (int w = 1; w <= 91; w++) inc[w] = c.gearTune ? incGear[w] : incNew[w];
    for (int w = 1; w <= 91; w++) wheelG[w] = (float) (wheelLvl[w] * std::pow(10.0, wheelVar[w] * 1.5 * c.cond / 20));
    // runout depth of the worst (top) wheels: ~0.2 % when new, ~1.6 % for a well-kept 60s organ (cond 0.45), ~4 % worn
    // out
    runScale = 0.002 + 0.045 * std::pow(c.cond, 1.5);
    // ---- Generator crosstalk, following the real layout ----
    // 24 shielded bins, each holding the same note in four octaves: wheels b, b+12, b+48, b+60 (b = 1..12 and 25..36);
    // positions above 91 are balance discs without pickups. The two wheels on the same side of the drive shaft
    // (b and b+48, b+12 and b+60) are the closest magnetically. Weaker, non-harmonic crosstalk comes from the
    // filter panels and the wiring loom: neighbours on the terminal strip (chromatic), scattered per organ.
    {
        const double sameSide = std::pow(10.0, -(46 - 14 * c.leak) / 20),
                     crossBin = std::pow(10.0, -(54 - 14 * c.leak) / 20);
        const double loom1 = std::pow(10.0, -(64 - 14 * c.leak) / 20), loom2 = loom1 * 0.5;
        for (int w = 1; w <= 91; w++) {
            int n = 0;
            auto& L = xtalk[(size_t) w];
            auto add = [&](int t, double g) {
                if (t >= 1 && t <= 91 && t != w && n < 8) L[(size_t) n++] = {t, (float) g};
            };
            const int oct = (w - 1) / 12;                                     // 0..7
            const bool groupA = oct == 0 || oct == 1 || oct == 4 || oct == 5; // bins b = 1..12 hold octaves 0, 1, 4, 5
            const int base = (w - 1) % 12 + (groupA ? 1 : 25);
            const int members[4] = {base, base + 12, base + 48, base + 60};
            for (int t : members)
                if (t != w) add(t, std::abs(t - w) == 48 ? sameSide : crossBin); // 48 apart = same side of the shaft
            add(w - 1, loom1 * xtVar[(size_t) w]);
            add(w + 1, loom1 * xtVar[(size_t) w]);
            add(w - 2, loom2 * xtVar[(size_t) w]);
            add(w + 2, loom2 * xtVar[(size_t) w]);
            xtalkN[(size_t) w] = n;
        }
    }
    // ---- Tonewheel harmonics: grow a little with age (magnets drift closer, pole pieces wear); the tonewheel filters
    //      on the upper wheels take most of it out again (up to -12 dB at the top) ----
    for (int w = 1; w <= 91; w++) {
        double filt = w <= 43 ? 1.0 : std::pow(10.0, -12.0 * (w - 43) / 48.0 / 20);
        double age = 1 + 0.6 * c.cond;
        harm2[(size_t) w] = (float) (harm2Raw[(size_t) w] * filt * age);
        harm3[(size_t) w] = (float) (harm3Raw[(size_t) w] * filt * age);
    }
    // ---- Mains hum (power supply ripple and heater/transformer fields): ~-74 dB new, ~-60 dB worn ----
    humLevel = 0.3 * std::pow(10.0, -(74 - 14 * c.leak) / 20);
    leakHum = std::pow(10.0, -(78 - 16 * c.leak) / 20);
    staggerS = (0.0008 + 0.0045 * c.contact) * fs;
    bounceP = 0.15 + 0.7 * c.contact;
    // contact closing time: a snapping spring closes in well under 0.1 ms (the click is nearly the full step of the
    // switched sine, loud and with its bass thump); worn, dirty contacts close more gradually
    gainStep = 1 / ((0.0001 + 0.00025 * (1 - c.contact)) * fs);
    busK = c.busComp ? 0.035 + 0.05 * c.cond : 0;
    // Mechanics: the organ's generator/run motor follows "Motor"; the Rotary's motors, belts and relay follow its own
    // setting or, on auto, the condition (a serviced cabinet is quiet, a road-worn one rumbles and clunks)
    const double lesMechLvl = mechUser >= 0 ? mechUser : 0.15 + 0.7 * c.cond;
    rotaryMech.setLevel(lesMechLvl, c.cond);
    orgMech.setLevels(c.motor, keyNoise);
    motorLevel = 0.008 * lesMechLvl;
    rotorWobble = wobbleOverride >= 0 ? 0.004 + 0.05 * wobbleOverride : 0.012 + 0.03 * c.cond;
    sagAmt = 0.25 + 0.35 * c.cond;
    amp.setAge(0.6 * c.cond);
}

} // namespace rotary
