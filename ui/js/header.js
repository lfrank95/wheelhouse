// Wheelhouse UI - Header bar, channel mapping, Rotary panel on the play surface.
// All files in ui/js run in one shared scope, in the order listed in tools/build_ui.py.

// ---------------- Header bar: preset, output format, meters ----------------
const presetSel = $('preset');
(() => {
  const o0 = h('option', '', 'Custom');
  o0.value = '';
  o0.disabled = true;
  presetSel.appendChild(o0);
  const g1 = h('optgroup');
  g1.label = 'Instrument (organ + Rotary)';
  INSTRUMENTS.forEach((p, i) => {
    const o = h('option', '', p.n);
    o.value = 'i' + i;
    o.title = p.note;
    g1.appendChild(o);
  });
  presetSel.appendChild(g1);
  const g2 = h('optgroup');
  g2.label = 'Registration';
  PRESETS.forEach((p, i) => {
    const o = h('option', '', p.n);
    o.value = i;
    g2.appendChild(o);
  });
  presetSel.appendChild(g2);
})();
let curInst = null,
  instSig = '';
const instState = () =>
  JSON.stringify([
    S.chr,
    S.organ,
    S.click,
    S.db,
    S.perc,
    S.vib,
    S.pre,
    S.amp,
    S.drive,
    S.balance,
    S.rotor,
    S.brake,
    S.cab,
    S.halfMoon,
    S.spring,
  ]);
function applyInstrument(i) {
  const P = clone(INSTRUMENTS[i]);
  Object.assign(S.chr, P.chr);
  Object.assign(S.organ, P.organ);
  S.click = P.click;
  S.db = P.db;
  S.perc = P.perc;
  S.vib = P.vib;
  Object.assign(S.pre, P.pre);
  Object.assign(S.amp, P.amp);
  S.drive = P.drive;
  S.balance = P.balance;
  S.rotor = P.rotor;
  S.brake = P.brake;
  Object.assign(S.cab, P.cab);
  S.halfMoon = P.halfMoon;
  Object.assign(S.spring, P.spring);
  if (S.halfMoon !== 'stop' && S.rotary === 'stop') S.rotary = 'slow';
  sendChr();
  sendOrgan();
  send({ type: 'click', v: S.click });
  [0, 1, 2].forEach(sendDb);
  sendPerc();
  send({ type: 'vib', v: S.vib });
  sendPre();
  sendAmp();
  send({ type: 'drive', v: S.drive });
  send({ type: 'balance', v: S.balance });
  sendRotor(0);
  sendRotor(1);
  send({ type: 'brake', v: S.brake });
  sendCab();
  sendSpring();
  send({ type: 'rotary', v: S.rotary });
  curInst = i;
  instSig = instState();
  syncAll();
}
presetSel.onchange = () => {
  presetSel.blur();
  const val = presetSel.value;
  if (val[0] === 'i') {
    applyInstrument(+val.slice(1));
    return;
  }
  const p = PRESETS[+val];
  if (!p) return;
  S.db.upper = p.u.slice();
  S.db.lower = p.l.slice();
  S.db.pedal = p.p.slice();
  S.perc = Object.assign({}, p.perc);
  S.vib = p.vib;
  S.drive = p.drive;
  [0, 1, 2].forEach(sendDb);
  sendPerc();
  send({ type: 'vib', v: S.vib });
  send({ type: 'drive', v: S.drive });
  syncAll();
};
const PRESET_TIP =
  'Instruments set organ, preamp, Rotary and reverb. Registrations only set drawbars, percussion, vibrato and drive. Room, mics, output and MIDI stay as they are.';
const markPreset = () => {
  if (curInst !== null && instState() === instSig) {
    presetSel.value = 'i' + curInst;
    presetSel.dataset.tip = INSTRUMENTS[curInst].note + '\n\n' + PRESET_TIP;
    return;
  }
  curInst = null;
  presetSel.dataset.tip = PRESET_TIP;
  const i = PRESETS.findIndex(
    p =>
      p.u.join() === S.db.upper.join() &&
      p.l.join() === S.db.lower.join() &&
      p.p.join() === S.db.pedal.join() &&
      p.vib === S.vib &&
      p.perc.on === S.perc.on
  );
  presetSel.value = i >= 0 ? String(i) : '';
};
reg({ sync: markPreset });

const outBtns = [
  ['stereo', 'Stereo'],
  ['quad', 'Quad'],
  ['s51', '5.1'],
].map(([m, t]) => {
  const b = h('button', '', t);
  b.type = 'button';
  b.onclick = () => {
    S.mode = m;
    applyMode();
    syncAll();
  };
  $('outSeg').appendChild(b);
  return [m, b];
});
reg({
  sync() {
    outBtns.forEach(([m, b]) => b.classList.toggle('on', m === S.mode));
  },
});
const modeBtns = [
  [false, 'Listener'],
  [true, 'Mics'],
].map(([v, t]) => {
  const b = h('button', '', t);
  b.type = 'button';
  b.onclick = () => setMicsOn(v);
  $('modeSeg').appendChild(b);
  return [v, b];
});
reg({
  sync() {
    modeBtns.forEach(([v, b]) => b.classList.toggle('on', v === S.mics.on));
  },
});

let meterEls = [],
  meterMode = null;
const meterHold = new Array(8).fill(-120);
function buildMeters() {
  meterMode = S.mode;
  $('meters').innerHTML = '';
  meterEls = [];
  MODES[S.mode].names.forEach(nm => {
    const m = h('div', 'meter', `<div class="t"><div class="f"></div></div><span>${nm}</span>`);
    $('meters').appendChild(m);
    meterEls.push(m.querySelector('.f'));
  });
}
function drawMeters() {
  if (meterMode !== S.mode) buildMeters();
  if (!S.meters) return;
  MODES[S.mode].names.forEach((nm, i) => {
    const phys = physCh(i);
    const db = S.meters[phys] !== undefined ? S.meters[phys] : -120;
    meterHold[i] = Math.max(db, meterHold[i] - 0.6);
    meterEls[i].style.height = clamp((meterHold[i] + 60) / 60, 0, 1) * 100 + '%';
    meterEls[i].classList.toggle('clip', meterHold[i] > -1);
  });
}

// ---------------- Channel mapping (logical channel -> physical output) ----------------
function autoMap(mode) {
  const M = MODES[mode];
  const out = [];
  for (let i = 0; i < M.ch; i++) out.push(i);
  return out;
}
function curMap() {
  const m = S.chmap[S.mode];
  return m && m.length === MODES[S.mode].ch && m.every(c => Number.isInteger(c) && c >= 0 && c < 8)
    ? m
    : autoMap(S.mode);
}
function physCh(i) {
  return curMap()[i];
}
let curSpk = null;
function applyMode() {
  const M = MODES[S.mode],
    map = curMap();
  curSpk = Object.assign({}, M.spk);
  if (S.mode === 's51') {
    curSpk.ch = [map[0], map[1], map[2], map[4], map[5]];
    curSpk.lfe = map[3];
  } else curSpk.ch = map.slice();
  send(Object.assign({ type: 'speakers' }, curSpk));
  updateChanInfo();
}
function updateChanInfo() {
  const M = MODES[S.mode],
    have = audioInfo.outputs || 0;
  const need = Math.max(...curMap()) + 1;
  warnText =
    have && have < need
      ? `The audio device only has ${have} active outputs – ${S.mode === 's51' ? '5.1' : S.mode === 'quad' ? 'Quad' : 'Stereo'} needs ${need} (Audio tab → Audio Settings).`
      : '';
}
const outName = i => {
  const n = audioInfo.channelNames && audioInfo.channelNames[i];
  return `Out ${i + 1}` + (n ? ` · ${n}` : '');
};

// ---------------- Rotary panel on the play surface (SVG cabinet) ----------------
(() => {
  const top = $('slatsTop'),
    bot = $('slatsBot');
  const mk = (g, y, hgt, op) => {
    const r = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    r.setAttribute('x', 12);
    r.setAttribute('y', y);
    r.setAttribute('width', 116);
    r.setAttribute('height', hgt);
    r.setAttribute('fill', 'url(#lslat)');
    r.setAttribute('opacity', op);
    g.appendChild(r);
  };
  [
    [21, 0.92],
    [28, 0.88],
    [35, 0.85],
    [42, 0.85],
    [49, 0.85],
    [56, 0.88],
    [63, 0.92],
  ].forEach(([y, o]) => mk(top, y, 3.5, o));
  mk(top, 70, 4, 1);
  [
    [103, 0.92],
    [110, 0.86],
    [117, 0.82],
    [124, 0.82],
    [131, 0.82],
    [138, 0.82],
    [145, 0.82],
    [152, 0.86],
    [159, 0.9],
  ].forEach(([y, o]) => mk(bot, y, 3.5, o));
  mk(bot, 166, 4, 1);
})();
const SPEED_TIP = {
  slow: 'Slow (chorale): gentle swirl.',
  stop: 'Stop: rotors stop (or coast, see Rotary › Brake).',
  fast: 'Fast (tremolo): full spin. Space bar toggles Slow/Fast.',
};
const speedBtns = [
  ['slow', 'Slow'],
  ['stop', 'Stop'],
  ['fast', 'Fast'],
].map(([v, t]) => {
  const b = h('button', '', t);
  b.type = 'button';
  b.dataset.tip = SPEED_TIP[v];
  b.onclick = () => setRotary(v);
  $('speed').appendChild(b);
  return [v, b];
});
reg({
  sync() {
    speedBtns.forEach(([v, b]) => {
      b.classList.toggle('on', v === S.rotary);
      if (v === 'stop') b.hidden = S.halfMoon !== 'stop';
    });
    $('speed').classList.toggle('two', S.halfMoon !== 'stop');
    $('lesModel').textContent = 'Model ' + ['122', '122 early', '147'][S.amp.model];
    $('lesHint').textContent =
      `Space bar or ${S.midi.rotaryCC === 64 ? 'sustain pedal' : S.midi.rotaryCC === 1 ? 'mod wheel' : 'CC ' + S.midi.rotaryCC} toggles speed`;
  },
});
function setRotary(v) {
  if (v === 'stop' && S.halfMoon !== 'stop') v = 'slow';
  S.rotary = v;
  send({ type: 'rotary', v });
  syncAll();
}
const svgSet = (id, attrs) => {
  const e = $(id);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
};
function drawCabinet(hornA, drumA) {
  const horn = ang => {
    const sx = Math.sin(ang),
      depth = Math.cos(ang),
      x = 70 + 38 * sx,
      rx = Math.max(2.5, 11 * Math.abs(depth)),
      tipH = 9 + 2 * depth;
    const nx = (x - (x > 70 ? rx * 0.3 : -rx * 0.3)).toFixed(1);
    return {
      x: x.toFixed(1),
      rx: rx.toFixed(1),
      rxIn: (rx * 0.62).toFixed(1),
      neck: `M70 41.5 L${nx} ${(44 - tipH).toFixed(1)} L${nx} ${(44 + tipH).toFixed(1)} L70 46.5 Z`,
      front: depth >= 0,
    };
  };
  // Angles relative to the cabinet front (the front faces the viewer). f = the sounding horn (gold mouth),
  // b = the capped dummy opposite. Whichever is nearer is drawn in front of the shaft, but each keeps its own look:
  // the sounding horn seen from behind shows its darker gold outside and no mouth.
  const a = hornA - S.cabYaw,
    f = horn(a),
    b = horn(a + Math.PI),
    live = f.front;
  const F = live ? f : b,
    B = live ? b : f;
  svgSet('hfNeck', { d: F.neck, fill: live ? 'url(#lbell)' : '#2a1d12' });
  svgSet('hfBell', { cx: F.x, rx: F.rx, fill: live ? '#f2c879' : '#1d140c', stroke: live ? '#a88a55' : '#3a2a1b' });
  svgSet('hfIn', { cx: F.x, rx: F.rxIn, display: live ? 'inline' : 'none' });
  svgSet('hbNeck', { d: B.neck, fill: live ? '#2a1d12' : '#8a6a3a' });
  svgSet('hbBell', { cx: B.x, rx: B.rx, fill: live ? '#1d140c' : '#a8803e', stroke: live ? '#3a2a1b' : '#6a4f24' });
  const d = drumA - S.cabYaw,
    dc = Math.cos(d),
    dxw = 70 + 40 * Math.sin(d),
    dw = Math.max(4, 30 * dc);
  $('drumWin').style.display = dc > 0.05 ? '' : 'none';
  svgSet('dwRect', { x: (dxw - dw / 2).toFixed(1), width: dw.toFixed(1) });
  svgSet('dwBaffle', { d: `M${(dxw - dw / 2 + 2).toFixed(1)} 156 L${(dxw + dw / 2 - 2).toFixed(1)} 118` });
  $('orbit').setAttribute('opacity', S.rot.hornHz > 3 ? 0.55 : 0.25);
}
