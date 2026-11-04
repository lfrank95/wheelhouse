// Erzeugt Resources/index.html für die .exe aus der Browser-Vorlage (template5.html + tracer.js).
// Die Klangberechnung läuft nativ in C++; die Seite spricht über die JUCE-Brücke (window.__JUCE__) mit dem Kern.
const fs = require('fs');
const path = require('path');
const src = process.argv[2] || '/home/claude/leslie';
let t = fs.readFileSync(path.join(src, 'template5.html'), 'utf8');
const tracer = fs.readFileSync(path.join(src, 'tracer.js'), 'utf8');
function rep(a, b) { if (!t.includes(a)) throw new Error('nicht gefunden: ' + a.slice(0, 80)); t = t.replace(a, b); }

t = t.replace('/*__TRACER__*/', () => tracer).replace('/*__PROCESSOR__*/', '');
t = t.replace(/Demo V\d+/g, 'Programm').replace('<title>Leslie im Raum – Programm</title>', '<title>Leslie im Raum</title>');

// Bridge
rep("  const send = (m) => { if (node) node.port.postMessage(m); };",
`  const NATIVE = !!(window.__JUCE__ && window.__JUCE__.backend);
  let nativeInvoke = null;
  if (NATIVE) {
    const promises = new Map(); let pid = 0;
    window.__JUCE__.backend.addEventListener('__juce__complete', ({ promiseId, result }) => { const p = promises.get(promiseId); if (p) { p(result); promises.delete(promiseId); } });
    nativeInvoke = (name, ...params) => new Promise(res => { const id = pid++; promises.set(id, res); window.__JUCE__.backend.emitEvent('__juce__invoke', { name, params, resultId: id }); });
  }
  let audioInfo = { outputs: 0, device: '', sampleRate: 0, bufferSize: 0, channelNames: [] };
  const send = (m) => { if (NATIVE) nativeInvoke('dsp', m); else if (node) node.port.postMessage(m); };`);

// Start: nativ ohne AudioContext
rep("  async function start() {\n    try {",
`  async function start() {
    if (NATIVE) {
      const B = window.__JUCE__.backend;
      B.addEventListener('rot', (m) => { Object.assign(S.rot, { horn: m.horn, drum: m.drum, hornHz: m.hornHz, drumHz: m.drumHz, t: performance.now() }); S.meters = m.meters; });
      B.addEventListener('roomInfo', (m) => { $('stRt').textContent = m.rt60.map(x => x.toFixed(1)).join(' / ') + ' s'; $('stRc').textContent = m.rc.toFixed(1) + ' m'; $('stPaths').textContent = m.paths; });
      B.addEventListener('audio', (m) => { audioInfo = m; applyMode(); });
      B.addEventListener('resend', () => { pushAll(); applyMode(); });
      B.addEventListener('midi', (m) => {
        if (m.type === 'noteOn') { active.add(m.key); keyEl(m.key, true); }
        else if (m.type === 'noteOff') { active.delete(m.key); keyEl(m.key, false); }
        else if (m.type === 'leslie') setLeslie(m.v);
        else if (m.type === 'volume') { S.volume = m.v; $('volume').value = m.v; upd('volume'); }
      });
      pushAll(); applyMode();
      $('overlay').style.display = 'none';
      $('stMidi').textContent = 'über Programm (alle Geräte)';
      nativeInvoke('audioInfo');
      return;
    }
    try {`);

// Ausgabemodus: physische Ausgänge des Audiogeräts
rep("  function applyMode() {\n    const M = MODES[S.mode];",
`  function applyMode() {
    if (NATIVE) {
      if (S.mode === 'binaural') { S.mode = 'stereo'; $('mode').value = 'stereo'; }
      const M = MODES[S.mode];
      $('lfeRow').style.display = S.mode === 's51' ? '' : 'none';
      const have = audioInfo.outputs || 0;
      $('modeWarn').textContent = have && have < M.ch ? \`Das Audiogerät hat nur \${have} aktive Ausgänge – unter „Audio-Einstellungen“ mehr Ausgänge aktivieren (\${M.ch} nötig für \${S.mode === 's51' ? '5.1' : S.mode === 'quad' ? 'Quad' : 'Stereo'}).\` : '';
      curSpk = Object.assign({}, M.spk);
      send(Object.assign({ type: 'speakers' }, curSpk));
      const names = (audioInfo.channelNames || []).slice(0, M.ch).join(', ');
      $('stCh').textContent = \`\${M.ch} (\${audioInfo.device || '–'}\${audioInfo.type ? ', ' + audioInfo.type : ''}, \${audioInfo.sampleRate ? (audioInfo.sampleRate / 1000).toFixed(1) + ' kHz' : ''}\${audioInfo.bufferSize ? ', ' + audioInfo.bufferSize + ' Samples' : ''})\` + (names ? ' → ' + names : '');
      return;
    }
    const M = MODES[S.mode];`);

// MIDI: nativ
rep("  function initMidi() {", "  function initMidi() {\n    if (NATIVE) return;");

// Kanaltest nativ
rep("  $('chantest').onclick = async () => {\n    if (!ctx) await start();",
`  $('chantest').onclick = async () => {
    if (NATIVE) {
      testTimers.forEach(clearTimeout); testTimers = [];
      const names = CHNAMES[S.mode]; const n = names.length; const btn = $('chantest');
      names.forEach((name, i) => testTimers.push(setTimeout(() => { const phys = curSpk ? curSpk.ch[i] : i; btn.textContent = \`\${i + 1}/\${n}: \${name} (Ausgang \${phys + 1})\`; btn.classList.add('on'); nativeInvoke('chantest', S.mode === 's51' && i === 3 ? 3 : phys); }, i * 750)));
      testTimers.push(setTimeout(() => { btn.textContent = 'Kanaltest'; btn.classList.remove('on'); }, n * 750));
      return;
    }
    if (!ctx) await start();`);

// Demo-Knopf, Neustart-Knopf
rep("  $('demo').onclick = () => { if (!ctx) { start().then(playDemo); } else playDemo(); };",
    "  $('demo').onclick = () => { if (!ctx && !NATIVE) { start().then(playDemo); } else playDemo(); };");
rep("  $('restart').onclick = restartAudio;",
    "  $('restart').onclick = () => { if (NATIVE) nativeInvoke('audioSettings'); else restartAudio(); };\n  if (NATIVE) { $('restart').textContent = 'Audio-Einstellungen'; $('restart').title = 'Audiogerät (ASIO/WASAPI), Puffergröße, Ausgänge und MIDI-Eingänge'; }");

// Pegelanzeige nativ
rep("    if (!meters.length) return;",
`    if (NATIVE) {
      if (!S.meters) return;
      names.forEach((nm, i) => {
        const phys = S.mode === 's51' ? i : (curSpk ? curSpk.ch[i] : i);
        const db = S.meters[phys] !== undefined ? S.meters[phys] : -120;
        meterHold[i] = Math.max(db, meterHold[i] - 0.5);
        const h = Math.max(0, Math.min(1, (meterHold[i] + 60) / 60));
        meterEls[i].querySelector('.bar').style.height = (h * 100) + '%';
        meterEls[i].querySelector('.db').textContent = db > -59 ? db.toFixed(0) : '–';
      });
      return;
    }
    if (!meters.length) return;`);

// Autostart + Modusliste ohne Kopfhörer-Option
rep("  resize(); requestAnimationFrame(draw); applyMode();",
`  if (NATIVE) {
    const opt = $('mode').querySelector('option[value=binaural]'); if (opt) opt.remove();
    S.mode = 'stereo'; $('mode').value = 'stereo';
    $('modeWarn').textContent = '';
    setTimeout(start, 50);
  }
  resize(); requestAnimationFrame(draw); applyMode();`);

// Overlay-Text
rep("<p>Am besten mit Kopfhörern starten. Für Quad/5.1 oben die Ausgabe umstellen (Windows-Audiogerät muss auf Mehrkanal stehen).</p>",
    "<p>Am besten mit Kopfhörern starten. Für Quad/5.1 oben die Ausgabe umstellen.</p>");

fs.mkdirSync(path.join(__dirname, 'Resources'), { recursive: true });
fs.writeFileSync(path.join(__dirname, 'Resources', 'index.html'), t);
console.log('Resources/index.html geschrieben:', (t.length / 1024).toFixed(0), 'kB');
