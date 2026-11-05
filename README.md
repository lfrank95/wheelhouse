# Wheelhouse

Open-source Hammond tonewheel organ with a physically modelled Leslie standing in a real room.
Stereo, quad and 5.1 output, ASIO. Windows program (VST3 later).

Hammond-Tonradorgel mit Leslie 122 als rotierende Schallquellen in einem simulierten Raum. Die Oberfläche
läuft in einem eingebetteten Browserfenster, die Klangberechnung nativ in C++ (`Source/LeslieRoomDSP.h`).

## Bauen über GitHub Actions

1. Push auf dieses Repository startet **Actions → Windows-Build** (beim ersten Mal 10–15 Minuten).
2. Am Ende liegt unter **Artifacts** die Datei `Wheelhouse-win64.zip` mit `Wheelhouse.exe`.

Der Workflow lädt JUCE 8.0.15, das WebView2-Paket (NuGet) und das Steinberg ASIO-SDK selbst herunter.
Schlägt der ASIO-Download fehl, wird ohne ASIO gebaut (nur WASAPI/DirectSound), das steht im Build-Log.

## Lokal bauen (Visual Studio 2022)

```
nuget install Microsoft.Web.WebView2 -Version 1.0.3485.44 -OutputDirectory packages
cmake -B build -A x64 -DASIO_SDK_DIR=C:/asiosdk/common -DJUCE_WEBVIEW2_PACKAGE_LOCATION=%cd%/packages
cmake --build build --config Release
```

JUCE wird per FetchContent geladen, falls kein Ordner `JUCE/` neben der CMakeLists liegt.

## Voraussetzungen auf dem Zielrechner

* Windows 10/11 mit WebView2-Runtime (Windows 11: immer vorhanden, Windows 10: meist über Edge).
* Für ASIO: der ASIO-Treiber des Interfaces.

## Bedienung

**Instrument-Presets** (Menü oben links, Gruppe „Instrument“) stellen Orgel, Vorverstärker, Leslie und Hall auf einmal
ein, Raum, Mikros, Ausgabe und MIDI bleiben unberührt:

* **B3 + Leslie 122 (Original)** – gepflegte B3 der 60er; Leslie-Drehzahlen nach Messungen an echten Leslies
  (Horn 48/400 U/min, Trommel 40/342 U/min), Serien-122 ohne Stopp, kein Hall. Startzustand beim ersten Start.
* **B3 + Leslie 147 (Rock)**, **A-100 + Federhall**, **Tour-B3 (abgerockt)**.

Das Fenster ist auf 1600 × 900 ausgelegt und skaliert mit. Oben die Registerkarten, darunter das Gehäuse
mit Leslie, Zugriegeln und Raum, unten zwei Manuale mit je 61 Tasten und ein Pedal mit 25 Tönen.

* **Spielen**: 9 Zugriegel Ober, 2 Pedal (16′/8′), 9 Unter; Percussion-Wippen (nur Obermanual) und Vibrato je Manual.
  Zugriegel und Knöpfe ziehen oder Mausrad, Umschalt = fein, Doppelklick = Standardwert.
* **Orgel**: Zustand (auch Pegelstreuung und Schlag der Tonräder: jedes Rad eiert mit eigener Stärke und Richtung,
  0,2 % neu bis etwa 4 % abgenutzt, obere Räder stärker), Übersprechen, Kontakte, Motor, Stimmung, Sammelschiene, Percussion-Pegel und Abklingzeiten,
  Netzfrequenz 50/60 Hz (Brumm aus dem Netzteil, steigt mit dem Zustand), Key-Click fest oder nach Anschlag,
  Tastengeräusch. „Motor“ ist das Surren von Generator und Laufmotor.
* **Generator**: Übersprechen wie im echten Tonradgenerator: je Kammer vier Räder im Oktavabstand (gleiche Seite
  4 Oktaven, etwa −39 dB; Nachbarseite etwa −47 dB) plus schwaches Übersprechen der Nachbarleitungen im Kabelbaum.
  Jedes Rad hat leichte 2. und 3. Harmonische (eigene Streuung), die oberen Räder werden von den Tonradfiltern bedämpft.
* **Vibrato/Chorus**: Scanner als 16-stufige LC-Laufzeitkette, der Abtaster gleitet über die Abgriffe;
  mit höherer Stufe mehr Laufzeit und etwas mehr Höhenverlust.
* **Anschlag**: MIDI-Velocity steuert Key-Click (Kontakte schließen schneller und härter, mehr Prellen) beim Drücken und
  Loslassen. Mit der Maus: Klick vorne auf der Taste = hart, hinten = weich.
* **Leslie**: Drehzahlen und Hochlauf/Auslauf für Horn und Trommel, Bremse, Endstufe (Modell 122 / 122 früh / 147,
  Qualität Eco / HQ / Ultra, Drive, Netzteil-Sag), Gehäuse (Hornfach, Resonanz, Mechanik, Unrund, Sperrholz/Spanplatte),
  Aufstellung (Balance, Ausrichtung).
* **Pedal**: wie in B-3/C-3 verdrahtet – acht Kontakte je Pedal auf vier Sammelschienen (Grundton + 3., 2. + 4.,
  6. + 8., 10. + 12. Harmonische), über die Widerstandsdrähte R81–R86 auf die Zugriegel 16′ und 8′ gemischt. Die
  untersten zwölf Töne kommen von den Complex-Tonrädern (fast rechteckig: ungerade Harmonische).
* **Modus Hörer / Mikros** (Kopfleiste): Hörer = du stehst im Raum; Mikros = Nahmikros und Raumpaar über das Pult.
  Umschalten blendet weich über, der jeweils andere Teil wird nicht gerechnet.
* **Mikros**: Nahmikros am Horn (Mono oder Stereo mit 90°/120°/180°) und an der Trommel (Mono oder Stereo), ein Raumpaar
  (AB, XY, ORTF), das im Grundriss gezogen und gedreht wird. Mikrotypen: 57, 421, D112, RE20, 87 (Niere/Kugel/Acht), KM 84,
  121 (Bändchen) – je mit Frequenzgang, Richtcharakteristik je Band, Nahbesprechungseffekt und Windempfindlichkeit.
  Pult mit drei Gruppen (Horn, Trommel, Raum): Stereo-Kanal mit Balance und Breite oder Mono-Kanal mit Panorama, dazu
  Fader, Polarität, Stumm/Solo, Laufzeit und Ziel (vorne/hinten oder Einzelausgang bis 16). Beim Wechsel Mono ↔ Stereo
  wandern die Mikros, das zweite blendet ein oder aus.
* **Raum**: Genauigkeit „Exakt“ rechnet jede Reflexion bis zur gewählten Ordnung pro Sample (Position, Doppler,
  Richtwirkung, Panorama); „Schnell“ vereinfacht ab der 2. Ordnung. Exakt mit 4.–5. Ordnung ist für starke Rechner.
* **Raum**: großer Grundriss (Leslie und Kopf ziehen, rechts ziehen = Blickrichtung, Mausrad = drehen), Maße, Material,
  Reflexionsordnung, Vorlagen; Zirkuszelt mit Strahlenverfolgung.
* **Ausgabe**: Stereo / Quad / 5.1, LFE, Ausgangspegel, freie Kanalzuordnung, Kanaltest.
* **MIDI**: Kanäle für Ober (oder Omni), Unter und Pedal, Controller für Leslie und Schweller. Alle MIDI-Eingänge sind aktiv.
* **Audio**: Kerne (Auto = halbe Kernzahl, höchstens 8, oder 1–16): Raum und Mikros rechnen auf mehreren Threads,
  jeder rendert einen Teil der Schallwege für den 64er-Teilblock. Gerät, Treiber, Puffer und Latenz; „Audio-Einstellungen …“ öffnet den Dialog (ASIO / Windows Audio).
* PC-Tastatur: Y–M und Q–P, wählbar für Ober, Unter oder Pedal. Leertaste = Leslie, Esc = Panik.
* Einstellungen und Oberflächenzustand liegen in `%APPDATA%\Wheelhouse\`.

## Geplant für die VST3-Version

* Offline-Rendern in maximaler Qualität: beim Bounce/Einfrieren meldet der Host „nicht Echtzeit“, dann rechnet das
  Plugin mit eigenen Render-Einstellungen (Ordnung, Exakt, Ultra), unabhängig davon, was in Echtzeit möglich ist.

## Endstufe

`Source/LeslieAmp.h` rechnet die Leslie-Endstufe als Schaltung: 12AU7-Treiber, Gegentakt-Endstufe mit 2 × 6550,
gemeinsamer Kathodenwiderstand, Ausgangsübertrager mit Kern-Hysterese, Netzteil mit Sag (beim 5U4 deutlich weicher).
**HQ** löst die Röhren mit Newton-Iteration bei 2-fachem Oversampling, **Ultra** zusätzlich mit dynamischer Last
(Streuinduktivität, Trommelresonanz), Drossel-Netzteil, Wirbelströmen und 4-fachem Oversampling. **Eco** ist eine
statische Gegentakt-Kennlinie.

## Vorverstärker

`Source/OrganPreamp.h` rechnet den Ausgangsteil des Hammond AO-28 hinter dem Schweller: 12AX7-Spannungsverstärker mit
Gitterstrom, Koppelkondensatoren mit Blocking, 12BH7-Gegentakt auf einen Übertrager mit Kernsättigung. Qualität wie bei
der Endstufe (Eco / HQ / Ultra; Ultra zusätzlich Miller-Kapazität, nachgebende Versorgung, atmender Kathoden-Bias).

## Federhall

`Source/SpringReverb.h`: Hammond-Tank wie in A-100 und 122RV, in der Orgel vor dem Leslie. Zwei oder drei Federn als
Rückkopplungsschleifen mit Ketten gedehnter Allpässe (Dispersion – das typische Zirpen), leicht schwankender Laufzeit,
weich sättigendem Treiber und bandbegrenztem Wandler; „Anstoßen“ schlägt die Federn an. Modell nach Välimäki, Parker &
Abel (2010) und Parker (2011). Regler: Hall (vorne), in der Karte Orgel Anteil, Nachhall, Klang, Antrieb, Federn.

## Gehäuse und Mechanik

`Source/Mechanics.h`. Alles hier sind *ruhende* Schallquellen: sie drehen sich nicht mit, sondern werden wie das
Gehäuse selbst in den Raum gestellt (Laufzeit, Abstand, Richtung zum Hörer und zu jedem Mikro, Einspeisung in den Hall).

* **Gehäuse**: das Mittelfach des 122 ist die Bassreflexbox des 15″-Tieftöners (Hochpass 4. Ordnung um 40 Hz),
  das Trommelfach hat Luftmoden (c/2L bei 0,70 m und 0,48 m). Die Holzwände (Seiten, Deckel, Rückwände, Front des
  Tieftönerfachs, Boden) schwingen als Platten: Eigenfrequenzen aus Maßen, Stärke und Material berechnet
  (Sperrholz 19 mm / 12,7 mm, E ≈ 8 GPa; Spanplatte E ≈ 3,2 GPa, stärker gedämpft), je drei Moden, angeregt vom
  Druck im Horn- bzw. Tieftönerfach.
* **Leslie-Mechanik**: Induktionsmotoren brummen mit doppelter Netzfrequenz (die kleinen Langsam-Motoren
  schnarrender), Riemen, Spannrolle und Lager rumpeln mit der Drehzahl, die Bespannung der Trommel rauscht im
  Quadrat der Drehzahl, beim Umschalten klickt das Relais und der Langsam-Motor kuppelt mit einem Klonk ein und aus.
* **Orgel-Mechanik**: Laufmotor (zwei Drehmomentstöße je Netzperiode), Zahneingriff der 12 Antriebsräder auf der
  Hauptwelle (20 U/s × Zähnezahl, 1,3–2,2 kHz) unter der Filzdecke, Surren des Generators. Tasten schlagen am Ende
  des Hubs auf den Filz (je nach Anschlag), beim Loslassen leiser oben an; Pedale poltern tiefer. Die Orgel steht
  direkt vor dem Hörer.
* Die Pegel sind geschätzt, nicht gemessen: Leerlauf-Mechanik bei mittlerem Zustand etwa 35–40 dB unter einem
  vollen Akkord, Tastenanschlag etwa 30 dB darunter. `tests/mechanics_test.cpp` misst alle Pegel nach.

## Dateien

* `Source/LeslieRoomDSP.h` – Klangkern (Orgel, Leslie, Raum, Hall), ohne JUCE-Abhängigkeit
* `Source/LeslieAmp.h` – Röhrenendstufe und Ausgangsübertrager
* `Source/OrganPreamp.h` – Orgel-Vorverstärker (AO-28)
* `Source/SpringReverb.h` – Federhall
* `Source/Mechanics.h` – Leslie-Gehäuse (Bassreflex, Plattenmoden) und Mechanikgeräusche von Leslie und Orgel
* `Source/MicRig.h` – Mikrotypen und Pult (die Berechnung steckt in `LeslieRoomDSP.h`)
* `Source/MainComponent.*` – Audio/MIDI/Oberfläche (JUCE)
* `ui/wheelhouse.html`, `ui/tracer.js` – Quelle der Oberfläche; `python3 tools/build_ui.py` erzeugt daraus `Resources/index.html`
* `Source/Workers.h` – Thread-Pool für die Raumberechnung
* `Source/FastMath.h` – schnelle atan-Näherung für die Röhrenmodelle
* `tests/` – Messungen und Prüfungen (`g++ -O2 -std=c++17 -ISource tests/<name>.cpp`):
  `regression.cpp` (Referenz aufnehmen/vergleichen, Klang muss bitgleich oder ≥ 90 dB identisch bleiben),
  `asan_all_test.cpp` (alle Betriebsarten mit AddressSanitizer), `threads_test.cpp` (mehrere Kerne: gleiches Ergebnis wie ein Kern),
  `bench.cpp` (Rechenlast je Stufe), `organ_features_test.cpp` (Click nach Anschlag, Scanner),
  `mechanics_test.cpp` (Pegel der Mechanik, Plattenresonanzen, Pedal-Spektrum); Tests mit `-pthread` bauen,
  dazu Einzelmessungen für Endstufe, Vorverstärker, Federhall und Mikros

## Lizenz

Wheelhouse steht unter der **GNU Affero General Public License v3.0** (`LICENSE`).
Die Röhren- und Übertragermodelle von Endstufe und Vorverstärker beruhen auf `dsp/glow.c` und `dsp/iron.c` aus
[Amberwave](https://github.com/lfrank95/amberwave) (AGPL-3.0, © The Amberwave contributors).
JUCE wird unter der AGPL-Option genutzt. Schriften: Barlow Condensed (SIL OFL 1.1), Yellowtail (Apache 2.0) –
der Build-Workflow lädt sie aus dem Google-Fonts-Repository.
