"""Turns the cabinet simulation (results/cabinet.npz, from cabinet.py) into the engine's radiation tables
Source/CabinetDirectivity.h: for every rotor angle, far-field azimuth and elevation (all relative to the cabinet
front) the gains of the in-phase Linkwitz-Riley bands - horn with deflector, horn open, drum. These tables replace
the pair "free-field rotor pattern x hand-made cabinet directivity" in the engine.

Steps
1. Third-octave spectrum 100 Hz .. 16 kHz per (rotor angle, direction): BEM up to 2 kHz, physical optics above,
   the PO level matched to the BEM over the 1.25..2 kHz overlap.
2. Normalisation per third-octave: the mean power over all rotor angles and the sphere equals that of the previous
   model (rotor table x cabinet()), so the diffuse-field tone of the instrument stays exactly as it was.
3. Band gains: power of the third-octaves weighted with each band's share, same band split as the engine.
Usage: python3 fit_cabinet.py [results/cabinet.npz] [out.h]
"""
import os, re, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', '..', 'Source')
NPZ = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'results', 'cabinet.npz')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(SRC, 'CabinetDirectivity.h')
HORN_EDGES, DRUM_EDGES = [1250, 3150, 8000], [200, 400]
THIRDS = np.array([100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
                   6300, 8000, 10000, 12500, 16000], float)


def lr4bands(f, edges):
    out, carry = [], 1.0
    for e in edges:
        lp = 1 / (1 + (f / e) ** 4); out.append(carry * lp); carry *= 1 - lp
    out.append(carry); return np.array(out)


def rotor_tables():
    """The engine's free-field rotor tables (dB per 5 deg) from RotorDirectivity.h."""
    s = open(os.path.join(SRC, 'RotorDirectivity.h')).read(); out = {}
    for name in ('HORN_DEFLECTOR', 'HORN_OPEN', 'DRUM'):
        body = re.search(name + r'\[37\]\[\d\] = \{(.*?)\};', s, re.S).group(1)
        rows = re.findall(r'\{([^{}]*)\}', body)
        out[name] = np.array([[float(v) for v in r.split(',')] for r in rows])
    return out


def old_cabinet(dx, dy, dz):
    """cabinet() of the previous engine (front = +x): gain and low-pass corner per direction."""
    hd = np.maximum(np.hypot(dx, dy), 1e-6); fwd = dx / hd; vert = np.abs(dz) / np.hypot(hd, dz)
    b = np.maximum(0, -fwd)
    g = np.where(fwd > 0, 1, 1 - 0.72 * b) * (1 - 0.35 * vert ** 2)
    fc = np.where(fwd > 0, 16000, 16000 - 14000 * b * b) * (1 - 0.7 * vert ** 2)
    return g, fc


def old_power(name, edges, alpha, az, el, f):
    """Power of the previous model on the grid: rotor band table at the mouth-path angle x cabinet gain x LP."""
    tab = rotor_tables()[name]
    A, Z, E = np.meshgrid(alpha, az, el, indexing='ij')
    dx, dy, dz = np.cos(E) * np.cos(Z), np.cos(E) * np.sin(Z), np.sin(E)
    cosT = np.clip(np.cos(A) * dx + np.sin(A) * dy, -1, 1)
    th = np.degrees(np.arccos(cosT)); j = np.minimum(35, (th / 5).astype(int)); w = (th - 5 * j) / 5
    G = tab[j] * (1 - w[..., None]) + tab[j + 1] * w[..., None]           # (..., nb) dB
    S = lr4bands(f, edges)
    amp = (S * 10 ** (G / 20)).sum(-1)
    g, fc = old_cabinet(dx, dy, dz)
    return amp ** 2 * g ** 2 / (1 + (f / fc) ** 2)


def spectrum(D, ci):
    """Third-octave power (nf, NA, NZ, NE) of case ci, BEM + level-matched PO."""
    NA, NZ, NE = len(D['alpha']), len(D['az']), len(D['el'])
    bem_b = list(D['bem_bands']); po_b = list(D['po_bands'])
    bem = D['bem'][:, ci].reshape(len(bem_b), NA, NZ, NE); po = D['po'][:, ci].reshape(len(po_b), NA, NZ, NE)
    ov = [b for b in bem_b if b in po_b]
    off = np.mean([bem[bem_b.index(b)].mean() / po[po_b.index(b)].mean() for b in ov]) if ov else 1.0
    out = []
    for f in THIRDS:
        if f in bem_b: out.append(bem[bem_b.index(int(f))])
        elif f in po_b: out.append(po[po_b.index(int(f))] * off)
        else: out.append(None)
    # frequencies without data (should not happen for a complete run): hold the nearest
    for i in range(len(out)):
        if out[i] is None:
            j = min((k for k in range(len(out)) if out[k] is not None), key=lambda k: abs(k - i)); out[i] = out[j]
    return np.array(out), 10 * np.log10(off)


def fit_case(D, ci, name, edges, fmin):
    alpha, az, el = np.radians(D['alpha']), np.radians(D['az']), np.radians(D['el'])
    P, offdb = spectrum(D, ci)
    use = THIRDS >= fmin
    wgt = np.cos(el)[None, None, :]                                        # sphere weight
    for i, f in enumerate(THIRDS):
        if not use[i]: continue
        old = old_power(name, edges, alpha, az, el, f)
        P[i] *= (old * wgt).mean() / (P[i] * wgt).mean()
    # band gain = power of the third-octaves weighted with the band's share of the sum (the Linkwitz-Riley bands
    # overlap smoothly, a plain least squares turns that overlap into wild, even negative gains)
    S = np.array([lr4bands(f, edges) for f in THIRDS[use]])                # (nf, nb)
    Pu = P[use].reshape(use.sum(), -1)                                       # (nf, npts)
    Wb = S ** 2 / (S ** 2).sum(0, keepdims=True)                             # (nf, nb) weights per band
    G = np.sqrt(Wb.T @ Pu)                                                   # (nb, npts) amplitudes
    T = np.sqrt(Pu)
    fitdb = 20 * np.log10(np.maximum(S @ G, 1e-6)) - 20 * np.log10(T)
    nb = len(edges) + 1
    Gdb = 20 * np.log10(G.T).reshape(len(alpha), len(az), len(el), nb)
    print(f'{name}: PO offset {offdb:+.1f} dB, band fit error median {np.median(np.abs(fitdb)):.2f} dB, '
          f'90% {np.percentile(np.abs(fitdb), 90):.2f} dB, gains {Gdb.min():.1f}..{Gdb.max():.1f} dB')
    # front-listener modulation depth per band (horizontal), for the record
    hz = len(el) // 2
    for b in range(nb):
        col = Gdb[:, 0, hz, b]
        print(f'   band {b}: front listener, swing over a revolution {col.max() - col.min():.1f} dB, '
              f'loudest at mouth angle {D["alpha"][np.argmax(col)]:.0f} deg; back of the cabinet {Gdb[:, len(az) // 2, hz, b].mean() - col.mean():+.1f} dB')
    return Gdb


def emit(name, Gdb):
    q = np.clip(np.round(Gdb * 10), -32000, 32000).astype(int).ravel()
    lines = [', '.join(str(v) for v in q[i:i + 24]) for i in range(0, len(q), 24)]
    return f'static const short {name}[{Gdb.shape[0]} * {Gdb.shape[1]} * {Gdb.shape[2]} * {Gdb.shape[3]}] = {{\n    ' + ',\n    '.join(lines) + '\n};\n'


HEAD = """// Wheelhouse - radiation of the Rotary cabinet: horn and bass rotor as heard outside the louvred box, from a
// boundary-element simulation of the 122 cabinet (tools/acoustics/cabinet.py: 3D BEM of the box with the louvred
// faces as apertures fed by the rotor's own BEM pattern, physical optics above 2 kHz) fitted by
// tools/acoustics/fit_cabinet.py - do not edit by hand.
//
// Tables: gains (dB x 10) of the in-phase Linkwitz-Riley bands (same split as RotorDirectivity.h), indexed
// [rotor angle][azimuth][elevation][band]: rotor angle = direction of the mouth / drum opening, azimuth = direction
// of the path, both counter-clockwise from the cabinet front in CAB_STEP degree steps; elevation -90..90 in
// CAB_EL_STEP steps. The box's shadowing and diffraction and the beaming of the louvred faces are in these numbers;
// normalised so that the mean power over all rotor angles and directions per third-octave equals the previous
// model (free-field rotor pattern x hand-made cabinet directivity): the diffuse-field tone stays.
#pragma once
namespace rotary { namespace cabdir {

"""

if __name__ == '__main__':
    D = np.load(NPZ)
    names = list(D['names'])
    print('bands: BEM', list(D['bem_bands']), 'PO', list(D['po_bands']))
    hd = fit_case(D, names.index('horn_deflector'), 'HORN_DEFLECTOR', HORN_EDGES, 800)
    ho = fit_case(D, names.index('horn_open'), 'HORN_OPEN', HORN_EDGES, 800)
    dr = fit_case(D, names.index('drum'), 'DRUM', DRUM_EDGES, 100)
    s = HEAD
    s += f'static const int CAB_NA = {hd.shape[0]}, CAB_NZ = {hd.shape[1]}, CAB_NE = {hd.shape[2]};\n'
    s += f'static const float CAB_STEP = {360.0 / hd.shape[0]:.3f}f, CAB_EL_STEP = {180.0 / (hd.shape[2] - 1):.3f}f;\n\n'
    s += emit('HORN_DEFLECTOR', hd) + emit('HORN_OPEN', ho) + emit('DRUM', dr)
    s += '\n}} // namespace rotary::cabdir\n'
    open(OUT, 'w').write(s)
    print('wrote', os.path.normpath(OUT), f'({os.path.getsize(OUT) // 1024} kB)')
