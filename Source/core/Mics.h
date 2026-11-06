// Wheelhouse - DSP core, part of RotaryRoomDSP (member definitions; included at the end of RotaryRoomDSP.h).
// Microphone rig: setup, close-mic placement, paths from the rotors to the mics, per-sample mic input.
#pragma once
#include "../RotaryRoomDSP.h"

namespace rotary {

// Applies a MicSetup: mic types/patterns, close-mic placement around the cabinet, room pair geometry,
// proximity effect, wind sensitivity and the mixer matrix per group.
inline void RotaryRoomDSP::micConfigure() {
    const MicSetup& M = micSetup;
    kBandLo = kOf(400);
    kBandHi = kOf(3500);
    kWind = kOf(160);
    for (int c = 0; c < 6; c++)
        if ((int) chDelay[c].size() != CHD) chDelay[c].assign(CHD, 0.f);
    struct Def {
        bool want, close;
        int rotor, type, pattern;
    };
    const Def d[6] = {{true, true, 0, M.hornType, M.hornPattern},   {M.hornStereo, true, 0, M.hornType, M.hornPattern},
                      {true, true, 1, M.drumType, M.drumPattern},   {M.drumStereo, true, 1, M.drumType, M.drumPattern},
                      {true, false, -1, M.roomType, M.roomPattern}, {true, false, -1, M.roomType, M.roomPattern}};
    const double PI = 3.14159265358979, D2R = PI / 180;
    for (int i = 0; i < 6; i++) {
        MicRt& m = mic[i];
        const bool first = !m.inited, wasWant = m.want;
        m.want = d[i].want;
        m.active = true;
        m.close = d[i].close;
        m.rotor = d[i].rotor;
        m.chan = i;
        int t = std::max(0, std::min(NUM_MIC_TYPES - 1, d[i].type));
        m.type = t;
        const MicType& T = MIC_TYPES[t];
        for (int b = 0; b < 3; b++) m.a[b] = T.a[b];
        if (t == 4) {
            if (d[i].pattern == 1) {
                m.a[0] = 1;
                m.a[1] = 0.95;
                m.a[2] = 0.72;
            } else if (d[i].pattern == 2) {
                m.a[0] = m.a[1] = 0;
                m.a[2] = 0.05;
            }
        }
        double aAvg = (m.a[0] + m.a[1] + m.a[2]) / 3;
        m.diffuse = std::sqrt(aAvg * aAvg +
                              (1 - aAvg) * (1 - aAvg) / 3); // diffuse-field sensitivity (random-energy efficiency)
        auto setBq = [&](Biquad& q, const char* ty, double f, double qq, double db) {
            Biquad nb;
            nb.set(ty, std::min(f, fs * 0.45), qq, fs, db);
            if (first)
                q = nb;
            else {
                q.b0 = nb.b0;
                q.b1 = nb.b1;
                q.b2 = nb.b2;
                q.a1 = nb.a1;
                q.a2 = nb.a2;
            } // keep the filter state so there is no click
        };
        m.nEq = 0;
        for (int k = 0; k < T.nEq; k++) {
            const MicEq& e = T.eq[k];
            setBq(m.eq[m.nEq++],
                  e.type == 'h'   ? "hp"
                  : e.type == 'l' ? "lp"
                  : e.type == 's' ? "shelf"
                                  : "peak",
                  e.f, e.q, e.db);
        }
        if (m.close) {
            bool horn = m.rotor == 0, st = horn ? M.hornStereo : M.drumStereo;
            double center = (horn ? M.hornCenter : M.drumCenter) * D2R, ang = (horn ? M.hornAngle : M.drumAngle) * D2R;
            m.phiT = st ? center + (i % 2 == 0 ? -0.5 : 0.5) * ang
                        : center;               // L = left as seen by the engineer facing the cabinet
            const double Dh = 0.275, Wh = 0.37; // half depth / half width of a Rotary 122 cabinet (m)
            double cf = std::fabs(std::cos(m.phiT)), sf = std::fabs(std::sin(m.phiT));
            double edge = std::min(cf > 1e-6 ? Dh / cf : 1e9, sf > 1e-6 ? Wh / sf : 1e9);
            m.RT = edge + std::max(0.02, horn ? M.hornDist : M.drumDist);
            if (first) {
                m.phi = m.phiT;
                m.R = m.RT;
            } else if (i % 2 == 1 && m.want &&
                       !wasWant) { // the second (B) mic starts from the centre position with fresh filter state
                m.phi = center;
                m.R = mic[i - 1].R;
                for (auto& q : m.eq) q.reset();
                m.prox.reset();
                for (auto& p : m.paths) {
                    p.lp = p.zlo = p.zhi = p.zd = 0;
                }
            }
            double r = rot[m.rotor].r;
            m.dMean = std::sqrt(m.RT * m.RT + r * r);
            m.trim = m.dMean / 2.5; // preamp trim: 0 dB on the mixer ~ listener at 2.5 m
            m.windAmt = horn ? T.wind * (M.windscreen ? 0.2 : 1) * std::pow(0.2 / std::max(0.05, m.RT - 0.2), 2) : 0;
        } else {
            m.trim = 1;
            m.windAmt = 0;
            m.dMean = 2;
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
        double yaw = M.roomYaw, px = -std::sin(yaw),
               py = std::cos(yaw); // unit vector pointing left of the viewing direction
        double sp = M.roomArr == 0 ? std::max(0.05, M.roomSpacing) : M.roomArr == 2 ? 0.17 : 0.02;
        double aimOff = M.roomArr == 0 ? 0 : M.roomArr == 2 ? 55 * D2R : 45 * D2R;
        for (int j = 0; j < 2; j++) {
            MicRt& m = mic[4 + j];
            double sgn = j == 0 ? 1 : -1; // 4 = left, 5 = right
            m.x = M.roomX + px * sp * 0.5 * sgn;
            m.y = M.roomY + py * sp * 0.5 * sgn;
            m.z = M.roomZ;
            double a = yaw + sgn * aimOff;
            m.ax = std::cos(a);
            m.ay = std::sin(a);
            m.az = 0;
        }
    }
    // Mixer: routing matrix per group (A->L, A->R, B->L, B->R)
    bool anySolo = false;
    for (int g = 0; g < MIC_GRP; g++)
        if (M.grp[g].solo) anySolo = true;
    const size_t nsp = spk.ch.size();
    const int fl = nsp > 0 ? spk.ch[0] : 0, fr = nsp > 1 ? spk.ch[1] : 1;
    const int rl = nsp >= 5   ? spk.ch[3]
                   : nsp >= 4 ? spk.ch[2]
                              : fl,
              rr = nsp >= 5   ? spk.ch[4]
                   : nsp >= 4 ? spk.ch[3]
                              : fr;
    for (int g = 0; g < MIC_GRP; g++) {
        const auto& G = M.grp[g];
        const bool stereo = g == 2 || (g == 0 ? M.hornStereo : M.drumStereo);
        double gain = (!G.mute && (!anySolo || G.solo)) ? std::pow(10.0, G.gainDb / 20) * (G.pol ? -1 : 1) : 0;
        double* T = mixT[g];
        if (stereo) {
            double w = std::max(0.0, std::min(1.0, G.width)), bal = std::max(-1.0, std::min(1.0, G.pan));
            double gl = bal > 0 ? 1 - bal : 1, gr = bal < 0 ? 1 + bal : 1;
            T[0] = gain * gl * (0.5 + 0.5 * w);
            T[1] = gain * gr * (0.5 - 0.5 * w);
            T[2] = gain * gl * (0.5 - 0.5 * w);
            T[3] = gain * gr * (0.5 + 0.5 * w);
        } else {
            double th = (std::max(-1.0, std::min(1.0, G.pan)) + 1) * 0.25 * PI;
            T[0] = gain * std::cos(th);
            T[1] = gain * std::sin(th);
            T[2] = T[3] = 0;
        }
        if (G.out >= 0) {
            grpOutL[g] = std::min(15, G.out);
            grpOutR[g] = stereo ? std::min(15, G.out + 1) : grpOutL[g];
            if (!stereo) {
                T[0] = gain;
                T[1] = 0;
            } // mono direct out: no panning
        } else {
            grpOutL[g] = G.out == -2 ? rl : fl;
            grpOutR[g] = G.out == -2 ? rr : fr;
        }
        grpDelayS[g] = std::max(0, std::min(CHD - 2, (int) std::lround(G.delayMs * 1e-3 * fs)));
    }
    micPlaceClose(micWasConfigured ? 0 : 1);
    micWasConfigured = true;
    micRebuildPaths();
    micFdnRows();
}

// Places the four close mics around the cabinet; a = smoothing factor (1 = jump to target).
inline void RotaryRoomDSP::micPlaceClose(double a) {
    for (int i = 0; i < 4; i++) {
        MicRt& m = mic[i];
        m.phi += (m.phiT - m.phi) * a;
        m.R += (m.RT - m.R) * a; // mono <-> stereo: mics glide to the new position instead of jumping
        double th = cabYaw + m.phi, c = std::cos(th), s = std::sin(th);
        m.x = rotaryPos.x + m.R * c;
        m.y = rotaryPos.y + m.R * s;
        m.z = m.rotor == 0 ? rot[0].h + micSetup.hornHeight : rot[1].h;
        m.ax = -c;
        m.ay = -s;
        m.az = 0;
        m.windC = c;
        m.windS = s;
    }
}

// Builds the acoustic paths from the rotors to each microphone.
inline void RotaryRoomDSP::micRebuildPaths() {
    const Room& R = room;
    for (int i = 0; i < 6; i++) {
        MicRt& m = mic[i];
        std::vector<MicPath> oldPaths = std::move(m.paths);
        m.paths.clear();
        if (m.close) {
            // Close mic: its own rotor directly and via the floor reflection (computed exactly per sample); the other
            // rotor as crosstalk
            for (int r = 0; r < 2; r++) {
                MicPath p;
                p.rotor = r;
                p.exact = r == m.rotor;
                p.kDrum = kOf(18000);
                m.paths.push_back(p);
                if (r == m.rotor) {
                    MicPath f = p;
                    f.sz = -1;
                    f.order = 1;
                    f.g = 0.75 * roomReflG;
                    f.kDrum = kOf(9000);
                    m.paths.push_back(f);
                }
            }
            if (oldPaths.size() == m.paths.size())
                for (size_t q = 0; q < m.paths.size(); q++) {
                    auto& a = m.paths[q];
                    const auto& b = oldPaths[q];
                    a.lp = b.lp;
                    a.zlo = b.zlo;
                    a.zhi = b.zhi;
                    a.zd = b.zd;
                }
            continue;
        }
        int maxO = std::max(0, std::min(R.shape == "tent" ? 1 : R.order, micSetup.roomOrder));
        for (int r = 0; r < 2; r++)
            for (int nx = -3; nx <= 3; nx++)
                for (int px = 0; px <= 1; px++) {
                    int ox = std::abs(2 * nx - px);
                    for (int ny = -3; ny <= 3; ny++)
                        for (int py = 0; py <= 1; py++) {
                            int oy = std::abs(2 * ny - py);
                            for (int nz = -3; nz <= 3; nz++)
                                for (int pz = 0; pz <= 1; pz++) {
                                    int oz = std::abs(2 * nz - pz), order = ox + oy + oz;
                                    if (order > maxO) continue;
                                    double hf = std::pow(WALL_HF_KEEP, order) * std::pow(roomHfRefl, order);
                                    MicPath p;
                                    p.rotor = r;
                                    p.order = order;
                                    p.sx = 1 - 2 * px;
                                    p.sy = 1 - 2 * py;
                                    p.sz = 1 - 2 * pz;
                                    p.ox = 2 * nx * R.Lx;
                                    p.oy = 2 * ny * R.Ly;
                                    p.oz = 2 * nz * R.Lz;
                                    p.g = std::pow(roomReflG, order) * (order > 0 ? 0.9 : 1);
                                    p.kDrum = kOf(std::min(20000.0, DRUM_WALL_HF_HZ * hf));
                                    p.kRefl = p.kDrum;
                                    p.jit = order > 0 ? (((r * 577 + (int) m.paths.size() * 97 + i * 31) % 89) / 89.0) *
                                                            0.9 * order
                                                      : 0;
                                    m.paths.push_back(p);
                                }
                        }
                }
        if (oldPaths.size() ==
            m.paths.size()) // same layout: carry the filter states over so a reconfigure does not click
            for (size_t q = 0; q < m.paths.size(); q++) {
                auto& a = m.paths[q];
                const auto& b = oldPaths[q];
                a.lp = b.lp;
                a.zlo = b.zlo;
                a.zhi = b.zhi;
                a.zd = b.zd;
                a.init = b.init;
                a.d0 = b.d0;
                a.d1 = b.d1;
                a.g0 = b.g0;
                a.g1 = b.g1;
                a.kb = b.kb;
            }
    }
}

// FDN output rows/offsets for the six microphones (same scheme as rebuildFdnOut(), different seeds).
inline void RotaryRoomDSP::micFdnRows() {
    static const int primes[] = {7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,  59,  61,  67,
                                 71,  73,  79,  83,  89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149,
                                 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233,
                                 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331};
    const int NP = sizeof(primes) / sizeof(int);
    double sc = fs / 48000;
    for (int i = 0; i < 6; i++) {
        int r = 8 + i;
        for (int j = 0; j < 16; j++) {
            int bits = r & j, par = 0;
            while (bits) {
                par ^= bits & 1;
                bits >>= 1;
            }
            mic[i].fdnRow[j] = par ? -0.25f : 0.25f;
            mic[i].fdnOff[j] = (int) std::lround(primes[(i * 23 + j * 13 + 3) % NP] * sc);
        }
    }
}

/** Per-block update: close-mic positions (they follow the Rotary), delays / gains / directivity of the room paths */
inline void RotaryRoomDSP::micBlock(double bt) {
    micPlaceClose(1 - std::exp(-bt / 0.12));
    for (int i = 0; i < 6; i++) {
        MicRt& m = mic[i];
        if (!micNeed(i)) continue;
        for (auto& p : m.paths) {
            const Rotor& r = rot[p.rotor];
            double icx = p.sx * rotaryPos.x + p.ox, icy = p.sy * rotaryPos.y + p.oy, iz = p.sz * r.h + p.oz;
            double vx = m.x - icx, vy = m.y - icy, vz = m.z - iz, dist = std::sqrt(vx * vx + vy * vy + vz * vz);
            double cg, cfc;
            cabinet(p.sx * vx, p.sy * vy, p.sz * vz, cg, cfc); // vx points image -> mic, i.e. the radiation direction
            p.cab = cg;
            p.kc = std::min(std::min(kOf(cfc), airK(dist)), p.kRefl);
            double cth = -(vx * m.ax + vy * m.ay + vz * m.az) / std::max(dist, 1e-6);
            for (int b = 0; b < 3; b++) p.band[b] = m.a[b] + (1 - m.a[b]) * cth;
            if (p.exact) continue;
            bool horn = p.rotor == 0;
            double a0 = r.ang, a1 = r.ang + TWO_PI * r.hz * bt;
            for (int e = 0; e < 2; e++) {
                double an = e ? a1 : a0, c = std::cos(an), sn = r.dir * std::sin(an);
                double dxp = p.sx * c, dyp = p.sy * sn;
                double wx = m.x - (icx + dxp * r.r), wy = m.y - (icy + dyp * r.r), wz = m.z - iz;
                double d = std::sqrt(wx * wx + wy * wy + wz * wz) + p.jit;
                double cosE = (dxp * wx + dyp * wy) / d;
                double g = p.g * p.cab / std::max(d, 0.1);
                (horn ? *dirHorn : dirDrum).at(cosE, e ? p.bgB : p.bgA);
                if (e) {
                    p.d1 = d;
                    p.g1 = g;
                    p.kb = horn ? p.kc : std::min(p.kDrum, p.kc);
                } else {
                    p.d0 = d;
                    p.g0 = g;
                }
            }
            if (!p.init) {
                p.init = true;
            }
        }
    }
}

/** 3c, microphones: rendered paths plus static sources and wind noise (the noise generator stays on this thread). */
inline void RotaryRoomDSP::micInputSample(int n, const MixParams& mp) {
    const int N = mp.N;
    const bool micsOn = mp.micsOn;
    const double hornCos = hornCosBuf[(size_t) n];
    const double hornSin = hornSinBuf[(size_t) n];
    const double tt = (n + 1.0) / N;
    Rotor& hornRotor = rot[0];
    if (micsOn) {
        for (int i = 0; i < 6; i++) {
            MicRt& m = mic[i];
            if (!micNeed(i)) {
                micSig[i] = 0;
                continue;
            }
            double sum = micRaw[i][n];
            { // static sources: upper / lower cabinet, console
                const MicStatic& q = micSt[i];
                const int wi = writeIdxBuf[(size_t) n];
                const std::vector<float>* B[3] = {&upperCabRing, &lowerCabRing, &consoleRing};
                for (int j = 0; j < 3; j++) {
                    const double g = q.g0[j] + (q.g1[j] - q.g0[j]) * tt;
                    if (g != 0) sum += readStatic(*B[j], wi, q.d0[j] + (q.d1[j] - q.d0[j]) * tt) * g;
                }
            }
            // Wind: the horn blows past the mic (proportional to speed squared, only while the horn mouth faces the
            // mic)
            if (m.windAmt > 0 && hornRotor.hz > 0.5) {
                double c = hornCos * m.windC + hornSin * m.windS;
                if (c > 0.3) {
                    double e = (c - 0.3) / 0.7;
                    e *= e;
                    e *= e;
                    double nz = fastRand() * 2 - 1;
                    m.windLp += kWind * (nz - m.windLp);
                    m.windLp2 += kWind * (m.windLp - m.windLp2);
                    double sp = hornRotor.hz / 6.8;
                    sum += m.windLp2 * e * sp * sp * m.windAmt * 0.9;
                }
            }
            micSig[i] = sum;
        }
    }
}

} // namespace rotary
