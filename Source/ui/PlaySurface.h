#pragma once
// Wheelhouse UI - the play surface: Rotary panel (animated cabinet, rpm, speed switch), organ panel (drawbars,
// front knobs, rocker tablets), room panel (floor plan with the paths the engine renders, room presets).
#include "Cards.h"
#include "core/Basics.h"

namespace wh {

using juce::Path;
using juce::Point;
using juce::Rectangle;

/** Rotor angles for drawing: the telemetry arrives irregularly, so the drawn rotors run on their own clock and
    are pulled gently towards the reported angles (a big difference, e.g. start or stop, snaps). */
struct RotorClock {
    double horn = 0, drum = 0, hornHz = 0.75, drumHz = -0.62, t = nowMs();
    static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
    /** Wraps an angle to 0 .. 2 pi: the drawing code hands angles to float arithmetic, which cannot step through an
        arc once the angle is in the millions (that hung the first paint on a machine with a long uptime). */
    static double wrap(double a) { return a - std::floor(a / (2 * PI)) * 2 * PI; }
    void report(double h, double d, double hHz, double dHz) {
        const double now = nowMs(), dt = juce::jlimit(0.0, 0.5, (now - t) / 1000);
        auto pull = [&](double shown, double hz, double reported) {
            const double cur = wrap(shown + hz * 2 * PI * dt);
            double e = reported - cur;
            e -= std::round(e / (2 * PI)) * 2 * PI;
            return std::abs(e) > 1.2 ? reported : wrap(cur + e * 0.2);
        };
        horn = pull(horn, hornHz, h);
        drum = pull(drum, drumHz, d);
        hornHz = hHz;
        drumHz = dHz;
        t = now;
    }
    double hornNow() const { return wrap(horn + hornHz * 2 * PI * juce::jlimit(0.0, 0.5, (nowMs() - t) / 1000)); }
    double drumNow() const { return wrap(drum + drumHz * 2 * PI * juce::jlimit(0.0, 0.5, (nowMs() - t) / 1000)); }
};

// ---------------- Rotary panel ----------------
class RotaryPanel : public Component, public Syncable, public juce::SettableTooltipClient {
public:
    RotaryPanel(Ctx& c, Actions& a, RotorClock& rc) : ctx(c), act(a), clock(rc) { ctx.reg(this); }
    ~RotaryPanel() override { ctx.unreg(this); }
    void sync() override { repaint(); }
    /** Per-frame repaint: only the cabinet and the rpm readouts move. */
    void repaintAnimated() {
        if (cabRect.isEmpty())
            repaint();
        else
            repaint(cabRect.withRight((float) getWidth() - 16).expanded(4).toNearestInt());
    }
    void paint(Graphics& g) override {
        drawInset(g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced(16, 14);
        // title line
        g.setColour(col::text);
        drawSpaced(g, "Rotary", r.withHeight(16), Fonts::make(13, 700), 2.4f, juce::Justification::centredLeft, true);
        g.setColour(col::muted);
        const char* models[] = {"122", "122 early", "147"};
        drawSpaced(g, "Model " + String(models[juce::jlimit(0, 2, (int) ctx.st.num("amp.model"))]), r.withHeight(16),
                   Fonts::make(12), 1, juce::Justification::centredRight);
        // cabinet (140 x 200 SVG units at 1:1)
        cabRect = {r.getX(), r.getY() + 28, 140, 200};
        drawCabinet(g, cabRect);
        // rpm readouts
        const float x = cabRect.getRight() + 16, w = r.getRight() - x;
        auto rpm = [&](float y, const char* label, double hz) {
            g.setColour(col::muted);
            drawSpaced(g, label, {x, y, w, 14}, Fonts::make(12), 1.4f, juce::Justification::centredLeft, true);
            g.setColour(col::gold);
            const String v(juce::roundToInt(std::abs(hz) * 60));
            g.setFont(Fonts::make(30, 600));
            g.drawText(v, Rectangle<float>(x, y + 16, w, 30), juce::Justification::centredLeft, false);
            const float vw = spacedWidth(v, Fonts::make(30, 600), 0);
            g.setColour(col::muted);
            g.setFont(Fonts::make(14));
            g.drawText(" rpm", Rectangle<float>(x + vw, y + 24, 60, 20), juce::Justification::centredLeft, false);
        };
        rpm(cabRect.getY() + 2, "Horn", clock.hornHz);
        rpm(cabRect.getY() + 64, "Drum", clock.drumHz);
        const int cc = (int) ctx.st.num("midi.rotaryCC");
        const String via = cc == 64 ? "sustain pedal" : cc == 1 ? "mod wheel" : "CC " + String(cc);
        juce::AttributedString as;
        as.append("Space bar or " + via + " toggles speed", Fonts::make(12.5f), col::muted);
        as.setLineSpacing(2);
        juce::TextLayout tl;
        tl.createLayout(as, w);
        tl.draw(g, {x, cabRect.getBottom() - tl.getHeight() - 2, w, tl.getHeight()});
        // speed switch
        auto sp = speedRect();
        g.setColour(col::segBg);
        g.fillRoundedRectangle(sp, 22);
        const auto opts = speedOpts();
        const float cw = (sp.getWidth() - 8) / opts.size();
        for (int i = 0; i < (int) opts.size(); i++) {
            auto b = Rectangle<float>(sp.getX() + 4 + i * cw, sp.getY() + 4, cw, sp.getHeight() - 8);
            const bool on = ctx.st.str("rotary") == opts[(size_t) i].first;
            if (on) {
                juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 4, {0, 2}).drawForRectangle(g, b.toNearestInt());
                g.setGradientFill(
                    juce::ColourGradient(Colour(0xfff6f0e4), 0, b.getY(), Colour(0xffcfc4ae), 0, b.getBottom(), false));
                g.fillRoundedRectangle(b, 18);
            }
            g.setColour(on ? Colour(0xff2a2016) : col::muted);
            drawSpaced(g, opts[(size_t) i].second, b, Fonts::make(14, 600), 0.8f, juce::Justification::centred, true);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        auto sp = speedRect();
        if (!sp.contains(e.position) || !e.mouseWasClicked()) return;
        const auto opts = speedOpts();
        const int i = juce::jlimit(0, (int) opts.size() - 1,
                                   (int) ((e.position.x - sp.getX() - 4) / ((sp.getWidth() - 8) / opts.size())));
        act.setRotary(opts[(size_t) i].first);
    }
    String getTooltip() override {
        auto p = getMouseXYRelative().toFloat();
        if (cabRect.contains(p)) return "Live view of horn and drum.";
        auto sp = speedRect();
        if (sp.contains(p)) {
            const auto opts = speedOpts();
            const int i = juce::jlimit(0, (int) opts.size() - 1,
                                       (int) ((p.x - sp.getX() - 4) / ((sp.getWidth() - 8) / opts.size())));
            const String v = opts[(size_t) i].first;
            if (v == "slow") return "Slow (chorale): gentle swirl.";
            if (v == "stop") return L"Stop: rotors stop (or coast, see Rotary › Brake).";
            return "Fast (tremolo): full spin. Space bar toggles Slow/Fast.";
        }
        return {};
    }

private:
    std::vector<std::pair<String, String>> speedOpts() const {
        if (ctx.st.str("halfMoon") == "stop") return {{"slow", "Slow"}, {"stop", "Stop"}, {"fast", "Fast"}};
        return {{"slow", "Slow"}, {"fast", "Fast"}};
    }
    Rectangle<float> speedRect() const {
        auto r = getLocalBounds().toFloat().reduced(16, 14);
        return r.removeFromBottom(42);
    }
    void drawCabinet(Graphics& g, Rectangle<float> box) {
        Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::translation(box.getX(), box.getY()));
        auto grad = [&](std::initializer_list<std::pair<float, uint32_t>> stops, float x1, float y1, float x2, float y2,
                        Rectangle<float> rr) {
            juce::ColourGradient cg(Colour(stops.begin()->second), rr.getX() + x1 * rr.getWidth(),
                                    rr.getY() + y1 * rr.getHeight(), Colour((stops.end() - 1)->second),
                                    rr.getX() + x2 * rr.getWidth(), rr.getY() + y2 * rr.getHeight(), false);
            for (auto& s : stops) cg.addColour(s.first, Colour(s.second));
            return cg;
        };
        auto rect = [&](Rectangle<float> rr, float rad, juce::FillType f, float alpha = 1) {
            g.setFillType(f);
            g.setOpacity(alpha);
            g.fillRoundedRectangle(rr, rad);
            g.setOpacity(1);
        };
        auto wood = [&](Rectangle<float> rr) {
            return juce::FillType(
                grad({{0, 0xff3b1f0d}, {0.12f, 0xff6e3d1d}, {0.5f, 0xff7c4724}, {0.88f, 0xff6e3d1d}, {1, 0xff3b1f0d}},
                     0, 0, 1, 0, rr));
        };
        auto cav = [&](Rectangle<float> rr) {
            return juce::FillType(grad({{0, 0xff0d0805}, {1, 0xff1c110a}}, 0, 0, 0, 1, rr));
        };
        auto solid = [](uint32_t c) {
            return juce::FillType(Colour(c));
        };
        Rectangle<float> body{6, 10, 128, 178};
        rect(body, 3, wood(body));
        Rectangle<float> capR{2, 4, 136, 9};
        rect(capR, 2.5f, juce::FillType(grad({{0, 0xff8a5430}, {1, 0xff4a2611}}, 0, 0, 0, 1, capR)));
        rect({2, 12, 136, 1.5f}, 0, solid(0xff1d0e05), 0.7f);
        Rectangle<float> topCav{14, 20, 112, 54};
        rect(topCav, 2, cav(topCav));
        // horn compartment: orbit, horns, shaft
        const double cabYaw = ctx.st.num("cabYaw");
        const double a = clock.hornNow() - cabYaw;
        g.setColour(col::amber.withAlpha(std::abs(clock.hornHz) > 3 ? 0.55f : 0.25f));
        {
            Path orbit;
            orbit.addEllipse(70 - 44, 44 - 7, 88, 14);
            juce::PathStrokeType ps(1);
            float dashes[] = {2, 3};
            Path dashed;
            ps.createDashedStroke(dashed, orbit, dashes, 2);
            g.fillPath(dashed);
        }
        struct HornGeo {
            float x, rx;
            Path neck;
            bool front;
        };
        auto horn = [](double ang) {
            const double sx = std::sin(ang), depth = std::cos(ang);
            const float x = (float) (70 + 38 * sx), rx = (float) std::max(2.5, 11 * std::abs(depth)),
                        tipH = (float) (9 + 2 * depth);
            const float nx = x - (x > 70 ? rx * 0.3f : -rx * 0.3f);
            HornGeo h{x, rx, {}, depth >= 0};
            h.neck.startNewSubPath(70, 41.5f);
            h.neck.lineTo(nx, 44 - tipH);
            h.neck.lineTo(nx, 44 + tipH);
            h.neck.lineTo(70, 46.5f);
            h.neck.closeSubPath();
            return h;
        };
        auto f = horn(a), b = horn(a + PI);
        const bool live = f.front;
        auto& F = live ? f : b;
        auto& B = live ? b : f;
        // back horn
        g.setColour(Colour(live ? 0xff2a1d12 : 0xff8a6a3a));
        g.fillPath(B.neck);
        g.setColour(Colour(live ? 0xff1d140c : 0xffa8803e));
        g.fillEllipse(B.x - B.rx, 34, 2 * B.rx, 20);
        g.setColour(Colour(live ? 0xff3a2a1b : 0xff6a4f24));
        g.drawEllipse(B.x - B.rx, 34, 2 * B.rx, 20, 1);
        rect({64, 44, 12, 26}, 2, solid(0xff1a1612));
        rect({60, 66, 20, 6}, 1.5f, solid(0xff2a2420));
        g.setColour(Colour(0xff3a332c));
        g.fillEllipse(65, 39, 10, 10);
        g.setColour(Colour(0xff5a5047));
        g.drawEllipse(65, 39, 10, 10, 1);
        // front horn
        if (live) {
            g.setGradientFill(juce::ColourGradient(Colour(0xff7d6a52), F.neck.getBounds().getX(), 0, Colour(0xff7d6a52),
                                                   F.neck.getBounds().getRight(), 0, false));
            juce::ColourGradient cg(Colour(0xff7d6a52), F.neck.getBounds().getX(), 0, Colour(0xff7d6a52),
                                    F.neck.getBounds().getRight(), 0, false);
            cg.addColour(0.5, Colour(0xffcdb894));
            g.setGradientFill(cg);
        } else
            g.setColour(Colour(0xff2a1d12));
        g.fillPath(F.neck);
        g.setColour(Colour(live ? 0xfff2c879 : 0xff1d140c));
        g.fillEllipse(F.x - F.rx, 32, 2 * F.rx, 24);
        g.setColour(Colour(live ? 0xffa88a55 : 0xff3a2a1b));
        g.drawEllipse(F.x - F.rx, 32, 2 * F.rx, 24, 1.2f);
        if (live) {
            g.setColour(Colour(0xff2a1d0e));
            g.fillEllipse(F.x - F.rx * 0.62f, 36.5f, 2 * F.rx * 0.62f, 15);
        }
        // louvres (slats) over the horn compartment
        auto slat = [&](float y, float h, float op) {
            Rectangle<float> s{12, y, 116, h};
            rect(s, 0, juce::FillType(grad({{0, 0xff8b5530}, {0.6f, 0xff5e3218}, {1, 0xff2e170a}}, 0, 0, 0, 1, s)), op);
        };
        for (auto [y, o] : std::initializer_list<std::pair<float, float>>{
                 {21, .92f}, {28, .88f}, {35, .85f}, {42, .85f}, {49, .85f}, {56, .88f}, {63, .92f}})
            slat(y, 3.5f, o);
        slat(70, 4, 1);
        g.setColour(Colour(0xff2a1508));
        g.drawRoundedRectangle(12, 19, 116, 56, 2, 2);
        Rectangle<float> mid{10, 78, 120, 20};
        rect(mid, 1.5f, wood(mid));
        rect({10, 78, 120, 1.5f}, 0, solid(0xff2a1508), 0.8f);
        rect({10, 96.5f, 120, 1.5f}, 0, solid(0xff2a1508), 0.8f);
        rect({56, 84, 28, 8}, 1.5f, solid(0xffc9a45e));
        g.setColour(Colour(0xff7a5a26));
        g.drawRoundedRectangle(56, 84, 28, 8, 1.5f, 0.8f);
        rect({59, 87, 22, 2}, 1, solid(0xff8a6a30), 0.8f);
        // drum compartment
        Rectangle<float> botCav{14, 102, 112, 68};
        rect(botCav, 2, cav(botCav));
        Rectangle<float> drum{24, 108, 92, 56};
        rect(drum, 4,
             juce::FillType(
                 grad({{0, 0xff1a110a}, {0.35f, 0xff4a3322}, {0.55f, 0xff5a3f2a}, {1, 0xff1a110a}}, 0, 0, 1, 0, drum)));
        g.setColour(Colour(0xff5a3f2a));
        g.fillEllipse(24, 103, 92, 10);
        g.setColour(Colour(0xff2a1a10));
        g.drawEllipse(24, 103, 92, 10, 1);
        const double d = clock.drumNow() - cabYaw, dc = std::cos(d);
        if (dc > 0.05) {
            const float dxw = (float) (70 + 40 * std::sin(d)), dw = (float) std::max(4.0, 30 * dc);
            Rectangle<float> win{dxw - dw / 2, 114, dw, 44};
            g.setColour(Colour(0xff0a0604).withAlpha(0.95f));
            g.fillRoundedRectangle(win, 2);
            g.setColour(col::amber.withAlpha(0.95f));
            g.drawRoundedRectangle(win, 2, 1.2f);
            g.setColour(Colour(0xff7a5a3c));
            g.drawLine(dxw - dw / 2 + 2, 156, dxw + dw / 2 - 2, 118, 2);
        }
        g.setColour(Colour(0xff120b06));
        g.fillEllipse(24, 160, 92, 8);
        for (auto [y, o] : std::initializer_list<std::pair<float, float>>{{103, .92f},
                                                                          {110, .86f},
                                                                          {117, .82f},
                                                                          {124, .82f},
                                                                          {131, .82f},
                                                                          {138, .82f},
                                                                          {145, .82f},
                                                                          {152, .86f},
                                                                          {159, .9f}})
            slat(y, 3.5f, o);
        slat(166, 4, 1);
        g.setColour(Colour(0xff2a1508));
        g.drawRoundedRectangle(12, 101, 116, 70, 2, 2);
        rect({6, 174, 128, 14}, 1.5f, solid(0xff3a1d0c));
        rect({6, 174, 128, 1.5f}, 0, solid(0xff8a5430), 0.6f);
        rect({12, 188, 14, 6}, 1.5f, solid(0xff1a0e06));
        rect({114, 188, 14, 6}, 1.5f, solid(0xff1a0e06));
        rect({6, 10, 2, 178}, 0, solid(0xffa86a3c), 0.35f);
    }
    Ctx& ctx;
    Actions& act;
    RotorClock& clock;
    Rectangle<float> cabRect;
};

// ---------------- Drawbars ----------------
class DrawbarGroup : public Component, public Syncable {
public:
    DrawbarGroup(Ctx& c, Actions& a, String nm, String k, int manual, juce::StringArray feet, juce::StringArray kinds,
                 const var& presets)
        : ctx(c), act(a), name(std::move(nm)), key(std::move(k)), man(manual), ft(std::move(feet)),
          kind(std::move(kinds)) {
        ctx.reg(this);
        if (auto* pl = presets.getArray()) {
            for (auto& p : *pl) presetList.push_back({p[0].toString(), p[1]});
            preset = std::make_unique<Select>(
                ctx,
                [this] {
                    Opts o{{"", L"–"}};
                    for (int i = 0; i < (int) presetList.size(); i++) o.push_back({i, presetList[(size_t) i].first});
                    return o;
                },
                [this] {
                    const String cur = juce::JSON::toString(ctx.st.get("db." + key), true);
                    for (int i = 0; i < (int) presetList.size(); i++)
                        if (juce::JSON::toString(presetList[(size_t) i].second, true) == cur) return var(i);
                    return var("");
                },
                [this](const var& v) {
                    if (v.toString().isEmpty()) return;
                    ctx.st.set("db." + key, deepCopy(presetList[(size_t) (int) v].second));
                    act.sendDb(man);
                });
            preset->fontPx = 13;
            preset->setTooltip("Registration presets for the " + name.toLowerCase() + " manual.");
            addAndMakeVisible(*preset);
        }
    }
    ~DrawbarGroup() override { ctx.unreg(this); }
    int naturalWidth() const { return ft.size() * 28 + (ft.size() - 1) * 5; }
    void sync() override { repaint(); }
    void resized() override {
        if (preset)
            preset->setBounds(getWidth() - juce::jmin(120, getWidth() - 60), 2, juce::jmin(120, getWidth() - 60), 24);
    }
    void paint(Graphics& g) override {
        g.setColour(col::text);
        drawSpaced(g, name, {0, 0, 80, 28}, Fonts::make(12, 700), 1.6f, juce::Justification::centredLeft, true);
        g.setColour(col::segLine);
        g.fillRect(0.f, 33.f, (float) getWidth(), 1.f);
        for (int i = 0; i < ft.size(); i++) {
            const float x = i * 33.f;
            g.setColour(col::text);
            g.setFont(Fonts::make(12, 600));
            g.drawText(ft[i], Rectangle<float>(x, 40, 28, 15), juce::Justification::centred, false);
            auto slot = slotRect(i);
            g.setColour(Colour(0xff070605));
            g.fillRoundedRectangle(slot, 3);
            const int v = value(i);
            const bool muted = man == 0 && i == 8 && ctx.st.flag("perc.on");
            const String kd = muted ? "muted" : kind[i];
            struct Look {
                uint32_t h0, h1, sh, n;
            };
            static const std::map<String, Look> looks{{"brown", {0xff8a5030, 0xff5e3018, 0xff6b3d22, 0xfff3dcc6}},
                                                      {"white", {0xfffaf6ee, 0xffd6ccb9, 0xffd8cfbf, 0xff2a2218}},
                                                      {"black", {0xff34302b, 0xff121010, 0xff2a2622, 0xffe8dfcf}},
                                                      {"pedal", {0xffa05a2c, 0xff6a3416, 0xff7a4422, 0xfff3dcc6}},
                                                      {"muted", {0xff5a554e, 0xff3a3631, 0xff3a3530, 0xffa8a092}}};
            const Look lk = looks.at(kd);
            const float shaft = 6.f + v * 18.f;
            g.setColour(Colour(lk.sh));
            g.fillRect(slot.getX() + 5, slot.getY(), slot.getWidth() - 10, shaft);
            auto head = Rectangle<float>(slot.getX() - 2, slot.getY() + shaft - 2, slot.getWidth() + 4, 34);
            juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 5, {0, 3}).drawForRectangle(g, head.toNearestInt());
            g.setGradientFill(
                juce::ColourGradient(Colour(lk.h0), 0, head.getY(), Colour(lk.h1), 0, head.getBottom(), false));
            Path hp;
            hp.addRoundedRectangle(head.getX(), head.getY(), head.getWidth(), head.getHeight(), 4, 7, true, true, true,
                                   true);
            g.fillPath(hp);
            g.setColour(Colour(lk.n));
            g.setFont(Fonts::make(15, 700));
            g.drawText(String(v), head.withTrimmedBottom(4), juce::Justification::centredBottom, false);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        dragBar = barAt(e.position.x);
        if (dragBar >= 0) setV(dragBar, fromY(dragBar, e.position.y));
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragBar >= 0) setV(dragBar, fromY(dragBar, e.position.y));
    }
    void mouseUp(const juce::MouseEvent&) override { dragBar = -1; }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const int i = barAt(e.position.x);
        if (i >= 0 && w.deltaY != 0) setV(i, value(i) + (w.deltaY < 0 ? 1 : -1));
    }
    String getTooltip() {
        const int i = barAt(getMouseXYRelative().toFloat().x);
        if (i < 0 || getMouseXYRelative().y < 36) return {};
        static const wchar_t* tips[] = {L"16′ – sub-octave, an octave below the fundamental.",
                                        L"5⅓′ – sub-third: the fifth above the sub-octave.",
                                        L"8′ – fundamental, the pitch you play.",
                                        L"4′ – octave.",
                                        L"2⅔′ – twelfth (octave + fifth).",
                                        L"2′ – fifteenth (two octaves).",
                                        L"1⅗′ – seventeenth (two octaves + major third).",
                                        L"1⅓′ – nineteenth (two octaves + fifth).",
                                        L"1′ – twenty-second (three octaves)."};
        String t;
        if (man == 0 && i == 8 && ctx.st.flag("perc.on"))
            t = L"1′ is muted while percussion is on – just like the original.";
        else if (man == 2)
            t = i == 0 ? String(L"Pedal 16′ – fundamental from the complex tone wheels plus the lower harmonics: the "
                                L"foundation.")
                       : String(L"Pedal 8′ – upper even harmonics only: brighter, more punch.");
        else
            t = String(tips[i]);
        return tipText(t, L"Drag or scroll · 0 = in, 8 = full out");
    }

private:
    Rectangle<float> slotRect(int i) const { return {i * 33.f + 2, 59, 24, (float) getHeight() - 59}; }
    int barAt(float x) const {
        const int i = (int) (x / 33.f);
        return i >= 0 && i < ft.size() ? i : -1;
    }
    int value(int i) const { return (int) ctx.st.num("db." + key + "." + String(i)); }
    int fromY(int i, float y) const { return juce::roundToInt(((y - slotRect(i).getY()) - 21) / 18); }
    void setV(int i, int v) {
        v = juce::jlimit(0, 8, v);
        if (v != value(i)) {
            ctx.st.set("db." + key + "." + String(i), v);
            act.sendDb(man);
            ctx.syncAll();
        }
    }
    Ctx& ctx;
    Actions& act;
    String name, key;
    int man;
    juce::StringArray ft, kind;
    std::vector<std::pair<String, var>> presetList;
    std::unique_ptr<Select> preset;
    int dragBar = -1;
};

/** The rocker tablets (percussion, vibrato upper/lower). */
class Tablets : public Component, public Syncable, public juce::TooltipClient {
public:
    Tablets(Ctx& c, Actions& a) : ctx(c), act(a) { ctx.reg(this); }
    ~Tablets() override { ctx.unreg(this); }
    void sync() override { repaint(); }
    struct Tab {
        const char* key;
        const char* on;
        const char* off;
        const wchar_t* tip;
    };
    static const std::vector<Tab>& tabs() {
        static const std::vector<Tab> t{
            {"on", "PERC\nON", "PERC\nOFF",
             L"Percussion on/off: a decaying 2nd or 3rd harmonic on the attack, upper manual only, single-trigger "
             L"(retriggers only after all keys are released). Mutes the 1′ drawbar, like the original."},
            {"soft", "PERC\nSOFT", "PERC\nNORM",
             L"Percussion volume: Soft or Normal. Normal also pulls the upper manual down a little – just like the "
             L"real thing."},
            {"fast", "PERC\nFAST", "PERC\nSLOW", L"Percussion decay: Fast or Slow (times in Organ › Percussion)."},
            {"third", "PERC\n3RD", "PERC\n2ND", L"Percussion harmonic: 3rd (2⅔′ – the Jimmy Smith sound) or 2nd (4′)."},
            {"vibU", "VIB\nUPPER", "VIB\nUPPER", L"Vibrato/chorus on the upper manual."},
            {"vibL", "VIB\nLOWER", "VIB\nLOWER", L"Vibrato/chorus on the lower manual."}};
        return t;
    }
    static int naturalWidth() { return 6 * 41 + 5 * 6; }
    void paint(Graphics& g) override {
        for (int i = 0; i < 6; i++) {
            auto r = Rectangle<float>(i * 47.f, (float) getHeight() - 88, 41, 88);
            const bool on = ctx.st.flag("perc." + String(tabs()[(size_t) i].key));
            if (!on)
                juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 6, {0, 4})
                    .drawForRectangle(g, r.toNearestInt());
            juce::ColourGradient cg(Colour(on ? 0xffbdb29c : 0xfffaf6ee), 0, r.getY(),
                                    Colour(on ? 0xfffaf6ee : 0xffbdb29c), 0, r.getBottom(), false);
            cg.addColour(on ? 0.45 : 0.55, Colour(0xffe9e1d0));
            g.setGradientFill(cg);
            g.fillRoundedRectangle(r, 4);
            if (on) {
                g.setColour(juce::Colours::black.withAlpha(0.25f));
                g.fillRect(r.withHeight(3).reduced(2, 0));
            }
            g.setColour(Colour(0xff2a2016));
            const String txt = on ? tabs()[(size_t) i].on : tabs()[(size_t) i].off;
            auto lines = juce::StringArray::fromLines(txt);
            for (int l = 0; l < lines.size(); l++)
                drawSpaced(g, lines[l], {r.getX(), r.getY() + 8 + l * 12.f, r.getWidth(), 12}, Fonts::make(10.5f, 700),
                           0.2f, juce::Justification::centred);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        const int i = (int) (e.position.x / 47.f);
        if (i < 0 || i > 5 || !e.mouseWasClicked()) return;
        const String k = "perc." + String(tabs()[(size_t) i].key);
        ctx.st.set(k, !ctx.st.flag(k));
        act.sendPerc();
        ctx.syncAll();
    }
    String getTooltip() override {
        const int i = (int) (getMouseXYRelative().x / 47.f);
        return i >= 0 && i < 6 ? String(tabs()[(size_t) i].tip) : String();
    }

private:
    Ctx& ctx;
    Actions& act;
};

class OrganPanel : public Component {
public:
    OrganPanel(Ctx& c, Actions& a, std::function<void()> markPreset) : ctx(c) {
        juce::StringArray ft{"16'", L"5⅓'", "8'", "4'", L"2⅔'", "2'", L"1⅗'", L"1⅓'", "1'"};
        juce::StringArray kind{"brown", "brown", "white", "white", "black", "white", "black", "black", "white"};
        groups.push_back(std::make_unique<DrawbarGroup>(c, a, "Upper", "upper", 0, ft, kind, tables()["upperPresets"]));
        groups.push_back(std::make_unique<DrawbarGroup>(c, a, "Pedal", "pedal", 2, juce::StringArray{"16'", "8'"},
                                                        juce::StringArray{"pedal", "pedal"}, var()));
        groups.push_back(std::make_unique<DrawbarGroup>(c, a, "Lower", "lower", 1, ft, kind, tables()["lowerPresets"]));
        for (auto& gr : groups) addAndMakeVisible(*gr);
        Opts vib;
        for (auto& v : *tables()["vib"].getArray()) vib.push_back({v[0], v[1].toString()});
        KnobOpts kv;
        kv.label = "Vibrato";
        kv.tip = L"Scanner vibrato/chorus. V1–V3: vibrato in three depths. C1–C3: chorus (vibrato mixed with the dry "
                 L"signal). C3 is the classic.";
        kv.opts = vib;
        kv.def = "c3";
        kv.get = [&c] {
            return c.st.get("vib");
        };
        kv.set = [&c, markPreset](const var& v) {
            c.st.set("vib", v);
            c.sendV("vib", v);
            if (markPreset) markPreset();
        };
        knobs.push_back(std::make_unique<Knob>(c, kv));
        auto mk = [&](const char* label, const String& tip, const char* path, double def,
                      std::function<String(double)> fmt, std::function<void()> send) {
            KnobOpts o;
            o.label = label;
            o.tip = tip;
            o.def = def;
            o.fmt = std::move(fmt);
            const String p = path;
            o.get = [&c, p] {
                return c.st.get(p);
            };
            o.set = [&c, p, send](const var& v) {
                c.st.set(p, v);
                send();
                c.syncAll();
            };
            knobs.push_back(std::make_unique<Knob>(c, o));
        };
        mk("Drive", L"How hard the organ hits the Rotary power amp – from clean to growl.", "drive", 0.35, pct,
           [&c] { c.sendV("drive", c.st.get("drive")); });
        mk("Reverb", L"Spring reverb mix (spring tank inside the organ – it spins through the Rotary).", "spring.mix",
           0.2, offPct, [&a] { a.sendSpring(); });
        mk("Swell",
           "Swell / expression pedal: volume, plus the bass boost of the original pedal when it is closed down.",
           "volume", 0.8, pct, [&c] { c.sendV("volume", c.st.get("volume")); });
        for (auto& k : knobs) addAndMakeVisible(*k);
        tablets = std::make_unique<Tablets>(c, a);
        addAndMakeVisible(*tablets);
    }
    void paint(Graphics& g) override {
        drawInset(g, getLocalBounds().toFloat());
        g.setColour(col::segLine);
        g.fillRect(16.f, frontY, getWidth() - 32.f, 1.f);
        g.fillRect(sepX, frontY + 8, 1.f, (float) getHeight() - frontY - 18);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(16, 10);
        auto front = r.removeFromBottom(96 + 8);
        frontY = (float) front.getY();
        r.removeFromBottom(8);
        // drawbar groups centred, 22 px apart
        int total = 0;
        for (auto& gr : groups) total += gr->naturalWidth();
        total += 22 * 2;
        int x = r.getX() + (r.getWidth() - total) / 2;
        for (auto& gr : groups) {
            gr->setBounds(x, r.getY(), gr->naturalWidth(), r.getHeight());
            x += gr->naturalWidth() + 22;
        }
        // front: knobs | separator | tablets, centred, 18 px apart
        const int kw = 58, kn = (int) knobs.size();
        const int fw = kn * kw + (kn - 1) * 18 + 18 + 1 + 18 + Tablets::naturalWidth();
        int fx = front.getX() + (front.getWidth() - fw) / 2;
        const int fy = front.getY() + 8;
        for (auto& k : knobs) {
            k->setBounds(fx, front.getBottom() - 84, kw, 84);
            fx += kw + 18;
        }
        sepX = (float) fx;
        fx += 1 + 18;
        tablets->setBounds(fx, fy, Tablets::naturalWidth(), front.getBottom() - fy);
    }

private:
    Ctx& ctx;
    std::vector<std::unique_ptr<DrawbarGroup>> groups;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<Tablets> tablets;
    float frontY = 0, sepX = 0;
};

// ---------------- Floor plan ----------------
/** Floor plan of the room: Rotary (with horn and drum), listener with speakers, mics, and the sound paths the
    engine renders right now (direct, image sources up to 2nd order, the lit wall patches), each drawn with its
    level. Drag: Rotary / head / room pair, right-drag: facing, scroll: rotate. */
class FloorPlan : public Component, public juce::SettableTooltipClient {
public:
    FloorPlan(Ctx& c, Actions& a, RotorClock& rc, bool large) : ctx(c), act(a), clock(rc), big(large) {
        setTooltip(
            big ? L"Floor plan. Drag the Rotary or your head (or the mics) · right-drag: facing · scroll: rotate."
                : L"Floor plan. Drag the Rotary or your head · right-drag: facing · scroll: rotate.");
    }
    std::function<bool(rotary::PathViz&)> readPaths;
    String infoText; // bottom-left line (small view)

    void paint(Graphics& g) override {
        auto rr = getLocalBounds().toFloat();
        g.setColour(Colour(0xff0b100e));
        g.fillRoundedRectangle(rr, 6);
        {
            Graphics::ScopedSaveState s(g);
            Path clip;
            clip.addRoundedRectangle(rr, 6);
            g.reduceClipRegion(clip);
            draw(g);
            drawCached(g, rr, 0, "planShade", [](Graphics& ig, Rectangle<float> l) { // inner shadow over the plan
                Path c;
                c.addRoundedRectangle(l, 6);
                ig.reduceClipRegion(c);
                juce::DropShadow(juce::Colours::black.withAlpha(0.8f), 14, {0, 4}).drawForPath(ig, [&] {
                    Path p;
                    p.addRectangle(l.expanded(30));
                    p.addRoundedRectangle(l, 6);
                    p.setUsingNonZeroWinding(false);
                    return p;
                }());
            });
        }
        g.setColour(Colour(0xff24302a));
        g.drawRoundedRectangle(rr.reduced(0.5f), 6, 1);
        g.setColour(Colour(0xff8f8370));
        g.setFont(Fonts::make(big ? 12 : 11.5f));
        if (big)
            g.drawText(L"Floor plan · Drag: Rotary / head · Right-drag: facing · Scroll: turn · lines: what the engine "
                       L"renders",
                       Rectangle<float>(12, 6, rr.getWidth() - 24, 16), juce::Justification::centredLeft, true);
        else if (infoText.isNotEmpty())
            g.drawText(infoText, Rectangle<float>(10, rr.getBottom() - 21, rr.getWidth() - 20, 15),
                       juce::Justification::centredLeft, true);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const auto p = e.position;
        const auto L = W2C(st().num("rotaryPos.x"), st().num("rotaryPos.y")),
                   H = W2C(st().num("listener.x"), st().num("listener.y"));
        const double dl = p.getDistanceFrom(L), dh = p.getDistanceFrom(H);
        const auto M = W2C(st().num("mics.roomX"), st().num("mics.roomY"));
        const double dm = st().flag("mics.on") ? p.getDistanceFrom(M) : 1e9;
        if (e.mods.isPopupMenu())
            drag = dm < 18 ? PairRot : Rot;
        else if (dm < 16 && dm <= dh && dm <= dl)
            drag = Pair;
        else if (dh < 24 && dh <= dl)
            drag = Listener;
        else if (dl < 30)
            drag = RotaryPos;
        else
            drag = None;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag == None) return;
        const auto w = C2W(e.position.x, e.position.y);
        if (drag == Rot)
            act.setYaw(std::atan2(w.y - st().num("listener.y"), w.x - st().num("listener.x")));
        else if (drag == Pair) {
            st().set("mics.roomX", clampd(w.x, 0.3, st().num("room.Lx") - 0.3));
            st().set("mics.roomY", clampd(w.y, 0.3, st().num("room.Ly") - 0.3));
            st().set("mics.placed", true);
            act.sendMics();
        } else if (drag == PairRot) {
            st().set("mics.roomYaw", std::atan2(w.y - st().num("mics.roomY"), w.x - st().num("mics.roomX")));
            act.sendMics();
        } else {
            const String o = drag == RotaryPos ? "rotaryPos" : "listener";
            st().set(o + ".x", w.x);
            st().set(o + ".y", w.y);
            act.clampPos();
            act.sendPos();
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (drag != None) ctx.syncAll();
        drag = None;
    }
    void mouseMove(const juce::MouseEvent& e) override {
        const auto L = W2C(st().num("rotaryPos.x"), st().num("rotaryPos.y")),
                   H = W2C(st().num("listener.x"), st().num("listener.y"));
        setMouseCursor(e.position.getDistanceFrom(L) < 30 || e.position.getDistanceFrom(H) < 24
                           ? juce::MouseCursor::DraggingHandCursor
                           : juce::MouseCursor::NormalCursor);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (w.deltaY == 0) return;
        const double d = (w.deltaY < 0 ? 1 : -1) * 7.5 * PI / 180;
        const auto L = W2C(st().num("rotaryPos.x"), st().num("rotaryPos.y")),
                   M = W2C(st().num("mics.roomX"), st().num("mics.roomY"));
        if (st().flag("mics.on") && e.position.getDistanceFrom(M) < 22) {
            st().set("mics.roomYaw", st().num("mics.roomYaw") - d);
            act.sendMics();
            ctx.syncAll();
        } else if (e.position.getDistanceFrom(L) < 34)
            act.setCabYaw(st().num("cabYaw") - d);
        else
            act.setYaw(st().num("listener.yaw") - d);
    }

private:
    UiState& st() const { return ctx.st; }
    Point<float> W2C(double x, double y) const {
        return {(float) (ox + x * s), (float) (oy + (st().num("room.Ly") - y) * s)};
    }
    Point<double> C2W(double px, double py) const { return {(px - ox) / s, st().num("room.Ly") - (py - oy) / s}; }

    void draw(Graphics& g) {
        const double W = getWidth(), H = getHeight(), Lx = st().num("room.Lx"), Ly = st().num("room.Ly");
        const double padT = big ? 30 : 12, padB = big ? 14 : 24, padX = big ? 18 : 14;
        s = std::min((W - 2 * padX) / Lx, (H - padT - padB) / Ly);
        ox = (W - Lx * s) / 2;
        oy = padT + (H - padT - padB - Ly * s) / 2;
        const auto c0 = W2C(0, Ly), c1 = W2C(Lx, 0);
        // grid
        const double step = Lx > 15 || Ly > 20 ? 5 : 1;
        g.setColour(Colour(0xff17221d));
        for (double x = step; x < Lx; x += step) g.drawVerticalLine((int) W2C(x, 0).x, c0.y, c1.y);
        for (double y = step; y < Ly; y += step) g.drawHorizontalLine((int) W2C(0, y).y, c0.x, c1.x);
        g.setColour(Colour(0xff6b5a40));
        g.drawRect(Rectangle<float>(c0, c1), 2.5f);
        drawPaths(g, Lx, Ly);
        // Rotary cabinet
        const double hornA = clock.hornNow(), drumA = clock.drumNow(), cabYaw = st().num("cabYaw");
        const auto L = W2C(st().num("rotaryPos.x"), st().num("rotaryPos.y"));
        const float cab = (float) clampd(0.74 * s, big ? 26 : 22, 60);
        {
            Graphics::ScopedSaveState ss(g);
            g.addTransform(juce::AffineTransform::rotation((float) (-cabYaw + PI / 2)).translated(L));
            const float cw = cab, cd = cab * 0.72f;
            g.setColour(Colour(0xff4a2e1c));
            g.fillRoundedRectangle(-cw / 2, -cd / 2, cw, cd, 3);
            g.setColour(Colour(0xffa8743f));
            g.drawRoundedRectangle(-cw / 2, -cd / 2, cw, cd, 3, 1.5f);
            g.setColour(Colour(0xffd9a86c));
            g.drawLine(-cw / 2 + 2, -cd / 2, cw / 2 - 2, -cd / 2, 1);
            g.setColour(Colour(0xff1a0e06));
            g.drawLine(-cw / 2, cd / 2, cw / 2, cd / 2, 3);
        }
        const float dr = cab * 0.3f;
        Path wedge;
        wedge.startNewSubPath(L);
        wedge.addCentredArc(L.x, L.y, dr, dr, 0, (float) (PI / 2 - drumA - 0.5), (float) (PI / 2 - drumA + 0.5), false);
        wedge.closeSubPath();
        g.setColour(Colour(0x59f2c879));
        g.fillPath(wedge);
        const float hl = cab * 0.62f;
        const Point<float> hp(L.x + (float) std::cos(hornA) * hl, L.y - (float) std::sin(hornA) * hl);
        g.setColour(col::gold);
        g.drawLine({L, hp}, 3);
        g.fillEllipse(hp.x - 3.5f, hp.y - 3.5f, 7, 7);
        g.setColour(Colour(0x4cf2c879));
        g.drawLine({L, {L.x - (float) std::cos(hornA) * hl * 0.7f, L.y + (float) std::sin(hornA) * hl * 0.7f}}, 2);
        g.setColour(Colour(0xffc9b89c));
        g.setFont(Fonts::make(12));
        g.drawText("Rotary", Rectangle<float>(L.x - 40, L.y + cab / 2 + 3, 80, 14), juce::Justification::centred,
                   false);
        if (st().flag("mics.on")) drawMics(g, L, cab);
        // listener with speakers
        const auto Hp = W2C(st().num("listener.x"), st().num("listener.y"));
        const double yaw = st().num("listener.yaw");
        Graphics::ScopedSaveState ss(g);
        g.setOpacity(st().flag("mics.on") ? 0.35f : 1.f);
        const var show = act.mode()["spk"]["az"];
        if (auto* az = show.getArray())
            for (auto& d : *az) {
                const double a = yaw + (double) d * PI / 180, rr = big ? 30 : 24, pr = big ? 3.5 : 2.5;
                g.setColour(Colour(0xff7d715f));
                g.fillEllipse((float) (Hp.x + std::cos(a) * rr - pr), (float) (Hp.y - std::sin(a) * rr - pr),
                              (float) (2 * pr), (float) (2 * pr));
            }
        g.setColour(Colour(0xffefe7d8));
        g.fillEllipse(Hp.x - 9, Hp.y - 9, 18, 18);
        Path nose;
        nose.addTriangle(Hp.x + (float) std::cos(yaw) * 15, Hp.y - (float) std::sin(yaw) * 15,
                         Hp.x + (float) std::cos(yaw + 0.55) * 8, Hp.y - (float) std::sin(yaw + 0.55) * 8,
                         Hp.x + (float) std::cos(yaw - 0.55) * 8, Hp.y - (float) std::sin(yaw - 0.55) * 8);
        g.fillPath(nose);
        g.setColour(col::amber);
        for (double o : {PI / 2, -PI / 2})
            g.fillEllipse((float) (Hp.x + std::cos(yaw + o) * 9 - 2.6), (float) (Hp.y - std::sin(yaw + o) * 9 - 2.6),
                          5.2f, 5.2f);
        g.setColour(Colour(0xffc9b89c));
        g.setFont(Fonts::make(12));
        const double dist = std::hypot(st().num("rotaryPos.x") - st().num("listener.x"),
                                       st().num("rotaryPos.y") - st().num("listener.y"));
        g.drawText(L"You · " + nf(dist) + " m", Rectangle<float>(Hp.x + 14, Hp.y + 12, 120, 14),
                   juce::Justification::centredLeft, false);
    }

    /** The paths of the engine's room model, alpha by level (dB relative to the loudest horn path). */
    void drawPaths(Graphics& g, double Lx, double Ly) {
        rotary::PathViz v;
        if (!readPaths || !readPaths(v) || v.n == 0) return;
        const Point<double> Lw(st().num("rotaryPos.x"), st().num("rotaryPos.y")),
            Hw(st().num("listener.x"), st().num("listener.y"));
        float ref = 1e-6f;
        for (int i = 0; i < v.n; i++) ref = std::max(ref, v.item[i].level);
        auto alphaOf = [&](float level) {
            const float db = 20 * std::log10(std::max(level, 1e-9f) / ref);
            return juce::jlimit(0.f, 1.f, (db + 30) / 30);
        };
        // where the line from the listener towards point q first leaves the room (last bounce of an image path)
        auto exitPoint = [&](Point<double> q) {
            const Point<double> d = q - Hw;
            double t = 1e9;
            if (d.x > 1e-9) t = std::min(t, (Lx - Hw.x) / d.x);
            if (d.x < -1e-9) t = std::min(t, (0 - Hw.x) / d.x);
            if (d.y > 1e-9) t = std::min(t, (Ly - Hw.y) / d.y);
            if (d.y < -1e-9) t = std::min(t, (0 - Hw.y) / d.y);
            return Hw + d * std::min(t, 1.0);
        };
        auto line = [&](std::initializer_list<Point<double>> pts, Colour c, float width) {
            Path p;
            bool first = true;
            for (auto& q : pts) {
                const auto cp = W2C(q.x, q.y);
                if (first)
                    p.startNewSubPath(cp);
                else
                    p.lineTo(cp);
                first = false;
            }
            g.setColour(c);
            g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        // draw quiet paths first so the strong ones end up on top
        std::vector<int> order((size_t) v.n);
        for (int i = 0; i < v.n; i++) order[(size_t) i] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return v.item[a].level < v.item[b].level; });
        for (int idx : order) {
            const auto& it = v.item[idx];
            const float a = alphaOf(it.level);
            if (a <= 0.02f) continue;
            const Colour base = it.rotor == 0 ? col::amber : Colour(0xffc97b4a);
            const Point<double> q(it.x, it.y);
            if (it.patch) {
                // diffuse: Rotary -> lit wall patch -> listener, plus a glow at the patch
                line({Lw, q, Hw}, base.withAlpha(0.06f + 0.3f * a), 1.0f);
                const auto cp = W2C(q.x, q.y);
                const float r = 2.5f + 4 * a;
                g.setColour(base.withAlpha(0.25f + 0.5f * a));
                g.fillEllipse(cp.x - r, cp.y - r, 2 * r, 2 * r);
            } else if (it.order == 0) {
                line({Lw, Hw}, base.withAlpha(0.25f + 0.6f * a), it.rotor == 0 ? 2.4f : 2.0f);
            } else {
                // image source outside the room: last leg from the wall to the listener; first order also the leg
                // from the Rotary to the reflection point
                const auto wall = exitPoint(q);
                const bool vertical =
                    std::abs(q.x - Lw.x) < 1e-6 && std::abs(q.y - Lw.y) < 0.4; // floor / ceiling bounce
                if (vertical) continue;                                        // on top of the direct line
                if (it.order == 1)
                    line({Lw, wall, Hw}, base.withAlpha(0.08f + 0.45f * a), 1.4f);
                else
                    line({wall, Hw}, base.withAlpha(0.05f + 0.3f * a), 1.0f);
            }
        }
    }

    void drawMics(Graphics& g, Point<float> L, float cab) {
        const double D2R = PI / 180;
        auto mic = [&](double x, double y, double ax, double ay, float r) {
            const auto p = W2C(x, y);
            g.setColour(col::gold);
            g.drawLine(p.x, p.y, p.x + (float) ax * (r + 7), p.y - (float) ay * (r + 7), 1.5f);
            g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r);
            g.setColour(col::panel);
            g.fillEllipse(p.x - r * 0.45f, p.y - r * 0.45f, r * 0.9f, r * 0.9f);
        };
        auto close = [&](bool stereo, double ang, double center, double dist) {
            std::vector<double> phs =
                stereo ? std::vector<double>{center - ang / 2, center + ang / 2} : std::vector<double>{center};
            for (double ph : phs) {
                const double t = st().num("cabYaw") + ph * D2R, c = std::cos(t), sn = std::sin(t);
                const double cf = std::abs(std::cos(ph * D2R)), sf = std::abs(std::sin(ph * D2R));
                const double R =
                    std::min(cf > 1e-6 ? 0.275 / cf : 1e9, sf > 1e-6 ? 0.37 / sf : 1e9) + std::max(0.02, dist);
                const double rp = std::max(R * s, cab * 0.6 + 7.0);
                const auto w = C2W(L.x + c * rp, L.y - sn * rp);
                mic(w.x, w.y, -c, -sn, big ? 4.f : 3.f);
            }
        };
        close(st().flag("mics.hornStereo"), st().num("mics.hornAngle"), st().num("mics.hornCenter"),
              st().num("mics.hornDist"));
        close(st().flag("mics.drumStereo"), st().num("mics.drumAngle"), st().num("mics.drumCenter"),
              st().num("mics.drumDist"));
        const double yawR = st().num("mics.roomYaw"), lx2 = -std::sin(yawR), ly2 = std::cos(yawR);
        const int arr = (int) st().num("mics.roomArr");
        const double sp = arr == 0 ? st().num("mics.roomSpacing") : arr == 2 ? 0.17 : 0.02;
        const double aimOff = arr == 0 ? 0 : (arr == 2 ? 55 : 45) * D2R;
        const auto C = W2C(st().num("mics.roomX"), st().num("mics.roomY"));
        for (int sg : {1, -1}) {
            const double off = std::max(sp * s, arr == 1 ? 3.0 : 9.0) / 2 * sg;
            const auto w = C2W(C.x + lx2 * off, C.y - ly2 * off);
            const double a = yawR + sg * aimOff;
            mic(w.x, w.y, std::cos(a), std::sin(a), big ? 5.f : 4.f);
        }
        g.setColour(Colour(0xffc9b89c));
        g.setFont(Fonts::make(11));
        g.drawText("Room pair", Rectangle<float>(C.x - 40, C.y + 12, 80, 14), juce::Justification::centred, false);
    }

    Ctx& ctx;
    Actions& act;
    RotorClock& clock;
    bool big;
    double s = 1, ox = 0, oy = 0;
    enum Drag { None, Rot, Pair, PairRot, Listener, RotaryPos } drag = None;
};

class RoomPanel : public Component, public Syncable {
public:
    RoomPanel(Ctx& c, Actions& a, RotorClock& rc, std::function<void()> openRoom)
        : plan(c, a, rc, false), ctx(c), act(a) {
        ctx.reg(this);
        addAndMakeVisible(plan);
        settings.onClick = std::move(openRoom);
        settings.setTooltip("Room settings: size, surfaces, reflections.");
        addAndMakeVisible(settings);
        Opts quick;
        auto* rooms = tables()["rooms"].getArray();
        for (int i = 0; i < rooms->size(); i++) quick.push_back({i, rooms->getReference(i)["s"].toString()});
        presets = std::make_unique<Seg>(
            ctx, quick, [this] { return var(act.matchRoom()); }, [this](const var& v) { act.loadRoom((int) v); });
        addAndMakeVisible(*presets);
    }
    ~RoomPanel() override { ctx.unreg(this); }
    FloorPlan plan;
    void sync() override {
        plan.infoText = act.roomLine();
        plan.repaint();
    }
    void paint(Graphics& g) override {
        drawInset(g, getLocalBounds().toFloat());
        g.setColour(col::text);
        drawSpaced(g, "Room", {16, 14, 100, 16}, Fonts::make(13, 700), 2.4f, juce::Justification::centredLeft, true);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(16, 12).withTrimmedTop(2);
        auto head = r.removeFromTop(18);
        settings.setBounds(head.removeFromRight(80));
        r.removeFromTop(10);
        presets->setBounds(r.removeFromBottom(32));
        r.removeFromBottom(10);
        plan.setBounds(r);
    }

private:
    struct LinkButton : juce::Button {
        LinkButton() : juce::Button(L"Settings ›") {}
        void paintButton(Graphics& g, bool over, bool) override {
            g.setColour(over ? Colour(0xffffdc9a) : col::gold);
            g.setFont(Fonts::make(12.5f));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centredRight, false);
        }
    } settings;
    Ctx& ctx;
    Actions& act;
    std::unique_ptr<Seg> presets;
};

} // namespace wh
