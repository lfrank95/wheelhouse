// Wheelhouse UI - Manuals, pedalboard, swell pedal, keyboard split, PC keyboard.
// All files in ui/js run in one shared scope, in the order listed in tools/build_ui.py.

// ---------------- Manuals (upper / lower keyboard) ----------------
const isBlack = k => [1, 3, 6, 8, 10].includes(((k % 12) + 12) % 12);
const keyEls = [[], [], []];
const active = [new Set(), new Set(), new Set()];
function buildManual(man) {
  const kb = $('kbd' + man);
  let wi = 0;
  for (let k = 0; k <= 60; k++) {
    const el = h('div', isBlack(k) ? 'kk' : 'kw');
    if (isBlack(k)) el.style.left = wi * 40 - 12 + 'px';
    else {
      el.style.left = wi * 40 + 'px';
      if (k % 12 === 0) el.textContent = 'C' + (k / 12 + 2);
      wi++;
    }
    el.dataset.k = k;
    kb.appendChild(el);
    keyEls[man][k] = el;
  }
  attachGliss(kb, man, el => el.closest && el.closest('.kw, .kk'));
}
function attachGliss(container, man, pick) {
  const ptr = new Map();
  // Mouse velocity from where the key is hit: near the front edge = fast stroke (127), near the back = slow (35)
  const velOf = (e, el) => {
    const r = el.getBoundingClientRect();
    return Math.round(35 + 92 * Math.max(0, Math.min(1, (e.clientY - r.top) / r.height)));
  };
  const vel = new Map();
  container.addEventListener('pointerdown', e => {
    const k = pick(e.target);
    if (!k) return;
    container.setPointerCapture(e.pointerId);
    const n = +k.dataset.k;
    const v = velOf(e, k);
    vel.set(e.pointerId, v);
    ptr.set(e.pointerId, n);
    noteOn(n, man, v);
    e.preventDefault();
  });
  container.addEventListener('pointermove', e => {
    if (!ptr.has(e.pointerId)) return;
    const el = document.elementFromPoint(e.clientX, e.clientY),
      k = el && pick(el),
      nk = k && container.contains(k) ? +k.dataset.k : -1,
      ok = ptr.get(e.pointerId);
    if (nk !== ok) {
      if (ok >= 0) noteOff(ok, man);
      if (nk >= 0) noteOn(nk, man, vel.get(e.pointerId) || 100);
      ptr.set(e.pointerId, nk);
    }
  });
  const up = e => {
    if (ptr.has(e.pointerId)) {
      const k = ptr.get(e.pointerId);
      if (k >= 0) noteOff(k, man);
      ptr.delete(e.pointerId);
    }
  };
  container.addEventListener('pointerup', up);
  container.addEventListener('pointercancel', up);
}
buildManual(0);
buildManual(1);

// Pedalboard: 25 wooden bars, sharps raised and black
(() => {
  const NS = 'http://www.w3.org/2000/svg',
    gk = $('pedKeys');
  const mk = (tag, a) => {
    const e = document.createElementNS(NS, tag);
    for (const k in a) e.setAttribute(k, a[k]);
    return e;
  };
  const natX = i => 43 + i * 46;
  let ni = 0;
  const nat = [],
    sharp = [];
  for (let p = 0; p <= 24; p++) {
    if (isBlack(p)) {
      const cx = natX(ni) - 8;
      sharp.push([p, cx]);
    } else {
      nat.push([p, natX(ni)]);
      ni++;
    }
  }
  nat.forEach(([p, x]) => {
    const g = mk('g', { 'data-k': p, class: 'pk' });
    g.appendChild(
      mk('rect', {
        x,
        y: 7,
        width: 30,
        height: 74,
        rx: 3,
        fill: 'url(#pedWood)',
        stroke: '#4a3820',
        'stroke-width': 0.8,
        class: 'body',
      })
    );
    g.appendChild(
      mk('rect', { x, y: 7, width: 30, height: 74, rx: 3, fill: 'url(#pedGrain)', 'pointer-events': 'none' })
    );
    g.appendChild(mk('rect', { x, y: 81, width: 30, height: 7, rx: 2, fill: '#6e5636', class: 'face' }));
    gk.appendChild(g);
    keyEls[2][p] = { g, nat: true, x };
  });
  sharp.forEach(([p, cx]) => {
    const g = mk('g', { 'data-k': p, class: 'pk' });
    g.appendChild(
      mk('rect', {
        x: cx - 9,
        y: 10,
        width: 24,
        height: 44,
        rx: 3,
        fill: '#000',
        opacity: 0.4,
        'pointer-events': 'none',
      })
    );
    g.appendChild(
      mk('rect', {
        x: cx - 11,
        y: 6,
        width: 22,
        height: 36,
        rx: 3,
        fill: 'url(#pedSharp)',
        stroke: '#000',
        'stroke-width': 0.8,
        class: 'body',
      })
    );
    g.appendChild(mk('rect', { x: cx - 11, y: 40, width: 22, height: 8, rx: 2, fill: '#4a423a', class: 'face' }));
    gk.appendChild(g);
    keyEls[2][p] = { g, nat: false };
  });
  attachGliss($('pedSvg'), 2, el => el.closest && el.closest('.pk'));
})();
function keyEl(man, k, on) {
  if (man < 2) {
    const e = keyEls[man][k];
    if (e) e.classList.toggle('act', on);
    return;
  }
  const e = keyEls[2][k];
  if (!e) return;
  const body = e.g.querySelector('.body'),
    face = e.g.querySelector('.face');
  if (e.nat) {
    body.setAttribute('fill', on ? 'url(#pedOn)' : 'url(#pedWood)');
    body.setAttribute('y', on ? 9 : 7);
    face.setAttribute('y', on ? 83 : 81);
    face.setAttribute('fill', on ? '#8a5a1c' : '#6e5636');
  } else {
    body.setAttribute('fill', on ? 'url(#pedSharpOn)' : 'url(#pedSharp)');
  }
}
function noteOn(k, man, vel = 100) {
  const max = man === 2 ? 24 : 60;
  if (k < 0 || k > max || active[man].has(k)) return;
  active[man].add(k);
  send({ type: 'noteOn', key: k, man, vel });
  keyEl(man, k, true);
}
function noteOff(k, man) {
  if (!active[man].has(k)) return;
  active[man].delete(k);
  send({ type: 'noteOff', key: k, man });
  keyEl(man, k, false);
}
function panic() {
  [0, 1, 2].forEach(m => [...active[m]].forEach(k => noteOff(k, m)));
  send({ type: 'panic' });
}
$('panic').onclick = panic;

// Swell pedal
(() => {
  const sv = $('swellSvg');
  let drag = false;
  const setFrom = e => {
    const r = sv.getBoundingClientRect();
    const v = clamp(1 - ((e.clientY - r.top) / r.height - 0.18) / 0.7, 0, 1);
    S.volume = +v.toFixed(3);
    send({ type: 'volume', v: S.volume });
    syncAll();
  };
  sv.addEventListener('pointerdown', e => {
    sv.setPointerCapture(e.pointerId);
    drag = true;
    setFrom(e);
  });
  sv.addEventListener('pointermove', e => {
    if (drag) setFrom(e);
  });
  sv.addEventListener('pointerup', () => (drag = false));
  sv.addEventListener('pointercancel', () => (drag = false));
  sv.addEventListener(
    'wheel',
    e => {
      e.preventDefault();
      S.volume = clamp(S.volume + (e.deltaY < 0 ? 0.03 : -0.03), 0, 1);
      send({ type: 'volume', v: S.volume });
      syncAll();
    },
    { passive: false }
  );
})();
// ---------------- Keyboard split and octave shift per manual ----------------
// One keyboard on the upper channel can play pedal | lower | upper; every manual can be shifted by octaves.
// The manuals show which of their keys the keyboard reaches (the rest is dimmed, the edges marked in gold).
const NOTE_N = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const noteName = n => NOTE_N[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
const KB_RANGE = { 49: [36, 84], 61: [36, 96], 73: [28, 100], 76: [28, 103], 88: [21, 108] };
const MAN_NAME = ['Upper manual', 'Lower manual', 'Pedal'];
let learnTarget = null; // 'hi' | 'lo' while waiting for a key on the MIDI keyboard
function setManOct(man, d) {
  const o = S.split.oct;
  o[man] = clamp(o[man] + d, -3, 3);
  sendKeyMap();
  syncAll();
}
function setSplit(k, v) {
  const sp = S.split;
  sp[k] = clamp(Math.round(v), 1, 126);
  if (sp.lo >= sp.hi) {
    if (k === 'hi') sp.lo = Math.max(0, sp.hi - 1);
    else sp.hi = Math.min(127, sp.lo + 1);
  }
  sendKeyMap();
  syncAll();
}
// reachable key window of a manual: [first, last] key, or null (not fed by the split keyboard: nothing dimmed)
function reach(man) {
  const sp = S.split,
    [a, b] = KB_RANGE[sp.kb] || KB_RANGE[61],
    sh = 12 * sp.oct[man];
  let lo, hi;
  if (man === 0) {
    lo = sp.mode >= 1 ? sp.hi : a;
    hi = b;
  } else if (man === 1) {
    if (sp.mode >= 1) {
      lo = sp.mode === 2 ? sp.lo : a;
      hi = sp.hi - 1;
    } else if (sh) {
      lo = a;
      hi = b;
    } else return null;
  } else {
    if (sp.mode !== 2) return null;
    lo = a;
    hi = sp.lo - 1;
  }
  const base = man === 2 ? 24 : 36,
    max = man === 2 ? 24 : 60;
  return [clamp(lo - base + sh, 0, max + 1), clamp(hi - base + sh, -1, max)];
}
function applyZones() {
  for (let man = 0; man < 3; man++) {
    const w = reach(man),
      max = man === 2 ? 24 : 60;
    let firstW = -1,
      lastW = -1;
    if (w)
      for (let k = w[0]; k <= w[1]; k++)
        if (!isBlack(k)) {
          if (firstW < 0) firstW = k;
          lastW = k;
        }
    for (let k = 0; k <= max; k++) {
      const out = !!w && (k < w[0] || k > w[1]);
      if (man < 2) {
        const e = keyEls[man][k];
        if (!e) continue;
        e.classList.toggle('out', out);
        e.classList.toggle('edgeL', !!w && k === firstW && w[0] > 0);
        e.classList.toggle('edgeR', !!w && k === lastW && w[1] < max);
      } else {
        const e = keyEls[2][k];
        if (e) e.g.setAttribute('opacity', out ? 0.42 : 1);
      }
    }
  }
}
// octave control right of each manual
for (let man = 0; man < 3; man++) {
  const box = $('ov' + man),
    up = h('button', '', '+'),
    val = h('span', 'ov', '0'),
    dn = h('button', '', '−'),
    lab = h('span', 'ol', 'OCT');
  up.type = dn.type = 'button';
  up.dataset.tip = `${MAN_NAME[man]} one octave up`;
  dn.dataset.tip = `${MAN_NAME[man]} one octave down`;
  val.dataset.tip = `Octave shift of the ${MAN_NAME[man].toLowerCase()} (±3)`;
  up.setAttribute('aria-label', up.dataset.tip);
  dn.setAttribute('aria-label', dn.dataset.tip);
  up.onclick = () => setManOct(man, 1);
  dn.onclick = () => setManOct(man, -1);
  box.append(lab, up, val, dn);
  reg({
    sync() {
      const o = S.split.oct[man];
      val.textContent = (o > 0 ? '+' : o < 0 ? '−' : '') + Math.abs(o);
      val.classList.toggle('hi', o !== 0);
    },
  });
}
// a split point chip: "Upper from ‹ C4 › ●" (● = learn from the keyboard)
function splitChip(k, label) {
  const el = h('span', 'spt'),
    t = h('span', '', label + ' from'),
    dn = h('button', '', '‹'),
    v = h('b', '', ''),
    up = h('button', '', '›'),
    ln = h('button', 'learn', '●');
  [dn, up, ln].forEach(b => (b.type = 'button'));
  dn.dataset.tip = 'Split point one semitone down';
  up.dataset.tip = 'Split point one semitone up';
  ln.dataset.tip = 'Learn: the next key you press on your keyboard becomes the split point';
  el.dataset.tip = `${label} manual starts at this key`;
  ln.setAttribute('aria-label', ln.dataset.tip);
  dn.setAttribute('aria-label', dn.dataset.tip);
  up.setAttribute('aria-label', up.dataset.tip);
  dn.onclick = () => setSplit(k, S.split[k] - 1);
  up.onclick = () => setSplit(k, S.split[k] + 1);
  ln.onclick = () => {
    learnTarget = learnTarget === k ? null : k;
    syncAll();
  };
  el.append(t, dn, v, up, ln);
  reg({
    sync() {
      v.textContent = noteName(S.split[k]);
      ln.classList.toggle('on', learnTarget === k);
    },
  });
  return el;
}
(() => {
  const g = $('splitG'),
    seg = h('span', 'pcseg'),
    lab = h('span', '', 'Split');
  lab.dataset.tip = 'Keyboard split: play several manuals from one keyboard (on the upper channel).';
  const btns = [
    [0, 'Off', 'No split'],
    [1, '2', '2 zones: Lower | Upper'],
    [2, '3', '3 zones: Pedal | Lower | Upper'],
  ].map(([v, t, ti]) => {
    const b = h('button', '', t);
    b.type = 'button';
    b.dataset.tip = ti;
    b.onclick = () => {
      S.split.mode = v;
      learnTarget = null;
      sendKeyMap();
      syncAll();
    };
    seg.appendChild(b);
    return [v, b];
  });
  const cLo = splitChip('lo', 'Lower'),
    cHi = splitChip('hi', 'Upper');
  g.append(lab, seg, cLo, cHi);
  reg({
    sync() {
      btns.forEach(([v, b]) => b.classList.toggle('on', v === S.split.mode));
      cHi.style.display = S.split.mode >= 1 ? '' : 'none';
      cLo.style.display = S.split.mode === 2 ? '' : 'none';
      applyZones();
    },
  });
})();
const pcBtns = [
  [0, 'Upper'],
  [1, 'Lower'],
  [2, 'Pedal'],
].map(([v, t]) => {
  const b = h('button', '', t);
  b.type = 'button';
  b.onclick = () => {
    releasePc();
    S.pcMan = v;
    syncAll();
    if (ready) scheduleSave();
  };
  $('pcSeg').appendChild(b);
  return [v, b];
});
reg({
  sync() {
    $('swBar').style.width = S.volume * 100 + '%';
    const hh = 62 * S.volume;
    $('swFill').setAttribute('y', (80 - hh).toFixed(1));
    $('swFill').setAttribute('height', hh.toFixed(1));
    $('swellSvg').setAttribute('aria-valuenow', Math.round(S.volume * 100));
    const ch = (v, omni) => (v === 0 ? (omni ? 'Omni' : 'off') : 'Ch ' + v);
    $('chanInfo').innerHTML =
      `Upper <span class="hi">${ch(S.midi.upper, true)}</span> · Lower <span class="hi">${ch(S.midi.lower)}</span> · Pedal <span class="hi">${ch(S.midi.pedal)}</span>`;
    const md = audioInfo.midiDevices || [];
    $('midiInfo').innerHTML = md.length
      ? `${md
          .slice(0, 2)
          .map(n => n.replace(/[<>&]/g, ''))
          .join(' · ')} <span class="hi">●</span>`
      : 'no MIDI device';
    $('octV').textContent = (S.octave > 0 ? '+' : '') + S.octave;
    pcBtns.forEach(([v, b]) => b.classList.toggle('on', v === S.pcMan));
  },
});

// PC keyboard as a MIDI substitute
const KEYMAP = {
  KeyZ: 0,
  KeyS: 1,
  KeyX: 2,
  KeyD: 3,
  KeyC: 4,
  KeyV: 5,
  KeyG: 6,
  KeyB: 7,
  KeyH: 8,
  KeyN: 9,
  KeyJ: 10,
  KeyM: 11,
  Comma: 12,
  KeyL: 13,
  Period: 14,
  KeyQ: 12,
  Digit2: 13,
  KeyW: 14,
  Digit3: 15,
  KeyE: 16,
  KeyR: 17,
  Digit5: 18,
  KeyT: 19,
  Digit6: 20,
  KeyY: 21,
  Digit7: 22,
  KeyU: 23,
  KeyI: 24,
  Digit9: 25,
  KeyO: 26,
  Digit0: 27,
  KeyP: 28,
};
const pcHeld = new Map();
function releasePc() {
  pcHeld.forEach(([k, m]) => noteOff(k, m));
  pcHeld.clear();
}
window.addEventListener('keydown', e => {
  if (e.target && e.target.tagName === 'SELECT') return;
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  if (e.repeat) {
    if (e.code === 'Space' || KEYMAP[e.code] !== undefined) e.preventDefault();
    return;
  }
  if (e.code === 'Space') {
    e.preventDefault();
    setRotary(S.rotary === 'fast' ? 'slow' : 'fast');
    return;
  }
  if (e.code === 'Escape') {
    panic();
    if (tab !== 'spielen') setTab('spielen');
    return;
  }
  if (KEYMAP[e.code] !== undefined) {
    e.preventDefault();
    const m = S.pcMan,
      k = (m === 2 ? 0 : 12) + S.octave * 12 + KEYMAP[e.code];
    pcHeld.set(e.code, [k, m]);
    noteOn(k, m);
  }
});
window.addEventListener('keyup', e => {
  if (pcHeld.has(e.code)) {
    const [k, m] = pcHeld.get(e.code);
    noteOff(k, m);
    pcHeld.delete(e.code);
  }
});
window.addEventListener('blur', releasePc);
const setOct = d => {
  releasePc();
  S.octave = clamp(S.octave + d, -1, 2);
  syncAll();
  if (ready) scheduleSave();
};
$('octDn').onclick = () => setOct(-1);
$('octUp').onclick = () => setOct(1);
