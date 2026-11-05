// Wheelhouse - radiation model of the rotating horn and bass rotor (tables in RotorDirectivity.h)
#pragma once
#include <cmath>
#include <algorithm>
#include "RotorDirectivity.h"

namespace rotary {

/** In-phase band split (Linkwitz-Riley 4th order with all-pass compensation): x -> nb bands whose sum is an
    all-pass of x. Run once per rotor; every path then only mixes the bands. */
struct LRBandSplit {
    struct BQ {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set(int type, double f, double fs) {   // 0 = low-pass, 1 = high-pass, 2 = all-pass; Q = 1/sqrt(2)
            const double w = 6.283185307179586 * f / fs, cw = std::cos(w), al = std::sin(w) / (2 * 0.7071067811865476), a0 = 1 + al;
            double B0, B1, B2;
            if (type == 0) { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; }
            else if (type == 1) { B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; }
            else { B0 = 1 - al; B1 = -2 * cw; B2 = 1 + al; }
            b0 = B0 / a0; b1 = B1 / a0; b2 = B2 / a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0; z1 = z2 = 0;
        }
        inline double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    static constexpr int MAXE = 3;
    int ne = 0;                                   // number of edges, bands = ne + 1
    BQ lp[MAXE][2], hp[MAXE][2], ap[MAXE][MAXE];  // ap[i][j]: band i aligned to edge j > i
    void prepare(const float* edges, int n, double fs) {
        ne = n;
        for (int i = 0; i < n; i++) { for (int k = 0; k < 2; k++) { lp[i][k].set(0, edges[i], fs); hp[i][k].set(1, edges[i], fs); } for (int j = 0; j < n; j++) ap[i][j].set(2, edges[j], fs); }
    }
    inline void run(double x, float* out) {
        double carry = x;
        for (int i = 0; i < ne; i++) {
            double l = lp[i][1].run(lp[i][0].run(carry));
            carry = hp[i][1].run(hp[i][0].run(carry));
            for (int j = i + 1; j < ne; j++) l = ap[i][j].run(l);
            out[i] = (float) l;
        }
        out[ne] = (float) carry;
    }
};

/** Band gains of a rotor towards a path, tabulated over cos(angle). */
struct RotorDir {
    static constexpr int NT = 256, MAXB = 4;
    float g[NT + 1][MAXB]; int nb = 4;
    template <int NB> void build(const float (&tab)[37][NB]) {
        nb = NB;
        for (int i = 0; i <= NT; i++) {
            const double c = -1 + 2.0 * i / NT, th = std::acos(std::max(-1.0, std::min(1.0, c))) * 180 / 3.141592653589793;
            const int j = std::min(35, (int) (th / 5)); const double w = (th - 5 * j) / 5;
            for (int b = 0; b < MAXB; b++) g[i][b] = b < NB ? (float) std::pow(10.0, (tab[j][b] * (1 - w) + tab[j + 1][b] * w) / 20) : 0.f;
        }
    }
    inline void at(double cosE, float* go) const {
        double x = (cosE + 1) * (NT / 2.0); if (x < 0) x = 0; else if (x > NT) x = NT;
        int i = (int) x; if (i >= NT) i = NT - 1; const float f = (float) (x - i);
        for (int b = 0; b < MAXB; b++) go[b] = g[i][b] + (g[i + 1][b] - g[i][b]) * f;
    }
};

} // namespace rotary
