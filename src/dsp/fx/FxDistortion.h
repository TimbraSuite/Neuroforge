#pragma once
// Distortion-family FX: DriveFx, FoldFx, CrushFx. Header-only.
#include "FxModule.h"
#include "Svf.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nf {

namespace fxd {

// ---- antiderivatives (double for precision at high gain) ----
inline double lnCosh(double x)
{
    x = std::fabs(x);
    return x + std::log1p(std::exp(-2.0 * x)) - 0.6931471805599453;
}
inline double clipF(double x) { return x < -1.0 ? -x - 0.5 : (x > 1.0 ? x - 0.5 : 0.5 * x * x); }
inline double clipf(double x) { return std::clamp(x, -1.0, 1.0); }

// Triangle fold: period 4, f(u)=u on [-1,1], 2-u on [1,3]. F periodic.
inline double triWrap(double u) { return u - 4.0 * std::floor((u + 1.0) * 0.25); } // -> [-1,3)
inline double triF0(double u)
{
    const double w = triWrap(u);
    return w <= 1.0 ? 0.5 * w * w : 0.5 + 2.0 * (w - 1.0) - 0.5 * (w * w - 1.0);
}
inline double trif0(double u)
{
    const double w = triWrap(u);
    return w <= 1.0 ? w : 2.0 - w;
}

constexpr double kHalfPi = 1.5707963267948966;

// Generic first-order ADAA step. f: function, F: antiderivative.
template <class Fn, class FnInt>
inline double adaa(double u, double up, Fn f, FnInt F)
{
    const double du = u - up;
    if (std::fabs(du) < 1e-5) return f(0.5 * (u + up));
    return (F(u) - F(up)) / du;
}

inline void toneGains(float tone, float& lo, float& hi)
{
    lo = fxu::dbToLin(-6.f * tone);
    hi = fxu::dbToLin(6.f * tone);
}

} // namespace fxd

// ---------------------------------------------------------------------------------------------
class DriveFx final : public FxModule {
public:
    // Naive reference shaper used by tests (same curve family, no ADAA).
    static float naiveShape(float u, float shape)
    {
        return (1.f - shape) * std::tanh(u) + shape * std::clamp(u, -1.f, 1.f);
    }

    void prepare(double sr, int) override
    {
        sampleRate = sr;
        dc[0].prepare(sr); dc[1].prepare(sr);
        for (auto& t : tilt) t.setFreq(1000.0, sr);
        reset();
    }
    void reset() override
    {
        first = true;
        up[0] = up[1] = 0.0;
        dc[0].reset(); dc[1].reset();
        tilt[0].reset(); tilt[1].reset();
    }
    void process(float* left, float* right, int n, const float* p, const FxContext&) override
    {
        float* ch[2] = { left, right };
        const float gT = fxu::dbToLin(std::clamp(p[0], 0.f, 48.f));
        const float shT = std::clamp(p[1], 0.f, 1.f);
        const float biT = std::clamp(p[2], -1.f, 1.f) * 0.7f;
        const float toT = std::clamp(p[3], -1.f, 1.f);
        if (first) { rg.snap(gT); rs.snap(shT); rb.snap(biT); rt.snap(toT); first = false; }
        rg.set(gT, n); rs.set(shT, n); rb.set(biT, n); rt.set(toT, n);

        for (int i = 0; i < n; ++i) {
            const float g = rg.next(), sh = rs.next(), b = rb.next(), t = rt.next();
            const double s0 = (1.0 - sh) * std::tanh((double) b) + (double) sh * fxd::clipf((double) b);
            const float comp = std::exp(-0.25f * std::log(std::max(g, 1.f)));
            float lo, hi;
            fxd::toneGains(t, lo, hi);
            for (int c = 0; c < 2; ++c) {
                const double u = (double) g * (double) ch[c][i] + (double) b;
                const double sT = fxd::adaa(u, up[c], [](double v) { return std::tanh(v); }, fxd::lnCosh);
                const double sC = fxd::adaa(u, up[c], fxd::clipf, fxd::clipF);
                up[c] = u;
                const double s = (1.0 - sh) * sT + (double) sh * sC - s0;
                float y = dc[c].tick((float) s * comp);
                y = tilt[c].tick(y, lo, hi);
                ch[c][i] = fxu::softLimit(y);
            }
        }
    }

private:
    double sampleRate = 48000.0;
    bool first = true;
    double up[2] = { 0.0, 0.0 };
    fxu::DcBlock dc[2];
    fxu::Tilt tilt[2];
    fxu::Ramp rg, rs, rb, rt;
};

// ---------------------------------------------------------------------------------------------
class FoldFx final : public FxModule {
public:
    void prepare(double sr, int) override
    {
        dc[0].prepare(sr); dc[1].prepare(sr);
        for (auto& t : tilt) t.setFreq(1000.0, sr);
        reset();
    }
    void reset() override
    {
        first = true;
        up[0] = up[1] = 0.0;
        dc[0].reset(); dc[1].reset();
        tilt[0].reset(); tilt[1].reset();
    }
    void process(float* left, float* right, int n, const float* p, const FxContext&) override
    {
        float* ch[2] = { left, right };
        const float gT = fxu::dbToLin(std::clamp(p[0], 0.f, 40.f));
        const float foT = std::clamp(p[1], 0.f, 1.f);
        const float biT = std::clamp(p[2], -1.f, 1.f) * 0.5f;
        const float toT = std::clamp(p[3], -1.f, 1.f);
        if (first) { rg.snap(gT); rf.snap(foT); rb.snap(biT); rt.snap(toT); first = false; }
        rg.set(gT, n); rf.set(foT, n); rb.set(biT, n); rt.set(toT, n);

        auto sinf0 = [](double v) { return std::sin(v * fxd::kHalfPi); };
        auto sinF0 = [](double v) { return -std::cos(v * fxd::kHalfPi) / fxd::kHalfPi; };

        for (int i = 0; i < n; ++i) {
            const float g = rg.next(), fo = rf.next(), b = rb.next(), t = rt.next();
            const double geff = (double) g * (0.5 + (double) fo);
            // static output at zero input so the bias DC is removed without a long transient
            const double s0 = (1.0 - fo) * std::sin((double) b * fxd::kHalfPi) + (double) fo * fxd::trif0((double) b);
            float lo, hi;
            fxd::toneGains(t, lo, hi);
            for (int c = 0; c < 2; ++c) {
                const double u = geff * (double) ch[c][i] + (double) b;
                const double sS = fxd::adaa(u, up[c], sinf0, sinF0);
                const double sT = fxd::adaa(u, up[c], fxd::trif0, fxd::triF0);
                up[c] = u;
                const double s = (1.0 - fo) * sS + (double) fo * sT - s0;
                float y = dc[c].tick((float) s);
                y = tilt[c].tick(y, lo, hi);
                ch[c][i] = fxu::softLimit(y);
            }
        }
    }

private:
    bool first = true;
    double up[2] = { 0.0, 0.0 };
    fxu::DcBlock dc[2];
    fxu::Tilt tilt[2];
    fxu::Ramp rg, rf, rb, rt;
};

// ---------------------------------------------------------------------------------------------
class CrushFx final : public FxModule {
public:
    void prepare(double sr, int) override
    {
        sampleRate = sr;
        for (auto& t : tilt) t.setFreq(1000.0, sr);
        reset();
    }
    void reset() override
    {
        counter = 1e9; // force capture on first sample
        held[0] = held[1] = 0.f;
        period = 1.0;
        rng = 0x9E3779B9u;
        first = true;
        tilt[0].reset(); tilt[1].reset();
    }
    void process(float* left, float* right, int n, const float* p, const FxContext&) override
    {
        float* ch[2] = { left, right };
        const double rate = std::clamp((double) p[0], 20.0, sampleRate);
        const double basePeriod = sampleRate / rate; // >= 1
        const float bits = std::clamp(p[1], 1.f, 16.f);
        const float jitter = std::clamp(p[2], 0.f, 1.f);
        const float toT = std::clamp(p[3], -1.f, 1.f);
        if (first) { rt.snap(toT); first = false; }
        rt.set(toT, n);
        const float levels = std::exp2(bits - 1.f);       // bits=1 -> {-1,0,1}
        const float invLevels = 1.f / levels;

        for (int i = 0; i < n; ++i) {
            counter += 1.0;
            if (counter >= period) {
                counter -= period;
                if (counter >= period || counter < 0.0) counter = 0.0; // big jump guard
                for (int c = 0; c < 2; ++c) {
                    const float x = std::clamp(ch[c][i], -1.f, 1.f);
                    held[c] = std::floor(x * levels + 0.5f) * invLevels;
                }
                // next hold length
                double per = basePeriod;
                if (jitter > 0.f) per *= 1.0 + (double) jitter * 0.9 * (nextRand() * 2.0 - 1.0);
                period = std::max(per, 1.0);
            }
            float lo, hi;
            fxd::toneGains(rt.next(), lo, hi);
            for (int c = 0; c < 2; ++c)
                ch[c][i] = fxu::softLimit(tilt[c].tick(held[c], lo, hi));
        }
    }

private:
    double nextRand()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (double) (rng >> 8) / 16777216.0;
    }
    double sampleRate = 48000.0, counter = 1e9, period = 1.0;
    float held[2] = { 0.f, 0.f };
    uint32_t rng = 0x9E3779B9u;
    bool first = true;
    fxu::Tilt tilt[2];
    fxu::Ramp rt;
};

} // namespace nf
