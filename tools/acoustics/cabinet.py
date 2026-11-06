"""Rotary 122 cabinet: radiation of the horn and the bass rotor through the louvred faces, including shadowing and
diffraction by the box. 3D exterior BEM (bem3d.py) up to 2 kHz, physical-optics apertures with edge diffraction
above. Output: power per third-octave band for every rotor angle and far-field direction, for
horn with deflector, horn open, drum.

Model
  * The cabinet is a closed box (41 x 29 x 20.5 in). The louvred areas (front and both sides of the upper
    compartment for the horn, same for the lower compartment for the drum) are the only places where sound gets out:
    each element there gets the normal velocity of the rotor's direct field (free-field directivity from the rotor
    BEM results, 1/r, propagation phase) as a Neumann boundary condition; the rest of the box is rigid. The exterior
    BEM then gives the far field with the box's diffraction: what wraps around to the back at low frequencies, how
    the openings beam at high ones, and the interference between the three faces.
  * The interior reverberant field of the compartment is not part of this table; it is the engine's diffuse
    horn-box path. The louvre slats themselves are small against the wavelength up to ~7 kHz and are taken as a
    frequency-independent transmission (normalised away).
  * Above 2 kHz (box ~ 6 wavelengths) the far field is the Kirchhoff sum of the aperture elements, each radiating
    into its half space with the (1 + cos) obliquity factor; behind a face the level follows Maekawa's barrier
    curve over the nearest box edge. Spliced to the BEM in the 1.25..2 kHz overlap.
Usage: python3 cabinet.py [out.npz]       (takes ~1 h: 42 BEM solves of ~5500 elements)
"""
import os, sys, time
import numpy as np
from scipy.linalg import lu_factor, lu_solve
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bem3d import QuadMesh, assemble, C, RHO

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, 'results')

# ---------------- geometry (m): front = +x, left = +y, floor z = 0 ----------------
D, W, H = 0.521, 0.737, 1.041                 # depth, width, height
HX, HY = D / 2, W / 2
EL = float(os.environ.get('CAB_EL', 0.025))  # element size
FRAME = 0.05                                  # solid frame around a louvre area
UP_Z = (0.760, 0.990)                         # upper (horn) louvres
LO_Z = (0.080, 0.330)                         # lower (drum) louvres
HORN = dict(r=0.178, a=0.08, z=0.910, tables=('horn_deflector', 'horn_open'), zone=UP_Z)
DRUM = dict(r=0.130, a=0.10, z=0.200, tables=('drum',), zone=LO_Z)
TAU = 1.0                                     # louvre transmission (level is normalised later)
JITTER, NREAL = 0.03, 3                       # slat scattering: path-length jitter per element (m), realisations

BEM_BANDS = [100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000]
PO_BANDS = [1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]
ALPHA = np.radians(np.arange(0, 360, 7.5))                     # rotor (mouth) angle from the front
AZ = np.radians(np.arange(0, 360, 7.5))                        # far-field azimuth from the front
ELV = np.radians(np.arange(-90, 91, 15))                       # far-field elevation


def build():
    """Closed box as structured quads per face; returns the mesh and per-element face ids / louvre zone ids."""
    V, Q, face, zone = [], [], [], []
    def grid(origin, u, v, lu, lv, fid):
        nu, nv = int(round(lu / EL)), int(round(lv / EL))
        base = len(V)
        for j in range(nv + 1):
            for i in range(nu + 1):
                V.append(np.asarray(origin) + u * (lu * i / nu) + v * (lv * j / nv))
        for j in range(nv):
            for i in range(nu):
                a = base + j * (nu + 1) + i
                Q.append([a, a + 1, a + nu + 2, a + nu + 1]); face.append(fid)
    ex, ey, ez = np.eye(3)
    grid((HX, -HY, 0), ey, ez, W, H, 'front')        # normal +x = ey x ez
    grid((-HX, HY, 0), -ey, ez, W, H, 'back')        # -ey x ez = -x
    grid((HX, HY, 0), -ex, ez, D, H, 'left')         # -ex x ez = +y
    grid((-HX, -HY, 0), ex, ez, D, H, 'right')       # ex x ez = -y
    grid((-HX, -HY, H), ex, ey, D, W, 'top')         # ex x ey = +z
    grid((-HX, HY, 0), ex, -ey, D, W, 'bottom')      # ex x -ey = -z
    mesh = QuadMesh(np.array(V), np.array(Q))
    face = np.array(face)
    c = mesh.c
    def louvre(zlo, zhi):
        inz = (c[:, 2] > zlo) & (c[:, 2] < zhi)
        fr = (face == 'front') & inz & (np.abs(c[:, 1]) < HY - FRAME)
        sd = ((face == 'left') | (face == 'right')) & inz & (np.abs(c[:, 0]) < HX - FRAME)
        return fr | sd
    zone = np.zeros(len(Q), int)
    zone[louvre(*UP_Z)] = 1
    zone[louvre(*LO_Z)] = 2
    return mesh, face, zone


class RotorPattern:
    """Free-field amplitude of a rotor vs angle from its mouth axis, from the rotor BEM results, prepared exactly as
    for the engine's own tables (fit_directivity: smoothed, horn held beyond 155 deg where the axisymmetric model
    shows a spurious rear bright spot); sqrt of the third-octave power, log-frequency interpolation, held flat
    outside the simulated range."""
    def __init__(self, name):
        from fit_directivity import load_horn, load_drum
        B, th, P = (load_drum if name == 'drum' else load_horn)(os.path.join(RES, name + '.npz'))
        self.f = B; self.th = np.radians(th.astype(float)); self.A = np.sqrt(P)
        self.A /= np.sqrt((self.A ** 2 * np.sin(self.th)).sum(1) / np.sin(self.th).sum())[:, None]  # unit mean power

    def __call__(self, cos_t, f):
        fi = np.clip(np.interp(np.log(f), np.log(self.f), np.arange(len(self.f))), 0, len(self.f) - 1)
        i0 = int(np.floor(fi)); i1 = min(i0 + 1, len(self.f) - 1); w = fi - i0
        th = np.arccos(np.clip(cos_t, -1, 1))
        return (1 - w) * np.interp(th, self.th, self.A[i0]) + w * np.interp(th, self.th, self.A[i1])


def jitter(sel, real):
    """Fixed per-element path-length jitter of one realisation (the slats scatter: at high frequencies the aperture
    radiates incoherently instead of beaming like a clean plane wave)."""
    return np.random.default_rng(100 + real).normal(0, JITTER, sel.max() + 1)[sel]


def mouth_points(rotor, alpha):
    """The mouth as a disc of sub-sources (centre, rings at 0.5 a and 0.9 a) in the plane normal to the mouth axis:
    close to the mouth the field is that of a piston, not of a point; the far field keeps the rotor's pattern."""
    u = np.array([np.cos(alpha), np.sin(alpha), 0.0]); s = np.array([-np.sin(alpha), np.cos(alpha), 0.0]); up = np.array([0, 0, 1.0])
    ctr = np.array([rotor['r'] * np.cos(alpha), rotor['r'] * np.sin(alpha), rotor['z']])
    pts = [ctr]
    for rad, n in ((0.5, 6), (0.9, 12)):
        for i in range(n):
            ph = 2 * np.pi * i / n
            pts.append(ctr + rotor['a'] * rad * (np.cos(ph) * s + np.sin(ph) * up))
    return np.array(pts), u


def incident_velocity(mesh, sel, rotor, pattern, alpha, f, real=0):
    """Normal velocity (complex, per selected element) of the rotor's direct field: the mouth's sub-sources with the
    free-field pattern and propagation phase, projected on the face normal (grazing incidence transmits nothing)."""
    k = 2 * np.pi * f / C
    pts, u = mouth_points(rotor, alpha)
    v = mesh.c[sel][:, None, :] - pts[None]                       # (ne, np, 3)
    rr = np.linalg.norm(v, axis=2); vh = v / rr[..., None]
    amp = (pattern((vh @ u).ravel(), f).reshape(rr.shape) / np.maximum(rr, 0.02) * np.exp(1j * k * rr)).mean(1)
    amp *= np.exp(1j * k * jitter(sel, real))
    vd = (mesh.c[sel] - pts[0]); vd /= np.linalg.norm(vd, axis=1)[:, None]
    cosn = np.maximum(0, np.einsum('ij,ij->i', vd, mesh.n[sel]))
    return TAU * amp * cosn / (RHO * C)


def dirs_grid():
    az, el = np.meshgrid(AZ, ELV, indexing='ij')
    return np.c_[(np.cos(el) * np.cos(az)).ravel(), (np.cos(el) * np.sin(az)).ravel(), np.sin(el).ravel()]


def far_matrices(mesh, k, dirs, m=2):
    """Fp, Fq (ndirs, ne): F = Fp @ p + Fq @ q, p ~ e^{ikr}/(4 pi r) F."""
    P, Wq = mesh.points(m)                                       # (ne,q,3), (ne,q)
    ph = np.exp(-1j * k * np.einsum('eqk,dk->deq', P, dirs))     # (nd,ne,q)
    Fq = -(ph * Wq[None]).sum(2)
    Fp = (ph * Wq[None]).sum(2) * (-1j * k) * (dirs @ mesh.n.T)
    return Fp, Fq


def chief_points(n=14, seed=1):
    rng = np.random.default_rng(seed)
    return np.c_[rng.uniform(-HX * 0.8, HX * 0.8, n), rng.uniform(-HY * 0.8, HY * 0.8, n), rng.uniform(0.1, H - 0.1, n)]


def bem_band(mesh, zone, cases, fc, dirs):
    """Power (ncases, nalpha, ndirs) in the third-octave band fc, three frequencies per band."""
    out = 0
    fl = fc * 2 ** ((np.arange(3) - 1) / 9)
    for f in fl:
        k = 2 * np.pi * f / C; N = len(mesh.Q)
        t0 = time.time()
        S, Dm = assemble(mesh, k, mesh.c, self_rows=np.arange(N))
        A = Dm - 0.5 * np.eye(N)
        Sc, Dc = assemble(mesh, k, chief_points())
        del Dm
        # right-hand sides: one per case and rotor angle
        Qs = []
        for (rotor, pattern, z) in cases:
            sel = np.nonzero(zone == z)[0]
            for a in ALPHA:
                for real in range(NREAL):
                    q = np.zeros(N, complex)
                    q[sel] = 1j * 2 * np.pi * f * RHO * incident_velocity(mesh, sel, rotor, pattern, a, f, real)
                    Qs.append(q)
        Qs = np.array(Qs).T                                         # (N, ncases*nalpha)
        rhs = S @ Qs; rhs_c = Sc @ Qs
        # least squares with the CHIEF rows via the normal equations of the augmented system (A is well
        # conditioned away from the irregular frequencies; the extra rows only remove the spurious null space)
        AH = A.conj().T
        M = AH @ A + Dc.conj().T @ Dc
        b = AH @ rhs + Dc.conj().T @ rhs_c
        del S, A, AH
        p = np.linalg.solve(M, b)
        del M, b
        Fp, Fq = far_matrices(mesh, k, dirs)
        F = Fp @ p + Fq @ Qs                                        # (ndirs, ncases*nalpha*NREAL)
        out = out + (np.abs(F.T) ** 2).reshape(len(cases), len(ALPHA), NREAL, len(dirs)).mean(2) / len(fl)
        print(f'  {f:7.1f} Hz  {time.time() - t0:5.0f} s', flush=True)
    return out


def po_band(mesh, face, zone, cases, fc, dirs):
    """Kirchhoff aperture sum with Maekawa shadowing behind each face."""
    out = 0
    fl = fc * 2 ** ((np.arange(3) - 1) / 9)
    lam_edges = {'front': ('left', 'right', 'top', 'bottom', 'back'), 'left': ('front', 'back', 'top', 'bottom'),
                 'right': ('front', 'back', 'top', 'bottom')}
    for f in fl:
        k = 2 * np.pi * f / C; lam = C / f
        res = []
        for (rotor, pattern, z) in cases:
            sel = np.nonzero(zone == z)[0]
            n = mesh.n[sel]; x = mesh.c[sel]; dA = mesh.area[sel]
            cosd = dirs @ n.T                                        # (nd, ne) cos of the angle face normal - direction
            ph = np.exp(-1j * k * (dirs @ x.T))
            front = np.where(cosd > 0, (1 + cosd) / 2, 0)
            # behind the face: detour over the nearest box edge in the direction's in-plane projection
            # delta ~ s (1 - cos beta) with s = distance element -> edge, beta = angle below the face plane
            beta = np.arcsin(np.clip(-cosd, 0, 1))
            dpar = dirs[:, None, :] - cosd[:, :, None] * n[None]                  # in-plane component
            dpar /= np.maximum(np.linalg.norm(dpar, axis=2, keepdims=True), 1e-9)
            # distance to the face border along dpar (box faces are axis aligned rectangles)
            lim = np.array([HX, HY, H / 2]); ctr = np.array([0, 0, H / 2])
            t = np.full(cosd.shape, 1e9)
            for ax in range(3):
                dd = dpar[:, :, ax]; xe = (x[:, ax] - ctr[ax])[None]
                with np.errstate(divide='ignore', invalid='ignore'):
                    tp = np.where(dd > 1e-9, (lim[ax] - xe) / dd, np.where(dd < -1e-9, (-lim[ax] - xe) / dd, 1e9))
                t = np.minimum(t, np.where(tp > 0, tp, 1e9))
            s = np.where(t < 1e8, t, 0.3)
            delta = s * (1 - np.cos(beta)) + 0.5 * EL * np.sin(beta)
            Nf = 2 * delta / lam
            il = np.where(cosd < 0, 10 * np.log10(np.maximum(3 + 20 * Nf, 1)), 0)
            shadow = np.where(cosd < 0, 0.5 * 10 ** (-il / 20), 0)  # 0.5: continuity with (1+cos)/2 at grazing
            Wd = front + shadow
            Fa = []
            for a in ALPHA:
                pw = 0
                for real in range(NREAL):
                    v = incident_velocity(mesh, sel, rotor, pattern, a, f, real)
                    pw = pw + np.abs((((1j * k * RHO * C) * v * dA)[None, :] * ph * Wd).sum(1) * 2) ** 2 / NREAL  # baffled: x2
                Fa.append(pw)
            res.append(np.array(Fa))
        out = out + np.array(res) / len(fl)
    return out


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RES, 'cabinet.npz')
    mesh, face, zone = build()
    dirs = dirs_grid()
    cases = [(HORN, RotorPattern('horn_deflector'), 1), (HORN, RotorPattern('horn_open'), 1), (DRUM, RotorPattern('drum'), 2)]
    print('elements', len(mesh.Q), 'upper louvres', int((zone == 1).sum()), 'lower louvres', int((zone == 2).sum()),
          'rhs per frequency', len(cases) * len(ALPHA), flush=True)
    po = []
    for fc in PO_BANDS:
        po.append(po_band(mesh, face, zone, cases, fc, dirs)); print('PO', fc, flush=True)
    np.savez(out, alpha=np.degrees(ALPHA), az=np.degrees(AZ), el=np.degrees(ELV), names=np.array(['horn_deflector', 'horn_open', 'drum']),
             po_bands=PO_BANDS, po=np.array(po), bem_bands=[], bem=np.zeros(0))
    bem = []
    for fc in BEM_BANDS:
        print('BEM', fc, flush=True)
        bem.append(bem_band(mesh, zone, cases, fc, dirs))
        np.savez(out, alpha=np.degrees(ALPHA), az=np.degrees(AZ), el=np.degrees(ELV), names=np.array(['horn_deflector', 'horn_open', 'drum']),
                 po_bands=PO_BANDS, po=np.array(po), bem_bands=BEM_BANDS[:len(bem)], bem=np.array(bem))
    print('wrote', out)
