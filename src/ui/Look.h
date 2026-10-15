#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

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

// Little "group box" painter shared by panels.
inline void paintPanel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title = {})
{
    g.setColour(cPanel);
    g.fillRoundedRectangle(r.toFloat(), 6.f);
    g.setColour(cEdge);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.f, 1.f);
    if (title.isNotEmpty()) {
        g.setColour(cDim);
        g.setFont(font(10.5f, true));
        g.drawText(title, r.getX() + 10, r.getY() + 4, r.getWidth() - 20, 14, juce::Justification::left);
    }
}

} // namespace nfui
