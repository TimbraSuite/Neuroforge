#include "Look.h"

namespace nfui {

LookAndFeel::LookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, cBg);
    setColour(juce::PopupMenu::backgroundColourId, cPanel2);
    setColour(juce::PopupMenu::textColourId, cText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, cAccent.withAlpha(0.85f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour(juce::PopupMenu::headerTextColourId, cDim);
    setColour(juce::ComboBox::backgroundColourId, cPanel2);
    setColour(juce::ComboBox::textColourId, cText);
    setColour(juce::ComboBox::outlineColourId, cEdge);
    setColour(juce::ComboBox::arrowColourId, cDim);
    setColour(juce::Label::textColourId, cText);
    setColour(juce::TextEditor::backgroundColourId, cPanel2);
    setColour(juce::TextEditor::textColourId, cText);
    setColour(juce::TextEditor::outlineColourId, cEdge);
    setColour(juce::TextEditor::focusedOutlineColourId, cAccent);
    setColour(juce::AlertWindow::backgroundColourId, cPanel);
    setColour(juce::AlertWindow::textColourId, cText);
    setColour(juce::AlertWindow::outlineColourId, cEdge);
    setColour(juce::TextButton::buttonColourId, cPanel2);
    setColour(juce::TextButton::textColourOffId, cText);
    setColour(juce::ScrollBar::thumbColourId, cEdge.brighter(0.3f));
    setColour(juce::Slider::thumbColourId, cAccent);
    setColour(juce::Slider::trackColourId, cAccent);
    setColour(juce::Slider::backgroundColourId, cTrack);
    setColour(juce::CaretComponent::caretColourId, cAccent);
    setColour(juce::TooltipWindow::backgroundColourId, cPanel2);
    setColour(juce::TooltipWindow::textColourId, cText);
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float>(0.f, 0.f, (float) w, (float) h).reduced(0.5f);
    g.setColour(down ? cPanel2.brighter(0.15f) : cPanel2);
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(box.hasKeyboardFocus(true) ? cAccent : cEdge);
    g.drawRoundedRectangle(r, 4.f, 1.f);
    juce::Path p;
    const float cx = (float) w - 10.f, cy = (float) h * 0.5f;
    p.addTriangle(cx - 3.f, cy - 1.5f, cx + 3.f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour(box.isEnabled() ? cDim : cDim.withAlpha(0.4f));
    g.fillPath(p);
}

void LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(5, 1, box.getWidth() - 20, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    auto c = base;
    if (b.getToggleState()) c = cAccent.withAlpha(0.9f);
    if (over) c = c.brighter(0.12f);
    if (down) c = c.brighter(0.25f);
    g.setColour(c);
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(b.getToggleState() ? cAccent : cEdge);
    g.drawRoundedRectangle(r, 4.f, 1.f);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.getToggleState() ? juce::Colours::black : b.findColour(juce::TextButton::textColourOffId));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(3, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float ph = juce::jmin(14.f, r.getHeight() - 2.f), pw = ph * 1.8f;
    auto pill = juce::Rectangle<float>(r.getX() + 1.f, r.getCentreY() - ph * 0.5f, pw, ph);
    g.setColour(on ? cAccent.withAlpha(0.9f) : cTrack);
    g.fillRoundedRectangle(pill, ph * 0.5f);
    g.setColour(on ? cAccent : cEdge);
    g.drawRoundedRectangle(pill, ph * 0.5f, 1.f);
    const float kd = ph - 4.f;
    g.setColour(on ? juce::Colours::black : (over ? cText : cDim));
    g.fillEllipse(on ? pill.getRight() - kd - 2.f : pill.getX() + 2.f, pill.getY() + 2.f, kd, kd);
    g.setColour(on ? cText : cDim);
    g.setFont(font(11.f));
    g.drawText(b.getButtonText(), (int) (pill.getRight() + 5.f), 0, b.getWidth() - (int) pw - 6, b.getHeight(),
               juce::Justification::centredLeft);
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(cPanel2);
    g.setColour(cEdge);
    g.drawRect(0, 0, w, h, 1);
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                int thumbStart, int thumbSize, bool over, bool)
{
    juce::ignoreUnused(x, y);
    auto t = vertical ? juce::Rectangle<float>((float) w * 0.5f - 2.f, (float) thumbStart, 4.f, (float) thumbSize)
                      : juce::Rectangle<float>((float) thumbStart, (float) h * 0.5f - 2.f, (float) thumbSize, 4.f);
    g.setColour(over ? cAccent.withAlpha(0.7f) : cEdge.brighter(0.4f));
    g.fillRoundedRectangle(t, 2.f);
}

} // namespace nfui
