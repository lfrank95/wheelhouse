// Wheelhouse UI - Full state push, events from the native app, animation loop, startup, tooltips.
// All files in ui/js run in one shared scope, in the order listed in tools/build_ui.py.

// ---------------- Push the whole state to the DSP ----------------
function pushAll() {
  if (S.room.shape === 'tent') applyTent(false);
  else send({ type: 'room', v: Object.assign({}, S.room, { absBands: null }) });
  send({ type: 'pos', rotaryPos: S.rotaryPos, listener: S.listener, jump: true });
  [0, 1, 2].forEach(sendDb);
  sendPerc();
  send({ type: 'vib', v: S.vib });
  send({ type: 'drive', v: S.drive });
  send({ type: 'click', v: S.click });
  send({ type: 'volume', v: S.volume });
  send({ type: 'balance', v: S.balance });
  send({ type: 'lfe', v: S.lfe });
  send({ type: 'rotary', v: S.rotary });
  sendChr();
  send({ type: 'cabYaw', v: S.cabYaw, jump: true });
  sendOrgan();
  sendRotor(0);
  sendRotor(1);
  send({ type: 'brake', v: S.brake });
  sendCab();
  sendAmp();
  sendOut();
  sendMidi();
  sendPre();
  sendMains();
  sendSpring();
  sendPrecision();
  sendThreads();
  sendMics();
  sendOffline();
  sendKeyMap();
  applyMode();
}

// ---------------- Events from the native app ----------------
if (NATIVE) {
  const B = window.__JUCE__.backend;
  B.addEventListener('rot', m => {
    // The events arrive irregularly (the WebView delivers them in bursts), so the drawn rotors run on their own
    // clock and are only pulled gently towards the reported angles; a big difference (start, stop) snaps.
    {
      const now = performance.now(),
        dt = (now - S.rot.t) / 1000;
      const pull = (shown, hz, reported) => {
        const cur = shown + hz * 2 * Math.PI * dt;
        let e = reported - cur;
        e -= Math.round(e / (2 * Math.PI)) * 2 * Math.PI;
        return Math.abs(e) > 1.2 ? reported : cur + e * 0.2;
      };
      S.rot.horn = pull(S.rot.horn, S.rot.hornHz, m.horn);
      S.rot.drum = pull(S.rot.drum, S.rot.drumHz, m.drum);
      S.rot.hornHz = m.hornHz;
      S.rot.drumHz = m.drumHz;
      S.rot.t = now;
    }
    S.meters = m.meters;
    if (m.cpu !== undefined) {
      cpuText = Math.round(m.cpu * 100) + '%';
      $('cpu').textContent = cpuText;
      $('stCpu').textContent = cpuText;
    }
    if (m.vb !== undefined) ampText = `${Math.round(m.vb)} V / ${nf(m.vk)} V`;
    if (m.mm) S.micMeters = m.mm;
    if (m.procLatency !== undefined) procLatency = m.procLatency;
    if (m.dropped !== undefined) droppedBlocks = m.dropped;
    if (tab !== 'spielen') {
      slowSync++;
      if (slowSync % 10 === 0) syncAll();
    }
  });
  B.addEventListener('roomInfo', m => {
    roomInfo = m;
    syncAll();
  });
  // plugin: the host automated a parameter (the engine has already applied it)
  B.addEventListener('param', m => {
    applyParam(m.id, m.v);
    syncAll();
    scheduleSave();
  });
  B.addEventListener('audio', m => {
    audioInfo = Object.assign(audioInfo, m);
    updateChanInfo();
    syncAll();
  });
  B.addEventListener('resend', () => pushAll());
  B.addEventListener('ping', () => nativeInvoke('pong')); // watchdog: the host rebuilds the view if this stops
  B.addEventListener('midi', m => {
    if (learnTarget && (m.type === 'noteOn' || m.type === 'noteOut') && m.note !== undefined) {
      setSplit(learnTarget, m.note);
      learnTarget = null;
      syncAll();
    }
    if (m.type === 'noteOn' || m.type === 'noteOff') {
      const man = m.man | 0;
      if (man < 0) return;
      if (m.type === 'noteOn') {
        active[man].add(m.key);
        keyEl(man, m.key, true);
      } else {
        active[man].delete(m.key);
        keyEl(man, m.key, false);
      }
    } else if (m.type === 'rotary') {
      S.rotary = m.v;
      syncAll();
    } else if (m.type === 'volume') {
      S.volume = m.v;
      syncAll();
    } else if (m.type === 'panic') {
      [0, 1, 2].forEach(man => {
        active[man].forEach(k => keyEl(man, k, false));
        active[man].clear();
      });
    }
  });
}

// ---------------- Animation loop (rotor angles, meters) ----------------
let lastRpm = '';
function frame() {
  const now = performance.now(),
    dt = (now - S.rot.t) / 1000;
  const hornA = S.rot.horn + S.rot.hornHz * 2 * Math.PI * dt,
    drumA = S.rot.drum + S.rot.drumHz * 2 * Math.PI * dt;
  const rpm = Math.round(S.rot.hornHz * 60) + '|' + Math.round(S.rot.drumHz * 60);
  if (rpm !== lastRpm) {
    lastRpm = rpm;
    $('hornRpm').textContent = Math.round(Math.abs(S.rot.hornHz) * 60);
    $('drumRpm').textContent = Math.round(Math.abs(S.rot.drumHz) * 60);
  }
  drawCabinet(hornA, drumA);
  if (tab === 'raum') rooms[1].draw(hornA, drumA);
  else rooms[0].draw(hornA, drumA);
  drawMeters();
  if (tab === 'mikros' && S.micMeters)
    micMeterEls.forEach((b, c) => {
      const db = S.micMeters[c];
      micHold[c] = Math.max(db, micHold[c] - 0.6);
      b.style.height = clamp((micHold[c] + 60) / 60, 0, 1) * 100 + '%';
    });
  requestAnimationFrame(frame);
}

// ---------------- Startup ----------------
window.addEventListener('resize', fit);
// Older saved states used German value names; map them to the current ones
function migrate(o) {
  const V = {
    holz: 'wood',
    beton: 'concrete',
    stein: 'stone',
    glas: 'glass',
    zelt: 'tent',
    quader: 'box',
    wiese: 'grass',
    schotter: 'gravel',
    sandplatz: 'sandlot',
    pflaster: 'paving',
    holzboden: 'woodfloor',
    schnee: 'snow',
    untergrund: 'ground',
  };
  const fix = (obj, k) => {
    if (obj && typeof obj[k] === 'string' && V[obj[k]]) obj[k] = V[obj[k]];
  };
  fix(o.room, 'material');
  fix(o.room, 'shape');
  fix(o.tent, 'ground');
  if (o.tent) {
    const T = o.tent,
      R = { man: 'ring', kup: 'dome', aud: 'audience', sagp: 'sagPct', manege: 'ringFloor' };
    for (const k in R) if (k in T && !(R[k] in T)) T[R[k]] = T[k];
    fix(T, 'ringFloor');
  }
  if (o.cab && 'hornfach' in o.cab && !('hornBox' in o.cab)) o.cab.hornBox = o.cab.hornfach;
  if (o.rotaryPos === undefined && o.lesliePos) o.rotaryPos = o.lesliePos; // saved by versions before the rename
  if (o.rotary === undefined && typeof o.leslie === 'string') o.rotary = o.leslie;
  if (o.midi && o.midi.rotaryCC === undefined && o.midi.leslieCC !== undefined) o.midi.rotaryCC = o.midi.leslieCC;
  delete o.lesliePos;
  delete o.leslie;
  if (o.midi) delete o.midi.leslieCC;
  if (o.rotaryPos === undefined && o.les) o.rotaryPos = o.les;
  if (o.listener === undefined && o.lis) o.listener = o.lis;
  return o;
}
// Plugin parameters (automation) -> UI state
const VIB_IDS = ['v1', 'v2', 'v3', 'c1', 'c2', 'c3'],
  LES_IDS = ['stop', 'slow', 'fast'];
function applyParam(id, v) {
  let m;
  if ((m = /^db_([ulp])_(\d)$/.exec(id))) {
    S.db[{ u: 'upper', l: 'lower', p: 'pedal' }[m[1]]][+m[2]] = Math.round(v);
    return;
  }
  const b = v > 0.5;
  switch (id) {
    case 'leslie':
      S.rotary = LES_IDS[Math.round(v)] || 'slow';
      break; // host parameter ID kept from earlier versions
    case 'swell':
      S.volume = v;
      break;
    case 'drive':
      S.drive = v;
      break;
    case 'vib':
      S.vib = VIB_IDS[Math.round(v)] || 'c3';
      break;
    case 'vib_upper':
      S.perc.vibU = b;
      break;
    case 'vib_lower':
      S.perc.vibL = b;
      break;
    case 'perc_on':
      S.perc.on = b;
      break;
    case 'perc_soft':
      S.perc.soft = b;
      break;
    case 'perc_fast':
      S.perc.fast = b;
      break;
    case 'perc_third':
      S.perc.third = b;
      break;
  }
}
let loadedState = false;
async function start() {
  if (NATIVE) {
    try {
      const txt = await nativeInvoke('loadUi');
      if (txt) {
        deepMerge(S, migrate(JSON.parse(txt)));
        loadedState = true;
      }
    } catch (e) {
      console.warn('Saved state unreadable', e);
    }
  }
  // plugin: the project's parameter values win over the stored UI state
  const pv = NATIVE ? await nativeInvoke('params') : null;
  if (pv) {
    if (!loadedState) {
      muteSend = true;
      applyInstrument(0);
      muteSend = false;
    }
    loadedState = true;
    Object.entries(pv).forEach(([id, v]) => applyParam(id, v));
  }
  if (!loadedState) applyInstrument(0); // first start: B3 + 122 as in the original
  if (S.rotary === 'stop' && S.halfMoon !== 'stop') S.rotary = 'slow';
  fit();
  setTab('spielen');
  buildMeters();
  pushAll();
  ready = true;
  nativeInvoke('audioInfo');
  requestAnimationFrame(frame);
}
// ---------------- Tooltips ----------------
// One styled tooltip for everything with data-tip (title attributes are migrated so the native one never doubles up).
(() => {
  const tip = $('tip');
  let cur = null,
    timer = 0,
    down = false;
  const hide = () => {
    clearTimeout(timer);
    timer = 0;
    tip.classList.remove('show');
    cur = null;
  };
  const target = el => {
    for (; el && el !== document.body; el = el.parentElement) {
      if (el.title && el.tagName !== 'OPTION') {
        el.dataset.tip = el.dataset.tip || el.title;
        el.removeAttribute('title');
      }
      if (el.dataset && el.dataset.tip) return el;
    }
    return null;
  };
  const place = el => {
    tip.textContent = el.dataset.tip;
    if (el.dataset.tipK) {
      const k = document.createElement('span');
      k.className = 'k';
      k.textContent = el.dataset.tipK;
      tip.appendChild(k);
    }
    const r = el.getBoundingClientRect(),
      tw = tip.offsetWidth,
      th = tip.offsetHeight,
      W = innerWidth,
      H = innerHeight;
    let x = clamp(r.left + r.width / 2 - tw / 2, 6, W - tw - 6),
      y = r.bottom + 8;
    if (y + th > H - 6) y = Math.max(6, r.top - th - 8);
    tip.style.left = x + 'px';
    tip.style.top = y + 'px';
    tip.classList.add('show');
  };
  document.addEventListener('pointerover', e => {
    const el = target(e.target);
    if (el === cur) return;
    hide();
    if (!el || down) return;
    cur = el;
    timer = setTimeout(() => {
      if (cur === el && el.isConnected) place(el);
    }, 450);
  });
  document.addEventListener('pointerout', e => {
    if (cur && !cur.contains(e.relatedTarget)) hide();
  });
  document.addEventListener(
    'pointerdown',
    () => {
      down = true;
      hide();
    },
    true
  );
  document.addEventListener(
    'pointerup',
    () => {
      down = false;
    },
    true
  );
  ['wheel', 'keydown', 'scroll'].forEach(n => document.addEventListener(n, hide, { capture: true, passive: true }));
  window.addEventListener('blur', hide);
})();
start();
