// Wheelhouse UI - Room presets, circus tent, positions and the floor plan.
// All files in ui/js run in one shared scope, in the order listed in tools/build_ui.py.

// ---------------- Room presets ----------------
function matchRoom() {
  if (S.room.shape === 'tent') return ROOMS.findIndex(r => r.tent);
  const i = ROOMS.findIndex(
    r =>
      !r.tent &&
      r.Lx === S.room.Lx &&
      r.Ly === S.room.Ly &&
      r.Lz === S.room.Lz &&
      r.absorb === S.room.absorb &&
      r.material === S.room.material &&
      r.reverb === S.room.reverb
  );
  return i >= 0 ? i : null;
}
function loadRoom(i) {
  const r = ROOMS[i];
  if (!r) return;
  if (r.tent) {
    setShape('tent');
    return;
  }
  if (S.room.shape !== 'box') {
    S.room.shape = 'box';
    send({ type: 'room', v: { shape: 'box', absBands: null } });
  }
  const fx = S.rotaryPos.x / S.room.Lx,
    fy = S.rotaryPos.y / S.room.Ly,
    gx = S.listener.x / S.room.Lx;
  Object.assign(S.room, { Lx: r.Lx, Ly: r.Ly, Lz: r.Lz, absorb: r.absorb, reverb: r.reverb, material: r.material });
  S.rotaryPos.x = fx * r.Lx;
  S.rotaryPos.y = Math.min(r.Ly - 1, fy * r.Ly);
  S.listener.x = gx * r.Lx;
  S.listener.y = Math.max(0.6, S.rotaryPos.y - Math.min(4 + r.Ly * 0.15, r.Ly * 0.6));
  clampPos();
  send({ type: 'room', v: S.room });
  sendPos(true);
  if (S.mics.placed) {
    S.mics.roomX = clamp(S.mics.roomX, 0.5, r.Lx - 0.5);
    S.mics.roomY = clamp(S.mics.roomY, 0.5, r.Ly - 0.5);
    sendMics();
  }
  syncAll();
}
const quick = ROOMS.map((r, i) => [r, i]).filter(([r]) => !r.noQuick);
const preBtns = quick.map(([r, i]) => {
  const b = h('button', '', r.s);
  b.type = 'button';
  b.dataset.tip = r.n + ' – load this room preset.';
  b.onclick = () => loadRoom(i);
  $('roomPre').appendChild(b);
  return [i, b];
});
reg({
  sync() {
    const m = matchRoom();
    preBtns.forEach(([i, b]) => b.classList.toggle('on', i === m));
  },
});

// ---------------- Circus tent (geometry, absorption, ray tracer worker) ----------------
let tracer = null,
  traceTimer = null,
  traceId = 0;
function tentGeo() {
  const T = S.tent,
    R = T.W / 2,
    rK = Math.min(R - 1, T.dome / 2);
  return {
    W: T.W,
    Ls: T.Ls,
    hw: T.hw,
    hCrown: T.hm,
    hb: T.hb,
    rK,
    hTip: T.hm + T.hb + T.tip,
    sag: 1 + T.sagPct * 1.6,
    cx: S.room.Lx / 2,
    cy: S.room.Ly / 2,
    rRing: T.ring / 2,
    rT1: T.ring / 2 + 1,
    rT2: Math.min(R - 0.5, T.ring / 2 + 1 + Math.max(3, R - T.ring / 2 - 2.5)),
    hT: 2.8,
  };
}
function tentPlanD(x, y) {
  const g = tentGeo();
  const dx = Math.max(Math.abs(x - g.cx) - g.Ls / 2, 0);
  return Math.hypot(dx, y - g.cy);
}
function setShape(shape) {
  if (shape === S.room.shape) {
    syncAll();
    return;
  }
  S.room.shape = shape;
  if (shape === 'tent') applyTent(true);
  else {
    const r = ROOMS[1];
    Object.assign(S.room, { Lx: r.Lx, Ly: r.Ly, Lz: r.Lz, absorb: r.absorb, reverb: r.reverb, material: r.material });
    S.rotaryPos = { x: 4.2, y: 6.3 };
    S.listener.x = 2.6;
    S.listener.y = 2.4;
    clampPos();
    send({ type: 'room', v: Object.assign({}, S.room, { absBands: null }) });
    sendPos(true);
  }
  syncAll();
}
function applyTent(reposition) {
  const T = S.tent,
    R = T.W / 2;
  S.room.Lx = T.W + T.Ls;
  S.room.Ly = T.W;
  S.room.Lz = T.hm;
  S.room.material = 'tent';
  S.room.absorb = 0.5;
  const area = Math.PI * R * R + T.W * T.Ls,
    perim = Math.PI * T.W + 2 * T.Ls,
    V = area * (T.hw + (T.hm - T.hw) * 0.42);
  const g = tentGeo();
  const GROUND = {
    grass: [0.75, 0.5, 0.25],
    gravel: [0.8, 0.6, 0.45],
    sandlot: [0.6, 0.45, 0.3],
    asphalt: [0.93, 0.9, 0.85],
    concrete: [0.95, 0.93, 0.9],
    paving: [0.9, 0.85, 0.75],
    woodfloor: [0.85, 0.8, 0.7],
    snow: [0.5, 0.25, 0.1],
  };
  const refl = {
    canvas: [0.12, 0.45, 0.68],
    sand: [0.55, 0.45, 0.3],
    trib: [0.7, 0.6, 0.5].map((v, i) => v + ([0.3, 0.15, 0.08][i] - v) * T.audience),
    ground: GROUND[T.ground] || GROUND.grass,
  };
  const aMan = Math.PI * g.rRing * g.rRing,
    aTrib = Math.PI * (g.rT2 * g.rT2 - g.rT1 * g.rT1),
    aGround = Math.max(0, area - aMan - aTrib),
    aPlane = area * 1.3 + perim * T.hw;
  const manRefl = T.ringFloor === 'ground' ? refl.ground : refl.sand;
  const absB = [0, 1, 2].map(
    i =>
      (aPlane * (1 - refl.canvas[i]) +
        aMan * (1 - manRefl[i]) +
        aTrib * (1 - refl.trib[i]) +
        aGround * (1 - refl.ground[i])) /
      (aPlane + aMan + aTrib + aGround)
  );
  if (reposition) {
    S.rotaryPos.x = S.room.Lx / 2;
    S.rotaryPos.y = S.room.Ly / 2 + 2;
    S.listener.x = S.room.Lx / 2;
    S.listener.y = S.room.Ly / 2 - T.ring / 2 - 2.5;
    S.listener.yaw = Math.PI / 2;
  }
  clampPos();
  send({
    type: 'room',
    v: {
      shape: 'tent',
      Lx: S.room.Lx,
      Ly: S.room.Ly,
      Lz: S.room.Lz,
      material: 'tent',
      absorb: 0.5,
      tentVolume: V,
      tentSurface: aPlane + aMan + aTrib + aGround,
      absBands: absB,
    },
  });
  sendPos(!!reposition);
  scheduleTrace();
}
function scheduleTrace() {
  if (S.room.shape !== 'tent') return;
  clearTimeout(traceTimer);
  traceTimer = setTimeout(runTrace, 180);
}
function runTrace() {
  if (!tracer) {
    const url = URL.createObjectURL(new Blob([$('tracer-src').textContent], { type: 'application/javascript' }));
    tracer = new Worker(url);
    tracer.onmessage = e => {
      const r = e.data;
      if (r.id !== traceId) return;
      send({ type: 'taps', taps: r.taps });
      tInfoText = `${r.taps.length} paths · ${r.ms.toFixed(0)} ms`;
      syncAll();
    };
  }
  traceId++;
  const caps = { 1: 10000, 2: 14000, 3: 18000, 4: 22000, 5: 28000 };
  tracer.postMessage({
    id: traceId,
    geo: tentGeo(),
    src: { x: S.rotaryPos.x, y: S.rotaryPos.y, z: 0.8 },
    rec: { x: S.listener.x, y: S.listener.y, z: 1.2 },
    rays: caps[S.room.order] || 14000,
    maxBounces: 2 + S.room.order,
    audience: S.tent.audience,
    scatter: 0.15,
    ground: S.tent.ground,
    ringFloor: S.tent.ringFloor,
  });
  tInfoText = 'tracing …';
}

// ---------------- Position and viewing direction ----------------
function yawDeg() {
  let d = Math.round(((S.listener.yaw - Math.PI / 2) * 180) / Math.PI);
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return d;
}
function setYaw(rad, jump) {
  S.listener.yaw = rad;
  sendPos(jump);
  syncAll();
}
function clampPos() {
  const R = S.room,
    c = (v, hi) => clamp(v, 0.35, hi - 0.35);
  S.rotaryPos.x = c(S.rotaryPos.x, R.Lx);
  S.rotaryPos.y = c(S.rotaryPos.y, R.Ly);
  S.listener.x = c(S.listener.x, R.Lx);
  S.listener.y = c(S.listener.y, R.Ly);
  if (R.shape === 'tent') {
    const g = tentGeo();
    for (const o of [S.rotaryPos, S.listener]) {
      const d = tentPlanD(o.x, o.y),
        lim = g.W / 2 - 0.6;
      if (d > lim) {
        const ax = clamp(o.x - g.cx, -g.Ls / 2, g.Ls / 2) + g.cx,
          ay = g.cy;
        o.x = ax + ((o.x - ax) * lim) / d;
        o.y = ay + ((o.y - ay) * lim) / d;
      }
    }
  }
}
let lastTracePos = '';
function sendPos(jump) {
  send({ type: 'pos', rotaryPos: S.rotaryPos, listener: S.listener, jump: !!jump });
  if (S.room.shape !== 'tent') return;
  const key = [S.rotaryPos.x, S.rotaryPos.y, S.listener.x, S.listener.y].map(v => v.toFixed(3)).join(); // the trace does not depend on the viewing direction
  if (key !== lastTracePos) {
    lastTracePos = key;
    scheduleTrace();
  }
}

// ---------------- Floor plan canvas (small on the play surface, large in the Room card) ----------------
function RoomView(cv, big) {
  const g = cv.getContext('2d');
  const view = { s: 1, ox: 0, oy: 0 };
  let W = 0,
    H = 0;
  const self = {
    resize() {
      W = cv.clientWidth;
      H = cv.clientHeight;
      const k = SCALE * (window.devicePixelRatio || 1);
      cv.width = Math.max(1, Math.round(W * k));
      cv.height = Math.max(1, Math.round(H * k));
      g.setTransform(k, 0, 0, k, 0, 0);
    },
    draw,
    view,
  };
  const W2C = (x, y) => [view.ox + x * view.s, view.oy + (S.room.Ly - y) * view.s];
  const C2W = (px, py) => [(px - view.ox) / view.s, S.room.Ly - (py - view.oy) / view.s];
  const local = e => {
    const r = cv.getBoundingClientRect();
    return [(e.clientX - r.left) / SCALE, (e.clientY - r.top) / SCALE];
  };
  let drag = null;
  cv.addEventListener('contextmenu', e => e.preventDefault());
  cv.addEventListener('pointerdown', e => {
    const [px, py] = local(e),
      [lx, ly] = W2C(S.rotaryPos.x, S.rotaryPos.y),
      [hx, hy] = W2C(S.listener.x, S.listener.y);
    const dl = Math.hypot(px - lx, py - ly),
      dh = Math.hypot(px - hx, py - hy);
    const [mx, my] = W2C(S.mics.roomX, S.mics.roomY),
      dm = S.mics.on ? Math.hypot(px - mx, py - my) : 1e9;
    if (e.button === 2) drag = dm < 18 ? 'pairRot' : 'rot';
    else if (dm < 16 && dm <= dh && dm <= dl) drag = 'pair';
    else if (dh < 24 && dh <= dl) drag = 'listener';
    else if (dl < 30) drag = 'rotaryPos';
    else drag = null;
    if (drag) cv.setPointerCapture(e.pointerId);
  });
  cv.addEventListener('pointermove', e => {
    const [px, py] = local(e);
    if (!drag) {
      const [lx, ly] = W2C(S.rotaryPos.x, S.rotaryPos.y),
        [hx, hy] = W2C(S.listener.x, S.listener.y);
      cv.style.cursor = Math.hypot(px - lx, py - ly) < 30 || Math.hypot(px - hx, py - hy) < 24 ? 'grab' : 'default';
      return;
    }
    const [wx, wy] = C2W(px, py);
    if (drag === 'rot') setYaw(Math.atan2(wy - S.listener.y, wx - S.listener.x));
    else if (drag === 'pair') {
      S.mics.roomX = clamp(wx, 0.3, S.room.Lx - 0.3);
      S.mics.roomY = clamp(wy, 0.3, S.room.Ly - 0.3);
      S.mics.placed = true;
      sendMics();
    } else if (drag === 'pairRot') {
      S.mics.roomYaw = Math.atan2(wy - S.mics.roomY, wx - S.mics.roomX);
      sendMics();
    } else {
      const o = drag === 'rotaryPos' ? S.rotaryPos : S.listener;
      o.x = wx;
      o.y = wy;
      clampPos();
      sendPos();
    }
  });
  const up = () => {
    if (drag) syncAll();
    drag = null;
  };
  cv.addEventListener('pointerup', up);
  cv.addEventListener('pointercancel', up);
  cv.addEventListener(
    'wheel',
    e => {
      e.preventDefault();
      const [px, py] = local(e),
        [lx, ly] = W2C(S.rotaryPos.x, S.rotaryPos.y),
        d = (Math.sign(e.deltaY) * 7.5 * Math.PI) / 180;
      const [mx, my] = W2C(S.mics.roomX, S.mics.roomY);
      if (S.mics.on && Math.hypot(px - mx, py - my) < 22) {
        S.mics.roomYaw -= d;
        sendMics();
        syncAll();
      } else if (Math.hypot(px - lx, py - ly) < 34) setCabYaw(S.cabYaw - d);
      else setYaw(S.listener.yaw - d);
    },
    { passive: false }
  );

  function drawTentSide(gt) {
    const bw = 240,
      bh = 130,
      bx = W - bw - 10,
      by = (H - bh) / 2;
    g.fillStyle = 'rgba(14,12,10,.9)';
    g.strokeStyle = '#2e2620';
    g.lineWidth = 1;
    g.beginPath();
    g.roundRect(bx, by, bw, bh, 6);
    g.fill();
    g.stroke();
    const R = gt.W / 2,
      mastTop = gt.hCrown + gt.hb + 1.5,
      top = Math.max(gt.hTip, mastTop),
      sc = Math.min((bw - 24) / gt.W, (bh - 32) / top);
    const X = x => bx + bw / 2 + x * sc,
      Y = hh => by + bh - 12 - hh * sc;
    const profile = dir => {
      const pts = [
        [0, gt.hTip],
        [gt.rK, gt.hCrown + gt.hb],
        [gt.rK, gt.hCrown],
      ];
      for (let i = 1; i <= 6; i++) {
        const d = gt.rK + ((R - gt.rK) * i) / 6;
        pts.push([d, gt.hw + (gt.hCrown - gt.hw) * Math.pow((R - d) / (R - gt.rK), gt.sag)]);
      }
      pts.push([R, 0]);
      return pts.map(([d, hh]) => [X(dir * d), Y(hh)]);
    };
    const pts = [...profile(-1).reverse(), ...profile(1)];
    g.fillStyle = 'rgba(138,90,54,.25)';
    g.beginPath();
    pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.closePath();
    g.fill();
    g.strokeStyle = '#d9a86c';
    g.lineWidth = 1.5;
    g.beginPath();
    pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y)));
    g.stroke();
    g.strokeStyle = '#f2c879';
    g.lineWidth = 2.5;
    for (const dir of [-1, 1]) {
      g.beginPath();
      g.moveTo(X(dir * gt.rK), Y(gt.hCrown));
      g.lineTo(X(dir * gt.rK), Y(gt.hCrown + gt.hb));
      g.stroke();
    }
    g.strokeStyle = '#6b5a40';
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(X(-R), Y(0));
    g.lineTo(X(R), Y(0));
    g.stroke();
    g.fillStyle = 'rgba(120,95,70,.6)';
    for (const dir of [-1, 1]) {
      g.beginPath();
      g.moveTo(X(dir * gt.rT1), Y(0));
      g.lineTo(X(dir * gt.rT1), Y(0.5));
      g.lineTo(X(dir * gt.rT2), Y(0.5 + gt.hT));
      g.lineTo(X(dir * gt.rT2), Y(0));
      g.closePath();
      g.fill();
    }
    g.fillStyle = '#5a4a2a';
    g.fillRect(X(-gt.rRing), Y(0) - 2, 2 * gt.rRing * sc, 2);
    g.strokeStyle = '#e8dcc4';
    g.lineWidth = 2;
    const md = (S.tent.mast / 2) * Math.SQRT2;
    for (const dir of [-1, 1]) {
      g.beginPath();
      g.moveTo(X(dir * md), Y(0));
      g.lineTo(X(dir * md), Y(mastTop));
      g.stroke();
    }
    const dl = Math.hypot(S.rotaryPos.x - gt.cx, S.rotaryPos.y - gt.cy),
      dh = Math.hypot(S.listener.x - gt.cx, S.listener.y - gt.cy);
    g.fillStyle = '#f2c879';
    g.fillRect(X(-dl) - 3, Y(1.1), 6, 1.1 * sc);
    g.fillStyle = '#efe7d8';
    g.beginPath();
    g.arc(X(dh), Y(1.5), 3, 0, Math.PI * 2);
    g.fill();
    g.fillStyle = '#8f8370';
    g.font = '11px "Barlow Condensed", sans-serif';
    g.fillText(`Section · crown ${nf(gt.hCrown)} m · peak ${nf(gt.hTip)} m`, bx + 8, by + 14);
  }

  function draw(hornA, drumA) {
    if (!W || !H) return;
    const R = S.room,
      padT = big ? 30 : 12,
      padB = big ? 14 : 24,
      padX = big ? 18 : 14,
      side = big && R.shape === 'tent' ? 250 : 0;
    view.s = Math.min((W - side - 2 * padX) / R.Lx, (H - padT - padB) / R.Ly);
    view.ox = (W - side - R.Lx * view.s) / 2;
    view.oy = padT + (H - padT - padB - R.Ly * view.s) / 2;
    g.clearRect(0, 0, W, H);
    const [x0, y0] = W2C(0, R.Ly),
      [x1, y1] = W2C(R.Lx, 0);
    const step = R.Lx > 15 || R.Ly > 20 ? 5 : 1;
    const grid = () => {
      g.strokeStyle = '#17221d';
      g.lineWidth = 1;
      g.beginPath();
      for (let x = step; x < R.Lx; x += step) {
        const [a] = W2C(x, 0);
        g.moveTo(a, y0);
        g.lineTo(a, y1);
      }
      for (let y = step; y < R.Ly; y += step) {
        const [, b] = W2C(0, y);
        g.moveTo(x0, b);
        g.lineTo(x1, b);
      }
      g.stroke();
    };
    if (R.shape === 'tent') {
      const gt = tentGeo(),
        rr = (gt.W / 2) * view.s,
        [cx, cy] = W2C(gt.cx, gt.cy),
        hl = (gt.Ls / 2) * view.s;
      const stadium = rad => {
        g.beginPath();
        g.moveTo(cx - hl, cy - rad);
        g.lineTo(cx + hl, cy - rad);
        g.arc(cx + hl, cy, rad, -Math.PI / 2, Math.PI / 2);
        g.lineTo(cx - hl, cy + rad);
        g.arc(cx - hl, cy, rad, Math.PI / 2, Math.PI * 1.5);
        g.closePath();
      };
      g.save();
      stadium(rr);
      g.clip();
      grid();
      g.restore();
      g.strokeStyle = '#8a5a36';
      g.lineWidth = 2.5;
      stadium(rr);
      g.stroke();
      g.fillStyle = `rgba(120,95,70,${0.18 + 0.3 * S.tent.audience})`;
      g.beginPath();
      g.arc(cx, cy, gt.rT2 * view.s, 0, Math.PI * 2);
      g.arc(cx, cy, gt.rT1 * view.s, 0, Math.PI * 2, true);
      g.fill();
      g.fillStyle = '#5a4a2a';
      g.beginPath();
      g.arc(cx, cy, gt.rRing * view.s, 0, Math.PI * 2);
      g.fill();
      g.strokeStyle = '#a0866a';
      g.lineWidth = 1.5;
      g.stroke();
      const hm2 = (S.tent.mast / 2) * view.s;
      g.setLineDash([3, 4]);
      g.strokeStyle = '#6a5a44';
      g.lineWidth = 1;
      g.strokeRect(cx - hm2, cy - hm2, 2 * hm2, 2 * hm2);
      g.setLineDash([]);
      g.fillStyle = '#e8dcc4';
      g.strokeStyle = '#8a5a36';
      g.lineWidth = 1.5;
      for (const [sx, sy] of [
        [-1, -1],
        [1, -1],
        [-1, 1],
        [1, 1],
      ]) {
        g.beginPath();
        g.arc(cx + sx * hm2, cy + sy * hm2, big ? 5 : 3.5, 0, Math.PI * 2);
        g.fill();
        g.stroke();
      }
      g.strokeStyle = '#8a7a60';
      g.lineWidth = 1.5;
      g.beginPath();
      g.arc(cx, cy, gt.rK * view.s, 0, Math.PI * 2);
      g.stroke();
      if (big) drawTentSide(gt);
    } else {
      grid();
      g.strokeStyle = '#6b5a40';
      g.lineWidth = 2.5;
      g.strokeRect(x0, y0, x1 - x0, y1 - y0);
      // Sound paths: direct + 1st order, weighted by horn radiation
      const L = S.rotaryPos,
        Hh = S.listener;
      const imgs = [
        [L.x, L.y],
        [-L.x, L.y],
        [2 * R.Lx - L.x, L.y],
        [L.x, -L.y],
        [L.x, 2 * R.Ly - L.y],
      ];
      g.lineCap = 'round';
      imgs.forEach(([ix, iy], i) => {
        const sx = i === 1 || i === 2 ? -1 : 1,
          sy = i === 3 || i === 4 ? -1 : 1;
        const dx = sx * Math.cos(hornA),
          dy = sy * Math.sin(hornA),
          vx = Hh.x - ix,
          vy = Hh.y - iy,
          d = Math.hypot(vx, vy) || 1;
        const u = 0.5 + (0.5 * (dx * vx + dy * vy)) / d,
          w = 0.08 + 0.92 * u * u;
        let pts = [
          [L.x, L.y],
          [Hh.x, Hh.y],
        ];
        if (i > 0) {
          let t;
          if (i === 1) t = (0 - ix) / (Hh.x - ix);
          else if (i === 2) t = (R.Lx - ix) / (Hh.x - ix);
          else if (i === 3) t = (0 - iy) / (Hh.y - iy);
          else t = (R.Ly - iy) / (Hh.y - iy);
          pts = [
            [L.x, L.y],
            [ix + vx * t, iy + vy * t],
            [Hh.x, Hh.y],
          ];
        }
        g.strokeStyle = `rgba(242,163,58,${(i === 0 ? 0.25 : 0.08) + w * (i === 0 ? 0.6 : 0.4)})`;
        g.lineWidth = i === 0 ? 2.2 : 1.3;
        g.beginPath();
        pts.forEach(([x, y], j) => {
          const [a, b] = W2C(x, y);
          j ? g.lineTo(a, b) : g.moveTo(a, b);
        });
        g.stroke();
      });
    }
    if (R.shape === 'tent') {
      const [a1, b1] = W2C(S.rotaryPos.x, S.rotaryPos.y),
        [a2, b2] = W2C(S.listener.x, S.listener.y);
      g.strokeStyle = 'rgba(242,163,58,.6)';
      g.lineWidth = 2;
      g.beginPath();
      g.moveTo(a1, b1);
      g.lineTo(a2, b2);
      g.stroke();
    }
    // Rotary cabinet
    const [lx, ly] = W2C(S.rotaryPos.x, S.rotaryPos.y),
      cab = clamp(0.74 * view.s, big ? 26 : 22, 60);
    g.save();
    g.translate(lx, ly);
    g.rotate(-S.cabYaw + Math.PI / 2);
    const cw = cab,
      cd = cab * 0.72;
    g.fillStyle = '#4a2e1c';
    g.strokeStyle = '#a8743f';
    g.lineWidth = 1.5;
    g.beginPath();
    g.roundRect(-cw / 2, -cd / 2, cw, cd, 3);
    g.fill();
    g.stroke();
    g.strokeStyle = '#d9a86c';
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(-cw / 2 + 2, -cd / 2);
    g.lineTo(cw / 2 - 2, -cd / 2);
    g.stroke();
    g.strokeStyle = '#1a0e06';
    g.lineWidth = 3;
    g.beginPath();
    g.moveTo(-cw / 2, cd / 2);
    g.lineTo(cw / 2, cd / 2);
    g.stroke();
    g.restore();
    const dr = cab * 0.3;
    g.fillStyle = 'rgba(242,200,121,.35)';
    g.beginPath();
    g.moveTo(lx, ly);
    g.arc(lx, ly, dr, -drumA - 0.5, -drumA + 0.5);
    g.closePath();
    g.fill();
    const hl = cab * 0.62,
      hx2 = lx + Math.cos(hornA) * hl,
      hy2 = ly - Math.sin(hornA) * hl;
    g.strokeStyle = '#f2c879';
    g.lineWidth = 3;
    g.lineCap = 'round';
    g.beginPath();
    g.moveTo(lx, ly);
    g.lineTo(hx2, hy2);
    g.stroke();
    g.fillStyle = '#f2c879';
    g.beginPath();
    g.arc(hx2, hy2, 3.5, 0, Math.PI * 2);
    g.fill();
    g.strokeStyle = 'rgba(242,200,121,.3)';
    g.lineWidth = 2;
    g.beginPath();
    g.moveTo(lx, ly);
    g.lineTo(lx - Math.cos(hornA) * hl * 0.7, ly + Math.sin(hornA) * hl * 0.7);
    g.stroke();
    g.fillStyle = '#c9b89c';
    g.font = '12px "Barlow Condensed", sans-serif';
    g.textAlign = 'center';
    g.fillText('Rotary', lx, ly + cab / 2 + 14);
    g.textAlign = 'left';
    // Mics
    if (S.mics.on) {
      const M = S.mics,
        D2R = Math.PI / 180;
      const mic = (x, y, ax, ay, r) => {
        const [a, b] = W2C(x, y);
        g.strokeStyle = '#f2c879';
        g.lineWidth = 1.5;
        g.beginPath();
        g.moveTo(a, b);
        g.lineTo(a + ax * (r + 7), b - ay * (r + 7));
        g.stroke();
        g.fillStyle = '#f2c879';
        g.beginPath();
        g.arc(a, b, r, 0, Math.PI * 2);
        g.fill();
        g.fillStyle = '#17130f';
        g.beginPath();
        g.arc(a, b, r * 0.45, 0, Math.PI * 2);
        g.fill();
      };
      const close = (st, ang, center, dist) => {
        const [a, b] = W2C(S.rotaryPos.x, S.rotaryPos.y);
        (st ? [center - ang / 2, center + ang / 2] : [center]).forEach(ph => {
          const t = S.cabYaw + ph * D2R,
            c = Math.cos(t),
            sn = Math.sin(t),
            cf = Math.abs(Math.cos(ph * D2R)),
            sf = Math.abs(Math.sin(ph * D2R));
          const R = Math.min(cf > 1e-6 ? 0.275 / cf : 1e9, sf > 1e-6 ? 0.37 / sf : 1e9) + Math.max(0.02, dist);
          const rp = Math.max(R * view.s, cab * 0.6 + 7); // at small scale keep the mic next to the cabinet
          const [wx, wy] = C2W(a + c * rp, b - sn * rp);
          mic(wx, wy, -c, -sn, big ? 4 : 3);
        });
      };
      close(M.hornStereo, M.hornAngle, M.hornCenter, M.hornDist);
      close(M.drumStereo, M.drumAngle, M.drumCenter, M.drumDist);
      const yawR = M.roomYaw,
        lx2 = -Math.sin(yawR),
        ly2 = Math.cos(yawR);
      const sp = M.roomArr === 0 ? M.roomSpacing : M.roomArr === 2 ? 0.17 : 0.02,
        aimOff = M.roomArr === 0 ? 0 : (M.roomArr === 2 ? 55 : 45) * D2R;
      [1, -1].forEach(sg => {
        // 1 = left (seen from the viewing direction)
        const off = (Math.max(sp * view.s, M.roomArr === 1 ? 3 : 9) / 2) * sg;
        const [ca, cb] = W2C(M.roomX, M.roomY),
          [wx, wy] = C2W(ca + lx2 * off, cb - ly2 * off),
          a = yawR + sg * aimOff;
        mic(wx, wy, Math.cos(a), Math.sin(a), big ? 5 : 4);
      });
      const [ca, cb] = W2C(M.roomX, M.roomY);
      g.fillStyle = '#c9b89c';
      g.font = '11px "Barlow Condensed", sans-serif';
      g.textAlign = 'center';
      g.fillText('Room pair', ca, cb + 20);
      g.textAlign = 'left';
    }
    // Listener + speakers
    const [hx, hy] = W2C(S.listener.x, S.listener.y),
      yaw = S.listener.yaw;
    g.globalAlpha = S.mics.on ? 0.35 : 1;
    MODES[S.mode].show.forEach(deg => {
      const a = yaw + (deg * Math.PI) / 180,
        rr = big ? 30 : 24;
      g.fillStyle = '#7d715f';
      g.beginPath();
      g.arc(hx + Math.cos(a) * rr, hy - Math.sin(a) * rr, big ? 3.5 : 2.5, 0, Math.PI * 2);
      g.fill();
    });
    g.fillStyle = '#efe7d8';
    g.beginPath();
    g.arc(hx, hy, 9, 0, Math.PI * 2);
    g.fill();
    g.beginPath();
    g.moveTo(hx + Math.cos(yaw) * 15, hy - Math.sin(yaw) * 15);
    g.lineTo(hx + Math.cos(yaw + 0.55) * 8, hy - Math.sin(yaw + 0.55) * 8);
    g.lineTo(hx + Math.cos(yaw - 0.55) * 8, hy - Math.sin(yaw - 0.55) * 8);
    g.fill();
    g.fillStyle = '#f2a33a';
    [Math.PI / 2, -Math.PI / 2].forEach(o => {
      g.beginPath();
      g.arc(hx + Math.cos(yaw + o) * 9, hy - Math.sin(yaw + o) * 9, 2.6, 0, Math.PI * 2);
      g.fill();
    });
    g.fillStyle = '#c9b89c';
    g.fillText(
      `You · ${nf(Math.hypot(S.rotaryPos.x - S.listener.x, S.rotaryPos.y - S.listener.y))} m`,
      hx + 14,
      hy + 22
    );
    g.globalAlpha = 1;
  }
  return self;
}
const rooms = [RoomView($('roomS'), false), RoomView($('roomB'), true)];
function roomLine() {
  const R = S.room;
  const dims =
    R.shape === 'tent'
      ? `Circus tent Ø ${nf(S.tent.W, 0)} m · crown ${nf(S.tent.hm)} m`
      : `${nf(R.Lx)} × ${nf(R.Ly)} × ${nf(R.Lz)} m · ${MAT_SHORT[R.material] || R.material}`;
  return dims + (roomInfo ? ` · RT60 ${nf(roomInfo.rt60[1])} s` : '');
}
reg({
  sync() {
    $('rinfoS').textContent = roomLine();
    if (roomInfo) {
      $('stRt').textContent = roomInfo.rt60.map(x => nf(x)).join(' / ') + ' s';
      $('stRc').textContent = nf(roomInfo.rc) + ' m';
      $('stPaths').textContent = roomInfo.paths;
    }
  },
});
