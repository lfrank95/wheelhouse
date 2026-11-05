// Ray tracing for non-rectangular rooms (runs in a Web Worker).
// Geometry: oval tent (stadium plan: two half circles + straight middle section), cone/ridge roof over a
// row of masts with a kink at the intermediate poles, round wall, ring (sand) and a tier of seating with audience.
// Result: taps {t (s), az (rad, room coordinates), el (-1..1), dep (departure direction at the Rotary), order, E[3 bands]}.

function makeTent(g) {
  const R = g.W / 2, Ls = g.Ls, hw = g.hw, hm = g.hm, hq = g.hq, rq = g.rq * R;
  const cx = g.cx, cy = g.cy; // tent centre in room coordinates
  // In-plane distance to the mast line (segment from -Ls/2..Ls/2 along x)
  const planD = (x, y) => { const dx = Math.max(Math.abs(x - cx) - Ls / 2, 0); return Math.hypot(dx, y - cy); };
  // Crown ring: vertical band of radius rK between hCrown and hCrown+hb; above it the cone cap up to hTip,
  // below/outside the main roof as a cone from the round wall (hw) to the lower edge of the crown (hCrown), slightly sagging.
  const rK = g.rK, hCrown = g.hCrown, hb = g.hb, hTip = g.hTip, sag = g.sag === undefined ? 1.2 : g.sag;
  const roofH = (x, y) => {
    const d = planD(x, y);
    if (d < rK) return hCrown + hb + (hTip - hCrown - hb) * (1 - d / rK);
    if (d >= R) return hw;
    return hw + (hCrown - hw) * Math.pow((R - d) / (R - rK), sag);
  };
  const ringD = (x, y) => Math.hypot(x - cx, y - cy); // ring / tiers around the tent centre
  const floorH = (x, y) => {
    const d = ringD(x, y);
    if (d < g.rRing) return 0;
    if (d < g.rT1) return 0.35;
    if (d < g.rT2) return 0.5 + (d - g.rT1) / (g.rT2 - g.rT1) * g.hT;
    return g.hT + 0.5;
  };
  const floorMat = (x, y) => { const d = ringD(x, y); return d < g.rRing ? 'sand' : d < g.rT1 ? 'piste' : d < g.rT2 ? 'tiers' : 'floor'; };
  return { planD, roofH, floorH, floorMat, R, cx, cy };
}

const MATS = {
  // Energy reflection low / mid / high (the rest passes through the canvas or is absorbed)
  canvas: [0.12, 0.45, 0.68],
  sand: [0.55, 0.45, 0.3],
  piste: [0.8, 0.8, 0.7],
  floor: [0.85, 0.85, 0.8],
  // Ground surfaces the tent stands on
  grass: [0.75, 0.5, 0.25],
  gravel: [0.8, 0.6, 0.45],
  sandlot: [0.6, 0.45, 0.3],
  asphalt: [0.93, 0.9, 0.85],
  concrete: [0.95, 0.93, 0.9],
  paving: [0.9, 0.85, 0.75],
  woodfloor: [0.85, 0.8, 0.7],
  snow: [0.5, 0.25, 0.1],
  tribuene_leer: [0.7, 0.6, 0.5],
  tribuene_voll: [0.3, 0.15, 0.08],
};

function trace(cfg) {
  const T = makeTent(cfg.geo);
  const g = cfg.geo;
  const src = cfg.src, rec = cfg.rec, rr = cfg.recRadius || 1.0, rr2 = rr * rr;
  const N = cfg.rays || 30000, maxB = cfg.maxBounces || 6, maxLen = cfg.maxLen || 180, step = 0.25;
  const scat = cfg.scatter || 0.15;
  const audience = cfg.audience; // 0..1
  const trib = MATS.tribuene_leer.map((v, i) => v + (MATS.tribuene_voll[i] - v) * audience);
  const ground = MATS[cfg.ground] || MATS.grass;
  const matOf = m => m === 'tiers' ? trib : m === 'floor' ? ground : m === 'sand' ? (cfg.ringFloor === 'ground' ? ground : MATS.sand) : MATS[m] || MATS.floor;
  let seed = 12345; const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
  const hits = []; let directE = 0, directN = 0;
  const C = 343;

  for (let i = 0; i < N; i++) {
    // Fibonacci sphere
    const k = i + 0.5, phi = Math.acos(1 - 2 * k / N), th = Math.PI * (1 + Math.sqrt(5)) * k;
    let dx = Math.cos(th) * Math.sin(phi), dy = Math.sin(th) * Math.sin(phi), dz = Math.cos(phi);
    const dep = [dx, dy, dz];
    let x = src.x, y = src.y, z = src.z;
    let E = [1, 1, 1], len = 0, order = 0, alive = true;
    let lastHit = -1;
    while (alive && len < maxLen && order <= maxB) {
      // Test the receiver sphere on the next segment (analytically up to the next surface)
      // 1) find the next surface by ray marching
      let tHit = -1, kind = null;
      let px = x, py = y, pz = z;
      let traveled = 0;
      for (let s = 0; s < 2000; s++) {
        const nx = x + dx * (traveled + step), ny = y + dy * (traveled + step), nz = z + dz * (traveled + step);
        let outside = null;
        if (T.planD(nx, ny) > T.R) outside = 'wall';
        else if (nz > T.roofH(nx, ny)) outside = 'roof';
        else if (nz < T.floorH(nx, ny)) outside = 'floor';
        if (outside) {
          // Bisection between traveled and traveled+step
          let a = traveled, b = traveled + step;
          for (let it = 0; it < 12; it++) {
            const m = (a + b) / 2, mx = x + dx * m, my = y + dy * m, mz = z + dz * m;
            const out = T.planD(mx, my) > T.R || mz > T.roofH(mx, my) || mz < T.floorH(mx, my);
            if (out) b = m; else a = m;
          }
          tHit = a; kind = outside; break;
        }
        traveled += step;
        if (len + traveled > maxLen) break;
      }
      if (tHit < 0) break;
      // 2) receiver on the segment [0, tHit]?
      const ox = x - rec.x, oy = y - rec.y, oz = z - rec.z;
      const bq = ox * dx + oy * dy + oz * dz;
      const cq = ox * ox + oy * oy + oz * oz - rr2;
      const disc = bq * bq - cq;
      if (disc > 0) {
        const tr = -bq - Math.sqrt(disc);
        if (tr > 0 && tr < tHit) {
          const tt = (len + tr) / C;
          if (order === 0) { directE += 1; directN++; }
          else {
            const ax = -dx, ay = -dy, az = -dz; // arrival direction: where the sound comes from
            hits.push({ t: tt, az: Math.atan2(ay, ax), el: az, dep, order, E: E.slice() });
          }
        }
      }
      // 3) reflection
      const hx = x + dx * tHit, hy = y + dy * tHit, hz = z + dz * tHit;
      len += tHit;
      let nx, ny, nz, mat;
      if (kind === 'wall') {
        // Normal of the stadium shape: outward from the nearest point on the mast line
        const ddx = Math.max(Math.abs(hx - T.cx) - g.Ls / 2, 0) * Math.sign(hx - T.cx), ddy = hy - T.cy;
        const l = Math.hypot(ddx, ddy) || 1; nx = -ddx / l; ny = -ddy / l; nz = 0; mat = MATS.canvas;
      } else if (kind === 'roof') {
        const e = 0.05;
        const gx = (T.roofH(hx + e, hy) - T.roofH(hx - e, hy)) / (2 * e), gy = (T.roofH(hx, hy + e) - T.roofH(hx, hy - e)) / (2 * e);
        const l = Math.hypot(gx, gy, 1); nx = gx / l; ny = gy / l; nz = -1 / l; mat = MATS.canvas;
      } else {
        const e = 0.05;
        const gx = (T.floorH(hx + e, hy) - T.floorH(hx - e, hy)) / (2 * e), gy = (T.floorH(hx, hy + e) - T.floorH(hx, hy - e)) / (2 * e);
        const l = Math.hypot(gx, gy, 1); nx = -gx / l; ny = -gy / l; nz = 1 / l; mat = matOf(T.floorMat(hx, hy));
      }
      const dot = dx * nx + dy * ny + dz * nz;
      dx -= 2 * dot * nx; dy -= 2 * dot * ny; dz -= 2 * dot * nz;
      // Scattering (folds in the canvas, steps of the tiers)
      const sc = kind === 'floor' && mat === trib ? scat * 2 : scat;
      dx += (rnd() - 0.5) * sc; dy += (rnd() - 0.5) * sc; dz += (rnd() - 0.5) * sc;
      const l2 = Math.hypot(dx, dy, dz); dx /= l2; dy /= l2; dz /= l2;
      if (dx * nx + dy * ny + dz * nz < 0.02) { dx += nx * 0.1; dy += ny * 0.1; dz += nz * 0.1; const l3 = Math.hypot(dx, dy, dz); dx /= l3; dy /= l3; dz /= l3; }
      E = [E[0] * mat[0], E[1] * mat[1], E[2] * mat[2]];
      x = hx + nx * 0.01; y = hy + ny * 0.01; z = hz + nz * 0.01;
      order++;
      if (E[1] < 1e-5 && E[2] < 1e-5 && E[0] < 1e-5) alive = false;
    }
  }
  // Calibration: direct-sound hits correspond to 1/d²
  const dd = Math.hypot(src.x - rec.x, src.y - rec.y, src.z - rec.z);
  const calib = directN > 0 ? (1 / (dd * dd)) / directE : (Math.PI * rr2) / (4 * Math.PI * N) * 0 + (1 / N) * (4 / (Math.PI * rr2)) * 0.25;
  // Bundle hits into taps: 1 ms grid, 15° direction, above/below, order
  const bins = new Map();
  for (const h of hits) {
    const key = `${Math.round(h.t * 1000)}|${Math.round(h.az / (Math.PI / 12))}|${h.el > 0.3 ? 1 : h.el < -0.3 ? -1 : 0}|${Math.min(h.order, 4)}`;
    let b = bins.get(key);
    if (!b) { b = { t: 0, n: 0, E: [0, 0, 0], ax: 0, ay: 0, el: 0, dx: 0, dy: 0, dz: 0, order: h.order }; bins.set(key, b); }
    const w = h.E[1] + 1e-9;
    b.t += h.t * w; b.n += w; b.E[0] += h.E[0]; b.E[1] += h.E[1]; b.E[2] += h.E[2];
    b.ax += Math.cos(h.az) * w; b.ay += Math.sin(h.az) * w; b.el += h.el * w;
    b.dx += h.dep[0] * w; b.dy += h.dep[1] * w; b.dz += h.dep[2] * w;
  }
  const taps = [];
  for (const b of bins.values()) {
    const dl = Math.hypot(b.dx, b.dy, b.dz) || 1;
    taps.push({ t: b.t / b.n, az: Math.atan2(b.ay, b.ax), el: b.el / b.n, dep: [b.dx / dl, b.dy / dl, b.dz / dl], order: b.order, E: b.E.map(e => e * calib) });
  }
  taps.sort((a, b) => a.t - b.t);
  // Energy over time (for display / Sabine substitute)
  let Etot = taps.reduce((s, t) => s + t.E[1], 0);
  return { taps, directN, hits: hits.length, Etot, dd };
}

self.onmessage = (e) => {
  const t0 = performance.now();
  const res = trace(e.data);
  res.ms = performance.now() - t0; res.id = e.data.id;
  self.postMessage(res);
};
