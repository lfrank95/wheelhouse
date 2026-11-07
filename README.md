# Wheelhouse

Open-source tonewheel organ with a physically modelled rotary speaker standing in a real room.
Stereo, quad and 5.1 output, ASIO. Windows program (VST3 later).

Tonradorgel mit Rotary-Lautsprecher (Typ 122): Horn und Trommel als rotierende Schallquellen in einem simulierten Raum. Oberfläche und
Klangberechnung laufen nativ in C++ (JUCE): `Source/ui/` und `Source/RotaryRoomDSP.h`.

## Bauen über GitHub Actions

1. Push auf dieses Repository startet **Actions → Windows-Build** (beim ersten Mal 10–15 Minuten).
2. Am Ende liegen unter **Artifacts**:
   * `Wheelhouse-Setup-win64` – der Installer `Wheelhouse-Setup-<Version>.exe` (App und VST3-Plugin),
   * `Wheelhouse-win64` – nur `Wheelhouse.exe`, `Wheelhouse-VST3-win64` – nur das Plugin,
   * `Wheelhouse-map` – Linker-Map zum Auflösen von Absturzprotokollen,
   * `Wheelhouse-VST3-macOS` – das Plugin für den Mac (Universal Binary: Apple Silicon und Intel, ab macOS 10.15).

Der Workflow lädt JUCE 8.0.15, die Schriften und das Steinberg ASIO-SDK selbst herunter.
Schlägt der ASIO-Download fehl, wird ohne ASIO gebaut (nur WASAPI/DirectSound), das steht im Build-Log.

## Lokal bauen (Visual Studio 2022)

```
cmake -B build -A x64 -DASIO_SDK_DIR=C:/asiosdk/common
cmake --build build --config Release
```

JUCE wird per FetchContent geladen, falls kein Ordner `JUCE/` neben der CMakeLists liegt.

## Installieren

`Wheelhouse-Setup-<Version>.exe` starten (braucht Administratorrechte). Wählbar: App und Plugin, nur App oder nur Plugin.

* App nach `C:\Program Files\Wheelhouse\`, Startmenü-Eintrag, optional Desktop-Symbol.
* Plugin nach `C:\Program Files\Common Files\VST3\Wheelhouse.vst3` – dort suchen alle gängigen DAWs.
* Ein Update installiert man einfach drüber; laufende Instanzen werden vorher geschlossen. Deinstallieren über
  Windows-Einstellungen → Apps. Einstellungen und Presets in `%APPDATA%\Wheelhouse` bleiben dabei erhalten.
* Der Installer ist nicht signiert: Windows SmartScreen meldet beim ersten Start „unbekannter Herausgeber“ →
  „Weitere Informationen“ → „Trotzdem ausführen“.

**Mac (nur VST3):** `Wheelhouse-VST3-macOS.zip` entpacken und `Wheelhouse.vst3` nach `~/Library/Audio/Plug-Ins/VST3/`
(nur für den eigenen Benutzer) oder `/Library/Audio/Plug-Ins/VST3/` (für alle) legen. Das Plugin ist nur ad hoc
signiert, nicht von Apple notarisiert: macOS sperrt heruntergeladene Dateien, die DAW findet es dann nicht oder meldet
es als beschädigt. Einmal im Terminal freigeben:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Wheelhouse.vst3
```

Logic Pro und GarageBand laden kein VST3 (nur Audio Units); Ableton Live, Cubase, Reaper, Bitwig, Studio One schon.

Installer lokal bauen: Inno Setup 6, dann `iscc /DAppVersion=0.1.0 /DBuildDir=..\build installer\Wheelhouse.iss`.

## Voraussetzungen auf dem Zielrechner

* Windows 10/11, 64 Bit. Keine weiteren Laufzeitkomponenten nötig (C++-Laufzeit ist statisch gelinkt).
* Für ASIO: der ASIO-Treiber des Interfaces.

## Bedienung

Die Oberfläche ist englisch, mit den üblichen Begriffen aus Studio, Bühne und DAW (Drawbars, Swell, Slow/Fast/Stop,
Direct Outs …). Jeder Regler, Schalter und jede Anzeige hat einen Tooltip (Maus kurz darüber halten): was er tut,
bei Reglern zusätzlich die Bedienung.

**Instrument-Presets** (Menü oben links, Gruppe „Instrument“) stellen Orgel, Vorverstärker, Rotary und Hall auf einmal
ein, Raum, Mikros, Ausgabe und MIDI bleiben unberührt:

* **B3 + 122 (Stock)** – gepflegte B3 der 60er; Rotary-Drehzahlen nach Messungen an echten Rotary-Kabinetten
  (Horn 48/400 rpm, Drum 40/342 rpm), Serien-122 ohne Stopp, kein Hall. Startzustand beim ersten Start.
* **B3 + 147 (Rock)**, **A-100 + Spring Reverb**, **Road-worn B3**.

Das Fenster ist auf 1600 × 900 ausgelegt und skaliert mit. Oben die Registerkarten, darunter das Gehäuse
mit Rotary, Zugriegeln und Raum, unten zwei Manuale mit je 61 Tasten und ein Pedal mit 25 Tönen.

* **Play**: 9 Zugriegel Upper, 2 Pedal (16′/8′), 9 Lower; Percussion-Wippen (nur Obermanual) und Vibrato je Manual.
  Zugriegel und Knöpfe ziehen oder Mausrad, Umschalt = fein, Doppelklick = Standardwert.
* **Organ**: Condition (auch Pegelstreuung und Schlag der Tonräder: jedes Rad eiert mit eigener Stärke und Richtung,
  0,2 % neu bis etwa 4 % abgenutzt, obere Räder stärker), Übersprechen, Kontakte, Motor, Stimmung, Sammelschiene, Percussion-Pegel und Abklingzeiten,
  Netzfrequenz 50/60 Hz (Brumm aus dem Netzteil, steigt mit dem Zustand), Key-Click fest oder nach Anschlag,
  Tastengeräusch. „Motor“ ist das Surren von Generator und Laufmotor.
* **Generator**: Übersprechen wie im echten Tonradgenerator: je Kammer vier Räder im Oktavabstand (gleiche Seite
  4 Oktaven, etwa −39 dB; Nachbarseite etwa −47 dB) plus schwaches Übersprechen der Nachbarleitungen im Kabelbaum.
  Jedes Rad hat leichte 2. und 3. Harmonische (eigene Streuung), die oberen Räder werden von den Tonradfiltern bedämpft.
* **Vibrato/Chorus**: die Scanner-Einheit der B-3 als Schaltung (`ScannerVibrato.h`). Die Laufzeitkette ist das
  18-gliedrige Netzwerk aus gekoppelten Spulen (450 mH, Kopplung 0,15) und 5,6-nF-Kondensatoren mit 2,7 nF und 15 kΩ
  (R77) am Ende, getrieben von der 12BH7-Stufe: etwa 1 ms Laufzeit, flach bis 4 kHz, −3 dB bei 5,8 kHz; die
  Fehlanpassung des Abschlusses an den Wellenwiderstand (≈ 9 kΩ) ergibt Reflexionen und damit ein paar dB Welligkeit
  entlang der Abgriffe. Der Abtaster ist der kapazitive Teiler der 16 Statorplatten (≈ 25 pF) gegen die Streukapazität
  des Rotors (412 U/min); die Plattenkapazität folgt der Überlappungsfläche, dazu Exzentrizität, Plattenstreuung und
  Bauteiltoleranzen. Übergänge zwischen den Abgriffen, die leichte Amplitudenmodulation und das Ausdünnen der Höhen
  bei V3 (benachbarte Abgriffe sind dort um 4 kHz weit außer Phase) entstehen so aus der Schaltung. V1/V2/V3 lesen
  8/12/16 Abschnitte (≈ ±0,65/1,0/1,3 % Tonhöhe), C1–C3 legen einen Widerstand in Reihe zur Leitung, sodass die
  Hälfte des Signals an ihr vorbeigeht. Annahmen: Quellwiderstand des Treibers, Spulenwiderstand, Belegung der
  Abgriffe für V1/V2 (nach dem Schema der älteren 25-gliedrigen Leitung), Streukapazität. `tools/acoustics/scanner.py`
  rechnet die Leitung im Frequenzbereich, `tests/scanner_test.cpp` prüft das Echtzeitmodell dagegen.
* **Key-Click**: kein Effekt, sondern das Schalten selbst. Die neun Kontaktfedern einer Taste schließen in 0,1–0,35 ms
  (je nach Kontaktzustand) und zu leicht verschiedenen Zeiten, prellen und flattern beim Aufwischen auf die
  Sammelschiene kurz (30–120 µs), und der laufende Sinus springt mit vollem Sprung ein: das gibt den Knack samt
  Bass-Wumms. Dazu das Kratzen der Feder als bandbegrenztes Rauschen (400 Hz–3,2 kHz, 1–5 ms). MIDI-Velocity ist
  Tastengeschwindigkeit: schnell = Kontakte schnappen fast gleichzeitig und hart zu (scharf, lauter, mehr Prellen),
  langsam = nacheinander und schmatzend. Click-Regler: 0,5 etwa so laut wie ein Zugriegel auf 8, 1 = +12 dB.
  Mit der Maus: Klick vorne auf der Taste = hart, hinten = weich.
* **Rotary**: Drehzahlen und Hochlauf/Auslauf für Horn und Trommel, Bremse, Endstufe (Modell 122 / 122 früh / 147,
  Qualität Eco / HQ / Ultra, Drive, Netzteil-Sag), Gehäuse (Hornfach, Resonanz, Mechanik, Unrund, Sperrholz/Spanplatte,
  Deflektoren In/Out), Aufstellung (Balance, Ausrichtung).
* **Pedal**: wie in B-3/C-3 verdrahtet – acht Kontakte je Pedal auf vier Sammelschienen (Grundton + 3., 2. + 4.,
  6. + 8., 10. + 12. Harmonische), über die Widerstandsdrähte R81–R86 auf die Zugriegel 16′ und 8′ gemischt. Die
  untersten zwölf Töne kommen von den Complex-Tonrädern (fast rechteckig: ungerade Harmonische).
  Schalter **Bass B-3 / Sustain** (Orgel → Pedal): B-3 verstummt beim Loslassen wie das Original; Sustain lässt den Ton
  ausklingen (Länge 1–5, T60 0,35–3,5 s), wie bei den Hammonds mit Pedal-Sustain (Spinette, H-100, X-66, eine
  europäische A-100) und den späteren Clones – ohne Loslass-Click, die Sustain-Schaltungen tasteten elektronisch.
* **Preset-Tasten und Zugriegelsätze**: links an jedem Manual die umgekehrt gefärbte Oktave des B-3 / C-3. Eine Taste
  bleibt gedrückt, bis eine andere kommt: C = Cancel (Manual stumm), C♯–A = Werksregistrierungen (oben Stopped Flute …
  Full Swell, unten Cello … Full Great mit 16′), A♯ = Zugriegelsatz A, B = Zugriegelsatz B. Die Umschalter A/B über den
  Zugriegeln sind dieselben Tasten; die Zugriegel zeigen und bewegen den zuletzt gewählten Satz, bei eingerastetem
  Werkspreset gedimmt. Wie am Original klingt die Percussion **nur auf B** des oberen Manuals (ihr Auslöser ist der
  1′-Kontakt des B-Satzes), nur dort fällt der 1′ weg. MIDI: die Oktave unter einem Manual (ab Werk C1–H1) sind seine
  Preset-Tasten; Host-Parameter „Upper/Lower Preset Key“ zum Automatisieren. Ältere Einstellungen: ihr Satz wird B.
* **Modus Listener / Mics** (Kopfleiste): Listener = du stehst im Raum; Mics = Nahmikros und Raumpaar über das Pult.
  Umschalten blendet weich über, der jeweils andere Teil wird nicht gerechnet.
* **Mics**: Nahmikros am Horn (Mono oder Stereo mit 90°/120°/180°) und an der Trommel (Mono oder Stereo), ein Raumpaar
  (AB, XY, ORTF), das im Grundriss gezogen und gedreht wird. Mikrotypen: 57, 421, D112, RE20, 87 (Niere/Kugel/Acht), KM 84,
  121 (Bändchen) – je mit Frequenzgang, Richtcharakteristik je Band, Nahbesprechungseffekt und Windempfindlichkeit.
  Pult mit drei Gruppen (Horn, Trommel, Raum): Stereo-Kanal mit Balance und Breite oder Mono-Kanal mit Panorama, dazu
  Fader, Polarität, Stumm/Solo, Laufzeit und Ziel (vorne/hinten oder Einzelausgang bis 16). Beim Wechsel Mono ↔ Stereo
  wandern die Mikros, das zweite blendet ein oder aus.
* **Room**: Precision „Exact“ rechnet jede Reflexion bis zur gewählten Ordnung pro Sample (Position, Doppler,
  Richtwirkung, Panorama); „Fast“ fasst ab der 2. Ordnung nach Richtung zusammen. Exact mit 4.–5. Ordnung ist für starke Rechner.
* **Room**: großer Grundriss (Rotary und Kopf ziehen, rechts ziehen = Blickrichtung, Mausrad = drehen), Maße, Material,
  Reflexionsordnung, Vorlagen. Absorption und Nachhall werden in Oktaven von 125 Hz bis 8 kHz gerechnet: der
  Absorptionsregler ist der Wert bei 500 Hz–1 kHz, das Material gibt die Form über die Oktaven (Holz/Trockenbau
  schluckt als Plattenschwinger Bass und verliert oben etwas, Beton/Stein/Fliesen schlucken unten fast nichts und oben
  etwas mehr; Tabellenwerte für möblierte Räume), dazu die Luftdämpfung je Oktave (ISO 9613, 20 °C, 50 % r. F.).
  Daraus die Nachhallzeit je Oktave nach Eyring; in einer Kirche bleiben so die Mitten lang, während 4 und 8 kHz in der
  Luft sterben. Der Nachhall (16-Kanal-FDN) hat je Leitung ein Siebenband-Dämpfungsfilter, dessen Bandverstärkungen
  per Gauss-Newton so gesetzt werden, dass die Schleife in jeder Oktave die Eyring-Zeit trifft (Allpass-Laufzeit in
  der Schleife eingerechnet); `tests/reverb_test.cpp` misst die Impulsantwort der Schleife dagegen (±12 %).
  Die Statuszeile der Karte zeigt RT60 bei 125 Hz, 500 Hz, 2 kHz und 8 kHz. Dieselbe Luft dämpft jeden einzelnen
  Schallweg (Direktschall, Spiegelbilder, Wandfelder, Mikrofonwege): ein Einpol je Weg, dessen Eckfrequenz so liegt,
  dass der Verlust bei 8 kHz dem ISO-Wert für die Weglänge entspricht (bis 30 m zwischen 4 und 16 kHz auf ±0,6 dB). Die Linien im Grundriss zeigen, was die Engine gerade rechnet: Direktschall,
  Spiegelquellen bis 2. Ordnung und die angestrahlten Wandfelder, jeweils mit ihrem Pegel (Horn orange, Trommel braun).
* **Output**: Stereo / Quad / 5.1, LFE, Master, freie Kanalzuordnung (Routing), Kanaltest. Monitoring (Quad/5.1): Trimm für
  die hinteren Kanäle und den Center, Divergence (Quelle vor dir zusätzlich als Phantom auf L/R).
* **Abbildung**: Direktschall und der spiegelnde Teil jeder Reflexion werden leistungskonstant zwischen den zwei
  nächsten Lautsprechern gelegt. Der Rest – was die Wand streut (Streugrad je Material, ISO 17497, Holz/Trockenbau
  möbliert 0,5) und was steil von Boden oder Decke kommt – bleibt in seiner Richtung, wird aber breit verteilt und
  vorher dekorreliert (vier Allpässe je Richtung). Die Streuung der 1. Ordnung kommt nicht vom Spiegelbild, sondern
  von den Wandstellen, die der Rotor gerade anstrahlt: jede Fläche ist in bis zu 4 × 4 Felder (~2,5 m) geteilt, jedes
  Feld ist ein eigener Schallweg (Lambert-Streuung, Abstrahlung des Rotors zum Feld, Doppler, Richtung zum Hörer).
  So wandert der gestreute Klang über die Wände, während Horn und Trommel drehen. Je Rotor rechnen nur die stärksten
  Felder (zusammen 97 % der Streuenergie, höchstens 24); höhere Ordnungen streuen wie bisher am Spiegelbild. Grund: im echten Raum treffen Direktschall und Bodenreflexion aus
  verschiedenen Richtungen auf die Ohren und löschen sich nicht aus; im selben Lautsprecherkanal elektrisch summiert
  würden sie kammfiltern und die Drehung vorne verschmieren. Jeder Wandkontakt nimmt dem Hornstrahl Höhen. Der
  Nachhall ist auf die Summe aller Lautsprecher normiert, damit Stereo, Quad und 5.1 gleich viel Raum haben.
* **Bewegung**: alle drehenden Schallwege folgen dem Rotor ohne Stufen. Die Laufzeit (Doppler) wird pro Sample mit
  einem 8-Tap-Sinus-Interpolator gelesen (bis 12 kHz unabhängig von der Nachkommastelle, ein kubischer ließ die Obertöne
  auf Slow mit ~100 Hz flattern); die Richtung einer Reflexion gleitet zwischen den 15°-Richtungsfächern, statt
  umgeschaltet zu werden; der breite Anteil wird erst gepannt und dann je Lautsprecherkanal dekorreliert (gleicher
  Diffusor in allen Kanälen, damit L/R so zusammenhängend bleiben wie vorher). Hornabstrahlung und Hornfach-Anteil
  haben keine Knicke über den Drehwinkel.
* **Rotoren**: wie im 122 drehen Horn und Trommel gegeneinander (Horn gegen, Trommel mit dem Uhrzeigersinn von oben).
  Doppler-Radius Horn 17,8 cm (Mund), Trommel 13 cm (akustisches Zentrum der Schaufel laut Simulation, nicht der
  Außenradius). Die Abstrahlung beider Rotoren stammt aus Randelement-Simulationen (`tools/acoustics`): das Signal
  wird einmal je Rotor in phasengleiche Linkwitz-Riley-Bänder zerlegt (Horn 4 Bänder, Trommel 3), jeder Schallweg
  mischt sie mit den Pegeln für seinen Winkel. Weil die Bänder phasengleich sind, ändert die Drehung nur den Pegel –
  keine falsche Tonhöhenmodulation. Dasselbe Modell gilt für Direktschall, Spiegelbilder, Wandfelder, Mikros
  und die Einspeisung in den Nachhall. Mit Deflektor (Serie) schwankt das Horn über 2 kHz um ~7–13 dB, ohne um
  20–40 dB; die Trommel unter 200 Hz um < 2,5 dB, von 250–800 Hz um 6–17 dB.
* **Gehäuse als Strahler**: die Rotoren sitzen in einem Kasten, und erst der bestimmt, was in welche Richtung
  hinauskommt. Statt einer handgemachten Richtcharakteristik (vorne offen, hinten leiser und dumpfer) steht in
  `Source/CabinetDirectivity.h` eine Simulation des 122-Gehäuses (`tools/acoustics/cabinet.py`): der Kasten
  (104 × 74 × 52 cm) als 3D-Randelementmodell, die Lamellenflächen vorne und an beiden Seiten des Horn- und des
  Trommelfachs als Öffnungen, die das direkt einfallende Feld des Rotors (seine eigene BEM-Richtcharakteristik, Mund
  als Kolbenscheibe, Streuung an den Lamellen) nach außen abgeben. Bis 2 kHz BEM, darüber Kirchhoff-Aperturen mit
  Kantenbeugung nach Maekawa. Ergebnis: Bandpegel je Rotorwinkel (7,5°), Abstrahlrichtung (7,5°) und Elevation
  (15°), für Horn mit/ohne Deflektor und Trommel. Darin stecken Abschattung und Beugung um den Kasten (unter 300 Hz
  praktisch rundum, bei 2 kHz hinten 15–20 dB leiser), die Bündelung der Lamellenflächen und ihre gegenseitige
  Interferenz. Normiert ist je Terz auf die mittlere Leistung des bisherigen Modells, die Klangfarbe im diffusen
  Feld bleibt also, nur die Richtungs- und Drehwinkelabhängigkeit ändert sich. Der nicht direkt austretende Anteil
  des Horns ist das Hornfach: ein kurzer, dichter Nachhall des Oberfachs (0,70 × 0,48 × 0,25 m, mittlere freie
  Weglänge 0,30 m, RT60 ≈ 50 ms aus offener Lamellenfläche und Absorption), der durch alle drei Lamellenflächen
  gleich austritt; Regler Horn Box = sein Pegel.
* **MIDI**: Kanäle für Upper (oder Omni), Lower und Pedal, Controller für Rotary und Swell. Alle MIDI-Eingänge sind aktiv.
* **Split und Oktaven**: ein Keyboard für mehrere Manuale – „2“ = Lower | Upper, „3“ = Pedal | Lower | Upper, auf dem
  Kanal des Obermanuals. Trennpunkte in der Leiste über den Manualen (‹ › halbtonweise, ● = nächste Taste am Keyboard
  lernen) und in der Karte MIDI. Jedes Manual und das Pedal lässt sich rechts daneben um ±3 Oktaven verschieben.
  Auf den Manualen ist abgedunkelt, was das Keyboard nicht erreicht, die Grenzen sind gold markiert (Keyboardgröße
  in der Karte MIDI). Ein losgelassener Ton geht immer dorthin, wo er angeschlagen wurde.
* **Audio**: Kerne (Auto = halbe Kernzahl, höchstens 8, oder 1–16): Raum und Mikros rechnen auf mehreren Threads,
  jeder rendert einen Teil der Schallwege für den 64er-Teilblock. Gerät, Treiber, Puffer und Latenz; „Audio Settings …“ öffnet den Dialog (ASIO / Windows Audio).
* QWERTY-Tasten: Z–M und Q–P (auf QWERTZ Y–M), wählbar für Upper, Lower oder Pedal. Leertaste = Rotary, Esc = Panic.
* Einstellungen und Oberflächenzustand liegen in `%APPDATA%\Wheelhouse\`.
* **Dropouts** (Karte Audio) zählt Blöcke, die still ausgegeben wurden, weil eine Einstellung gerade den Klangkern belegt hat (sollte bei normalem Spielen 0 bleiben).

## VST3

Derselbe Klangkern und dieselbe Oberfläche als Instrument-Plugin (`Wheelhouse.vst3`). Der Installer legt es nach
`C:\Program Files\Common Files\VST3\`; ohne Installer den Ordner `Wheelhouse.vst3` aus dem Artefakt
„Wheelhouse-VST3-win64“ dorthin kopieren.

* **Ausgänge**: Hauptbus Stereo, Quad oder 5.1 (je nach Spur der DAW) plus vier optionale Stereo-Busse
  „Direct 9/10“ … „Direct 15/16“ für die Mikro-Einzelausgänge (Ziel im Mixer der Karte Mics). Der Hauptbus heißt „Main“.
* **MIDI** vom Host, Noten sampelgenau. Kanalzuordnung für Upper/Lower/Pedal wie in der App (Karte MIDI).
* **Automation**: alle 20 Zugriegel, Rotary (Stop/Slow/Fast), Swell, Drive, Vibrato/Chorus, Vibrato
  Upper/Lower, Percussion (On, Soft, Fast, 3rd). Die Parameter-IDs sind unverändert, ältere Automation bleibt erhalten. Bewegungen in der Oberfläche werden als Automation aufgezeichnet.
* **Projekt**: alle übrigen Einstellungen (Raum, Mikros, Orgel, Rotary …) werden mit dem Projekt gespeichert und
  auch ohne geöffnetes Fenster geladen.
* **Latenz** der Röhren-Oversampling-Filter wird an den Host gemeldet (Ausgleich durch die DAW).
* **Bounce**: meldet der Host „nicht Echtzeit“, rechnet Wheelhouse mit 5. Ordnung, Exakt und Ultra-Endstufe
  (abschaltbar in der Karte Audio: „Bounce: As set“); danach wieder mit den eigenen Einstellungen.
* **Kerne**: Auto nimmt im Plugin ein Viertel der Kerne, höchstens 4.

## Endstufe

`Source/RotaryAmp.h` rechnet die Rotary-Endstufe als Schaltung: 12AU7-Treiber, Gegentakt-Endstufe mit 2 × 6550,
gemeinsamer Kathodenwiderstand, Ausgangsübertrager mit Kern-Hysterese, Netzteil mit Sag (beim 5U4 deutlich weicher).
**HQ** löst die Röhren mit Newton-Iteration bei 2-fachem Oversampling, **Ultra** zusätzlich mit Drossel-Netzteil,
Wirbelströmen und 4-fachem Oversampling. **Eco** ist eine statische Gegentakt-Kennlinie.

## Weiche und Lautsprecher

`Source/Speakers.h` rechnet alles hinter dem Ausgangsübertrager als eine Schaltung, und die Endstufe sieht sie in
jedem (überabgetasteten) Schritt als ihre Last – bei Eco über eine Ersatzquelle mit dem Innenwiderstand der Stufe:

* **Weiche** 800 Hz, 2. Ordnung, 16 Ω, Werte aus dem Leslie-Schaltplan: Woofer 5,2 mH in Serie, 12,5 µF parallel;
  Horn 7,8 µF in Serie, 3,2 mH parallel. An ohmschen 16 Ω lägen die Zweige bei 620 Hz und 1 kHz – erst die echten
  Treiberimpedanzen bringen sie auf 800 Hz.
* **Jensen 15"** als elektromechanisches Modell (Schwingspule mit Semi-Induktivität, Antrieb, Membran, Federung)
  im Bassreflex-Mittelfach (100 l, 44 Hz, undichtes Fach), Abstrahlung als Kolben in den Halbraum.
* **Jensen V21** ebenso, dazu Rückkammer, Kompressionskammer und der Hornweg als Wellenleiter (Krümmer + Horn nach
  der Geometrie aus `tools/acoustics`, Wandverluste, Mündung als Strahlungswiderstand ∥ Mündungsmasse). Die
  zurücklaufende Welle wirkt auf die Membran: daher die Stehwellen bei 1,2 und 3 kHz (±5 dB) und das Abrollen über
  4 kHz.
* **Rückwirkung**: die 122 hat keine Gegenkopplung, ihre Pentoden treiben die Weiche fast wie eine Stromquelle
  (≈ 110 Ω an der 16-Ω-Wicklung). Impedanzspitzen werden dadurch lauter – ein Bassbuckel um 80 Hz, wo Woofer und
  Bassreflex die höchste Impedanz haben. Die 147 mit Kathoden-Gegenkopplung bleibt straffer.
* **Nichtlinear**: Antriebskraft und Federung des 15" hängen vom Hub ab (Spule 0,6 mm außermittig: k2 ≈ 3 % bei voller
  Leistung, 100 Hz), Kompressionskammer und Hornhals des V21 verzerren die Luft (k2 ≈ 8 % bei voller Leistung,
  proportional zum Pegel), beide Schwingspulen erwärmen sich mit zwei Zeitkonstanten (20 s laut ins Horn: −0,9 dB).

Treiberdaten sind Schätzungen aus Unterlagen der Zeit, keine Messungen. `tools/acoustics/speakers.py` rechnet dasselbe
Netzwerk im Frequenzbereich; `tests/speakers_test.cpp` vergleicht das Echtzeitmodell damit (±0,5 dB).

## Vorverstärker

`Source/OrganPreamp.h` ist der Orgel-Vorverstärker AO-28 der B-3 als Schaltung, Bauteil für Bauteil nach Schaltplan und
Stückliste: Anpassübertrager → V1/V2 6AU6 (Trocken- und Vibratokanal; Anode 470 kΩ, Schirm über 3,9/2,7 MΩ auf 60 V,
gemeinsame 1,2-kΩ-Kathode mit 30 µF – die Röhren laufen mit 0,3 mA knapp über der Sperrgrenze, daher ihre zweite
Harmonische; das Schirmgitternetz sackt mit 1,3 s unter Last) → V2 über 4,7 nF auf den 12BH7-Kathodenfolger, der die
Vibratoleitung treibt (Scanner, siehe oben) → Mischknoten (2 × 1,8 MΩ auf 3,3 MΩ) → V4A 12AX7 (330 kΩ) mit Gitterstrom
gegen den 1,1-MΩ-Knoten → 33 nF → **Schweller** → V4B 12AX7 → 33 nF / 470 kΩ → V3A 12BH7 **im Eintakt** (1,2 kΩ, 12 V,
10 mA) auf den Ausgangsübertrager, dessen Kern von diesen 10 mA vormagnetisiert ist (Hysterese, einseitige Sättigung).
Weil V1, V2 und V4A vor dem Schweller liegen, knurrt eine B-3 mit vollem Register auch bei zurückgenommenem Pedal;
der Gain-Regler ist der Pegel der Anpassübertrager (±11 dB um 12 mV am 6AU6-Gitter bei vollem Register). Angenommen:
dieser Pegel, der Bias-Abgriff des Folgers, der Übertrager (30 H, ≈ 15 kΩ reflektiert, halbe Sättigung) und die feste
Dämpfung des Schwellernetzes. Die Unter-5-Hz-Schleife über die gemeinsame Kathode (V1 → V4A → Kathode → V1) ist nicht
modelliert. Qualität wie bei der Endstufe (Eco: lineare Eingangsseite und statische Kennlinien; HQ: die Schaltung je
Sample, Ausgangsstufe 2× überabgetastet; Ultra: zusätzlich Miller-Kapazitäten, nachgebende Versorgung, Wirbelströme,
4×).

## Federhall

`Source/SpringReverb.h`: Orgel-Federhall wie in A-100 und 122RV, in der Orgel vor dem Rotary. Zwei oder drei Federn als
Rückkopplungsschleifen mit Ketten gedehnter Allpässe (Dispersion – das typische Zirpen), leicht schwankender Laufzeit,
weich sättigendem Treiber und bandbegrenztem Wandler; „Kick“ schlägt die Federn an. Modell nach Välimäki, Parker &
Abel (2010) und Parker (2011). Regler: Reverb (vorne), in der Karte Organ Mix, Decay, Tone, Drive, Springs.

## Gehäuse und Mechanik

`Source/Mechanics.h`. Alles hier sind *ruhende* Schallquellen: sie drehen sich nicht mit, sondern werden wie das
Gehäuse selbst in den Raum gestellt (Laufzeit, Abstand, Richtung zum Hörer und zu jedem Mikro, Einspeisung in den Hall).

* **Gehäuse**: das Mittelfach des 122 ist die Bassreflexbox des 15″-Tieftöners (Hochpass 4. Ordnung um 40 Hz),
  das Trommelfach hat Luftmoden (c/2L bei 0,70 m und 0,48 m). Die Holzwände (Seiten, Deckel, Rückwände, Front des
  Tieftönerfachs, Boden) schwingen als Platten: Eigenfrequenzen aus Maßen, Stärke und Material berechnet
  (Sperrholz 19 mm / 12,7 mm, E ≈ 8 GPa; Spanplatte E ≈ 3,2 GPa, stärker gedämpft), je drei Moden, angeregt vom
  Druck im Horn- bzw. Tieftönerfach.
* **Rotary-Mechanik**: Induktionsmotoren brummen mit doppelter Netzfrequenz (die kleinen Langsam-Motoren
  schnarrender), Riemen, Spannrolle und Lager rumpeln mit der Drehzahl, die Bespannung der Trommel rauscht im
  Quadrat der Drehzahl, beim Umschalten klickt das Relais und der Langsam-Motor kuppelt mit einem Klonk ein und aus.
* **Orgel-Mechanik**: Laufmotor (zwei Drehmomentstöße je Netzperiode), Zahneingriff der 12 Antriebsräder auf der
  Hauptwelle (20 U/s × Zähnezahl, 1,3–2,2 kHz) unter der Filzdecke, Surren des Generators. Tasten schlagen am Ende
  des Hubs auf den Filz (je nach Anschlag), beim Loslassen leiser oben an; Pedale poltern tiefer. Die Orgel steht
  direkt vor dem Hörer.
* Die Pegel sind geschätzt, nicht gemessen: Leerlauf-Mechanik bei mittlerem Zustand etwa 35–40 dB unter einem
  vollen Akkord, Tastenanschlag etwa 30 dB darunter. `tests/mechanics_test.cpp` misst alle Pegel nach.

## Abstrahlung simulieren (`tools/acoustics`)

Eigene Randelement-Löser in Python/NumPy, an der Kugel gegen die analytische Lösung geprüft (`test_sphere.py`,
`test3d.py`):

* `axibem.py` – rotationssymmetrisches BEM (Ringquellen, CHIEF gegen innere Resonanzen); `horn.py` baut damit das
  122-Horn nach (geschätzte Maße: Hals r 9,5 mm, Länge 150 mm, Mund r 45 mm, Wand 3 mm; Deflektorkegel r 51 mm,
  30 mm tief, Spitze 8 mm im Mund = Variante `g-8h30`, `none` = ohne) und rechnet Polardiagramme je Terzband
  (8 Frequenzen je Band gemittelt).
* `bem3d.py` – 3D-BEM mit Vierecken; `drum.py` baut die Trommel als Voxelmodell (2,5 cm, r 0,215 m, h 0,25 m,
  Schaufel, 15″-Kolben darunter, Prallbrett).
* `cabinet.py` – das Gehäuse (siehe oben): geschlossener Kasten aus 2,5-cm-Vierecken (~5400 Elemente), Lamellenzonen
  als Neumann-Randbedingung mit dem Einfallsfeld des Rotors, 14 Terzen 100 Hz–2 kHz per BEM (CHIEF-Punkte im
  Kasten, 3 Frequenzen je Terz, 48 Rotorwinkel × 3 Streurealisierungen als rechte Seiten), 12 Terzen 1,25–16 kHz
  per Kirchhoff/Maekawa; etwa eine Stunde. `fit_cabinet.py` fügt beides zusammen (Pegelabgleich im Überlappbereich),
  normiert je Terz auf das bisherige Modell und schreibt `Source/CabinetDirectivity.h`.
* `fit_directivity.py` – glättet, normiert (mittlere Leistung über den Kreis je Band = bisheriges Modell, die
  Klangfarbe im diffusen Feld bleibt) und passt die Bandpegel an; schreibt `Source/RotorDirectivity.h`.
  `results/` enthält die Simulationsergebnisse, mit denen die Tabelle erzeugt wurde.
* Grenzen: die Maße sind aus Fotos und Datenblättern geschätzt, nicht gemessen; das achssymmetrische Horn hat bei
  0°/180° einen Lichtfleck, der darum über ±15° geglättet und ab 155° festgehalten wird.

## Dateien

* `Source/RotaryRoomDSP.h` – Klangkern (Orgel, Rotary, Raum, Hall), ohne JUCE-Abhängigkeit: Klasse und Schnittstelle;
  die Rechnung steckt nach Themen in `Source/core/`:
  `Basics.h` (Bausteine, Datentypen, Raum-Konstanten), `Setup.h` (Tabellen, Stimmung, Charakter),
  `Organ.h` (Tasten, Kontakte, Perkussion, Scanner), `Room.h` (Spiegelquellen, Wandfelder, Geometrie je Block,
  Lautsprecher), `Mics.h` (Mikros), `Render.h` (Ablauf je Teilblock: Rotoren, parallele Schallwege, Hall, Ausgang)
* `Source/RotaryAmp.h` – Röhrenendstufe und Ausgangsübertrager
* `Source/Speakers.h` – Weiche, 15"-Woofer im Bassreflexfach, V21 mit Hornwellenleiter (Last der Endstufe)
* `Source/OrganPresets.h` – Werksregistrierungen der Preset-Tasten (DSP und Oberfläche)
* `Source/OrganPreamp.h` – Orgel-Vorverstärker (AO-28)
* `Source/SpringReverb.h` – Federhall
* `Source/Mechanics.h` – Rotary-Gehäuse (Bassreflex, Plattenmoden) und Mechanikgeräusche von Rotary und Orgel
* `Source/MicRig.h` – Mikrotypen und Pult (die Berechnung steckt in `Source/core/Mics.h`)
* `Source/RotorRadiation.h`, `Source/RotorDirectivity.h`, `Source/CabinetDirectivity.h` – Bandzerlegung,
  Abstrahltabellen von Horn und Trommel (frei) und von Horn und Trommel im Gehäuse
* `Source/Engine.*` – gemeinsamer Kern von App und Plugin: Nachrichten der Oberfläche, Echtzeit-Regeln, gespeicherter Zustand, Parameter
* `Source/MainComponent.*` – App: Audio-/MIDI-Geräte und Fenster (JUCE)
* `Source/PluginProcessor.*` – VST3: Busse, Parameter, Projektzustand, Editor
* `Source/ui/` – die Oberfläche (JUCE-Komponenten), von App und Plugin geteilt; feste Bühne 1600 × 900, die aufs
  Fenster skaliert wird: `NativeUi.h` (Hauptkomponente, Ereignisse der Engine, Tastatur), `UiState.h` (Zustand als
  JSON mit denselben Schlüsseln wie früher, Vorgaben, Presets, Übernahme alter Speicherstände), `Actions.h` (Sender,
  Positionen, Presets, Kanal-Zuordnung), `Controls.h` (Drehregler, Umschalter, Auswahl, Abschnitte, Tooltips),
  `Cards.h` (Einstellungskarten, Mikro-Pult), `PlaySurface.h` (Rotary-Kabinett, Zugriegel, Grundriss),
  `Keys.h` (Manuale, Pedal, Schweller), `Header.h` (Kopfleiste), `Theme.h` (Farben, Schriften, Holz)
* Code-Stil: `.clang-format` (`clang-format -i Source/*.h Source/*.cpp Source/core/*.h Source/ui/*.h tests/*.cpp`),
  Zeilen bis 120 Zeichen
* `Source/Workers.h` – Thread-Pool für die Raumberechnung
* `Source/FastMath.h` – schnelle atan-Näherung für die Röhrenmodelle
* `installer/` – Inno-Setup-Skript `Wheelhouse.iss`, Setup-Icon und Bilder des Assistenten
* `Resources/icon/` – App-Icon (Rotary über Zugriegeln); `python3 tools/make_icon.py` erzeugt SVG und PNG neu
* `tests/` – Messungen und Prüfungen (`g++ -O2 -std=c++17 -ISource tests/<name>.cpp`):
  `regression.cpp` (Referenz aufnehmen/vergleichen, Klang muss bitgleich oder ≥ 90 dB identisch bleiben),
  `asan_all_test.cpp` (alle Betriebsarten mit AddressSanitizer), `threads_test.cpp` (mehrere Kerne: gleiches Ergebnis wie ein Kern),
  `bench.cpp` (Rechenlast je Stufe), `organ_features_test.cpp` (Click nach Anschlag, Scanner),
  `mechanics_test.cpp` (Pegel der Mechanik, Plattenresonanzen, Pedal-Spektrum),
  `cabinet_test.cpp` (Gehäusetabellen: Interpolation, Abschattung vorn/hinten, Drehung des Gehäuses),
  `speakers_test.cpp` (Lautsprechernetzwerk gegen die Frequenzbereichsrechnung, Klirr, Spulenerwärmung),
  `scanner_test.cpp` (Vibrato-Leitung: Laufzeit, Grenzfrequenz, Welligkeit; Abtaster: Tonhöhenhub V1–V3, Rate, Pegel),
  `reverb_test.cpp` (Oktavband-Dämpfungsfilter; Nachhallzeit der Hallschleife je Oktave gegen Eyring),
  `engine_test.cpp` (Parameter, Projektzustand, Neuaufbau, Bounce – braucht die JUCE-Module juce_core und juce_audio_basics); Tests mit `-pthread` bauen,
  dazu Einzelmessungen für Endstufe, Vorverstärker, Federhall und Mikros;
  `ui_state_test.cpp` (alte Speicherstände laden in die native Oberfläche – braucht die JUCE-GUI-Module und ein
  X-Display, unter Linux z. B. `xvfb-run`)

## Lizenz

Wheelhouse steht unter der **GNU Affero General Public License v3.0** (`LICENSE`).
Die Röhren- und Übertragermodelle von Endstufe und Vorverstärker beruhen auf `dsp/glow.c` und `dsp/iron.c` aus
[Amberwave](https://github.com/lfrank95/amberwave) (AGPL-3.0, © The Amberwave contributors).
JUCE wird unter der AGPL-Option genutzt. Schriften: Barlow Condensed (SIL OFL 1.1), Yellowtail (Apache 2.0) –
der Build-Workflow lädt sie aus dem Google-Fonts-Repository.
