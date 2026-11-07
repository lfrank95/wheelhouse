// Wheelhouse - DSP core, part of RotaryRoomDSP (member definitions; included at the end of RotaryRoomDSP.h).
// Organ: drawbars, key contacts, percussion, scanner vibrato, expression pedal, tonewheel runout.
#pragma once
#include "../RotaryRoomDSP.h"

namespace rotary {

inline void RotaryRoomDSP::drawbarAmps(int man, double* amps) const {
    // Linear amplitude per drawbar (3 dB per step). Percussion only affects the upper manual: the 1' drawbar is
    // muted and the "normal" (loud) percussion setting slightly attenuates the drawbars, as on the original console.
    double percAtt = man == 0 && percActive() && !perc.soft ? 0.72 : 1;
    int n = man == 2 ? 2 : 9;
    for (int i = 0; i < 9; i++) {
        if (i >= n || (man == 0 && i == 8 && percActive())) {
            amps[i] = 0;
            continue;
        }
        double lvl = man == 2 ? 1.15 : 1.0; // pedal a little louder (it has its own level curve on the original)
        amps[i] = drawbars[man][i] <= 0 ? 0 : std::pow(10.0, -(8 - drawbars[man][i]) * 3 / 20.0) * percAtt * lvl;
    }
}

// Schedules a gain change (key contact closing/opening) for tonewheel w on manual m. The contacts of the nine
// bus bars close at slightly different times (stagger) and may bounce, producing the characteristic key click.
inline void RotaryRoomDSP::schedule(int m, int w, double d, double baseDelay, double spreadScale, double bounceScale) {
    double t0 = sampleClock + baseDelay + fastRand() * staggerS * spreadScale * (m == 2 ? 1.6 : 1.0);
    events.push_back({t0, m, w, d, curRamp});
    // chatter: before it settles, the spring touches the bus bar a few times for tens of microseconds while it
    // wipes along (the "smack" of the click); the slower the key, the more of it
    if (spreadScale > 0.6 && fastRand() < 0.75) {
        const int n = 1 + (int) (3 * fastRand() * std::min(1.0, (spreadScale - 0.35) / 1.4));
        for (int b = 0; b < n; b++) {
            const double t = t0 - (0.0002 + 0.0014 * fastRand()) * fs, dur = (0.00003 + 0.00009 * fastRand()) * fs;
            events.push_back({t, m, w, d, curRamp});
            events.push_back({t + dur, m, w, -d, curRamp});
        }
    }
    if (fastRand() < std::min(0.95, bounceP * bounceScale)) {
        int n = fastRand() < 0.4 ? 2 : 1;
        double t = t0;
        for (int b = 0; b < n; b++) {
            double dt = (0.00015 + 0.0006 * fastRand()) * fs;
            events.push_back({t + dt, m, w, -d, curRamp});
            events.push_back({t + dt + (0.0001 + 0.0004 * fastRand()) * fs, m, w, d, curRamp});
            t += dt * 2;
        }
    }
}

// Key velocity -> contact behaviour. An organ has no velocity sensing, but how fast the key travels decides how
// the nine contact springs meet the bus bars: a fast stroke closes them almost together and hard (sharp click,
// more bounce), a slow press lets them close one after another and scrape (spread-out, softer click).
inline void RotaryRoomDSP::noteOn(int k, int man, int velocity) {
    int maxK = man == 2 ? 24 : 60;
    if (man < 0 || man > 2 || k < 0 || k > maxK || keys[man][k]) return;
    keys[man][k] = 1;
    if (man == 0) {
        bool wasEmpty = numKeysUpper == 0;
        numKeysUpper++;
        if (percActive() && wasEmpty) percEnv = perc.soft ? 0.55 : 1.0;
    }
    const double v = velocityClick ? std::max(1, std::min(127, velocity)) / 127.0 : 100 / 127.0;
    const double spread = 0.35 + 1.4 * std::pow(1 - v, 1.5), bounce = 0.55 + 0.6 * v;
    curRamp =
        (float) (0.35 + 2.2 * std::pow(v, 1.5)); // hard stroke: contacts snap on (sharp edge); slow press: they wipe on
    double amps[9];
    drawbarAmps(man, amps);
    // the click is the keyed signal being switched: no pulled drawbar, no click (888000000 = 1)
    double keyed = 0;
    for (double a : amps) keyed += a * a;
    keyed = std::min(1.2, std::sqrt(keyed / 3));
    clickEnv = std::max(clickEnv, (man == 2 ? 0.6 : 1.0) * (0.25 + 0.95 * v) * keyed);
    clickDecay = std::exp(-1 / ((0.0012 + 0.004 * std::pow(1 - v, 1.5)) * fs)); // slow press: longer scrape
    orgMech.key(man, std::max(1, std::min(127, velocity)) / 127.0, true);
    forParts(man, k, amps, [&](int w, double a) { schedule(man, w, a, 0, spread, bounce); });
    if (man == 0 && percActive()) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
}

inline void RotaryRoomDSP::noteOff(int k, int man, int velocity) {
    int maxK = man == 2 ? 24 : 60;
    if (man < 0 || man > 2 || k < 0 || k > maxK || !keys[man][k]) return;
    keys[man][k] = 0;
    if (man == 0) numKeysUpper--;
    // release velocity: many keyboards send none (0 or 64) -> medium
    const double v = (velocityClick && velocity > 0) ? std::min(127, velocity) / 127.0 : 0.5;
    double amps[9];
    drawbarAmps(man, amps);
    if (man == 2 && pedSusOn) {
        // sustain: no contact opens - each wheel's share is handed to the decaying sustain gain when its (staggered)
        // release would have happened, so pending note-on contacts of a very short press are still in order
        orgMech.key(man, velocity > 0 ? std::min(127, velocity) / 127.0 : 0.5, false);
        forParts(man, k, amps, [&](int w, double a) {
            events.push_back({sampleClock + fastRand() * staggerS * 1.6, man, w, -a, curRamp, true});
        });
        return;
    }
    double keyed = 0;
    for (double a : amps) keyed += a * a;
    keyed = std::min(1.2, std::sqrt(keyed / 3));
    clickEnv = std::max(clickEnv, 0.35 * (0.5 + v) * keyed);
    clickDecay = std::exp(-1 / ((0.0012 + 0.003 * (1 - v)) * fs));
    orgMech.key(man, velocity > 0 ? std::min(127, velocity) / 127.0 : 0.5, false);
    curRamp = (float) (0.5 + 1.2 * v);
    forParts(man, k, amps, [&](int w, double a) { schedule(man, w, -a, 0, 0.5 + 1.0 * (1 - v), 0.6 + 0.5 * v); });
    if (man == 0 && percActive()) {
        int w = wheelFor(k, perc.third ? 4 : 3);
        percG[w] = std::max(0.f, percG[w] - 1);
    }
}

// Recomputes all tonewheel target gains from the currently held keys (after drawbar or percussion changes).
inline void RotaryRoomDSP::retarget() {
    events.clear();
    std::fill(percG.begin(), percG.end(), 0.f);
    for (int m = 0; m < 3; m++) {
        std::fill(target[m].begin(), target[m].end(), 0.f);
        double amps[9];
        drawbarAmps(m, amps);
        int maxK = m == 2 ? 24 : 60;
        for (int k = 0; k <= maxK; k++) {
            if (!keys[m][k]) continue;
            forParts(m, k, amps, [&](int w, double a) { applyDelta(m, w, a); });
            if (m == 0 && percActive()) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
        }
    }
}

// Adds d to the target gain of wheel w and spreads crosstalk to its partners (same bin, filter panel, loom).
inline void RotaryRoomDSP::applyDelta(int m, int w, double d) {
    auto& t = target[m];
    t[w] += (float) d;
    const auto& L = xtalk[(size_t) w];
    for (int i = 0; i < xtalkN[(size_t) w]; i++) t[(size_t) L[(size_t) i].w] += (float) (d * L[(size_t) i].g);
}

// Sustained pedal release: the wheel's share (d < 0) leaves the keyed gain at once and continues in the sustain
// gain, which then decays - no step in the sum, no release click (the sustain units gated electronically).
inline void RotaryRoomDSP::transferSus(int w, double d) {
    auto move = [&](int wi, double dd) {
        target[2][(size_t) wi] += (float) dd;
        gain[2][(size_t) wi] += (float) dd;
        cgain[2][(size_t) wi] += (float) dd;
        pedSus[(size_t) wi] -= (float) dd;
    };
    move(w, d);
    const auto& L = xtalk[(size_t) w];
    for (int i = 0; i < xtalkN[(size_t) w]; i++) move(L[(size_t) i].w, d * L[(size_t) i].g);
    pedSusAny = true;
}

/** Tonewheel runout: modulation factor at the end of this sub-block (<= 66 Hz content, so a linear ramp is exact
 * enough). */
inline void RotaryRoomDSP::prepareRunout(int N) {
    for (int w = 1; w <= 91; w++) {
        double ph = runPh[w] + runInc[w] * N;
        ph -= std::floor(ph);
        runPh[w] = ph;
        double f = 1 + runScale * (runA1[w] * std::sin(TWO_PI * (ph + runP1[w])) +
                                   runA2[w] * std::sin(2 * TWO_PI * (ph + runP2[w])));
        runDF[w] = (f - runF[w]) / N;
    }
}

/** 1) Organ: tonewheels, manuals, key contacts, percussion, scanner vibrato, expression pedal -> orgBuf. */
inline void RotaryRoomDSP::renderOrgan(int N) {
    const double gs = gainStep;
    const double percDecay = std::exp(-1 / ((perc.fast ? percShortS : percLongS) * fs / 6.9));
    // contact smoothing (see cgain) and the band of the scrape noise (the contact spring on the bus bar: no
    // energy at the very top, nothing below the tone)
    const double kContact = 1 - std::exp(-1 / (0.00008 * fs));
    const double clickGain = 1.6 * click * std::pow(2.0, 2 * click - 1); // 1.6 x click, rising to twice that at 1
    const double kHp = 1 - std::exp(-2 * 3.14159265358979 * 400 / fs),
                 kLp = 1 - std::exp(-2 * 3.14159265358979 * 3200 / fs);
    const double percLevel = (perc.soft ? 0.6 : 1.0) * percLevelUser;
    const double vol = volume, expBass = (1 - vol) * 0.9 * vol * 2, expGain = 0.08 + 0.92 * vol * vol;
    if ((int) orgBuf.size() < N)
        orgBuf.assign((size_t) N, 0.f); // pre-sized in init(); only grows for unusually large host blocks
    if (pedSusAny) {
        bool any = false;
        for (float v : pedSus) any = any || v != 0;
        pedSusAny = any;
    }
    for (int n = 0; n < N; n++) {
        if (!events.empty()) {
            double now = sampleClock;
            for (size_t i = 0; i < events.size();) {
                if (events[i].t <= now) {
                    if (events[i].sus)
                        transferSus(events[i].w, events[i].d);
                    else
                        applyDelta(events[i].m, events[i].w, events[i].d);
                    rampMul[events[i].m][(size_t) events[i].w] = events[i].ramp;
                    events[i] = events.back();
                    events.pop_back();
                } else
                    i++;
            }
        }
        sampleClock++;

        double sU = 0, sL = 0, sP = 0, ps = 0, load = 0, hum = 0;
        for (int w = 1; w <= 91; w++) {
            double ph = phase[w] + inc[w];
            if (ph >= 1) ph -= 1;
            phase[w] = ph;
            float g0 = gain[0][w], g1 = gain[1][w], g2 = gain[2][w];
            const float t0 = target[0][w], t1 = target[1][w], t2 = target[2][w];
            if (g0 != t0) {
                const float st = (float) (gs * rampMul[0][w]);
                g0 = g0 < t0 ? std::min(t0, g0 + st) : std::max(t0, g0 - st);
                gain[0][w] = g0;
            }
            if (g1 != t1) {
                const float st = (float) (gs * rampMul[1][w]);
                g1 = g1 < t1 ? std::min(t1, g1 + st) : std::max(t1, g1 - st);
                gain[1][w] = g1;
            }
            if (g2 != t2) {
                const float st = (float) (gs * 0.6 * rampMul[2][w]);
                g2 = g2 < t2 ? std::min(t2, g2 + st) : std::max(t2, g2 - st);
                gain[2][w] = g2;
            }
            // the contact itself: smooth approach to the ramped value
            float& c0 = cgain[0][w];
            float& c1 = cgain[1][w];
            float& c2 = cgain[2][w];
            if (c0 != g0) c0 = std::abs(c0 - g0) < 1e-5f ? g0 : c0 + (float) kContact * (g0 - c0);
            if (c1 != g1) c1 = std::abs(c1 - g1) < 1e-5f ? g1 : c1 + (float) kContact * (g1 - c1);
            if (c2 != g2) c2 = std::abs(c2 - g2) < 1e-5f ? g2 : c2 + (float) kContact * (g2 - c2);
            g0 = c0;
            g1 = c1;
            g2 = c2;
            if (pedSusAny) { // released pedal notes fading out
                float& su = pedSus[w];
                if (su != 0) {
                    g2 += su;
                    su *= pedSusK;
                    if (std::abs(su) < 1e-6f) su = 0;
                }
            }
            const float pg = percG[w];
            const double gsum = (double) g0 + g1 + g2;
            if (gsum + leakHum > 1e-6 || pg > 0) {
                double x = ph * TAB;
                int i = (int) x;
                double f = x - i;
                const float* tab = (w <= 12 && complexLow) ? complexWave.data() : sine.data();
                double v = (tab[i] + (tab[i + 1] - tab[i]) * f) * wheelG[w] * (runF[w] + runDF[w] * (n + 1));
                hum += v;
                if (gsum > 1e-6 || pg > 0) {
                    if (gsum > 0.03 ||
                        pg > 0) { // harmonics only where a drawbar really feeds the wheel (crosstalk alone is < -38 dB)
                        // keyed wheel: add its 2nd and 3rd harmonic (phase-locked, read from the same sine table)
                        double p2 = ph * 2;
                        p2 -= (int) p2;
                        double p3 = ph * 3;
                        p3 -= (int) p3;
                        double x2 = p2 * TAB, x3 = p3 * TAB;
                        int i2 = (int) x2, i3 = (int) x3;
                        const float* sn = sine.data();
                        v += ((sn[i2] + (sn[i2 + 1] - sn[i2]) * (x2 - i2)) * harm2[w] +
                              (sn[i3] + (sn[i3 + 1] - sn[i3]) * (x3 - i3)) * harm3[w]) *
                             wheelG[w];
                    }
                    sU += v * g0;
                    sL += v * g1;
                    sP += v * g2;
                    ps += v * pg;
                    load += gsum;
                }
            }
        }
        double comp = 1 / (1 + busK * load);
        double sVib = ((vibUpper ? sU : 0) + (vibLower ? sL : 0)) * comp;
        double sDry = ((vibUpper ? 0 : sU) + (vibLower ? 0 : sL) + sP) * comp + hum * leakHum;
        // Percussion bypasses the vibrato (as in the original wiring)
        sDry += ps * percEnv * percLevel * 0.9;
        percEnv *= percDecay;
        if (clickEnv > 1e-4) {
            const double nz = fastRand() * 2 - 1;
            clickLp += kLp * (nz - clickLp);
            clickHp += kHp * (clickLp - clickHp);
            sDry += (clickLp - clickHp) * clickEnv * clickGain; // 0.5: about as loud as one drawbar at 8; 1: +12 dB
            clickEnv *= clickDecay;
        }
        sVib *= organGain;
        sDry *= organGain;

        // scanner vibrato / chorus: the LC line and the rotating capacitor (a circuit, see ScannerVibrato.h);
        // the manuals routed past it stay in sDry
        double s = sDry + scanner.process(sVib);
        s = (s + expLp.run(s) * expBass) * expGain;
        // mains hum enters after the expression pedal (preamp heaters, supply): audible even with the swell closed
        mainsPh += mainsHz / fs;
        if (mainsPh >= 1) mainsPh -= 1;
        {
            const double a = TWO_PI * mainsPh;
            s += humLevel * (0.5 * std::sin(a) + std::sin(2 * a) + 0.3 * std::sin(3 * a) + 0.15 * std::sin(4 * a));
        }
        orgBuf[(size_t) n] = (float) s;
    }

    for (int w = 1; w <= 91; w++) runF[w] += runDF[w] * N;
}

} // namespace rotary
