import numpy as np, sys, time
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from axibem import Mesh, solve, far_field, RHO, C
from scipy.special import spherical_jn, spherical_yn
a = 0.1
def h1(n, x): return spherical_jn(n, x) + 1j * spherical_yn(n, x)
def h1d(n, x): return spherical_jn(n, x, True) + 1j * spherical_yn(n, x, True)
for f in (500, 1715, 3430, 8000):
    k = 2*np.pi*f/C; w = 2*np.pi*f
    h = min(0.004, C/f/10)
    th = np.linspace(np.pi, 0, 200)
    m = Mesh(); m.add_polyline(np.c_[a*np.sin(th), a*np.cos(th)], h); m.finish()
    chief = [(0.02, 0.03), (0.05, -0.02), (0.03, 0.0), (0.06, 0.04)]
    for mode in (0, 1):
        ct = m.mid[:, 1] / a
        m.vn = np.ones(len(m.len), complex) if mode == 0 else ct.astype(complex)
        t0 = time.time(); p, q, kk = solve(m, f, chief=chief); dt = time.time() - t0
        if mode == 0: pex = 1j*w*RHO*a/(1j*k*a - 1) * np.ones_like(ct)
        else: B = 1j*w*RHO/(k*h1d(1, k*a)); pex = B*h1(1, k*a)*ct
        err = np.linalg.norm(p - pex)/np.linalg.norm(pex)
        thf = np.radians([0, 45, 90, 135, 180]); F = far_field(m, p, q, kk, thf)
        if mode == 0: Fex = 4*np.pi*(1j*w*RHO*a/(1j*k*a-1))*a*np.exp(-1j*k*a)*np.ones(5)
        else: Fex = -4*np.pi*B*np.cos(thf)/k
        ef = np.abs(F - Fex).max()/np.abs(Fex).max()
        print(f"f {f:5d} N {len(m.len):4d} mode {mode}: surface err {err:.2e}  far err {ef:.2e}  {dt:.1f}s")
