#pragma once
// Wheelhouse UI - colours, fonts and the shared drawing helpers (wood case, inset panels, buttons, tooltips).
// The whole UI is laid out on a fixed 1600 x 900 stage that is scaled to the window, so sizes here are in stage px.
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace wh {

using juce::Colour;

namespace col {
const Colour bg{0xff0b0907};          // page background
const Colour text{0xffe8dcc4};        // main text
const Colour muted{0xffa8977c};       // labels, notes
const Colour gold{0xfff2c879};        // values, highlights
const Colour amber{0xfff2a33a};       // active buttons, accents
const Colour onAmber{0xff1a1206};     // text on amber
const Colour panel{0xff17130f};       // inset panels, card
const Colour section{0xff120f0c};     // card sections
const Colour sectionLine{0xff2a231c}; // section border
const Colour field{0xff1a1511};       // buttons, selects
const Colour fieldLine{0xff3a2f25};   // their border
const Colour fieldHover{0xff5a4a3a};
const Colour segBg{0xff0c0a08}; // segmented control background
const Colour segLine{0xff2e2620};
const Colour tipLine{0xff6b4426};
} // namespace col

/** The UI fonts, loaded once from BinaryData (Barlow Condensed 500/600/700, Yellowtail for the logo). */
struct Fonts {
    juce::Typeface::Ptr medium, semibold, bold, script;
    static Fonts& get() {
        static Fonts f;
        return f;
    }
    static juce::Font make(float px, int weight = 500) {
        auto& f = get();
        auto tf = weight >= 700 ? f.bold : weight >= 600 ? f.semibold : f.medium;
        if (tf == nullptr) return juce::Font(juce::FontOptions(px));
        return juce::Font(juce::FontOptions(tf).withHeight(px * 1.2f)); // CSS px ~ JUCE height / 1.2 for this face
    }
    static juce::Font logo(float px) {
        auto& f = get();
        if (f.script == nullptr) return juce::Font(juce::FontOptions(px));
        return juce::Font(juce::FontOptions(f.script).withHeight(px * 1.2f));
    }

private:
    static juce::Typeface::Ptr load(const char* file) {
        for (int i = 0; i < BinaryData::namedResourceListSize; i++)
            if (juce::String(BinaryData::originalFilenames[i]) == file) {
                int size = 0;
                if (auto* p = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
                    return juce::Typeface::createSystemTypefaceFor(p, (size_t) size);
            }
        return nullptr;
    }
    Fonts()
        : medium(load("BarlowCondensed-Medium.ttf")), semibold(load("BarlowCondensed-SemiBold.ttf")),
          bold(load("BarlowCondensed-Bold.ttf")), script(load("Yellowtail-Regular.ttf")) {}
};

/** Text with letter spacing (CSS letter-spacing), optionally upper-cased. */
inline void drawSpaced(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const juce::Font& f,
                       float spacing, juce::Justification j, bool upper = false) {
    const juce::String t = upper ? s.toUpperCase() : s;
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, t, 0, 0);
    for (int i = 0; i < ga.getNumGlyphs(); i++) ga.getGlyph(i).moveBy(spacing * (float) i, 0);
    auto bb = ga.getBoundingBox(0, -1, true);
    const float w = bb.getWidth() + (t.isEmpty() ? 0 : 0);
    float x = r.getX();
    if (j.testFlags(juce::Justification::horizontallyCentred))
        x = r.getCentreX() - w / 2;
    else if (j.testFlags(juce::Justification::right))
        x = r.getRight() - w;
    const float y = r.getCentreY() + (f.getAscent() - f.getDescent()) / 2;
    ga.moveRangeOfGlyphs(0, -1, x - bb.getX(), y);
    ga.draw(g);
}
inline float spacedWidth(const juce::String& s, const juce::Font& f, float spacing, bool upper = false) {
    const juce::String t = upper ? s.toUpperCase() : s;
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, t, 0, 0);
    return ga.getBoundingBox(0, -1, true).getWidth() + spacing * (float) juce::jmax(0, t.length() - 1);
}

/** The wood of the console (diagonal grain over a dark gradient), with rounded corners and a soft shadow. */
inline void drawWood(juce::Graphics& g, juce::Rectangle<float> r, float radius = 10) {
    juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 30, {0, 10}).drawForPath(g, [&] {
        juce::Path p;
        p.addRoundedRectangle(r, radius);
        return p;
    }());
    juce::Path p;
    p.addRoundedRectangle(r, radius);
    g.setGradientFill(juce::ColourGradient(Colour(0xff5a321b), r.getX(), r.getY(), Colour(0xff331a0b), r.getX(),
                                           r.getBottom(), false));
    g.fillPath(p);
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(p);
        // grain: thin vertical-ish streaks every 16 px (repeating-linear-gradient at 92 deg)
        for (float x = r.getX() - 20; x < r.getRight() + 20; x += 16) {
            g.setColour(juce::Colours::black.withAlpha(0.1f));
            g.drawLine(x + 3, r.getY(), x + 3 + r.getHeight() * 0.035f, r.getBottom(), 2.5f);
            g.setColour(Colour(0x0affd2a0));
            g.drawLine(x + 11, r.getY(), x + 11 + r.getHeight() * 0.035f, r.getBottom(), 2.0f);
        }
    }
    g.setColour(Colour(0x2effdcb4));
    g.drawHorizontalLine((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
}

/** A recessed black panel inside the wood (inset shadow). */
inline void drawInset(juce::Graphics& g, juce::Rectangle<float> r, float radius = 6) {
    juce::Path p;
    p.addRoundedRectangle(r, radius);
    g.setColour(col::panel);
    g.fillPath(p);
    juce::Graphics::ScopedSaveState s(g);
    g.reduceClipRegion(p);
    juce::Path outer;
    outer.addRectangle(r.expanded(20));
    outer.addRoundedRectangle(r, radius);
    outer.setUsingNonZeroWinding(false);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 8, {0, 2}).drawForPath(g, outer);
    g.setColour(Colour(0x1fffdcb4));
    g.drawHorizontalLine((int) r.getBottom(), r.getX() + radius, r.getRight() - radius);
}

/** Small "field" box (buttons, selects): dark fill, brown border, or amber when active. */
inline void drawField(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool hover, float radius = 6) {
    g.setColour(on ? col::amber : col::field);
    g.fillRoundedRectangle(r, radius);
    g.setColour(on ? col::amber : hover ? col::fieldHover : col::fieldLine);
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

} // namespace wh
