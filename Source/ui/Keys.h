#pragma once
// Wheelhouse UI - manuals, pedalboard, swell pedal, octave buttons and the strip above the keys (swell bar, MIDI
// channels, split, QWERTY manual, octave). Mouse play with velocity from where the key is hit, glissando.
#include "PlaySurface.h"
#include <set>

namespace wh {

/** Notes played from the UI (mouse, computer keyboard) and shown from MIDI. */
struct Notes {
    Ctx& ctx;
    std::set<int> active[3];
    std::function<void()> changed;
    explicit Notes(Ctx& c) : ctx(c) {}
    void on(int k, int man, int vel = 100) {
        const int max = man == 2 ? 24 : 60;
        if (k < 0 || k > max || active[man].count(k)) return;
        active[man].insert(k);
        ctx.send(Ctx::obj({{"type", "noteOn"}, {"key", k}, {"man", man}, {"vel", vel}}));
        if (changed) changed();
    }
    void off(int k, int man) {
        if (!active[man].count(k)) return;
        active[man].erase(k);
        ctx.send(Ctx::obj({{"type", "noteOff"}, {"key", k}, {"man", man}}));
        if (changed) changed();
    }
    void panic() {
        for (int m = 0; m < 3; m++)
            for (int k : std::set<int>(active[m])) off(k, m);
        ctx.send(Ctx::obj({{"type", "panic"}}));
    }
    /** MIDI shown on the keys (the engine plays it already). */
    void shown(int man, int k, bool isOn) {
        if (man < 0 || man > 2) return;
        if (isOn)
            active[man].insert(k);
        else
            active[man].erase(k);
        if (changed) changed();
    }
    void clearShown() {
        for (auto& a : active) a.clear();
        if (changed) changed();
    }
};

/** Shared mouse logic for keys: velocity from the hit position, glissando while dragging. */
class KeysBase : public Component, public Syncable {
public:
    KeysBase(Ctx& c, Actions& a, Notes& n, int manual) : ctx(c), act(a), notes(n), man(manual) { ctx.reg(this); }
    ~KeysBase() override { ctx.unreg(this); }
    void sync() override { repaint(); }
    virtual int keyAt(Point<float> p, Rectangle<float>* bounds) const = 0;
    void mouseDown(const juce::MouseEvent& e) override {
        Rectangle<float> kb;
        const int k = keyAt(e.position, &kb);
        if (k < 0) return;
        vel = juce::roundToInt(35 + 92 * juce::jlimit(0.f, 1.f, (e.position.y - kb.getY()) / kb.getHeight()));
        held = k;
        notes.on(k, man, vel);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (held == -2) return;
        const int k = keyAt(e.position, nullptr);
        if (k != held) {
            if (held >= 0) notes.off(held, man);
            if (k >= 0) notes.on(k, man, vel);
            held = k;
        }
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (held >= 0) notes.off(held, man);
        held = -2;
    }

protected:
    /** Dimmed (outside the split window) and edge markers. */
    bool isOut(int k) const {
        const auto w = act.reach(man);
        return w.first >= 0 && (k < w.first || k > w.second);
    }
    Ctx& ctx;
    Actions& act;
    Notes& notes;
    int man;
    int held = -2, vel = 100;
};

class Manual : public KeysBase {
public:
    using KeysBase::KeysBase;
    static constexpr int KEYS = 61, WHITE_W = 40;
    void paint(Graphics& g) override {
        const float H = (float) getHeight();
        const auto w = act.reach(man);
        int firstW = -1, lastW = -1;
        if (w.first >= 0)
            for (int k = w.first; k <= w.second; k++)
                if (!Actions::isBlack(k)) {
                    if (firstW < 0) firstW = k;
                    lastW = k;
                }
        int wi = 0;
        for (int k = 0; k < KEYS; k++) {
            if (Actions::isBlack(k)) continue;
            auto r = Rectangle<float>((float) wi * WHITE_W, 0, WHITE_W - 1, H);
            wi++;
            const bool on = notes.active[man].count(k) > 0;
            juce::DropShadow(juce::Colours::black.withAlpha(0.35f), 4, {0, 3}).drawForRectangle(g, r.toNearestInt());
            g.setGradientFill(juce::ColourGradient(Colour(on ? 0xffffe0a6 : 0xfff6f1e6), 0, 0,
                                                   Colour(on ? 0xfff2b257 : 0xffddd3c2), 0, H, false));
            Path kp;
            kp.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4, 4, false, false, true, true);
            g.fillPath(kp);
            g.setColour(juce::Colours::black.withAlpha(0.1f));
            g.fillRect(r.withTop(H - 5).reduced(1, 0));
            g.setColour(Colour(0xff8d8170));
            g.strokePath(kp, juce::PathStrokeType(1));
            if (k % 12 == 0) {
                g.setColour(Colour(0xff6b604f));
                g.setFont(Fonts::make(10.5f));
                g.drawText("C" + String(k / 12 + 2), r.withTrimmedBottom(4), juce::Justification::centredBottom, false);
            }
            if (w.first >= 0 && k == firstW && w.first > 0) {
                g.setColour(col::amber);
                g.fillRect(r.withWidth(3));
            }
            if (w.first >= 0 && k == lastW && w.second < KEYS - 1) {
                g.setColour(col::amber);
                g.fillRect(r.withLeft(r.getRight() - 3));
            }
            if (isOut(k)) {
                g.setColour(Colour(0xff1d140a).withAlpha(0.55f));
                g.fillPath(kp);
            }
        }
        wi = 0;
        for (int k = 0; k < KEYS; k++) {
            if (!Actions::isBlack(k)) {
                wi++;
                continue;
            }
            auto r = Rectangle<float>((float) wi * WHITE_W - 12, 0, 24, H * 0.6f);
            const bool on = notes.active[man].count(k) > 0;
            juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 5, {0, 4}).drawForRectangle(g, r.toNearestInt());
            g.setGradientFill(juce::ColourGradient(Colour(on ? 0xffb5731e : 0xff2a251f), 0, 0,
                                                   Colour(on ? 0xff7a4a10 : 0xff0e0c0a), 0, r.getBottom(), false));
            Path kp;
            kp.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4, 4, false, false, true, true);
            g.fillPath(kp);
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRect(r.withTop(r.getBottom() - 4).reduced(2, 0));
            if (isOut(k)) {
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillPath(kp);
            }
        }
    }
    int keyAt(Point<float> p, Rectangle<float>* bounds) const override {
        const float H = (float) getHeight();
        if (p.y < 0 || p.y > H) return -1;
        int wi = 0;
        if (p.y < H * 0.6f)
            for (int k = 0; k < KEYS; k++) {
                if (!Actions::isBlack(k)) {
                    wi++;
                    continue;
                }
                auto r = Rectangle<float>((float) wi * WHITE_W - 12, 0, 24, H * 0.6f);
                if (r.contains(p)) {
                    if (bounds) *bounds = r;
                    return k;
                }
            }
        wi = 0;
        for (int k = 0; k < KEYS; k++) {
            if (Actions::isBlack(k)) continue;
            auto r = Rectangle<float>((float) wi * WHITE_W, 0, WHITE_W, H);
            wi++;
            if (r.contains(p)) {
                if (bounds) *bounds = r;
                return k;
            }
        }
        return -1;
    }
};

/** 25-note pedalboard, drawn in the SVG's 760 x 92 units. */
class Pedalboard : public KeysBase, public juce::SettableTooltipClient {
public:
    Pedalboard(Ctx& c, Actions& a, Notes& n) : KeysBase(c, a, n, 2) {
        setTooltip("25-note pedalboard: click to play, or use the pedal MIDI channel.");
        int ni = 0;
        for (int p = 0; p <= 24; p++) {
            const float natX = 43.f + ni * 46.f;
            if (Actions::isBlack(p))
                keys.push_back({p, natX - 8, false});
            else {
                keys.push_back({p, natX, true});
                ni++;
            }
        }
    }
    void paint(Graphics& g) override {
        Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::scale(getWidth() / 760.f, getHeight() / 92.f));
        g.setColour(Colour(0xff140a04));
        g.fillRoundedRectangle(22, 4, 716, 86, 3);
        g.setGradientFill(juce::ColourGradient(Colour(0xff5a321b), 0, 0, Colour(0xff2e170a), 0, 9, false));
        g.fillRoundedRectangle(22, 0, 716, 9, 2);
        for (auto& k : keys) {
            if (!k.nat) continue;
            const bool on = notes.active[2].count(k.p) > 0;
            const float y = on ? 9.f : 7.f;
            juce::ColourGradient cg(Colour(on ? 0xff9a6520 : 0xff8f7448), k.x, 0, Colour(on ? 0xff9a6520 : 0xff8f7448),
                                    k.x + 30, 0, false);
            if (on)
                cg.addColour(0.5, Colour(0xfff2b257));
            else {
                cg.addColour(0.18, Colour(0xffd9c398));
                cg.addColour(0.5, Colour(0xffe8d7b0));
                cg.addColour(0.82, Colour(0xffd9c398));
            }
            g.setGradientFill(cg);
            g.fillRoundedRectangle(k.x, y, 30, 74, 3);
            g.setColour(Colour(0xff4a3820));
            g.drawRoundedRectangle(k.x, y, 30, 74, 3, 0.8f);
            g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.18f), 0, y,
                                                   juce::Colours::transparentBlack, 0, y + 22, false));
            g.fillRoundedRectangle(k.x, y, 30, 74, 3);
            g.setColour(Colour(on ? 0xff8a5a1c : 0xff6e5636));
            g.fillRoundedRectangle(k.x, on ? 83.f : 81.f, 30, 7, 2);
            if (isOut(k.p)) {
                g.setColour(Colour(0xff140a04).withAlpha(0.58f));
                g.fillRoundedRectangle(k.x, y, 30, 83, 3);
            }
        }
        for (auto& k : keys) {
            if (k.nat) continue;
            const bool on = notes.active[2].count(k.p) > 0;
            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.fillRoundedRectangle(k.x - 9, 10, 24, 44, 3);
            juce::ColourGradient cg(Colour(on ? 0xff5a3a10 : 0xff0c0a09), k.x - 11, 0,
                                    Colour(on ? 0xff5a3a10 : 0xff1a1714), k.x + 11, 0, false);
            if (on)
                cg.addColour(0.5, Colour(0xffb5731e));
            else {
                cg.addColour(0.35, Colour(0xff3a342e));
                cg.addColour(0.65, Colour(0xff2a2521));
            }
            g.setGradientFill(cg);
            g.fillRoundedRectangle(k.x - 11, 6, 22, 36, 3);
            g.setColour(juce::Colours::black);
            g.drawRoundedRectangle(k.x - 11, 6, 22, 36, 3, 0.8f);
            g.setColour(Colour(0xff4a423a));
            g.fillRoundedRectangle(k.x - 11, 40, 22, 8, 2);
            if (isOut(k.p)) {
                g.setColour(Colour(0xff140a04).withAlpha(0.58f));
                g.fillRoundedRectangle(k.x - 11, 6, 22, 42, 3);
            }
        }
    }
    int keyAt(Point<float> p, Rectangle<float>* bounds) const override {
        const Point<float> q(p.x * 760.f / getWidth(), p.y * 92.f / getHeight());
        for (auto& k : keys)
            if (!k.nat && Rectangle<float>(k.x - 11, 6, 22, 42).contains(q)) {
                if (bounds) *bounds = Rectangle<float>(k.x - 11, 6, 22, 42).transformedBy(toView());
                return k.p;
            }
        for (auto& k : keys)
            if (k.nat && Rectangle<float>(k.x, 7, 30, 83).contains(q)) {
                if (bounds) *bounds = Rectangle<float>(k.x, 7, 30, 83).transformedBy(toView());
                return k.p;
            }
        return -1;
    }

private:
    juce::AffineTransform toView() const {
        return juce::AffineTransform::scale(getWidth() / 760.f, getHeight() / 92.f);
    }
    struct PKey {
        int p;
        float x;
        bool nat;
    };
    std::vector<PKey> keys;
};

class SwellPedal : public Component, public Syncable, public juce::SettableTooltipClient {
public:
    explicit SwellPedal(Ctx& c) : ctx(c) {
        ctx.reg(this);
        setTooltip("Swell pedal: drag or scroll.");
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    ~SwellPedal() override { ctx.unreg(this); }
    void sync() override { repaint(); }
    void paint(Graphics& g) override {
        Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::scale(getWidth() / 64.f, getHeight() / 92.f));
        Path outer;
        outer.startNewSubPath(10, 4);
        outer.lineTo(54, 4);
        outer.lineTo(60, 86);
        outer.lineTo(4, 86);
        outer.closeSubPath();
        g.setColour(Colour(0xff1a1512));
        g.fillPath(outer);
        g.setColour(Colour(0xff3a312a));
        g.strokePath(outer, juce::PathStrokeType(1.2f));
        Path inner;
        inner.startNewSubPath(14, 10);
        inner.lineTo(50, 10);
        inner.lineTo(54, 78);
        inner.lineTo(10, 78);
        inner.closeSubPath();
        g.setColour(Colour(0xff26201c));
        g.fillPath(inner);
        g.setColour(Colour(0xff3e352e));
        const float ribs[][3] = {{16, 48, 16}, {15, 49, 24}, {15, 49, 32}, {14, 50, 40},
                                 {13, 51, 48}, {13, 51, 56}, {12, 52, 64}, {12, 52, 72}};
        for (auto& r : ribs) g.drawLine(r[0], r[2], r[1], r[2], 3);
        g.setColour(Colour(0xff0d0a08));
        g.fillRoundedRectangle(4, 86, 56, 5, 1.5f);
        g.setColour(Colour(0xff120f0c));
        g.fillRoundedRectangle(58, 18, 4, 62, 2);
        const float hh = 62 * (float) ctx.st.num("volume");
        g.setColour(col::amber);
        g.fillRoundedRectangle(58, 80 - hh, 4, hh, 2);
    }
    void mouseDown(const juce::MouseEvent& e) override { setFrom(e.position.y); }
    void mouseDrag(const juce::MouseEvent& e) override { setFrom(e.position.y); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (w.deltaY == 0) return;
        set(clampd(ctx.st.num("volume") + (w.deltaY > 0 ? 0.03 : -0.03), 0, 1));
    }

private:
    void setFrom(float y) { set(std::round(clampd(1 - (y / getHeight() - 0.18) / 0.7, 0, 1) * 1000) / 1000); }
    void set(double v) {
        ctx.st.set("volume", v);
        ctx.sendV("volume", v);
        ctx.syncAll();
    }
    Ctx& ctx;
};

/** OCT label, +, value, − to the right of each manual. */
class OctaveBox : public Component, public Syncable, public juce::TooltipClient {
public:
    OctaveBox(Ctx& c, Actions& a, int manual) : ctx(c), act(a), man(manual) { ctx.reg(this); }
    ~OctaveBox() override { ctx.unreg(this); }
    void sync() override { repaint(); }
    void paint(Graphics& g) override {
        layout();
        g.setColour(col::muted);
        drawSpaced(g, "OCT", lab, Fonts::make(9), 1, juce::Justification::centred);
        for (auto* b : {&up, &dn}) {
            g.setColour(Colour(0xff2a170a));
            g.fillRoundedRectangle(*b, 5);
            g.setColour(Colour(0xff6b4426));
            g.drawRoundedRectangle(b->reduced(0.5f), 5, 1);
        }
        g.setColour(Colour(0xffe3cfae));
        g.setFont(Fonts::make(13));
        g.drawText("+", up, juce::Justification::centred, false);
        g.drawText(L"−", dn, juce::Justification::centred, false);
        const int o = (int) ctx.st.num("split.oct." + String(man));
        g.setColour(o != 0 ? col::amber : Colour(0xffe3cfae));
        g.setFont(Fonts::make(12.5f, 700));
        g.drawText(String(o > 0   ? L"+"
                          : o < 0 ? L"−"
                                  : L"") +
                       String(std::abs(o)),
                   val, juce::Justification::centred, false);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasClicked()) return;
        if (up.contains(e.position))
            act.setManOct(man, 1);
        else if (dn.contains(e.position))
            act.setManOct(man, -1);
    }
    String getTooltip() override {
        static const char* names[] = {"Upper manual", "Lower manual", "Pedal"};
        const String n = names[man];
        auto p = getMouseXYRelative().toFloat();
        if (up.contains(p)) return n + " one octave up";
        if (dn.contains(p)) return n + " one octave down";
        return L"Octave shift of the " + n.toLowerCase() + L" (±3)";
    }

private:
    void layout() {
        const float cy = getHeight() / 2.f;
        lab = {0, cy - 44, 22, 12};
        up = {0, cy - 30, 22, 22};
        val = {0, cy - 6, 22, 15};
        dn = {0, cy + 12, 22, 22};
    }
    Ctx& ctx;
    Actions& act;
    int man;
    Rectangle<float> lab, up, val, dn;
};

/** The strip above the manuals: swell bar, MIDI channels, split, devices, QWERTY manual and octave. */
class KeyStrip : public Component, public Syncable, public juce::TooltipClient {
public:
    KeyStrip(Ctx& c, Actions& a, std::function<void()> releasePcKeys)
        : ctx(c), act(a), releasePc(std::move(releasePcKeys)), chipLo(c, a, "lo", "Lower", false),
          chipHi(c, a, "hi", "Upper", false) {
        ctx.reg(this);
        addChildComponent(chipLo);
        addChildComponent(chipHi);
    }
    ~KeyStrip() override { ctx.unreg(this); }
    void sync() override {
        const int mode = (int) ctx.st.num("split.mode");
        chipHi.setVisible(mode >= 1);
        chipLo.setVisible(mode == 2);
        resized();
        repaint();
    }
    void resized() override { layoutDone = false; }
    void paint(Graphics& g) override {
        const float H = (float) getHeight();
        const juce::Font f = Fonts::make(13);
        float x = 48;
        hits.clear();
        auto text = [&](const String& t, Colour c) {
            g.setColour(c);
            g.setFont(f);
            const float w = spacedWidth(t, f, 0);
            g.drawText(t, Rectangle<float>(x, 0, w + 2, H), juce::Justification::centredLeft, false);
            x += w;
        };
        const Colour txt(0xffe3cfae);
        // swell
        const float x0 = x;
        text("Swell", txt);
        x += 8;
        auto bar = Rectangle<float>(x, H / 2 - 4, 110, 8);
        g.setColour(Colour(0xff1c0f06));
        g.fillRoundedRectangle(bar, 4);
        g.setColour(col::amber);
        g.fillRoundedRectangle(bar.withWidth(110 * (float) ctx.st.num("volume")), 4);
        x += 110;
        hits.push_back({{x0, 0, x - x0, H}, "Swell (expression) pedal position.", {}});
        x += 18;
        // channels
        auto ch = [&](int v, bool omni) {
            return v == 0 ? String(omni ? "Omni" : "off") : "Ch " + String(v);
        };
        text("Upper ", txt);
        text(ch((int) ctx.st.num("midi.upper"), true), col::gold);
        text(L" · Lower ", txt);
        text(ch((int) ctx.st.num("midi.lower"), false), col::gold);
        text(L" · Pedal ", txt);
        text(ch((int) ctx.st.num("midi.pedal"), false), col::gold);
        x += 18;
        // split
        const float sx0 = x;
        text("Split", txt);
        hits.push_back({{sx0, 0, x - sx0, H},
                        "Keyboard split: play several manuals from one keyboard (on the upper channel).",
                        {}});
        x += 8;
        const char* splitT[] = {"Off", "2", "3"};
        const char* splitTip[] = {"No split", "2 zones: Lower | Upper", "3 zones: Pedal | Lower | Upper"};
        x = segment(
            g, x, H, 3, splitT, (int) ctx.st.num("split.mode"), [&](int i) { return String(splitTip[i]); },
            [this](int i) {
                ctx.st.set("split.mode", i);
                act.learnTarget = {};
                act.sendKeyMap();
                ctx.syncAll();
            });
        x += 8;
        if (!layoutDone) {
            int cx = (int) x;
            for (auto* c : {&chipLo, &chipHi})
                if (c->isVisible()) {
                    c->setBounds(cx, (int) (H / 2 - 11), c->naturalWidth(), 22);
                    cx += c->naturalWidth() + 8;
                }
            layoutDone = true;
        }
        // right side: devices, QWERTY, octave (right-aligned)
        float rx = getWidth() - 48.f;
        auto rtext = [&](const String& t, Colour c) {
            const float w = spacedWidth(t, f, 0);
            rx -= w;
            g.setColour(c);
            g.setFont(f);
            g.drawText(t, Rectangle<float>(rx, 0, w + 2, H), juce::Justification::centredLeft, false);
        };
        // octave: "Octave − 0 +"
        auto btn = [&](const String& t, std::function<void()> fn, const String& tip) {
            rx -= 26;
            auto b = Rectangle<float>(rx, H / 2 - 11, 26, 22);
            g.setColour(Colour(0xff2a170a));
            g.fillRoundedRectangle(b, 5);
            g.setColour(Colour(0xff6b4426));
            g.drawRoundedRectangle(b.reduced(0.5f), 5, 1);
            g.setColour(txt);
            g.setFont(f);
            g.drawText(t, b, juce::Justification::centred, false);
            hits.push_back({b, tip, std::move(fn)});
        };
        const float ox1 = rx;
        btn("+", [this] { setOct(1); }, "Octave up");
        rx -= 8;
        const int o = (int) ctx.st.num("octave");
        rx -= 14;
        g.setColour(col::gold);
        g.setFont(f);
        g.drawText((o > 0 ? "+" : "") + String(o), Rectangle<float>(rx, 0, 14, H), juce::Justification::centred, false);
        rx -= 8;
        btn(L"−", [this] { setOct(-1); }, "Octave down");
        rx -= 8;
        rtext("Octave", txt);
        hits.push_back({{rx, 0, ox1 - rx, H}, "Octave of the computer keyboard.", {}});
        rx -= 18;
        // QWERTY manual
        const char* pcT[] = {"Upper", "Lower", "Pedal"};
        float segW = 0;
        for (auto* t : pcT) segW += spacedWidth(t, Fonts::make(12.5f, 700), 0) + 14 + 2;
        rx -= segW + 2;
        const float qx = rx;
        segment(
            g, rx, H, 3, pcT, (int) ctx.st.num("pcMan"),
            [](int) { return String(L"Which manual the computer keyboard plays (Z–M and Q–P rows)."); },
            [this](int i) {
                if (releasePc) releasePc();
                ctx.st.set("pcMan", i);
                ctx.syncAll();
                if (ctx.ready && ctx.stateChanged) ctx.stateChanged();
            });
        rx = qx - 8;
        rtext("QWERTY", txt);
        rx -= 18;
        // MIDI devices
        juce::StringArray md;
        if (auto* a = ctx.st.audioInfo["midiDevices"].getArray())
            for (int i = 0; i < std::min(2, a->size()); i++) md.add(a->getReference(i).toString());
        if (md.isEmpty())
            rtext("no MIDI device", txt);
        else {
            rtext(L"●", col::gold);
            rtext(md.joinIntoString(String(L" · ")) + " ", txt);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasClicked()) return;
        for (auto& h : hits)
            if (h.fn && h.r.contains(e.position)) {
                h.fn();
                return;
            }
    }
    String getTooltip() override {
        auto p = getMouseXYRelative().toFloat();
        for (auto& h : hits)
            if (h.r.contains(p)) return h.tip;
        return {};
    }

private:
    float segment(Graphics& g, float x, float H, int n, const char* const* labels, int sel,
                  std::function<String(int)> tip, std::function<void(int)> click) {
        const juce::Font f = Fonts::make(12.5f, 700);
        float w = 2;
        for (int i = 0; i < n; i++) w += spacedWidth(labels[i], f, 0) + 14 + 2;
        auto box = Rectangle<float>(x, H / 2 - 10, w, 20);
        g.setColour(Colour(0xff2a170a));
        g.fillRoundedRectangle(box, 5);
        g.setColour(Colour(0xff6b4426));
        g.drawRoundedRectangle(box.reduced(0.5f), 5, 1);
        float bx = x + 2;
        for (int i = 0; i < n; i++) {
            const float bw = spacedWidth(labels[i], f, 0) + 14;
            auto b = Rectangle<float>(bx, box.getY() + 1, bw, box.getHeight() - 2);
            if (i == sel) {
                g.setColour(col::amber);
                g.fillRoundedRectangle(b, 3);
            }
            g.setColour(i == sel ? col::onAmber : Colour(0xffe3cfae));
            g.setFont(Fonts::make(12.5f, i == sel ? 700 : 500));
            g.drawText(labels[i], b, juce::Justification::centred, false);
            hits.push_back({b, tip(i), [click, i] {
                                click(i);
                            }});
            bx += bw + 2;
        }
        return x + w;
    }
    void setOct(int d) {
        if (releasePc) releasePc();
        ctx.st.set("octave", juce::jlimit(-1, 2, (int) ctx.st.num("octave") + d));
        ctx.syncAll();
        if (ctx.ready && ctx.stateChanged) ctx.stateChanged();
    }
    struct Hit {
        Rectangle<float> r;
        String tip;
        std::function<void()> fn;
    };
    Ctx& ctx;
    Actions& act;
    std::function<void()> releasePc;
    SplitChip chipLo, chipHi;
    std::vector<Hit> hits;
    bool layoutDone = false;
};

} // namespace wh
