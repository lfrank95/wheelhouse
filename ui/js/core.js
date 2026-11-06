// Wheelhouse UI - Helpers, stage scaling, bridge to the native app, state and instrument presets.
// All files in ui/js run in one shared scope, in the order listed in tools/build_ui.py.

'use strict';
const $ = id => document.getElementById(id);
const h = (tag, cls, html) => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (html !== undefined) e.innerHTML = html;
  return e;
};
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const nf = (x, d = 1) => (+x).toFixed(d);
const pct = v => Math.round(v * 100) + '%';

// ---------------- Stage scaling (fixed 1600 × 900 design, scaled to the window) ----------------
const stage = $('stage');
let SCALE = 1;
function fit() {
  SCALE = Math.min(innerWidth / 1600, innerHeight / 900);
  stage.style.transform = `translate(${Math.round((innerWidth - 1600 * SCALE) / 2)}px, ${Math.round((innerHeight - 900 * SCALE) / 2)}px) scale(${SCALE})`;
  rooms.forEach(r => r.resize());
}

// ---------------- Bridge to the native app (JUCE backend) ----------------
// nativeInvoke() calls a native function and resolves with its result; send() pushes a DSP message.
const NATIVE = !!(window.__JUCE__ && window.__JUCE__.backend);
const promises = new Map();
let pid = 0;
const nativeInvoke = (name, ...params) =>
  new Promise(res => {
    if (!NATIVE) {
      res(null);
      return;
    }
    const id = pid++;
    promises.set(id, res);
    window.__JUCE__.backend.emitEvent('__juce__invoke', { name, params, resultId: id });
  });
if (NATIVE)
  window.__JUCE__.backend.addEventListener('__juce__complete', ({ promiseId, result }) => {
    const p = promises.get(promiseId);
    if (p) {
      p(result);
      promises.delete(promiseId);
    }
  });
let ready = false;
const NO_SAVE = new Set(['noteOn', 'noteOff', 'panic', 'springKick', 'taps']);
let muteSend = false; // while the state is assembled at startup (pushAll sends everything afterwards)
const send = m => {
  if (muteSend) return;
  if (NATIVE) nativeInvoke('dsp', m);
  if (ready && !NO_SAVE.has(m.type)) scheduleSave();
};

// ---------------- State ----------------
// DEF holds the defaults, S is the live state; all keys of DEF are persisted via saveUi/loadUi.
const DEF = {
  room: { Lx: 6, Ly: 8, Lz: 2.8, absorb: 0.3, order: 2, reverb: 1, material: 'wood', shape: 'box' },
  exact: false,
  threads: 0,
  chr: { cond: 0.5, leak: 0.5, contact: 0.5, motor: 0.4, gearTune: true, busComp: true },
  tent: {
    W: 30,
    Ls: 0,
    hm: 8.5,
    hw: 4,
    hb: 1.3,
    ring: 13,
    audience: 0.8,
    tip: 3.5,
    mast: 10,
    dome: 14.1,
    sagPct: 0.45,
    ground: 'grass',
    ringFloor: 'sand',
  },
  rotaryPos: { x: 4.2, y: 6.3 },
  listener: { x: 2.6, y: 2.4, yaw: Math.PI / 2 },
  cabYaw: -Math.PI / 2,
  db: { upper: [8, 8, 8, 0, 0, 0, 0, 0, 0], lower: [0, 0, 8, 4, 0, 0, 0, 0, 0], pedal: [6, 3] },
  perc: { on: true, third: true, fast: true, soft: false, vibU: true, vibL: false },
  vib: 'c3',
  drive: 0.35,
  click: 0.5,
  volume: 0.8,
  balance: 0.5,
  lfe: 0.5,
  organ: { percLevel: 1, percShort: 0.2, percLong: 1.0, complex: true, keyNoise: 0.5 },
  mains: 50,
  velClick: true,
  offlineMax: true,
  rotor: [
    { slow: 0.75, fast: 6.8, up: 0.8, down: 1.4 },
    { slow: 0.62, fast: 5.7, up: 4.5, down: 5.5 },
  ],
  brake: true,
  cab: { hornBox: 0.45, wobble: -1, board: 'ply', res: 0.5, mech: -1, deflector: true },
  amp: { model: 0, quality: 1, sag: 0.5 },
  pre: { on: true, gain: 0.5 },
  spring: { mix: 0.2, decay: 2.8, tone: 0.5, drive: 0.3, springs: 3 },
  mics: {
    on: false,
    hornStereo: true,
    hornAngle: 180,
    hornCenter: 0,
    hornDist: 0.15,
    hornHeight: 0,
    hornType: 0,
    hornPattern: 0,
    windscreen: false,
    drumStereo: false,
    drumAngle: 180,
    drumCenter: 0,
    drumDist: 0.15,
    drumType: 2,
    drumPattern: 0,
    roomArr: 0,
    roomX: 3,
    roomY: 3.5,
    roomZ: 2,
    roomYaw: Math.PI / 2,
    roomSpacing: 0.6,
    roomType: 4,
    roomPattern: 1,
    roomOrder: 2,
    placed: false,
    grp: [{ gain: 0 }, { gain: 0 }, { gain: -6 }].map(c =>
      Object.assign({ pan: 0, width: 1, delay: 0, pol: false, mute: false, solo: false, out: -1 }, c)
    ),
  },
  out: { masterDb: 0, lfeHz: 100, rearDb: 0, centerDb: 0, centerDiv: 0 },
  chmap: { stereo: null, quad: null, s51: null },
  midi: { upper: 1, lower: 2, pedal: 3, rotaryCC: 1, swellCC: 11 },
  split: { mode: 0, hi: 60, lo: 48, oct: [0, 0, 0], kb: 61 },
  rotary: 'slow',
  mode: 'stereo',
  octave: 0,
  pcMan: 0,
  halfMoon: 'stop',
};
const clone = o => JSON.parse(JSON.stringify(o));
const S = clone(DEF);
S.rot = { horn: 0, drum: 0, hornHz: 0.75, drumHz: -0.62, t: performance.now() };
S.meters = null;
let audioInfo = {
  outputs: 0,
  device: '',
  type: '',
  sampleRate: 0,
  bufferSize: 0,
  outLatencySamples: 0,
  driverLatencySamples: 0,
  channelNames: [],
  midiDevices: [],
};
let procLatency = 0,
  droppedBlocks = 0;
const micMeterEls = [];
const micHold = new Array(6).fill(-120);
let ampText = '–',
  tInfoText = '–',
  cpuText = '–',
  warnText = '',
  slowSync = 0,
  roomInfo = null;
const SAVE_KEYS = Object.keys(DEF);
let saveTimer = null;
function scheduleSave() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    const o = {};
    SAVE_KEYS.forEach(k => (o[k] = S[k]));
    nativeInvoke('saveUi', JSON.stringify(o));
  }, 800);
}
function deepMerge(dst, src) {
  for (const k of Object.keys(src)) {
    if (!(k in dst)) continue;
    const a = dst[k],
      b = src[k];
    if (a && typeof a === 'object' && !Array.isArray(a) && b && typeof b === 'object' && !Array.isArray(b))
      deepMerge(a, b);
    else if (Array.isArray(a) && Array.isArray(b)) {
      if (a.length === b.length) {
        if (a.length && a[0] && typeof a[0] === 'object')
          a.forEach((el, i) => {
            if (b[i] && typeof b[i] === 'object') deepMerge(el, b[i]);
          });
        else dst[k] = b.slice();
      }
    } else if (b === null || typeof b === typeof a || a === null) dst[k] = b;
  }
}

const MODES = {
  stereo: {
    ch: 2,
    names: ['L', 'R'],
    long: ['Left', 'Right'],
    spk: { az: [30, -30], ch: [0, 1], lfe: -1, stereo: true },
    show: [30, -30],
  },
  quad: {
    ch: 4,
    names: ['FL', 'FR', 'RL', 'RR'],
    long: ['Front left', 'Front right', 'Rear left', 'Rear right'],
    spk: { az: [45, -45, 135, -135], ch: [0, 1, 2, 3], lfe: -1 },
    show: [45, -45, 135, -135],
  },
  s51: {
    ch: 6,
    names: ['L', 'R', 'C', 'LFE', 'SL', 'SR'],
    long: ['Front left', 'Front right', 'Center', 'LFE', 'Surround left', 'Surround right'],
    spk: { az: [30, -30, 0, 110, -110], ch: [0, 1, 2, 4, 5], lfe: 3 },
    show: [30, -30, 0, 110, -110],
  },
};
const ROOMS = [
  { n: 'Living room', s: 'Living', Lx: 5, Ly: 6, Lz: 2.6, absorb: 0.38, reverb: 1, material: 'wood' },
  { n: 'Rehearsal room', s: 'Jam', Lx: 6, Ly: 8, Lz: 2.8, absorb: 0.3, reverb: 1, material: 'wood' },
  { n: 'Club', s: 'Club', Lx: 12, Ly: 18, Lz: 4.5, absorb: 0.27, reverb: 1, material: 'concrete' },
  {
    n: 'Festival tent',
    s: 'Festival',
    Lx: 20,
    Ly: 30,
    Lz: 7,
    absorb: 0.45,
    reverb: 0.8,
    material: 'tent',
    noQuick: true,
  },
  { n: 'Hall', s: 'Hall', Lx: 25, Ly: 40, Lz: 10, absorb: 0.12, reverb: 1, material: 'concrete' },
  { n: 'Church', s: 'Church', Lx: 16, Ly: 36, Lz: 14, absorb: 0.09, reverb: 1.1, material: 'stone' },
  { n: 'Circus tent', s: 'Circus', tent: true },
];
const MATERIALS = [
  ['wood', 'Wood / drywall'],
  ['concrete', 'Concrete'],
  ['stone', 'Stone'],
  ['glass', 'Glass / tiles'],
  ['tent', 'Canvas'],
];
const MAT_SHORT = { wood: 'Wood', concrete: 'Concrete', stone: 'Stone', glass: 'Glass', tent: 'Canvas' };
const UPPER_PRESETS = [
  ['Jazz', [8, 8, 8, 0, 0, 0, 0, 0, 0]],
  ['Gospel', [8, 8, 8, 8, 8, 8, 8, 8, 8]],
  ['Rock', [8, 8, 8, 8, 0, 0, 0, 0, 0]],
  ['Ballad', [8, 3, 8, 0, 0, 0, 0, 0, 0]],
  ['Flute', [0, 0, 8, 0, 0, 0, 0, 0, 0]],
  ['Theatre', [8, 7, 6, 5, 4, 3, 2, 1, 1]],
  ['Perc 2nd', [8, 4, 8, 0, 0, 0, 0, 0, 0]],
  ['Smith', [8, 8, 8, 8, 0, 0, 0, 0, 8]],
];
const LOWER_PRESETS = [
  ['Comping', [0, 0, 8, 4, 0, 0, 0, 0, 0]],
  ['Mellow', [0, 0, 6, 6, 0, 0, 0, 0, 0]],
  ['Jazz', [8, 8, 8, 0, 0, 0, 0, 0, 0]],
  ['Flute', [0, 0, 8, 0, 0, 0, 0, 0, 0]],
  ['Strings', [0, 0, 4, 5, 4, 4, 2, 0, 0]],
  ['Full', [8, 8, 8, 8, 8, 8, 8, 8, 8]],
];
const PRESETS = [
  {
    n: 'Jazz Combo',
    u: [8, 8, 8, 0, 0, 0, 0, 0, 0],
    l: [0, 0, 8, 4, 0, 0, 0, 0, 0],
    p: [6, 3],
    perc: { on: true, third: true, fast: true, soft: false, vibU: true, vibL: false },
    vib: 'c3',
    drive: 0.3,
  },
  {
    n: 'Full Gospel',
    u: [8, 8, 8, 8, 8, 8, 8, 8, 8],
    l: [8, 8, 8, 8, 0, 0, 0, 0, 0],
    p: [8, 5],
    perc: { on: false, third: true, fast: true, soft: false, vibU: true, vibL: true },
    vib: 'c3',
    drive: 0.45,
  },
  {
    n: 'Rock',
    u: [8, 8, 8, 8, 0, 0, 0, 0, 0],
    l: [8, 8, 8, 0, 0, 0, 0, 0, 0],
    p: [8, 4],
    perc: { on: false, third: true, fast: true, soft: false, vibU: false, vibL: false },
    vib: 'c3',
    drive: 0.75,
  },
  {
    n: 'Ballad',
    u: [8, 3, 8, 0, 0, 0, 0, 0, 0],
    l: [0, 0, 6, 6, 0, 0, 0, 0, 0],
    p: [5, 2],
    perc: { on: false, third: true, fast: true, soft: false, vibU: true, vibL: true },
    vib: 'c3',
    drive: 0.15,
  },
  {
    n: 'Flute',
    u: [0, 0, 8, 0, 0, 0, 0, 0, 0],
    l: [0, 0, 8, 0, 0, 0, 0, 0, 0],
    p: [4, 2],
    perc: { on: false, third: true, fast: true, soft: false, vibU: true, vibL: true },
    vib: 'v2',
    drive: 0.1,
  },
  {
    n: 'Theatre',
    u: [8, 7, 6, 5, 4, 3, 2, 1, 1],
    l: [0, 0, 4, 5, 4, 4, 2, 0, 0],
    p: [7, 4],
    perc: { on: false, third: true, fast: true, soft: false, vibU: true, vibL: true },
    vib: 'v3',
    drive: 0.2,
  },
  {
    n: 'Perc 2nd',
    u: [8, 4, 8, 0, 0, 0, 0, 0, 0],
    l: [0, 0, 8, 4, 0, 0, 0, 0, 0],
    p: [6, 3],
    perc: { on: true, third: false, fast: false, soft: false, vibU: true, vibL: false },
    vib: 'c2',
    drive: 0.3,
  },
];
// Rotor times are time constants: the rotor reaches speed after about 3 τ
// (measured on real rotary cabinets: horn ~1.5 s up / 2.5 s down, drum ~5 s / 7.5 s; 48/400 and 40/342 rpm)
const rpmHz = r => r / 60;
const INSTRUMENTS = [
  {
    n: 'B3 + 122 (Stock)',
    note: 'well-kept 60s B3, stock 122 without stop, no reverb',
    chr: { cond: 0.45, leak: 0.45, contact: 0.45, motor: 0.46, gearTune: true, busComp: true },
    organ: { percLevel: 1, percShort: 0.2, percLong: 1.0, complex: true },
    click: 0.5,
    db: { upper: [8, 8, 8, 0, 0, 0, 0, 0, 0], lower: [0, 0, 8, 4, 0, 0, 0, 0, 0], pedal: [6, 3] },
    perc: { on: true, third: true, fast: true, soft: false, vibU: true, vibL: false },
    vib: 'c3',
    pre: { on: true, gain: 0.5 },
    amp: { model: 0, quality: 1, sag: 0.5 },
    drive: 0.35,
    balance: 0.5,
    rotor: [
      { slow: rpmHz(48), fast: rpmHz(400), up: 0.5, down: 0.85 },
      { slow: rpmHz(40), fast: rpmHz(342), up: 1.7, down: 2.5 },
    ],
    brake: true,
    cab: { hornBox: 0.45, wobble: -1, board: 'ply', res: 0.5, mech: -1, deflector: true },
    halfMoon: 'two',
    spring: { mix: 0 },
  },
  {
    n: 'B3 + 147 (Rock)',
    note: '147 with negative feedback, hot preamp, vibrato off',
    chr: { cond: 0.55, leak: 0.55, contact: 0.55, motor: 0.54, gearTune: true, busComp: true },
    organ: { percLevel: 1, percShort: 0.2, percLong: 1.0, complex: true },
    click: 0.6,
    db: { upper: [8, 8, 8, 8, 0, 0, 0, 0, 0], lower: [8, 8, 8, 0, 0, 0, 0, 0, 0], pedal: [8, 4] },
    perc: { on: false, third: true, fast: true, soft: false, vibU: false, vibL: false },
    vib: 'c3',
    pre: { on: true, gain: 0.75 },
    amp: { model: 2, quality: 1, sag: 0.6 },
    drive: 0.75,
    balance: 0.5,
    rotor: [
      { slow: rpmHz(48), fast: rpmHz(400), up: 0.5, down: 0.85 },
      { slow: rpmHz(40), fast: rpmHz(342), up: 1.7, down: 2.5 },
    ],
    brake: true,
    cab: { hornBox: 0.45, wobble: -1, board: 'ply', res: 0.5, mech: -1, deflector: true },
    halfMoon: 'two',
    spring: { mix: 0 },
  },
  {
    n: 'A-100 + Spring Reverb',
    note: 'same tone generator as the B3, built-in organ spring tank',
    chr: { cond: 0.45, leak: 0.45, contact: 0.45, motor: 0.46, gearTune: true, busComp: true },
    organ: { percLevel: 1, percShort: 0.2, percLong: 1.0, complex: true },
    click: 0.45,
    db: { upper: [8, 3, 8, 0, 0, 0, 0, 0, 0], lower: [0, 0, 6, 6, 0, 0, 0, 0, 0], pedal: [5, 2] },
    perc: { on: false, third: true, fast: true, soft: false, vibU: true, vibL: true },
    vib: 'c3',
    pre: { on: true, gain: 0.5 },
    amp: { model: 0, quality: 1, sag: 0.5 },
    drive: 0.3,
    balance: 0.5,
    rotor: [
      { slow: rpmHz(48), fast: rpmHz(400), up: 0.5, down: 0.85 },
      { slow: rpmHz(40), fast: rpmHz(342), up: 1.7, down: 2.5 },
    ],
    brake: true,
    cab: { hornBox: 0.45, wobble: -1, board: 'ply', res: 0.5, mech: -1, deflector: true },
    halfMoon: 'two',
    spring: { mix: 0.3, decay: 2.8, tone: 0.5, drive: 0.3, springs: 3 },
  },
  {
    n: 'Road-worn B3',
    note: '40 years on the road: bouncy contacts, slack belts, tired tubes',
    chr: { cond: 0.85, leak: 0.85, contact: 0.9, motor: 0.8, gearTune: true, busComp: true },
    organ: { percLevel: 0.85, percShort: 0.17, percLong: 0.9, complex: true },
    click: 0.75,
    db: { upper: [8, 8, 8, 8, 0, 0, 0, 0, 0], lower: [8, 4, 8, 0, 0, 0, 0, 0, 0], pedal: [8, 5] },
    perc: { on: true, third: true, fast: true, soft: false, vibU: true, vibL: false },
    vib: 'c3',
    pre: { on: true, gain: 0.7 },
    amp: { model: 1, quality: 1, sag: 0.8 },
    drive: 0.6,
    balance: 0.5,
    rotor: [
      { slow: rpmHz(44), fast: rpmHz(380), up: 0.75, down: 1.1 },
      { slow: rpmHz(36), fast: rpmHz(325), up: 2.3, down: 3.2 },
    ],
    brake: true,
    cab: { hornBox: 0.5, wobble: 0.65, board: 'chip', res: 0.65, mech: -1, deflector: true },
    halfMoon: 'two',
    spring: { mix: 0 },
  },
];
const VIB = [
  ['v1', 'V1'],
  ['v2', 'V2'],
  ['v3', 'V3'],
  ['c1', 'C1'],
  ['c2', 'C2'],
  ['c3', 'C3'],
];
const AMP_MODELS = [
  [0, '122 · 2 × 6550'],
  [1, '122 early · 5U4'],
  [2, '147 · neg. feedback'],
];
