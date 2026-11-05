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
* **Orgel**: Zustand, Übersprechen, Kontakte, Motor, Stimmung, Sammelschiene, Percussion-Pegel und Abklingzeiten.
* **Leslie**: Drehzahlen und Hochlauf/Auslauf für Horn und Trommel, Bremse, Endstufe (Modell 122 / 122 früh / 147,
  Qualität Eco / HQ / Ultra, Drive, Netzteil-Sag), Balance, Hornfach, Ausrichtung.
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
* **Audio**: Gerät, Treiber, Puffer und Latenz; „Audio-Einstellungen …“ öffnet den Dialog (ASIO / Windows Audio).
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

## Dateien

* `Source/LeslieRoomDSP.h` – Klangkern (Orgel, Leslie, Raum, Hall), ohne JUCE-Abhängigkeit
* `Source/LeslieAmp.h` – Röhrenendstufe und Ausgangsübertrager
* `Source/OrganPreamp.h` – Orgel-Vorverstärker (AO-28)
* `Source/SpringReverb.h` – Federhall
* `Source/MicRig.h` – Mikrotypen und Pult (die Berechnung steckt in `LeslieRoomDSP.h`)
* `Source/MainComponent.*` – Audio/MIDI/Oberfläche (JUCE)
* `ui/wheelhouse.html`, `ui/tracer.js` – Quelle der Oberfläche; `python3 tools/build_ui.py` erzeugt daraus `Resources/index.html`
* `Source/FastMath.h` – schnelle atan-Näherung für die Röhrenmodelle
* `tests/` – Messungen und Prüfungen (`g++ -O2 -std=c++17 -ISource tests/<name>.cpp`):
  `regression.cpp` (Referenz aufnehmen/vergleichen, Klang muss bitgleich oder ≥ 90 dB identisch bleiben),
  `asan_all_test.cpp` (alle Betriebsarten mit AddressSanitizer), `bench.cpp` (Rechenlast je Stufe),
  dazu Einzelmessungen für Endstufe, Vorverstärker, Federhall und Mikros

## Lizenz

Wheelhouse steht unter der **GNU Affero General Public License v3.0** (`LICENSE`).
Die Röhren- und Übertragermodelle von Endstufe und Vorverstärker beruhen auf `dsp/glow.c` und `dsp/iron.c` aus
[Amberwave](https://github.com/lfrank95/amberwave) (AGPL-3.0, © The Amberwave contributors).
JUCE wird unter der AGPL-Option genutzt. Schriften: Barlow Condensed (SIL OFL 1.1), Yellowtail (Apache 2.0) –
der Build-Workflow lädt sie aus dem Google-Fonts-Repository.
