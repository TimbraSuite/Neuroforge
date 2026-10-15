#pragma once
#include "Controls.h"
#include <array>

namespace nfui {

class WavetableView : public juce::Component, public Tickable {
public:
    WavetableView(NeuroforgeProcessor& p, int oscIndex);
    void paint(juce::Graphics&) override;
    void tick() override;
private:
    NeuroforgeProcessor& proc;
    int osc;
    float lastTable = -1.f, lastPos = -1.f;
    int lastGen = -1;
};

class EnvView : public juce::Component, public Tickable {
public:
    EnvView(NeuroforgeProcessor& p, int envIndex);
    void paint(juce::Graphics&) override;
    void tick() override;
private:
    NeuroforgeProcessor& proc;
    int env;
    std::array<float, 5> last {};
    float level = 0.f;
};

// Scrolling history of a modulation source (LFO / Motion).
class TraceView : public juce::Component, public Tickable {
public:
    TraceView(NeuroforgeProcessor& p, std::function<float()> getter, juce::Colour c, int shapeParam = -1);
    void paint(juce::Graphics&) override;
    void tick() override;
private:
    NeuroforgeProcessor& proc;
    std::function<float()> get;
    juce::Colour colour;
    int shapeParam;
    static constexpr int N = 72;
    std::array<float, N> hist {};
    int head = 0;
};

class StepSeq : public juce::Component, public Tickable {
public:
    StepSeq(NeuroforgeProcessor& p, int motionIndex);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void tick() override;
private:
    void paintAt(juce::Point<int>);
    NeuroforgeProcessor& proc;
    int m;
    std::array<float, 16> last {};
    float lastSteps = -1.f, lastLive = 0.f; float lastPhase = -1.f;
    std::array<bool, 16> touched {};
    int prevIdx = -1;
    float prevVal = 0.f;
};

class ScopeView : public juce::Component, public Tickable {
public:
    explicit ScopeView(NeuroforgeProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void tick() override;
private:
    NeuroforgeProcessor& proc;
    std::array<float, 2048> buf {};
};

class SpectrumView : public juce::Component, public Tickable {
public:
    explicit SpectrumView(NeuroforgeProcessor& p);
    void paint(juce::Graphics&) override;
    void tick() override;
private:
    NeuroforgeProcessor& proc;
    juce::dsp::FFT fft { 11 };
    std::array<float, 2048> buf {};
    std::array<float, 4096> fftData {};
    std::array<float, 2048> window {};
    static constexpr int kBars = 96;
    std::array<float, kBars> bars {};
};

class Meter : public juce::Component, public Tickable {
public:
    explicit Meter(NeuroforgeProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void tick() override;
private:
    NeuroforgeProcessor& proc;
    float l = 0.f, r = 0.f;
};

//==============================================================================================
// Curve stepper editor: a palette of curve shapes above a grid of steps.
//  * drag a curve from the palette onto a step (or select a step, then click a curve)
//  * drag up/down inside a step to set its intensity (drag sideways to paint several steps)
//  * mouse wheel on a step cycles its curve, right-click picks one from a menu, double-click = full
class StepperEditor : public juce::Component, public juce::SettableTooltipClient, public Tickable {
public:
    StepperEditor(NeuroforgeProcessor& p, int stepperIndex);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hoverCell = hoverCurve = -1; repaint(); }
    void tick() override;
private:
    juce::Rectangle<float> paletteArea() const;
    juce::Rectangle<float> gridArea() const;
    int paletteIndexAt(juce::Point<float>) const;
    int stepAt(juce::Point<float>) const;
    int numSteps() const;
    int shapeOf(int step) const;
    float amountOf(int step) const;
    void setNorm(int paramId, float norm);
    void setShape(int step, int shape);
    void setAmount(int step, float amt);
    void drawCurve(juce::Graphics&, juce::Rectangle<float> r, int shape, float amt, juce::Colour c, bool filled) const;

    NeuroforgeProcessor& proc;
    int st;
    std::array<float, 32> cache {};
    float lastPhase = -1.f, lastSteps = -1.f;
    int dragCurve = -1, dragPosX = 0, dragPosY = 0, hoverCell = -1, hoverCurve = -1, selected = 0, lastPaint = -1;
    bool draggingAmount = false;
};

} // namespace nfui
