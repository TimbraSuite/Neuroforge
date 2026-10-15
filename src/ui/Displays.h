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

} // namespace nfui
