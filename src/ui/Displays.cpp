#include "Displays.h"
#include <cmath>

using namespace nf;

namespace nfui {

static void paintDisplayBg(juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour(juce::Colour(0xff0a0c0f));
    g.fillRoundedRectangle(r.toFloat(), 4.f);
    g.setColour(cEdge);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.f, 1.f);
}

//==============================================================================================
WavetableView::WavetableView(NeuroforgeProcessor& p, int o) : proc(p), osc(o) {}

void WavetableView::tick()
{
    if (!isShowing()) return;
    const float t = proc.getRaw(pid::osc(osc, pid::oTable));
    const float pos = proc.getRaw(pid::osc(osc, pid::oPos)) + proc.getEngine().uiOffsets[pid::osc(osc, pid::oPos)].load(std::memory_order_relaxed);
    const int gen = proc.captureGeneration.load();
    if (t != lastTable || std::abs(pos - lastPos) > 0.002f || gen != lastGen) {
        lastTable = t; lastPos = pos; lastGen = gen;
        repaint();
    }
}

void WavetableView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintDisplayBg(g, r);
    const int ti = (int) normToReal(pid::osc(osc, pid::oTable), proc.getRaw(pid::osc(osc, pid::oTable)));
    const int nb = tables::getNumBuiltIn();
    std::shared_ptr<const WavetableBank> bank = ti < nb ? tables::getBuiltIn(ti) : proc.getEngine().getCapture(ti - nb);
    auto inner = r.reduced(6, 8).toFloat();
    g.setColour(cEdge.withAlpha(0.6f));
    g.drawHorizontalLine((int) inner.getCentreY(), inner.getX(), inner.getRight());
    if (!bank) {
        g.setColour(cDim);
        g.setFont(font(12.f));
        g.drawText("empty capture slot - press CAPTURE", r, juce::Justification::centred);
        return;
    }
    const int nf_ = bank->getNumFrames();
    constexpr int kPts = 256, stride = WavetableBank::kFrameSize / kPts;
    const float pos = juce::jlimit(0.f, 1.f, lastPos < 0.f ? 0.f : lastPos);

    auto makePath = [&](const std::vector<float>& v) {
        juce::Path p;
        for (int i = 0; i < kPts; ++i) {
            const float x = inner.getX() + inner.getWidth() * (float) i / (float) (kPts - 1);
            const float y = inner.getCentreY() - juce::jlimit(-1.2f, 1.2f, v[(size_t) i]) * inner.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        return p;
    };
    std::vector<float> v((size_t) kPts);
    const int ghosts = juce::jmin(nf_, 10);
    if (ghosts > 1) {
        for (int k = 0; k < ghosts; ++k) {
            const float* f = bank->getFrame(k * (nf_ - 1) / (ghosts - 1));
            for (int i = 0; i < kPts; ++i) v[(size_t) i] = f[i * stride];
            g.setColour(cDim.withAlpha(0.18f));
            g.strokePath(makePath(v), juce::PathStrokeType(1.f));
        }
    }
    const float fp = pos * (float) (nf_ - 1);
    const int f0 = (int) std::floor(fp), f1 = juce::jmin(nf_ - 1, f0 + 1);
    const float t = fp - (float) f0;
    const float *a = bank->getFrame(f0), *b = bank->getFrame(f1);
    for (int i = 0; i < kPts; ++i) v[(size_t) i] = a[i * stride] * (1.f - t) + b[i * stride] * t;
    auto path = makePath(v);
    g.setColour(cAccent.withAlpha(0.18f));
    g.strokePath(path, juce::PathStrokeType(4.f));
    g.setColour(cAccent);
    g.strokePath(path, juce::PathStrokeType(1.6f));
    g.setColour(cDim);
    g.setFont(font(10.f));
    g.drawText(juce::String(bank->getName()) + "   frame " + juce::String(fp + 1.f, 1) + " / " + juce::String(nf_),
               r.reduced(8, 3), juce::Justification::topLeft);
}

//==============================================================================================
EnvView::EnvView(NeuroforgeProcessor& p, int e) : proc(p), env(e) {}

void EnvView::tick()
{
    if (!isShowing()) return;
    bool ch = false;
    for (int k = 0; k < 5; ++k) {
        const float v = proc.getRaw(pid::env(env, k));
        if (v != last[(size_t) k]) { last[(size_t) k] = v; ch = true; }
    }
    const float lv = proc.getEngine().uiEnv[env].load(std::memory_order_relaxed);
    if (std::abs(lv - level) > 0.01f) { level = lv; ch = true; }
    if (ch) repaint();
}

void EnvView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintDisplayBg(g, r);
    auto in = r.toFloat().reduced(6.f, 6.f);
    const float wA = 0.08f + 0.92f * last[pid::eA], wD = 0.08f + 0.92f * last[pid::eD], wH = 0.3f,
                wR = 0.08f + 0.92f * last[pid::eR];
    const float S = last[pid::eS], tot = wA + wD + wH + wR;
    auto X = [&](float u) { return in.getX() + in.getWidth() * u / tot; };
    auto Y = [&](float l) { return in.getBottom() - in.getHeight() * l; };
    juce::Path p;
    p.startNewSubPath(X(0), Y(0));
    p.lineTo(X(wA), Y(1));
    p.quadraticTo(X(wA + wD * 0.25f), Y(S + (1.f - S) * 0.35f), X(wA + wD), Y(S));
    p.lineTo(X(wA + wD + wH), Y(S));
    p.quadraticTo(X(wA + wD + wH + wR * 0.25f), Y(S * 0.35f), X(tot), Y(0));
    auto fill = p;
    fill.lineTo(X(tot), Y(0));
    fill.closeSubPath();
    g.setColour(juce::Colour(0xffffb23c).withAlpha(0.10f));
    g.fillPath(fill);
    g.setColour(juce::Colour(0xffffb23c));
    g.strokePath(p, juce::PathStrokeType(1.6f));
    g.setColour(cCyan.withAlpha(0.8f));
    g.fillRect(r.getRight() - 5.f, in.getBottom() - in.getHeight() * juce::jlimit(0.f, 1.f, level), 3.f, in.getHeight() * juce::jlimit(0.f, 1.f, level));
    if (last[pid::eLoop] >= 0.5f) {
        g.setColour(cAccent);
        g.setFont(font(9.f, true));
        g.drawText("LOOP", r.reduced(6, 3), juce::Justification::topRight);
    }
}

//==============================================================================================
TraceView::TraceView(NeuroforgeProcessor& p, std::function<float()> getter, juce::Colour c, int sp)
    : proc(p), get(std::move(getter)), colour(c), shapeParam(sp) {}

void TraceView::tick()
{
    if (!isShowing()) return;
    hist[(size_t) head] = get();
    head = (head + 1) % N;
    repaint();
}

void TraceView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintDisplayBg(g, r);
    auto in = r.toFloat().reduced(4.f, 4.f);
    g.setColour(cEdge.withAlpha(0.6f));
    g.drawHorizontalLine((int) in.getCentreY(), in.getX(), in.getRight());
    juce::Path p;
    for (int i = 0; i < N; ++i) {
        const float v = juce::jlimit(-1.f, 1.f, hist[(size_t) ((head + i) % N)]);
        const float x = in.getX() + in.getWidth() * (float) i / (float) (N - 1);
        const float y = in.getCentreY() - v * in.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(0.9f));
    g.strokePath(p, juce::PathStrokeType(1.4f));
    if (shapeParam >= 0) {
        const int si = (int) normToReal(shapeParam, proc.getRaw(shapeParam));
        g.setColour(cDim);
        g.setFont(font(9.f));
        g.drawText(juce::String(lfoShapeNames()[(size_t) juce::jlimit(0, (int) lfoShapeNames().size() - 1, si)]), r.reduced(5, 2), juce::Justification::topLeft);
    }
}

//==============================================================================================
StepSeq::StepSeq(NeuroforgeProcessor& p, int mi) : proc(p), m(mi) { setMouseCursor(juce::MouseCursor::CrosshairCursor); }

void StepSeq::tick()
{
    if (!isShowing()) return;
    bool ch = false;
    for (int i = 0; i < 16; ++i) {
        const float v = proc.getRaw(pid::motion(m, pid::mStep0 + i));
        if (v != last[(size_t) i]) { last[(size_t) i] = v; ch = true; }
    }
    const float s = proc.getRaw(pid::motion(m, pid::mSteps));
    if (s != lastSteps) { lastSteps = s; ch = true; }
    const float live = (proc.getEngine().uiMotion[m].load(std::memory_order_relaxed) + 1.f) * 0.5f;
    if (std::abs(live - lastLive) > 0.01f) { lastLive = live; ch = true; }
    const float ph = proc.getEngine().uiMotionPhase[m].load(std::memory_order_relaxed);
    if (std::abs(ph - lastPhase) > 0.004f) { lastPhase = ph; ch = true; }
    if (ch) repaint();
}

void StepSeq::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintDisplayBg(g, r);
    auto in = r.toFloat().reduced(2.f);
    const int steps = (int) normToReal(pid::motion(m, pid::mSteps), lastSteps < 0.f ? 0.5f : lastSteps);
    const float cw = in.getWidth() / 16.f;
    for (int i = 0; i < 16; ++i) {
        const bool on = i < steps;
        const float h = in.getHeight() * last[(size_t) i];
        auto bar = juce::Rectangle<float>(in.getX() + cw * (float) i + 1.f, in.getBottom() - h, cw - 2.f, juce::jmax(1.5f, h));
        g.setColour(on ? cAccent.withAlpha(i % 4 == 0 ? 0.95f : 0.75f) : cDim.withAlpha(0.25f));
        g.fillRect(bar);
    }
    // playhead: the step the motion is currently on
    const int cur = juce::jlimit(0, juce::jmax(0, steps - 1), (int) (lastPhase * (float) juce::jmax(1, steps)));
    g.setColour(cCyan.withAlpha(0.22f));
    g.fillRect(juce::Rectangle<float>(in.getX() + cw * (float) cur, in.getY(), cw, in.getHeight()));
    g.setColour(cCyan.withAlpha(0.85f));
    g.drawHorizontalLine((int) (in.getBottom() - in.getHeight() * juce::jlimit(0.f, 1.f, lastLive)), in.getX(), in.getRight());
}

void StepSeq::paintAt(juce::Point<int> p)
{
    const float cw = (float) getWidth() / 16.f;
    const int idx = juce::jlimit(0, 15, (int) ((float) p.x / cw));
    const float val = juce::jlimit(0.f, 1.f, 1.f - (float) (p.y - 2) / (float) juce::jmax(1, getHeight() - 4));
    auto set = [&](int i, float v) {
        auto* prm = proc.getParam(pid::motion(m, pid::mStep0 + i));
        if (!touched[(size_t) i]) { prm->beginChangeGesture(); touched[(size_t) i] = true; }
        prm->setValueNotifyingHost(v);
    };
    if (prevIdx >= 0 && std::abs(prevIdx - idx) > 1) {
        const int dir = idx > prevIdx ? 1 : -1;
        for (int i = prevIdx + dir; i != idx; i += dir) {
            const float t = (float) (i - prevIdx) / (float) (idx - prevIdx);
            set(i, prevVal + (val - prevVal) * t);
        }
    }
    set(idx, val);
    prevIdx = idx; prevVal = val;
}

void StepSeq::mouseDown(const juce::MouseEvent& e) { prevIdx = -1; paintAt(e.getPosition()); }
void StepSeq::mouseDrag(const juce::MouseEvent& e) { paintAt(e.getPosition()); }
void StepSeq::mouseUp(const juce::MouseEvent&)
{
    for (int i = 0; i < 16; ++i)
        if (touched[(size_t) i]) { proc.getParam(pid::motion(m, pid::mStep0 + i))->endChangeGesture(); touched[(size_t) i] = false; }
    prevIdx = -1;
}

//==============================================================================================
void ScopeView::tick()
{
    if (!isShowing()) return;
    proc.copyScope(buf.data(), 2048);
    repaint();
}

void ScopeView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintDisplayBg(g, r);
    auto in = r.toFloat().reduced(4.f);
    g.setColour(cEdge.withAlpha(0.6f));
    g.drawHorizontalLine((int) in.getCentreY(), in.getX(), in.getRight());
    // rising zero crossing trigger in the first half
    int start = 0;
    for (int i = 1; i < 1024; ++i) if (buf[(size_t) i - 1] <= 0.f && buf[(size_t) i] > 0.f) { start = i; break; }
    constexpr int len = 900;
    juce::Path p;
    for (int i = 0; i < len; ++i) {
        const float v = juce::jlimit(-1.f, 1.f, buf[(size_t) (start + i)]);
        const float x = in.getX() + in.getWidth() * (float) i / (float) (len - 1);
        const float y = in.getCentreY() - v * in.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.setColour(cAccent);
    g.strokePath(p, juce::PathStrokeType(1.3f));
}

//==============================================================================================
SpectrumView::SpectrumView(NeuroforgeProcessor& p) : proc(p)
{
    for (size_t i = 0; i < window.size(); ++i)
        window[i] = 0.5f - 0.5f * std::cos(2.f * juce::MathConstants<float>::pi * (float) i / (float) (window.size() - 1));
}

void SpectrumView::tick()
{
    if (!isShowing()) return;
    proc.copyScope(buf.data(), 2048);
    std::fill(fftData.begin(), fftData.end(), 0.f);
    for (size_t i = 0; i < 2048; ++i) fftData[i] = buf[i] * window[i];
    fft.performFrequencyOnlyForwardTransform(fftData.data());
    const float sr = (float) juce::jmax(8000.0, proc.getSampleRate());
    const float binHz = sr / 2048.f;
    for (int b = 0; b < kBars; ++b) {
        const float f0 = 25.f * std::pow(18000.f / 25.f, (float) b / (float) kBars);
        const float f1 = 25.f * std::pow(18000.f / 25.f, (float) (b + 1) / (float) kBars);
        const int i0 = juce::jlimit(1, 1023, (int) std::floor(f0 / binHz)), i1 = juce::jlimit(i0, 1023, (int) std::ceil(f1 / binHz));
        float mag = 0.f;
        for (int i = i0; i <= i1; ++i) mag = std::max(mag, fftData[(size_t) i]);
        const float db = 20.f * std::log10(mag / 512.f + 1e-6f);
        const float v = juce::jlimit(0.f, 1.f, (db + 80.f) / 80.f);
        bars[(size_t) b] = std::max(v, bars[(size_t) b] * 0.86f);
    }
    repaint();
}

void SpectrumView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    paintDisplayBg(g, r);
    auto in = r.toFloat().reduced(4.f);
    juce::Path p;
    for (int b = 0; b < kBars; ++b) {
        const float x = in.getX() + in.getWidth() * (float) b / (float) (kBars - 1);
        const float y = in.getBottom() - in.getHeight() * bars[(size_t) b];
        if (b == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    auto fill = p;
    fill.lineTo(in.getRight(), in.getBottom());
    fill.lineTo(in.getX(), in.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(cAccent.withAlpha(0.35f), 0.f, in.getY(), cAccent.withAlpha(0.02f), 0.f, in.getBottom(), false));
    g.fillPath(fill);
    g.setColour(cAccent);
    g.strokePath(p, juce::PathStrokeType(1.3f));
    g.setColour(cDim.withAlpha(0.7f));
    g.setFont(font(9.f));
    for (float f : { 50.f, 100.f, 500.f, 1000.f, 5000.f, 10000.f }) {
        const float x = in.getX() + in.getWidth() * std::log(f / 25.f) / std::log(18000.f / 25.f);
        g.drawVerticalLine((int) x, in.getY(), in.getBottom());
        g.drawText(f >= 1000.f ? juce::String((int) (f / 1000.f)) + "k" : juce::String((int) f), (int) x + 2, (int) in.getBottom() - 11, 24, 10, juce::Justification::left);
    }
}

//==============================================================================================
void Meter::tick()
{
    if (!isShowing()) return;
    const float nl = proc.peakL.load(std::memory_order_relaxed), nr = proc.peakR.load(std::memory_order_relaxed);
    if (std::abs(nl - l) > 0.002f || std::abs(nr - r) > 0.002f) { l = nl; r = nr; repaint(); }
}

void Meter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto draw = [&](juce::Rectangle<float> rc, float peak) {
        g.setColour(cTrack);
        g.fillRoundedRectangle(rc, 2.f);
        const float db = 20.f * std::log10(peak + 1e-5f);
        const float u = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
        g.setColour(db > -1.f ? cWarn : (db > -9.f ? juce::Colour(0xffffd23c) : cAccent));
        g.fillRoundedRectangle(rc.withWidth(rc.getWidth() * u), 2.f);
    };
    const float h = (b.getHeight() - 3.f) * 0.5f;
    draw(b.withHeight(h), l);
    draw(b.withTrimmedTop(h + 3.f), r);
}

} // namespace nfui
