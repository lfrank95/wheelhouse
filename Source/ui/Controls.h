#pragma once
// Wheelhouse UI - the controls of the settings cards: knob, segmented buttons, select, value and button rows,
// sections and pages (grid). Every control reads and writes the state through get/set functions and is refreshed
// by Ctx::syncAll(), like the former web UI's builders.
#include "Theme.h"
#include "UiState.h"
#include <map>
#include <optional>

namespace wh {

using juce::Component;
using juce::Graphics;

/** Anything that shows state and can be refreshed / reset to its default. */
struct Syncable {
    virtual ~Syncable() = default;
    virtual void sync() {}
    virtual void reset() {}
};

/** Shared context of the UI: the state, the message sender and the registry of controls. */
struct Ctx {
    UiState st;
    std::function<void(const var&)> sendRaw;   // one DSP message to the engine
    std::function<void()> stateChanged;        // the saved state changed (save it soon)
    std::function<void(const String&)> invoke; // native actions: "audioSettings", "audioInfo", "chantest:<ch>:<lfe>"
    std::vector<Syncable*> all;
    std::map<String, std::vector<Syncable*>> cardCtl; // card id -> controls with a default
    String curCard;                                   // card being built
    bool muteSend = false, ready = false;
    std::function<void()> onSync; // extra work after syncAll (layout of the open card)

    void reg(Syncable* s) {
        all.push_back(s);
        if (curCard.isNotEmpty()) cardCtl[curCard].push_back(s);
    }
    void unreg(Syncable* s) {
        all.erase(std::remove(all.begin(), all.end(), s), all.end());
        for (auto& [k, v] : cardCtl) v.erase(std::remove(v.begin(), v.end(), s), v.end());
    }
    void syncAll() {
        for (auto* s : std::vector<Syncable*>(all)) s->sync();
        if (onSync) onSync();
    }
    void send(const var& m) {
        if (muteSend) return;
        if (sendRaw) sendRaw(m);
        static const juce::StringArray noSave{"noteOn", "noteOff", "panic", "springKick"};
        if (ready && !noSave.contains(m["type"].toString()) && stateChanged) stateChanged();
    }
    /** {type: t, v: value} */
    void sendV(const String& type, const var& v) { send(obj({{"type", type}, {"v", v}})); }
    /** {type: t, ...all properties of the object at path} */
    void sendSpread(const String& type, const String& path,
                    std::initializer_list<std::pair<const char*, var>> extra = {}) {
        auto* o = new juce::DynamicObject();
        o->setProperty("type", type);
        for (auto& e : extra) o->setProperty(e.first, e.second);
        if (auto* src = st.get(path).getDynamicObject())
            for (auto& p : src->getProperties()) o->setProperty(p.name, p.value);
        send(var(o));
    }
    static var obj(std::initializer_list<std::pair<const char*, var>> props) {
        auto* o = new juce::DynamicObject();
        for (auto& p : props) o->setProperty(p.first, p.second);
        return var(o);
    }
};

/** Tooltip text: main text, then optionally a separator and a muted hint line (see WhLook::drawTooltip). */
inline String tipText(const String& tip, const String& hint = {}) {
    return hint.isEmpty() ? tip : tip + "\x1f" + hint;
}

using Opts = std::vector<std::pair<var, String>>;

// ---------------- Knob ----------------
struct KnobOpts {
    String label;
    std::function<var()> get;
    std::function<void(const var&)> set;
    double min = 0, max = 1;
    var def;
    std::function<String(double)> fmt;
    double step = 0;
    bool log = false;
    Opts opts; // discrete values (then min/max/fmt are unused)
    struct Auto {
        var v;                        // the value that means "auto"
        String label;                 // shown instead of a number
        std::function<double()> show; // position of the pointer while auto
    };
    std::optional<Auto> autoMode;
    bool small = false;
    String tip;
};

class Knob : public Component, public Syncable, public juce::SettableTooltipClient {
public:
    Knob(Ctx& c, KnobOpts o) : ctx(c), k(std::move(o)) {
        ctx.reg(this);
        setTooltip(tipText(k.tip, L"Drag or scroll · Shift = fine · Double-click = default"));
        setWantsKeyboardFocus(true);
        sync();
    }
    ~Knob() override { ctx.unreg(this); }
    const String& label() const { return k.label; }
    void setTip(const String& t) {
        k.tip = t;
        setTooltip(tipText(t, L"Drag or scroll · Shift = fine · Double-click = default"));
    }
    bool hasTip() const { return k.tip.isNotEmpty(); }
    bool isSmall() const { return k.small; }
    int preferredHeight() const { return k.small ? 64 : 84; }
    float labelSize = 0;             // 0 = default; card may shrink labels to fit (fitKnobLabels)
    std::function<bool()> visibleFn; // hidden knobs leave no gap
    std::function<String()> labelFn; // label that depends on the state
    bool shownNow() const { return !visibleFn || visibleFn(); }

    void sync() override {
        if (labelFn) k.label = labelFn();
        const var v = k.get();
        const bool isAuto = k.autoMode && v == k.autoMode->v;
        frac = juce::jlimit(0.0, 1.0, isAuto ? toF(k.autoMode->show()) : toF(v));
        if (isAuto)
            valueText = k.autoMode->label;
        else if (!k.opts.empty()) {
            valueText = v.toString();
            for (auto& [x, t] : k.opts)
                if (x == v) valueText = t;
        } else
            valueText = k.fmt ? k.fmt((double) v) : String((double) v, 2);
        repaint();
    }
    void reset() override {
        k.set(k.def);
        sync();
    }

    void paint(Graphics& g) override {
        const float D = k.small ? 34.f : 46.f, C = k.small ? 26.f : 35.f;
        const float cx = getWidth() / 2.f, top = 0;
        auto dial = juce::Rectangle<float>(D, D).withCentre({cx, top + D / 2});
        // ribbed outer ring (repeating conic gradient, 7 deg stripes)
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 6, {0, 3}).drawForRectangle(g, dial.toNearestInt());
        g.setColour(Colour(0xff1a1614));
        g.fillEllipse(dial);
        g.setColour(Colour(0xff2c2622));
        for (int i = 0; i < 360; i += 14) {
            juce::Path seg;
            seg.addPieSegment(dial, juce::degreesToRadians((float) i), juce::degreesToRadians((float) i + 7), 0);
            g.fillPath(seg);
        }
        auto cap = juce::Rectangle<float>(C, C).withCentre(dial.getCentre());
        g.setGradientFill(juce::ColourGradient(Colour(0xff5a524c), cap.getX() + C * 0.35f, cap.getY() + C * 0.3f,
                                               Colour(0xff1b1715), cap.getX() + C * 0.35f + C * 0.72f,
                                               cap.getY() + C * 0.3f, true));
        g.fillEllipse(cap);
        // pointer
        const float ang = juce::degreesToRadians(-135.f + 270.f * (float) frac);
        const float pl = k.small ? 8.f : 11.f, pt = k.small ? 3.f : 4.f;
        juce::Path ptr;
        ptr.addRoundedRectangle(-1.5f, -C / 2 + pt, 3.f, pl, 1.5f);
        g.setColour(col::gold);
        g.fillPath(ptr, juce::AffineTransform::rotation(ang).translated(cap.getCentreX(), cap.getCentreY()));
        if (hasKeyboardFocus(false)) {
            g.setColour(col::amber);
            g.drawEllipse(dial.expanded(3), 2);
        }
        // label + value
        const float ls = labelSize > 0 ? labelSize : (k.small ? 10.f : 11.f);
        const float y1 = D + (k.small ? 2.f : 4.f);
        drawSpaced(g, k.label, {0, y1, (float) getWidth(), ls + 3}, Fonts::make(ls), labelSize > 0 ? 0.f : 0.5f,
                   juce::Justification::centred, true);
        g.setColour(col::text);
        const float vs = k.small ? 11.5f : 12.5f;
        g.setFont(Fonts::make(vs));
        g.drawText(valueText,
                   juce::Rectangle<float>(0, y1 + ls + 3 + (k.small ? 2.f : 4.f), (float) getWidth(), vs + 3),
                   juce::Justification::centred, false);
    }
    float labelWidth(float size) const {
        return spacedWidth(k.label, Fonts::make(size), size >= 11 ? 0.5f : 0.f, true);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragY = e.position.y;
        dragF = cur();
        grabKeyboardFocus();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const float sens = e.mods.isShiftDown() ? 600.f : 160.f;
        apply(dragF + (dragY - e.position.y) / sens);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const int n = (int) k.opts.size();
        const double d = w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0;
        apply(cur() + d * (n > 1 ? 1.0 / (n - 1) : e.mods.isShiftDown() ? 0.005 : 0.025));
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { reset(); }
    bool keyPressed(const juce::KeyPress& key) override {
        const int kc = key.getKeyCode();
        const int d = (kc == juce::KeyPress::upKey || kc == juce::KeyPress::rightKey)    ? 1
                      : (kc == juce::KeyPress::downKey || kc == juce::KeyPress::leftKey) ? -1
                                                                                         : 0;
        if (!d) return false;
        const int n = (int) k.opts.size();
        apply(cur() + d * (n > 1 ? 1.0 / (n - 1) : 0.025));
        return true;
    }

private:
    double toF(const var& v) const {
        const int n = (int) k.opts.size();
        if (n > 0) {
            int idx = 0;
            for (int i = 0; i < n; i++)
                if (k.opts[(size_t) i].first == v) idx = i;
            return n > 1 ? idx / (double) (n - 1) : 0;
        }
        const double x = (double) v;
        return k.log ? std::log(x / k.min) / std::log(k.max / k.min) : (x - k.min) / (k.max - k.min);
    }
    var fromF(double f) const {
        f = juce::jlimit(0.0, 1.0, f);
        const int n = (int) k.opts.size();
        if (n > 0) return k.opts[(size_t) juce::roundToInt(f * (n - 1))].first;
        double v = k.log ? k.min * std::pow(k.max / k.min, f) : k.min + f * (k.max - k.min);
        if (k.step > 0) v = std::round(v / k.step) * k.step;
        return std::round(v * 1e6) / 1e6;
    }
    double cur() const {
        const var v = k.get();
        return k.autoMode && v == k.autoMode->v ? toF(k.autoMode->show()) : toF(v);
    }
    void apply(double f) {
        const var v = fromF(f);
        if (v != k.get()) {
            k.set(v);
            sync();
        }
    }
    Ctx& ctx;
    KnobOpts k;
    double frac = 0;
    String valueText;
    float dragY = 0;
    double dragF = 0;
};

// ---------------- Segmented buttons ----------------
class Seg : public Component, public Syncable, public juce::SettableTooltipClient {
public:
    Seg(Ctx& c, Opts o, std::function<var()> g, std::function<void(const var&)> s, var d = {}, float fontPx = 13.5f)
        : ctx(c), opts(std::move(o)), get(std::move(g)), set(std::move(s)), def(std::move(d)), px(fontPx) {
        ctx.reg(this);
        sync();
    }
    ~Seg() override { ctx.unreg(this); }
    void sync() override {
        const var v = get();
        int idx = -1;
        for (int i = 0; i < (int) opts.size(); i++)
            if (opts[(size_t) i].first == v) idx = i;
        if (idx != sel) {
            sel = idx;
            repaint();
        }
    }
    void reset() override {
        if (!def.isVoid()) {
            set(def);
            sync();
        }
    }
    bool compact = false;    // header segmented control (.oseg) style
    bool equalWidths = true; // flex: 1 buttons; else sized to the text
    float padX = 3, padBox = 2;
    int preferredWidth() const {
        float w = 2 * padBox;
        for (auto& o : opts) w += spacedWidth(o.second, Fonts::make(px, 700), 0) + 2 * (compact ? 10 : 10) + padBox;
        return (int) std::ceil(w);
    }
    void paint(Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(compact ? col::field : col::segBg);
        g.fillRoundedRectangle(r, compact ? 7.f : 6.f);
        g.setColour(compact ? col::fieldLine : col::segLine);
        g.drawRoundedRectangle(r.reduced(0.5f), compact ? 7.f : 6.f, 1);
        auto cells = cellRects();
        for (int i = 0; i < (int) opts.size(); i++) {
            auto cr = cells[(size_t) i];
            if (i == sel) {
                g.setColour(col::amber);
                g.fillRoundedRectangle(cr, compact ? 5.f : 4.f);
            } else if (i == hover) {
                g.setColour(juce::Colours::white.withAlpha(0.05f));
                g.fillRoundedRectangle(cr, compact ? 5.f : 4.f);
            }
            g.setColour(i == sel ? col::onAmber : col::text);
            g.setFont(Fonts::make(px, i == sel ? 700 : 500));
            g.drawText(opts[(size_t) i].second, cr, juce::Justification::centred, false);
        }
    }
    void mouseMove(const juce::MouseEvent& e) override { setHover(cellAt(e.position)); }
    void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
    void mouseUp(const juce::MouseEvent& e) override {
        const int i = cellAt(e.position);
        if (i >= 0 && e.mouseWasClicked()) {
            set(opts[(size_t) i].first);
            sync();
        }
    }
    void setOptions(Opts o) {
        opts = std::move(o);
        sel = -2;
        sync();
        repaint();
    }

private:
    std::vector<juce::Rectangle<float>> cellRects() const {
        std::vector<juce::Rectangle<float>> out;
        auto r = getLocalBounds().toFloat().reduced(padBox + 1);
        const int n = (int) opts.size();
        if (n == 0) return out;
        const float gap = padBox;
        if (equalWidths) {
            const float w = (r.getWidth() - gap * (n - 1)) / n;
            for (int i = 0; i < n; i++) out.push_back({r.getX() + i * (w + gap), r.getY(), w, r.getHeight()});
        } else {
            std::vector<float> ws;
            float tot = 0;
            for (auto& o : opts) {
                ws.push_back(spacedWidth(o.second, Fonts::make(px, 700), 0) + 20);
                tot += ws.back();
            }
            const float scale = (r.getWidth() - gap * (n - 1)) / tot;
            float x = r.getX();
            for (int i = 0; i < n; i++) {
                out.push_back({x, r.getY(), ws[(size_t) i] * scale, r.getHeight()});
                x += ws[(size_t) i] * scale + gap;
            }
        }
        return out;
    }
    int cellAt(juce::Point<float> p) const {
        auto cells = cellRects();
        for (int i = 0; i < (int) cells.size(); i++)
            if (cells[(size_t) i].expanded(1).contains(p)) return i;
        return -1;
    }
    void setHover(int i) {
        if (i != hover) {
            hover = i;
            repaint();
        }
    }
    Ctx& ctx;
    Opts opts;
    std::function<var()> get;
    std::function<void(const var&)> set;
    var def;
    float px;
    int sel = -2, hover = -1;
};

// ---------------- Rows: label + control ----------------
class Row : public Component, public juce::SettableTooltipClient {
public:
    Row(const String& l, std::unique_ptr<Component> ctl) : label(l), control(std::move(ctl)) {
        if (control) addAndMakeVisible(*control);
    }
    String label;
    std::unique_ptr<Component> control;
    std::function<String()> valueText; // read-only value row
    std::function<bool()> visible;
    int labelWidth = 92;
    void paint(Graphics& g) override {
        if (label.isNotEmpty()) {
            g.setColour(col::muted);
            g.setFont(Fonts::make(13.5f));
            g.drawText(label, juce::Rectangle<float>(0, 0, (float) labelWidth, (float) getHeight()),
                       juce::Justification::centredLeft, true);
        }
        if (valueText) {
            g.setColour(col::gold);
            g.setFont(Fonts::make(14.f));
            g.drawText(valueText(),
                       getLocalBounds().toFloat().withTrimmedLeft(label.isNotEmpty() ? labelWidth + 10.f : 0),
                       juce::Justification::centredLeft, true);
        }
    }
    void resized() override {
        if (!control) return;
        auto r = getLocalBounds();
        if (label.isNotEmpty()) r.removeFromLeft(labelWidth + 10);
        if (fitControl) {
            const int w = juce::jmin(r.getWidth(), fitControl());
            control->setBounds(r.withWidth(w).withSizeKeepingCentre(w, juce::jmin(r.getHeight(), 30)));
        } else
            control->setBounds(r.withSizeKeepingCentre(r.getWidth(), juce::jmin(r.getHeight(), 30)));
    }
    std::function<int()> fitControl; // natural width of the control (buttons), else it fills the row
};

/** A value row that refreshes on sync. */
class ValueRow : public Row, public Syncable {
public:
    ValueRow(Ctx& c, const String& l, std::function<String()> text) : Row(l, nullptr), ctx(c) {
        valueText = std::move(text);
        ctx.reg(this);
    }
    ~ValueRow() override { ctx.unreg(this); }
    void sync() override { repaint(); }
    Ctx& ctx;
};

/** Styled push button. */
class Button : public juce::Button {
public:
    Button(const String& text, bool primaryStyle = false, float fontPx = 13.5f)
        : juce::Button(text), primary(primaryStyle), px(fontPx) {}
    bool primary;
    float px;
    bool toggledOn = false;
    float radius = 6;
    int naturalWidth() const {
        return (int) std::ceil(spacedWidth(getButtonText(), Fonts::make(px, primary ? 700 : 500), 0.3f)) +
               (primary ? 28 : 24);
    }
    void paintButton(Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        const bool on = primary || toggledOn;
        drawField(g, r, on, over || down, radius);
        g.setColour(on ? col::onAmber : col::text);
        drawSpaced(g, getButtonText(), r, Fonts::make(px, on ? 700 : 500), 0.3f, juce::Justification::centred);
    }
};

/** Select (drop-down) with the UI's look. Options may change (function), values are vars. */
class Select : public juce::ComboBox, public Syncable {
public:
    Select(Ctx& c, std::function<Opts()> optsFn, std::function<var()> g, std::function<void(const var&)> s, var d = {})
        : ctx(c), opts(std::move(optsFn)), get(std::move(g)), set(std::move(s)), def(std::move(d)) {
        ctx.reg(this);
        onChange = [this] {
            if (filling) return;
            const int i = getSelectedId() - 1;
            if (i >= 0 && i < (int) current.size()) {
                set(current[(size_t) i].first);
                ctx.syncAll();
            }
        };
        sync();
    }
    ~Select() override { ctx.unreg(this); }
    void sync() override {
        auto list = opts();
        bool same = list.size() == current.size();
        for (size_t i = 0; same && i < list.size(); i++)
            same = list[i].first == current[i].first && list[i].second == current[i].second;
        filling = true;
        if (!same) {
            current = list;
            clear(juce::dontSendNotification);
            for (int i = 0; i < (int) current.size(); i++) addItem(current[(size_t) i].second, i + 1);
        }
        const var v = get();
        int id = 0;
        for (int i = 0; i < (int) current.size(); i++)
            if (current[(size_t) i].first.toString() == v.toString()) id = i + 1;
        setSelectedId(id, juce::dontSendNotification);
        filling = false;
    }
    void reset() override {
        if (!def.isVoid()) {
            set(def);
            sync();
        }
    }
    float fontPx = 14;

private:
    Ctx& ctx;
    std::function<Opts()> opts;
    std::function<var()> get;
    std::function<void(const var&)> set;
    var def;
    Opts current;
    bool filling = false;
};

// ---------------- Row factories (same names as the web UI) ----------------
inline std::unique_ptr<Row> rowSeg(Ctx& c, const String& label, Opts opts, std::function<var()> get,
                                   std::function<void(const var&)> set, var def = {}) {
    return std::make_unique<Row>(label, std::make_unique<Seg>(c, std::move(opts), std::move(get), std::move(set), def));
}
inline std::unique_ptr<Row> rowSel(Ctx& c, const String& label, std::function<Opts()> opts, std::function<var()> get,
                                   std::function<void(const var&)> set, var def = {}) {
    return std::make_unique<Row>(label,
                                 std::make_unique<Select>(c, std::move(opts), std::move(get), std::move(set), def));
}
inline std::unique_ptr<Row> rowSel(Ctx& c, const String& label, Opts opts, std::function<var()> get,
                                   std::function<void(const var&)> set, var def = {}) {
    return rowSel(c, label, [opts] { return opts; }, std::move(get), std::move(set), def);
}
inline std::unique_ptr<Row> rowVal(Ctx& c, const String& label, std::function<String()> text) {
    return std::make_unique<ValueRow>(c, label, std::move(text));
}
inline std::unique_ptr<Row> rowBtn(const String& label, const String& text, std::function<void()> onClick) {
    auto b = std::make_unique<Button>(text);
    b->onClick = std::move(onClick);
    auto* bp = b.get();
    auto r = std::make_unique<Row>(label, std::move(b));
    r->fitControl = [bp] {
        return bp->naturalWidth();
    };
    return r;
}

// ---------------- Section (a box on a card) ----------------
struct SecOpts {
    std::unique_ptr<Component> head; // control in the title line (e.g. a segmented switch)
    int kcols = 4, rcols = 1, span = 1;
    std::unique_ptr<Component> extra; // custom component below the rows
    int extraHeight = 0;
    std::function<bool()> visible;
    String id;
};

class Section : public Component, public Syncable {
public:
    Section(Ctx& c, const String& t, std::vector<std::unique_ptr<Knob>> ks, std::vector<std::unique_ptr<Row>> rs,
            std::function<String()> noteFn, SecOpts o, const std::map<String, String>& tips)
        : ctx(c), title(t), note(std::move(noteFn)), opt(std::move(o)) {
        for (auto& k : ks) {
            addAndMakeVisible(*k);
            if (!k->hasTip()) k->setTip(lookupTip(tips, k->label()));
            knobs.push_back(std::move(k));
        }
        for (auto& r : rs) {
            addAndMakeVisible(*r);
            if (r->getTooltip().isEmpty() && r->label.isNotEmpty()) r->setTooltip(lookupTip(tips, r->label));
            if (auto* st = dynamic_cast<juce::SettableTooltipClient*>(r->control.get());
                st && st->getTooltip().isEmpty())
                st->setTooltip(r->getTooltip());
            rows.push_back(std::move(r));
        }
        if (opt.head) {
            addAndMakeVisible(*opt.head);
            if (auto* st = dynamic_cast<juce::SettableTooltipClient*>(opt.head.get()); st && st->getTooltip().isEmpty())
                st->setTooltip(lookupTip(tips, "head"));
        }
        if (opt.extra) addAndMakeVisible(*opt.extra);
        ctx.reg(this);
        if (note) noteText = note();
    }
    ~Section() override { ctx.unreg(this); }
    const String id() const { return opt.id; }
    int span() const { return opt.span; }
    bool shown() const { return !opt.visible || opt.visible(); }
    void sync() override {
        if (note) {
            const String n = note();
            if (n != noteText) {
                noteText = n;
                resized();
                if (getParentComponent()) getParentComponent()->resized();
                repaint();
            }
        }
        bool changed = false;
        for (auto& r : rows) {
            const bool v = !r->visible || r->visible();
            if (r->isVisible() != v) {
                r->setVisible(v);
                changed = true;
            }
        }
        for (auto& k : knobs)
            if (k->isVisible() != k->shownNow()) changed = true;
        if (changed) {
            resized();
            if (getParentComponent()) getParentComponent()->resized();
        }
    }
    std::vector<Knob*> knobList() const {
        std::vector<Knob*> v;
        for (auto& k : knobs) v.push_back(k.get());
        return v;
    }

    int heightFor(int width) const {
        int h = 12 + 16; // padding top + title line
        const auto vk = visibleKnobs();
        if (!vk.empty()) {
            const int rowsN = ((int) vk.size() + opt.kcols - 1) / opt.kcols;
            int kh = 0;
            for (auto* k : vk) kh = juce::jmax(kh, k->preferredHeight());
            h += 10 + rowsN * kh + (rowsN - 1) * 10;
        }
        const int nRows = visibleRows();
        if (nRows > 0) {
            const int lines = (nRows + opt.rcols - 1) / opt.rcols;
            h += 10 + lines * 30 + (lines - 1) * 8;
        }
        if (noteText.isNotEmpty()) h += 10 + noteHeight(width - 28);
        if (opt.extra) h += 10 + opt.extraHeight;
        return h + 14;
    }
    void paint(Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(col::section);
        g.fillRoundedRectangle(r, 6);
        g.setColour(col::sectionLine);
        g.drawRoundedRectangle(r.reduced(0.5f), 6, 1);
        g.setColour(col::text);
        drawSpaced(g, title, {14, 12, (float) getWidth() - 28, 16}, Fonts::make(12.5f, 700), 1.6f,
                   juce::Justification::centredLeft, true);
        if (noteText.isNotEmpty()) {
            g.setColour(col::muted);
            g.setFont(Fonts::make(12.5f));
            juce::AttributedString as;
            as.append(noteText, Fonts::make(12.5f), col::muted);
            as.setLineSpacing(3.5f);
            juce::TextLayout tl;
            tl.createLayout(as, (float) getWidth() - 28);
            tl.draw(g, juce::Rectangle<float>(14, (float) noteY, (float) getWidth() - 28, tl.getHeight()));
        }
    }
    void resized() override {
        const int W = getWidth() - 28;
        int y = 12;
        if (opt.head) {
            const int hw = juce::jmin(W / 2 + 40, preferredHeadWidth());
            opt.head->setBounds(14 + W - hw, y - 4, hw, 24);
        }
        y += 16;
        const auto vk = visibleKnobs();
        for (auto& k : knobs) k->setVisible(k->shownNow());
        if (!vk.empty()) {
            y += 10;
            const int cols = opt.kcols, gapX = 4;
            const float cw = (W - gapX * (cols - 1)) / (float) cols;
            int kh = 0;
            for (auto* k : vk) kh = juce::jmax(kh, k->preferredHeight());
            for (int i = 0; i < (int) vk.size(); i++) {
                const int cxi = i % cols, ryi = i / cols;
                vk[(size_t) i]->setBounds((int) (14 + cxi * (cw + gapX)), y + ryi * (kh + 10), (int) cw, kh);
            }
            const int rowsN = ((int) vk.size() + cols - 1) / cols;
            y += rowsN * kh + (rowsN - 1) * 10;
        }
        const int nRows = visibleRows();
        if (nRows > 0) {
            y += 10;
            const int cols = opt.rcols, gapX = 18;
            const float cw = (W - gapX * (cols - 1)) / (float) cols;
            int i = 0;
            for (auto& r : rows) {
                if (!r->isVisible()) continue;
                r->setBounds((int) (14 + (i % cols) * (cw + gapX)), y + (i / cols) * 38, (int) cw, 30);
                i++;
            }
            const int lines = (nRows + cols - 1) / cols;
            y += lines * 30 + (lines - 1) * 8;
        }
        if (noteText.isNotEmpty()) {
            y += 10;
            noteY = y;
            y += noteHeight(W);
        }
        if (opt.extra) {
            y += 10;
            opt.extra->setBounds(14, y, W, juce::jmax(opt.extraHeight, getHeight() - y - 14));
        }
    }

private:
    // Tooltip for a control of this section: "Title/Label", then "Title/<any>", then "<any>/Label"
    String lookupTip(const std::map<String, String>& tips, const String& label) const {
        for (auto key : {title + "/" + label, title + "/*", "*/" + label})
            if (auto it = tips.find(key); it != tips.end()) return it->second;
        return {};
    }
    std::vector<Knob*> visibleKnobs() const {
        std::vector<Knob*> v;
        for (auto& k : knobs)
            if (k->shownNow()) v.push_back(k.get());
        return v;
    }
    int visibleRows() const {
        int n = 0;
        for (auto& r : rows) n += (!r->visible || r->visible()) ? 1 : 0;
        return n;
    }
    int noteHeight(int w) const {
        juce::AttributedString as;
        as.append(noteText, Fonts::make(12.5f), col::muted);
        as.setLineSpacing(3.5f);
        juce::TextLayout tl;
        tl.createLayout(as, (float) w);
        return (int) std::ceil(tl.getHeight());
    }
    int preferredHeadWidth() const {
        if (auto* s = dynamic_cast<Seg*>(opt.head.get())) return s->preferredWidth();
        return 160;
    }
    Ctx& ctx;
    String title;
    std::function<String()> note;
    String noteText;
    int noteY = 0;
    SecOpts opt;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Row>> rows;

    friend class Page;

public:
    String getTitle() const { return title; }
    const std::vector<std::unique_ptr<Row>>& rowList() const { return rows; }
    Component* headComponent() const { return opt.head.get(); }
};

// ---------------- Page: the sections of one card in a grid ----------------
class Page : public Component {
public:
    explicit Page(int columns) : cols(columns) {}
    void add(std::unique_ptr<Section> s) {
        addAndMakeVisible(*s);
        secs.push_back(std::move(s));
    }
    std::vector<Section*> sections() const {
        std::vector<Section*> v;
        for (auto& s : secs) v.push_back(s.get());
        return v;
    }
    int layoutHeight = 0;
    void resized() override {
        const int gap = 14;
        const float cw = (getWidth() - gap * (cols - 1)) / (float) cols;
        // CSS grid auto-placement (row major); every section of a grid row gets the row's height
        struct Cell {
            Section* s;
            int col, row, span;
        };
        std::vector<Cell> cells;
        int c = 0, r = 0;
        for (auto& s : secs) {
            if (!s->shown()) {
                s->setVisible(false);
                continue;
            }
            s->setVisible(true);
            const int sp = juce::jmin(cols, s->span());
            if (c + sp > cols) {
                c = 0;
                r++;
            }
            cells.push_back({s.get(), c, r, sp});
            c += sp;
            if (c >= cols) {
                c = 0;
                r++;
            }
        }
        std::map<int, int> rowH;
        for (auto& cl : cells) {
            const int w = (int) (cw * cl.span + gap * (cl.span - 1));
            rowH[cl.row] = juce::jmax(rowH[cl.row], cl.s->heightFor(w));
        }
        std::map<int, int> rowY;
        int y = 0;
        for (auto& [row, h] : rowH) {
            rowY[row] = y;
            y += h + gap;
        }
        layoutHeight = juce::jmax(0, y - gap);
        for (auto& cl : cells)
            cl.s->setBounds((int) (cl.col * (cw + gap)), rowY[cl.row], (int) (cw * cl.span + gap * (cl.span - 1)),
                            rowH[cl.row]);
        fitKnobLabels();
    }

private:
    /** Long knob labels next to each other: if any two neighbours would touch, shrink all labels of the page. */
    void fitKnobLabels() {
        std::vector<Knob*> ks;
        for (auto& s : secs)
            if (s->isVisible())
                for (auto* k : s->knobList())
                    if (!k->isSmall()) ks.push_back(k);
        auto clash = [&](float size) {
            for (auto* k : ks)
                if (k->labelWidth(size) + 3 > k->getWidth()) return true;
            return false;
        };
        float size = 0;
        if (clash(11)) {
            size = 8.5f;
            for (float fs = 11; fs >= 8.5f; fs -= 0.5f)
                if (!clash(fs)) {
                    size = fs;
                    break;
                }
        }
        for (auto* k : ks)
            if (k->labelSize != size) {
                k->labelSize = size;
                k->repaint();
            }
    }
    int cols;
    std::vector<std::unique_ptr<Section>> secs;
};

// ---------------- Look and feel: selects, popups, tooltips ----------------
class WhLook : public juce::LookAndFeel_V4 {
public:
    WhLook() {
        setColour(juce::PopupMenu::backgroundColourId, col::field);
        setColour(juce::PopupMenu::textColourId, col::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::amber);
        setColour(juce::PopupMenu::highlightedTextColourId, col::onAmber);
        setColour(juce::ComboBox::textColourId, col::text);
        setColour(juce::TooltipWindow::backgroundColourId, col::field);
        setColour(juce::TooltipWindow::textColourId, col::text);
        setColour(juce::TooltipWindow::outlineColourId, col::tipLine);
        setColour(juce::ScrollBar::thumbColourId, col::fieldLine);
    }
    void drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
        g.setColour(col::field);
        g.fillRoundedRectangle(r, 5);
        g.setColour(box.isMouseOver(true) ? col::fieldHover : col::fieldLine);
        g.drawRoundedRectangle(r, 5, 1);
        juce::Path arrow;
        const float ax = (float) w - 14, ay = (float) h / 2;
        arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour(col::muted);
        g.fillPath(arrow);
    }
    juce::Font getComboBoxFont(juce::ComboBox& b) override {
        if (auto* s = dynamic_cast<Select*>(&b)) return Fonts::make(s->fontPx);
        return Fonts::make(14);
    }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(4, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
    juce::Font getPopupMenuFont() override { return Fonts::make(15); }
    void drawPopupMenuBackground(Graphics& g, int w, int h) override {
        g.fillAll(col::field);
        g.setColour(col::fieldLine);
        g.drawRect(0, 0, w, h);
    }
    juce::Rectangle<int> getTooltipBounds(const String& tip, juce::Point<int> pos,
                                          juce::Rectangle<int> parent) override {
        auto tl = layoutTip(tip, 300);
        const int w = (int) std::ceil(tl.getWidth()) + 22, h = (int) std::ceil(tl.getHeight()) + 17;
        int x = pos.x - w / 2, y = pos.y + 18;
        x = juce::jlimit(parent.getX() + 6, parent.getRight() - w - 6, x);
        if (y + h > parent.getBottom() - 6) y = pos.y - h - 12;
        return {x, y, w, h};
    }
    void drawTooltip(Graphics& g, const String& tip, int w, int h) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h);
        g.setColour(col::field);
        g.fillRoundedRectangle(r, 5);
        g.setColour(col::tipLine);
        g.drawRoundedRectangle(r.reduced(0.5f), 5, 1);
        auto tl = layoutTip(tip, 300);
        tl.draw(g, r.reduced(11, 8).withTrimmedTop(-1));
    }

private:
    static juce::TextLayout layoutTip(const String& tip, float maxW) {
        juce::AttributedString as;
        const String main = tip.upToFirstOccurrenceOf("\x1f", false, false);
        const String hint = tip.fromFirstOccurrenceOf("\x1f", false, false);
        as.append(main, Fonts::make(13.5f), col::text);
        if (hint.isNotEmpty()) as.append("\n" + hint, Fonts::make(12.5f), col::muted);
        as.setLineSpacing(1.5f);
        juce::TextLayout tl;
        tl.createLayout(as, maxW);
        float w = 0;
        for (auto& line : tl) w = juce::jmax(w, line.getLineBoundsX().getLength());
        juce::TextLayout fit;
        fit.createLayout(as, std::ceil(w) + 1);
        return fit;
    }
};

} // namespace wh
