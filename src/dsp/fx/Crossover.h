#pragma once
// Linkwitz-Riley crossovers (TPT SVF based) + small shared helpers for the fxb effect headers.
#include <algorithm>
#include <cmath>

namespace nf {
namespace fxb {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDenorm = 1e-18f;

// Rational tanh approximation, accurate for small |x|, saturates at +-1.
inline float fastTanh(float x)
{
    x = std::clamp(x, -3.f, 3.f);
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

// Linear below thr, smooth saturation towards ceil.
inline float softLimit(float x, float thr, float ceil)
{
    const float a = std::fabs(x);
    if (a <= thr) return x;
    const float r = ceil - thr;
    const float y = thr + r * fastTanh((a - thr) / r);
    return x < 0.f ? -y : y;
}

// 2nd-order TPT state-variable filter state (Butterworth when k = sqrt2).
struct SvfState {
    float s1 = 0.f, s2 = 0.f;
    // returns lp, bp, hp. g = tan(pi f/fs), k = 1/Q, a1 = 1/(1+g(g+k))
    inline void tick(float x, float g, float k, float a1, float& lp, float& bp, float& hp)
    {
        hp = (x - (g + k) * s1 - s2) * a1;
        const float v1 = g * hp;
        bp = v1 + s1;
        s1 = bp + v1;
        const float v2 = g * bp;
        lp = v2 + s2;
        s2 = lp + v2;
    }
    void reset() { s1 = s2 = 0.f; }
};

} // namespace fxb

// 2-band Linkwitz-Riley 4th-order split. low + high is an allpass (flat magnitude).
class LR4Crossover {
public:
    void prepare(double sampleRate)
    {
        fs_ = (float) sampleRate;
        smooth_ = 1.f - std::exp(-1.f / (0.005f * fs_));
        setFrequency(freqTarget_, true);
        reset();
    }
    void reset()
    {
        for (auto& c : st_) for (auto& s : c) s.reset();
    }
    void setFrequency(float hz, bool immediate = false)
    {
        freqTarget_ = std::clamp(hz, 5.f, 0.45f * fs_);
        if (immediate) { freqCur_ = freqTarget_; updateCoeffs(); }
    }
    // Per-sample processing (also advances the frequency smoother).
    inline void tick(float inL, float inR, float& lowL, float& lowR, float& highL, float& highR)
    {
        advance();
        stage(0, inL, lowL, highL);
        stage(1, inR, lowR, highR);
    }
    void process(const float* inL, const float* inR, float* lowL, float* lowR,
                 float* highL, float* highR, int n)
    {
        for (int i = 0; i < n; ++i) {
            float ll, lr, hl, hr;
            tick(inL[i], inR[i], ll, lr, hl, hr);
            lowL[i] = ll; lowR[i] = lr; highL[i] = hl; highR[i] = hr;
        }
    }

private:
    inline void advance()
    {
        if (freqCur_ != freqTarget_) {
            freqCur_ += (freqTarget_ - freqCur_) * smooth_;
            if (std::fabs(freqCur_ - freqTarget_) < 1e-4f * freqTarget_) freqCur_ = freqTarget_;
            updateCoeffs();
        }
    }
    void updateCoeffs()
    {
        g_ = std::tan(fxb::kPi * freqCur_ / fs_);
        a1_ = 1.f / (1.f + g_ * (g_ + k_));
    }
    inline void stage(int ch, float x, float& low, float& high)
    {
        x += fxb::kDenorm;
        float lp, bp, hp, lp2, bp2, hp2;
        st_[ch][0].tick(x, g_, k_, a1_, lp, bp, hp);
        st_[ch][1].tick(lp, g_, k_, a1_, lp2, bp2, hp2);
        low = lp2;
        float l3, b3, h3;
        st_[ch][2].tick(x, g_, k_, a1_, l3, b3, h3);
        float l4, b4, h4;
        st_[ch][3].tick(h3, g_, k_, a1_, l4, b4, h4);
        high = h4;
    }

    float fs_ = 48000.f, smooth_ = 0.01f;
    float freqTarget_ = 1000.f, freqCur_ = 1000.f;
    float g_ = 0.1f, a1_ = 1.f;
    static constexpr float k_ = 1.41421356f; // 1/Q, Q = 0.7071
    fxb::SvfState st_[2][4];
};

// 3-band split; the low band is allpass-compensated so low+mid+high has flat magnitude.
class LR4ThreeBand {
public:
    void prepare(double sampleRate)
    {
        a_.prepare(sampleRate); b_.prepare(sampleRate); comp_.prepare(sampleRate);
    }
    void reset() { a_.reset(); b_.reset(); comp_.reset(); }
    void setFrequencies(float lowHz, float highHz, bool immediate = false)
    {
        a_.setFrequency(lowHz, immediate);
        b_.setFrequency(highHz, immediate);
        comp_.setFrequency(highHz, immediate);
    }
    inline void tick(float inL, float inR, float* lo, float* mid, float* hi)
    {
        // lo/mid/hi: arrays of 2 (L,R)
        float lowL, lowR, restL, restR;
        a_.tick(inL, inR, lowL, lowR, restL, restR);
        float cl0, cr0, cl1, cr1;
        comp_.tick(lowL, lowR, cl0, cr0, cl1, cr1);
        b_.tick(restL, restR, mid[0], mid[1], hi[0], hi[1]);
        lo[0] = cl0 + cl1;
        lo[1] = cr0 + cr1;
    }
    void process(const float* inL, const float* inR, float* lowL, float* lowR,
                 float* midL, float* midR, float* highL, float* highR, int n)
    {
        for (int i = 0; i < n; ++i) {
            float lo[2], mid[2], hi[2];
            tick(inL[i], inR[i], lo, mid, hi);
            lowL[i] = lo[0]; lowR[i] = lo[1];
            midL[i] = mid[0]; midR[i] = mid[1];
            highL[i] = hi[0]; highR[i] = hi[1];
        }
    }

private:
    LR4Crossover a_, b_, comp_;
};

} // namespace nf
