// Wheelhouse UI - Drawbars, front controls and the settings cards (Organ, Rotary, Room, Mics, Output, MIDI, Audio).
// All files in ui/js run in one shared scope, in the order listed in tools/build_ui.py.

// ---------------- Drawbars ----------------
const FT = ["16'", "5⅓'", "8'", "4'", "2⅔'", "2'", "1⅗'", "1⅓'", "1'"];
const KIND = ['brown', 'brown', 'white', 'white', 'black', 'white', 'black', 'black', 'white'];
const LOOK = {
  brown: { h: 'linear-gradient(180deg, #8a5030, #5e3018)', sh: '#6b3d22', n: '#f3dcc6' },
  white: { h: 'linear-gradient(180deg, #faf6ee, #d6ccb9)', sh: '#d8cfbf', n: '#2a2218' },
  black: { h: 'linear-gradient(180deg, #34302b, #121010)', sh: '#2a2622', n: '#e8dfcf' },
  pedal: { h: 'linear-gradient(180deg, #a05a2c, #6a3416)', sh: '#7a4422', n: '#f3dcc6' },
  muted: { h: 'linear-gradient(180deg, #5a554e, #3a3631)', sh: '#3a3530', n: '#a8a092' },
};
const DB_TIP = [
  '16′ – sub-octave, an octave below the fundamental.',
  '5⅓′ – sub-third: the fifth above the sub-octave.',
  '8′ – fundamental, the pitch you play.',
  '4′ – octave.',
  '2⅔′ – twelfth (octave + fifth).',
  '2′ – fifteenth (two octaves).',
  '1⅗′ – seventeenth (two octaves + major third).',
  '1⅓′ – nineteenth (two octaves + fifth).',
  '1′ – twenty-second (three octaves).',
];
DB_TIP.p0 = 'Pedal 16′ – fundamental from the complex tone wheels plus the lower harmonics: the foundation.';
DB_TIP.p1 = 'Pedal 8′ – upper even harmonics only: brighter, more punch.';
function dbGroup(name, key, man, fts, kinds, presets) {
  const g = h('div', 'dbg');
  const hd = h('div', 'dbgh');
  hd.appendChild(h('span', '', name));
  g.appendChild(hd);
  let psel = null;
  if (presets) {
    psel = h('select');
    psel.setAttribute('aria-label', name + ' registration');
    psel.dataset.tip = 'Registration presets for the ' + name.toLowerCase() + (name === 'Pedal' ? '.' : ' manual.');
    const o0 = h('option', '', '–');
    o0.value = '';
    o0.disabled = true;
    psel.appendChild(o0);
    presets.forEach(([n], i) => {
      const o = h('option', '', n);
      o.value = i;
      psel.appendChild(o);
    });
    psel.onchange = () => {
      psel.blur();
      const p = presets[+psel.value];
      if (!p) return;
      S.db[key] = p[1].slice();
      sendDb(man);
      syncAll();
    };
    hd.appendChild(psel);
  }
  const bars = h('div', 'dbbars');
  g.appendChild(bars);
  const els = fts.map((ft, i) => {
    const c = h('div', 'dbc');
    c.appendChild(h('span', 'ft', ft));
    const t = h('button', 'dbt', '<span class="sh"></span><span class="hd"></span>');
    t.type = 'button';
    c.appendChild(t);
    bars.appendChild(c);
    const setV = v => {
      v = clamp(v, 0, 8);
      if (v !== S.db[key][i]) {
        S.db[key][i] = v;
        sendDb(man);
        syncAll();
      }
    };
    const fromY = y => {
      const r = t.getBoundingClientRect();
      return Math.round(((y - r.top) / SCALE - 21) / 18);
    };
    let drag = false;
    t.addEventListener('pointerdown', e => {
      t.setPointerCapture(e.pointerId);
      drag = true;
      setV(fromY(e.clientY));
    });
    t.addEventListener('pointermove', e => {
      if (drag) setV(fromY(e.clientY));
    });
    t.addEventListener('pointerup', () => (drag = false));
    t.addEventListener('pointercancel', () => (drag = false));
    t.addEventListener(
      'wheel',
      e => {
        e.preventDefault();
        setV(S.db[key][i] + (e.deltaY > 0 ? 1 : -1));
      },
      { passive: false }
    );
    t.addEventListener('keydown', e => {
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        e.stopPropagation();
        setV(S.db[key][i] + 1);
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        e.stopPropagation();
        setV(S.db[key][i] - 1);
      }
    });
    return { t, sh: t.querySelector('.sh'), hd: t.querySelector('.hd'), ft, kind: kinds[i] };
  });
  reg({
    sync() {
      els.forEach((e, i) => {
        const v = S.db[key][i],
          muted = man === 0 && i === 8 && S.perc.on,
          lk = LOOK[muted ? 'muted' : e.kind],
          shaft = 6 + v * 18;
        e.sh.style.height = shaft + 'px';
        e.sh.style.background = lk.sh;
        e.hd.style.top = shaft - 2 + 'px';
        e.hd.style.background = lk.h;
        e.hd.style.color = lk.n;
        e.hd.textContent = v;
        e.t.setAttribute('aria-label', `${name} ${e.ft} drawbar ${v}` + (muted ? ', muted by percussion' : ''));
        e.t.dataset.tip = muted
          ? '1′ is muted while percussion is on – just like the original.'
          : DB_TIP[man === 2 ? 'p' + i : i];
        e.t.dataset.tipK = 'Drag or scroll · 0 = in, 8 = full out';
      });
      if (psel) {
        const i = presets.findIndex(p => p[1].join() === S.db[key].join());
        psel.value = i >= 0 ? String(i) : '';
      }
    },
  });
  $('dbgroups').appendChild(g);
}
dbGroup('Upper', 'upper', 0, FT, KIND, UPPER_PRESETS);
dbGroup('Pedal', 'pedal', 2, ["16'", "8'"], ['pedal', 'pedal'], null);
dbGroup('Lower', 'lower', 1, FT, KIND, LOWER_PRESETS);

// Front knobs and rocker tablets
const TIP_VIB =
  'Scanner vibrato/chorus. V1–V3: vibrato in three depths. C1–C3: chorus (vibrato mixed with the dry signal). C3 is the classic.';
const TIP_DRIVE = 'How hard the organ hits the Rotary power amp – from clean to growl.';
const fk = $('frontKnobs');
fk.appendChild(
  knob({
    label: 'Vibrato',
    tip: TIP_VIB,
    opts: VIB,
    get: () => S.vib,
    set: v => {
      S.vib = v;
      send({ type: 'vib', v });
      markPreset();
    },
    def: 'c3',
  }).el
);
fk.appendChild(
  knob({
    label: 'Drive',
    tip: TIP_DRIVE,
    min: 0,
    max: 1,
    def: 0.35,
    fmt: pct,
    get: () => S.drive,
    set: v => {
      S.drive = v;
      send({ type: 'drive', v });
      syncAll();
    },
  }).el
);
fk.appendChild(
  knob({
    label: 'Reverb',
    tip: 'Spring reverb mix (spring tank inside the organ – it spins through the Rotary).',
    min: 0,
    max: 1,
    def: 0.2,
    fmt: v => (v < 0.005 ? 'off' : pct(v)),
    get: () => S.spring.mix,
    set: v => {
      S.spring.mix = v;
      sendSpring();
      syncAll();
    },
  }).el
);
fk.appendChild(
  knob({
    label: 'Swell',
    tip: 'Swell / expression pedal: volume, plus the bass boost of the original pedal when it is closed down.',
    min: 0,
    max: 1,
    def: 0.8,
    fmt: pct,
    get: () => S.volume,
    set: v => {
      S.volume = v;
      send({ type: 'volume', v });
      syncAll();
    },
  }).el
);
const TB = [
  [
    'on',
    'PERC\nON',
    'PERC\nOFF',
    'Percussion',
    'Percussion on/off: a decaying 2nd or 3rd harmonic on the attack, upper manual only, single-trigger (retriggers only after all keys are released). Mutes the 1′ drawbar, like the original.',
  ],
  [
    'soft',
    'PERC\nSOFT',
    'PERC\nNORM',
    'Percussion volume',
    'Percussion volume: Soft or Normal. Normal also pulls the upper manual down a little – just like the real thing.',
  ],
  [
    'fast',
    'PERC\nFAST',
    'PERC\nSLOW',
    'Percussion decay',
    'Percussion decay: Fast or Slow (times in Organ › Percussion).',
  ],
  [
    'third',
    'PERC\n3RD',
    'PERC\n2ND',
    'Percussion harmonic',
    'Percussion harmonic: 3rd (2⅔′ – the Jimmy Smith sound) or 2nd (4′).',
  ],
  ['vibU', 'VIB\nUPPER', 'VIB\nUPPER', 'Vibrato upper manual', 'Vibrato/chorus on the upper manual.'],
  ['vibL', 'VIB\nLOWER', 'VIB\nLOWER', 'Vibrato lower manual', 'Vibrato/chorus on the lower manual.'],
];
TB.forEach(([k, on, off, aria, tip]) => {
  const b = h('button', 'tablet', '<span></span>');
  b.type = 'button';
  b.setAttribute('aria-label', aria);
  b.dataset.tip = tip;
  b.onclick = () => {
    S.perc[k] = !S.perc[k];
    sendPerc();
    syncAll();
  };
  $('tablets').appendChild(b);
  reg({
    sync() {
      const v = !!S.perc[k];
      b.classList.toggle('on', v);
      b.setAttribute('aria-pressed', v);
      b.firstChild.textContent = v ? on : off;
    },
  });
});

// ---------------- Card: Organ ----------------
page('orgel', p => {
  const condK = (id, label, def) =>
    knob({
      label,
      min: 0,
      max: 1,
      def,
      fmt: pct,
      get: () => S.chr[id],
      set: v => {
        S.chr[id] = v;
        if (id === 'cond') {
          S.chr.leak = v;
          S.chr.contact = v;
          S.chr.motor = Math.min(1, v * 0.8 + 0.1);
        }
        sendChr();
        syncAll();
      },
    });
  p.appendChild(
    sec(
      'Character',
      [
        condK('cond', 'Condition', 0.5),
        condK('leak', 'Leakage', 0.5),
        condK('contact', 'Contacts', 0.5),
        condK('motor', 'Motor', 0.4),
      ],
      [],
      'Condition pulls the other three along and sets the level spread and runout of the tone wheels: from showroom-fresh to 40 years on the road.'
    )
  );
  p.appendChild(
    sec(
      'Generator',
      [
        knob({
          label: 'Key noise',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: v => (v < 0.005 ? 'off' : pct(v)),
          get: () => S.organ.keyNoise,
          set: v => {
            S.organ.keyNoise = v;
            sendOrgan();
          },
        }),
      ],
      [
        rowSeg(
          'Tuning',
          [
            [true, 'Gears'],
            [false, 'Equal'],
          ],
          () => S.chr.gearTune,
          v => {
            S.chr.gearTune = v;
            sendChr();
          },
          true
        ),
        rowSeg(
          'Busbar load',
          [
            [true, 'on'],
            [false, 'off'],
          ],
          () => S.chr.busComp,
          v => {
            S.chr.busComp = v;
            sendChr();
          },
          true
        ),
        rowSeg(
          'Low wheels',
          [
            [true, 'Complex'],
            [false, 'Sine'],
          ],
          () => S.organ.complex,
          v => {
            S.organ.complex = v;
            sendOrgan();
          },
          true
        ),
        rowSeg(
          'Mains',
          [
            [50, '50 Hz'],
            [60, '60 Hz'],
          ],
          () => S.mains,
          v => {
            S.mains = v;
            sendMains();
          },
          50
        ),
      ],
      'Low wheels: nearly square, pedal only. Key noise: keys bottoming out on the felt – the console sits right in front of you.'
    )
  );
  p.appendChild(
    sec(
      'Percussion · Vibrato · Click',
      [
        knob({
          label: 'Key click',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: pct,
          get: () => S.click,
          set: v => {
            S.click = v;
            send({ type: 'click', v });
          },
        }),
        knob({
          label: 'Perc level',
          min: 0,
          max: 1.5,
          def: 1,
          fmt: pct,
          get: () => S.organ.percLevel,
          set: v => {
            S.organ.percLevel = v;
            sendOrgan();
          },
        }),
        knob({
          label: 'Fast decay',
          min: 0.1,
          max: 0.6,
          def: 0.2,
          step: 0.01,
          fmt: v => nf(v, 2) + ' s',
          get: () => S.organ.percShort,
          set: v => {
            S.organ.percShort = v;
            sendOrgan();
          },
        }),
        knob({
          label: 'Slow decay',
          min: 0.5,
          max: 2.5,
          def: 1.0,
          step: 0.05,
          fmt: v => nf(v, 1) + ' s',
          get: () => S.organ.percLong,
          set: v => {
            S.organ.percLong = v;
            sendOrgan();
          },
        }),
      ],
      [
        rowSeg(
          'Vibrato',
          VIB,
          () => S.vib,
          v => {
            S.vib = v;
            send({ type: 'vib', v });
            syncAll();
          },
          'c3'
        ),
        rowSeg(
          'Click',
          [
            [true, 'Velocity'],
            [false, 'Fixed'],
          ],
          () => S.velClick,
          v => {
            S.velClick = v;
            sendMains();
          },
          true
        ),
      ],
      'Velocity: a fast stroke closes all nine contacts almost at once and hard, a slow press one after another and softer.'
    )
  );
  p.appendChild(
    sec(
      'Preamp AO-28',
      [
        knob({
          label: 'Gain',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: pct,
          get: () => S.pre.gain,
          set: v => {
            S.pre.gain = v;
            sendPre();
          },
        }),
      ],
      [
        rowSeg(
          'Tubes',
          [
            [true, 'on'],
            [false, 'bypass'],
          ],
          () => S.pre.on,
          v => {
            S.pre.on = v;
            sendPre();
          },
          true
        ),
        rowVal('Quality', () => ['Eco', 'HQ', 'Ultra'][S.amp.quality] + ' (= power amp)'),
      ],
      'Post-swell: 12AX7 and a 12BH7 push-pull into a transformer. Swell wide open and full drawbars – the organ already breaks up before the Rotary.'
    )
  );
  const kickBtn = rowBtn('Tank', 'Kick', () => send({ type: 'springKick', v: 1 }));
  p.appendChild(
    sec(
      'Spring Reverb',
      [
        knob({
          label: 'Mix',
          min: 0,
          max: 1,
          def: 0.2,
          fmt: v => (v < 0.005 ? 'off' : pct(v)),
          get: () => S.spring.mix,
          set: v => {
            S.spring.mix = v;
            sendSpring();
            syncAll();
          },
        }),
        knob({
          label: 'Decay',
          min: 1,
          max: 5,
          step: 0.1,
          def: 2.8,
          fmt: v => nf(v, 1) + ' s',
          get: () => S.spring.decay,
          set: v => {
            S.spring.decay = v;
            sendSpring();
          },
        }),
        knob({
          label: 'Tone',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: v => (v < 0.33 ? 'dark' : v > 0.66 ? 'bright' : 'mid'),
          get: () => S.spring.tone,
          set: v => {
            S.spring.tone = v;
            sendSpring();
          },
        }),
        knob({
          label: 'Drive',
          min: 0,
          max: 1,
          def: 0.3,
          fmt: pct,
          get: () => S.spring.drive,
          set: v => {
            S.spring.drive = v;
            sendSpring();
          },
        }),
      ],
      [
        rowSeg(
          'Springs',
          [
            [2, '2'],
            [3, '3'],
          ],
          () => S.spring.springs,
          v => {
            S.spring.springs = v;
            sendSpring();
          },
          3
        ),
        kickBtn,
      ],
      'Organ spring tank as in the A-100 and 122RV: inside the organ, before the Rotary – so the reverb spins too.'
    )
  );
});

// ---------------- Card: Rotary ----------------
const rpmFmt = v => Math.round(v * 60) + ' rpm';
const secFmt = v => nf(v, 1) + ' s';
function cabDeg() {
  let d = Math.round(((S.cabYaw - Math.PI / 2) * 180) / Math.PI);
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return d;
}
function setCabYaw(rad, jump) {
  S.cabYaw = rad;
  send({ type: 'cabYaw', v: rad, jump: !!jump });
  syncAll();
}
page('rotary', p => {
  const rk = (r, k, label, min, max, def, fmt, log) =>
    knob({
      label,
      min,
      max,
      def,
      fmt,
      log,
      get: () => S.rotor[r][k],
      set: v => {
        S.rotor[r][k] = v;
        sendRotor(r);
      },
    });
  p.appendChild(
    sec(
      'Horn',
      [
        rk(0, 'slow', 'Slow', 0.5, 1.2, 0.8, rpmFmt),
        rk(0, 'fast', 'Fast', 5.5, 7.6, 6.67, rpmFmt),
        rk(0, 'up', 'Rise', 0.2, 3, 0.5, secFmt, true),
        rk(0, 'down', 'Fall', 0.3, 4, 0.85, secFmt, true),
      ],
      [],
      'The light horn gets up to speed in under a second.'
    )
  );
  p.appendChild(
    sec(
      'Drum',
      [
        rk(1, 'slow', 'Slow', 0.4, 1.0, 0.667, rpmFmt),
        rk(1, 'fast', 'Fast', 4.5, 6.5, 5.7, rpmFmt),
        rk(1, 'up', 'Rise', 1, 8, 1.7, secFmt, true),
        rk(1, 'down', 'Fall', 1.5, 10, 2.5, secFmt, true),
      ],
      [
        rowSeg(
          'Brake',
          [
            [true, 'on'],
            [false, 'off'],
          ],
          () => S.brake,
          v => {
            S.brake = v;
            send({ type: 'brake', v });
          },
          true
        ),
      ]
    )
  );
  p.appendChild(
    sec(
      'Power Amp',
      [
        knob({
          label: 'Drive',
          min: 0,
          max: 1,
          def: 0.35,
          fmt: pct,
          get: () => S.drive,
          set: v => {
            S.drive = v;
            send({ type: 'drive', v });
            syncAll();
          },
        }),
        knob({
          label: 'Sag',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: pct,
          get: () => S.amp.sag,
          set: v => {
            S.amp.sag = v;
            sendAmp();
          },
        }),
      ],
      [
        rowSel(
          'Model',
          AMP_MODELS,
          () => S.amp.model,
          v => {
            S.amp.model = +v;
            sendAmp();
            syncAll();
          },
          0
        ),
        rowSeg(
          'Quality',
          [
            [0, 'Eco'],
            [1, 'HQ'],
            [2, 'Ultra'],
          ],
          () => S.amp.quality,
          v => {
            S.amp.quality = v;
            sendAmp();
          },
          1
        ),
        rowVal('Plate / cath.', () => ampText),
      ],
      'Eco: static curve. HQ and Ultra solve the tubes and the output transformer as a circuit (Glow/Iron from Amberwave).'
    )
  );
  p.appendChild(
    sec(
      'Cabinet',
      [
        knob({
          label: 'Horn box',
          min: 0,
          max: 1,
          def: 0.45,
          fmt: pct,
          get: () => S.cab.hornBox,
          set: v => {
            S.cab.hornBox = v;
            sendCab();
          },
        }),
        knob({
          label: 'Resonance',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: v => (v < 0.005 ? 'off' : pct(v)),
          get: () => S.cab.res,
          set: v => {
            S.cab.res = v;
            sendCab();
          },
        }),
        knob({
          label: 'Mechanics',
          min: 0,
          max: 1,
          def: -1,
          fmt: v => (v < 0.005 ? 'off' : pct(v)),
          auto: { v: -1, label: 'auto', show: () => 0.15 + 0.7 * S.chr.cond },
          get: () => S.cab.mech,
          set: v => {
            S.cab.mech = v;
            sendCab();
          },
        }),
        knob({
          label: 'Wobble',
          min: 0,
          max: 1,
          def: -1,
          fmt: pct,
          auto: { v: -1, label: 'auto', show: () => S.chr.cond },
          get: () => S.cab.wobble,
          set: v => {
            S.cab.wobble = v;
            sendCab();
          },
        }),
      ],
      [
        rowSeg(
          'Build',
          [
            ['ply', 'Plywood'],
            ['chip', 'Chipboard'],
          ],
          () => S.cab.board,
          v => {
            S.cab.board = v;
            sendCab();
          },
          'ply'
        ),
        rowSeg(
          'Deflectors',
          [
            [true, 'In'],
            [false, 'Out'],
          ],
          () => S.cab.deflector !== false,
          v => {
            S.cab.deflector = v;
            sendCab();
          },
          true
        ),
      ],
      'The middle compartment is a vented box for the 15″ woofer. Early cabinets are plywood, later ones chipboard (softer, more damped).'
    )
  );
  p.appendChild(
    sec(
      'Placement',
      [
        knob({
          label: 'Balance',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: v => `${Math.round((1 - v) * 100)}/${Math.round(v * 100)}`,
          get: () => S.balance,
          set: v => {
            S.balance = v;
            send({ type: 'balance', v });
          },
        }),
        knob({
          label: 'Facing',
          min: -180,
          max: 180,
          step: 1,
          def: 0,
          fmt: v => (v === 0 ? '0° front' : v + '°'),
          get: () => cabDeg(),
          set: v => setCabYaw(Math.PI / 2 + (v * Math.PI) / 180),
        }),
      ],
      [
        rowBtn('Quick turn', 'Face the listener', () =>
          setCabYaw(Math.atan2(S.listener.y - S.rotaryPos.y, S.listener.x - S.rotaryPos.x))
        ),
        rowBtn('', 'Back to the wall', () => {
          const R = S.room,
            d = [
              [S.rotaryPos.x, Math.PI],
              [R.Lx - S.rotaryPos.x, 0],
              [S.rotaryPos.y, -Math.PI / 2],
              [R.Ly - S.rotaryPos.y, Math.PI / 2],
            ].sort((a, b) => a[0] - b[0])[0];
          setCabYaw(d[1] + Math.PI);
        }),
      ]
    )
  );
  p.appendChild(
    sec(
      'Control',
      [],
      [
        rowSel(
          'Switch via',
          [
            [1, 'CC 1 Mod wheel'],
            [64, 'CC 64 Sustain'],
            [4, 'CC 4 Foot controller'],
            [80, 'CC 80 Switch'],
          ],
          () => S.midi.rotaryCC,
          v => {
            S.midi.rotaryCC = +v;
            sendMidi();
            syncAll();
          },
          1
        ),
        rowSeg(
          'Half-moon',
          [
            ['two', '2-way'],
            ['stop', 'with Stop'],
          ],
          () => S.halfMoon,
          v => {
            S.halfMoon = v;
            if (v !== 'stop' && S.rotary === 'stop') setRotary('slow');
            syncAll();
            if (ready) scheduleSave();
          },
          'stop'
        ),
      ],
      'The space bar always toggles Slow/Fast. You can also turn the Rotary in the floor plan with the scroll wheel.'
    )
  );
});

// ---------------- Card: Room ----------------
page('raum', p => {
  const m = v => nf(v, 1) + ' m';
  const rk = (id, label, min, max, def, fmt) =>
    knob({
      label,
      min,
      max,
      def,
      step: id === 'absorb' || id === 'reverb' ? 0.01 : 0.1,
      fmt,
      get: () => S.room[id],
      set: v => {
        S.room[id] = v;
        clampPos();
        send({ type: 'room', v: { [id]: v } });
        sendPos();
      },
    });
  p.appendChild(
    sec(
      'Dimensions',
      [
        rk('Lx', 'Width', 3, 30, 6, m),
        rk('Ly', 'Length', 3, 40, 8, m),
        rk('Lz', 'Height', 2.2, 15, 2.8, m),
        rk('reverb', 'Reverb', 0, 2, 1, pct),
      ],
      [
        rowSeg(
          'Shape',
          [
            ['box', 'Box'],
            ['tent', 'Circus tent'],
          ],
          () => S.room.shape,
          v => setShape(v),
          'box'
        ),
      ],
      '',
      { id: 'secBox' }
    )
  );
  p.appendChild(
    sec(
      'Surfaces · Listener',
      [
        rk('absorb', 'Absorption', 0.05, 0.8, 0.3, pct),
        knob({
          label: 'Facing',
          min: -180,
          max: 180,
          step: 1,
          def: 0,
          fmt: v => v + '°',
          get: () => yawDeg(),
          set: v => setYaw(Math.PI / 2 + (v * Math.PI) / 180),
        }),
      ],
      [
        rowSel(
          'Material',
          MATERIALS,
          () => S.room.material,
          v => {
            S.room.material = v;
            send({ type: 'room', v: { material: v } });
            syncAll();
          },
          'wood'
        ),
        rowBtn('Head', 'Look at the Rotary', () =>
          setYaw(Math.atan2(S.rotaryPos.y - S.listener.y, S.rotaryPos.x - S.listener.x))
        ),
      ],
      '',
      { id: 'secSurf' }
    )
  );
  // Circus tent section
  const tk = (key, label, min, max, def, fmt, step) =>
    knob({
      label,
      min,
      max,
      def,
      step,
      fmt,
      get: () => S.tent[key],
      set: v => {
        S.tent[key] = v;
        if (S.room.shape === 'tent') applyTent(false);
      },
    });
  p.appendChild(
    sec(
      'Circus Tent',
      [
        tk('W', 'Ø Round', 16, 50, 30, m, 0.5),
        tk('Ls', 'Mid section', 0, 30, 0, m, 0.5),
        tk('mast', 'Pole dist.', 4, 20, 10, m, 0.5),
        tk('dome', 'Dome Ø', 6, 30, 14.1, m, 0.5),
        tk('hm', 'Crown ht.', 5, 16, 8.5, m, 0.1),
        tk('hb', 'Crown band', 0.3, 3, 1.3, m, 0.1),
        tk('tip', 'Peak', 0.5, 8, 3.5, m, 0.1),
        tk('hw', 'Wall ht.', 2.2, 5, 4, m, 0.1),
        tk('sagPct', 'Sag', 0, 1, 0.45, pct, 0.01),
        tk('ring', 'Ring Ø', 8, 18, 13, m, 0.5),
        tk('audience', 'Audience', 0, 1, 0.8, pct, 0.01),
      ],
      [
        rowSel(
          'Ground',
          [
            ['grass', 'Grass'],
            ['gravel', 'Gravel'],
            ['sandlot', 'Sand lot'],
            ['asphalt', 'Asphalt'],
            ['concrete', 'Concrete slab'],
            ['paving', 'Paving'],
            ['woodfloor', 'Wooden floor'],
            ['snow', 'Snow'],
          ],
          () => S.tent.ground,
          v => {
            S.tent.ground = v;
            if (S.room.shape === 'tent') applyTent(false);
          },
          'grass'
        ),
        rowSel(
          'Ring floor',
          [
            ['sand', 'Sawdust / sand'],
            ['ground', 'Same as ground'],
          ],
          () => S.tent.ringFloor,
          v => {
            S.tent.ringFloor = v;
            if (S.room.shape === 'tent') applyTent(false);
          },
          'sand'
        ),
        rowSeg(
          'Shape',
          [
            ['box', 'Box'],
            ['tent', 'Circus tent'],
          ],
          () => S.room.shape,
          v => setShape(v),
          'box'
        ),
        rowVal('Rays', () => tInfoText),
      ],
      '',
      { id: 'secTent', span: 2, kcols: 6, rcols: 2 }
    )
  );
  p.appendChild(
    sec(
      'Engine · Presets',
      [],
      [
        rowSeg(
          'Reflections',
          [1, 2, 3, 4, 5].map(v => [v, String(v)]),
          () => S.room.order,
          v => {
            S.room.order = v;
            send({ type: 'room', v: { order: v } });
            scheduleTrace();
            syncAll();
          },
          2
        ),
        rowSeg(
          'Precision',
          [
            [false, 'Fast'],
            [true, 'Exact'],
          ],
          () => S.exact,
          v => {
            S.exact = v;
            sendPrecision();
            syncAll();
          },
          false
        ),
        rowVal('Paths', () => (roomInfo ? `${roomInfo.paths} · ${S.exact ? 'all exact' : 'grouped ≥ 2nd'}` : '–')),
        rowSel(
          'Load room',
          () => [['', matchRoom() !== null ? '–' : 'Custom room'], ...ROOMS.map((r, i) => [i, r.n])],
          () => {
            const i = matchRoom();
            return i === null ? '' : i;
          },
          v => {
            if (v !== '') loadRoom(+v);
          }
        ),
        rowVal('CPU', () => cpuText),
      ],
      'Exact renders every reflection per sample with position, Doppler, directivity and panning. From 4th order that takes a lot of CPU.'
    )
  );
});
reg({
  sync() {
    const z = S.room.shape === 'tent';
    $('secBox').hidden = z;
    $('secSurf').hidden = z;
    $('secTent').hidden = !z;
  },
});

// ---------------- Card: Mics ----------------
const MIC_TYPES = [
  [0, '57 · dynamic'],
  [1, '421 · dynamic'],
  [2, 'D112 · kick'],
  [3, 'RE20 · supercardioid'],
  [4, '87 · condenser'],
  [5, 'KM 84 · SDC'],
  [6, '121 · ribbon'],
];
const PATTERNS = [
  [0, 'Cardioid'],
  [1, 'Omni'],
  [2, 'Figure-8'],
];
const CH_NAMES = ['Horn L', 'Horn R', 'Drum 1', 'Drum 2', 'Room L', 'Room R', 'Listener'];
const micActive = c => {
  const M = S.mics;
  return c === 1 ? M.hornStereo : c === 3 ? M.drumStereo : true;
};
const degFmt = v => (v === 0 ? '0° front' : Math.round(v) + '°');
function placeRoomPair() {
  const f = S.cabYaw,
    d = Math.min(3, Math.max(1.5, Math.min(S.room.Lx, S.room.Ly) * 0.35));
  S.mics.roomX = clamp(S.rotaryPos.x + Math.cos(f) * d, 0.5, S.room.Lx - 0.5);
  S.mics.roomY = clamp(S.rotaryPos.y + Math.sin(f) * d, 0.5, S.room.Ly - 0.5);
  S.mics.roomYaw = Math.atan2(S.rotaryPos.y - S.mics.roomY, S.rotaryPos.x - S.mics.roomX);
  S.mics.placed = true;
}
function setMicsOn(v) {
  S.mics.on = v;
  if (v && !S.mics.placed) placeRoomPair();
  sendMics();
  syncAll();
}
page('mikros', p => {
  const M = () => S.mics;
  const mk = k => v => {
    S.mics[k] = v;
    sendMics();
    syncAll();
  };
  const arrSeg = (label, kSt, kAng, opts) =>
    rowSeg(
      label,
      opts,
      () => (S.mics[kSt] ? S.mics[kAng] : 0),
      v => {
        if (v === 0) S.mics[kSt] = false;
        else {
          S.mics[kSt] = true;
          S.mics[kAng] = v;
        }
        sendMics();
        syncAll();
      },
      180
    );
  const patRow = (kType, kPat) => {
    const r = rowSel(
      'Pattern',
      PATTERNS,
      () => S.mics[kPat],
      v => {
        S.mics[kPat] = +v;
        sendMics();
      },
      0
    );
    reg({
      sync() {
        r.el.style.display = S.mics[kType] === 4 ? '' : 'none';
      },
    });
    return r;
  };
  p.appendChild(
    sec(
      'Horn Mics',
      [
        knob({
          label: 'Angle',
          min: -180,
          max: 180,
          step: 5,
          def: 0,
          fmt: degFmt,
          get: () => M().hornCenter,
          set: mk('hornCenter'),
        }),
        knob({
          label: 'Distance',
          min: 0.03,
          max: 1.0,
          def: 0.15,
          log: true,
          fmt: v => Math.round(v * 100) + ' cm',
          get: () => M().hornDist,
          set: mk('hornDist'),
        }),
        knob({
          label: 'Height',
          min: -0.3,
          max: 0.3,
          step: 0.01,
          def: 0,
          fmt: v => (v > 0 ? '+' : v < 0 ? '−' : '') + Math.round(Math.abs(v) * 100) + ' cm',
          get: () => M().hornHeight,
          set: mk('hornHeight'),
        }),
      ],
      [
        arrSeg('Setup', 'hornStereo', 'hornAngle', [
          [0, 'Mono'],
          [90, '90°'],
          [120, '120°'],
          [180, '180°'],
        ]),
        rowSel(
          'Mic',
          MIC_TYPES,
          () => M().hornType,
          v => {
            S.mics.hornType = +v;
            sendMics();
            syncAll();
          },
          0
        ),
        patRow('hornType', 'hornPattern'),
        rowSeg(
          'Windscreen',
          [
            [false, 'off'],
            [true, 'on'],
          ],
          () => M().windscreen,
          mk('windscreen'),
          false
        ),
      ]
    )
  );
  p.appendChild(
    sec(
      'Drum Mics',
      [
        knob({
          label: 'Angle',
          min: -180,
          max: 180,
          step: 5,
          def: 0,
          fmt: degFmt,
          get: () => M().drumCenter,
          set: mk('drumCenter'),
        }),
        knob({
          label: 'Distance',
          min: 0.03,
          max: 1.0,
          def: 0.15,
          log: true,
          fmt: v => Math.round(v * 100) + ' cm',
          get: () => M().drumDist,
          set: mk('drumDist'),
        }),
      ],
      [
        arrSeg('Setup', 'drumStereo', 'drumAngle', [
          [0, 'Mono'],
          [90, '90°'],
          [180, '180°'],
        ]),
        rowSel(
          'Mic',
          MIC_TYPES,
          () => M().drumType,
          v => {
            S.mics.drumType = +v;
            sendMics();
            syncAll();
          },
          2
        ),
        patRow('drumType', 'drumPattern'),
      ],
      'From the back (180°) you get more motor, from the front more cabinet.'
    )
  );
  p.appendChild(
    sec(
      'Room Pair',
      [
        knob({
          label: 'Spacing',
          min: 0.2,
          max: 3,
          step: 0.05,
          def: 0.6,
          fmt: v => nf(v, 2) + ' m',
          get: () => M().roomSpacing,
          set: mk('roomSpacing'),
        }),
        knob({
          label: 'Height',
          min: 0.5,
          max: 4,
          step: 0.05,
          def: 2,
          fmt: v => nf(v, 1) + ' m',
          get: () => M().roomZ,
          set: mk('roomZ'),
        }),
        knob({
          label: 'Aim',
          min: -180,
          max: 180,
          step: 1,
          def: 0,
          fmt: v => Math.round(v) + '°',
          get: () => {
            let d = Math.round(((M().roomYaw - Math.PI / 2) * 180) / Math.PI);
            while (d > 180) d -= 360;
            while (d < -180) d += 360;
            return d;
          },
          set: v => {
            S.mics.roomYaw = Math.PI / 2 + (v * Math.PI) / 180;
            sendMics();
            syncAll();
          },
        }),
        knob({
          label: 'Reflect.',
          opts: [
            [1, '1st'],
            [2, '2nd'],
            [3, '3rd'],
          ],
          def: 2,
          get: () => M().roomOrder,
          set: mk('roomOrder'),
        }),
      ],
      [
        rowSeg(
          'Array',
          [
            [0, 'AB'],
            [1, 'XY'],
            [2, 'ORTF'],
          ],
          () => M().roomArr,
          v => {
            S.mics.roomArr = v;
            if (v > 0 && S.mics.roomType === 4 && S.mics.roomPattern === 1) S.mics.roomPattern = 0;
            sendMics();
            syncAll();
          },
          0
        ),
        rowSel(
          'Mic',
          MIC_TYPES,
          () => M().roomType,
          v => {
            S.mics.roomType = +v;
            sendMics();
            syncAll();
          },
          4
        ),
        patRow('roomType', 'roomPattern'),
        rowBtn('', 'Place in front', () => {
          placeRoomPair();
          sendMics();
          syncAll();
        }),
      ]
    )
  );
  // Mixer: three groups, each stereo (balance + width) or mono (pan)
  const desk = h('div', 'desk');
  const GRP = [
    ['Horn', () => S.mics.hornStereo, () => S.mics.hornAngle + '°'],
    ['Drum', () => S.mics.drumStereo, () => S.mics.drumAngle + '°'],
    ['Room', () => true, () => ['AB', 'XY', 'ORTF'][S.mics.roomArr]],
  ];
  GRP.forEach(([nm, isSt, stInfo], g) => {
    const G = () => S.mics.grp[g];
    const st = h('div', 'strip');
    const hd = h('div', 'shd');
    hd.appendChild(h('span', 'sn', nm));
    const badge = h('span', 'badge');
    hd.appendChild(badge);
    st.appendChild(hd);
    const tg = h('div', 'tg');
    const tb = (txt, key, cls, title) => {
      const b = h('button', cls || '', txt);
      b.type = 'button';
      b.dataset.tip = title;
      b.onclick = () => {
        G()[key] = !G()[key];
        sendMics();
        syncAll();
      };
      tg.appendChild(b);
      return [key, b];
    };
    const tbs = [
      tb('Ø', 'pol', '', 'Flip polarity – check it against the other groups, the drum mics often want it.'),
      tb('M', 'mute', '', 'Mute'),
      tb('S', 'solo', 'solo', 'Solo'),
    ];
    st.appendChild(tg);
    const body = h('div', 'body');
    const fw = h('div', 'fadw');
    const fd = h('div', 'fad', '<i></i>');
    const mL = h('div', 'smeter', '<b></b>'),
      mR = h('div', 'smeter r', '<b></b>');
    fw.appendChild(fd);
    fw.appendChild(mL);
    fw.appendChild(mR);
    body.appendChild(fw);
    const toP = db => (db <= -60 ? 0 : clamp((db + 48) / 60, 0.02, 1)),
      fromP = p => (p < 0.02 ? -100 : Math.round((-48 + 60 * p) * 2) / 2);
    const setDb = db => {
      G().gain = clamp(db, -100, 12);
      sendMics();
      syncAll();
    };
    let drag = null;
    fd.addEventListener('pointerdown', e => {
      fd.setPointerCapture(e.pointerId);
      drag = { y: e.clientY, p: toP(G().gain) };
      e.preventDefault();
    });
    fd.addEventListener('pointermove', e => {
      if (!drag) return;
      const r = fd.getBoundingClientRect();
      setDb(fromP(clamp(drag.p + ((drag.y - e.clientY) / r.height) * (e.shiftKey ? 0.25 : 1), 0, 1)));
    });
    fd.addEventListener('pointerup', () => (drag = null));
    fd.addEventListener('pointercancel', () => (drag = null));
    fd.addEventListener(
      'wheel',
      e => {
        e.preventDefault();
        const db = G().gain <= -60 ? -48 : G().gain;
        setDb(db + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.5 : 1));
      },
      { passive: false }
    );
    fd.addEventListener('dblclick', () => setDb(0));
    fd.dataset.tip = 'Group fader';
    fd.dataset.tipK = 'Drag or scroll · Shift = fine · Double-click = 0 dB';
    const kc = h('div', 'kcol');
    const panK = knob({
      small: true,
      label: 'Pan',
      tip: 'Pan (mono group) or balance (stereo group).',
      min: -1,
      max: 1,
      step: 0.05,
      def: 0,
      fmt: v => (Math.abs(v) < 0.025 ? 'C' : (v < 0 ? 'L ' : 'R ') + Math.round(Math.abs(v) * 100)),
      get: () => G().pan,
      set: v => {
        G().pan = v;
        sendMics();
      },
    });
    const wK = knob({
      small: true,
      label: 'Width',
      tip: 'Stereo width of the pair: full to mono.',
      min: 0,
      max: 1,
      step: 0.01,
      def: 1,
      fmt: v => (v >= 0.995 ? 'full' : v <= 0.005 ? 'mono' : pct(v)),
      get: () => G().width,
      set: v => {
        G().width = v;
        sendMics();
      },
    });
    wK.el.classList.add('wk');
    kc.appendChild(panK.el);
    kc.appendChild(wK.el);
    body.appendChild(kc);
    st.appendChild(body);
    const fv = h('span', 'fv');
    st.appendChild(fv);
    const foot = h('div', 'foot');
    const dn = h('div', 'dnum');
    dn.dataset.tip = 'Delay: time-align the group to the others.';
    dn.dataset.tipK = 'Drag or scroll · Double-click = 0';
    let dd = null;
    const setD = v => {
      G().delay = clamp(Math.round(v * 10) / 10, 0, 50);
      sendMics();
      syncAll();
    };
    dn.addEventListener('pointerdown', e => {
      dn.setPointerCapture(e.pointerId);
      dd = { y: e.clientY, v: G().delay };
    });
    dn.addEventListener('pointermove', e => {
      if (dd) setD(dd.v + ((dd.y - e.clientY) / SCALE) * (e.shiftKey ? 0.02 : 0.1));
    });
    dn.addEventListener('pointerup', () => (dd = null));
    dn.addEventListener('pointercancel', () => (dd = null));
    dn.addEventListener(
      'wheel',
      e => {
        e.preventDefault();
        setD(G().delay + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.1 : 0.5));
      },
      { passive: false }
    );
    dn.addEventListener('dblclick', () => setD(0));
    const osel = h('select');
    osel.setAttribute('aria-label', nm + ' output');
    osel.dataset.tip = 'Destination: the front or rear mix, or a direct out of its own (plugin: the Direct buses).';
    osel.onchange = () => {
      G().out = +osel.value;
      sendMics();
      syncAll();
    };
    foot.appendChild(dn);
    foot.appendChild(osel);
    st.appendChild(foot);
    desk.appendChild(st);
    let oSig = '';
    reg({
      sync() {
        const v = G(),
          stereo = isSt();
        st.classList.toggle('st', stereo);
        st.classList.toggle('off', !S.mics.on);
        badge.textContent = stereo ? 'Stereo · ' + stInfo() : 'Mono';
        panK.el.querySelector('.kl').textContent = stereo ? 'Balance' : 'Pan';
        tbs.forEach(([k, b]) => b.classList.toggle('on', !!v[k]));
        fd.firstChild.style.top = (1 - toP(v.gain)) * 100 + '%';
        fv.textContent =
          v.gain <= -60 ? '−∞' : (v.gain > 0 ? '+' : v.gain < 0 ? '−' : '') + nf(Math.abs(v.gain), 1) + ' dB';
        dn.textContent = nf(v.delay, 1) + ' ms';
        const n = Math.min(16, Math.max(audioInfo.outputs || 0, 2));
        const list = [
          [-1, 'front'],
          [-2, 'rear'],
          ...Array.from({ length: stereo ? n - 1 : n }, (_, k) => [
            k,
            stereo ? `Out ${k + 1}/${k + 2}` : `Out ${k + 1}`,
          ]),
        ];
        const sig = JSON.stringify(list);
        if (sig !== oSig) {
          oSig = sig;
          osel.innerHTML = '';
          list.forEach(([val, t]) => {
            const o = h('option', '', t);
            o.value = val;
            osel.appendChild(o);
          });
        }
        osel.value = String(Math.min(v.out, stereo ? n - 2 : n - 1));
      },
      reset() {
        S.mics.grp[g] = clone(DEF.mics.grp[g]);
        sendMics();
      },
    });
    micMeterEls[2 * g] = mL.firstChild;
    micMeterEls[2 * g + 1] = mR.firstChild;
  });
  const onSeg = rowSeg(
    '',
    [
      [false, 'Listener'],
      [true, 'Mics'],
    ],
    () => S.mics.on,
    v => setMicsOn(v),
    false
  );
  p.appendChild(sec('Mixer', [], [], '', { span: 2, head: onSeg, extra: desk }));
});

// ---------------- Card: Output ----------------
page('ausgabe', p => {
  p.appendChild(
    sec(
      'Format',
      [
        knob({
          label: 'LFE',
          min: 0,
          max: 1,
          def: 0.5,
          fmt: pct,
          get: () => S.lfe,
          set: v => {
            S.lfe = v;
            send({ type: 'lfe', v });
          },
        }),
        knob({
          label: 'LFE x-over',
          min: 60,
          max: 160,
          step: 5,
          def: 100,
          fmt: v => v + ' Hz',
          get: () => S.out.lfeHz,
          set: v => {
            S.out.lfeHz = v;
            sendOut();
          },
        }),
        knob({
          label: 'Master',
          min: -24,
          max: 6,
          step: 0.5,
          def: 0,
          fmt: v => (v > 0 ? '+' : v < 0 ? '−' : '') + nf(Math.abs(v), 1) + ' dB',
          get: () => S.out.masterDb,
          set: v => {
            S.out.masterDb = v;
            sendOut();
          },
        }),
      ],
      [
        rowSeg(
          'Format',
          [
            ['stereo', 'Stereo'],
            ['quad', 'Quad'],
            ['s51', '5.1'],
          ],
          () => S.mode,
          v => {
            S.mode = v;
            applyMode();
            syncAll();
          },
          'stereo'
        ),
      ],
      'LFE and crossover only apply in 5.1.'
    )
  );
  // Channel mapping: rows for up to 6 channels, visible count depends on the format
  const mapRows = [0, 1, 2, 3, 4, 5].map(i => {
    const r = rowSel(
      'Ch ' + (i + 1),
      () => {
        const n = 8;
        return Array.from({ length: n }, (_, k) => [k, outName(k)]);
      }, // the room mix has 8 speaker slots; higher outputs are for mic direct outs
      () => curMap()[i] ?? i,
      v => {
        const m = curMap().slice();
        m[i] = +v;
        S.chmap[S.mode] = m;
        applyMode();
        syncAll();
      }
    );
    return r;
  });
  const s2 = sec(
    'Routing',
    [],
    [
      ...mapRows,
      rowBtn('', 'Auto (1, 2, 3 …)', () => {
        S.chmap[S.mode] = null;
        applyMode();
        syncAll();
      }),
    ]
  );
  p.appendChild(s2);
  reg({
    sync() {
      const M = MODES[S.mode];
      mapRows.forEach((r, i) => {
        r.el.hidden = i >= M.ch;
        r.el.style.display = i >= M.ch ? 'none' : '';
        r.el.querySelector('.rl').textContent = M.long[i] || '';
      });
    },
    reset() {
      S.chmap = { stereo: null, quad: null, s51: null };
      applyMode();
    },
  });
  const dbFmt = v => (v > 0 ? '+' : v < 0 ? '−' : '') + nf(Math.abs(v), 1) + ' dB';
  const rearK = knob({
    label: 'Rear',
    min: -12,
    max: 3,
    step: 0.5,
    def: 0,
    fmt: dbFmt,
    get: () => S.out.rearDb,
    set: v => {
      S.out.rearDb = v;
      sendOut();
    },
  });
  const centK = knob({
    label: 'Center',
    min: -12,
    max: 3,
    step: 0.5,
    def: 0,
    fmt: dbFmt,
    get: () => S.out.centerDb,
    set: v => {
      S.out.centerDb = v;
      sendOut();
    },
  });
  const divK = knob({
    label: 'Divergence',
    min: 0,
    max: 1,
    def: 0,
    fmt: v => (v < 0.005 ? 'center only' : v > 0.995 ? 'phantom' : pct(v)),
    get: () => S.out.centerDiv,
    set: v => {
      S.out.centerDiv = v;
      sendOut();
    },
  });
  const monSec = sec(
    'Monitoring',
    [rearK, centK, divK],
    [],
    'Trims for your speakers, not the room. Center and divergence only in 5.1.'
  );
  p.appendChild(monSec);
  reg({
    sync() {
      const m = S.mode;
      monSec.style.display = m === 'stereo' ? 'none' : '';
      centK.el.style.display = m === 's51' ? '' : 'none';
      divK.el.style.display = m === 's51' ? '' : 'none';
    },
  });
  const testRow = rowBtn('Channel test', 'Ping all channels', () => chanTest(testRow.btn));
  p.appendChild(
    sec(
      'Check',
      [],
      [
        testRow,
        rowVal('Device', () => (audioInfo.device ? `${audioInfo.device}, ${audioInfo.outputs} outputs` : '–')),
        rowVal('Status', () => warnText || 'all good'),
      ],
      'With a source straight ahead the rear only gets the room: in a small, live room about 8 dB below the front, more with distance and reverb.'
    )
  );
});
let testTimers = [];
function chanTest(btn) {
  testTimers.forEach(clearTimeout);
  testTimers = [];
  const M = MODES[S.mode],
    n = M.ch;
  M.long.forEach((name, i) =>
    testTimers.push(
      setTimeout(() => {
        const phys = physCh(i);
        btn.textContent = `${i + 1}/${n}: ${name} → ${phys + 1}`;
        btn.classList.add('on');
        nativeInvoke('chantest', phys, S.mode === 's51' && i === 3);
      }, i * 750)
    )
  );
  testTimers.push(
    setTimeout(() => {
      btn.textContent = 'Ping all channels';
      btn.classList.remove('on');
    }, n * 750)
  );
}

// ---------------- Card: MIDI ----------------
page('midi', p => {
  const chOpts = omni => [
    [0, omni ? 'Omni (all)' : 'off'],
    ...Array.from({ length: 16 }, (_, i) => [i + 1, 'Channel ' + (i + 1)]),
  ];
  const chRow = (label, k, def, omni) =>
    rowSel(
      label,
      chOpts(omni),
      () => S.midi[k],
      v => {
        S.midi[k] = +v;
        sendMidi();
        syncAll();
      },
      def
    );
  p.appendChild(
    sec(
      'Channels',
      [],
      [chRow('Upper', 'upper', 1, true), chRow('Lower', 'lower', 2, false), chRow('Pedal', 'pedal', 3, false)],
      'Upper on Omni: everything that is not on the lower or pedal channel plays the upper manual.'
    )
  );
  const chipRow = (label, k, name) => {
    const r = h('div', 'row');
    r.appendChild(h('span', 'rl', label));
    r.appendChild(splitChip(k, name));
    return { el: r, label: label || 'Split point', tipEl: r };
  };
  const hiRow = chipRow('Split point', 'hi', 'Upper'),
    loRow = chipRow('', 'lo', 'Lower');
  reg({
    sync() {
      hiRow.el.style.display = S.split.mode >= 1 ? '' : 'none';
      loRow.el.style.display = S.split.mode === 2 ? '' : 'none';
    },
  });
  const OCT = [-3, -2, -1, 0, 1, 2, 3].map(v => [v, v > 0 ? '+' + v : v < 0 ? '−' + -v : '0']);
  const octRow = (label, man) =>
    rowSeg(
      label,
      OCT,
      () => S.split.oct[man],
      v => {
        S.split.oct[man] = v;
        sendKeyMap();
        syncAll();
      },
      0
    );
  p.appendChild(
    sec(
      'Split · Octaves',
      [],
      [
        rowSeg(
          'Split',
          [
            [0, 'off'],
            [1, '2 zones'],
            [2, '3 zones'],
          ],
          () => S.split.mode,
          v => {
            S.split.mode = v;
            learnTarget = null;
            sendKeyMap();
            syncAll();
          },
          0
        ),
        hiRow,
        loRow,
        rowSel(
          'Keyboard',
          [
            [49, '49 keys'],
            [61, '61 keys'],
            [73, '73 keys'],
            [76, '76 keys'],
            [88, '88 keys'],
          ],
          () => S.split.kb,
          v => {
            S.split.kb = +v;
            syncAll();
            if (ready) scheduleSave();
          },
          61
        ),
        octRow('Upper octave', 0),
        octRow('Lower octave', 1),
        octRow('Pedal octave', 2),
      ],
      'Split works on the upper channel · ● learns from the next key.'
    )
  );
  p.appendChild(
    sec(
      'Controllers',
      [],
      [
        rowSel(
          'Rotary',
          [
            [1, 'CC 1 Mod wheel'],
            [64, 'CC 64 Sustain'],
            [4, 'CC 4 Foot controller'],
            [80, 'CC 80 Switch'],
          ],
          () => S.midi.rotaryCC,
          v => {
            S.midi.rotaryCC = +v;
            sendMidi();
            syncAll();
          },
          1
        ),
        rowSel(
          'Swell',
          [
            [11, 'CC 11 Expression'],
            [7, 'CC 7 Volume'],
            [4, 'CC 4 Foot controller'],
          ],
          () => S.midi.swellCC,
          v => {
            S.midi.swellCC = +v;
            sendMidi();
            syncAll();
          },
          11
        ),
        rowVal('Devices', () =>
          audioInfo.midiDevices && audioInfo.midiDevices.length ? audioInfo.midiDevices.join(', ') : 'none found'
        ),
        rowBtn('', 'MIDI Inputs …', () => nativeInvoke('audioSettings')),
      ],
      'Newly connected MIDI devices are enabled automatically.'
    )
  );
  p.appendChild(
    sec(
      'QWERTY Keys',
      [],
      [
        rowSeg(
          'Plays',
          [
            [0, 'Upper'],
            [1, 'Lower'],
            [2, 'Pedal'],
          ],
          () => S.pcMan,
          v => {
            releasePc();
            S.pcMan = v;
            syncAll();
            if (ready) scheduleSave();
          },
          0
        ),
        rowVal('Octave', () => (S.octave > 0 ? '+' : '') + S.octave),
        rowVal('Keys', () => 'Z–M and Q–P, sharps above (Y–M on QWERTZ)'),
      ],
      'Space: toggle Rotary · Esc: panic.'
    )
  );
});

// ---------------- Card: Audio ----------------
page('audio', p => {
  const setBtn = rowBtn('', 'Audio Settings …', () => nativeInvoke('audioSettings'));
  const bounce = rowSeg(
    'Bounce',
    [
      [true, 'Max quality'],
      [false, 'As set'],
    ],
    () => S.offlineMax,
    v => {
      S.offlineMax = v;
      sendOffline();
    },
    true
  );
  reg({
    sync() {
      const pl = isPlugin();
      setBtn.el.style.display = pl ? 'none' : '';
      bounce.el.style.display = pl ? '' : 'none';
    },
  });
  p.appendChild(
    sec(
      'Device',
      [],
      [rowVal('Driver', () => audioInfo.type || '–'), rowVal('Device', () => audioInfo.device || '–'), setBtn, bounce],
      () =>
        isPlugin()
          ? 'In a plugin the host sets sample rate, buffer and outputs.'
          : 'Driver (ASIO, Windows Audio), device, sample rate, buffer and outputs are set in the audio dialog; Wheelhouse remembers them.'
    )
  );
  p.appendChild(
    sec(
      'Buffer',
      [],
      [
        rowVal('Sample rate', () => (audioInfo.sampleRate ? nf(audioInfo.sampleRate / 1000, 1) + ' kHz' : '–')),
        rowVal('Buffer', () =>
          audioInfo.bufferSize
            ? `${audioInfo.bufferSize} samples = ${nf((audioInfo.bufferSize / audioInfo.sampleRate) * 1000, 1)} ms`
            : '–'
        ),
        rowVal('Output', () =>
          audioInfo.sampleRate
            ? isPlugin()
              ? `${nf(outMs(), 1)} ms buffer (output latency: host)`
              : `${nf(outMs(), 1)} ms (driver, incl. buffer)`
            : '–'
        ),
        rowVal('Processing', () =>
          audioInfo.sampleRate ? `${nf((procLatency / audioInfo.sampleRate) * 1000, 1)} ms (tube oversampling)` : '–'
        ),
        rowVal('Total', () =>
          audioInfo.sampleRate
            ? `${nf(outMs() + (procLatency / audioInfo.sampleRate) * 1000, 1)} ms + sound path ${nf(soundPathMs(), 1)} ms`
            : '–'
        ),
      ],
      "Sound path: travel time from the Rotary to the listener or the nearest mic – as in a real room. Key-to-computer time (MIDI, USB) can't be measured."
    )
  );
  p.appendChild(
    sec(
      'Outputs · CPU',
      [],
      [
        rowVal('Active', () => (audioInfo.outputs ? `${audioInfo.outputs} outputs` : '–')),
        rowVal('Channels', () => (audioInfo.channelNames || []).slice(0, 8).join(', ') || '–'),
        rowVal('CPU', () => (isPlugin() ? 'see host' : cpuText)),
        rowVal('Dropouts', () =>
          droppedBlocks ? `${droppedBlocks} blocks skipped (a setting was being applied)` : 'none'
        ),
        rowSeg(
          'Power amp',
          [
            [0, 'Eco'],
            [1, 'HQ'],
            [2, 'Ultra'],
          ],
          () => S.amp.quality,
          v => {
            S.amp.quality = v;
            sendAmp();
            syncAll();
          }
        ),
        rowSeg(
          'Cores',
          [
            [0, 'Auto'],
            [1, '1'],
            [2, '2'],
            [4, '4'],
            [8, '8'],
            [16, '16'],
          ],
          () => S.threads,
          v => {
            S.threads = v;
            sendThreads();
          },
          0
        ),
      ],
      'Running out of headroom? Raise the buffer, lower the reflections or set the power amp to HQ or Eco.'
    )
  );
});

// Latency figures for the Audio card: the driver's output latency already contains the buffer
function isPlugin() {
  return audioInfo.type === 'Plugin';
}
function outMs() {
  return ((audioInfo.outLatencySamples || audioInfo.bufferSize || 0) / (audioInfo.sampleRate || 48000)) * 1000;
}
function soundPathMs() {
  if (S.mics.on) {
    const M = S.mics;
    return (Math.min(0.37 + M.hornDist, Math.hypot(M.roomX - S.rotaryPos.x, M.roomY - S.rotaryPos.y)) / 343) * 1000;
  }
  return (Math.hypot(S.rotaryPos.x - S.listener.x, S.rotaryPos.y - S.listener.y) / 343) * 1000;
}
