#pragma once
// Wheelhouse - fast transcendental approximations for the per-sample circuit solvers.
//
// The pentode plate characteristic evaluates atan() in every Newton iteration of every output tube at the
// oversampled rate. This polynomial version has ~2e-6 absolute error (far below the 1e-4 V solver tolerance,
// > 110 dB below the signal) and runs about twice as fast as libm. exp/log were tried as well, but glibc's
// FMA implementations are already faster than any accurate polynomial, so the tube models keep std::exp/log.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace rotary {
namespace fastmath {

/** atan with ~1e-6 max error: argument reduction to [0,1], odd polynomial. */
inline double atanFast(double x) {
    double ax = std::fabs(x);
    bool inv = ax > 1;
    double t = inv ? 1 / ax : ax, t2 = t * t;
    double r = t * (0.99997726 +
                    t2 * (-0.33262347 + t2 * (0.19354346 + t2 * (-0.11643287 + t2 * (0.05265332 + t2 * -0.01172120)))));
    if (inv) r = 1.5707963267948966 - r;
    return x < 0 ? -r : r;
}

} // namespace fastmath
} // namespace rotary
