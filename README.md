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

* **Audio-Einstellungen**: Gerätetyp (ASIO / Windows Audio), Gerät, Abtastrate, Puffergröße, aktive Ausgänge, MIDI-Eingänge.
  Für Quad die ersten 4 Ausgänge aktivieren, für 5.1 die ersten 6 (Reihenfolge L, R, C, LFE, SL, SR).
* Alle MIDI-Eingänge sind automatisch aktiv. Modwheel oder Sustain schaltet das Leslie, CC11 ist der Schweller.
* Einstellungen liegen in `%APPDATA%\Wheelhouse\`.

## Dateien

* `Source/LeslieRoomDSP.h` – Klangkern (Orgel, Leslie, Raum, Hall), ohne JUCE-Abhängigkeit
* `Source/MainComponent.*` – Audio/MIDI/Oberfläche (JUCE)
* `Resources/index.html` – Oberfläche
* `Source/test_dsp.cpp` – Vergleichstest gegen die JavaScript-Version (`g++ -O2 -std=c++17 Source/test_dsp.cpp`)
* `build_native.js` – erzeugt `Resources/index.html` aus der Browser-Vorlage
