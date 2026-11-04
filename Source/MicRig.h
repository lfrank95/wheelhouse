#pragma once
// Wheelhouse – Mikrofonierung: Nahmikros an Horn und Trommel, Raummikro-Paar, Pult mit Einzelausgängen.
//
// Jedes Mikro hat
//   * eine Richtcharakteristik je Band (tief / mittel / hoch): Gewicht g = a + (1 − a)·cos θ
//     (a = 1 Kugel, 0,5 Niere, ≈0,37 Superniere, 0 Acht; die Rückseite der Acht nimmt gegenphasig auf),
//   * einen Frequenzgang auf Achse (nach Datenblatt angenähert, als Filterkette),
//   * einen Nahbesprechungseffekt (Druckgradient: Bassanhebung, die mit kleinerem Abstand wächst),
//   * eine Windempfindlichkeit (das Horn bläst Luft, Bändchen und Kondensator reagieren stärker).
// Die Namen „Typ 57“ usw. bezeichnen die Bauart, nicht die Marke.

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace leslie {

struct MicEq { char type; double f, q, db; };   // 'h' Hochpass, 'l' Tiefpass, 'p' Glocke, 's' Tiefenregal
struct MicType { const char* name; double a[3]; double prox, wind, diffuse; int nEq; MicEq eq[5]; };

static const MicType MIC_TYPES[] = {
    // 0 Typ 57: Niere, Präsenzanhebung um 5 kHz, schlanker Bass auf Abstand, seitlich deutlich dumpfer
    {"57", {0.55, 0.50, 0.32}, 1.0, 0.15, 0.58, 4, {{'h', 90, 0.7, 0}, {'p', 5500, 1.3, 5}, {'p', 2200, 1.0, 1.5}, {'l', 14000, 0.7, 0}}},
    // 1 Typ 421: Niere, kräftig, breite Präsenz um 4 kHz
    {"421", {0.55, 0.50, 0.38}, 0.9, 0.15, 0.58, 4, {{'h', 45, 0.7, 0}, {'p', 4000, 0.8, 3.5}, {'p', 250, 1.0, 1.5}, {'l', 16000, 0.7, 0}}},
    // 2 Typ D112: Bassmikro, Buckel um 90 Hz, Mittenloch, Anhebung um 4 kHz
    {"D112", {0.60, 0.50, 0.35}, 1.0, 0.12, 0.58, 4, {{'p', 90, 0.9, 5}, {'p', 650, 1.0, -5}, {'p', 4200, 1.3, 6}, {'l', 11000, 0.7, 0}}},
    // 3 Typ RE20: Superniere, kaum Nahbesprechung (Variable-D), neutral, sanfter Höhenabfall
    {"RE20", {0.42, 0.38, 0.36}, 0.15, 0.10, 0.52, 3, {{'h', 40, 0.7, 0}, {'p', 2500, 0.7, 1}, {'l', 12000, 0.6, 0}}},
    // 4 Typ 87: Großmembran-Kondensator, Niere / Kugel / Acht, leichte Anhebung um 9 kHz
    {"87", {0.50, 0.50, 0.42}, 1.0, 0.45, 0.58, 3, {{'h', 30, 0.7, 0}, {'p', 9000, 0.9, 2}, {'l', 19000, 0.7, 0}}},
    // 5 Typ KM 84: Kleinmembran-Niere, auch seitlich sehr gleichmäßig
    {"KM84", {0.50, 0.50, 0.47}, 1.0, 0.40, 0.58, 2, {{'h', 35, 0.7, 0}, {'p', 12000, 0.8, 1}}},
    // 6 Typ 121: Bändchen, Acht, dunkler Höhenabfall, starker Nahbesprechungseffekt, sehr windempfindlich
    {"121", {0.0, 0.0, 0.0}, 1.3, 1.0, 0.58, 3, {{'h', 25, 0.7, 0}, {'l', 9500, 0.6, 0}, {'p', 3000, 0.8, -1}}},
};
constexpr int NUM_MIC_TYPES = 7;

/** Pultkanäle: 0 Horn L, 1 Horn R, 2 Trommel 1, 3 Trommel 2, 4 Raum L, 5 Raum R, 6 Hörer (bisheriger Raumklang) */
constexpr int MIC_CH = 7;

struct MicSetup {
    bool on = false;
    // Horn: Stereo = zwei Mikros um die Mitte herum, Mono = eins in der Mitte
    bool hornStereo = true; double hornAngle = 180, hornCenter = 0, hornDist = 0.15, hornHeight = 0.0; int hornType = 0; bool windscreen = false;
    bool drumStereo = false; double drumAngle = 180, drumCenter = 0, drumDist = 0.15; int drumType = 2;
    // Raumpaar: 0 AB, 1 XY, 2 ORTF
    int roomArr = 0; double roomX = 3, roomY = 3, roomZ = 2.0, roomYaw = 1.5707963, roomSpacing = 0.6; int roomType = 4, roomPattern = 1;
    int hornPattern = 0, drumPattern = 0;   // nur Typ 87: 0 Niere, 1 Kugel, 2 Acht
    int roomOrder = 2;
    struct Ch { double gainDb = 0, pan = 0, delayMs = 0; bool pol = false, mute = false, solo = false; int out = -1; };   // out: −1 vorne, −2 hinten, ≥0 Einzelausgang
    Ch ch[MIC_CH];
    MicSetup() {
        ch[0].pan = -1; ch[1].pan = 1; ch[2].pan = 0; ch[3].pan = 0; ch[4].pan = -1; ch[5].pan = 1; ch[4].gainDb = -6; ch[5].gainDb = -6; ch[6].mute = true;
    }
};

struct MicPath {
    int rotor = 0, order = 0; bool exact = false;
    double sx = 1, sy = 1, sz = 1, ox = 0, oy = 0, oz = 0, g = 1, jit = 0;
    double d0 = 0, d1 = 0, g0 = 0, g1 = 0, kb = 1, kc = 1, kDrum = 1, cab = 1;
    double band[3] = {1, 1, 1};
    double lp = 0, zlo = 0, zhi = 0, zd = 0;
    bool init = false;
};

struct MicRt {
    bool active = false, close = false; int rotor = 0, type = 0, chan = 0;
    double x = 0, y = 0, z = 1, ax = 1, ay = 0, az = 0;     // Ort und Blickrichtung (Raumkoordinaten)
    double a[3] = {0.5, 0.5, 0.5}, wind = 0, diffuse = 0.58, trim = 1, dMean = 1;
    Biquad eq[6]; int nEq = 0;
    std::vector<MicPath> paths;
    Biquad prox; bool hasProx = false;                     // Nahbesprechungseffekt (Tiefenregal, abstandsabhängig)
    double phi = 0, R = 1;                                   // Nahmikro: Winkel zur Gehäusefront, Abstand zur Gehäusemitte
    double windLp = 0, windLp2 = 0, windAmt = 0, windC = 1, windS = 0;
    std::array<float, 16> fdnRow{}; std::array<int, 16> fdnOff{};
};

} // namespace leslie
