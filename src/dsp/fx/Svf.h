#pragma once
// Cytomic / Simper TPT state-variable filter + small shared DSP helpers for the FX modules.
// Header-only, no allocation. The SVF is stable under per-sample modulation of g / k
// (set coefficients every sample with setGK(); use gFromHz() once per block and ramp g linearly).
#include <algorithm>
#include <cmath>

namespace nf {

namespace fxu {

constexpr float kPi = 3.14159265358979f;

inline float dbToLin(float db) { return std::pow(10.f, db * 0.05f); }

// Transparent below |x|=2, smoothly compresses towards +-6 (hard bound of the whole FX chain safety net).
inline float softLimit(float x)
{
    const float ax = std::fabs(x);
    if (ax <= 2.f) return x;
    const float y = 2.f + 4.f * std::tanh((ax - 2.f) * 0.25f);
    return x < 0.f ? -y : y;
}

inline float flushDenorm(float v) { return std::fabs(v) < 1e-20f ? 0.f : v; }

// Linear ramp helper: value goes from `cur` to `target` over n samples.
struct Ramp {
    float cur = 0.f, step = 0.f;
    void set(float target, int n) { step = (target - cur) / (float) std::max(n, 1); }
    void snap(float v) { cur = v; step = 0.f; }
    float next() { cur += step; return cur; }
};

// ~10 Hz DC blocker.
struct DcBlock {
    float x1 = 0.f, y1 = 0.f, R = 0.999f;
    void prepare(double sr) { R = (float) (1.0 - 2.0 * 3.14159265358979 * 10.0 / sr); }
    void reset() { x1 = y1 = 0.f; }
    float tick(float x)
    {
        const float y = flushDenorm(x - x1 + R * y1);
        x1 = x; y1 = y;
        return y;
    }
};

// First-order tilt: out = lp*lo + (x-lp)*hi, split frequency fixed at prepare/setFreq.
struct Tilt {
    float lp = 0.f, a = 0.1f;
    void setFreq(double hz, double sr) { a = (float) (1.0 - std::exp(-2.0 * 3.14159265358979 * hz / sr)); }
    void reset() { lp = 0.f; }
    float tick(float x, float lo, float hi)
    {
        lp = flushDenorm(lp + a * (x - lp));
        return lp * lo + (x - lp) * hi;
    }
};

} // namespace fxu

struct SvfOut {
    float low = 0.f, band = 0.f /* raw, peak gain 1/k */, high = 0.f, notch = 0.f, peak = 0.f, allpass = 0.f;
};

class Svf {
public:
    static constexpr int kMaxCh = 2;

    void reset() { for (auto& s : ic1) s = 0.f; for (auto& s : ic2) s = 0.f; }

    // g = tan(pi*fc/sr); fc is clamped to a safe range.
    static float gFromHz(double fc, double sr)
    {
        fc = std::clamp(fc, 1.0, sr * 0.45);
        return (float) std::tan(3.14159265358979 * fc / sr);
    }
    // k = 1/Q.  res 0..1 -> k = 2 - 2*res*0.98  (Q 0.5 .. 25, never unbounded)
    static float kFromRes(float res) { return 2.f - 2.f * std::clamp(res, 0.f, 1.f) * 0.98f; }
    static float kFromQ(float q) { return 1.f / std::max(q, 0.05f); }

    void setGK(float g, float k)
    {
        gc = g; kc = k;
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void setParams(float cutoffHz, float res, double sr) { setGK(gFromHz(cutoffHz, sr), kFromRes(res)); }
    void setFreqQ(float cutoffHz, float q, double sr) { setGK(gFromHz(cutoffHz, sr), kFromQ(q)); }

    float k() const { return kc; }

    SvfOut tick(int ch, float v0)
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = fxu::flushDenorm(2.f * v1 - ic1[ch]);
        ic2[ch] = fxu::flushDenorm(2.f * v2 - ic2[ch]);
        SvfOut o;
        o.low = v2;
        o.band = v1;
        o.notch = v0 - kc * v1;
        o.high = o.notch - v2;
        o.peak = v2 - o.high;
        o.allpass = v0 - 2.f * kc * v1;
        return o;
    }

    // Continuous LP(0) -> BP(1) -> HP(2) -> Notch(3). BP is normalised to unity peak gain.
    static float morph(const SvfOut& o, float k, float mode)
    {
        mode = std::clamp(mode, 0.f, 3.f);
        const float bp = k * o.band;
        if (mode < 1.f) return o.low * (1.f - mode) + bp * mode;
        if (mode < 2.f) return bp * (2.f - mode) + o.high * (mode - 1.f);
        return o.high * (3.f - mode) + o.notch * (mode - 2.f);
    }

private:
    float gc = 0.f, kc = 1.f, a1 = 1.f, a2 = 0.f, a3 = 0.f;
    float ic1[kMaxCh] = { 0.f, 0.f }, ic2[kMaxCh] = { 0.f, 0.f };
};

} // namespace nf
