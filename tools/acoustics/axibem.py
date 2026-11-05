"""Axisymmetric exterior Helmholtz BEM (Neumann problem), constant elements on straight meridian segments.

Convention: time factor e^{-i w t}, G = e^{ikR}/(4 pi R), normals point out of the body into the fluid.
Boundary equation at a smooth collocation point x (element midpoint):
    1/2 p(x) = int_S [ p(y) dG/dn_y - G q(y) ] dS_y,   q = dp/dn = i w rho v_n
Interior CHIEF points (inside the bodies) add rows 0 = int_S [ p dG/dn - G q ] dS (overdetermined, least squares).

The azimuthal integral is split: static parts (1/R and d/dn 1/R) in closed form with complete elliptic integrals,
the dynamic remainders ((e^{ikR}-1)/R and its normal derivative, both bounded) with the trapezoid rule in phi.
The remaining meridian integral uses Gauss-Legendre, split at the collocation point with a quadratic grading on
the element itself (log singularity of the ring kernel) and graded towards the near end for neighbours.
"""
import numpy as np
from scipy.special import ellipk, ellipe

RHO, C = 1.204, 343.0


class Mesh:
    """Closed meridian polylines (rho, z). Each polyline is traversed so that the fluid lies to the LEFT of the
    direction of travel in the (rho, z) half-plane drawn with rho to the right and z up... (normals given explicitly)."""

    def __init__(self):
        self.a, self.b, self.vn = [], [], []   # segment endpoints, normal velocity per element

    def add_polyline(self, pts, h, vn=0.0, flip=False):
        pts = np.asarray(pts, float)
        keep = np.r_[True, np.hypot(*np.diff(pts, axis=0).T) > 1e-12]; pts = pts[keep]   # drop repeated points
        for p0, p1 in zip(pts[:-1], pts[1:]):
            L = np.hypot(*(p1 - p0)); n = max(1, int(np.ceil(L / h)))
            for i in range(n):
                q0 = p0 + (p1 - p0) * i / n; q1 = p0 + (p1 - p0) * (i + 1) / n
                if flip: q0, q1 = q1, q0
                self.a.append(q0); self.b.append(q1); self.vn.append(vn)

    def finish(self):
        self.a = np.array(self.a); self.b = np.array(self.b); self.vn = np.array(self.vn, complex)
        d = self.b - self.a; self.len = np.hypot(d[:, 0], d[:, 1])
        self.t = d / self.len[:, None]
        # normal = tangent rotated by -90 deg in (rho, z): (t_z, -t_rho). Polylines are built so this points into the fluid.
        self.n = np.stack([self.t[:, 1], -self.t[:, 0]], 1)
        self.mid = 0.5 * (self.a + self.b)
        return self


def _static_ring(rho, z, rp, zp, nr, nz):
    """phi-integrals of 1/R and of d/dn' (1/R) over the full ring (times nothing), for target (rho, z), source ring
    (rp, zp) with source normal (nr, nz). Vectorised."""
    a = rho**2 + rp**2 + (z - zp)**2
    b = 2 * rho * rp
    s = np.sqrt(a + b)
    m = np.clip(2 * b / (a + b), 0, 1 - 1e-15)
    K = ellipk(m); E = ellipe(m)
    I0 = 4 * K / s                                    # int dphi / R
    I3 = 4 * E / ((a - b) * s)                        # int dphi / R^3
    small = b < 1e-12 * a
    with np.errstate(divide='ignore', invalid='ignore'):
        I3c = np.where(small, 1.5 * np.pi * b / a**2.5, 4 / (b * s) * (a / (a - b) * E - K))   # int cos / R^3
    # d/dn' (1/R) = -[(rp - rho cos) nr + (zp - z) nz] / R^3
    D = -((rp * I3 - rho * I3c) * nr + (zp - z) * nz * I3)
    return I0, D


def _gauss(n):
    x, w = np.polynomial.legendre.leggauss(n); return 0.5 * (x + 1), 0.5 * w


def assemble(mesh, k, targets, self_idx=None, nphi=48, ng=6, ng_near=14):
    """Rows for target points (rho, z): returns S, D with  int G q dS = S @ q,  int p dG/dn dS = D @ p.
    self_idx[i] = element index the target i lies on (or -1)."""
    M = len(targets); N = len(mesh.len)
    S = np.zeros((M, N), complex); Dm = np.zeros((M, N), complex)
    phi = (np.arange(nphi) + 0.5) * 2 * np.pi / nphi
    cphi = np.cos(phi); sphi = np.sin(phi)
    gx, gw = _gauss(ng)
    if self_idx is None: self_idx = -np.ones(M, int)
    for i, (rho, z) in enumerate(targets):
        # distance from target to every element (to pick quadrature)
        d = np.hypot(mesh.mid[:, 0] - rho, mesh.mid[:, 1] - z)
        near = d < 2.5 * mesh.len
        for which in (False, True):
            idx = np.where(near == which)[0]
            if len(idx) == 0: continue
            if not which:
                xs, ws = gx, gw
                P = mesh.a[idx][:, None, :] + (mesh.b[idx] - mesh.a[idx])[:, None, :] * xs[None, :, None]   # (n, q, 2)
                W = ws[None, :] * mesh.len[idx][:, None]
                _accumulate(S, Dm, i, idx, P, W, mesh, k, rho, z, cphi, sphi, nphi)
            else:
                for j in idx:
                    # split at the projection of the target on the element, grade quadratically towards it
                    t = np.clip(np.dot(np.array([rho, z]) - mesh.a[j], mesh.t[j]) / mesh.len[j], 0, 1)
                    xs_l, ws_l = _gauss(ng_near)
                    parts = []
                    if t > 1e-9: parts.append((t - t * xs_l**2, 2 * t * xs_l * ws_l))          # towards t from 0
                    if t < 1 - 1e-9: parts.append((t + (1 - t) * xs_l**2, 2 * (1 - t) * xs_l * ws_l))
                    xs = np.concatenate([p[0] for p in parts]); ws = np.concatenate([p[1] for p in parts])
                    P = (mesh.a[j][None, :] + (mesh.b[j] - mesh.a[j])[None, :] * xs[:, None])[None]
                    W = (ws * mesh.len[j])[None]
                    _accumulate(S, Dm, i, np.array([j]), P, W, mesh, k, rho, z, cphi, sphi, nphi)
    return S, Dm


def _accumulate(S, Dm, i, idx, P, W, mesh, k, rho, z, cphi, sphi, nphi):
    rp = P[..., 0]; zp = P[..., 1]
    nr = mesh.n[idx, 0][:, None]; nz = mesh.n[idx, 1][:, None]
    I0, Ist = _static_ring(rho, z, rp, zp, nr, nz)
    # dynamic remainders, trapezoid in phi
    R2 = rho**2 + rp[..., None]**2 - 2 * rho * rp[..., None] * cphi + (z - zp[..., None])**2
    R = np.sqrt(np.maximum(R2, 1e-30))
    ikR = 1j * k * R
    e = np.exp(ikR)
    g1 = np.where(R > 1e-9, (e - 1) / R, 1j * k)                                  # (e^{ikR}-1)/R
    dRdn = ((rp[..., None] - rho * cphi) * nr[..., None] + (zp[..., None] - z) * nz[..., None]) / R
    g2 = np.where(R > 1e-9, (e * (ikR - 1) + 1) / R2, -0.5 * k * k) * dRdn        # d/dn' of g1
    dphi = 2 * np.pi / nphi
    Sk = (I0 + g1.sum(-1) * dphi) / (4 * np.pi)
    Dk = (Ist + g2.sum(-1) * dphi) / (4 * np.pi)
    S[i, idx] += (Sk * rp * W).sum(-1)
    Dm[i, idx] += (Dk * rp * W).sum(-1)


def solve(mesh, f, chief=(), **kw):
    k = 2 * np.pi * f / C
    q = 1j * 2 * np.pi * f * RHO * mesh.vn
    N = len(mesh.len)
    S, D = assemble(mesh, k, mesh.mid, self_idx=np.arange(N), **kw)
    A = D - 0.5 * np.eye(N); rhs = S @ q
    if len(chief):
        Sc, Dc = assemble(mesh, k, np.asarray(chief, float), **kw)
        A = np.vstack([A, Dc]); rhs = np.concatenate([rhs, Sc @ q])
        p = np.linalg.lstsq(A, rhs, rcond=None)[0]
    else:
        p = np.linalg.solve(A, rhs)
    return p, q, k


def far_field(mesh, p, q, k, theta, ng=6, nphi=96):
    """Far-field amplitude F(theta) with p ~ e^{ikr}/(4 pi r) F; theta from +z axis, observer in the phi=0 plane."""
    gx, gw = _gauss(ng)
    P = mesh.a[:, None, :] + (mesh.b - mesh.a)[:, None, :] * gx[None, :, None]
    W = gw[None, :] * mesh.len[:, None]
    rp = P[..., 0]; zp = P[..., 1]
    phi = (np.arange(nphi) + 0.5) * 2 * np.pi / nphi
    out = []
    for th in theta:
        xr, xz = np.sin(th), np.cos(th)
        # x_hat . y = xr * rp cos(phi) + xz * zp ; x_hat . n = xr * nr cos(phi) + xz * nz
        ph = np.exp(-1j * k * (xr * rp[..., None] * np.cos(phi) + xz * zp[..., None]))
        xn = xr * mesh.n[:, 0][:, None, None] * np.cos(phi) + xz * mesh.n[:, 1][:, None, None]
        integ = (p[:, None, None] * (-1j * k * xn) - q[:, None, None]) * ph
        val = (integ.sum(-1) * (2 * np.pi / nphi) * rp * W).sum()
        out.append(val)
    return np.array(out)
