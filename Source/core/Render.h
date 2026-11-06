// Wheelhouse - DSP core, part of RotaryRoomDSP (member definitions; included at the end of RotaryRoomDSP.h).
// Main render: sub-block loop, rotors, parallel path rendering, listener mix, FDN reverb, output.
#pragma once
#include "../RotaryRoomDSP.h"

namespace rotary {

/** Host blocks are split into sub-blocks of at most SUB samples so that the per-block geometry (rotor angles,
    path distances, directivity) is updated often enough; the result no longer depends on the host buffer size. */
inline void RotaryRoomDSP::process(float* const* out, int nCh, int N) {
    float* sub[16];
    const int nc = std::min(nCh, 16);
    for (int off = 0; off < N; off += SUB) {
        for (int c = 0; c < nc; c++) sub[c] = out[c] + off;
        processBlock(sub, nc, std::min(SUB, N - off));
    }
}

inline void RotaryRoomDSP::processBlock(float* const* out, int nCh, int N) {
    const double bt = smoothMotion(N);
    updatePathGeometry(bt);
    for (int c = 0; c < nCh; c++) std::fill(out[c], out[c] + N, 0.f);
    buildFastWorkingSet(N);
    prepareFdnModulation(N);
    // Listener <-> microphone mode: crossfade smoothly; whatever is fully faded out is not computed (the reverb runs
    // for both)
    const double modeT = micSetup.on ? 1 : 0;
    const bool micsOn = micSetup.on || modeMix > 1e-5, lisOn = !micSetup.on || modeMix < 1 - 1e-5;
    if (micsOn) micBlock(bt);
    staticBlock(lisOn, micsOn);
    MixParams mp;
    mp.N = N;
    mp.lisOn = lisOn;
    mp.micsOn = micsOn;
    mp.modeT = modeT;
    mp.chK = 1 - std::exp(-1 / (0.03 * fs));
    mp.modeK = 1 - std::exp(-1 / (0.04 * fs));
    mp.nOut = std::min(nCh, 16);
    mp.nSp = std::min(
        nCh, 8); // room/listener: at most 8 speakers (acc, panning tables); channels 9-16 are mic direct outs only
    mp.revGain = fdnInGain * room.reverb;
    mp.nSpk = (int) fdnOutRows.size();
    mp.lfeIdx = spk.lfe;
    mp.master = 6.5 * masterGain;

    prepareRunout(N);
    renderOrgan(N); // 1) organ -> orgBuf
    pre.process(orgBuf.data(), N);
    spring.process(orgBuf.data(), N);
    amp.process(orgBuf.data(), N); // 2) circuits
    renderRotors(N);               // 3a) crossover, cabinet, rotors

    // ---------- 3b) Room paths and microphones, in parallel: each job renders a slice of the paths for the whole
    // sub-block ----------
    blockN = N;
    blockLisOn = lisOn;
    blockMicsOn = micsOn;
    const int threads = pool.getThreads();
    listenerJobs = lisOn ? std::max(1, std::min(threads, (int) (exactIdx.size() + fastRT.size() / 4 + 15) / 16)) : 0;
    const int micJobs = micsOn ? 6 : 0;
    if ((int) partials.size() < listenerJobs)
        partials.resize((size_t) listenerJobs); // normally pre-sized by setThreads()
    pool.run(listenerJobs + micJobs);

    // ---------- 3c) Per sample (sequential): sum the partials, mic post-processing, FDN reverb, mixer, output
    // ----------
    for (int n = 0; n < N; n++) {
        double lfeIn = 0;
        mixListenerSample(n, mp, lfeIn);
        if (micsOn) micInputSample(n, mp);
        reverbSample(n, mp);
        outputSample(out, n, mp, lfeIn);
    }
    for (size_t i = 0; i < fastRT.size(); i++) {
        Path& p = paths[(size_t) fastIdx[i]];
        const FastPathRT& r = fastRT[i];
        p.zd = r.zd;
        p.lp = r.lp;
        p.zlo = r.zlo;
    }
    if (micsOn) {
        micMeterN += N;
        if (micMeterN >= 2048) {
            for (int c = 0; c < MIC_CH; c++) {
                micMeterVal[c] = (float) std::sqrt(micMeterAcc[c] / micMeterN) * (float) mp.master;
                micMeterAcc[c] = 0;
            }
            micMeterN = 0;
        }
    }
}

/** Per-block smoothing of positions and orientations, then rotor speed ramps (acceleration/deceleration, brake).
    Returns the duration of the sub-block in seconds. */
inline double RotaryRoomDSP::smoothMotion(int N) {
    const double PI = 3.14159265358979;
    double a = 1 - std::exp(-N / (0.12 * fs));
    auto& L = rotaryPos;
    auto& H = listener;
    L.x += (L.tx - L.x) * a;
    L.y += (L.ty - L.y) * a;
    H.x += (H.tx - H.x) * a;
    H.y += (H.ty - H.y) * a;
    double dy = H.tyaw - H.yaw;
    while (dy > PI) dy -= TWO_PI;
    while (dy < -PI) dy += TWO_PI;
    H.yaw += dy * a;
    double dcy = cabTyaw - cabYaw;
    while (dcy > PI) dcy -= TWO_PI;
    while (dcy < -PI) dcy += TWO_PI;
    cabYaw += dcy * a;

    // Per-block smoothing of positions/orientations, then rotor speed ramps (acceleration/deceleration, brake)
    double bt = N / fs;
    for (auto& r : rot) {
        double tau = r.tgt > r.hz ? r.upTau : r.dnTau;
        if (r.tgt == 0) tau *= brake ? 0.45 : 1.6;
        r.hz += (r.tgt - r.hz) * (1 - std::exp(-bt / tau));
        if (r.tgt == 0 && r.hz < 0.02) r.hz = 0;
    }
    return bt;
}

/** FDN delay modulation (0.1-0.5 Hz): evaluate the sine at the sub-block edges and ramp in between. */
inline void RotaryRoomDSP::prepareFdnModulation(int N) {
    for (int i = 0; i < 16; i++) {
        double ph0 = fdnModPh[i], ph1 = ph0 + fdnModInc[i] * N;
        if (ph1 >= 1) ph1 -= 1;
        double o0 = fdnLen[i] - 2 - fdnModDepth * (0.5 + 0.5 * std::sin(TWO_PI * ph0));
        double o1 = fdnLen[i] - 2 - fdnModDepth * (0.5 + 0.5 * std::sin(TWO_PI * ph1));
        fdnOff0[i] = o0;
        fdnOffD[i] = o1 - o0;
        fdnModPh[i] = ph1;
    }
}

/** 3a) Crossover and rotors (sequential): fill the rotor ring buffers and the per-sample angles.
    Every path only ever reads the past of the ring buffers, so the whole sub-block can be written first and the
    paths rendered afterwards, which is what allows splitting them across threads. */
inline void RotaryRoomDSP::renderRotors(int N) {
    Rotor& hornRotor = rot[0];
    Rotor& drumRotor = rot[1];
    const double wobbleAmt = rotorWobble;
    constexpr double COS1 = 0.5403023058681398, SIN1 = 0.8414709848078965;
    for (int n = 0; n < N; n++) {
        double s = orgBuf[(size_t) n];
        double lo = body.woofer(drumRes.run(lp2.run(lp1.run(s)))) * drumLevel;
        double hi = hornHi.run(hornRes2.run(hornRes.run(hp2.run(hp1.run(s))))) * hornLevel;
        // horn wind: turbulence at the horn mouth, it rotates with the horn
        if (motorLevel > 0)
            hi += windBp2.run(windBp1.run(fastRand() * 2 - 1)) * motorLevel * (hornRotor.hz / hornRotor.fast) * 0.25;
        // the cabinet itself: panel modes, motors, belts, relay (upper / lower compartment), and the organ console
        double su = 0, sl = 0;
        body.tick(hi, lo, su, sl);
        rotaryMech.tick(hornRotor, drumRotor, su, sl);
        const double kc = orgMech.tick();
        const int wi = wIdx;
        hornRing[wi] = (float) hi;
        drumRing[wi] = (float) lo;
        hornCabRing[wi] = (float) cabLp.run(hi);
        {
            float hb[4], dbb[3];
            splitH.run(hi, hb);
            splitD.run(lo, dbb);
            for (int b = 0; b < 4; b++) bandH[b][wi] = hb[b];
            for (int b = 0; b < 3; b++) bandD[b][wi] = dbb[b];
        }
        upperCabRing[wi] = (float) su;
        lowerCabRing[wi] = (float) sl;
        consoleRing[wi] = (float) kc;
        staticRevFeed[(size_t) n] = su + sl + kc * 0.7;
        // Rotor angles. The wobble term (eccentric belt: speed varies once per revolution) uses the sine from the
        // previous sample, which saves four trig calls per sample at no audible cost.
        hornRotor.ang += hornRotor.hz / fs * TWO_PI * (1 + wobbleAmt * hornRotor.lastSin);
        if (hornRotor.ang > TWO_PI) hornRotor.ang -= TWO_PI;
        drumRotor.ang +=
            drumRotor.hz / fs * TWO_PI * (1 + wobbleAmt * (drumRotor.lastSin * COS1 + drumRotor.lastCos * SIN1));
        if (drumRotor.ang > TWO_PI) drumRotor.ang -= TWO_PI; // sin(ang + 1)
        const double hornCos = std::cos(hornRotor.ang), hornSinRaw = std::sin(hornRotor.ang),
                     drumCos = std::cos(drumRotor.ang), drumSinRaw = std::sin(drumRotor.ang);
        const double hornSin = hornRotor.dir * hornSinRaw,
                     drumSin = drumRotor.dir * drumSinRaw; // direction of rotation: a clockwise rotor mirrors y
        hornRotor.lastSin = hornSinRaw;
        hornRotor.lastCos = hornCos;
        drumRotor.lastSin = drumSinRaw;
        drumRotor.lastCos = drumCos;
        writeIdxBuf[(size_t) n] = wi;
        hornCosBuf[(size_t) n] = hornCos;
        hornSinBuf[(size_t) n] = hornSin;
        drumCosBuf[(size_t) n] = drumCos;
        drumSinBuf[(size_t) n] = drumSin;
        hornSigBuf[(size_t) n] = hi;
        drumSigBuf[(size_t) n] = lo;
        wIdx = (wi + 1) & DM;
    }
}

// Per sub-block geometry of the static sources (cabinet body, console): delay and gain towards the listener's
// speakers and towards every microphone, ramped over the block like the room paths.
inline void RotaryRoomDSP::staticBlock(bool lisOn, bool micsOn) {
    const auto& H = listener;
    const auto& L = rotaryPos;
    const double fsc = fs / C_SOUND;
    // The console stands right in front of the listener: you sit at the organ, the Rotary is wherever it was put
    consoleX = H.x + 0.6 * std::cos(H.yaw);
    consoleY = H.y + 0.6 * std::sin(H.yaw);
    for (int k = 0; k < RDIR; k++) {
        const double a = TWO_PI * (k + 0.5) / RDIR;
        rdX[k] = std::cos(a);
        rdY[k] = std::sin(a);
        double fc;
        cabinet(rdX[k], rdY[k], 0.25, rdCab[k], fc);
    }
    {
        double m = 0;
        for (int k = 0; k < RDIR; k++) m += rdCab[k];
        m /= RDIR;
        for (int k = 0; k < RDIR; k++) rdCab[k] /= m;
    } // same reverb level as before
    float tmp[8];
    auto set = [](StaticPath& p, double ds, const float* g, double scale) {
        p.d0 = p.init ? p.d1 : ds;
        p.d1 = ds;
        for (int c = 0; c < 8; c++) {
            float v = (float) (g[c] * scale);
            p.g0[c] = p.init ? p.g1[c] : v;
            p.g1[c] = v;
        }
        p.init = true;
    };
    if (lisOn) {
        // cabinet: from its centre (0.6 m up) to the head; the panels radiate all round, the louvred front a bit more
        double vx = H.x - L.x, vy = H.y - L.y, vz = H.z - 0.6, hd = std::hypot(vx, vy), d = std::hypot(hd, vz);
        double cg, cfc;
        cabinet(vx, vy, vz, cg, cfc);
        panGains(std::atan2(-vy, -vx) - H.yaw, std::min(1.0, hd / std::max(d, 1e-6) * 1.15), tmp);
        set(stCab, d * fsc, tmp, (0.55 + 0.45 * cg) / std::max(d, 0.5));
        const double dk = std::hypot(0.6, H.z - 0.8);
        panGains(0, std::min(1.0, 0.6 / dk * 1.15), tmp);
        set(stKey, dk * fsc, tmp, 1 / dk);
    } else {
        stCab.init = stKey.init = false;
    }
    if (!micsOn) {
        for (auto& q : micSt) q.init = false;
        return;
    }
    const double src[3][3] = {{L.x, L.y, 0.85}, {L.x, L.y, 0.35}, {consoleX, consoleY, 0.8}};
    for (int i = 0; i < 6; i++) {
        MicStatic& q = micSt[i];
        const MicRt& m = mic[i];
        if (!micNeed(i)) {
            q.init = false;
            for (int j = 0; j < 3; j++) q.g0[j] = q.g1[j] = 0;
            continue;
        }
        for (int j = 0; j < 3; j++) {
            double wx = m.x - src[j][0], wy = m.y - src[j][1], wz = m.z - src[j][2];
            double d = std::sqrt(wx * wx + wy * wy + wz * wz);
            if (d < 1e-3) d = 1e-3;
            double deff = j < 2 ? std::max(0.12, d - 0.3) : std::max(0.3, d); // cabinet: distance to the panel surface
            double cth = -(wx * m.ax + wy * m.ay + wz * m.az) / d, pol = m.a[1] + (1 - m.a[1]) * cth;
            double g = pol / deff, ds = d * fsc;
            q.d0[j] = q.init ? q.d1[j] : ds;
            q.d1[j] = ds;
            q.g0[j] = q.init ? q.g1[j] : g;
            q.g1[j] = g;
        }
        q.init = true;
    }
}

inline void RotaryRoomDSP::renderListenerJob(int job) {
    const int N = blockN, nSp = 8;
    Partial& P = partials[(size_t) job];
    for (int n = 0; n < N; n++) {
        for (int c = 0; c < 8; c++) P.acc[n][c] = 0;
        for (int k = 0; k < 2 * NB; k++) P.bucket[n][k] = 0;
        P.lfe[n] = 0;
    }
    const double fsc = fs / C_SOUND;
    const double cabC = std::cos(cabYaw), cabS = std::sin(cabYaw);
    const auto& H = listener;
    const Rotor& hornRotor = rot[0];
    const Rotor& drumRotor = rot[1];
    // slice of the exact paths
    const size_t ne = exactIdx.size(), e0 = ne * (size_t) job / (size_t) listenerJobs,
                 e1 = ne * (size_t) (job + 1) / (size_t) listenerJobs;
    for (size_t qi = e0; qi < e1; qi++) {
        Path& p = paths[(size_t) exactIdx[qi]];
        const bool horn = p.rotor == 0;
        const double rr = horn ? hornRotor.r : drumRotor.r;
        for (int n = 0; n < N; n++) {
            const int wi = writeIdxBuf[(size_t) n];
            const double ca = horn ? hornCosBuf[(size_t) n] : drumCosBuf[(size_t) n],
                         sa = horn ? hornSinBuf[(size_t) n] : drumSinBuf[(size_t) n];
            const double tt = (n + 1.0) / N;
            double dxp = p.sx * ca, dyp = p.sy * sa;
            double vx = H.x - (p.icx + dxp * rr), vy = H.y - (p.icy + dyp * rr), vz = H.z - p.iz;
            double dist = std::sqrt(vx * vx + vy * vy + vz * vz) + p.jit;
            double cosE = (dxp * vx + dyp * vy) / dist;
            double rp = wi - dist * fsc;
            if (rp < 0) rp += DM + 1;
            int i1 = (int) rp;
            double fr = rp - i1;
            float gb[4];
            (horn ? *dirHorn : dirDrum).at(cosE, gb); // radiation of the rotor towards this path
            const float* const* bands = horn ? bandHp : bandDp;
            const int nb = horn ? 4 : 3;
            double v = 0;
            if (p.lin) {
                const int ia = i1 & DM, ib = (i1 + 1) & DM;
                for (int b = 0; b < nb; b++) {
                    const double y1 = bands[b][ia];
                    v += gb[b] * (y1 + (bands[b][ib] - y1) * fr);
                }
            } else
                v = sinc.readBands(bands, gb, nb, DM, i1, fr);
            if (horn && (p.order == 0 || p.order == 1)) {
                double backness = std::max(0.0, -(dxp * cabC + dyp * cabS));
                backness *= backness * (3 - 2 * backness); // no corner at the onset
                if (backness > 0) {
                    double rp2 = rp - cabDelay;
                    if (rp2 < 0) rp2 += DM + 1;
                    const int j = (int) rp2;
                    v += sinc.read(hornCabRing.data(), DM, j, rp2 - j) * backness * hornBoxLevel;
                }
            }
            const double k = horn ? p.kc : std::min(p.kc, p.kDrum);
            p.lp += k * (v - p.lp);
            v = p.lp;
            v *= p.g * (p.cab0 + (p.cab1 - p.cab0) * tt);
            v /= (dist > 0.3 ? dist : 0.3);
            if (!horn) P.lfe[n] += v;
            const double hf = p.hfPrev + (p.hf - p.hfPrev) * tt, vs = v * hf;
            for (int c = 0; c < nSp; c++) {
                double gg = p.gPrev[c] + (p.gNext[c] - p.gPrev[c]) * tt;
                if (gg != 0) P.acc[n][c] += vs * gg;
            }
            if (hf < 1) addBuckets(P.bucket[n], p.bx0 + (p.bx1 - p.bx0) * tt, 0.f, (float) (v - vs));
        }
    }
    // slice of the fast paths (float working set, see FastPathRT)
    const size_t nf = fastRT.size(), f0 = nf * (size_t) job / (size_t) listenerJobs,
                 f1 = nf * (size_t) (job + 1) / (size_t) listenerJobs;
    const float kLoBandF = (float) kLoBand;
    for (size_t qi = f0; qi < f1; qi++) {
        FastPathRT& r = fastRT[qi];
        const float* const* bands = r.horn ? bandHp : bandDp;
        const int nb = r.horn ? 4 : 3;
        for (int n = 0; n < N; n++) {
            r.ds += r.dds;
            r.g += r.dg;
            double rp = writeIdxBuf[(size_t) n] - r.ds;
            if (rp < 0) rp += DM + 1;
            int i1 = (int) rp;
            float fr = (float) (rp - i1);
            const int ia = i1 & DM, ib = (i1 + 1) & DM;
            float xin = 0;
            for (int b = 0; b < nb; b++) {
                r.bg[b] += r.dbg[b];
                const float y1 = bands[b][ia];
                xin += r.bg[b] * (y1 + (bands[b][ib] - y1) * fr);
            }
            if (r.trace) {
                r.zlo += kLoBandF * (xin - r.zlo);
                xin += (r.glr - 1.0f) * r.zlo;
            }
            r.lp += r.kb * (xin - r.lp);
            float v = r.lp * r.g;
            if (!r.horn) P.lfe[n] += v;
            float vh = v * r.hf;
            float* bk = P.bucket[n];
            if (r.bi >= 0) {
                r.bf += r.dbx;
                const float ve = v - vh;
                bk[r.bi] += vh * (1 - r.bf);
                bk[r.bj] += vh * r.bf;
                bk[NB + r.bi] += ve * (1 - r.bf);
                bk[NB + r.bj] += ve * r.bf;
            } else {
                r.bx += r.dbx;
                addBuckets(bk, r.bx, vh, v - vh);
            }
        }
    }
}

inline void RotaryRoomDSP::renderMicJob(int i) {
    const int N = blockN;
    MicRt& m = mic[i];
    double* out = micRaw[i];
    if (!micNeed(i)) {
        for (int n = 0; n < N; n++) out[n] = 0;
        return;
    }
    for (int n = 0; n < N; n++) out[n] = 0;
    const double fsc = fs / C_SOUND;
    const auto& L = rotaryPos;
    const Rotor& hornRotor = rot[0];
    const Rotor& drumRotor = rot[1];
    for (auto& p : m.paths) {
        const bool horn = p.rotor == 0;
        for (int n = 0; n < N; n++) {
            const int wi = writeIdxBuf[(size_t) n];
            const double tt = (n + 1.0) / N;
            double v, b0, b1, b2;
            if (p.exact) {
                const double ca = horn ? hornCosBuf[(size_t) n] : drumCosBuf[(size_t) n],
                             sa = horn ? hornSinBuf[(size_t) n] : drumSinBuf[(size_t) n],
                             rr = horn ? hornRotor.r : drumRotor.r, hh = horn ? hornRotor.h : drumRotor.h;
                double dxp = p.sx * ca, dyp = p.sy * sa;
                double wx = m.x - (p.sx * L.x + p.ox + dxp * rr), wy = m.y - (p.sy * L.y + p.oy + dyp * rr),
                       wz = m.z - (p.sz * hh + p.oz);
                double d = std::sqrt(wx * wx + wy * wy + wz * wz);
                if (d < 0.06) d = 0.06;
                double cosE = (dxp * wx + dyp * wy) / d;
                double rp = wi - d * fsc;
                if (rp < 0) rp += DM + 1;
                int i1 = (int) rp;
                double fr = rp - i1;
                float gb[4];
                (horn ? *dirHorn : dirDrum).at(cosE, gb);
                v = sinc.readBands(horn ? bandHp : bandDp, gb, horn ? 4 : 3, DM, i1, fr);
                if (horn) { // when the horn points away, sound reaches the mic via the horn compartment (reflection
                            // inside the cabinet)
                    double back = std::max(0.0, -cosE);
                    back *= back * (3 - 2 * back);
                    if (back > 0) {
                        double rp2 = rp - cabDelay;
                        if (rp2 < 0) rp2 += DM + 1;
                        const int j = (int) rp2;
                        v += sinc.read(hornCabRing.data(), DM, j, rp2 - j) * back * hornBoxLevel * 0.6;
                    }
                }
                const double k = horn ? p.kc : std::min(p.kc, p.kDrum);
                p.lp += k * (v - p.lp);
                v = p.lp * p.g * p.cab / d;
                double cth = -(wx * m.ax + wy * m.ay + wz * m.az) / d;
                b0 = m.a[0] + (1 - m.a[0]) * cth;
                b1 = m.a[1] + (1 - m.a[1]) * cth;
                b2 = m.a[2] + (1 - m.a[2]) * cth;
            } else {
                double d = p.d0 + (p.d1 - p.d0) * tt, g = p.g0 + (p.g1 - p.g0) * tt;
                double rp = wi - d * fsc;
                if (rp < 0) rp += DM + 1;
                int i1 = (int) rp;
                double fr = rp - i1;
                const float* const* bands = horn ? bandHp : bandDp;
                const int nb = horn ? 4 : 3, ia = i1 & DM, ib = (i1 + 1) & DM;
                v = 0;
                for (int b = 0; b < nb; b++) {
                    const double gg = p.bgA[b] + (p.bgB[b] - p.bgA[b]) * tt, y1 = bands[b][ia];
                    v += gg * (y1 + (bands[b][ib] - y1) * fr);
                }
                p.lp += p.kb * (v - p.lp);
                v = p.lp * g;
                b0 = p.band[0];
                b1 = p.band[1];
                b2 = p.band[2];
            }
            // Polar pattern per band (low < 400 Hz, mid, high > 3.5 kHz)
            p.zlo += kBandLo * (v - p.zlo);
            p.zhi += kBandHi * (v - p.zhi);
            out[n] += p.zlo * b0 + (p.zhi - p.zlo) * b1 + (v - p.zhi) * b2;
        }
    }
}

/** 3c, listener: sum the parallel partials, pan the sharp and wide (diffused) parts, add the static sources. */
inline void RotaryRoomDSP::mixListenerSample(int n, const MixParams& mp, double& lfeIn) {
    const int N = mp.N;
    const bool lisOn = mp.lisOn;
    const int nSp = mp.nSp;
    const double tt = (n + 1.0) / N;

    acc.fill(0);
    bucket.fill(0);
    for (int j = 0; j < listenerJobs; j++) {
        const Partial& P = partials[(size_t) j];
        for (int c = 0; c < nSp; c++) acc[c] += P.acc[n][c];
        for (int k = 0; k < 2 * NB; k++) bucket[k] += P.bucket[n][k];
        lfeIn += P.lfe[n];
    }
    if (lisOn) {
        for (int k = 0; k < NB; k++) {
            double bv = bucket[k];
            if (bv == 0) continue;
            const auto& bg = bucketG[k];
            for (int c = 0; c < nSp; c++) acc[c] += bv * bg[c];
        }
        // wide part: panned first (continuously, coherent), then decorrelated per speaker channel. One diffuser per
        // channel, so a reflection spread over two buckets never runs through two different diffusers into the same
        // channel (their phases would partly cancel it, mostly in the bass).
        double wch[8] = {};
        for (int k = 0; k < NB; k++) {
            const double bv = bucket[NB + k];
            if (bv == 0) continue;
            const auto& bg = bucketG[NB + k];
            for (int c = 0; c < nSp; c++) wch[c] += bv * bg[c];
        }
        for (int c = 0; c < nSp; c++) acc[c] += diffuse(c, (float) wch[c]);
        diffIdx = (diffIdx + 1) & (DIFF_LEN - 1);
    }
    // static sources at the listener: the cabinet body (panels, motors) and the organ console
    if (lisOn) {
        const int wi = writeIdxBuf[(size_t) n];
        const double sc = readStatic(upperCabRing, wi, stCab.d0 + (stCab.d1 - stCab.d0) * tt) +
                          readStatic(lowerCabRing, wi, stCab.d0 + (stCab.d1 - stCab.d0) * tt);
        const double sk = readStatic(consoleRing, wi, stKey.d0 + (stKey.d1 - stKey.d0) * tt);
        for (int c = 0; c < nSp; c++)
            acc[c] += sc * (stCab.g0[c] + (stCab.g1[c] - stCab.g0[c]) * tt) +
                      sk * (stKey.g0[c] + (stKey.g1[c] - stKey.g0[c]) * tt);
    }
}

/** 3c, reverb: feed from the rotating sources, pre-delay, input diffusion, 16 modulated lines with 3-band decay,
    Hadamard mixing; outputs to the speakers and the microphones. */
inline void RotaryRoomDSP::reverbSample(int n, const MixParams& mp) {
    const int N = mp.N;
    const bool micsOn = mp.micsOn;
    const double hornCos = hornCosBuf[(size_t) n];
    const double hornSin = hornSinBuf[(size_t) n];
    const double tt = (n + 1.0) / N;
    Rotor& hornRotor = rot[0];
    Rotor& drumRotor = rot[1];
    const double revGain = mp.revGain;
    const int nSpk = mp.nSpk;
    // reverb feed from the rotating sources (see rdX/rdY): far field at six directions, 0.5 m out
    double revIn;
    {
        const int wi = writeIdxBuf[(size_t) n];
        const double drumCos = drumCosBuf[(size_t) n], drumSin = drumSinBuf[(size_t) n];
        const double fscR = fs / C_SOUND;
        double sum = 0;
        for (int k = 0; k < RDIR; k++) {
            const double cH = hornCos * rdX[k] + hornSin * rdY[k], cD = drumCos * rdX[k] + drumSin * rdY[k];
            double rp = wi - (0.5 - hornRotor.r * cH) * fscR;
            if (rp < 0) rp += DM + 1;
            int i1 = (int) rp;
            double fr = rp - i1;
            float gb[4];
            double vh = 0;
            dirHorn->at(cH, gb);
            for (int b = 0; b < 4; b++) {
                const double y1 = bandH[b][(size_t) (i1 & DM)];
                vh += gb[b] * (y1 + (bandH[b][(size_t) ((i1 + 1) & DM)] - y1) * fr);
            }
            rp = wi - (0.5 - drumRotor.r * cD) * fscR;
            if (rp < 0) rp += DM + 1;
            i1 = (int) rp;
            fr = rp - i1;
            double vd = 0;
            dirDrum.at(cD, gb);
            for (int b = 0; b < 3; b++) {
                const double y1 = bandD[b][(size_t) (i1 & DM)];
                vd += gb[b] * (y1 + (bandD[b][(size_t) ((i1 + 1) & DM)] - y1) * fr);
            }
            const double x = (vh / 0.65 + vd / 0.85) * rdCab[k];
            rdSig[k] = x;
            sum += x;
        }
        sum /= RDIR;
        for (int k = 0; k < RDIR; k++) rdSig[k] -= sum; // directional part, fed straight into the lines below
        revIn = sum + staticRevFeed[(size_t) n];
    }
    revInZ += revInK * (revIn - revInZ);
    revHpZ += revHpK * (revInZ - revHpZ);
    preBuf[preIdx] = (float) (revInZ - revHpZ);
    int pi = preIdx + 1;
    if (pi >= preLen) pi = 0;
    preIdx = pi;
    double inj = preBuf[pi] * revGain;
    for (int i = 0; i < 6; i++) {
        auto& b = apBuf[i];
        int ix = apIdx[i];
        double dl = b[ix];
        double x = inj - 0.68 * dl;
        b[ix] = (float) x;
        inj = dl + 0.68 * x;
        int ni = ix + 1;
        if (ni >= apLen[i]) ni = 0;
        apIdx[i] = ni;
    }
    for (int i = 0; i < 16; i++) {
        double off = fdnOff0[i] + fdnOffD[i] * tt; // modulated read offset, ramped over the sub-block
        double rp = fdnIdx[i] - off;
        if (rp < 0) rp += 65536;
        int ri = (int) rp;
        double rf = rp - ri;
        const auto& b = fdnBuf[i];
        double y = b[ri & FM] + (b[(ri + 1) & FM] - b[ri & FM]) * rf;
        {
            auto& lb = lapBuf[i];
            int lx = lapIdx[i];
            double dl = lb[lx];
            double x = y - 0.55 * dl;
            lb[lx] = (float) x;
            y = dl + 0.55 * x;
            int ni = lx + 1;
            if (ni >= lapLen[i]) ni = 0;
            lapIdx[i] = ni;
        }
        fdnZlo[i] += kLoBand * (y - fdnZlo[i]);
        double mid = y - fdnZlo[i];
        fdnZhi[i] += kHiBand * (mid - fdnZhi[i]);
        rv[i] = fdnZlo[i] * fdnGl[i] + fdnZhi[i] * fdnGm[i] + (mid - fdnZhi[i]) * fdnGh[i];
    }
    for (int h = 1; h < 16; h <<= 1)
        for (int i = 0; i < 16; i += h << 1)
            for (int j = i; j < i + h; j++) {
                double x = rv[j], y = rv[j + h];
                rv[j] = x + y;
                rv[j + h] = x - y;
            }
    for (int c = 0; c < nSpk; c++) {
        const auto& row = fdnOutRows[c];
        const auto& offs = fdnOutOff[c];
        double o = 0;
        for (int i = 0; i < 16; i++) o += fdnBuf[i][(fdnIdx[i] - fdnLen[i] + 2 + offs[i]) & FM] * row[i];
        int ch = fdnOutChs[c];
        if (ch < 8) acc[ch] += o;
    }
    if (micsOn)
        for (int i = 0; i < 6; i++) {
            MicRt& m = mic[i];
            if (!micNeed(i)) continue;
            double o = 0;
            for (int j = 0; j < 16; j++) o += fdnBuf[j][(fdnIdx[j] - fdnLen[j] + 2 + m.fdnOff[j]) & FM] * m.fdnRow[j];
            // Mic sound: diffuse field (weighted by the polar pattern), proximity effect, frequency response, preamp
            // trim
            double x = (micSig[i] + o * m.diffuse * micRevScale);
            if (m.hasProx) x = m.prox.run(x);
            for (int k = 0; k < m.nEq; k++) x = m.eq[k].run(x);
            micSig[i] = x * m.trim;
        }
    for (int i = 0; i < 16; i++) {
        fdnBuf[i][fdnIdx[i]] =
            (float) (rv[i] * 0.25 + inj * (0.8 + 0.2 * ((i * 7) % 5) / 4.0) + rdSig[i % RDIR] * revGain * 0.5);
        fdnIdx[i] = (fdnIdx[i] + 1) & FM;
    }
}

/** 3c, output: LFE, listener/mic crossfade, mic group matrix with delays, direct outs, soft clipping. */
inline void RotaryRoomDSP::outputSample(float* const* out, int n, const MixParams& mp, double lfeIn) {
    const double PI = 3.14159265358979;
    const bool micsOn = mp.micsOn;
    const double chK = mp.chK, modeK = mp.modeK, modeT = mp.modeT, master = mp.master;
    const int nOut = mp.nOut;
    if (mp.lfeIdx >= 0 && mp.lfeIdx < 8) acc[mp.lfeIdx] = lfeLp2.run(lfeLp1.run(lfeIn)) * lfeLevel * 2;
    modeMix += modeK * (modeT - modeMix);
    if (std::fabs(modeT - modeMix) < 1e-6) modeMix = modeT;
    const double lisG = std::cos(modeMix * 0.5 * PI), micG = std::sin(modeMix * 0.5 * PI);
    if (micsOn) {
        micOut.fill(0);
        const int di = chDelayIdx;
        for (int g = 0; g < MIC_GRP; g++) {
            double* C = mixC[g];
            const double* T = mixT[g];
            for (int k = 0; k < 4; k++) C[k] += chK * (T[k] - C[k]);
            double xa = micNeed(2 * g) ? micSig[2 * g] : 0, xb = micNeed(2 * g + 1) ? micSig[2 * g + 1] : 0;
            auto& da = chDelay[2 * g];
            auto& db = chDelay[2 * g + 1];
            da[(size_t) di] = (float) xa;
            db[(size_t) di] = (float) xb;
            if (grpDelayS[g] > 0) {
                int ri = (di - grpDelayS[g]) & (CHD - 1);
                xa = da[(size_t) ri];
                xb = db[(size_t) ri];
            }
            double l = (xa * C[0] + xb * C[2]) * micG, r = (xa * C[1] + xb * C[3]) * micG;
            micMeterAcc[2 * g] += l * l;
            micMeterAcc[2 * g + 1] += r * r;
            const int ol = grpOutL[g], orr = grpOutR[g];
            if (ol >= 0 && ol < 16) micOut[(size_t) ol] += l;
            if (orr >= 0 && orr < 16) micOut[(size_t) orr] += r;
        }
        chDelayIdx = (di + 1) & (CHD - 1);
    }
    for (int c = 0; c < nOut; c++) {
        double x = ((c < 8 ? acc[c] * lisG * spkTrim[(size_t) c] : 0) + (micsOn ? micOut[(size_t) c] : 0)) * master;
        out[c][n] = (float) (x > 0.9    ? 0.9 + 0.1 * std::tanh((x - 0.9) * 10)
                             : x < -0.9 ? -0.9 + 0.1 * std::tanh((x + 0.9) * 10)
                                        : x);
    }
}

} // namespace rotary
