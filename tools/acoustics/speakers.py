"""Rotary 122 loudspeaker system as an electro-acousto-mechanical network: amp output impedance -> passive 800 Hz
crossover -> Jensen 15" woofer in the vented middle compartment and Jensen V21 compression driver on the horn.
Frequency-domain reference for the real-time model in Source/Speakers.h (same parameters, same equations).

Usage: python3 speakers.py            prints impedance, crossover voltages, radiated power; writes results/speakers.npz

Sources and estimates (no measurements of a 122 were available):
  crossover   2nd order, 16 ohm: woofer series 5.2 mH + shunt 12.5 uF, horn series 7.8 uF + shunt 3.2 mH (values from
              Leslie 145 schematics; 122 / 145 / 147 share the network). Coil resistances estimated.
  V21         16 ohm, DCR ~13 ohm, 2" voice coil, 86 mm phenolic diaphragm, usable ~100 Hz .. 4 kHz (service literature,
              replacement diaphragm data); motor strength, moving mass, chamber volumes estimated for a driver of
              this size and ~108 dB/W/m sensitivity on a horn.
  woofer      Jensen 15" (P15LL / C15 family, 16 ohm). Era-typical: fs ~45-60 Hz, Vas 170-200 l, Qts 0.25-0.4.
  amplifier   6550 push-pull in pentode mode, no global feedback (122): plate resistance ~15 k per tube -> about
              110 ohm at the 16 ohm secondary. The 147 has cathode feedback (lower). In the engine the amp circuit
              itself sees this network; here a Thevenin source stands in.
"""
import os, sys
import numpy as np

RHO, C = 1.18, 343.0
GAMMA, P0 = 1.4, 101325.0

# ---------------- parameters (keep in sync with Source/Speakers.h) ----------------
P = dict(
    # crossover
    L1=5.2e-3, r1=0.55, C1=12.5e-6,             # woofer: series L (+ its DCR), shunt C
    C2=7.8e-6, L3=3.2e-3, r3=0.45,              # horn:   series C, shunt L (+ DCR)
    # woofer (Jensen 15", 16 ohm)
    Rew=13.6, Lew=1.6e-3, L2w=2.2e-3, R2w=9.0,  # voice coil, semi-inductance (L2 || R2)
    Blw=24.0, Mmw=0.072, Kmw=5700.0, Rmw=5.7,   # motor, moving mass (incl. air load), stiffness, mech. loss (Qms 3.6)
    Sdw=0.0855,                                  # cone area (15")
    Vb=0.100, fb=44.0, Qp=4.0,                   # middle compartment volume, port tuning, port Q (leaky compartment)
    aw=0.15,                                     # effective radiating radius (radiation resistance)
    # V21 compression driver (16 ohm)
    Reh=13.0, Leh=0.28e-3, L2h=0.35e-3, R2h=12.0,
    Blh=14.5, Mmh=0.0036, Ksh=7000.0, Rmh=3.0,   # motor, diaphragm + coil mass, suspension stiffness, loss
    Sdh=4.6e-3, Vbc=320e-6, Vfc=2.8e-6,          # diaphragm area, back chamber (large rear cover), front chamber
    rt=0.0105,                                   # throat radius at the driver exit
    # amplifier output impedance at the 16 ohm tap (Thevenin)
    Rs=110.0,
)

# horn path: elbow from the driver into the rotor, then the horn (profile from horn.py)
mm = 1e-3
A0, L, LB, R1, AM = 9.5 * mm, 150 * mm, 25 * mm, 28 * mm, 45 * mm
ELBOW = 0.12


def r_horn(z):
    zc = L - LB
    if z <= zc: return A0 + (R1 - A0) * z / zc
    u = (z - zc) / LB
    slope = (R1 - A0) / zc * LB
    return R1 + slope * u + (AM - R1 - slope) * u * u


def horn_profile(dz):
    """Radius per segment of length dz along the whole path (elbow + horn)."""
    n = int(round((ELBOW + L) / dz)); out = []
    for i in range(n):
        z = (i + 0.5) * dz
        out.append(P['rt'] if z < ELBOW else r_horn(z - ELBOW))
    return np.array(out)


def mouth_reflection(f, a):
    """Mouth load as the radiation resistance rho c / S in parallel with the end-correction mass (delta = 0.61 a):
    R = -1 / (1 + j w 2 delta / c). First order, |R| = 0.63 at ka = 1 (Levine-Schwinger: about 0.5); the real-time
    model uses exactly this (one pole), so both agree."""
    w = 2 * np.pi * f
    return -1 / (1 + 1j * w * 2 * 0.61 * a / C)


def horn_tmatrix(f, dz=2 * mm):
    """Throat input impedance and throat -> mouth (pressure, volume velocity) of the horn path (lossless tubes)."""
    k = 2 * np.pi * f / C
    rs = horn_profile(dz)
    T = np.array([[1, 0], [0, 1]], complex)
    for r in rs:
        S = np.pi * r * r; Z0 = RHO * C / S
        kl = k - 1j * 3.0e-5 * np.sqrt(f) / r          # visco-thermal wall loss (Np/m)
        Ti = np.array([[np.cos(kl * dz), 1j * Z0 * np.sin(kl * dz)], [1j * np.sin(kl * dz) / Z0, np.cos(kl * dz)]])
        T = T @ Ti
    am = rs[-1]; Sm = np.pi * am * am; Zm0 = RHO * C / Sm
    R = mouth_reflection(f, am)
    Zl = Zm0 * (1 + R) / (1 - R)
    # [p_t, U_t] = T [p_m, U_m], p_m = Zl U_m
    pt_per_Um = T[0, 0] * Zl + T[0, 1]; Ut_per_Um = T[1, 0] * Zl + T[1, 1]
    Zt = pt_per_Um / Ut_per_Um
    Um_per_Ut = 1 / Ut_per_Um
    Prad_per_Ut2 = 0.5 * np.real(Zl) * np.abs(Um_per_Ut) ** 2     # W per (m^3/s)^2 peak
    return Zt, Um_per_Ut, Prad_per_Ut2, Zl


def solve(f, Rs=None):
    """Network at one frequency, unit Thevenin EMF (1 V peak) behind Rs. Returns a dict of phasors."""
    p = P; Rs = p['Rs'] if Rs is None else Rs
    w = 2 * np.pi * f; s = 1j * w
    # woofer: electrical (Re + sLe + L2||R2), motional through a mechanical admittance including the vented box
    Ze_w = p['Rew'] + s * p['Lew'] + (s * p['L2w'] * p['R2w']) / (s * p['L2w'] + p['R2w'])
    Cab = p['Vb'] / (RHO * C ** 2); Map = 1 / ((2 * np.pi * p['fb']) ** 2 * Cab); Rap = 2 * np.pi * p['fb'] * Map / p['Qp']
    Zbox = 1 / (s * Cab + 1 / (s * Map + Rap))                  # acoustic impedance behind the cone
    Zm_w = s * p['Mmw'] + p['Rmw'] + p['Kmw'] / s + p['Sdw'] ** 2 * Zbox
    Zw = Ze_w + p['Blw'] ** 2 / Zm_w
    # V21: diaphragm against back chamber (stiffness) and front chamber || throat load
    Zt, Um_per_Ut, Prad_per_Ut2, _ = horn_tmatrix(float(f))
    Cfc = p['Vfc'] / (RHO * C ** 2)
    Zfront = 1 / (s * Cfc + 1 / Zt)                            # acoustic impedance at the diaphragm front
    Kbc = RHO * C ** 2 * p['Sdh'] ** 2 / p['Vbc']
    Zm_h = s * p['Mmh'] + p['Rmh'] + (p['Ksh'] + Kbc) / s + p['Sdh'] ** 2 * Zfront
    Ze_h = p['Reh'] + s * p['Leh'] + (s * p['L2h'] * p['R2h']) / (s * p['L2h'] + p['R2h'])
    Zh = Ze_h + p['Blh'] ** 2 / Zm_h
    # crossover
    Zlow = s * p['L1'] + p['r1'] + 1 / (s * p['C1'] + 1 / Zw)
    Zhigh = 1 / (s * p['C2']) + 1 / (1 / (s * p['L3'] + p['r3']) + 1 / Zh)
    Zin = 1 / (1 / Zlow + 1 / Zhigh)
    V = Zin / (Zin + Rs)
    iw_branch = V / Zlow; vw = V - iw_branch * (s * p['L1'] + p['r1']); iw = vw / Zw
    ih_branch = V / Zhigh; vh = V - ih_branch / (s * p['C2']); ih = vh / Zh
    uw = p['Blw'] * iw / Zm_w; uh = p['Blh'] * ih / Zm_h
    # woofer: front volume velocity + port (port flow out = box pressure / port impedance)
    pbox = -p['Sdw'] * uw * Zbox
    Up = pbox / (s * Map + Rap)
    Uw = p['Sdw'] * uw + Up
    ka = w / C * p['aw']; Rrad_w = RHO * C / (np.pi * p['aw'] ** 2) * ka ** 2 / (2 + ka ** 2)  # half space: rho w^2/(2 pi c) below ka 1
    Pw = 0.5 * Rrad_w * abs(Uw) ** 2
    # horn: throat volume velocity
    pf = p['Sdh'] * uh * Zfront; Ut = pf / Zt
    Ph = Prad_per_Ut2 * abs(Ut) ** 2
    return dict(Zin=Zin, V=V, vw=vw, vh=vh, Zw=Zw, Zh=Zh, uw=uw, uh=uh, Uw=Uw, Ut=Ut, Pw=Pw, Ph=Ph,
                pf=pf, xw=uw / s, xh=uh / s)


THIRDS = np.array([31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                   2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000])


def band(fc, Rs, key, n=15):
    fs = fc * 2 ** (np.linspace(-1, 1, n) / 6)
    return np.mean([solve(f, Rs)[key] for f in fs])


if __name__ == '__main__':
    print('third-octave radiated power, dB re 1 W per V^2 of EMF; columns: Rs = 0 (voltage drive), 20 ohm (147), 110 ohm (122)')
    print('  f      woofer: V    147   122  |   horn: V    147   122  |  |Zin| (V drive)')
    out = []
    for f in THIRDS:
        r = []
        for key in ('Pw', 'Ph'):
            for Rs in (0.01, 20, 110): r.append(10 * np.log10(band(f, Rs, key) + 1e-30))
        z = abs(solve(f, 0.01)['Zin'])
        out.append([f] + r + [z])
        print('%6.0f  %7.1f %6.1f %6.1f  | %7.1f %6.1f %6.1f  |  %5.1f' % tuple([f] + r + [z]))
    np.savez(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'results', 'speakers.npz'), rows=np.array(out))
    for f in (100, 400, 2000):
        a = solve(f, Rs=0.01)
        Pin = 0.5 * np.real(a['V'] * np.conj(a['V'] / a['Zin']))
        print(f'{f} Hz: efficiency {(a["Pw"] + a["Ph"]) / Pin * 100:.2f} %  (woofer {a["Pw"] / Pin * 100:.2f}, horn {a["Ph"] / Pin * 100:.2f}),'
              f' excursion at 40 W: woofer {abs(a["xw"]) * np.sqrt(80 / (2*Pin)) * 1e3:.2f} mm, V21 {abs(a["xh"]) * np.sqrt(80 / (2*Pin)) * 1e3:.3f} mm')
