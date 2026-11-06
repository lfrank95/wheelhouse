// Wheelhouse - DSP core, part of RotaryRoomDSP (member definitions; included at the end of RotaryRoomDSP.h).
// Room: positions, image sources, wall patches, cabinet directivity, per-block path geometry,
// speaker layout and panning.
#pragma once
#include "../RotaryRoomDSP.h"

namespace rotary {

inline void RotaryRoomDSP::setPos(bool hasLes, double lx, double ly, bool hasLis, double hx, double hy, bool hasYaw,
                                  double yaw, bool jump) {
    if (hasLes) {
        rotaryPos.tx = lx;
        rotaryPos.ty = ly;
    }
    if (hasLis) {
        listener.tx = hx;
        listener.ty = hy;
        if (hasYaw) listener.tyaw = yaw;
    }
    if (jump) {
        rotaryPos.x = rotaryPos.tx;
        rotaryPos.y = rotaryPos.ty;
        listener.x = listener.tx;
        listener.y = listener.ty;
        listener.yaw = listener.tyaw;
    }
}

inline void RotaryRoomDSP::setSpeakers(const Speakers& s) {
    spk = s;
    for (auto& a : spk.az) a *= 3.14159265358979 / 180;
    prepSpeakers();
    rebuildFdnOut();
    for (auto& p : paths) {
        p.gPrev.fill(0);
        p.gNext.fill(0);
        p.init = false;
    }
    if (mic[0].inited) micConfigure(); // the mixer's front/rear output targets depend on the output format
}

// Cabinet directivity: gain g and low-pass cutoff fc (Hz) for a path leaving the cabinet in direction (dx, dy, dz).
// The louvred front (facing cabYaw) is open; towards the back and vertically the sound is attenuated and dulled.
inline void RotaryRoomDSP::cabinet(double dx, double dy, double dz, double& g, double& fc) const {
    double c = std::cos(cabYaw), s = std::sin(cabYaw);
    double hd = std::hypot(dx, dy);
    if (hd == 0) hd = 1e-6;
    double fwd = (dx * c + dy * s) / hd;
    double vert = std::fabs(dz) / std::hypot(hd, dz);
    double b = std::max(0.0, -fwd);
    g = (fwd > 0 ? 1 : 1 - 0.72 * b) * (1 - 0.35 * vert * vert);
    fc = (fwd > 0 ? 16000 : 16000 - 14000 * b * b) * (1 - 0.7 * vert * vert);
}

/** Lambert weights of the wall patches for the current cabinet and listener position (4 x 4 sample points per
    patch): scattered energy  s R^2 * cos(in) cos(out) dA / (pi d1^2 d2^2)  for a unit source, and its centroid,
    which sets the patch's delay, departure direction and arrival azimuth. Per rotor only the strongest patches
    that together carry 97 % of the diffuse energy stay active (at most 24), the rest is skipped. */
inline void RotaryRoomDSP::updatePatches() {
    const auto& L = rotaryPos;
    const auto& H = listener;
    patchAt[0] = L.x;
    patchAt[1] = L.y;
    patchAt[2] = H.x;
    patchAt[3] = H.y;
    patchesDirty = false;
    struct WI {
        double w;
        size_t i;
    };
    std::vector<WI> ws[2];
    double tot[2] = {0, 0};
    for (size_t q = 0; q < paths.size(); q++) {
        Path& p = paths[q];
        if (!p.patch) continue;
        const double S[3] = {L.x, L.y, rot[p.rotor].h}, Hp[3] = {H.x, H.y, H.z};
        const int ua = (p.axis + 1) % 3, va = (p.axis + 2) % 3;
        const int K = 4;
        const double du = (p.u1 - p.u0) / K, dv = (p.v1 - p.v0) / K, dA = du * dv;
        double W = 0, cx[3] = {0, 0, 0};
        for (int a = 0; a < K; a++)
            for (int b = 0; b < K; b++) {
                double P[3];
                P[p.axis] = p.at;
                P[ua] = p.u0 + (a + 0.5) * du;
                P[va] = p.v0 + (b + 0.5) * dv;
                double s1 = 0, s2 = 0;
                for (int k = 0; k < 3; k++) {
                    s1 += (S[k] - P[k]) * (S[k] - P[k]);
                    s2 += (Hp[k] - P[k]) * (Hp[k] - P[k]);
                }
                s1 = std::max(s1, 0.04);
                s2 = std::max(s2, 0.04);
                const double ci = std::abs(S[p.axis] - P[p.axis]) / std::sqrt(s1),
                             co = std::abs(Hp[p.axis] - P[p.axis]) / std::sqrt(s2);
                const double w = ci * co * dA / (3.14159265358979 * s1 * s2);
                W += w;
                for (int k = 0; k < 3; k++) cx[k] += w * P[k];
            }
        if (W <= 0) {
            p.gm = 0;
            continue;
        }
        for (int k = 0; k < 3; k++) cx[k] /= W;
        double v1[3], v2[3];
        for (int k = 0; k < 3; k++) {
            v1[k] = cx[k] - S[k];
            v2[k] = cx[k] - Hp[k];
        }
        const double d1 = std::sqrt(v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2]),
                     d2 = std::sqrt(v2[0] * v2[0] + v2[1] * v2[1] + v2[2] * v2[2]);
        p.dist = d1 + d2;
        p.cx = cx[0];
        p.cy = cx[1];
        p.cz = cx[2];
        for (int k = 0; k < 3; k++) p.dep[k] = v1[k] / std::max(d1, 1e-6);
        p.azRoom = std::atan2(v2[1], v2[0]);
        p.el = v2[2] / std::max(d2, 1e-6);
        p.gm = roomScatterG * std::sqrt(W);
        ws[p.rotor].push_back({W, q});
        tot[p.rotor] += W;
    }
    for (int r = 0; r < 2; r++) {
        std::sort(ws[r].begin(), ws[r].end(), [](const WI& a, const WI& b) { return a.w > b.w; });
        double acc = 0;
        int n = 0;
        for (auto& e : ws[r]) {
            Path& p = paths[e.i];
            const bool on = n < PATCH_MAX_ACTIVE && acc < PATCH_ENERGY * tot[r];
            if (on != p.active) {
                p.active = on;
                p.init = false;
                p.lp = 0;
                p.zd = 0;
                p.cab1 = 0;
            }
            if (on) {
                acc += e.w;
                n++;
            }
        }
    }
    for (auto& p : paths)
        if (p.patch && p.gm == 0) p.active = false;
}

// Rebuilds the acoustic model after a room change: clamps positions, derives per-band absorption from the material,
// builds the image-source paths and wall patches for both rotors, computes RT60 (Eyring), the
// pre-delay and the FDN delay lengths/decay gains, then refreshes the output rows and microphone paths.
inline void RotaryRoomDSP::rebuildRoom() {
    auto clampd = [](double v, double lo, double hi) {
        return std::max(lo, std::min(hi, v));
    };
    auto& R = room;
    rotaryPos.tx = clampd(rotaryPos.tx, 0.3, R.Lx - 0.3);
    rotaryPos.ty = clampd(rotaryPos.ty, 0.3, R.Ly - 0.3);
    rotaryPos.x = clampd(rotaryPos.x, 0.3, R.Lx - 0.3);
    rotaryPos.y = clampd(rotaryPos.y, 0.3, R.Ly - 0.3);
    listener.tx = clampd(listener.tx, 0.3, R.Lx - 0.3);
    listener.ty = clampd(listener.ty, 0.3, R.Ly - 0.3);
    listener.x = clampd(listener.x, 0.3, R.Lx - 0.3);
    listener.y = clampd(listener.y, 0.3, R.Ly - 0.3);
    double mat[3] = {1.35, 1.0, 0.8};
    // scattering coefficient (ISO 17497): how much of a reflection the surface spreads instead of mirroring.
    // Real walls are not mirrors - furniture, doors, panelling, plaster texture scatter a good part of the mid/high
    // range. Values for furnished rooms, not bare test walls: wood panelling / drywall with furniture ~0.5, stone 0.35,
    // bare concrete 0.25, glass and tiles 0.15.
    double scatter = 0.5;
    if (R.material == "concrete") {
        mat[0] = 0.55;
        mat[2] = 1.1;
        scatter = 0.25;
    } else if (R.material == "stone") {
        mat[0] = 0.6;
        mat[2] = 1.35;
        scatter = 0.35;
    } else if (R.material == "glass") {
        mat[0] = 0.9;
        mat[2] = 1.6;
        scatter = 0.15;
    }
    roomScatter = scatter;
    double ab[3];
    for (int i = 0; i < 3; i++) ab[i] = clampd(R.absorb * mat[i], 0.03, 0.95);
    double reflG = std::sqrt(1 - ab[1]), hfRefl = std::sqrt((1 - ab[2]) / (1 - ab[1]));
    roomReflG = reflG;
    roomHfRefl = hfRefl;
    std::vector<Path> old = std::move(paths);
    std::vector<Path> np;
    for (int r = 0; r < 2; r++)
        for (int nx = -3; nx <= 3; nx++)
            for (int px = 0; px <= 1; px++) {
                int ox = std::abs(2 * nx - px);
                for (int ny = -3; ny <= 3; ny++)
                    for (int py = 0; py <= 1; py++) {
                        int oy = std::abs(2 * ny - py);
                        for (int nz = -3; nz <= 3; nz++)
                            for (int pz = 0; pz <= 1; pz++) {
                                int oz = std::abs(2 * nz - pz);
                                int order = ox + oy + oz;
                                if (order > R.order) continue;
                                double hf = std::pow(WALL_HF_KEEP, order) * std::pow(hfRefl, order);
                                Path p;
                                p.rotor = r;
                                p.order = order;
                                p.sx = 1 - 2 * px;
                                p.sy = 1 - 2 * py;
                                p.sz = 1 - 2 * pz;
                                p.ox = 2 * nx * R.Lx;
                                p.oy = 2 * ny * R.Ly;
                                p.oz = 2 * nz * R.Lz;
                                p.g = std::pow(reflG, order) * (order > 0 ? 0.9 : 1);
                                // each bounce dulls the treble
                                p.kOn = kOf(std::min(
                                    20000.0,
                                    (order == 0 ? 20000.0 : (r == 0 ? HORN_WALL_HF_HZ : DRUM_WALL_HF_HZ)) * hf));
                                // drum: the woofer and scoop do not reach above ~18 kHz, also on the direct path
                                // (same as the mic paths)
                                p.kDrum = kOf(std::min(20000.0, DRUM_WALL_HF_HZ * hf));
                                // exact mode: every image is rendered per sample (position, Doppler, directivity,
                                // panning); fast mode: order >= 2 uses the block-ramped float path with direction
                                // buckets
                                p.lin = order > 1 && !exactPaths;
                                p.fast = order > 1 && !exactPaths;
                                p.jit = order > 0 ? (((r * 977 + (int) np.size() * 131) % 97) / 97.0) * 0.9 * order : 0;
                                // first order: the diffuse share leaves the image and comes from the illuminated
                                // wall patches below
                                p.hfScat = std::pow(1 - scatter, order);
                                if (order == 1 && scatter > 0) {
                                    p.g *= std::sqrt(1 - scatter);
                                    p.hfScat = 1;
                                }
                                np.push_back(p);
                            }
                    }
            }
    // Diffuse first-order reflection from wall patches: every surface is cut into up to 4 x 4 patches (~2.5 m).
    // Each patch is lit by the rotor's beam (radiation model towards the patch, Doppler, cabinet) and re-radiates
    // its scattered share (Lambert) towards the listener. So the scattered sound wanders over the walls as the
    // horn and drum turn, instead of sitting at the mirror image. All of it goes into the wide (diffused) part.
    if (R.order >= 1 && scatter > 0) {
        const double Ls[3] = {R.Lx, R.Ly, R.Lz};
        for (int r = 0; r < 2; r++)
            for (int ax = 0; ax < 3; ax++)
                for (int side = 0; side < 2; side++) {
                    const int ua = (ax + 1) % 3, va = (ax + 2) % 3;
                    const int nu = std::max(1, std::min(PATCH_MAX_PER_SIDE, (int) std::ceil(Ls[ua] / PATCH_SIZE_M))),
                              nv = std::max(1, std::min(PATCH_MAX_PER_SIDE, (int) std::ceil(Ls[va] / PATCH_SIZE_M)));
                    for (int iu = 0; iu < nu; iu++)
                        for (int iv = 0; iv < nv; iv++) {
                            Path p;
                            p.rotor = r;
                            p.order = 1;
                            p.patch = true;
                            p.fast = true;
                            p.lin = true;
                            p.hfScat = 0;
                            p.axis = ax;
                            p.at = side ? Ls[ax] : 0;
                            p.u0 = Ls[ua] * iu / nu;
                            p.u1 = Ls[ua] * (iu + 1) / nu;
                            p.v0 = Ls[va] * iv / nv;
                            p.v1 = Ls[va] * (iv + 1) / nv;
                            const double hf = WALL_HF_KEEP * hfRefl;
                            p.kWall = kOf(std::min(20000.0, (r == 0 ? HORN_WALL_HF_HZ : DRUM_WALL_HF_HZ) * hf));
                            p.gm = 0;
                            p.active = false;
                            np.push_back(p);
                        }
                }
    }
    roomScatterG = std::sqrt(scatter) * reflG;
    patchesDirty = true;
    if (old.size() == np.size())
        for (size_t i = 0; i < np.size(); i++) {
            np[i].gPrev = old[i].gPrev;
            np[i].gNext = old[i].gNext;
            np[i].init = old[i].init;
            np[i].lp = old[i].lp;
        }
    paths = std::move(np);
    fastRT.reserve(paths.size());
    fastIdx.reserve(paths.size());
    exactIdx.reserve(paths.size());
    nRefl = 0;
    for (auto& p : paths)
        if (p.order > 0 && !p.patch) nRefl++;
    nRefl /= 2;

    double V = R.Lx * R.Ly * R.Lz;
    double S = 2 * (R.Lx * R.Ly + R.Lx * R.Lz + R.Ly * R.Lz);
    double rt[3];
    for (int i = 0; i < 3; i++)
        rt[i] = clampd(0.161 * V / (-S * std::log(1 - ab[i]) + (i == 2 ? 4 * 0.004 * V : 0)), 0.12, 12);
    rt[0] = std::min(rt[0], rt[1] * 1.25);
    rt[2] = std::min(rt[2], rt[1]);
    for (int i = 0; i < 3; i++) rt60[i] = rt[i];
    double A = -S * std::log(1 - ab[1]);
    double rc2 = A / (16 * 3.14159265358979);
    double mfp = 4 * V / S / C_SOUND;
    double tCover = clampd((R.order + 0.5) * mfp, 0.012, 0.25);
    preLen = std::max(32, std::min(8191, (int) std::lround(tCover * 0.55 * fs)));
    if (preIdx >= preLen) preIdx = 0;
    double base = clampd(mfp, 0.026, 0.08);
    auto isPrime = [](int n) {
        if (n < 2) return false;
        for (int d = 2; d * d <= n; d++)
            if (n % d == 0) return false;
        return true;
    };
    auto toPrime = [&](int n) {
        while (!isPrime(n)) n++;
        return n;
    };
    // round up to a prime, but never beyond the buffer: fall back to the largest prime <= cap
    auto primeCapped = [&](int n, int cap) {
        int q = toPrime(n);
        if (q > cap) {
            q = cap;
            while (!isPrime(q)) q--;
        }
        return q;
    };
    double lenSum = 0;
    for (int i = 0; i < 16; i++) {
        double frac = 0.6 + 1.6 * i / 15.0;
        int len = primeCapped((int) std::lround(base * frac * fs * (1 + 0.017 * ((i * 7) % 5))), 60000);
        fdnLen[i] = len;
        lenSum += len;
        auto gOf = [&](double t) {
            return std::pow(10.0, -3.0 * len / (fs * t));
        };
        fdnGl[i] = gOf(rt[0]);
        fdnGm[i] = gOf(rt[1]);
        fdnGh[i] = gOf(rt[2]);
        if (fdnIdx[i] >= len) fdnIdx[i] = 0;
    }
    double lenAvg = lenSum / 16;
    double gBar2 = std::pow(10.0, -6 * lenAvg / (fs * rt[1]));
    double frac = std::exp(-13.8 * tCover * 0.55 / rt[1]);
    double Etarget = 0.232 / rc2 * frac;
    fdnInGain = std::sqrt(Etarget * (1 - gBar2) / 16);
    fdnModDepth = 5 + 9 * clampd((rt[1] - 0.4) / 3, 0, 1);
    const double apMs[6] = {1.3, 2.1, 3.2, 4.7, 6.3, 8.1};
    double apScale = clampd(rt[1] / 0.8, 0.5, 4);
    for (int i = 0; i < 6; i++) {
        apLen[i] = primeCapped((int) std::lround(apMs[i] * apScale * fs / 1000), 4093);
        if (apIdx[i] >= apLen[i]) apIdx[i] = 0;
    }
    const double lapMs[16] = {2.1, 2.9, 3.7, 4.6, 5.3, 6.1, 7.0, 7.9, 8.7, 9.6, 10.4, 11.3, 12.1, 13.0, 13.8, 14.7};
    for (int i = 0; i < 16; i++) {
        lapLen[i] =
            primeCapped((int) std::lround(lapMs[(i * 5) % 16] * 0.6 * clampd(apScale, 0.6, 2) * fs / 1000), 2039);
        if (lapIdx[i] >= lapLen[i]) lapIdx[i] = 0;
    }
    rebuildFdnOut();
    micRebuildPaths();
    micFdnRows();
    roomInfo.rt60[0] = rt[0];
    roomInfo.rt60[1] = rt[1];
    roomInfo.rt60[2] = rt[2];
    roomInfo.rc = std::sqrt(rc2);
    roomInfo.paths = (int) paths.size();
}

// Per-speaker FDN output taps: a Hadamard-like +-0.25 row per speaker plus prime-number read offsets so that
// every output gets a decorrelated mix of the 16 lines.
inline void RotaryRoomDSP::rebuildFdnOut() {
    int n = (int) spk.az.size();
    fdnOutRows.assign(n, {});
    fdnOutOff.assign(n, {});
    fdnOutChs = spk.ch;
    static const int primes[] = {
        2,   3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,  59,  61,  67,  71,  73,  79,
        83,  89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193,
        197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317,
        331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457,
        461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599};
    const int NP = sizeof(primes) / sizeof(int);
    double sc = fs / 48000;
    // The diffuse field at the listener is the sum of all speakers: scale the per-speaker feed so the total stays the
    // same whatever the layout (reference: stereo, where the level was calibrated - direct = room at the critical
    // distance)
    const float rowG = 0.25f * (float) std::sqrt(2.0 / std::max(1, n));
    for (int c = 0; c < n; c++) {
        int r = c + 1;
        for (int i = 0; i < 16; i++) {
            int bits = r & i, par = 0;
            while (bits) {
                par ^= bits & 1;
                bits >>= 1;
            }
            fdnOutRows[c][i] = par ? -rowG : rowG;
            fdnOutOff[c][i] = (int) std::lround(primes[(c * 37 + i * 11 + 5) % NP] * sc);
        }
    }
}

// Sorts the speakers by azimuth and precomputes the panning gains for the NB azimuth buckets (+ omni bucket).
inline void RotaryRoomDSP::prepSpeakers() {
    int n = (int) spk.az.size();
    spkSorted.clear();
    for (int i = 0; i < n; i++) {
        double a = spk.az[i];
        while (a < 0) a += TWO_PI;
        while (a >= TWO_PI) a -= TWO_PI;
        spkSorted.push_back({a, spk.ch[i]});
    }
    std::sort(spkSorted.begin(), spkSorted.end(), [](const SpkS& p, const SpkS& q) { return p.a < q.a; });
    spkTrim.fill(1.f);
    for (int i = 0; i < n; i++) {
        double a = spk.az[i];
        while (a > 3.14159265358979) a -= TWO_PI;
        while (a < -3.14159265358979) a += TWO_PI;
        const bool rear = std::fabs(a) > 3.14159265358979 / 2 + 1e-6, center = std::fabs(a) < 1e-6 && !spk.stereo;
        if (spk.ch[i] >= 0 && spk.ch[i] < 8)
            spkTrim[(size_t) spk.ch[i]] = (float) std::pow(10.0, (rear ? trimRear : center ? trimCenter : 0) / 20);
    }
    centerCh = centerL = centerR = -1;
    if (!spk.stereo && n >= 3)
        for (int i = 0; i < n; i++)
            if (std::fabs(spkSorted[(size_t) i].a) < 1e-6) {
                centerCh = spkSorted[(size_t) i].ch;
                centerL = spkSorted[(size_t) ((i + 1) % n)].ch;
                centerR = spkSorted[(size_t) ((i - 1 + n) % n)].ch;
            }
    diffBuf.assign((size_t) DIFF_ST * NB * DIFF_LEN, 0.f);
    { // delays 0.5 .. 6 ms, a different prime per stage; the same set in every channel, so the wide part keeps the
        // left/right coherence of its panning (it is decorrelated from the direct sound, not between the speakers)
        static const int pr[] = {23,  29,  31,  37,  41,  43,  47,  53,  59,  61,  67,  71,  73,  79,
                                 83,  89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151,
                                 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229,
                                 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293};
        const double sc = fs / 48000, stMul[DIFF_ST] = {0.45, 0.8, 1.0, 0.65};
        for (int k = 0; k < NB; k++)
            for (int st = 0; st < DIFF_ST; st++)
                diffLen[(size_t) (DIFF_ST * k + st)] =
                    std::max(1, std::min(DIFF_LEN - 1, (int) std::lround(pr[(st * 17) % 54] * sc * stMul[st])));
    }
    bucketG.assign(2 * NB, {});
    for (int k = 0; k < NB; k++) {
        panGains(k * TWO_PI / NB, 1, bucketG[k].data());
        panGains(k * TWO_PI / NB, 0, bucketG[NB + k].data());
    }
}

// Panning gains for a source at azimuth az (radians, listener-relative). hfrac = horizontal fraction: that part is
// panned between the two nearest speakers (constant power). The rest (elevated sound: floor and ceiling reflections)
// has no height layer to go to, so it stays at its azimuth but spreads wide over the ring (cardioid-shaped weights,
// constant power) - a ceiling reflection of a Rotary in front still comes from the front, just less pin-pointed.
inline void RotaryRoomDSP::panGains(double az, double hfrac, float* out) const {
    for (int i = 0; i < 8; i++) out[i] = 0;
    const double efrac = 1 - hfrac;
    if (spk.stereo) {
        double p = std::max(-1.0, std::min(1.0, std::sin(az) / std::sin(3.14159265358979 / 6) * 0.5));
        double t = (1 - p) / 2 * 3.14159265358979 / 2;
        // elevated: narrower stereo spread (half the pan excursion)
        double te = (1 - 0.5 * p) / 2 * 3.14159265358979 / 2;
        out[spk.ch[0]] = (float) (hfrac * std::cos(t) + efrac * std::cos(te));
        out[spk.ch[1]] = (float) (hfrac * std::sin(t) + efrac * std::sin(te));
        return;
    }
    int n = (int) spkSorted.size();
    if (efrac > 0) {
        double w[8] = {0}, e = 0;
        for (int i = 0; i < n; i++) {
            double c = std::cos(az - spkSorted[i].a);
            w[i] = 0.5 + 0.5 * c;
            e += w[i] * w[i];
        }
        const double norm = efrac / std::sqrt(std::max(e, 1e-9));
        for (int i = 0; i < n; i++) out[spkSorted[i].ch] += (float) (w[i] * norm);
    }
    while (az < 0) az += TWO_PI;
    while (az >= TWO_PI) az -= TWO_PI;
    int i = 0;
    while (i < n && spkSorted[i].a <= az) i++;
    const SpkS& A = spkSorted[(i - 1 + n) % n];
    const SpkS& B = spkSorted[i % n];
    double span = B.a - A.a;
    while (span <= 0) span += TWO_PI;
    if (span >= 3.14159265358979) {
        double dA = az - A.a;
        while (dA < 0) dA += TWO_PI;
        double f = dA / span, w = std::sin(f * 3.14159265358979);
        out[A.ch] += (float) (hfrac * std::cos(f * 3.14159265358979 / 2) * (1 - 0.3 * w));
        out[B.ch] += (float) (hfrac * std::sin(f * 3.14159265358979 / 2) * (1 - 0.3 * w));
    } else {
        double d = az - A.a;
        while (d < 0) d += TWO_PI;
        double t = d / span * 3.14159265358979 / 2;
        out[A.ch] += (float) (hfrac * std::cos(t));
        out[B.ch] += (float) (hfrac * std::sin(t));
    }
    if (centerCh >= 0 && centerDiv > 0 &&
        out[centerCh] > 0) { // centre divergence: part of the centre feed goes to L/R as a phantom
        const double gc = out[centerCh], th = centerDiv * 3.14159265358979 / 2;
        out[centerCh] = (float) (gc * std::cos(th));
        out[centerL] += (float) (gc * std::sin(th) * 0.70710678);
        out[centerR] += (float) (gc * std::sin(th) * 0.70710678);
    }
}

/** Per-block geometry for every room path: distances, cabinet directivity, radiation, panning (the values at the
    start and end of the sub-block are interpolated per sample by the renderers). */
inline void RotaryRoomDSP::updatePathGeometry(double bt) {
    const double PI = 3.14159265358979;
    auto& L = rotaryPos;
    auto& H = listener;
    float tmp[8];
    double midAng[2] = {rot[0].ang + PI * rot[0].hz * bt,
                        rot[1].ang + PI * rot[1].hz * bt}; // internal angle (grows); y carries r.dir
    if (patchesDirty || std::abs(L.x - patchAt[0]) + std::abs(L.y - patchAt[1]) + std::abs(H.x - patchAt[2]) +
                                std::abs(H.y - patchAt[3]) >
                            0.02)
        updatePatches();
    for (auto& p : paths) {
        const Rotor& r = rot[p.rotor];
        if (p.patch && !p.active) continue;
        if (p.patch) {
            bool horn = p.rotor == 0;
            double a0 = r.ang, a1 = r.ang + TWO_PI * r.hz * bt;
            for (int e = 0; e < 2; e++) {
                double an = e ? a1 : a0, c = std::cos(an), sn = r.dir * std::sin(an);
                double cosE = p.dep[0] * c + p.dep[1] * sn;
                double d = p.dist - r.r * cosE;
                (horn ? *dirHorn : dirDrum).at(cosE, e ? p.bgB : p.bgA); // same radiation model as the box room
                if (e) {
                    p.d1 = d;
                    p.g1 = p.gm;
                    p.kb = std::min(p.kWall, airK(d));
                } else {
                    p.d0 = d;
                    p.g0 = p.gm;
                }
            }
            // cabinet directivity along the real departure direction (towards the patch, no mirroring)
            double cg, cfc;
            cabinet(p.dep[0], p.dep[1], p.dep[2], cg, cfc);
            p.cab0 = p.init ? p.cab1 : cg;
            p.cab1 = cg;
            p.kb = std::min(p.kb, kOf(cfc));
            setBx(p, bucketX(p.azRoom - H.yaw));
            p.hf = p.patch ? 0
                           : std::min(1.0, std::max(0.0, 1 - p.el * p.el) * SHARP_GAIN) *
                                 std::pow(1 - roomScatter, p.order);
            p.init = true;
            continue;
        }
        p.icx = p.sx * L.x + p.ox;
        p.icy = p.sy * L.y + p.oy;
        p.iz = p.sz * r.h + p.oz;
        p.gPrev = p.gNext;
        double ca = std::cos(midAng[p.rotor]), sa = r.dir * std::sin(midAng[p.rotor]);
        double vx = p.icx + p.sx * r.r * ca - H.x, vy = p.icy + p.sy * r.r * sa - H.y, vz = p.iz - H.z;
        double hd = std::hypot(vx, vy), dist = std::hypot(hd, vz);
        p.kAir = airK(dist);
        double cg, cfc;
        cabinet(-p.sx * vx, -p.sy * vy, -p.sz * vz, cg, cfc);
        p.cab0 = p.cab1 != 0 ? p.cab1 : cg;
        p.cab1 = cg;
        p.kc = std::min(std::min(kOf(cfc), p.kAir), p.kOn); // kOn: treble lost at the walls (per bounce)
        if (p.fast) {
            double a0 = r.ang, a1 = r.ang + TWO_PI * r.hz * bt;
            bool horn = p.rotor == 0;
            for (int e = 0; e < 2; e++) {
                double an = e ? a1 : a0, c = std::cos(an), sn = r.dir * std::sin(an);
                double dxp = p.sx * c, dyp = p.sy * sn;
                double wx = H.x - (p.icx + dxp * r.r), wy = H.y - (p.icy + dyp * r.r), wz = H.z - p.iz;
                double d = std::sqrt(wx * wx + wy * wy + wz * wz) + p.jit;
                double cosE = (dxp * wx + dyp * wy) / d;
                double g = p.g / (d > MIN_PATH_DIST ? d : MIN_PATH_DIST);
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
        }
        double az = std::atan2(vy, vx) - H.yaw;
        // sharp (pin-point) fraction: horizontal and still specular after p.order bounces; the rest is spread wide
        // around the same azimuth (floor/ceiling images, scattered reflections)
        // (cos^2 of the elevation: a bounce arriving steeply from below or above reaches the ears so differently from
        // the direct sound that it does not comb-filter it; mixed into the same channel it would, so it goes wide +
        // diffused)
        const double ce = hd / std::max(dist, 1e-6);
        double sharpFrac = std::min(1.0, ce * ce * SHARP_GAIN) * p.hfScat;
        setBx(p, bucketX(az));
        if (p.fast) {
            p.hf = sharpFrac;
            p.init = true;
        } else { // sharp part: pairwise gains; the wide part goes through the diffuser with the scattered sound (see
                 // 3c)
            p.hfPrev = p.init ? p.hf : sharpFrac;
            p.hf = sharpFrac;
            panGains(az, 1, tmp);
            for (int c = 0; c < 8; c++) p.gNext[c] = tmp[c];
            if (!p.init) {
                p.gPrev = p.gNext;
                p.init = true;
            }
        }
    }
}

/** Working set for the fast paths: linear ramps over this sub-block. The cabinet factor is folded into the
    gain ramp (start and end products interpolated linearly, which is indistinguishable from the exact product
    over <= 64 samples). */
inline void RotaryRoomDSP::buildFastWorkingSet(int N) {
    fastRT.clear();
    fastIdx.clear();
    exactIdx.clear();
    {
        const float invN = 1.0f / (float) N;
        const double fsc = fs / C_SOUND;
        for (size_t q = 0; q < paths.size(); q++) {
            const Path& p = paths[q];
            if (!p.fast) {
                exactIdx.push_back((int) q);
                continue;
            }
            if (p.patch && !p.active) continue;
            FastPathRT r;
            const double ds0 = p.d0 * fsc, ds1 = p.d1 * fsc;
            const float g0 = (float) (p.g0 * p.cab0), g1 = (float) (p.g1 * p.cab1);
            r.ds = ds0;
            r.dds = (ds1 - ds0) / N;
            r.g = g0;
            r.dg = (g1 - g0) * invN;
            r.kb = (float) p.kb;
            r.zd = (float) p.zd;
            r.lp = (float) p.lp;
            r.hf = (float) p.hf;
            for (int b = 0; b < 4; b++) {
                r.bg[b] = p.bgA[b];
                r.dbg[b] = (p.bgB[b] - p.bgA[b]) * invN;
            }
            r.bx = (float) p.bx0;
            r.dbx = (float) ((p.bx1 - p.bx0) / N);
            { // same cell at both ends of the sub-block: fixed pair, only the weight slides
                double x0 = p.bx0, x1 = p.bx1;
                const double sh = std::floor(std::min(x0, x1) / NB) * NB;
                x0 -= sh;
                x1 -= sh;
                const int c0 = (int) x0, c1 = (int) x1;
                if (c0 == c1) {
                    r.bi = c0 % NB;
                    r.bj = (r.bi + 1) % NB;
                    r.bf = (float) (x0 - c0);
                } else
                    r.bi = -1;
            }
            r.horn = p.rotor == 0;
            fastRT.push_back(r);
            fastIdx.push_back((int) q);
        }
    }
}

inline void RotaryRoomDSP::fillPathViz(PathViz& v) const {
    const auto& L = rotaryPos;
    const auto& H = listener;
    v.roomLx = (float) room.Lx;
    v.roomLy = (float) room.Ly;
    v.cabX = (float) L.x;
    v.cabY = (float) L.y;
    v.cabYaw = (float) cabYaw;
    v.lisX = (float) H.x;
    v.lisY = (float) H.y;
    v.lisYaw = (float) H.yaw;
    v.hornAngle = (float) hornAngle();
    v.drumAngle = (float) drumAngle();
    // broadband level of the radiation towards cosE: power mean of the band gains
    auto radiation = [&](int rotor, double cosE) {
        float g[4];
        const RotorDir& dm = rotor == 0 ? *dirHorn : dirDrum;
        dm.at(cosE, g);
        double sum = 0;
        for (int b = 0; b < dm.nb; b++) sum += (double) g[b] * g[b];
        return std::sqrt(sum / dm.nb);
    };
    int n = 0;
    for (const auto& p : paths) {
        if (n >= PathViz::MAX) break;
        const Rotor& r = rot[p.rotor];
        const double c = std::cos(r.ang), sn = r.dir * std::sin(r.ang);
        PathViz::Item it;
        it.rotor = (uint8_t) p.rotor;
        if (p.patch) {
            if (!p.active) continue;
            it.patch = 1;
            it.order = 1;
            it.x = (float) p.cx;
            it.y = (float) p.cy;
            it.level = (float) (p.gm * p.cab1 * radiation(p.rotor, p.dep[0] * c + p.dep[1] * sn));
        } else {
            if (p.order > 2) continue;
            // same geometry as the fast image paths in updatePathGeometry()
            const double dxp = p.sx * c, dyp = p.sy * sn;
            const double ix = p.icx + dxp * r.r, iy = p.icy + dyp * r.r;
            const double wx = H.x - ix, wy = H.y - iy, wz = H.z - p.iz;
            const double d = std::max(1e-6, std::sqrt(wx * wx + wy * wy + wz * wz));
            it.order = (uint8_t) p.order;
            it.x = (float) ix;
            it.y = (float) iy;
            it.level =
                (float) (p.g / std::max(d, MIN_PATH_DIST) * p.cab1 * radiation(p.rotor, (dxp * wx + dyp * wy) / d));
        }
        v.item[n++] = it;
    }
    v.n = n;
}

} // namespace rotary
