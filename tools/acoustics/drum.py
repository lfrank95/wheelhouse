"""Leslie 122 bass rotor (estimated geometry) as a voxel model, 3D BEM, horizontal polar per third-octave band.
Usage: python3 drum.py [out.npz]
"""
import numpy as np, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from bem3d import QuadMesh, solve, far_field, C

H = 0.025                      # voxel size
R = 0.215                      # drum radius (17 in)
ZB, ZT = 0.0, 0.25             # drum bottom / top
W = 0.15                       # half width of the scoop channel (12 in opening)
XB = -0.15                     # back of the channel at the top (scoop starts vertical here)
GAP = 0.025                    # drum top -> woofer board
RW = 0.165                     # woofer piston radius (15 in cone, effective)
SX, SY, SZ = 0.375, 0.25, 0.10 # woofer board / middle compartment floor: half sizes x, y and thickness


def scoop_z(x):
    """Upper surface of the scoop: quarter ellipse, vertical at the back top, horizontal at the front bottom."""
    u = np.clip((x - R) / (R - XB), -1, 0)
    return ZT - (ZT - ZB - H) * np.sqrt(1 - u * u)


def build():
    xs = np.arange(-0.40, 0.40 + 1e-9, H); ys = np.arange(-0.275, 0.275 + 1e-9, H); zs = np.arange(-0.05, ZT + GAP + SZ + 0.05 + 1e-9, H)
    xc, yc, zc = (xs[:-1] + xs[1:]) / 2, (ys[:-1] + ys[1:]) / 2, (zs[:-1] + zs[1:]) / 2
    X, Y, Z = np.meshgrid(xc, yc, zc, indexing='ij')
    r = np.hypot(X, Y)
    inR = r <= R
    solid = np.zeros(X.shape, bool)
    solid |= inR & (Z > ZB) & (Z < ZB + H)                                   # bottom disc
    top = inR & (Z > ZT - H) & (Z < ZT)
    inlet = (np.abs(Y) < W) & (X > XB) & (np.hypot(X, Y) < RW + 1e-9)
    solid |= top & ~inlet                                                    # top ring with the inlet over the channel
    zsurf = scoop_z(X)
    chan = (np.abs(Y) < W) & (X > XB - H) & inR
    solid |= chan & (Z > ZB) & (Z < ZT) & (Z <= zsurf + H / 2) & (Z > zsurf - 1.5 * H)    # scoop board (two voxels, face-connected)
    side = inR & (np.abs(np.abs(Y) - (W + H / 2)) < H / 2 + 1e-9) & (X > XB - H) & (Z > ZB) & (Z < ZT)
    solid |= side                                                            # channel side boards
    zb0 = ZT + GAP
    solid |= (np.abs(X) < SX) & (np.abs(Y) < SY) & (Z > zb0) & (Z < zb0 + SZ)   # woofer board / compartment
    # boundary faces
    V = {}; Vl = []; Q = []; vn = []
    def vid(p):
        key = tuple(np.round(np.array(p) / H * 2).astype(int))
        if key not in V: V[key] = len(Vl); Vl.append(p)
        return V[key]
    nx, ny, nz = solid.shape
    S = np.pad(solid, 1)
    dirs = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    for d in dirs:
        nb = S[1 + d[0]:1 + d[0] + nx, 1 + d[1]:1 + d[1] + ny, 1 + d[2]:1 + d[2] + nz]
        I, J, K = np.nonzero(solid & ~nb)
        ax = int(np.argmax(np.abs(d))); sgn = d[ax]
        o = [a for a in range(3) if a != ax]
        for i, j, k in zip(I, J, K):
            c = np.array([xc[i], yc[j], zc[k]]); c[ax] += sgn * H / 2
            e1 = np.zeros(3); e1[o[0]] = H / 2; e2 = np.zeros(3); e2[o[1]] = H / 2
            pts = [c - e1 - e2, c + e1 - e2, c + e1 + e2, c - e1 + e2]
            nrm = np.cross(pts[2] - pts[0], pts[3] - pts[1])
            if np.dot(nrm, d) < 0: pts = pts[::-1]
            Q.append([vid(p) for p in pts])
            piston = (d == (0, 0, -1)) and abs(c[2] - zb0) < 1e-9 and np.hypot(c[0], c[1]) <= RW
            vn.append(1.0 if piston else 0.0)
    mesh = QuadMesh(np.array(Vl), np.array(Q), np.array(vn))
    chief = [(0.1, 0.1, zb0 + SZ / 2), (-0.2, -0.1, zb0 + SZ / 2), (0.25, -0.15, zb0 + SZ * 0.3), (0.0, 0.0, ZB + H / 2), (0.1, -0.1, ZB + H / 2)]
    return mesh, chief


BANDS = [100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000]
PHI = np.radians(np.arange(0, 360, 5))

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'drum.npz'
    mesh, chief = build()
    print('elements', len(mesh.Q), 'piston', int((mesh.vn != 0).sum()), flush=True)
    zmid = (ZB + ZT) / 2
    dirs = np.c_[np.cos(PHI), np.sin(PHI), np.zeros_like(PHI)]
    res = []
    for fc in BANDS:
        pw = np.zeros(len(PHI))
        fl = fc * 2 ** ((np.arange(4) - 1.5) / 12)
        for f in fl:
            p, q, k = solve(mesh, f)          # interior (irregular) frequencies of the bodies lie above 1.7 kHz
            pw += np.abs(far_field(mesh, p, q, k, dirs)) ** 2 / len(fl)
        res.append(pw)
        r = 10 * np.log10(pw / pw.max())
        print(fc, 'front/back %.1f dB  swing %.1f dB  max at %d deg' % (r[0] - r[36], r.max() - r.min(), int(np.degrees(PHI[np.argmax(pw)]))), flush=True)
    np.savez(out, bands=BANDS, phi=np.degrees(PHI), power=np.array(res))
