#pragma once
// Wheelhouse UI - header bar: logo, preset menu, tabs, listener/mics switch, output format, meters, CPU, panic.
#include "Keys.h"

namespace wh {

class Header : public Component, public Syncable {
public:
    Header(Ctx& c, Actions& a, std::function<void(const String&)> setTab, std::function<void()> panic)
        : ctx(c), act(a), onTab(std::move(setTab)),
          mode(
              c, Opts{{false, "Listener"}, {true, "Mics"}}, [&c] { return c.st.get("mics.on"); },
              [&a](const var& v) { a.setMicsOn((bool) v); }),
          out(
              c, Opts{{"stereo", "Stereo"}, {"quad", "Quad"}, {"s51", "5.1"}}, [&c] { return c.st.get("mode"); },
              [&c, &a](const var& v) {
                  c.st.set("mode", v);
                  a.applyMode();
                  c.syncAll();
              }),
          tabs(
              c, tabOpts(), [this] { return var(current); }, [this](const var& v) { onTab(v.toString()); }),
          panicBtn("Panic", false, 14) {
        ctx.reg(this);
        for (auto* s : {&mode, &out}) {
            s->compact = true;
            s->equalWidths = false;
            addAndMakeVisible(*s);
        }
        mode.setTooltip("Listener: you stand in the room and hear it on your speakers.\nMics: close mics and a room "
                        "pair through the mixer, like in the studio.");
        out.setTooltip("Output format: the room is rendered for this speaker layout.");
        tabs.compact = true;
        tabs.equalWidths = false;
        addAndMakeVisible(tabs);
        preset.onChange = [this] {
            presetChosen();
        };
        preset.setTooltip(presetTip);
        addAndMakeVisible(preset);
        panicBtn.radius = 7;
        panicBtn.onClick = std::move(panic);
        panicBtn.setTooltip(L"All notes off and reset (MIDI CC 123). Shortcut: Esc.");
        addAndMakeVisible(panicBtn);
        fillPresets();
        for (int i = 0; i < BinaryData::namedResourceListSize; i++)
            if (String(BinaryData::originalFilenames[i]) == "icon-small-256.png") {
                int size = 0;
                if (auto* p = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
                    icon = juce::ImageFileFormat::loadFrom(p, (size_t) size);
            }
    }
    ~Header() override { ctx.unreg(this); }
    String current = "spielen";
    juce::Array<var> meters; // dB per physical output
    bool showCpu = true;

    static Opts tabOpts() {
        return {{"spielen", "Play"}, {"orgel", "Organ"},    {"rotary", "Rotary"}, {"raum", "Room"},
                {"mikros", "Mics"},  {"ausgabe", "Output"}, {"midi", "MIDI"},     {"audio", "Audio"}};
    }
    void sync() override {
        tabs.sync();
        // preset menu: instrument (unchanged), registration or custom
        const String v = act.presetValue();
        int id = 1;
        if (v.startsWith("i"))
            id = 100 + v.substring(1).getIntValue();
        else if (v.isNotEmpty())
            id = 200 + v.getIntValue();
        preset.setSelectedId(id, juce::dontSendNotification);
        String tip = presetTip;
        if (v.startsWith("i"))
            tip = tables()["instruments"][v.substring(1).getIntValue()]["note"].toString() + "\n\n" + presetTip;
        preset.setTooltip(tip);
        resized();
        repaint();
    }
    void updateMeters(const juce::Array<var>& m) {
        meters = m;
        const int n = act.modeChannels();
        for (int i = 0; i < n; i++) {
            const int phys = act.physCh(i);
            const float db = phys < m.size() ? (float) (double) m[phys] : -120.f;
            hold[i] = std::max(db, hold[i] - 0.6f);
        }
        repaint(meterRect.toNearestInt().expanded(2));
    }
    void paint(Graphics& g) override {
        g.setColour(Colour(0xff2a221b));
        g.fillRect(0.f, getHeight() - 1.f, (float) getWidth(), 1.f);
        if (icon.isValid()) g.drawImage(icon, Rectangle<float>(0, (getHeight() - 38) / 2.f, 38, 38));
        g.setColour(col::gold);
        g.setFont(Fonts::logo(34));
        g.drawText("Wheelhouse", Rectangle<float>(47, 0, 180, (float) getHeight()), juce::Justification::centredLeft,
                   false);
        // meters
        const var names = act.mode()["names"];
        const int n = act.modeChannels();
        float x = meterRect.getX();
        for (int i = 0; i < n; i++) {
            auto t = Rectangle<float>(x + (14 - 8) / 2.f, meterRect.getY() + 2, 8, 24);
            g.setColour(Colour(0xff120f0c));
            g.fillRoundedRectangle(t, 2);
            g.setColour(col::fieldLine);
            g.drawRoundedRectangle(t, 2, 1);
            const float f = juce::jlimit(0.f, 1.f, (hold[i] + 60) / 60);
            g.setColour(hold[i] > -1 ? Colour(0xffe8553a) : col::amber);
            g.fillRect(t.withTop(t.getBottom() - f * t.getHeight()).reduced(1, 1));
            g.setColour(col::muted);
            g.setFont(Fonts::make(10.5f));
            g.drawText(at(names, i).toString(), Rectangle<float>(x - 4, t.getBottom() + 2, 22, 12),
                       juce::Justification::centred, false);
            x += 14 + 6;
        }
        if (showCpu) {
            g.setColour(col::muted);
            g.setFont(Fonts::make(12.5f));
            g.drawText("CPU", cpuRect.withWidth(26), juce::Justification::centredLeft, false);
            g.setColour(col::text);
            g.drawText(ctx.st.cpuText, cpuRect.withTrimmedLeft(26), juce::Justification::centredLeft, false);
        }
    }
    void resized() override {
        const int H = getHeight();
        int x = 228;
        preset.setBounds(x, (H - 32) / 2, 205, 32);
        x += 205 + 12;
        tabs.setBounds(x, (H - 36) / 2, tabs.preferredWidth() - 8, 36);
        int rx = getWidth();
        const int pw = panicBtn.naturalWidth();
        rx -= pw;
        panicBtn.setBounds(rx, (H - 34) / 2, pw, 34);
        rx -= 12;
        if (showCpu) {
            rx -= 60;
            cpuRect = {(float) rx, 0, 60, (float) H};
            rx -= 12;
        }
        const int n = act.modeChannels(), mw = n * 14 + (n - 1) * 6;
        rx -= mw;
        meterRect = {(float) rx, (H - 40) / 2.f, (float) mw, 40};
        rx -= 12;
        const int ow = out.preferredWidth() - 6;
        rx -= ow;
        out.setBounds(rx, (H - 34) / 2, ow, 34);
        rx -= 12;
        const int mdw = mode.preferredWidth() - 4;
        rx -= mdw;
        mode.setBounds(rx, (H - 34) / 2, mdw, 34);
    }

private:
    void fillPresets() {
        preset.clear(juce::dontSendNotification);
        preset.addItem("Custom", 1);
        preset.setItemEnabled(1, false);
        preset.addSectionHeading("Instrument (organ + Rotary)");
        auto* inst = tables()["instruments"].getArray();
        for (int i = 0; i < inst->size(); i++) preset.addItem(inst->getReference(i)["n"].toString(), 100 + i);
        preset.addSectionHeading("Registration");
        auto* reg = tables()["presets"].getArray();
        for (int i = 0; i < reg->size(); i++) preset.addItem(reg->getReference(i)["n"].toString(), 200 + i);
    }
    void presetChosen() {
        const int id = preset.getSelectedId();
        if (id >= 200)
            act.applyRegistration(id - 200);
        else if (id >= 100)
            act.applyInstrument(id - 100);
    }
    struct PresetBox : juce::ComboBox {
        float fontPx = 15;
    };
    Ctx& ctx;
    Actions& act;
    std::function<void(const String&)> onTab;
    Seg mode, out, tabs;
    juce::ComboBox preset;
    Button panicBtn;
    juce::Image icon;
    Rectangle<float> meterRect, cpuRect;
    float hold[8] = {-120, -120, -120, -120, -120, -120, -120, -120};
    const String presetTip = "Instruments set organ, preamp, Rotary and reverb. Registrations only set drawbars, "
                             "percussion, vibrato and drive. Room, mics, output and MIDI stay as they are.";
};

} // namespace wh
