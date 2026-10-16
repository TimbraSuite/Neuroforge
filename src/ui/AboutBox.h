#pragma once
#include "Look.h"

namespace nfui {

// Cosmetic "keygen" About overlay (cracktro style). Purely visual: it touches no parameters, state or licensing.
// Add it as a child of the (design-space) editor content; it fills its parent and draws a centred 420x300 window.
class AboutBox : public juce::Component, private juce::Timer {
public:
    AboutBox();
    ~AboutBox() override { stopTimer(); }

    std::function<void()> onClose;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;
    bool keyPressed(const juce::KeyPress&) override;
    void visibilityChanged() override;

    static constexpr int kW = 420, kH = 300;

private:
    void timerCallback() override;
    void rollSerial();
    void close();
    juce::Rectangle<int> box() const { return juce::Rectangle<int>(kW, kH).withCentre(getLocalBounds().getCentre()); }
    juce::Rectangle<int> genRect() const { return juce::Rectangle<int>(box().getX() + 30, box().getY() + 184, 170, 22); }
    juce::Rectangle<int> exitRect() const { return juce::Rectangle<int>(box().getX() + 220, box().getY() + 184, 170, 22); }

    struct Star { float x, y, v; };
    std::vector<Star> stars;
    juce::String serial, nameText { "Neurofunk Producer" };
    double t0 = 0.0, serialT0 = 0.0;
    int hover = 0; // 1 = generate, 2 = exit
    juce::Random rng;
};

} // namespace nfui
