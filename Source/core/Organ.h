// Wheelhouse - DSP core, part of RotaryRoomDSP (member definitions; included at the end of RotaryRoomDSP.h).
// Organ: drawbars, key contacts, percussion, scanner vibrato, expression pedal, tonewheel runout.
#pragma once
#include "../RotaryRoomDSP.h"

namespace rotary {

inline void RotaryRoomDSP::drawbarAmps(int man, double* amps) const {
    // Linear amplitude per drawbar (3 dB per step). Percussion only affects the upper manual: the 1' drawbar is
    // muted and the "normal" (loud) percussion setting slightly attenuates the drawbars, as on the original console.
    double percAtt = man == 0 && perc.on && !perc.soft ? 0.72 : 1;
    int n = man == 2 ? 2 : 9;
    for (int i = 0; i < 9; i++) {
        if (i >= n || (man == 0 && i == 8 && perc.on)) {
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
        if (perc.on && wasEmpty) percEnv = perc.soft ? 0.55 : 1.0;
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
    clickEnv = std::max(clickEnv, (man == 2 ? 0.6 : 1.0) * (0.35 + 0.83 * v) * keyed);
    orgMech.key(man, std::max(1, std::min(127, velocity)) / 127.0, true);
    forParts(man, k, amps, [&](int w, double a) { schedule(man, w, a, 0, spread, bounce); });
    if (man == 0 && perc.on) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
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
    double keyed = 0;
    for (double a : amps) keyed += a * a;
    keyed = std::min(1.2, std::sqrt(keyed / 3));
    clickEnv = std::max(clickEnv, 0.35 * (0.5 + v) * keyed);
    orgMech.key(man, velocity > 0 ? std::min(127, velocity) / 127.0 : 0.5, false);
    curRamp = (float) (0.5 + 1.2 * v);
    forParts(man, k, amps, [&](int w, double a) { schedule(man, w, -a, 0, 0.5 + 1.0 * (1 - v), 0.6 + 0.5 * v); });
    if (man == 0 && perc.on) {
        int w = wheelFor(k, perc.third ? 4 : 3);
        percG[w] = std::max(0.f, percG[w] - 1);
    }
}

// Scanner taps for the current vibrato depth: V1/V2/V3 scan a shorter or longer part of the same LC line, so
// the deeper settings also reach taps with more treble loss (about -0.5 dB per section above a few kHz).
inline void RotaryRoomDSP::configureScanner() {
    const double depth = vibDepthSec > 0 ? vibDepthSec : 0.0008;
    const double sectionsMax = 18 * depth / 0.0008; // line sections reached at the far end of the sweep
    for (int k = 0; k < SCAN_TAPS; k++) {
        double frac = k / (double) (SCAN_TAPS - 1);
        scanDelay[(size_t) k] = 20 + frac * 2 * depth * fs;
        double sections = frac * sectionsMax;
        scanK[(size_t) k] = kOf(16000 * std::pow(0.94, sections));
        scanGain[(size_t) k] = std::pow(10.0, -0.08 * sections / 20); // slight level loss along the line
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
            if (m == 0 && perc.on) percG[wheelFor(k, perc.third ? 4 : 3)] += 1;
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
    const double clickDecay = std::exp(-1 / (0.002 * fs));
    const double percLevel = (perc.soft ? 0.6 : 1.0) * percLevelUser;
    const double vibDepth = vibDepthSec * fs;
    const bool vibChorus = vibChorusMode;
    const double vibInc = 6.87 / fs;
    const double vol = volume, expBass = (1 - vol) * 0.9 * vol * 2, expGain = 0.08 + 0.92 * vol * vol;
    if ((int) orgBuf.size() < N)
        orgBuf.assign((size_t) N, 0.f); // pre-sized in init(); only grows for unusually large host blocks
    for (int n = 0; n < N; n++) {
        if (!events.empty()) {
            double now = sampleClock;
            for (size_t i = 0; i < events.size();) {
                if (events[i].t <= now) {
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
            sDry += (fastRand() * 2 - 1) * clickEnv * click * 0.35;
            clickEnv *= clickDecay;
        }
        sVib *= organGain;
        sDry *= organGain;

        double s = sDry;
        if (vibDepth > 0) {
            // ---- Scanner vibrato: delay line with 16 taps; a rotating capacitor sweeps tap 1 -> 16 -> 1 (triangle).
            //      Each stator plate couples to the rotor over ~1.3 plate widths (smooth but audibly stepped
            //      crossfade), stray capacitance lets every plate leak in a little, and each tap carries the treble
            //      loss of the LC sections in front of it. ----
            vibBuf[vibW] = (float) sVib;
            vibPh += vibInc;
            if (vibPh >= 1) vibPh -= 1;
            const double pos = (1 - std::fabs(2 * vibPh - 1)) * (SCAN_TAPS - 1);
            double wet = 0, wsum = 0;
            // only the 2-3 plates under the rotor need their own tap; a tap that comes into reach starts its filter
            // from the current input (its weight starts at zero, so this is seamless)
            const int k0 = std::max(0, (int) std::ceil(pos - 1.3)),
                      k1 = std::min(SCAN_TAPS - 1, (int) std::floor(pos + 1.3));
            for (int k = k0; k <= k1; k++) {
                double rp = vibW - scanDelay[(size_t) k];
                if (rp < 0) rp += 2048;
                int ri = (int) rp;
                double rf = rp - ri;
                double x = vibBuf[(size_t) ri] + (vibBuf[(size_t) ((ri + 1) & 2047)] - vibBuf[(size_t) ri]) * rf;
                if (!scanActive[(size_t) k]) {
                    scanLp[(size_t) k] = x;
                    scanActive[(size_t) k] = 1;
                }
                scanLp[(size_t) k] += scanK[(size_t) k] * (x - scanLp[(size_t) k]);
                double u = std::fabs(pos - k), c = u < 1.3 ? std::cos(0.5 * 3.14159265358979 * u / 1.3) : 0,
                       wgt = c * c;
                wet += wgt * scanLp[(size_t) k] * scanGain[(size_t) k];
                wsum += wgt;
            }
            for (int k = 0; k < SCAN_TAPS; k++)
                if (k < k0 || k > k1) scanActive[(size_t) k] = 0;
            // stray capacitance: every plate leaks in a little (about -40 dB each); summed, that is roughly the
            // middle of the line with its treble loss
            {
                double rp = vibW - scanDelay[SCAN_TAPS / 2];
                if (rp < 0) rp += 2048;
                int ri = (int) rp;
                double rf = rp - ri;
                double x = vibBuf[(size_t) ri] + (vibBuf[(size_t) ((ri + 1) & 2047)] - vibBuf[(size_t) ri]) * rf;
                scanStray += scanK[SCAN_TAPS / 2] * (x - scanStray);
                const double ws = 0.008 * SCAN_TAPS;
                wet += ws * scanStray * scanGain[SCAN_TAPS / 2];
                wsum += ws;
            }
            wet /= wsum;
            vibW = (vibW + 1) & 2047;
            s += vibChorus ? 0.6 * sVib + 0.6 * wet : wet;
        } else
            s += sVib;
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
