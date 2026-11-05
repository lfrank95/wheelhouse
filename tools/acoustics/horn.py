"""Rotary 122 treble horn (estimated geometry), axisymmetric BEM: polar response per third-octave band.
Usage: python3 horn.py <variant> [out.npz]   variant: none (no deflector) | g<gap>h<depth>, e.g. g-8h30 (deflector apex <gap> mm in front of the mouth, negative = inside; cone depth <depth> mm). Wheelhouse uses g-8h30.
"""
import numpy as np, sys, json
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from axibem import Mesh, solve, far_field, C

mm = 1e-3
A0, L, LB, R1, AM, T = 9.5*mm, 150*mm, 25*mm, 28*mm, 45*mm, 3*mm      # throat r, length, bell length, r at bell start, mouth r, wall
RD, HC, TB = 51*mm, 12*mm, 2*mm                                         # deflector radius, cone depth, back plate


def r_in(z):
    zc = L - LB
    if z <= zc: return A0 + (R1 - A0) * z / zc
    u = (z - zc) / LB
    slope = (R1 - A0) / zc * LB                       # keep the cone's slope at the joint, then flare (quadratic)
    return R1 + slope * u + (AM - R1 - slope) * u * u


def build(variant, h):
    m = Mesh()
    zs = np.linspace(0, L, 121)
    inner = np.array([[r_in(z), z] for z in zs])
    outer = inner + np.array([T, 0])
    body = [[0, -10*mm], [outer[0, 0], -10*mm]] + outer.tolist() + [[AM, L]] + inner[::-1].tolist()
    m.add_polyline(body, h)                                       # bottom, outer wall up, rim, inner wall down
    m.add_polyline([[A0, 0], [0, 0]], h, vn=1.0)                  # piston face into the horn
    chief = [((r_in(z) + T / 2), z) for z in (0.03, 0.08, 0.12)] + [(4*mm, -5*mm)]
    if variant != 'none':
        import re
        mt = re.match(r'g(-?[0-9.]+)(?:h([0-9.]+))?$', variant)
        g = float(mt.group(1)) * mm; hc = float(mt.group(2)) * mm if mt.group(2) else HC
        z0 = L + g
        HCv = hc
        m.add_polyline([[0, z0], [RD, z0 + HCv], [RD, z0 + HCv + TB], [0, z0 + HCv + TB]], h)
        chief += [(rr, z0 + HCv * rr / RD + (HCv * (1 - rr / RD) + TB) / 2) for rr in (15*mm, 30*mm, 45*mm)]
    return m.finish(), chief


BANDS = [800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]
TH = np.radians(np.arange(0, 181, 5))

if __name__ == '__main__':
    variant = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else f'{variant}.npz'
    res = []
    for fc in BANDS:
        pw = np.zeros(len(TH))
        for f in fc * 2 ** ((np.arange(8) - 3.5) / 24):
            h = min(4*mm, C / f / 12)
            m, chief = build(variant, h)
            p, q, k = solve(m, f, chief=chief)
            F = far_field(m, p, q, k, TH)
            pw += np.abs(F) ** 2 / 8
        res.append(pw)
        print(variant, fc, 'N', len(m.len), 'front/back %.1f dB' % (10 * np.log10(pw[0] / pw[-1])), flush=True)
    np.savez(out, bands=BANDS, theta=np.degrees(TH), power=np.array(res))
