"""Hammond vibrato line (B-3/C-3, 18 coupled-coil sections) in the frequency domain.

Checks the fitted L and coupling k of Source/ScannerVibrato.h against the documented figures: about 1 ms over the
whole line, flat delay (the reason for the coupled coils), -3 dB near 5.8 kHz, 15 kOhm termination (R77).
    python3 tools/acoustics/scanner.py
"""
import numpy as np

N = 18


def analyse(L=0.45, k=0.15, Rs=8e3, Rt=15e3, Rcoil=400, C=5.6e-9, Cend=2.7e-9):
    f = np.logspace(1.5, 4.3, 600)
    Lm = np.diag([L] * N) + np.diag([k * L] * (N - 1), 1) + np.diag([k * L] * (N - 1), -1)
    H = np.zeros((len(f), N + 1), complex)
    for i, ff in enumerate(f):
        s = 2j * np.pi * ff
        n = 2 * N + 1
        A = np.zeros((n, n), complex)
        b = np.zeros(n, complex)
        A[0, 0] = 1 / Rs  # node 0: (vs - v0)/Rs = i1
        A[0, N + 1] = 1
        b[0] = 1 / Rs
        for q in range(1, N + 1):  # node q: i_q - i_{q+1} = sC v_q (+ v/Rt at the end)
            Cq = Cend if q == N else C
            A[q, q] = s * Cq + (1 / Rt if q == N else 0)
            A[q, N + q] = -1
            if q < N:
                A[q, N + q + 1] = 1
        for q in range(1, N + 1):  # coil q: v_{q-1} - v_q = Rcoil i_q + s sum_j Lm[q-1, j] i_{j+1}
            r = N + q
            A[r, q - 1] = 1
            A[r, q] = -1
            A[r, N + q] -= Rcoil
            for j in range(N):
                A[r, N + 1 + j] -= s * Lm[q - 1, j]
        H[i, :] = np.linalg.solve(A, b)[: N + 1]
    return f, H


def report(**kw):
    f, H = analyse(**kw)
    ph = np.unwrap(np.angle(H[:, N]))
    gd = -np.gradient(ph, 2 * np.pi * f)
    mag = 20 * np.log10(abs(H[:, N]) + 1e-12)
    i1k = np.argmin(abs(f - 1000))
    below = np.where(mag < mag[i1k] - 3)[0]
    fc = f[below[0]] if len(below) else float("nan")
    sel = (f > 100) & (f < 3000)
    m9 = 20 * np.log10(abs(H[sel, 9]))
    L, k = kw.get("L", 0.45), kw.get("k", 0.15)
    print(
        f"L={L*1e3:.0f} mH k={k:.2f}  Z0={np.sqrt(L/5.6e-9)/1e3:.1f} kOhm  delay "
        f"{gd[np.argmin(abs(f-200))]*1e6:.0f} us @200 Hz, {gd[np.argmin(abs(f-4000))]*1e6:.0f} us @4 kHz  "
        f"-3 dB {fc:.0f} Hz  tap 9 ripple {m9.max()-m9.min():.1f} dB"
    )


if __name__ == "__main__":
    for L in (0.4, 0.45, 0.5):
        for k in (0, 0.15, 0.3):
            report(L=L, k=k)
