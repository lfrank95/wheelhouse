// Wheelhouse UI - Control builders (knobs, rows), senders per DSP section, tabs and settings cards.
// All files in ui/js run in one shared scope, in the order listed in tools/build_ui.py.

// ---------------- Control builders ----------------
// Every control is registered via reg(); syncAll() refreshes all of them from S.
const all = []; // all controls with sync()
const cardCtl = {}; // card id -> controls with reset()
let curCard = null;
const reg = c => {
  all.push(c);
  if (curCard) (cardCtl[curCard] = cardCtl[curCard] || []).push(c);
  return c;
};
// After a sync, refit the knob labels of the open card if its visible sections changed (e.g. room <-> tent)
let fitSig = '';
const syncAll = () => {
  all.forEach(c => c.sync && c.sync());
  let pg = null;
  try {
    pg = cardPages[tab];
  } catch (e) {
    return;
  } // not declared yet while the cards are being built
  if (!pg) return;
  const sig = tab + [...pg.querySelectorAll('.sec')].map(e => (e.hidden ? 0 : 1)).join('');
  if (sig !== fitSig) {
    fitSig = sig;
    requestAnimationFrame(() => fitKnobLabels(pg));
  }
};

// Rotary knob: o = { label, get, set, min, max, def, fmt, step, log, opts (discrete), auto }
function knob(o) {
  const el = h(
    'div',
    o.small ? 'knob sm' : 'knob',
    `<button type="button" class="kb"><span class="kc"><span class="kp"></span></span></button><span class="kl"></span><span class="kv"></span>`
  );
  const kb = el.querySelector('.kb'),
    kc = el.querySelector('.kc'),
    kv = el.querySelector('.kv');
  el.querySelector('.kl').textContent = o.label;
  kb.setAttribute('aria-label', o.label);
  const disc = !!o.opts,
    n = disc ? o.opts.length : 0;
  const toF = v =>
    disc
      ? Math.max(
          0,
          o.opts.findIndex(x => x[0] === v)
        ) /
        (n - 1)
      : o.log
        ? Math.log(v / o.min) / Math.log(o.max / o.min)
        : (v - o.min) / (o.max - o.min);
  const fromF = f => {
    f = clamp(f, 0, 1);
    if (disc) return o.opts[Math.round(f * (n - 1))][0];
    let v = o.log ? o.min * Math.pow(o.max / o.min, f) : o.min + f * (o.max - o.min);
    if (o.step) v = Math.round(v / o.step) * o.step;
    return +v.toFixed(6);
  };
  const c = {
    el,
    sync() {
      const v = o.get(),
        isAuto = o.auto && v === o.auto.v;
      const f = isAuto ? toF(o.auto.show()) : toF(v);
      kc.style.transform = `rotate(${Math.round(-135 + 270 * clamp(f, 0, 1))}deg)`;
      kv.textContent = isAuto ? o.auto.label : disc ? (o.opts.find(x => x[0] === v)?.[1] ?? v) : o.fmt(v);
      kb.setAttribute('aria-valuetext', kv.textContent);
    },
    reset() {
      o.set(o.def);
      c.sync();
    },
  };
  let drag = null;
  const cur = () => {
    const v = o.get();
    return o.auto && v === o.auto.v ? toF(o.auto.show()) : toF(v);
  };
  const apply = f => {
    const v = fromF(f);
    if (v !== o.get()) {
      o.set(v);
      c.sync();
    }
  };
  kb.addEventListener('pointerdown', e => {
    kb.setPointerCapture(e.pointerId);
    drag = { y: e.clientY, f: cur() };
    e.preventDefault();
  });
  kb.addEventListener('pointermove', e => {
    if (!drag) return;
    const sens = (e.shiftKey ? 600 : 160) * SCALE;
    apply(drag.f + (drag.y - e.clientY) / sens);
  });
  const up = () => (drag = null);
  kb.addEventListener('pointerup', up);
  kb.addEventListener('pointercancel', up);
  kb.addEventListener(
    'wheel',
    e => {
      e.preventDefault();
      const d = e.deltaY < 0 ? 1 : -1;
      apply(cur() + d * (disc ? 1 / (n - 1) : e.shiftKey ? 0.005 : 0.025));
    },
    { passive: false }
  );
  kb.addEventListener('dblclick', () => c.reset());
  kb.addEventListener('keydown', e => {
    const d =
      e.key === 'ArrowUp' || e.key === 'ArrowRight' ? 1 : e.key === 'ArrowDown' || e.key === 'ArrowLeft' ? -1 : 0;
    if (d) {
      e.preventDefault();
      e.stopPropagation();
      apply(cur() + d * (disc ? 1 / (n - 1) : 0.025));
    }
  });
  kb.dataset.tipK = 'Drag or scroll · Shift = fine · Double-click = default';
  if (o.tip) kb.dataset.tip = o.tip;
  c.tipEl = kb;
  c.label = o.label;
  c.sync();
  return reg(c);
}
const tipMissing = [];
window.__tipMissing = tipMissing;
const TIPS = {
  // Organ
  'Character/Condition':
    'Overall wear of the instrument – from showroom-fresh to 40 years on the road. Pulls Leakage, Contacts and Motor along and sets the level spread and runout of the tone wheels.',
  'Character/Leakage':
    'Tone-wheel crosstalk: neighbouring wheels bleed into the pickups and wiring – the faint "hiss and whistle" under every B3.',
  'Character/Contacts':
    'Key contact wear: dirty busbar contacts bounce and crackle, so the click gets dirtier and less even.',
  'Character/Motor': 'Hum and whir of the generator and run motor, audible between notes.',
  'Generator/Key noise':
    'Mechanical key noise: keys bottoming out on the felt. The console sits right in front of you, so it is acoustic, not through the Rotary.',
  'Generator/Tuning':
    'Gears: the slightly off equal temperament of the real gear train. Equal: mathematically exact tuning.',
  'Generator/Busbar load':
    'Loading of the generator: the more drawbars and keys you add, the more each one sags in level (the classic "compression" of a full registration).',
  'Generator/Low wheels':
    'Complex: the lowest wheels are nearly square, with odd harmonics (pedal only). Sine: idealised.',
  'Generator/Mains':
    'Mains frequency of the synchronous motor: sets the hum and – on a 50 Hz organ run at 60 Hz – the pitch.',
  'Percussion · Vibrato · Click/Key click':
    'Key click: the burst from nine contacts closing on the busbar. Part of the sound – purists keep it up.',
  'Percussion · Vibrato · Click/Perc level': 'Overall level of the percussion (on top of Soft/Normal).',
  'Percussion · Vibrato · Click/Fast decay': 'Decay time of the percussion when set to Fast.',
  'Percussion · Vibrato · Click/Slow decay': 'Decay time of the percussion when set to Slow.',
  'Percussion · Vibrato · Click/Vibrato':
    'Scanner vibrato/chorus. V1–V3: vibrato. C1–C3: chorus (vibrato mixed with dry). C3 is the classic.',
  'Percussion · Vibrato · Click/Click':
    'Velocity: a fast stroke closes all nine contacts almost at once and hard, a slow press one after another and softer. Fixed: always the same.',
  'Preamp AO-28/Gain':
    'Preamp gain after the swell: 12AX7 into a 12BH7 push-pull and transformer. With the swell wide open and full drawbars the organ breaks up before the Rotary.',
  'Preamp AO-28/Tubes': 'Bypass skips the tube stages for a clean, line-level organ.',
  'Preamp AO-28/Quality': 'Uses the same oversampling quality as the power amp.',
  'Spring Reverb/Mix':
    'Wet/dry mix of the spring tank. It sits inside the organ, before the Rotary – so the reverb spins too.',
  'Spring Reverb/Decay': 'Decay time of the springs.',
  'Spring Reverb/Tone': 'Tone of the tank return: dark to bright.',
  'Spring Reverb/Drive': 'Drive into the tank driver: more drive = more boing and splash.',
  'Spring Reverb/Springs': 'Number of springs: 2 is sparser and twangier, 3 is denser.',
  'Spring Reverb/Tank': 'Kick the tank – the springs crash, like bumping the organ.',
  // Rotary
  'Horn/Slow': 'Horn speed on Slow.',
  'Horn/Fast': 'Horn speed on Fast.',
  'Horn/Rise': 'Ramp-up time of the horn from Slow to Fast. The light horn gets there in under a second.',
  'Horn/Fall': 'Ramp-down time of the horn from Fast to Slow.',
  'Drum/Slow': 'Drum (bass rotor) speed on Slow.',
  'Drum/Fast': 'Drum speed on Fast.',
  'Drum/Rise': 'Ramp-up time of the heavy drum – it lags behind the horn, which is a big part of the Rotary sound.',
  'Drum/Fall': 'Ramp-down time of the drum.',
  'Drum/Brake': 'On: the rotors stop on Stop. Off: they coast to a halt.',
  'Power Amp/Drive': 'How hard the organ hits the Rotary power amp – from clean to growl.',
  'Power Amp/Sag':
    'Power-supply sag: the rectifier voltage dips under load – softer attack, more compression when driven.',
  'Power Amp/Model':
    '122: 2 × 6550, tight. 122 early: 5U4 tube rectifier, more sag. 147: with negative feedback, cleaner and stiffer.',
  'Power Amp/Quality':
    'Eco: static curve, low CPU. HQ: tubes and output transformer as a circuit, 2× oversampling. Ultra: 4× oversampling.',
  'Power Amp/Plate / cath.': 'Live plate and cathode readings of the output tubes.',
  'Cabinet/Horn box': 'Sound from the back of the horn bouncing around inside the horn compartment and leaking out.',
  'Cabinet/Resonance': 'Cabinet panel resonances: the wooden walls vibrate along and colour the low mids.',
  'Cabinet/Mechanics':
    'Mechanical noise: motors, belts, bearings, relays and the slow motor clutching in. Auto follows Organ › Condition.',
  'Cabinet/Wobble': 'Rotor runout: slightly out-of-round rotation and speed flutter. Auto follows Organ › Condition.',
  'Cabinet/Build': 'Early cabinets are plywood, later ones chipboard – softer, more damped.',
  'Cabinet/Deflectors':
    'The cone-shaped deflectors in the horn mouths. In (stock) spreads the treble – gentle, wide swirl. Out beams the highs – a sharper, more pronounced sweep, as some players mod their cabinets.',
  'Placement/Balance': 'Horn/drum level balance: 50/50 = both at full level, left = more horn, right = more drum.',
  'Placement/Facing':
    'Which way the Rotary front points in the room. You can also turn it in the floor plan with the scroll wheel.',
  'Placement/Quick turn': 'Turn the Rotary front towards the listener (or the room pair in Mics mode).',
  'Placement/Back to the wall': 'Put the Rotary against the nearest wall, front facing into the room.',
  'Control/Switch via': 'Which MIDI controller toggles Slow/Fast. The space bar always works.',
  'Control/Half-moon': '2-way: Slow/Fast. With Stop: the 3-way half-moon switch with a stop position in the middle.',
  // Room
  'Dimensions/Width': 'Room width.',
  'Dimensions/Length': 'Room length.',
  'Dimensions/Height': 'Ceiling height.',
  'Dimensions/Reverb': 'Scales the late reverb tail. 100% = physically correct for the room.',
  '*/Shape': 'Box: rectangular room with image sources. Circus tent: ray-traced tent with poles, crown and canvas.',
  'Surfaces · Listener/Absorption': 'How much the surfaces soak up. Low = live and splashy, high = dry and dead.',
  'Surfaces · Listener/Facing':
    'Which way your head points. Turning shifts the image – just like turning your head in a real room.',
  'Surfaces · Listener/Material': 'Wall material: sets the frequency response and scattering of the reflections.',
  'Surfaces · Listener/Head': 'Turn your head towards the Rotary.',
  'Circus Tent/Ø Round': 'Diameter of the round ends.',
  'Circus Tent/Mid section': 'Straight section between the two round ends (0 = round tent).',
  'Circus Tent/Pole dist.': 'Distance between the king poles.',
  'Circus Tent/Dome Ø': 'Diameter of the dome between the poles.',
  'Circus Tent/Crown ht.': 'Height of the crown (where the canvas meets the poles).',
  'Circus Tent/Crown band': 'Height of the crown band.',
  'Circus Tent/Peak': 'Height of the peaks above the crown.',
  'Circus Tent/Wall ht.': 'Height of the side walls.',
  'Circus Tent/Sag': 'How much the canvas sags between the poles.',
  'Circus Tent/Ring Ø': 'Diameter of the ring.',
  'Circus Tent/Audience': 'How full the tent is. People absorb a lot – a full house sounds much drier.',
  'Circus Tent/Ground': 'What the tent stands on: sets the floor reflection.',
  'Circus Tent/Ring floor': 'Floor inside the ring.',
  'Circus Tent/Rays': 'Ray tracer status: found paths and computation time.',
  'Engine · Presets/Reflections':
    'Image-source order: how many bounces are rendered as discrete reflections. More = denser and more realistic, but more CPU.',
  'Engine · Presets/Precision':
    'Fast: from the 2nd order, reflections are grouped by direction. Exact: every reflection per sample with position, Doppler, directivity and panning – very CPU-heavy from the 4th order.',
  'Engine · Presets/Paths': 'Number of rendered sound paths.',
  'Engine · Presets/Load room': 'Load a room preset (keeps the Rotary and listener positions).',
  '*/CPU':
    'Audio CPU load. Running out of headroom? Raise the buffer, lower the reflections or set the power amp to HQ or Eco.',
  // Mics
  'Horn Mics/Angle':
    'Where the horn mics sit around the cabinet: 0° = front. From the back (180°) you get more motor, from the front more cabinet.',
  'Horn Mics/Distance':
    'Distance from the cabinet. Closer = more Doppler and more proximity effect, further = more room.',
  'Horn Mics/Height': 'Mic height relative to the horn.',
  'Horn Mics/Setup':
    'Mono: one mic. 90°/120°/180°: a stereo pair spread around the cabinet – 180° is left/right of the horn, the classic wide spin.',
  'Horn Mics/Windscreen': 'Windscreen: tames the wind rumble from the rotor blowing straight at the mic.',
  'Drum Mics/Angle': 'Where the drum mics sit around the cabinet: 0° = front, 180° = back (more motor).',
  'Drum Mics/Distance': 'Distance from the cabinet. Closer = more proximity effect.',
  'Drum Mics/Setup': 'Mono: one mic on the bass louvres. 90°/180°: a stereo pair.',
  '*/Mic':
    '57/421: dynamic, mid-forward. D112: kick mic, big lows. RE20: broadcast dynamic. 87: large-diaphragm condenser. KM 84: small-diaphragm condenser. 121: ribbon, smooth top.',
  '*/Pattern': 'Polar pattern of the 87 (multi-pattern): cardioid, omni or figure-8.',
  'Room Pair/Spacing': 'Distance between the two room mics (AB only).',
  'Room Pair/Height': 'Height of the room pair.',
  'Room Pair/Aim': 'Which way the pair points.',
  'Room Pair/Reflect.': 'Reflection order rendered for the room pair.',
  'Room Pair/Array':
    'AB: spaced omnis/cardioids, wide and airy. XY: coincident, mono-safe. ORTF: 17 cm, 110° – the compromise.',
  'Room Pair/Place in front': 'Place the room pair in front of the Rotary, aimed at it.',
  'Mixer/head': 'Listener: you stand in the room. Mics: close mics and room pair through the mixer.',
  // Output
  'Format/LFE': 'Level of the LFE (.1) channel – 5.1 only.',
  'Format/LFE x-over': 'Crossover frequency for the LFE – 5.1 only.',
  'Format/Master': 'Master output level.',
  'Format/Format': 'Speaker layout the room is rendered for: stereo, quad or 5.1.',
  'Routing/*': 'Which device output this channel goes to.',
  'Routing/Auto (1, 2, 3 …)': 'Route channels to outputs 1, 2, 3 … in order.',
  'Monitoring/Rear': 'Trim for your rear speakers (surround channels).',
  'Monitoring/Center': 'Trim for your center speaker – 5.1 only.',
  'Monitoring/Divergence':
    'Center divergence: also spreads a source in front of you onto L/R as a phantom center, if your center speaker is weaker.',
  'Check/Channel test': 'Sends a short ping to each channel in turn – check your speakers and routing.',
  'Check/Device': 'Active audio device.',
  'Check/Status': 'Warnings about the output setup.',
  // MIDI
  'Channels/Upper':
    'MIDI channel for the upper manual. Omni: everything not on the lower or pedal channel plays the upper manual.',
  'Channels/Lower': 'MIDI channel for the lower manual.',
  'Channels/Pedal': 'MIDI channel for the pedalboard.',
  'Split · Octaves/Split': 'Keyboard split: play two or three manuals from one keyboard (on the upper channel).',
  'Split · Octaves/Keyboard': 'Size of your keyboard – only for the split display.',
  'Split · Octaves/Split point': 'Where the zones begin. ● learns from the next key you press.',
  'Split · Octaves/Upper octave': 'Octave shift of the upper manual (±3).',
  'Split · Octaves/Lower octave': 'Octave shift of the lower manual (±3).',
  'Split · Octaves/Pedal octave': 'Octave shift of the pedal (±3).',
  'Controllers/Rotary': 'MIDI controller that toggles the Rotary speed.',
  'Controllers/Swell': 'MIDI controller for the swell (expression) pedal.',
  'Controllers/Devices': 'Connected MIDI inputs. Newly connected devices are enabled automatically.',
  'Controllers/MIDI Inputs …': 'Choose which MIDI inputs are active.',
  'QWERTY Keys/Plays': 'Which manual the computer keyboard plays.',
  'QWERTY Keys/Octave': 'Octave of the computer keyboard.',
  'QWERTY Keys/Keys': 'Key layout of the computer keyboard.',
  // Audio
  'Device/Driver': 'Audio driver type (ASIO, Windows Audio …).',
  'Device/Device': 'Active audio device.',
  'Device/Audio Settings …': 'Driver, device, sample rate, buffer size and outputs.',
  'Device/Bounce':
    'Max quality: when rendering offline the plugin uses 5th order, Exact and Ultra – no matter what runs in real time. As set: renders exactly what you hear.',
  'Buffer/Sample rate': 'Current sample rate.',
  'Buffer/Buffer': 'Buffer size: smaller = lower latency, larger = more CPU headroom.',
  'Buffer/Output': 'Output latency.',
  'Buffer/Processing': 'Extra latency from the tube oversampling.',
  'Buffer/Total':
    'Total latency plus the acoustic sound path from the Rotary to the listener or nearest mic – as in a real room.',
  'Outputs · CPU/Active': 'Active device outputs.',
  'Outputs · CPU/Channels': 'Channels rendered for the current format.',
  'Outputs · CPU/Dropouts': 'Skipped audio blocks.',
  'Outputs · CPU/Power amp': 'Power amp quality – the biggest CPU lever after the reflections.',
  'Outputs · CPU/Cores':
    'Room and mics run on several cores. Auto: standalone half the cores (max 8), plugin a quarter (max 4) – the DAW needs some too.',
};
// Card rows: segmented buttons (rowSeg), select (rowSel), read-only value (rowVal), button (rowBtn); sec() groups them
function rowSeg(label, opts, get, set, def) {
  const r = h('div', 'row');
  r.appendChild(h('span', 'rl', label));
  const sg = h('div', 'rseg');
  r.appendChild(sg);
  const bs = opts.map(([v, t]) => {
    const b = h('button', '', t);
    b.type = 'button';
    b.onclick = () => {
      set(v);
      c.sync();
    };
    sg.appendChild(b);
    return [v, b];
  });
  const c = {
    el: r,
    label,
    tipEl: r,
    sync() {
      const v = get();
      bs.forEach(([x, b]) => b.classList.toggle('on', x === v));
    },
    reset() {
      if (def !== undefined) {
        set(def);
        c.sync();
      }
    },
  };
  c.sync();
  return reg(c);
}
function rowSel(label, opts, get, set, def) {
  const r = h('div', 'row');
  r.appendChild(h('span', 'rl', label));
  const s = h('select');
  s.setAttribute('aria-label', label);
  r.appendChild(s);
  let sig = '';
  const fill = list => {
    const ns = JSON.stringify(list);
    if (ns === sig) return;
    sig = ns;
    s.innerHTML = '';
    list.forEach(([v, t]) => {
      const op = h('option', '', t);
      op.value = String(v);
      s.appendChild(op);
    });
  };
  fill(typeof opts === 'function' ? opts() : opts);
  s.onchange = () => {
    const raw = s.value;
    const list = typeof opts === 'function' ? opts() : opts;
    const it = list.find(x => String(x[0]) === raw);
    set(it ? it[0] : raw);
    c.sync();
    s.blur();
  };
  const c = {
    el: r,
    sel: s,
    label,
    tipEl: r,
    sync() {
      if (typeof opts === 'function') fill(opts());
      s.value = String(get());
    },
    reset() {
      if (def !== undefined) {
        set(def);
        c.sync();
      }
    },
  };
  c.sync();
  return reg(c);
}
function rowVal(label, getText) {
  const r = h('div', 'row');
  r.appendChild(h('span', 'rl', label));
  const v = h('span', 'rv');
  r.appendChild(v);
  const c = {
    el: r,
    label,
    tipEl: r,
    sync() {
      v.textContent = getText();
    },
  };
  c.sync();
  return reg(c);
}
function rowBtn(label, text, onclick) {
  const r = h('div', 'row');
  r.appendChild(h('span', 'rl', label));
  const b = h('button', 'btn', text);
  b.type = 'button';
  b.onclick = onclick;
  r.appendChild(b);
  return { el: r, btn: b, label: label || text, tipEl: r };
}
function sec(title, knobs, rows, note, opts = {}) {
  const s = h('div', 'sec');
  if (opts.head) {
    const hd = h('div', 'sth');
    if (TIPS[title + '/head']) hd.dataset.tip = TIPS[title + '/head'];
    hd.appendChild(h('span', 'st', title));
    hd.appendChild(opts.head.el.querySelector('.rseg') || opts.head.el);
    s.appendChild(hd);
  } else s.appendChild(h('span', 'st', title));
  if (knobs && knobs.length) {
    const g = h('div', 'kgrid');
    if (opts.kcols) g.style.gridTemplateColumns = `repeat(${opts.kcols}, minmax(0, 1fr))`;
    knobs.forEach(k => g.appendChild(k.el));
    s.appendChild(g);
  }
  if (rows && rows.length) {
    const g = h('div', 'rows');
    if (opts.rcols) {
      g.style.display = 'grid';
      g.style.gridTemplateColumns = `repeat(${opts.rcols}, minmax(0, 1fr))`;
      g.style.columnGap = '18px';
    }
    rows.forEach(r => g.appendChild(r.el));
    s.appendChild(g);
  }
  if (note) {
    // text, or a function for notes that depend on the state (registered for syncAll)
    const n = h('span', 'note');
    s.appendChild(n);
    if (typeof note === 'function')
      reg({
        sync() {
          n.textContent = note();
        },
      });
    else n.textContent = note;
  }
  if (opts.extra) s.appendChild(opts.extra);
  [...(knobs || []), ...(rows || [])].forEach(c => {
    // tooltips from TIPS: "Section/Label", then "*/Label"
    if (!c || !c.tipEl || c.tipEl.dataset.tip || !c.label) return;
    const t = TIPS[title + '/' + c.label] ?? TIPS[title + '/*'] ?? TIPS['*/' + c.label];
    if (t) c.tipEl.dataset.tip = t;
    else tipMissing.push(title + '/' + c.label);
  });
  if (opts.span) s.style.gridColumn = `span ${opts.span}`;
  if (opts.id) s.id = opts.id;
  return s;
}

// ---------------- Senders per DSP section ----------------
const sendDb = man => send({ type: 'drawbars', man, v: man === 0 ? S.db.upper : man === 1 ? S.db.lower : S.db.pedal });
const sendPerc = () => send({ type: 'perc', v: S.perc });
const sendChr = () => send({ type: 'character', v: S.chr });
const sendOrgan = () => send(Object.assign({ type: 'organDetail' }, S.organ));
const sendRotor = r => send(Object.assign({ type: 'rotor', r }, S.rotor[r]));
const sendAmp = () => send(Object.assign({ type: 'amp' }, S.amp));
const sendCab = () => send(Object.assign({ type: 'cabDetail' }, S.cab));
const sendOffline = () => send({ type: 'offlineMax', v: S.offlineMax });
const sendOut = () => send(Object.assign({ type: 'output' }, S.out));
const sendMidi = () => send(Object.assign({ type: 'midiChannels' }, S.midi));
const sendKeyMap = () => send({ type: 'keyMap', mode: S.split.mode, hi: S.split.hi, lo: S.split.lo, oct: S.split.oct });
const sendPre = () => send(Object.assign({ type: 'preamp' }, S.pre));
const sendMains = () => {
  send({ type: 'mains', hz: S.mains });
  send({ type: 'velocityClick', v: S.velClick });
};
const sendSpring = () => send(Object.assign({ type: 'spring' }, S.spring));
const sendPrecision = () => send({ type: 'precision', exact: S.exact });
const sendThreads = () => send({ type: 'threads', n: S.threads });
const sendMics = () => send({ type: 'mics', v: S.mics });

// ---------------- Tabs and settings cards ----------------
const TABS = [
  ['spielen', 'Play'],
  ['orgel', 'Organ'],
  ['rotary', 'Rotary'],
  ['raum', 'Room'],
  ['mikros', 'Mics'],
  ['ausgabe', 'Output'],
  ['midi', 'MIDI'],
  ['audio', 'Audio'],
];
const CARD_INFO = {
  orgel: ['Organ', 'Tone generator, contacts, preamp, spring reverb', 5],
  rotary: ['Rotary', 'Rotors, power amp, cabinet and placement', 5],
  raum: ['Room', 'Shape, surfaces and reflections', 3],
  mikros: ['Mics', 'Close mics, room pair and mixer', 5],
  ausgabe: ['Output', 'Format, routing, monitoring', 4],
  midi: ['MIDI', 'Channels, split, controllers, QWERTY keys', 3],
  audio: ['Audio', 'Device, driver and latency', 3],
};
let tab = 'spielen';
const TAB_TIP = {
  spielen: 'Play: drawbars, Rotary and room at a glance.',
  orgel: 'Tone generator, contacts, percussion, preamp, spring reverb.',
  rotary: 'Rotors, power amp, cabinet and placement.',
  raum: 'Room shape, surfaces and reflections.',
  mikros: 'Close mics, room pair and mixer.',
  ausgabe: 'Output format, routing and monitoring.',
  midi: 'Channels, split, octaves, controllers and QWERTY keys.',
  audio: 'Device, driver, latency and CPU.',
};
const tabBtns = TABS.map(([id, t]) => {
  const b = h('button', '', t);
  b.type = 'button';
  b.dataset.tip = TAB_TIP[id];
  b.onclick = () => setTab(id);
  $('tabs').appendChild(b);
  return [id, b];
});
const cardPages = {};
function setTab(id) {
  tab = id;
  tabBtns.forEach(([x, b]) => {
    b.classList.toggle('on', x === id);
    b.setAttribute('aria-current', x === id ? 'page' : 'false');
  });
  const info = CARD_INFO[id];
  $('card').hidden = !info;
  if (info) {
    $('cTitle').textContent = info[0];
    $('cSub').textContent = info[1];
  }
  Object.entries(cardPages).forEach(([k, el]) => (el.hidden = k !== id));
  $('bigRoom').hidden = id !== 'raum';
  if (id === 'raum') requestAnimationFrame(() => rooms[1].resize());
  if (id === 'audio' || id === 'ausgabe' || id === 'midi') nativeInvoke('audioInfo');
  syncAll();
}
// Long knob labels (DIVERGENCE, LFE X-OVER ...) next to each other: if any two neighbouring labels on a card
// would touch, tighten and shrink *all* labels of that card by the same amount, so the card stays uniform.
function fitKnobLabels(root) {
  const labels = [...root.querySelectorAll('.knob:not(.sm) .kl')];
  const pairs = [];
  root.querySelectorAll('.knob:not(.sm)').forEach(k => {
    const n = k.nextElementSibling;
    if (n && n.classList.contains('knob') && !n.classList.contains('sm'))
      pairs.push([k.querySelector('.kl'), n.querySelector('.kl')]);
  });
  const clash = () =>
    pairs.some(([a, b]) => {
      const A = a.getBoundingClientRect(),
        B = b.getBoundingClientRect();
      return A.width > 0 && B.width > 0 && Math.abs(A.top - B.top) < 2 && A.right + 3 * SCALE > B.left;
    });
  const apply = (fs, ls) =>
    labels.forEach(l => {
      l.style.fontSize = fs ? fs + 'px' : '';
      l.style.letterSpacing = ls;
    });
  apply(0, '');
  if (!clash()) return;
  for (let fs = 11; fs >= 8.5; fs -= 0.5) {
    apply(fs, fs === 11 ? '.2px' : '0px');
    if (!clash()) return;
  }
}
$('cClose').onclick = () => setTab('spielen');
$('openRoom').onclick = () => setTab('raum');
$('cReset').onclick = () => {
  (cardCtl[tab] || []).forEach(c => c.reset && c.reset());
  if (tab === 'raum') {
    if (S.room.shape === 'tent') applyTent(false);
    else {
      clampPos();
      send({ type: 'room', v: Object.assign({}, S.room, { absBands: null }) });
      sendPos(true);
    }
  }
  syncAll();
};
function page(id, build) {
  const p = h('div', 'secs');
  p.style.gridTemplateColumns = `repeat(${CARD_INFO[id][2]}, minmax(0, 1fr))`;
  p.hidden = true;
  curCard = id;
  build(p);
  curCard = null;
  $('cbody').appendChild(p);
  cardPages[id] = p;
}
