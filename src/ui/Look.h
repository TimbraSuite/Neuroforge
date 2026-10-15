#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include <cmath>

namespace nfui {

inline const juce::Colour cBg(0xff0d0f12), cPanel(0xff15181d), cPanel2(0xff1c2027), cEdge(0xff2a2f38),
    cText(0xffd9dde3), cDim(0xff7d8794), cAccent(0xffb4ff3c), cCyan(0xff3cf0ff), cWarn(0xffff5470),
    cTrack(0xff2b3038);

inline juce::Font font(float h, bool bold = false)
{
    return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain));
}

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(12.f); }
    juce::Font getPopupMenuFont() override { return font(13.f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(12.f, true); }
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                       int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
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
        g.setColour(cDim);
        g.setFont(font(10.5f, true));
        g.drawText(title, r.getX() + 14, r.getY() + 4, r.getWidth() - 28, 14, juce::Justification::left);
    }
}

} // namespace nfui
