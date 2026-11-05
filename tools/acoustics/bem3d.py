"""3D exterior Helmholtz BEM (Neumann), constant elements on flat quadrilaterals, collocation at the centroids.

Same conventions as axibem: e^{-iwt}, G = e^{ikR}/(4 pi R), normals out of the body into the fluid,
    1/2 p_i = sum_j [ D_ij p_j - S_ij q_j ],   q = i w rho v_n.
Quadrature: 2x2 Gauss for far pairs, 6x6 for near pairs (< 3 element sizes), self term: static 1/R analytic over
the four triangles centroid-edge, dynamic remainder by 4x4 Gauss; D_ii = 0 (flat element).
"""
import numpy as np

RHO, C = 1.204, 343.0


def _gl(n):
    x, w = np.polynomial.legendre.leggauss(n); return 0.5 * (x + 1), 0.5 * w


class QuadMesh:
    def __init__(self, V, Q, vn=None):
        """V: (nv,3) vertices, Q: (ne,4) vertex indices ordered counter-clockwise seen from the fluid."""
        self.V = np.asarray(V, float); self.Q = np.asarray(Q, int)
        P = self.V[self.Q]                                   # (ne,4,3)
        self.P = P
        self.c = P.mean(1)
        n = np.cross(P[:, 2] - P[:, 0], P[:, 3] - P[:, 1])  # diagonal cross product: 2 * area * normal
        self.area = 0.5 * np.linalg.norm(n, axis=1)
        self.n = n / (2 * self.area[:, None])
        self.size = np.sqrt(self.area)
        self.vn = np.zeros(len(self.Q), complex) if vn is None else np.asarray(vn, complex)

    def points(self, m):
        """Gauss points (ne, m*m, 3) and weights (ne, m*m) on the bilinear map of each quad."""
        x, w = _gl(m)
        u, v = np.meshgrid(x, x, indexing='ij'); u = u.ravel(); v = v.ravel(); ww = np.outer(w, w).ravel()
        P = self.P
        N = np.stack([(1 - u) * (1 - v), u * (1 - v), u * v, (1 - u) * v], 1)            # (q,4)
        dNu = np.stack([-(1 - v), (1 - v), v, -v], 1); dNv = np.stack([-(1 - u), -u, u, (1 - u)], 1)
        X = np.einsum('qa,eak->eqk', N, P)
        Xu = np.einsum('qa,eak->eqk', dNu, P); Xv = np.einsum('qa,eak->eqk', dNv, P)
        J = np.linalg.norm(np.cross(Xu, Xv), axis=2)
        return X, ww[None, :] * J


def _static_self(mesh):
    """int 1/R over each element with R measured from its centroid (four triangles centroid - edge)."""
    out = np.zeros(len(mesh.Q))
    for a in range(4):
        A = mesh.P[:, a] - mesh.c; B = mesh.P[:, (a + 1) % 4] - mesh.c
        e = B - A; L = np.linalg.norm(e, axis=1); ok = L > 1e-12; L = np.where(ok, L, 1); t = e / L[:, None]
        sA = np.einsum('ij,ij->i', A, t); sB = np.einsum('ij,ij->i', B, t)     # signed positions along the edge
        d = np.linalg.norm(A - sA[:, None] * t, axis=1)                         # distance centroid -> edge line
        d = np.where(ok & (d > 1e-15), d, 1)
        out += np.where(ok, d * (np.arcsinh(sB / d) - np.arcsinh(sA / d)), 0)
    return out


def assemble(mesh, k, X, self_rows=None, far_q=2, near_q=6, near_fac=3.0, chunk=150):
    """S, D rows for target points X (M,3). self_rows[i] = element index of target i, or -1."""
    M = len(X); N = len(mesh.Q)
    S = np.zeros((M, N), complex); D = np.zeros((M, N), complex)
    Pf, Wf = mesh.points(far_q); Pn, Wn = mesh.points(near_q)
    nrm = mesh.n
    if self_rows is None: self_rows = -np.ones(M, int)
    for r0 in range(0, M, chunk):
        r1 = min(M, r0 + chunk); x = X[r0:r1]
        dist = np.linalg.norm(x[:, None, :] - mesh.c[None], axis=2)            # (m,N)
        near = dist < near_fac * mesh.size[None]
        for (Pq, Wq, mask) in ((Pf, Wf, ~near), (Pn, Wn, near)):
            ii, jj = np.nonzero(mask)
            if len(ii) == 0: continue
            for s0 in range(0, len(ii), 400000 // Pq.shape[1] + 1):
                a = ii[s0:s0 + 400000 // Pq.shape[1] + 1]; b = jj[s0:s0 + 400000 // Pq.shape[1] + 1]
                own = self_rows[r0 + a] == b
                Rv = Pq[b] - x[a][:, None, :]                                    # y - x
                R = np.linalg.norm(Rv, axis=2)
                R = np.where(R < 1e-12, 1e-12, R)
                g = np.exp(1j * k * R) / (4 * np.pi * R)
                dGdR = g * (1j * k - 1 / R)
                dRdn = np.einsum('pqk,pk->pq', Rv, nrm[b]) / R
                sv = (g * Wq[b]).sum(1); dv = (dGdR * dRdn * Wq[b]).sum(1)
                sv[own] = 0; dv[own] = 0
                np.add.at(S, (r0 + a, b), sv); np.add.at(D, (r0 + a, b), dv)
    # self terms
    rows = np.nonzero(self_rows >= 0)[0]
    if len(rows):
        e = self_rows[rows]
        P4, W4 = mesh.points(4)
        R = np.linalg.norm(P4[e] - X[rows][:, None, :], axis=2)
        rem = np.where(R > 1e-12, (np.exp(1j * k * R) - 1) / R, 1j * k)
        S[rows, e] = (_static_self(mesh)[e] + (rem * W4[e]).sum(1)) / (4 * np.pi)
        D[rows, e] = 0
    return S, D


def solve(mesh, f, chief=()):
    k = 2 * np.pi * f / C; q = 1j * 2 * np.pi * f * RHO * mesh.vn
    N = len(mesh.Q)
    S, D = assemble(mesh, k, mesh.c, self_rows=np.arange(N))
    A = D - 0.5 * np.eye(N); rhs = S @ q
    if len(chief):
        Sc, Dc = assemble(mesh, k, np.asarray(chief, float))
        A = np.vstack([A, Dc]); rhs = np.concatenate([rhs, Sc @ q])
        p = np.linalg.lstsq(A, rhs, rcond=None)[0]
    else:
        p = np.linalg.solve(A, rhs)
    return p, q, k


def far_field(mesh, p, q, k, dirs, m=3):
    """F(x_hat) with p ~ e^{ikr}/(4 pi r) F. dirs: (nd,3) unit vectors."""
    P, W = mesh.points(m)
    out = np.zeros(len(dirs), complex)
    for i, d in enumerate(dirs):
        ph = np.exp(-1j * k * (P @ d))                                         # (ne,q)
        xn = mesh.n @ d
        out[i] = ((p[:, None] * (-1j * k * xn[:, None]) - q[:, None]) * ph * W).sum()
    return out


def sphere(a, nlat=24, nlon=48):
    """Lat-long quad mesh of a sphere (poles as degenerate quads), normals outward."""
    th = np.linspace(0, np.pi, nlat + 1); ph = np.linspace(0, 2 * np.pi, nlon + 1)[:-1]
    V = []; idx = {}
    for i, t in enumerate(th):
        for j, p in enumerate(ph):
            if (i == 0 or i == nlat) and j > 0: idx[(i, j)] = idx[(i, 0)]; continue
            idx[(i, j)] = len(V); V.append([a * np.sin(t) * np.cos(p), a * np.sin(t) * np.sin(p), a * np.cos(t)])
    Q = []
    for i in range(nlat):
        for j in range(nlon):
            j1 = (j + 1) % nlon
            Q.append([idx[(i, j)], idx[(i + 1, j)], idx[(i + 1, j1)], idx[(i, j1)]])
    return np.array(V), np.array(Q)
