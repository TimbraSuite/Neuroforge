#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include <cmath>

namespace nfui {

//==============================================================================================
// Design tokens. Every colour, type size and spacing value used by the UI comes from here.
//==============================================================================================
namespace tok {
// ---- surfaces (dark graphite, darkest to lightest) ----
inline const juce::Colour bg       { 0xff0c0e11 };  // window background
inline const juce::Colour panel    { 0xff14171c };  // main panels
inline const juce::Colour card     { 0xff1a1e24 };  // FX cards, pass columns, source cards
inline const juce::Colour raised   { 0xff20252c };  // buttons, combos, inputs
inline const juce::Colour well     { 0xff0a0c0f };  // graphs and displays
inline const juce::Colour edge     { 0xff2c323b };  // control outlines
inline const juce::Colour edgeSoft { 0xff23282f };  // subtle separators
inline const juce::Colour track    { 0xff2a2f37 };  // inactive knob/slider track

// ---- text ----
inline const juce::Colour textPrimary   { 0xffe6e9ee }; // titles, values
inline const juce::Colour textLabel     { 0xffa9b2be }; // parameter labels
inline const juce::Colour textSecondary { 0xff7d8794 }; // metadata, inactive tabs
inline const juce::Colour textDisabled  { 0xff4b535e };

// ---- accents ----
inline const juce::Colour accent    { 0xffb4ff3c }; // acid green: values, active state, primary actions
inline const juce::Colour accentDim { 0xff6f9a2c }; // values of inactive/disabled controls
inline const juce::Colour danger    { 0xffff5470 };
inline const juce::Colour focus     { 0xffe6e9ee }; // keyboard focus ring

// ---- modulation source colours ----
inline const juce::Colour modEnv    { 0xffff5fa2 }; // envelopes: pink
inline const juce::Colour modLfo    { 0xff3cf0ff }; // LFOs: cyan
inline const juce::Colour modMotion { 0xffb48cff }; // pass Motion: purple
inline const juce::Colour modMacro  { 0xffb4ff3c }; // macros: acid green
inline const juce::Colour modStep   { 0xffffb23c }; // curve steppers: amber
inline const juce::Colour modOther  { 0xff9aa6b4 }; // velocity, note, wheel, ...

// ---- type scale (font height in px at 100% UI scale) ----
constexpr float fsLogo    = 16.f;
constexpr float fsSection = 14.f;  // panel and pass headings
constexpr float fsModule  = 13.f;  // FX / source card titles
constexpr float fsButton  = 12.5f;
constexpr float fsLabel   = 12.f;  // parameter labels
constexpr float fsValue   = 12.f;  // parameter values (tabular digits)
constexpr float fsMeta    = 11.f;  // secondary information
constexpr float fsBadge   = 10.f;  // modulation badges, chips

enum class Weight { Regular, Medium, SemiBold, Bold };

// ---- spacing / shape ----
constexpr int sp1 = 2, sp2 = 4, sp3 = 6, sp4 = 8, sp5 = 12;
constexpr float rCard = 5.f, rControl = 4.f;
} // namespace tok

// Legacy names, kept so older call sites read naturally; they map onto the tokens.
inline const juce::Colour cBg = tok::bg, cPanel = tok::panel, cPanel2 = tok::raised, cEdge = tok::edge,
    cText = tok::textPrimary, cDim = tok::textSecondary, cAccent = tok::accent, cCyan = tok::modLfo,
    cWarn = tok::danger, cTrack = tok::track;

// Embedded Roboto Condensed (assets/fonts). Its digits are tabular, so values don't jitter.
juce::Typeface::Ptr typeface(tok::Weight w);
juce::Font font(float height, tok::Weight w);
inline juce::Font font(float height, bool bold = false) { return font(height, bold ? tok::Weight::SemiBold : tok::Weight::Medium); }

// Named type styles
inline juce::Font fSection() { return font(tok::fsSection, tok::Weight::Bold); }
inline juce::Font fModule()  { return font(tok::fsModule, tok::Weight::SemiBold); }
inline juce::Font fButton()  { return font(tok::fsButton, tok::Weight::SemiBold); }
inline juce::Font fLabel()   { return font(tok::fsLabel, tok::Weight::Medium); }
inline juce::Font fValue()   { return font(tok::fsValue, tok::Weight::SemiBold); }
inline juce::Font fMeta()    { return font(tok::fsMeta, tok::Weight::Regular); }
inline juce::Font fBadge()   { return font(tok::fsBadge, tok::Weight::Bold); }

// Component properties that select a style variant in the LookAndFeel.
//   TextButton: "nfStyle" = "tab" (category tab), "pill" (source selector, colour in "nfColour"),
//                           "ghost" (low-emphasis, e.g. empty FX slot / remove), default = standard
//   ComboBox:   "nfStyle" = "title" (borderless card title)
inline void setStyle(juce::Component& c, const char* style) { c.getProperties().set("nfStyle", style); }
inline juce::String styleOf(const juce::Component& c) { return c.getProperties()["nfStyle"].toString(); }

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox& b) override { return styleOf(b) == "title" ? fModule() : fLabel(); }
    juce::Font getPopupMenuFont() override { return font(13.f, tok::Weight::Medium); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return fButton(); }
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                       int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
    juce::Font getAlertWindowMessageFont() override { return fLabel(); }
    juce::Font getAlertWindowTitleFont() override { return fModule(); }
};

// ---- Theme assets (embedded) -----------------------------------------------------------------
inline const juce::Image& logoImage()
{
    static const juce::Image img = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
    return img;
}
inline const juce::Image& metalImage()
{
    static const juce::Image img = juce::ImageCache::getFromMemory(BinaryData::metal_tile_png, BinaryData::metal_tile_pngSize);
    return img;
}

// How strongly the worn-metal texture shows. Deliberately subtle: raise carefully.
constexpr float kMetalOpacity = 0.14f;

// Worn-metal overlay: brushed grain, faint scratches, rare rust. `seed` shifts the tile so
// neighbouring panels don't show the same patch of texture.
inline void paintMetal(juce::Graphics& g, juce::Rectangle<float> r, float corner, int seed, float opacity = kMetalOpacity)
{
    const auto& img = metalImage();
    if (!img.isValid()) return;
    const float ox = (float) ((seed * 7919) % img.getWidth()), oy = (float) ((seed * 104729) % img.getHeight());
    g.setTiledImageFill(img, (int) -ox, (int) -oy, opacity);
    g.fillRoundedRectangle(r, corner);
}

// A small slotted screw head, like the bolts on the logo's metal letters.
inline void paintScrew(juce::Graphics& g, juce::Point<float> c, float radius, float angle)
{
    juce::ColourGradient grad(juce::Colour(0xff4a5260), c.x - radius * 0.6f, c.y - radius * 0.6f,
                              juce::Colour(0xff15181d), c.x + radius, c.y + radius, true);
    g.setGradientFill(grad);
    g.fillEllipse(c.x - radius, c.y - radius, radius * 2.f, radius * 2.f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawEllipse(c.x - radius, c.y - radius, radius * 2.f, radius * 2.f, 0.6f);
    const float dx = std::cos(angle) * radius * 0.7f, dy = std::sin(angle) * radius * 0.7f;
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.drawLine(c.x - dx, c.y - dy, c.x + dx, c.y + dy, 0.9f);
}

// Group-box painter shared by panels: dark plate + worn-metal grain + bevel + corner screws.
inline void paintPanel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title = {})
{
    const auto rf = r.toFloat();
    const int seed = r.getWidth() * 31 + r.getHeight() * 17 + title.hashCode();
    g.setColour(cPanel);
    g.fillRoundedRectangle(rf, 6.f);
    paintMetal(g, rf, 6.f, seed);
    // bevel: faint light on the top edge, shadow on the bottom edge
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawHorizontalLine(r.getY() + 1, rf.getX() + 6.f, rf.getRight() - 6.f);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawHorizontalLine(r.getBottom() - 2, rf.getX() + 6.f, rf.getRight() - 6.f);
    g.setColour(cEdge);
    g.drawRoundedRectangle(rf.reduced(0.5f), 6.f, 1.f);
    // screws in the corners of larger plates (hidden where controls sit on top of them)
    if (r.getWidth() > 160 && r.getHeight() > 60) {
        const float in = 5.5f, rad = 2.6f;
        const float a = (float) (seed % 628) * 0.01f;
        paintScrew(g, { rf.getX() + in, rf.getY() + in }, rad, a);
        paintScrew(g, { rf.getRight() - in, rf.getY() + in }, rad, a + 1.1f);
        paintScrew(g, { rf.getX() + in, rf.getBottom() - in }, rad, a + 2.3f);
        paintScrew(g, { rf.getRight() - in, rf.getBottom() - in }, rad, a + 0.6f);
    }
    if (title.isNotEmpty()) {
        g.setColour(tok::textLabel);
        g.setFont(fSection());
        g.drawText(title, r.getX() + 14, r.getY() + 4, r.getWidth() - 28, 16, juce::Justification::left);
    }
}

} // namespace nfui
