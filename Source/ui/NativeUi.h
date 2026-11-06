#pragma once
// Wheelhouse UI - the native user interface (JUCE components), shared by the standalone app and the plugin
// editor. It talks to the engine through UiHooks (same messages as before) and receives the engine's events
// through emitEventIfBrowserIsVisible(name, payload) - the name is kept from the former WebView host so the hosts'
// code did not change.
#include "Header.h"

namespace wh {

/** What the host provides. */
struct UiHooks {
    std::function<void(const var&)> dspMessage;      // one UI message for WheelhouseEngine::handleMessage
    std::function<void()> audioSettings, audioInfo;  // open the device dialog / send the "audio" event
    std::function<void(const String&)> saveUi;       // store the UI state (file or project)
    std::function<String()> loadUi;                  // stored UI state
    std::function<void(int, bool)> chantest;         // test tone on one output (LFE: low tone)
    std::function<var()> params;                     // plugin: {id: value} of the host parameters
    std::function<bool(rotary::PathViz&)> readPaths; // engine's room paths for the floor plan
};

class NativeUi : public Component, private juce::Timer {
public:
    explicit NativeUi(UiHooks h) : hooks(std::move(h)), act(ctx), cards(ctx, act), notes(ctx), stage(*this) {
        juce::LookAndFeel::setDefaultLookAndFeel(&look);
        setLookAndFeel(&look);
        ctx.sendRaw = [this](const var& m) {
            if (hooks.dspMessage) hooks.dspMessage(m);
        };
        ctx.stateChanged = [this] {
            saveCountdown = 24;
        }; // ~0.8 s at 30 Hz
        ctx.invoke = [this](const String& what) {
            if (what == "audioSettings" && hooks.audioSettings)
                hooks.audioSettings();
            else if (what == "audioInfo" && hooks.audioInfo)
                hooks.audioInfo();
            else if (what.startsWith("chantest:") && hooks.chantest) {
                auto p = juce::StringArray::fromTokens(what, ":", "");
                hooks.chantest(p[1].getIntValue(), p[2] == "1");
            }
        };
        cards.releasePc = [this] {
            releasePc();
        };
        notes.changed = [this] {
            stage.upper.repaint();
            stage.lower.repaint();
            stage.pedals.repaint();
        };
        stage.build();
        addAndMakeVisible(stage);
        tooltips.toFront(false);
        ctx.onSync = [this] {
            stage.afterSync();
        };
        setWantsKeyboardFocus(true);
        startTimerHz(30);
    }
    /** Loads the saved state and pushes everything to the engine. Runs on the first timer tick (the host's pointer
        to this UI is set by then); a host may call it earlier. */
    void startIfNeeded() {
        if (started) return;
        started = true;
        start();
    }
    ~NativeUi() override {
        stopTimer();
        flushSave();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }

    /** Events from the engine (rot, roomInfo, param, audio, resend, midi). */
    void emitEventIfBrowserIsVisible(const juce::Identifier& name, const var& m) {
        const String n = name.toString();
        if (n == "rot")
            onRot(m);
        else if (n == "roomInfo") {
            ctx.st.roomInfo = m;
            ctx.syncAll();
        } else if (n == "param") {
            act.applyParam(m["id"].toString(), (double) m["v"]);
            ctx.syncAll();
            saveCountdown = 24;
        } else if (n == "audio") {
            if (auto* src = m.getDynamicObject())
                for (auto& p : src->getProperties()) ctx.st.audioInfo.getDynamicObject()->setProperty(p.name, p.value);
            act.updateChanInfo();
            stage.header.showCpu = !act.isPlugin();
            stage.header.resized();
            ctx.syncAll();
        } else if (n == "resend")
            act.pushAll();
        else if (n == "midi")
            onMidi(m);
    }

    /** Opens a tab: "spielen" (play), "orgel", "rotary", "raum", "mikros", "ausgabe", "midi", "audio". */
    void setTab(const String& t) { stage.setTab(t); }

    void resized() override {
        // fixed 1600 x 900 design, scaled to the window and centred
        const float sc = std::min(getWidth() / 1600.f, getHeight() / 900.f);
        stage.setBounds(0, 0, 1600, 900);
        stage.setTransform(juce::AffineTransform::scale(sc).translated(std::round((getWidth() - 1600 * sc) / 2),
                                                                       std::round((getHeight() - 900 * sc) / 2)));
    }
    void paint(Graphics& g) override { g.fillAll(col::bg); }

    // ---------------- computer keyboard: QWERTY manual, space = Rotary, Esc = panic ----------------
    bool keyPressed(const juce::KeyPress& k) override {
        if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;
        if (k.getKeyCode() == juce::KeyPress::spaceKey) {
            act.toggleRotary();
            return true;
        }
        if (k.getKeyCode() == juce::KeyPress::escapeKey) {
            notes.panic();
            if (stage.tab != "spielen") stage.setTab("spielen");
            return true;
        }
        const int off = keyOffset(k.getTextCharacter());
        if (off < 0) return false;
        const int code = k.getKeyCode();
        if (pcHeld.count(code)) return true; // key repeat
        const int m = (int) ctx.st.num("pcMan");
        const int note = (m == 2 ? 0 : 12) + (int) ctx.st.num("octave") * 12 + off;
        pcHeld[code] = {note, m};
        notes.on(note, m);
        return true;
    }
    bool keyStateChanged(bool isKeyDown) override {
        if (isKeyDown) return false;
        for (auto it = pcHeld.begin(); it != pcHeld.end();)
            if (!juce::KeyPress::isKeyCurrentlyDown(it->first)) {
                notes.off(it->second.first, it->second.second);
                it = pcHeld.erase(it);
            } else
                ++it;
        return false;
    }
    void focusLost(FocusChangeType) override { releasePc(); }

private:
    // ---------------- the stage: header, cabinet (play surface + settings card), manuals ----------------
    struct Stage : Component {
        explicit Stage(NativeUi& o)
            : ui(o), header(
                         o.ctx, o.act, [this](const String& t) { setTab(t); }, [&o] { o.notes.panic(); }),
              rotaryPanel(o.ctx, o.act, o.clock), organPanel(o.ctx, o.act, [&o] { o.ctx.syncAll(); }),
              roomPanel(o.ctx, o.act, o.clock, [this] { setTab("raum"); }), bigPlan(o.ctx, o.act, o.clock, true),
              strip(o.ctx, o.act, [&o] { o.releasePc(); }), upper(o.ctx, o.act, o.notes, 0),
              lower(o.ctx, o.act, o.notes, 1), pedals(o.ctx, o.act, o.notes), swell(o.ctx), ov0(o.ctx, o.act, 0),
              ov1(o.ctx, o.act, 1), ov2(o.ctx, o.act, 2), resetBtn("Reset"), closeBtn("Back to Play", true, 14) {}
        NativeUi& ui;
        Header header;
        RotaryPanel rotaryPanel;
        OrganPanel organPanel;
        RoomPanel roomPanel;
        FloorPlan bigPlan;
        KeyStrip strip;
        Manual upper, lower;
        Pedalboard pedals;
        SwellPedal swell;
        OctaveBox ov0, ov1, ov2;
        Button resetBtn, closeBtn;
        struct CardBox : Component {
            void paint(Graphics& g) override {
                auto r = getLocalBounds().toFloat();
                juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 30, {0, 12})
                    .drawForRectangle(g, r.toNearestInt());
                g.setColour(col::panel);
                g.fillRoundedRectangle(r, 6);
                g.setGradientFill(juce::ColourGradient(Colour(0xffd9b56a), 0, 0, Colour(0xff8a6a30), 0, 6, false));
                g.fillRect(r.withHeight(6).reduced(3, 0));
                g.setColour(col::segLine);
                g.fillRect(0.f, 52.f, r.getWidth(), 1.f);
                g.setColour(col::text);
                drawSpaced(g, title, {18, 6, 400, 46}, Fonts::make(18, 700), 2.4f, juce::Justification::centredLeft,
                           true);
                g.setColour(col::muted);
                g.setFont(Fonts::make(13.5f));
                const float tw = spacedWidth(title, Fonts::make(18, 700), 2.4f, true);
                g.drawText(sub, Rectangle<float>(18 + tw + 16, 6, 500, 46), juce::Justification::centredLeft, false);
                if (stats.isNotEmpty()) {
                    g.setColour(col::muted);
                    g.setFont(Fonts::make(13));
                    g.drawText(stats, statsRect, juce::Justification::centredLeft, false);
                }
            }
            String title, sub, stats;
            Rectangle<float> statsRect;
        } card;
        juce::Viewport viewport;
        std::map<String, std::unique_ptr<Page>> pages;
        String tab = "spielen";

        void build() {
            for (auto* c : std::initializer_list<Component*>{&header, &rotaryPanel, &organPanel, &roomPanel, &strip,
                                                             &upper, &lower, &pedals, &swell, &ov0, &ov1, &ov2})
                addAndMakeVisible(c);
            addChildComponent(card);
            card.addAndMakeVisible(resetBtn);
            card.addAndMakeVisible(closeBtn);
            card.addAndMakeVisible(viewport);
            card.addChildComponent(bigPlan);
            resetBtn.setTooltip("Reset everything on this page to its defaults.");
            resetBtn.onClick = [this] {
                resetCard();
            };
            closeBtn.onClick = [this] {
                setTab("spielen");
            };
            viewport.setScrollBarsShown(true, false);
            viewport.setScrollBarThickness(8);
            for (String id : {"orgel", "rotary", "raum", "mikros", "ausgabe", "midi", "audio"})
                pages[id] = ui.cards.build(id);
            roomPanel.plan.readPaths = [this](rotary::PathViz& v) {
                return ui.hooks.readPaths && ui.hooks.readPaths(v);
            };
            bigPlan.readPaths = roomPanel.plan.readPaths;
        }
        void setTab(const String& id) {
            tab = id;
            header.current = id;
            static const std::map<String, std::pair<const char*, const char*>> info{
                {"orgel", {"Organ", "Tone generator, contacts, preamp, spring reverb"}},
                {"rotary", {"Rotary", "Rotors, power amp, cabinet and placement"}},
                {"raum", {"Room", "Size, surfaces and reflections"}},
                {"mikros", {"Mics", "Close mics, room pair and mixer"}},
                {"ausgabe", {"Output", "Format, routing, monitoring"}},
                {"midi", {"MIDI", "Channels, split, controllers, QWERTY keys"}},
                {"audio", {"Audio", "Device, driver and latency"}}};
            auto it = info.find(id);
            card.setVisible(it != info.end());
            if (it != info.end()) {
                card.title = it->second.first;
                card.sub = it->second.second;
                viewport.setViewedComponent(pages[id].get(), false);
                bigPlan.setVisible(id == "raum");
            } else
                viewport.setViewedComponent(nullptr, false);
            if ((id == "audio" || id == "ausgabe" || id == "midi") && ui.hooks.audioInfo) ui.hooks.audioInfo();
            layoutCard();
            ui.ctx.syncAll();
            ui.grabKeyboardFocus();
        }
        void resetCard() {
            for (auto* c : ui.ctx.cardCtl[tab]) c->reset();
            if (tab == "raum") {
                ui.act.clampPos();
                ui.act.sendRoomAll();
                ui.act.sendPos(true);
            }
            if (tab == "ausgabe") {
                ui.ctx.st.set("chmap", juce::JSON::parse(R"({"stereo": null, "quad": null, "s51": null})"));
                ui.act.applyMode();
            }
            ui.ctx.syncAll();
        }
        void afterSync() {
            if (card.isVisible()) {
                if (tab == "raum" && ui.ctx.st.roomInfo.isObject()) {
                    const var ri = ui.ctx.st.roomInfo;
                    card.stats = L"RT60 low / mid / high  " + nf((double) at(ri["rt60"], 0)) + " / " +
                                 nf((double) at(ri["rt60"], 1)) + " / " + nf((double) at(ri["rt60"], 2)) +
                                 L" s     Critical distance  " + nf((double) ri["rc"]) + L" m     Paths  " +
                                 String((int) ri["paths"]) + L"     CPU  " + ui.ctx.st.cpuText;
                } else
                    card.stats = {};
                layoutCard();
                card.repaint();
            }
            header.sync();
        }
        void layoutCard() {
            if (!card.isVisible()) return;
            auto r = card.getLocalBounds();
            auto head = r.removeFromTop(53).withTrimmedTop(6).reduced(18, 0);
            closeBtn.setBounds(
                head.removeFromRight(closeBtn.naturalWidth()).withSizeKeepingCentre(closeBtn.naturalWidth(), 32));
            head.removeFromRight(16);
            resetBtn.setBounds(
                head.removeFromRight(resetBtn.naturalWidth()).withSizeKeepingCentre(resetBtn.naturalWidth(), 30));
            auto body = r.reduced(18, 14);
            if (tab == "raum") {
                auto side = body.removeFromLeft(620);
                body.removeFromLeft(14);
                card.statsRect = side.removeFromBottom(18).toFloat();
                side.removeFromBottom(8);
                bigPlan.setBounds(side);
            }
            viewport.setBounds(body);
            if (auto* p = pages.count(tab) ? pages[tab].get() : nullptr) {
                const int w = body.getWidth() - (p->layoutHeight > body.getHeight() ? 12 : 0);
                p->setSize(w, 10);
                p->resized();
                p->setSize(w, std::max(body.getHeight(), p->layoutHeight));
            }
        }
        void paint(Graphics& g) override {
            g.fillAll(col::bg);
            drawWood(g, caseRect);
            // manuals area: wood without rounded top corners
            Rectangle<float> keys = keysRect;
            g.setGradientFill(juce::ColourGradient(Colour(0xff4a2812), 0, keys.getY(), Colour(0xff351c0b), 0,
                                                   keys.getBottom(), false));
            g.fillRoundedRectangle(keys, 10);
            {
                Graphics::ScopedSaveState s(g);
                g.reduceClipRegion(keys.toNearestInt());
                for (float x = keys.getX() - 20; x < keys.getRight() + 20; x += 16) {
                    g.setColour(juce::Colours::black.withAlpha(0.1f));
                    g.drawLine(x + 3, keys.getY(), x + 3 + keys.getHeight() * 0.035f, keys.getBottom(), 2.5f);
                    g.setColour(Colour(0x0affd2a0));
                    g.drawLine(x + 11, keys.getY(), x + 11 + keys.getHeight() * 0.035f, keys.getBottom(), 2.0f);
                }
            }
            // felt strips and manual labels
            for (auto* m : {&upper, &lower}) {
                g.setColour(Colour(0xff7a1c16));
                g.fillRect(keys.getX() + 48, (float) m->getY() - 4, keys.getWidth() - 96, 4.f);
            }
            g.setColour(Colour(0xffe3cfae));
            auto vlab = [&](const String& t, Rectangle<float> r) {
                Graphics::ScopedSaveState s(g);
                g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, r.getCentreX(),
                                                               r.getCentreY()));
                auto rr = Rectangle<float>(r.getHeight(), r.getWidth()).withCentre(r.getCentre());
                drawSpaced(g, t, rr, Fonts::make(11.5f, 700), 2, juce::Justification::centred, true);
            };
            vlab("Upper", {(float) upper.getX() - 32, (float) upper.getY(), 22, (float) upper.getHeight()});
            vlab("Lower", {(float) lower.getX() - 32, (float) lower.getY(), 22, (float) lower.getHeight()});
            vlab("Pedal", {(float) pedals.getX() - 36, (float) pedals.getY(), 22, (float) pedals.getHeight()});
        }
        Rectangle<float> caseRect, keysRect;
        void resized() override {
            // #stage: padding 0 16 8, gap 8; header 52; case 440; keys fill the rest
            header.setBounds(16, 0, 1600 - 32, 52);
            caseRect = {16, 60, 1600 - 32, 440};
            auto play = caseRect.reduced(14).toNearestInt();
            rotaryPanel.setBounds(play.removeFromLeft(330));
            play.removeFromLeft(14);
            roomPanel.setBounds(play.removeFromRight(400));
            play.removeFromRight(14);
            organPanel.setBounds(play);
            card.setBounds(caseRect.reduced(14).toNearestInt());
            layoutCard();
            keysRect = {16, 508, 1600 - 32, 900 - 8 - 508};
            auto k = keysRect.toNearestInt().withTrimmedLeft(16).withTrimmedRight(16);
            strip.setBounds(k.removeFromTop(30));
            const int kbX = (1600 - 1440) / 2 + 6; // manual centred between its label and octave box
            auto manRow = [&](Manual& m, OctaveBox& ov, int y) {
                m.setBounds(kbX, y + 4, 1440, 118);
                ov.setBounds(kbX + 1440 + 10, y + 4, 22, 118);
            };
            manRow(upper, ov0, k.getY());
            manRow(lower, ov1, k.getY() + 122 + 6);
            const int py = k.getY() + 2 * 122 + 6 + 6;
            const int ph = std::min(86, (int) keysRect.getBottom() - 8 - py);
            const int pw = 710, total = 22 + 14 + pw + 14 + 60 + 14 + 22;
            int px = (1600 - total) / 2 + 22 + 14;
            pedals.setBounds(px, py, pw, ph);
            px += pw + 14;
            swell.setBounds(px, py, 60, ph);
            px += 60 + 14;
            ov2.setBounds(px, py, 22, ph);
        }
    };

    void start() {
        bool loaded = false;
        if (hooks.loadUi) loaded = ctx.st.load(hooks.loadUi());
        if (hooks.params) {
            const var pv = hooks.params();
            if (auto* o = pv.getDynamicObject()) {
                if (!loaded) {
                    ctx.muteSend = true;
                    act.applyInstrument(0);
                    ctx.muteSend = false;
                }
                loaded = true;
                for (auto& p : o->getProperties()) act.applyParam(p.name.toString(), (double) p.value);
            }
        }
        if (!loaded) act.applyInstrument(0); // first start: B3 + 122 as in the original
        if (ctx.st.str("rotary") == "stop" && ctx.st.str("halfMoon") != "stop") ctx.st.set("rotary", "slow");
        stage.setTab("spielen");
        act.pushAll();
        ctx.ready = true;
        if (hooks.audioInfo) hooks.audioInfo();
        ctx.syncAll();
    }
    void onRot(const var& m) {
        clock.report((double) m["horn"], (double) m["drum"], (double) m["hornHz"], (double) m["drumHz"]);
        if (auto* a = m["meters"].getArray()) stage.header.updateMeters(*a);
        if (m.hasProperty("cpu")) ctx.st.cpuText = String(juce::roundToInt((double) m["cpu"] * 100)) + "%";
        if (m.hasProperty("vb"))
            ctx.st.ampText = String(juce::roundToInt((double) m["vb"])) + " V / " + nf((double) m["vk"]) + " V";
        if (auto* mm = m["mm"].getArray()) {
            ctx.st.micMeters = *mm;
            if (stage.tab == "mikros" && cards.desk) cards.desk->setMeters(*mm);
        }
        if (m.hasProperty("procLatency")) ctx.st.procLatency = (int) m["procLatency"];
        if (m.hasProperty("dropped")) ctx.st.droppedBlocks = (int) m["dropped"];
        if (stage.tab != "spielen" && ++slowSync % 10 == 0) ctx.syncAll();
    }
    void onMidi(const var& m) {
        const String t = m["type"].toString();
        if (act.learnTarget.isNotEmpty() && t == "noteOn" && m.hasProperty("note")) {
            act.setSplit(act.learnTarget, (int) m["note"]);
            act.learnTarget = {};
            ctx.syncAll();
        }
        if (t == "noteOn" || t == "noteOff")
            notes.shown((int) m["man"], (int) m["key"], t == "noteOn");
        else if (t == "rotary") {
            ctx.st.set("rotary", m["v"]);
            ctx.syncAll();
        } else if (t == "volume") {
            ctx.st.set("volume", m["v"]);
            ctx.syncAll();
        } else if (t == "panic")
            notes.clearShown();
    }
    void timerCallback() override {
        startIfNeeded();
        // animation: cabinet and floor plan follow the rotors
        stage.rotaryPanel.repaint();
        if (stage.tab == "raum")
            stage.bigPlan.repaint();
        else if (stage.tab == "spielen")
            stage.roomPanel.plan.repaint();
        if (saveCountdown > 0 && --saveCountdown == 0) flushSave();
    }
    void flushSave() {
        if (hooks.saveUi && ctx.ready) hooks.saveUi(ctx.st.save());
        saveCountdown = 0;
    }
    void releasePc() {
        for (auto& [code, nm] : pcHeld) notes.off(nm.first, nm.second);
        pcHeld.clear();
    }
    static int keyOffset(juce::juce_wchar c) {
        static const std::map<juce::juce_wchar, int> map{
            {'z', 0},  {'s', 1},  {'x', 2},  {'d', 3},  {'c', 4},  {'v', 5},  {'g', 6},  {'b', 7},
            {'h', 8},  {'n', 9},  {'j', 10}, {'m', 11}, {',', 12}, {'l', 13}, {'.', 14}, {'q', 12},
            {'2', 13}, {'w', 14}, {'3', 15}, {'e', 16}, {'r', 17}, {'5', 18}, {'t', 19}, {'6', 20},
            {'y', 21}, {'7', 22}, {'u', 23}, {'i', 24}, {'9', 25}, {'o', 26}, {'0', 27}, {'p', 28}};
        auto it = map.find(juce::CharacterFunctions::toLowerCase(c));
        return it != map.end() ? it->second : -1;
    }

    UiHooks hooks;
    WhLook look;
    Ctx ctx;
    Actions act;
    Cards cards;
    Notes notes;
    RotorClock clock;
    juce::TooltipWindow tooltips{this, 450};
    Stage stage;
    std::map<int, std::pair<int, int>> pcHeld;
    int saveCountdown = 0, slowSync = 0;
    bool started = false;
};

} // namespace wh
