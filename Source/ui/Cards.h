#pragma once
// Wheelhouse UI - the settings cards (Organ, Rotary, Room, Mics, Output, MIDI, Audio): sections of knobs and rows,
// the mic mixer desk and the split-point chips. Same content, texts and behaviour as the former web UI.
#include "Actions.h"

namespace wh {

// Tooltips per "Section/Label", then "Section/<any>", then "<any>/Label".
inline const std::map<String, String>& cardTips() {
    static const std::map<String, String> t{
        // Organ
        {"Character/Condition",
         L"Overall wear of the instrument – from showroom-fresh to 40 years on the road. Pulls Leakage, Contacts and "
         L"Motor along and sets the level spread and runout of the tone wheels."},
        {"Character/Leakage", L"Tone-wheel crosstalk: neighbouring wheels bleed into the pickups and wiring – the "
                              L"faint \"hiss and whistle\" under every B3."},
        {"Character/Contacts",
         "Key contact wear: dirty busbar contacts bounce and crackle, so the click gets dirtier and less even."},
        {"Character/Motor", "Hum and whir of the generator and run motor, audible between notes."},
        {"Pedal/Bass", "B-3: the pedal tone stops when the pedal is released (keyed by contacts like the manuals). "
                       "Sustain: it fades out after release - not on the B-3 / C-3 / A-100, but on Hammonds with pedal "
                       "sustain and on the later clones."},
        {"Pedal/Sustain", "How long a released pedal note fades: 1 short (plucked) ... 5 long."},
        {"Generator/Key noise", "Mechanical key noise: keys bottoming out on the felt. The console sits right in front "
                                "of you, so it is acoustic, not through the Rotary."},
        {"Generator/Tuning",
         "Gears: the slightly off equal temperament of the real gear train. Equal: mathematically exact tuning."},
        {"Generator/Busbar load", "Loading of the generator: the more drawbars and keys you add, the more each one "
                                  "sags in level (the classic \"compression\" of a full registration)."},
        {"Generator/Low wheels",
         "Complex: the lowest wheels are nearly square, with odd harmonics (pedal only). Sine: idealised."},
        {"Generator/Mains",
         L"Mains frequency of the synchronous motor: sets the hum and – on a 50 Hz organ run at 60 Hz – the pitch."},
        {L"Percussion · Vibrato · Click/Key click",
         L"Key click: the burst from nine contacts closing on the busbar. Part of the sound – purists keep it up."},
        {L"Percussion · Vibrato · Click/Perc level", "Overall level of the percussion (on top of Soft/Normal)."},
        {L"Percussion · Vibrato · Click/Fast decay", "Decay time of the percussion when set to Fast."},
        {L"Percussion · Vibrato · Click/Slow decay", "Decay time of the percussion when set to Slow."},
        {L"Percussion · Vibrato · Click/Vibrato",
         L"Scanner vibrato/chorus: the B-3's LC delay line and rotating capacitor as a circuit. V1–V3 scan 8/12/16 of "
         L"the 18 line sections. C1–C3: chorus (half the signal bypasses the line). C3 is the classic."},
        {L"Percussion · Vibrato · Click/Click",
         "Velocity: a fast stroke closes all nine contacts almost at once and hard, a slow press one after another and "
         "softer. Fixed: always the same."},
        {"Preamp AO-28/Gain", "Preamp gain after the swell: 12AX7 into a 12BH7 push-pull and transformer. With the "
                              "swell wide open and full drawbars the organ breaks up before the Rotary."},
        {"Preamp AO-28/Tubes", "Bypass skips the tube stages for a clean, line-level organ."},
        {"Preamp AO-28/Quality", "Uses the same oversampling quality as the power amp."},
        {"Spring Reverb/Mix",
         L"Wet/dry mix of the spring tank. It sits inside the organ, before the Rotary – so the reverb spins too."},
        {"Spring Reverb/Decay", "Decay time of the springs."},
        {"Spring Reverb/Tone", "Tone of the tank return: dark to bright."},
        {"Spring Reverb/Drive", "Drive into the tank driver: more drive = more boing and splash."},
        {"Spring Reverb/Springs", "Number of springs: 2 is sparser and twangier, 3 is denser."},
        {"Spring Reverb/Tank", L"Kick the tank – the springs crash, like bumping the organ."},
        // Rotary
        {"Horn/Slow", "Horn speed on Slow."},
        {"Horn/Fast", "Horn speed on Fast."},
        {"Horn/Rise", "Ramp-up time of the horn from Slow to Fast. The light horn gets there in under a second."},
        {"Horn/Fall", "Ramp-down time of the horn from Fast to Slow."},
        {"Drum/Slow", "Drum (bass rotor) speed on Slow."},
        {"Drum/Fast", "Drum speed on Fast."},
        {"Drum/Rise",
         L"Ramp-up time of the heavy drum – it lags behind the horn, which is a big part of the Rotary sound."},
        {"Drum/Fall", "Ramp-down time of the drum."},
        {"Drum/Brake", "On: the rotors stop on Stop. Off: they coast to a halt."},
        {"Power Amp/Drive", L"How hard the organ hits the Rotary power amp – from clean to growl."},
        {"Power Amp/Sag",
         L"Power-supply sag: the rectifier voltage dips under load – softer attack, more compression when driven."},
        {"Power Amp/Model", L"122: 2 × 6550, tight. 122 early: 5U4 tube rectifier, more sag. 147: with negative "
                            L"feedback, cleaner and stiffer."},
        {"Power Amp/Quality", L"Eco: static curve, low CPU. HQ: tubes and output transformer as a circuit, 2× "
                              L"oversampling. Ultra: 4× oversampling."},
        {"Power Amp/Plate / cath.", "Live plate and cathode readings of the output tubes."},
        {"Cabinet/Horn box", "Level of the horn compartment's own reverberation (what bounces between top board, "
                             "shelf and slats before it leaves through the louvres, all sides alike)."},
        {"Cabinet/Resonance", "Cabinet panel resonances: the wooden walls vibrate along and colour the low mids."},
        {"Cabinet/Mechanics", L"Mechanical noise: motors, belts, bearings, relays and the slow motor clutching in. "
                              L"Auto follows Organ › Condition."},
        {"Cabinet/Wobble",
         L"Rotor runout: slightly out-of-round rotation and speed flutter. Auto follows Organ › Condition."},
        {"Cabinet/Build", L"Early cabinets are plywood, later ones chipboard – softer, more damped."},
        {"Cabinet/Deflectors",
         L"The cone-shaped deflectors in the horn mouths. In (stock) spreads the treble – gentle, wide swirl. Out "
         L"beams the highs – a sharper, more pronounced sweep, as some players mod their cabinets."},
        {"Placement/Balance",
         "Horn/drum level balance: 50/50 = both at full level, left = more horn, right = more drum."},
        {"Placement/Facing", "Which way the Rotary front points in the room. You can also turn it in the floor plan "
                             "with the scroll wheel."},
        {"Placement/Quick turn", "Turn the Rotary front towards the listener (or the room pair in Mics mode)."},
        {"Placement/Back to the wall", "Put the Rotary against the nearest wall, front facing into the room."},
        {"Control/Switch via", "Which MIDI controller toggles Slow/Fast. The space bar always works."},
        {"Control/Half-moon",
         "2-way: Slow/Fast. With Stop: the 3-way half-moon switch with a stop position in the middle."},
        // Room
        {"Dimensions/Width", "Room width."},
        {"Dimensions/Length", "Room length."},
        {"Dimensions/Height", "Ceiling height."},
        {"Dimensions/Reverb", "Scales the late reverb tail. 100% = physically correct for the room."},
        {L"Surfaces · Listener/Absorption",
         "How much the surfaces soak up. Low = live and splashy, high = dry and dead."},
        {L"Surfaces · Listener/Facing",
         L"Which way your head points. Turning shifts the image – just like turning your head in a real room."},
        {L"Surfaces · Listener/Material",
         "Wall material: sets the frequency response and scattering of the reflections."},
        {L"Surfaces · Listener/Head", "Turn your head towards the Rotary."},
        {L"Engine · Presets/Reflections", "Image-source order: how many bounces are rendered as discrete reflections. "
                                          "More = denser and more realistic, but more CPU."},
        {L"Engine · Presets/Precision",
         L"Fast: from the 2nd order, reflections are grouped by direction. Exact: every reflection per sample with "
         L"position, Doppler, directivity and panning – very CPU-heavy from the 4th order."},
        {L"Engine · Presets/Paths", "Number of rendered sound paths."},
        {L"Engine · Presets/Load room", "Load a room preset (keeps the Rotary and listener positions)."},
        {"*/CPU", "Audio CPU load. Running out of headroom? Raise the buffer, lower the reflections or set the power "
                  "amp to HQ or Eco."},
        // Mics
        {"Horn Mics/Angle", L"Where the horn mics sit around the cabinet: 0° = front. From the back (180°) you get "
                            L"more motor, from the front more cabinet."},
        {"Horn Mics/Distance",
         "Distance from the cabinet. Closer = more Doppler and more proximity effect, further = more room."},
        {"Horn Mics/Height", "Mic height relative to the horn."},
        {"Horn Mics/Setup", L"Mono: one mic. 90°/120°/180°: a stereo pair spread around the cabinet – 180° is "
                            L"left/right of the horn, the classic wide spin."},
        {"Horn Mics/Windscreen", "Windscreen: tames the wind rumble from the rotor blowing straight at the mic."},
        {"Drum Mics/Angle", L"Where the drum mics sit around the cabinet: 0° = front, 180° = back (more motor)."},
        {"Drum Mics/Distance", "Distance from the cabinet. Closer = more proximity effect."},
        {"Drum Mics/Setup", L"Mono: one mic on the bass louvres. 90°/180°: a stereo pair."},
        {"*/Mic", "57/421: dynamic, mid-forward. D112: kick mic, big lows. RE20: broadcast dynamic. 87: "
                  "large-diaphragm condenser. KM 84: small-diaphragm condenser. 121: ribbon, smooth top."},
        {"*/Pattern", "Polar pattern of the 87 (multi-pattern): cardioid, omni or figure-8."},
        {"Room Pair/Spacing", "Distance between the two room mics (AB only)."},
        {"Room Pair/Height", "Height of the room pair."},
        {"Room Pair/Aim", "Which way the pair points."},
        {"Room Pair/Reflect.", "Reflection order rendered for the room pair."},
        {"Room Pair/Array",
         L"AB: spaced omnis/cardioids, wide and airy. XY: coincident, mono-safe. ORTF: 17 cm, 110° – the compromise."},
        {"Room Pair/Place in front", "Place the room pair in front of the Rotary, aimed at it."},
        {"Mixer/head", "Listener: you stand in the room. Mics: close mics and room pair through the mixer."},
        // Output
        {"Format/LFE", L"Level of the LFE (.1) channel – 5.1 only."},
        {"Format/LFE x-over", L"Crossover frequency for the LFE – 5.1 only."},
        {"Format/Master", "Master output level."},
        {"Format/Format", "Speaker layout the room is rendered for: stereo, quad or 5.1."},
        {"Routing/*", "Which device output this channel goes to."},
        {L"Routing/Auto (1, 2, 3 …)", L"Route channels to outputs 1, 2, 3 … in order."},
        {"Monitoring/Rear", "Trim for your rear speakers (surround channels)."},
        {"Monitoring/Center", L"Trim for your center speaker – 5.1 only."},
        {"Monitoring/Divergence", "Center divergence: also spreads a source in front of you onto L/R as a phantom "
                                  "center, if your center speaker is weaker."},
        {"Check/Channel test", L"Sends a short ping to each channel in turn – check your speakers and routing."},
        {"Check/Device", "Active audio device."},
        {"Check/Status", "Warnings about the output setup."},
        // MIDI
        {"Channels/Upper", "MIDI channel for the upper manual. Omni: everything not on the lower or pedal channel "
                           "plays the upper manual."},
        {"Channels/Lower", "MIDI channel for the lower manual."},
        {"Channels/Pedal", "MIDI channel for the pedalboard."},
        {L"Split · Octaves/Split",
         "Keyboard split: play two or three manuals from one keyboard (on the upper channel)."},
        {L"Split · Octaves/Keyboard", L"Size of your keyboard – only for the split display."},
        {L"Split · Octaves/Split point", L"Where the zones begin. ● learns from the next key you press."},
        {L"Split · Octaves/Upper octave", L"Octave shift of the upper manual (±3)."},
        {L"Split · Octaves/Lower octave", L"Octave shift of the lower manual (±3)."},
        {L"Split · Octaves/Pedal octave", L"Octave shift of the pedal (±3)."},
        {"Controllers/Rotary", "MIDI controller that toggles the Rotary speed."},
        {"Controllers/Swell", "MIDI controller for the swell (expression) pedal."},
        {"Controllers/Devices", "Connected MIDI inputs. Newly connected devices are enabled automatically."},
        {L"Controllers/MIDI Inputs …", "Choose which MIDI inputs are active."},
        {"QWERTY Keys/Plays", "Which manual the computer keyboard plays."},
        {"QWERTY Keys/Octave", "Octave of the computer keyboard."},
        {"QWERTY Keys/Keys", "Key layout of the computer keyboard."},
        // Audio
        {"Device/Driver", L"Audio driver type (ASIO, Windows Audio …)."},
        {"Device/Device", "Active audio device."},
        {L"Device/Audio Settings …", "Driver, device, sample rate, buffer size and outputs."},
        {"Device/Bounce", L"Max quality: when rendering offline the plugin uses 5th order, Exact and Ultra – no matter "
                          L"what runs in real time. As set: renders exactly what you hear."},
        {"Buffer/Sample rate", "Current sample rate."},
        {"Buffer/Buffer", "Buffer size: smaller = lower latency, larger = more CPU headroom."},
        {"Buffer/Output", "Output latency."},
        {"Buffer/Processing", "Extra latency from the tube oversampling."},
        {"Buffer/Total", L"Total latency plus the acoustic sound path from the Rotary to the listener or nearest mic – "
                         L"as in a real room."},
        {L"Outputs · CPU/Active", "Active device outputs."},
        {L"Outputs · CPU/Channels", "Channels rendered for the current format."},
        {L"Outputs · CPU/Dropouts", "Skipped audio blocks."},
        {L"Outputs · CPU/Power amp", L"Power amp quality – the biggest CPU lever after the reflections."},
        {L"Outputs · CPU/Cores", L"Room and mics run on several cores. Auto: standalone half the cores (max 8), plugin "
                                 L"a quarter (max 4) – the DAW needs some too."},
    };
    return t;
}

/** "Upper from ‹ C4 › ●" (● = learn the split point from the next key). */
class SplitChip : public Component, public Syncable, public juce::SettableTooltipClient {
public:
    SplitChip(Ctx& c, Actions& a, String which, String label, bool onCard)
        : ctx(c), act(a), k(std::move(which)), name(std::move(label)), card(onCard) {
        ctx.reg(this);
        setTooltip(name + " manual starts at this key");
    }
    ~SplitChip() override { ctx.unreg(this); }
    void sync() override { repaint(); }
    int naturalWidth() const { return (int) spacedWidth(name + " from", Fonts::make(12.5f), 0) + 7 + 3 * 20 + 34 + 6; }
    void paint(Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(card ? col::field : Colour(0xff2a170a));
        g.fillRoundedRectangle(r, 5);
        g.setColour(card ? col::fieldLine : Colour(0xff6b4426));
        g.drawRoundedRectangle(r, 5, 1);
        auto rr = getLocalBounds().toFloat().reduced(2, 1).withTrimmedLeft(5);
        g.setColour(Colour(0xffe3cfae));
        g.setFont(Fonts::make(12.5f));
        const float tw = spacedWidth(name + " from", Fonts::make(12.5f), 0);
        g.drawText(name + " from", rr.removeFromLeft(tw + 2), juce::Justification::centredLeft, false);
        cells.clear();
        auto cell = [&](float w) {
            auto c = rr.removeFromLeft(w);
            cells.push_back(c);
            return c;
        };
        auto dn = cell(18), val = cell(34), up = cell(18), ln = cell(20);
        g.drawText(juce::CharPointer_UTF8("\xe2\x80\xb9"), dn, juce::Justification::centred, false);
        g.drawText(juce::CharPointer_UTF8("\xe2\x80\xba"), up, juce::Justification::centred, false);
        g.setColour(col::gold);
        g.setFont(Fonts::make(12.5f, 700));
        g.drawText(Actions::noteName((int) ctx.st.num("split." + k)), val, juce::Justification::centred, false);
        const bool learning = act.learnTarget == k;
        if (learning) {
            g.setColour(col::amber);
            g.fillRoundedRectangle(ln.reduced(1, 2), 3);
        }
        g.setColour(learning ? col::onAmber : col::muted);
        g.setFont(Fonts::make(12.5f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x97\x8f"), ln, juce::Justification::centred, false);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (cells.size() < 4 || !e.mouseWasClicked()) return;
        const int v = (int) ctx.st.num("split." + k);
        if (cells[0].contains(e.position))
            act.setSplit(k, v - 1);
        else if (cells[2].contains(e.position))
            act.setSplit(k, v + 1);
        else if (cells[3].contains(e.position)) {
            act.learnTarget = act.learnTarget == k ? String() : k;
            ctx.syncAll();
        }
    }
    String getTooltip() override {
        auto p = getMouseXYRelative().toFloat();
        if (cells.size() >= 4) {
            if (cells[0].contains(p)) return "Split point one semitone down";
            if (cells[2].contains(p)) return "Split point one semitone up";
            if (cells[3].contains(p)) return "Learn: the next key you press on your keyboard becomes the split point";
        }
        return name + " manual starts at this key";
    }

private:
    Ctx& ctx;
    Actions& act;
    String k, name;
    bool card;
    std::vector<juce::Rectangle<float>> cells;
};

/** One channel strip of the mic mixer: polarity / mute / solo, fader with meters, pan and width, delay, output. */
class MixerStrip : public Component, public Syncable {
public:
    MixerStrip(Ctx& c, Actions& a, int group, String nm, std::function<bool()> stereoFn, std::function<String()> stInfo)
        : ctx(c), act(a), g(group), name(std::move(nm)), isStereo(std::move(stereoFn)), info(std::move(stInfo)),
          out(
              c, [this] { return outOptions(); }, [this] { return outValue(); },
              [this](const var& v) { set("out", (int) v); }) {
        ctx.reg(this);
        KnobOpts pan;
        pan.label = "Pan";
        pan.tip = "Pan (mono group) or balance (stereo group).";
        pan.min = -1;
        pan.max = 1;
        pan.step = 0.05;
        pan.def = 0.0;
        pan.small = true;
        pan.fmt = [](double v) {
            return std::abs(v) < 0.025 ? String("C")
                                       : String(v < 0 ? "L " : "R ") + String(juce::roundToInt(std::abs(v) * 100));
        };
        pan.get = [this] {
            return G("pan");
        };
        pan.set = [this](const var& v) {
            ctx.st.set(path("pan"), v);
            act.sendMics();
        };
        panK = std::make_unique<Knob>(ctx, pan);
        panK->labelFn = [this] {
            return isStereo() ? String("Balance") : String("Pan");
        };
        KnobOpts w;
        w.label = "Width";
        w.tip = "Stereo width of the pair: full to mono.";
        w.min = 0;
        w.max = 1;
        w.step = 0.01;
        w.def = 1.0;
        w.small = true;
        w.fmt = [](double v) {
            return v >= 0.995 ? String("full") : v <= 0.005 ? String("mono") : pct(v);
        };
        w.get = [this] {
            return G("width");
        };
        w.set = [this](const var& v) {
            ctx.st.set(path("width"), v);
            act.sendMics();
        };
        widthK = std::make_unique<Knob>(ctx, w);
        addAndMakeVisible(*panK);
        addChildComponent(*widthK);
        out.fontPx = 11.5f;
        out.setTooltip("Destination: the front or rear mix, or a direct out of its own (plugin: the Direct buses).");
        addAndMakeVisible(out);
    }
    ~MixerStrip() override { ctx.unreg(this); }

    void sync() override {
        widthK->setVisible(isStereo());
        setAlpha(ctx.st.flag("mics.on") ? 1.f : 0.38f);
        repaint();
    }
    void reset() override {
        ctx.st.set("mics.grp." + String(g), deepCopy(ctx.st.defaultOf("mics.grp." + String(g))));
        act.sendMics();
    }
    float meterL = -120, meterR = -120;

    void resized() override {
        const int W = getWidth();
        auto knobX = 8 + 16 + 5 + 6 + 5 + 6 + 10;
        panK->setBounds(knobX, 66, W - knobX - 8, 64);
        widthK->setBounds(knobX, 66 + 68, W - knobX - 8, 64);
        out.setBounds(W / 2 + 2, getHeight() - 30, W / 2 - 10, 22);
    }
    void paint(Graphics& gr) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        gr.setColour(col::segBg);
        gr.fillRoundedRectangle(r, 5);
        gr.setColour(Colour(0xff241e18));
        gr.drawRoundedRectangle(r, 5, 1);
        const bool st = isStereo();
        const float W = (float) getWidth();
        // header: name + badge
        gr.setColour(col::text);
        drawSpaced(gr, name, {8, 8, 80, 16}, Fonts::make(11.5f, 700), 0.6f, juce::Justification::centredLeft, true);
        const String badge = st ? L"Stereo · " + info() : String("Mono");
        const float bw = spacedWidth(badge, Fonts::make(10.5f), 0.6f, true) + 14;
        auto br = juce::Rectangle<float>(W - 8 - bw, 9, bw, 15);
        gr.setColour(st ? Colour(0xff6b5226) : col::fieldLine);
        gr.drawRoundedRectangle(br, 7.5f, 1);
        gr.setColour(st ? col::gold : col::muted);
        drawSpaced(gr, badge, br, Fonts::make(10.5f), 0.6f, juce::Justification::centred, true);
        // toggles
        const char* labels[3] = {"\xc3\x98", "M", "S"};
        const char* keys[3] = {"pol", "mute", "solo"};
        const float tw = 22, gap = 3, tx0 = W / 2 - (3 * tw + 2 * gap) / 2;
        toggleRects.clear();
        for (int i = 0; i < 3; i++) {
            auto tr = juce::Rectangle<float>(tx0 + i * (tw + gap), 32, tw, 20);
            toggleRects.push_back(tr);
            const bool on = (bool) G(keys[i]);
            gr.setColour(on ? (i == 2 ? col::gold : col::amber) : col::field);
            gr.fillRoundedRectangle(tr, 3);
            gr.setColour(on ? (i == 2 ? col::gold : col::amber) : col::fieldLine);
            gr.drawRoundedRectangle(tr.reduced(0.5f), 3, 1);
            gr.setColour(on ? col::onAmber : col::muted);
            gr.setFont(Fonts::make(11, 700));
            gr.drawText(juce::CharPointer_UTF8(labels[i]), tr, juce::Justification::centred, false);
        }
        // fader + meters
        fader = {8, 62, 16, 124};
        gr.setColour(Colour(0xff070605));
        gr.fillRoundedRectangle(fader, 3);
        const double gain = (double) G("gain");
        const float p = (float) toP(gain);
        const float cy = fader.getY() + (1 - p) * fader.getHeight();
        auto cap = juce::Rectangle<float>(fader.getX() - 5, cy - 6, fader.getWidth() + 10, 12);
        gr.setGradientFill(
            juce::ColourGradient(Colour(0xfffaf6ee), 0, cap.getY(), Colour(0xffbdb29c), 0, cap.getBottom(), false));
        gr.fillRoundedRectangle(cap, 2);
        gr.setColour(Colour(0xff2a2016));
        gr.fillRect(cap.getX() + 3, cap.getY() + 5, cap.getWidth() - 6, 2.f);
        auto meter = [&](float x, float db) {
            auto m = juce::Rectangle<float>(x, fader.getY(), 6, fader.getHeight());
            gr.setColour(Colour(0xff120f0c));
            gr.fillRoundedRectangle(m, 2);
            gr.setColour(col::segLine);
            gr.drawRoundedRectangle(m, 2, 1);
            const float f = juce::jlimit(0.f, 1.f, (db + 60) / 60);
            gr.setColour(col::amber);
            gr.fillRect(m.withTop(m.getBottom() - f * m.getHeight()).reduced(1, 0));
        };
        meter(fader.getRight() + 5, meterL);
        if (st) meter(fader.getRight() + 16, meterR);
        // fader value
        gr.setColour(col::gold);
        gr.setFont(Fonts::make(11.5f));
        const String fv = gain <= -60 ? String(L"−∞")
                                      : String(gain > 0   ? L"+"
                                               : gain < 0 ? L"−"
                                                          : L"") +
                                            nf(std::abs(gain), 1) + " dB";
        gr.drawText(fv, juce::Rectangle<float>(0, 192, W, 14), juce::Justification::centred, false);
        // delay number
        delayBox = {8, (float) getHeight() - 30, W / 2 - 10, 22};
        gr.setColour(col::field);
        gr.fillRoundedRectangle(delayBox, 4);
        gr.setColour(col::fieldLine);
        gr.drawRoundedRectangle(delayBox.reduced(0.5f), 4, 1);
        gr.setColour(col::text);
        gr.drawText(nf((double) G("delay"), 1) + " ms", delayBox, juce::Justification::centred, false);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        drag = None;
        if (fader.expanded(6, 8).contains(e.position)) {
            drag = Fader;
            dragY = e.position.y;
            dragV = toP((double) G("gain"));
        } else if (delayBox.contains(e.position)) {
            drag = Delay;
            dragY = e.position.y;
            dragV = (double) G("delay");
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag == Fader)
            setDb(fromP(juce::jlimit(
                0.0, 1.0, dragV + (dragY - e.position.y) / fader.getHeight() * (e.mods.isShiftDown() ? 0.25 : 1))));
        else if (drag == Delay)
            setDelay(dragV + (dragY - e.position.y) * (e.mods.isShiftDown() ? 0.02 : 0.1));
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (drag == None && e.mouseWasClicked()) {
            const char* keys[3] = {"pol", "mute", "solo"};
            for (int i = 0; i < (int) toggleRects.size(); i++)
                if (toggleRects[(size_t) i].contains(e.position)) {
                    ctx.st.set(path(keys[i]), !(bool) G(keys[i]));
                    act.sendMics();
                    ctx.syncAll();
                }
        }
        drag = None;
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (fader.expanded(6, 8).contains(e.position))
            setDb(0);
        else if (delayBox.contains(e.position))
            setDelay(0);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const double d = w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0;
        if (fader.expanded(6, 8).contains(e.position)) {
            const double db = (double) G("gain") <= -60 ? -48 : (double) G("gain");
            setDb(db + d * (e.mods.isShiftDown() ? 0.5 : 1));
        } else if (delayBox.contains(e.position))
            setDelay((double) G("delay") + d * (e.mods.isShiftDown() ? 0.1 : 0.5));
    }
    String getTooltip() {
        auto p = getMouseXYRelative().toFloat();
        if (fader.expanded(6, 8).contains(p))
            return tipText("Group fader", L"Drag or scroll · Shift = fine · Double-click = 0 dB");
        if (delayBox.contains(p))
            return tipText("Delay: time-align the group to the others.", L"Drag or scroll · Double-click = 0");
        if (toggleRects.size() == 3) {
            if (toggleRects[0].contains(p))
                return L"Flip polarity – check it against the other groups, the drum mics often want it.";
            if (toggleRects[1].contains(p)) return "Mute";
            if (toggleRects[2].contains(p)) return "Solo";
        }
        return {};
    }

private:
    String path(const char* k) const { return "mics.grp." + String(g) + "." + k; }
    var G(const char* k) const { return ctx.st.get(path(k)); }
    void set(const char* k, const var& v) {
        ctx.st.set(path(k), v);
        act.sendMics();
        ctx.syncAll();
    }
    static double toP(double db) { return db <= -60 ? 0 : juce::jlimit(0.02, 1.0, (db + 48) / 60); }
    static double fromP(double p) { return p < 0.02 ? -100 : std::round((-48 + 60 * p) * 2) / 2; }
    void setDb(double db) { set("gain", juce::jlimit(-100.0, 12.0, db)); }
    void setDelay(double v) { set("delay", juce::jlimit(0.0, 50.0, std::round(v * 10) / 10)); }
    Opts outOptions() const {
        const int n = std::min(16, std::max((int) ctx.st.audioInfo["outputs"], 2));
        const bool st = isStereo();
        Opts o{{-1, "front"}, {-2, "rear"}};
        for (int k = 0; k < (st ? n - 1 : n); k++)
            o.push_back({k, st ? "Out " + String(k + 1) + "/" + String(k + 2) : "Out " + String(k + 1)});
        return o;
    }
    var outValue() const {
        const int n = std::min(16, std::max((int) ctx.st.audioInfo["outputs"], 2));
        return std::min((int) G("out"), isStereo() ? n - 2 : n - 1);
    }
    Ctx& ctx;
    Actions& act;
    int g;
    String name;
    std::function<bool()> isStereo;
    std::function<String()> info;
    std::unique_ptr<Knob> panK, widthK;
    Select out;
    juce::Rectangle<float> fader, delayBox;
    std::vector<juce::Rectangle<float>> toggleRects;
    enum Drag { None, Fader, Delay } drag = None;
    float dragY = 0;
    double dragV = 0;
};

class MixerDesk : public Component, public juce::TooltipClient {
public:
    MixerDesk(Ctx& c, Actions& a) {
        strips.push_back(std::make_unique<MixerStrip>(
            c, a, 0, "Horn", [&c] { return c.st.flag("mics.hornStereo"); },
            [&c] { return String((int) c.st.num("mics.hornAngle")) + L"°"; }));
        strips.push_back(std::make_unique<MixerStrip>(
            c, a, 1, "Drum", [&c] { return c.st.flag("mics.drumStereo"); },
            [&c] { return String((int) c.st.num("mics.drumAngle")) + L"°"; }));
        strips.push_back(std::make_unique<MixerStrip>(
            c, a, 2, "Room", [] { return true; },
            [&c] {
                const char* n[] = {"AB", "XY", "ORTF"};
                return String(n[juce::jlimit(0, 2, (int) c.st.num("mics.roomArr"))]);
            }));
        for (auto& s : strips) addAndMakeVisible(*s);
    }
    void resized() override {
        const float w = (getWidth() - 16) / 3.f;
        for (int i = 0; i < 3; i++) strips[(size_t) i]->setBounds((int) (i * (w + 8)), 0, (int) w, getHeight());
    }
    void setMeters(const juce::Array<var>& mm) {
        for (int g = 0; g < 3; g++) {
            auto& s = *strips[(size_t) g];
            const float l = mm.size() > 2 * g ? (float) (double) mm[2 * g] : -120.f;
            const float r = mm.size() > 2 * g + 1 ? (float) (double) mm[2 * g + 1] : -120.f;
            hold[2 * g] = std::max(l, hold[2 * g] - 0.6f);
            hold[2 * g + 1] = std::max(r, hold[2 * g + 1] - 0.6f);
            s.meterL = hold[2 * g];
            s.meterR = hold[2 * g + 1];
            s.repaint();
        }
    }
    String getTooltip() override {
        for (auto& s : strips)
            if (s->isMouseOver(true)) return s->getTooltip();
        return {};
    }
    static constexpr int HEIGHT = 246;

private:
    std::vector<std::unique_ptr<MixerStrip>> strips;
    float hold[6] = {-120, -120, -120, -120, -120, -120};
};

/** Builds the settings cards. */
class Cards {
public:
    Cards(Ctx& c, Actions& a) : ctx(c), act(a), st(c.st) {}

    static int columns(const String& id) {
        static const std::map<String, int> cols{{"orgel", 5},   {"rotary", 5}, {"raum", 3}, {"mikros", 5},
                                                {"ausgabe", 4}, {"midi", 3},   {"audio", 3}};
        return cols.at(id);
    }
    MixerDesk* desk = nullptr;
    Button* testButton = nullptr;

    std::unique_ptr<Page> build(const String& id) {
        ctx.curCard = id;
        auto p = std::make_unique<Page>(columns(id));
        if (id == "orgel")
            organ(*p);
        else if (id == "rotary")
            rotary(*p);
        else if (id == "raum")
            room(*p);
        else if (id == "mikros")
            mics(*p);
        else if (id == "ausgabe")
            output(*p);
        else if (id == "midi")
            midi(*p);
        else if (id == "audio")
            audio(*p);
        ctx.curCard = {};
        return p;
    }

private:
    Ctx& ctx;
    Actions& act;
    UiState& st;
    using KnobList = std::vector<std::unique_ptr<Knob>>;
    using RowList = std::vector<std::unique_ptr<Row>>;
    using Fmt = std::function<String(double)>;

    // ---- small builders ----
    std::unique_ptr<Knob> knob(KnobOpts o) { return std::make_unique<Knob>(ctx, std::move(o)); }
    /** Knob bound to a state path; after() runs after the value was stored. */
    std::unique_ptr<Knob> K(const String& label, const String& path, double mn, double mx, double def, Fmt fmt,
                            std::function<void()> after, double step = 0, bool log = false) {
        KnobOpts o;
        o.label = label;
        o.min = mn;
        o.max = mx;
        o.def = def;
        o.fmt = std::move(fmt);
        o.step = step;
        o.log = log;
        o.get = [this, path] {
            return st.get(path);
        };
        o.set = [this, path, after](const var& v) {
            st.set(path, v);
            if (after) after();
        };
        return knob(std::move(o));
    }
    /** Segmented row bound to a state path. */
    std::unique_ptr<Row> Seg(const String& label, Opts opts, const String& path, std::function<void()> after, var def) {
        return rowSeg(
            ctx, label, std::move(opts), [this, path] { return st.get(path); },
            [this, path, after](const var& v) {
                st.set(path, v);
                if (after) after();
            },
            def);
    }
    std::unique_ptr<Row> Sel(const String& label, Opts opts, const String& path, std::function<void()> after, var def) {
        return rowSel(
            ctx, label, std::move(opts), [this, path] { return st.get(path); },
            [this, path, after](const var& v) {
                st.set(path, (int) v);
                if (after) after();
            },
            def);
    }
    std::unique_ptr<Section> sec(const String& title, KnobList ks, RowList rs, std::function<String()> note,
                                 SecOpts o = {}) {
        return std::make_unique<Section>(ctx, title, std::move(ks), std::move(rs), std::move(note), std::move(o),
                                         cardTips());
    }
    static std::function<String()> text(const String& s) {
        return s.isEmpty() ? std::function<String()>() : [s] {
            return s;
        };
    }
    template <class... T> static KnobList knobs(T&&... k) {
        KnobList v;
        (v.push_back(std::forward<T>(k)), ...);
        return v;
    }
    template <class... T> static RowList rows(T&&... r) {
        RowList v;
        (v.push_back(std::forward<T>(r)), ...);
        return v;
    }
    std::function<void()> sync() {
        return [this] {
            ctx.syncAll();
        };
    }
    static String secs(double v, int d = 1) { return nf(v, d) + " s"; }
    static Opts onOff() { return {{true, "on"}, {false, "off"}}; }
    static Opts ccOpts() {
        return {{1, "CC 1 Mod wheel"}, {64, "CC 64 Sustain"}, {4, "CC 4 Foot controller"}, {80, "CC 80 Switch"}};
    }

    // ---------------- Organ ----------------
    void organ(Page& p) {
        auto condK = [&](const char* id, const char* label, double def) {
            KnobOpts o;
            o.label = label;
            o.def = def;
            o.fmt = pct;
            const String path = "chr." + String(id);
            o.get = [this, path] {
                return st.get(path);
            };
            const String sid = id;
            o.set = [this, path, sid](const var& v) {
                st.set(path, v);
                if (sid == "cond") {
                    st.set("chr.leak", v);
                    st.set("chr.contact", v);
                    st.set("chr.motor", std::min(1.0, (double) v * 0.8 + 0.1));
                }
                act.sendChr();
                ctx.syncAll();
            };
            return knob(o);
        };
        p.add(sec("Character",
                  knobs(condK("cond", "Condition", 0.5), condK("leak", "Leakage", 0.5),
                        condK("contact", "Contacts", 0.5), condK("motor", "Motor", 0.4)),
                  {},
                  text("Condition pulls the other three along and sets the level spread and runout of the tone wheels: "
                       "from showroom-fresh to 40 years on the road.")));
        auto organ = [this] {
            act.sendOrgan();
        };
        auto chr = [this] {
            act.sendChr();
        };
        p.add(sec("Generator", knobs(K("Key noise", "organ.keyNoise", 0, 1, 0.5, offPct, organ)),
                  rows(Seg("Tuning", {{true, "Gears"}, {false, "Equal"}}, "chr.gearTune", chr, true),
                       Seg("Busbar load", onOff(), "chr.busComp", chr, true),
                       Seg("Low wheels", {{true, "Complex"}, {false, "Sine"}}, "organ.complex", organ, true),
                       Seg(
                           "Mains", {{50, "50 Hz"}, {60, "60 Hz"}}, "mains", [this] { act.sendMains(); }, 50)),
                  text(L"Low wheels: nearly square, pedal only. Key noise: keys bottoming out on the felt – the "
                       "console sits right in front of you.")));
        {
            auto susLen = K(
                "Sustain", "organ.pedSusLen", 1, 5, 3, [](double v) { return String(juce::roundToInt(v)); }, organ, 1);
            susLen->visibleFn = [this] {
                return st.flag("organ.pedSus");
            };
            p.add(sec("Pedal", knobs(std::move(susLen)),
                      rows(Seg(
                          "Bass", {{false, "B-3"}, {true, "Sustain"}}, "organ.pedSus",
                          [this] {
                              act.sendOrgan();
                              ctx.syncAll();
                          },
                          false)),
                      text(L"B-3: the pedal is keyed like the manuals and stops when you lift your foot. Sustain: "
                           L"the note fades out after release (1 short … 5 long), as on the Hammonds with pedal "
                           L"sustain – spinets, H-100, X-66, a European A-100 – and the later clones.")));
        }
        Opts vib;
        for (auto& v : *tables()["vib"].getArray()) vib.push_back({v[0], v[1].toString()});
        p.add(sec(
            L"Percussion · Vibrato · Click",
            knobs(K("Key click", "click", 0, 1, 0.5, pct, [this] { ctx.sendV("click", st.get("click")); }),
                  K("Perc level", "organ.percLevel", 0, 1.5, 1, pct, organ),
                  K(
                      "Fast decay", "organ.percShort", 0.1, 0.6, 0.2, [](double v) { return secs(v, 2); }, organ, 0.01),
                  K(
                      "Slow decay", "organ.percLong", 0.5, 2.5, 1.0, [](double v) { return secs(v, 1); }, organ, 0.05)),
            rows(Seg(
                     "Vibrato", vib, "vib",
                     [this] {
                         ctx.sendV("vib", st.get("vib"));
                         ctx.syncAll();
                     },
                     "c3"),
                 Seg(
                     "Click", {{true, "Velocity"}, {false, "Fixed"}}, "velClick", [this] { act.sendMains(); }, true)),
            text("Velocity: a fast stroke closes all nine contacts almost at once and hard, a slow press one after "
                 "another and softer.")));
        auto pre = [this] {
            act.sendPre();
        };
        p.add(sec("Preamp AO-28", knobs(K("Gain", "pre.gain", 0, 1, 0.5, pct, pre)),
                  rows(Seg("Tubes", {{true, "on"}, {false, "bypass"}}, "pre.on", pre, true),
                       rowVal(ctx, "Quality",
                              [this] {
                                  const char* q[] = {"Eco", "HQ", "Ultra"};
                                  return String(q[juce::jlimit(0, 2, (int) st.num("amp.quality"))]) + " (= power amp)";
                              })),
                  text("Post-swell: 12AX7 and a 12BH7 push-pull into a transformer. Swell wide open and full drawbars "
                       L"– the organ already breaks up before the Rotary.")));
        auto spring = [this] {
            act.sendSpring();
        };
        p.add(sec("Spring Reverb",
                  knobs(K("Mix", "spring.mix", 0, 1, 0.2, offPct,
                          [this] {
                              act.sendSpring();
                              ctx.syncAll();
                          }),
                        K(
                            "Decay", "spring.decay", 1, 5, 2.8, [](double v) { return secs(v, 1); }, spring, 0.1),
                        K(
                            "Tone", "spring.tone", 0, 1, 0.5,
                            [](double v) {
                                return v < 0.33 ? String("dark") : v > 0.66 ? String("bright") : String("mid");
                            },
                            spring),
                        K("Drive", "spring.drive", 0, 1, 0.3, pct, spring)),
                  rows(Seg("Springs", {{2, "2"}, {3, "3"}}, "spring.springs", spring, 3),
                       rowBtn("Tank", "Kick", [this] { ctx.sendV("springKick", 1); })),
                  text(L"Organ spring tank as in the A-100 and 122RV: inside the organ, before the Rotary – so the "
                       "reverb spins too.")));
    }

    // ---------------- Rotary ----------------
    void rotary(Page& p) {
        auto rpm = [](double v) {
            return String(juce::roundToInt(v * 60)) + " rpm";
        };
        auto s1 = [](double v) {
            return secs(v, 1);
        };
        auto rk = [&](int r, const char* k, const char* label, double mn, double mx, double def, Fmt fmt, bool log) {
            return K(label, "rotor." + String(r) + "." + k, mn, mx, def, fmt, [this, r] { act.sendRotor(r); }, 0, log);
        };
        p.add(sec("Horn",
                  knobs(rk(0, "slow", "Slow", 0.5, 1.2, 0.8, rpm, false),
                        rk(0, "fast", "Fast", 5.5, 7.6, 6.67, rpm, false), rk(0, "up", "Rise", 0.2, 3, 0.5, s1, true),
                        rk(0, "down", "Fall", 0.3, 4, 0.85, s1, true)),
                  {}, text("The light horn gets up to speed in under a second.")));
        p.add(sec("Drum",
                  knobs(rk(1, "slow", "Slow", 0.4, 1.0, 0.667, rpm, false),
                        rk(1, "fast", "Fast", 4.5, 6.5, 5.7, rpm, false), rk(1, "up", "Rise", 1, 8, 1.7, s1, true),
                        rk(1, "down", "Fall", 1.5, 10, 2.5, s1, true)),
                  rows(Seg("Brake", onOff(), "brake", [this] { ctx.sendV("brake", st.get("brake")); }, true)), {}));
        Opts models;
        for (auto& m : *tables()["ampModels"].getArray()) models.push_back({m[0], m[1].toString()});
        auto amp = [this] {
            act.sendAmp();
        };
        p.add(sec("Power Amp",
                  knobs(K("Drive", "drive", 0, 1, 0.35, pct,
                          [this] {
                              ctx.sendV("drive", st.get("drive"));
                              ctx.syncAll();
                          }),
                        K("Sag", "amp.sag", 0, 1, 0.5, pct, amp)),
                  rows(Sel(
                           "Model", models, "amp.model",
                           [this] {
                               act.sendAmp();
                               ctx.syncAll();
                           },
                           0),
                       Seg("Quality", {{0, "Eco"}, {1, "HQ"}, {2, "Ultra"}}, "amp.quality", amp, 1),
                       rowVal(ctx, "Plate / cath.", [this] { return st.ampText; })),
                  text("Eco: static curve. HQ and Ultra solve the tubes and the output transformer as a circuit "
                       "(Glow/Iron from Amberwave).")));
        auto cab = [this] {
            act.sendCab();
        };
        std::unique_ptr<Knob> mech, wob;
        {
            KnobOpts o;
            o.label = "Mechanics";
            o.def = -1;
            o.fmt = offPct;
            o.autoMode = KnobOpts::Auto{-1, "auto", [this] {
                                            return 0.15 + 0.7 * st.num("chr.cond");
                                        }};
            o.get = [this] {
                return st.get("cab.mech");
            };
            o.set = [this](const var& v) {
                st.set("cab.mech", v);
                act.sendCab();
            };
            mech = knob(o);
            o.label = "Wobble";
            o.fmt = pct;
            o.autoMode = KnobOpts::Auto{-1, "auto", [this] {
                                            return st.num("chr.cond");
                                        }};
            o.get = [this] {
                return st.get("cab.wobble");
            };
            o.set = [this](const var& v) {
                st.set("cab.wobble", v);
                act.sendCab();
            };
            wob = knob(o);
        }
        p.add(sec("Cabinet",
                  knobs(K("Horn box", "cab.hornBox", 0, 1, 0.45, pct, cab),
                        K("Resonance", "cab.res", 0, 1, 0.5, offPct, cab), std::move(mech), std::move(wob)),
                  rows(Seg("Build", {{"ply", "Plywood"}, {"chip", "Chipboard"}}, "cab.board", cab, "ply"),
                       rowSeg(
                           ctx, "Deflectors", {{true, "In"}, {false, "Out"}},
                           [this] { return var(!(st.get("cab.deflector").isBool() && !st.flag("cab.deflector"))); },
                           [this](const var& v) {
                               st.set("cab.deflector", v);
                               act.sendCab();
                           },
                           true)),
                  text(L"The middle compartment is a vented box for the 15″ woofer. Early cabinets are plywood, "
                       "later ones chipboard (softer, more damped).")));
        KnobOpts face;
        face.label = "Facing";
        face.min = -180;
        face.max = 180;
        face.step = 1;
        face.def = 0;
        face.fmt = [](double v) {
            return v == 0 ? String(L"0° front") : String(juce::roundToInt(v)) + L"°";
        };
        face.get = [this] {
            return act.cabDeg();
        };
        face.set = [this](const var& v) {
            act.setCabYaw(PI / 2 + (double) v * PI / 180);
        };
        p.add(sec("Placement",
                  knobs(K(
                            "Balance", "balance", 0, 1, 0.5,
                            [](double v) {
                                return String(juce::roundToInt((1 - v) * 100)) + "/" +
                                       String(juce::roundToInt(v * 100));
                            },
                            [this] { ctx.sendV("balance", st.get("balance")); }),
                        knob(face)),
                  rows(rowBtn("Quick turn", "Face the listener", [this] { act.faceListener(); }),
                       rowBtn("", "Back to the wall", [this] { act.backToWall(); })),
                  {}));
        p.add(sec(
            "Control", {},
            rows(Sel(
                     "Switch via", ccOpts(), "midi.rotaryCC",
                     [this] {
                         act.sendMidi();
                         ctx.syncAll();
                     },
                     1),
                 rowSeg(
                     ctx, "Half-moon", {{"two", "2-way"}, {"stop", "with Stop"}}, [this] { return st.get("halfMoon"); },
                     [this](const var& v) {
                         st.set("halfMoon", v);
                         if (v.toString() != "stop" && st.str("rotary") == "stop") act.setRotary("slow");
                         ctx.syncAll();
                         if (ctx.ready && ctx.stateChanged) ctx.stateChanged();
                     },
                     "stop")),
            text("The space bar always toggles Slow/Fast. You can also turn the Rotary in the floor plan with the "
                 "scroll wheel.")));
    }

    // ---------------- Room ----------------
    void room(Page& p) {
        auto m = [](double v) {
            return nf(v, 1) + " m";
        };
        auto rk = [&](const char* id, const char* label, double mn, double mx, double def, Fmt fmt) {
            const String sid = id;
            KnobOpts o;
            o.label = label;
            o.min = mn;
            o.max = mx;
            o.def = def;
            o.step = sid == "absorb" || sid == "reverb" ? 0.01 : 0.1;
            o.fmt = std::move(fmt);
            o.get = [this, sid] {
                return st.get("room." + sid);
            };
            o.set = [this, sid](const var& v) {
                st.set("room." + sid, v);
                act.clampPos();
                auto* ro = new juce::DynamicObject();
                ro->setProperty(juce::Identifier(sid), v);
                ctx.send(Ctx::obj({{"type", "room"}, {"v", var(ro)}}));
                act.sendPos();
            };
            return knob(o);
        };
        p.add(sec("Dimensions",
                  knobs(rk("Lx", "Width", 3, 30, 6, m), rk("Ly", "Length", 3, 40, 8, m),
                        rk("Lz", "Height", 2.2, 15, 2.8, m), rk("reverb", "Reverb", 0, 2, 1, pct)),
                  {}, {}));
        KnobOpts face;
        face.label = "Facing";
        face.min = -180;
        face.max = 180;
        face.step = 1;
        face.def = 0;
        face.fmt = [](double v) {
            return String(juce::roundToInt(v)) + L"°";
        };
        face.get = [this] {
            return act.yawDeg();
        };
        face.set = [this](const var& v) {
            act.setYaw(PI / 2 + (double) v * PI / 180);
        };
        Opts mats;
        for (auto& mt : *tables()["materials"].getArray()) mats.push_back({mt[0], mt[1].toString()});
        p.add(sec(L"Surfaces · Listener", knobs(rk("absorb", "Absorption", 0.05, 0.8, 0.3, pct), knob(face)),
                  rows(rowSel(
                           ctx, "Material", mats, [this] { return st.get("room.material"); },
                           [this](const var& v) {
                               st.set("room.material", v);
                               ctx.send(Ctx::obj({{"type", "room"}, {"v", Ctx::obj({{"material", v}})}}));
                               ctx.syncAll();
                           },
                           "wood"),
                       rowBtn("Head", "Look at the Rotary", [this] { act.lookAtRotary(); })),
                  {}));
        Opts orders;
        for (int v = 1; v <= 5; v++) orders.push_back({v, String(v)});
        p.add(
            sec(L"Engine · Presets", {},
                rows(Seg(
                         "Reflections", orders, "room.order",
                         [this] {
                             ctx.send(Ctx::obj({{"type", "room"}, {"v", Ctx::obj({{"order", st.get("room.order")}})}}));
                             ctx.syncAll();
                         },
                         2),
                     Seg(
                         "Precision", {{false, "Fast"}, {true, "Exact"}}, "exact",
                         [this] {
                             act.sendPrecision();
                             ctx.syncAll();
                         },
                         false),
                     rowVal(ctx, "Paths",
                            [this] {
                                if (!st.roomInfo.isObject()) return String(L"–");
                                return String((int) st.roomInfo["paths"]) + L" · " +
                                       (st.flag(L"exact") ? L"all exact" : L"grouped ≥ 2nd");
                            }),
                     rowSel(
                         ctx, "Load room",
                         [this] {
                             Opts o{{"", act.matchRoom() >= 0 ? String(L"–") : String("Custom room")}};
                             auto* rooms = tables()["rooms"].getArray();
                             for (int i = 0; i < rooms->size(); i++)
                                 o.push_back({i, rooms->getReference(i)["n"].toString()});
                             return o;
                         },
                         [this] {
                             const int i = act.matchRoom();
                             return i < 0 ? var("") : var(i);
                         },
                         [this](const var& v) {
                             if (v.toString().isNotEmpty()) act.loadRoom((int) v);
                         }),
                     rowVal(ctx, "CPU", [this] { return st.cpuText; })),
                text("Exact renders every reflection per sample with position, Doppler, directivity and panning. From "
                     "4th order that takes a lot of CPU.")));
    }

    // ---------------- Mics ----------------
    void mics(Page& p) {
        Opts micTypes{{0, L"57 · dynamic"},   {1, L"421 · dynamic"}, {2, L"D112 · kick"}, {3, L"RE20 · supercardioid"},
                      {4, L"87 · condenser"}, {5, L"KM 84 · SDC"},   {6, L"121 · ribbon"}};
        Opts patterns{{0, "Cardioid"}, {1, "Omni"}, {2, "Figure-8"}};
        auto mk = [this] {
            act.sendMics();
            ctx.syncAll();
        };
        auto degFmt = [](double v) {
            return v == 0 ? String(L"0° front") : String(juce::roundToInt(v)) + L"°";
        };
        auto cm = [](double v) {
            return String(juce::roundToInt(v * 100)) + " cm";
        };
        auto arrSeg = [&](const char* label, const char* kSt, const char* kAng, Opts opts) {
            const String s = "mics." + String(kSt), a = "mics." + String(kAng);
            return rowSeg(
                ctx, label, std::move(opts), [this, s, a] { return st.flag(s) ? st.get(a) : var(0); },
                [this, s, a](const var& v) {
                    if ((int) v == 0)
                        st.set(s, false);
                    else {
                        st.set(s, true);
                        st.set(a, v);
                    }
                    act.sendMics();
                    ctx.syncAll();
                },
                180);
        };
        auto patRow = [&](const char* kType, const char* kPat) {
            auto r = Sel("Pattern", patterns, "mics." + String(kPat), [this] { act.sendMics(); }, 0);
            const String t = "mics." + String(kType);
            r->visible = [this, t] {
                return (int) st.num(t) == 4;
            };
            return r;
        };
        auto micSel = [&](const char* k, int def) {
            return Sel("Mic", micTypes, "mics." + String(k), mk, def);
        };
        p.add(sec("Horn Mics",
                  knobs(K("Angle", "mics.hornCenter", -180, 180, 0, degFmt, mk, 5),
                        K("Distance", "mics.hornDist", 0.03, 1.0, 0.15, cm, mk, 0, true),
                        K(
                            "Height", "mics.hornHeight", -0.3, 0.3, 0,
                            [](double v) {
                                return String(v > 0   ? L"+"
                                              : v < 0 ? L"−"
                                                      : L"") +
                                       String(juce::roundToInt(std::abs(v) * 100)) + " cm";
                            },
                            mk, 0.01)),
                  rows(arrSeg("Setup", "hornStereo", "hornAngle",
                              {{0, "Mono"}, {90, L"90°"}, {120, L"120°"}, {180, L"180°"}}),
                       micSel("hornType", 0), patRow("hornType", "hornPattern"),
                       Seg("Windscreen", {{false, "off"}, {true, "on"}}, "mics.windscreen", mk, false)),
                  {}));
        p.add(sec("Drum Mics",
                  knobs(K("Angle", "mics.drumCenter", -180, 180, 0, degFmt, mk, 5),
                        K("Distance", "mics.drumDist", 0.03, 1.0, 0.15, cm, mk, 0, true)),
                  rows(arrSeg("Setup", "drumStereo", "drumAngle", {{0, "Mono"}, {90, L"90°"}, {180, L"180°"}}),
                       micSel("drumType", 2), patRow("drumType", "drumPattern")),
                  text(L"From the back (180°) you get more motor, from the front more cabinet.")));
        KnobOpts aim;
        aim.label = "Aim";
        aim.min = -180;
        aim.max = 180;
        aim.step = 1;
        aim.def = 0;
        aim.fmt = [](double v) {
            return String(juce::roundToInt(v)) + L"°";
        };
        aim.get = [this] {
            return wrapDeg(st.num("mics.roomYaw"), PI / 2);
        };
        aim.set = [this](const var& v) {
            st.set("mics.roomYaw", PI / 2 + (double) v * PI / 180);
            act.sendMics();
            ctx.syncAll();
        };
        KnobOpts refl;
        refl.label = "Reflect.";
        refl.opts = {{1, "1st"}, {2, "2nd"}, {3, "3rd"}};
        refl.def = 2;
        refl.get = [this] {
            return st.get("mics.roomOrder");
        };
        refl.set = [this](const var& v) {
            st.set("mics.roomOrder", v);
            act.sendMics();
            ctx.syncAll();
        };
        p.add(sec(
            "Room Pair",
            knobs(K(
                      "Spacing", "mics.roomSpacing", 0.2, 3, 0.6, [](double v) { return nf(v, 2) + " m"; }, mk, 0.05),
                  K(
                      "Height", "mics.roomZ", 0.5, 4, 2, [](double v) { return nf(v, 1) + " m"; }, mk, 0.05),
                  knob(aim), knob(refl)),
            rows(rowSeg(
                     ctx, "Array", {{0, "AB"}, {1, "XY"}, {2, "ORTF"}}, [this] { return st.get("mics.roomArr"); },
                     [this](const var& v) {
                         st.set("mics.roomArr", v);
                         if ((int) v > 0 && (int) st.num("mics.roomType") == 4 && (int) st.num("mics.roomPattern") == 1)
                             st.set("mics.roomPattern", 0);
                         act.sendMics();
                         ctx.syncAll();
                     },
                     0),
                 micSel("roomType", 4), patRow("roomType", "roomPattern"),
                 rowBtn("", "Place in front",
                        [this] {
                            act.placeRoomPair();
                            act.sendMics();
                            ctx.syncAll();
                        })),
            {}));
        SecOpts o;
        o.span = 2;
        o.head = std::make_unique<wh::Seg>(
            ctx, Opts{{false, "Listener"}, {true, "Mics"}}, [this] { return st.get("mics.on"); },
            [this](const var& v) { act.setMicsOn((bool) v); }, false);
        auto d = std::make_unique<MixerDesk>(ctx, act);
        desk = d.get();
        o.extra = std::move(d);
        o.extraHeight = MixerDesk::HEIGHT;
        p.add(sec("Mixer", {}, {}, {}, std::move(o)));
    }

    // ---------------- Output ----------------
    void output(Page& p) {
        auto out = [this] {
            act.sendOut();
        };
        p.add(sec("Format",
                  knobs(K("LFE", "lfe", 0, 1, 0.5, pct, [this] { ctx.sendV("lfe", st.get("lfe")); }),
                        K(
                            "LFE x-over", "out.lfeHz", 60, 160, 100,
                            [](double v) { return String(juce::roundToInt(v)) + " Hz"; }, out, 5),
                        K("Master", "out.masterDb", -24, 6, 0, dbText, out, 0.5)),
                  rows(Seg(
                      "Format", {{"stereo", "Stereo"}, {"quad", "Quad"}, {"s51", "5.1"}}, "mode",
                      [this] {
                          act.applyMode();
                          ctx.syncAll();
                      },
                      "stereo")),
                  text("LFE and crossover only apply in 5.1.")));
        RowList map;
        for (int i = 0; i < 6; i++) {
            auto r = rowSel(
                ctx, "Ch " + String(i + 1),
                [this] {
                    Opts o;
                    for (int k = 0; k < 8; k++) o.push_back({k, act.outName(k)});
                    return o;
                },
                [this, i] {
                    auto m = act.curMap();
                    return i < m.size() ? var(m[i]) : var(i);
                },
                [this, i](const var& v) {
                    auto m = act.curMap();
                    juce::Array<var> a;
                    for (int c : m) a.add(c);
                    if (i < a.size()) a.set(i, (int) v);
                    st.set("chmap." + st.str("mode"), a);
                    act.applyMode();
                    ctx.syncAll();
                });
            r->visible = [this, i] {
                return i < act.modeChannels();
            };
            auto* rp = r.get();
            labelUpdaters.push_back([this, rp, i] {
                const var n = at(act.mode()["long"], i);
                if (rp->label != n.toString()) {
                    rp->label = n.toString();
                    rp->repaint();
                }
            });
            map.push_back(std::move(r));
        }
        map.push_back(rowBtn("", L"Auto (1, 2, 3 …)", [this] {
            st.set("chmap." + st.str("mode"), var());
            act.applyMode();
            ctx.syncAll();
        }));
        p.add(sec("Routing", {}, std::move(map), {}));
        auto rear = K("Rear", "out.rearDb", -12, 3, 0, dbText, out, 0.5);
        auto cent = K("Center", "out.centerDb", -12, 3, 0, dbText, out, 0.5);
        auto div = K(
            "Divergence", "out.centerDiv", 0, 1, 0,
            [](double v) { return v < 0.005   ? String("center only")
                                  : v > 0.995 ? String("phantom")
                                              : pct(v); }, out);
        cent->visibleFn = [this] {
            return st.str("mode") == "s51";
        };
        div->visibleFn = [this] {
            return st.str("mode") == "s51";
        };
        SecOpts mon;
        mon.visible = [this] {
            return st.str("mode") != "stereo";
        };
        p.add(sec("Monitoring", knobs(std::move(rear), std::move(cent), std::move(div)), {},
                  text("Trims for your speakers, not the room. Center and divergence only in 5.1."), std::move(mon)));
        auto test = rowBtn("Channel test", "Ping all channels", [this] { chanTest(); });
        testButton = dynamic_cast<Button*>(test->control.get());
        p.add(
            sec("Check", {},
                rows(std::move(test),
                     rowVal(ctx, "Device",
                            [this] {
                                const String d = st.audioInfo["device"].toString();
                                return d.isNotEmpty() ? d + L", " + String((int) st.audioInfo["outputs"]) + L" outputs"
                                                      : String(L"–");
                            }),
                     rowVal(ctx, "Status",
                            [this] { return act.warnText.isNotEmpty() ? act.warnText : String("all good"); })),
                text("With a source straight ahead the rear only gets the room: in a small, live room about 8 dB below "
                     "the front, more with distance and reverb.")));
        modeLabels.fn = [this] {
            syncLabels();
        };
        ctx.reg(&modeLabels);
    }
    struct LabelSync : Syncable {
        std::function<void()> fn;
        void sync() override {
            if (fn) fn();
        }
    } modeLabels;
    std::vector<std::function<void()>> labelUpdaters;

public:
    /** Pings every channel of the current format in turn (0.75 s apart). */
    void chanTest() {
        const int n = act.modeChannels();
        testStep = 0;
        testTimer.callback = [this, n] {
            if (!testButton) return;
            if (testStep >= n) {
                testButton->setButtonText("Ping all channels");
                testButton->toggledOn = false;
                testButton->repaint();
                testTimer.stopTimer();
                return;
            }
            const int i = testStep++;
            const int phys = act.physCh(i);
            testButton->setButtonText(String(i + 1) + "/" + String(n) + ": " + at(act.mode()["long"], i).toString() +
                                      L" → " + String(phys + 1));
            testButton->toggledOn = true;
            testButton->repaint();
            if (ctx.invoke)
                ctx.invoke("chantest:" + String(phys) + ":" + (st.str("mode") == "s51" && i == 3 ? "1" : "0"));
        };
        testTimer.callback();
        testTimer.startTimer(750);
    }
    void syncLabels() {
        for (auto& f : labelUpdaters) f();
    }

private:
    struct FnTimer : juce::Timer {
        std::function<void()> callback;
        void timerCallback() override {
            if (callback) callback();
        }
    } testTimer;
    int testStep = 0;

    // ---------------- MIDI ----------------
    void midi(Page& p) {
        auto chOpts = [](bool omni) {
            Opts o{{0, omni ? "Omni (all)" : "off"}};
            for (int i = 1; i <= 16; i++) o.push_back({i, "Channel " + String(i)});
            return o;
        };
        auto midiSync = [this] {
            act.sendMidi();
            ctx.syncAll();
        };
        p.add(sec("Channels", {},
                  rows(Sel("Upper", chOpts(true), "midi.upper", midiSync, 1),
                       Sel("Lower", chOpts(false), "midi.lower", midiSync, 2),
                       Sel("Pedal", chOpts(false), "midi.pedal", midiSync, 3)),
                  text("Upper on Omni: everything that is not on the lower or pedal channel plays the upper manual.")));
        auto chip = [&](const char* label, const char* k, const char* name) {
            auto c = std::make_unique<SplitChip>(ctx, act, k, name, true);
            auto* cp = c.get();
            auto r = std::make_unique<Row>(label, std::move(c));
            r->fitControl = [cp] {
                return cp->naturalWidth();
            };
            return r;
        };
        auto hi = chip("Split point", "hi", "Upper");
        hi->visible = [this] {
            return st.num("split.mode") >= 1;
        };
        auto lo = chip("", "lo", "Lower");
        lo->visible = [this] {
            return (int) st.num("split.mode") == 2;
        };
        Opts oct;
        for (int v = -3; v <= 3; v++)
            oct.push_back({v, v > 0 ? "+" + String(v) : v < 0 ? L"−" + String(-v) : String("0")});
        auto octRow = [&](const char* label, int man) {
            return Seg(
                label, oct, "split.oct." + String(man),
                [this] {
                    act.sendKeyMap();
                    ctx.syncAll();
                },
                0);
        };
        p.add(sec(
            L"Split · Octaves", {},
            rows(rowSeg(
                     ctx, "Split", {{0, "off"}, {1, "2 zones"}, {2, "3 zones"}},
                     [this] { return st.get("split.mode"); },
                     [this](const var& v) {
                         st.set("split.mode", v);
                         act.learnTarget = {};
                         act.sendKeyMap();
                         ctx.syncAll();
                     },
                     0),
                 std::move(hi), std::move(lo),
                 Sel(
                     "Keyboard", {{49, "49 keys"}, {61, "61 keys"}, {73, "73 keys"}, {76, "76 keys"}, {88, "88 keys"}},
                     "split.kb",
                     [this] {
                         ctx.syncAll();
                         if (ctx.ready && ctx.stateChanged) ctx.stateChanged();
                     },
                     61),
                 octRow("Upper octave", 0), octRow("Lower octave", 1), octRow("Pedal octave", 2)),
            text(L"Split works on the upper channel · ● learns from the next key.")));
        p.add(sec("Controllers", {},
                  rows(Sel("Rotary", ccOpts(), "midi.rotaryCC", midiSync, 1),
                       Sel("Swell", {{11, "CC 11 Expression"}, {7, "CC 7 Volume"}, {4, "CC 4 Foot controller"}},
                           "midi.swellCC", midiSync, 11),
                       rowVal(ctx, "Devices",
                              [this] {
                                  juce::StringArray d;
                                  if (auto* a = st.audioInfo["midiDevices"].getArray())
                                      for (auto& x : *a) d.add(x.toString());
                                  return d.isEmpty() ? String("none found") : d.joinIntoString(", ");
                              }),
                       rowBtn("", L"MIDI Inputs …",
                              [this] {
                                  if (ctx.invoke) ctx.invoke("audioSettings");
                              })),
                  text("Newly connected MIDI devices are enabled automatically.")));
        p.add(sec("QWERTY Keys", {},
                  rows(rowSeg(
                           ctx, "Plays", {{0, "Upper"}, {1, "Lower"}, {2, "Pedal"}}, [this] { return st.get("pcMan"); },
                           [this](const var& v) {
                               if (releasePc) releasePc();
                               st.set("pcMan", v);
                               ctx.syncAll();
                               if (ctx.ready && ctx.stateChanged) ctx.stateChanged();
                           },
                           0),
                       rowVal(ctx, "Octave",
                              [this] {
                                  const int o = (int) st.num("octave");
                                  return (o > 0 ? "+" : "") + String(o);
                              }),
                       rowVal(ctx, "Keys", [] { return String(L"Z–M and Q–P, sharps above (Y–M on QWERTZ)"); })),
                  text(L"Space: toggle Rotary · Esc: panic.")));
    }

public:
    std::function<void()> releasePc;

private:
    // ---------------- Audio ----------------
    void audio(Page& p) {
        auto setBtn = rowBtn("", L"Audio Settings …", [this] {
            if (ctx.invoke) ctx.invoke("audioSettings");
        });
        setBtn->visible = [this] {
            return !act.isPlugin();
        };
        auto bounce = Seg(
            "Bounce", {{true, "Max quality"}, {false, "As set"}}, "offlineMax", [this] { act.sendOffline(); }, true);
        bounce->visible = [this] {
            return act.isPlugin();
        };
        auto info = [this](const char* k) {
            return [this, k] {
                const String s = st.audioInfo[k].toString();
                return s.isNotEmpty() ? s : String(L"–");
            };
        };
        p.add(sec("Device", {},
                  rows(rowVal(ctx, "Driver", info("type")), rowVal(ctx, "Device", info("device")), std::move(setBtn),
                       std::move(bounce)),
                  [this] {
                      return act.isPlugin()
                                 ? String("In a plugin the host sets sample rate, buffer and outputs.")
                                 : String("Driver (ASIO, Windows Audio), device, sample rate, buffer and outputs "
                                          "are set in the audio dialog; Wheelhouse remembers them.");
                  }));
        p.add(sec("Buffer", {},
                  rows(rowVal(ctx, "Sample rate",
                              [this] {
                                  return act.sampleRate() > 0 ? nf(act.sampleRate() / 1000, 1) + L" kHz" : String(L"–");
                              }),
                       rowVal(ctx, "Buffer",
                              [this] {
                                  const double b = (double) st.audioInfo["bufferSize"];
                                  return b > 0 ? String((int) b) + " samples = " + nf(b / act.sampleRate() * 1000, 1) +
                                                     " ms"
                                               : String(L"–");
                              }),
                       rowVal(ctx, "Output",
                              [this] {
                                  if (act.sampleRate() <= 0) return String(L"–");
                                  return act.isPlugin() ? nf(act.outMs(), 1) + " ms buffer (output latency: host)"
                                                        : nf(act.outMs(), 1) + " ms (driver, incl. buffer)";
                              }),
                       rowVal(ctx, "Processing",
                              [this] {
                                  return act.sampleRate() > 0 ? nf(st.procLatency / act.sampleRate() * 1000, 1) +
                                                                    " ms (tube oversampling)"
                                                              : String(L"–");
                              }),
                       rowVal(ctx, "Total",
                              [this] {
                                  if (act.sampleRate() <= 0) return String(L"–");
                                  return nf(act.outMs() + st.procLatency / act.sampleRate() * 1000, 1) +
                                         " ms + sound path " + nf(act.soundPathMs(), 1) + " ms";
                              })),
                  text(L"Sound path: travel time from the Rotary to the listener or the nearest mic – as in a real "
                       "room. Key-to-computer time (MIDI, USB) can't be measured.")));
        p.add(sec(
            L"Outputs · CPU", {},
            rows(rowVal(ctx, "Active",
                        [this] {
                            const int n = (int) st.audioInfo["outputs"];
                            return n ? String(n) + L" outputs" : String(L"–");
                        }),
                 rowVal(ctx, "Channels",
                        [this] {
                            juce::StringArray s;
                            if (auto* a = st.audioInfo["channelNames"].getArray())
                                for (int i = 0; i < std::min(8, a->size()); i++) s.add(a->getReference(i).toString());
                            return s.isEmpty() ? String(L"–") : s.joinIntoString(", ");
                        }),
                 rowVal(ctx, "CPU", [this] { return act.isPlugin() ? String("see host") : st.cpuText; }),
                 rowVal(ctx, "Dropouts",
                        [this] {
                            return st.droppedBlocks
                                       ? String(st.droppedBlocks) + " blocks skipped (a setting was being applied)"
                                       : String("none");
                        }),
                 rowSeg(
                     ctx, "Power amp", {{0, "Eco"}, {1, "HQ"}, {2, "Ultra"}}, [this] { return st.get("amp.quality"); },
                     [this](const var& v) {
                         st.set("amp.quality", v);
                         act.sendAmp();
                         ctx.syncAll();
                     }),
                 Seg(
                     "Cores", {{0, "Auto"}, {1, "1"}, {2, "2"}, {4, "4"}, {8, "8"}, {16, "16"}}, "threads",
                     [this] { act.sendThreads(); }, 0)),
            text("Running out of headroom? Raise the buffer, lower the reflections or set the power amp to HQ or "
                 "Eco.")));
    }
};

} // namespace wh
