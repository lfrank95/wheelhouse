// Native UI: states saved by earlier versions (web UI, Leslie names, circus tent) load into the native UI and the
// right messages reach the engine. Needs the JUCE GUI modules and an X display (e.g. xvfb-run), see README "Tests".
#include "ui/NativeUi.h"
using namespace juce;
int fails = 0;
static void check(bool c, const char* m) {
    printf("%s  %s\n", c ? "ok  " : "FAIL", m);
    if (!c) fails++;
}
int main() {
    ScopedJuceInitialiser_GUI init;
    // a state saved by the web UI: Leslie names, German material, circus tent, mics, MIDI, drawbars
    const String old =
        R"({"room":{"Lx":30,"Ly":30,"Lz":8.5,"absorb":0.5,"order":3,"reverb":1,"material":"zelt","shape":"zelt"},
      "lesliePos":{"x":1.5,"y":2.5},"leslie":"fast","midi":{"upper":4,"lower":2,"pedal":3,"leslieCC":64,"swellCC":7},
      "db":{"upper":[8,7,6,5,4,3,2,1,0],"lower":[0,0,8,4,0,0,0,0,0],"pedal":[5,2]},"tent":{"W":30},
      "mics":{"on":true,"hornAngle":120,"grp":[{"gain":-3},{"gain":0},{"gain":-6}]},"halfMoon":"stop","mode":"quad",
      "cab":{"hornfach":0.6},"split":{"mode":2,"hi":60,"lo":48,"oct":[1,0,-1],"kb":61}})";
    std::vector<var> sent;
    wh::UiHooks h;
    h.loadUi = [&] {
        return old;
    };
    h.dspMessage = [&](const var& m) {
        sent.push_back(m);
    };
    String saved;
    h.saveUi = [&](const String& s) {
        saved = s;
    };
    auto ui = std::make_unique<wh::NativeUi>(h);
    ui->startIfNeeded();
    auto find = [&](const char* type) {
        var r;
        for (auto& m : sent)
            if (m["type"].toString() == type) r = m;
        return r;
    };
    check(find("rotary")["v"].toString() == "fast", "old 'leslie' speed restored as rotary");
    check((double) find("pos")["rotaryPos"]["x"] == 1.5, "old 'lesliePos' restored as rotaryPos");
    check((int) find("midiChannels")["rotaryCC"] == 64 && (int) find("midiChannels")["upper"] == 4,
          "MIDI channels and old leslieCC");
    check(find("room")["v"]["material"].toString() == "concrete" && !find("room")["v"].hasProperty("shape"),
          "circus tent replaced by a plain room");
    check((double) find("room")["v"]["Lx"] <= 30, "room size valid");
    check((int) find("drawbars")["v"][1] >= 0, "drawbars sent");
    var db;
    for (auto& m : sent)
        if (m["type"].toString() == "drawbars" && (int) m["man"] == 0) db = m["v"];
    check(JSON::toString(db, true) == "[8, 7, 6, 5, 4, 3, 2, 1, 0]", "upper drawbars from the saved state");
    check((bool) find("mics")["v"]["on"] && (int) find("mics")["v"]["hornAngle"] == 120, "mic settings restored");
    check((double) find("cabDetail")["hornBox"] == 0.6, "old 'hornfach' restored as hornBox");
    check(find("speakers")["ch"].getArray() && find("speakers")["ch"].getArray()->size() == 4, "quad output format");
    check((int) find("keyMap")["mode"] == 2, "keyboard split restored");
    printf(fails ? "%d FAILED\n" : "all checks passed\n", fails);
    return fails;
}
