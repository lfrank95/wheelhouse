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

Das Fenster ist auf 1600 × 900 ausgelegt und skaliert mit. Oben die Registerkarten, darunter das Gehäuse
mit Leslie, Zugriegeln und Raum, unten zwei Manuale mit je 61 Tasten und ein Pedal mit 25 Tönen.

* **Spielen**: 9 Zugriegel Ober, 2 Pedal (16′/8′), 9 Unter; Percussion-Wippen (nur Obermanual) und Vibrato je Manual.
  Zugriegel und Knöpfe ziehen oder Mausrad, Umschalt = fein, Doppelklick = Standardwert.
* **Orgel**: Zustand, Übersprechen, Kontakte, Motor, Stimmung, Sammelschiene, Percussion-Pegel und Abklingzeiten.
* **Leslie**: Drehzahlen und Hochlauf/Auslauf für Horn und Trommel, Bremse, Endstufe (Modell 122 / 122 früh / 147,
  Qualität Eco / HQ / Ultra, Drive, Netzteil-Sag), Balance, Hornfach, Ausrichtung.
* **Raum**: großer Grundriss (Leslie und Kopf ziehen, rechts ziehen = Blickrichtung, Mausrad = drehen), Maße, Material,
  Reflexionsordnung, Vorlagen; Zirkuszelt mit Strahlenverfolgung.
* **Ausgabe**: Stereo / Quad / 5.1, LFE, Ausgangspegel, freie Kanalzuordnung, Kanaltest.
* **MIDI**: Kanäle für Ober (oder Omni), Unter und Pedal, Controller für Leslie und Schweller. Alle MIDI-Eingänge sind aktiv.
* **Audio**: Gerät, Treiber, Puffer und Latenz; „Audio-Einstellungen …“ öffnet den Dialog (ASIO / Windows Audio).
* PC-Tastatur: Y–M und Q–P, wählbar für Ober, Unter oder Pedal. Leertaste = Leslie, Esc = Panik.
* Einstellungen und Oberflächenzustand liegen in `%APPDATA%\Wheelhouse\`.

## Endstufe

`Source/LeslieAmp.h` rechnet die Leslie-Endstufe als Schaltung: 12AU7-Treiber, Gegentakt-Endstufe mit 2 × 6550,
gemeinsamer Kathodenwiderstand, Ausgangsübertrager mit Kern-Hysterese, Netzteil mit Sag (beim 5U4 deutlich weicher).
**HQ** löst die Röhren mit Newton-Iteration bei 2-fachem Oversampling, **Ultra** zusätzlich mit dynamischer Last
(Streuinduktivität, Trommelresonanz), Drossel-Netzteil, Wirbelströmen und 4-fachem Oversampling. **Eco** ist eine
statische Gegentakt-Kennlinie.

## Dateien

* `Source/LeslieRoomDSP.h` – Klangkern (Orgel, Leslie, Raum, Hall), ohne JUCE-Abhängigkeit
* `Source/LeslieAmp.h` – Röhrenendstufe und Ausgangsübertrager
* `Source/MainComponent.*` – Audio/MIDI/Oberfläche (JUCE)
* `ui/wheelhouse.html`, `ui/tracer.js` – Quelle der Oberfläche; `python3 tools/build_ui.py` erzeugt daraus `Resources/index.html`
* `tests/` – Messungen für Endstufe und Klangkern (`g++ -O2 -std=c++17 -ISource tests/dsp_test.cpp`)

## Lizenz

Wheelhouse steht unter der **GNU Affero General Public License v3.0** (`LICENSE`).
Die Röhren- und Übertragermodelle der Endstufe beruhen auf `dsp/glow.c` und `dsp/iron.c` aus
[Amberwave](https://github.com/lfrank95/amberwave) (AGPL-3.0, © The Amberwave contributors).
JUCE wird unter der AGPL-Option genutzt. Schriften: Barlow Condensed (SIL OFL 1.1), Yellowtail (Apache 2.0) –
der Build-Workflow lädt sie aus dem Google-Fonts-Repository.
