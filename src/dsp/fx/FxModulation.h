#pragma once
// Modulation-type effects: Phaser, Flanger, FreqShift, RingMod, Disperse.
#include "FxModule.h"
#include "Crossover.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace nf {

// ------------------------------------------------------------------------------------------
class PhaserFx final : public FxModule {
public:
    void prepare(double sr, int) override { fs_ = (float) sr; reset(); }
    void reset() override
    {
        for (auto& c : z_) for (auto& s : c) s = 0.f;
        fb_[0] = fb_[1] = 0.f;
        first_ = true;
    }
    void process(float* L, float* R, int n, const float* p, const FxContext&) override
    {
        const float fTarget = std::clamp(p[0], 20.f, std::min(15000.f, 0.45f * fs_));
        const float fbT = std::clamp(p[1], -0.95f, 0.95f);
        const float stT = std::clamp(std::round(p[2]), 2.f, (float) kMax);
        const float spT = std::clamp(p[3], 0.f, 1.f);
        if (first_) { lc_ = std::log(fTarget); fbS_ = fbT; stS_ = stT; spS_ = spT; first_ = false; }
        const float sm = 1.f - std::exp(-(float) kChunk / (0.005f * fs_));
        float* io[2] = { L, R };
        for (int start = 0; start < n; start += kChunk) {
            const int len = std::min(kChunk, n - start);
            lc_ += (std::log(fTarget) - lc_) * sm;
            fbS_ += (fbT - fbS_) * sm;
            stS_ += (stT - stS_) * sm;
            spS_ += (spT - spS_) * sm;
            const float center = std::exp(lc_);
            const float ns = std::max(2.f, stS_);
            const int active = std::min(kMax, (int) std::ceil(stS_ - 1e-4f));
            for (int ch = 0; ch < 2; ++ch) {
                const float cc = ch == 0 ? center : center * 1.07f;
                for (int i = 0; i < kMax; ++i) {
                    const float t = std::min(1.f, (float) i / (ns - 1.f));
                    float f = cc * std::exp2(spS_ * 2.f * (2.f * t - 1.f));
                    f = std::clamp(f, 15.f, 0.45f * fs_);
                    const float g = std::tan(fxb::kPi * f / fs_);
                    G_[ch][i] = g / (1.f + g);
                    W_[ch][i] = std::clamp(stS_ - (float) i, 0.f, 1.f);
                }
            }
            for (int ch = 0; ch < 2; ++ch) {
                float* x = io[ch] + start;
                float* z = z_[ch];
                for (int s = 0; s < len; ++s) {
                    const float in = x[s] + fxb::kDenorm;
                    float sig = in + fbS_ * fxb::fastTanh(fb_[ch]);
                    for (int i = 0; i < active; ++i) {
                        const float v = (sig - z[i]) * G_[ch][i];
                        const float lp = v + z[i];
                        z[i] = lp + v;
                        const float ap = 2.f * lp - sig;
                        sig += W_[ch][i] * (ap - sig);
                    }
                    sig = std::clamp(sig, -16.f, 16.f);
                    fb_[ch] = sig;
                    x[s] = 0.5f * (in + sig);
                }
            }
        }
    }

private:
    static constexpr int kMax = 12, kChunk = 16;
    float fs_ = 48000.f, lc_ = 6.f, fbS_ = 0.f, stS_ = 6.f, spS_ = 0.5f;
    bool first_ = true;
    float z_[2][kMax] {}, G_[2][kMax] {}, W_[2][kMax] {}, fb_[2] {};
};

// ------------------------------------------------------------------------------------------
class FlangerFx final : public FxModule {
public:
    void prepare(double sr, int) override
    {
        fs_ = (float) sr;
        int size = 1;
        while (size < (int) (0.03 * sr) + 16) size <<= 1;
        for (auto& b : buf_) b.assign((size_t) size, 0.f);
        mask_ = size - 1;
        reset();
    }
    void reset() override
    {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.f);
        w_ = 0; lp_[0] = lp_[1] = 0.f; first_ = true;
    }
    void process(float* L, float* R, int n, const float* p, const FxContext&) override
    {
        const float maxD = 0.0245f * fs_;   // 15 ms * 1.6 stereo offset = 24 ms
        const float dL = std::clamp(p[0] * 0.001f * fs_, 3.f, maxD);
        const float stereo = std::clamp(p[2], 0.f, 1.f);
        const float dR = std::clamp(dL * (1.f + 0.6f * stereo), 3.f, maxD);
        const float fbT = std::clamp(p[1], -0.97f, 0.97f);
        const float damp = std::clamp(p[3], 0.f, 1.f);
        const float fc = std::min(20000.f * std::pow(0.08f, damp), 0.45f * fs_);
        const float a = std::exp(-2.f * fxb::kPi * fc / fs_);
        if (first_) { dPrev_[0] = dL; dPrev_[1] = dR; fbPrev_ = fbT; first_ = false; }
        const float inv = 1.f / (float) std::max(n, 1);
        const float dTgt[2] = { dL, dR };
        float* io[2] = { L, R };
        const float fbStep = (fbT - fbPrev_) * inv;
        for (int ch = 0; ch < 2; ++ch) {
            float* x = io[ch];
            float* buf = buf_[ch].data();
            const float dStep = (dTgt[ch] - dPrev_[ch]) * inv;
            float d = dPrev_[ch], fb = fbPrev_, lp = lp_[ch];
            int w = w_;
            for (int i = 0; i < n; ++i) {
                d += dStep; fb += fbStep;
                const float in = x[i] + fxb::kDenorm;
                const float pos = (float) w - d;
                const float fl = std::floor(pos);
                const float fr = pos - fl;
                const int i0 = (int) fl;
                const float xm1 = buf[(i0 - 1) & mask_], x0 = buf[i0 & mask_];
                const float x1 = buf[(i0 + 1) & mask_], x2 = buf[(i0 + 2) & mask_];
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                const float dl = ((c3 * fr + c2) * fr + c1) * fr + x0;
                lp = a * lp + (1.f - a) * dl;
                buf[w & mask_] = in + fxb::fastTanh(fb * lp);
                x[i] = 0.5f * (in + dl);
                ++w;
            }
            lp_[ch] = lp;
            dPrev_[ch] = dTgt[ch];
            if (ch == 1) w_ = w & mask_;
        }
        fbPrev_ = fbT;
    }

private:
    float fs_ = 48000.f, dPrev_[2] {}, fbPrev_ = 0.f, lp_[2] {};
    std::vector<float> buf_[2];
    int mask_ = 0, w_ = 0;
    bool first_ = true;
};

// ------------------------------------------------------------------------------------------
class FreqShiftFx final : public FxModule {
public:
    void prepare(double sr, int) override { fs_ = (float) sr; reset(); }
    void reset() override
    {
        for (auto& c : hil_) c = Hilbert {};
        for (int c = 0; c < 2; ++c) { cs_[c] = 1.0; sn_[c] = 0.0; fb_[c] = 0.f; }
        first_ = true;
    }
    void process(float* L, float* R, int n, const float* p, const FxContext&) override
    {
        const float shT = std::clamp(p[0], -1000.f, 1000.f);
        const float fbT = std::clamp(p[1], 0.f, 0.95f);
        const float st = std::clamp(p[2], 0.f, 1.f);
        if (first_) { shS_ = shT; fbS_ = fbT; stS_ = st; first_ = false; }
        const float sm = 1.f - std::exp(-(float) kChunk / (0.005f * fs_));
        float* io[2] = { L, R };
        for (int start = 0; start < n; start += kChunk) {
            const int len = std::min(kChunk, n - start);
            shS_ += (shT - shS_) * sm;
            fbS_ += (fbT - fbS_) * sm;
            stS_ += (st - stS_) * sm;
            for (int ch = 0; ch < 2; ++ch) {
                const float sh = ch == 0 ? shS_ : shS_ * (1.f - 2.f * stS_);
                const double w = 2.0 * 3.14159265358979323846 * (double) sh / (double) fs_;
                const double cw = std::cos(w), sw = std::sin(w);
                double c = cs_[ch], s = sn_[ch];
                float* x = io[ch] + start;
                Hilbert& h = hil_[ch];
                float fb = fb_[ch];
                for (int i = 0; i < len; ++i) {
                    const float in = x[i] + fbS_ * fxb::fastTanh(fb) + fxb::kDenorm;
                    float re, im;
                    h.tick(in, re, im);
                    const float o = (float) ((double) re * c + (double) im * s);
                    fb = o;
                    x[i] = o;
                    const double nc = c * cw - s * sw;
                    s = c * sw + s * cw;
                    c = nc;
                }
                const double m = 1.0 / std::sqrt(c * c + s * s);
                cs_[ch] = c * m; sn_[ch] = s * m;
                fb_[ch] = fb;
            }
        }
    }

private:
    static constexpr int kChunk = 16;
    // Olli Niemitalo's 8-coefficient 90-degree allpass pair (a^2 form: y = a2*(x + y[-2]) - x[-2]).
    struct Hilbert {
        float x1[4] {}, x2[4] {}, y1[4] {}, y2[4] {};   // path A
        float u1[4] {}, u2[4] {}, v1[4] {}, v2[4] {};   // path B
        float delay = 0.f;
        static constexpr float A[4] = { 0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
                                        0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f };
        static constexpr float B[4] = { 0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
                                        0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f };
        // re = in-phase (delayed path), im = quadrature
        inline void tick(float in, float& re, float& im)
        {
            float a = in;
            for (int k = 0; k < 4; ++k) {
                const float y = A[k] * (a + y2[k]) - x2[k];
                x2[k] = x1[k]; x1[k] = a; y2[k] = y1[k]; y1[k] = y;
                a = y;
            }
            float b = in;
            for (int k = 0; k < 4; ++k) {
                const float y = B[k] * (b + v2[k]) - u2[k];
                u2[k] = u1[k]; u1[k] = b; v2[k] = v1[k]; v1[k] = y;
                b = y;
            }
            re = delay;   // path A delayed by one sample
            delay = a;
            im = b;
        }
    };
    float fs_ = 48000.f, shS_ = 0.f, fbS_ = 0.f, stS_ = 0.f, fb_[2] {};
    double cs_[2] { 1.0, 1.0 }, sn_[2] { 0.0, 0.0 };
    Hilbert hil_[2];
    bool first_ = true;
};

// ------------------------------------------------------------------------------------------
class RingModFx final : public FxModule {
public:
    void prepare(double sr, int) override { fs_ = (float) sr; reset(); }
    void reset() override { phase_ = 0.f; first_ = true; }
    void process(float* L, float* R, int n, const float* p, const FxContext& ctx) override
    {
        float f = p[1] >= 0.5f ? p[0] / 100.f * ctx.noteHz : p[0];
        f = std::clamp(f, 0.f, 0.45f * fs_);
        const float shape = std::clamp(p[2], 0.f, 1.f);
        if (first_) { fPrev_ = f; shPrev_ = shape; first_ = false; }
        const float inv = 1.f / (float) std::max(n, 1);
        const float fStep = (f - fPrev_) * inv, shStep = (shape - shPrev_) * inv;
        float fc = fPrev_, sh = shPrev_, ph = phase_;
        for (int i = 0; i < n; ++i) {
            fc += fStep; sh += shStep;
            const float dt = fc / fs_;
            float sq = ph < 0.5f ? 1.f : -1.f;
            sq += blep(ph, dt);
            float p2 = ph + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
            sq -= blep(p2, dt);
            const float sn = std::sin(2.f * fxb::kPi * ph);
            const float car = sn + sh * (sq - sn);
            L[i] *= car; R[i] *= car;
            ph += dt; if (ph >= 1.f) ph -= 1.f;
        }
        phase_ = ph; fPrev_ = f; shPrev_ = shape;
    }

private:
    static inline float blep(float t, float dt)
    {
        if (dt <= 0.f) return 0.f;
        if (t < dt) { const float x = t / dt; return x + x - x * x - 1.f; }
        if (t > 1.f - dt) { const float x = (t - 1.f) / dt; return x * x + x + x + 1.f; }
        return 0.f;
    }
    float fs_ = 48000.f, phase_ = 0.f, fPrev_ = 200.f, shPrev_ = 0.f;
    bool first_ = true;
};

// ------------------------------------------------------------------------------------------
class DisperseFx final : public FxModule {
public:
    static constexpr int kStages = 64;
    void prepare(double sr, int) override { fs_ = (float) sr; reset(); }
    void reset() override
    {
        for (auto& c : s1_) for (auto& s : c) s = 0.f;
        for (auto& c : s2_) for (auto& s : c) s = 0.f;
        hi_ = 0; first_ = true;
    }
    void process(float* L, float* R, int n, const float* p, const FxContext&) override
    {
        const float f = std::clamp(p[0], 20.f, std::min(10000.f, 0.45f * fs_));
        const float cnt = std::clamp(p[1], 0.f, 1.f) * (float) kStages;
        const float q = 0.3f * std::pow(8.f / 0.3f, std::clamp(p[2], 0.f, 1.f));
        const float spread = std::clamp(p[3], 0.f, 1.f);
        const float fR = std::min(f * std::exp2(spread), 0.45f * fs_);
        const float k = 1.f / q;
        if (first_) { fPrev_[0] = f; fPrev_[1] = fR; kPrev_ = k; cPrev_ = cnt; first_ = false; }
        const float inv = 1.f / (float) std::max(n, 1);
        const float fT[2] = { f, fR };
        float* io[2] = { L, R };
        const float cStep = (cnt - cPrev_) * inv;
        const float kMul = std::pow(k / kPrev_, inv);
        for (int ch = 0; ch < 2; ++ch) {
            float* x = io[ch];
            float* s1 = s1_[ch];
            float* s2 = s2_[ch];
            float fc = fPrev_[ch], kc = kPrev_, c = cPrev_;
            const float fMul = std::pow(fT[ch] / fPrev_[ch], inv);
            for (int i = 0; i < n; ++i) {
                fc *= fMul; kc *= kMul; c += cStep;
                const float g = std::tan(fxb::kPi * std::min(fc, 0.45f * fs_) / fs_);
                const float a1 = 1.f / (1.f + g * (g + kc));
                const float k2 = 2.f * kc;
                const int full = std::clamp((int) c, 0, kStages);
                const float frac = c - (float) full;
                float sig = x[i] + fxb::kDenorm;
                for (int j = 0; j < full; ++j) {
                    const float hp = (sig - (g + kc) * s1[j] - s2[j]) * a1;
                    const float v1 = g * hp;
                    const float bp = v1 + s1[j];
                    s1[j] = bp + v1;
                    const float v2 = g * bp;
                    const float lp = v2 + s2[j];
                    s2[j] = lp + v2;
                    sig -= k2 * bp;
                }
                if (full < kStages && frac > 0.f) {
                    const int j = full;
                    const float hp = (sig - (g + kc) * s1[j] - s2[j]) * a1;
                    const float v1 = g * hp;
                    const float bp = v1 + s1[j];
                    s1[j] = bp + v1;
                    const float v2 = g * bp;
                    const float lp = v2 + s2[j];
                    s2[j] = lp + v2;
                    sig -= frac * k2 * bp;
                }
                x[i] = fxb::softLimit(sig, 2.f, 4.f);
            }
            fPrev_[ch] = fT[ch];
        }
        const int used = std::min(kStages, (int) std::ceil(std::max(cnt, cPrev_)) + 1);
        kPrev_ = k; cPrev_ = cnt;
        for (int j = used; j < hi_; ++j)
            for (int ch = 0; ch < 2; ++ch) s1_[ch][j] = s2_[ch][j] = 0.f;
        hi_ = used;
    }

private:
    float fs_ = 48000.f, fPrev_[2] { 400.f, 400.f }, kPrev_ = 1.f, cPrev_ = 0.f;
    float s1_[2][kStages] {}, s2_[2][kStages] {};
    int hi_ = 0;
    bool first_ = true;
};

} // namespace nf
