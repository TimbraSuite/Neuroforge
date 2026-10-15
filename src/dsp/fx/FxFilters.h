#pragma once
// Filter-family FX: FilterFx, FormantFx, CombFx, EqFx. Header-only.
#include "FxModule.h"
#include "Svf.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace nf {

// ---------------------------------------------------------------------------------------------
class FilterFx final : public FxModule {
public:
    void prepare(double sr, int) override { sampleRate = sr; reset(); }
    void reset() override { svf.reset(); first = true; }

    void process(float* left, float* right, int n, const float* p, const FxContext&) override
    {
        float* ch[2] = { left, right };
        const float gT = Svf::gFromHz(std::clamp(p[0], 20.f, 20000.f), sampleRate);
        const float kT = Svf::kFromRes(p[1]);
        const float mT = std::clamp(p[2], 0.f, 3.f);
        const float dT = fxu::dbToLin(std::clamp(p[3], 0.f, 36.f));
        if (first) { rg.snap(gT); rk.snap(kT); rm.snap(mT); rd.snap(dT); first = false; }
        rg.set(gT, n); rk.set(kT, n); rm.set(mT, n); rd.set(dT, n);

        for (int i = 0; i < n; ++i) {
            const float g = rg.next(), k = rk.next(), m = rm.next(), d = rd.next();
            svf.setGK(g, k);
            // drive crossfade: exactly linear at 0 dB, tanh-saturated above
            const float w = std::min(1.f, (d - 1.f) * 4.f);
            const float comp = 1.f / std::sqrt(d);
            for (int c = 0; c < 2; ++c) {
                float x = ch[c][i];
                if (w > 0.f) x += w * (std::tanh(d * x) * comp - x);
                const SvfOut o = svf.tick(c, x);
                ch[c][i] = fxu::softLimit(Svf::morph(o, k, m));
            }
        }
    }

private:
    double sampleRate = 48000.0;
    bool first = true;
    Svf svf;
    fxu::Ramp rg, rk, rm, rd;
};

// ---------------------------------------------------------------------------------------------
class FormantFx final : public FxModule {
public:
    static constexpr int kBands = 3;

    // Interpolated formants for continuous vowel position v (0..4), shift in semitones.
    static void formantsFor(float v, float shiftSt, double sr, float* f, float* amp)
    {
        static const float F[5][3] = { { 730.f, 1090.f, 2440.f }, { 530.f, 1840.f, 2480.f }, { 270.f, 2290.f, 3010.f },
                                       { 570.f, 840.f, 2410.f }, { 300.f, 870.f, 2240.f } };
        static const float G[3] = { 0.f, -3.f, -8.f }; // strong F2: it carries vowel identity
        v = std::clamp(v, 0.f, 4.f);
        const int i0 = std::min((int) v, 3);
        const float fr = v - (float) i0;
        const float sh = std::exp2(shiftSt / 12.f);
        const float fmax = (float) (sr * 0.45);
        for (int b = 0; b < kBands; ++b) {
            const float lf = std::log(F[i0][b]) * (1.f - fr) + std::log(F[i0 + 1][b]) * fr;
            f[b] = std::clamp(std::exp(lf) * sh, 30.f, fmax);
            amp[b] = fxu::dbToLin(G[b]);
        }
    }

    void prepare(double sr, int) override { sampleRate = sr; reset(); }
    void reset() override { for (auto& s : svf) for (auto& f : s) f.reset(); first = true; }

    void process(float* left, float* right, int n, const float* p, const FxContext&) override
    {
        float* ch[2] = { left, right };
        const float vowel = std::clamp(p[0], 0.f, 4.f);
        const float shift = std::clamp(p[1], -12.f, 12.f);
        const float q01 = std::clamp(p[2], 0.f, 1.f);
        const float spread = std::clamp(p[3], 0.f, 1.f);
        const float Q = 1.5f * std::exp(q01 * std::log(24.f)); // 1.5 .. 36
        const float k = 1.f / Q;

        for (int c = 0; c < 2; ++c) {
            float f[kBands], a[kBands];
            formantsFor(c == 0 ? vowel : std::min(4.f, vowel + spread), shift, sampleRate, f, a);
            // white-noise level matching (softened): power of a unity-peak 2nd-order BP ~ pi/2 * f / Q
            float pw = 0.f;
            for (int b = 0; b < kBands; ++b) pw += a[b] * a[b] * 1.5708f * f[b] / Q;
            const float comp = std::clamp(std::pow((float) (sampleRate * 0.5) / std::max(pw, 1.f), 0.3f), 1.f, 6.f);
            for (int b = 0; b < kBands; ++b) {
                tg[c][b] = Svf::gFromHz(f[b], sampleRate);
                ta[c][b] = a[b] * comp;
            }
        }
        if (first) {
            for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) { cg[c][b] = tg[c][b]; ca[c][b] = ta[c][b]; }
            ck = k; first = false;
        }
        const float inv = 1.f / (float) std::max(n, 1);
        float sg[2][kBands], sa[2][kBands];
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < kBands; ++b) { sg[c][b] = (tg[c][b] - cg[c][b]) * inv; sa[c][b] = (ta[c][b] - ca[c][b]) * inv; }
        const float sk = (k - ck) * inv;

        for (int i = 0; i < n; ++i) {
            ck += sk;
            for (int c = 0; c < 2; ++c) {
                const float x = ch[c][i];
                float y = 0.f;
                for (int b = 0; b < kBands; ++b) {
                    cg[c][b] += sg[c][b]; ca[c][b] += sa[c][b];
                    svf[c][b].setGK(cg[c][b], ck);
                    y += ca[c][b] * ck * svf[c][b].tick(0, x).band;
                }
                ch[c][i] = fxu::softLimit(y);
            }
        }
    }

private:
    double sampleRate = 48000.0;
    bool first = true;
    Svf svf[2][kBands];
    float cg[2][kBands] = {}, ca[2][kBands] = {}, tg[2][kBands] = {}, ta[2][kBands] = {};
    float ck = 0.5f;
};

// ---------------------------------------------------------------------------------------------
class CombFx final : public FxModule {
public:
    void prepare(double sr, int) override
    {
        sampleRate = sr;
        size = 1;
        const int need = (int) (sr / 15.0) + 16;
        while (size < need) size <<= 1;
        mask = size - 1;
        for (auto& b : buf) b.assign((size_t) size, 0.f);
        smoothCoef = (float) (1.0 - std::exp(-1.0 / (0.008 * sr)));
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) std::fill(b.begin(), b.end(), 0.f);
        pos = 0; lp[0] = lp[1] = 0.f; first = true;
    }

    void process(float* left, float* right, int n, const float* p, const FxContext& ctx) override
    {
        float* ch[2] = { left, right };
        float freq = p[0];
        if (p[3] >= 0.5f) freq = p[0] * 0.01f * ctx.noteHz;
        freq = std::clamp(freq, 20.f, (float) (sampleRate * 0.25));
        const float fbT = std::clamp(p[1], -0.99f, 0.99f);
        const float damp = std::clamp(p[2], 0.f, 1.f);
        // damping one-pole: cutoff 20 kHz (damp 0) .. 200 Hz (damp 1)
        const double fc = std::min(20000.0 * std::pow(0.01, (double) damp), sampleRate * 0.45);
        const float a = (float) (1.0 - std::exp(-2.0 * 3.14159265358979 * fc / sampleRate));
        const float lpDelay = (1.f - a) / a; // low-frequency group delay of the damper, compensated
        const float maxD = (float) (size - 8);
        const float dT = std::clamp((float) (sampleRate / freq) - lpDelay, 3.f, maxD);
        if (first) { dSm = dT; fb.snap(fbT); first = false; }
        fb.set(fbT, n);

        for (int i = 0; i < n; ++i) {
            dSm += smoothCoef * (dT - dSm);
            const float f = fb.next();
            const float norm = std::sqrt(1.f - f * f);
            const int di = (int) dSm - 1;               // node delays di .. di+3, target in [di+1, di+2)
            const float t = dSm - (float) di;
            const float h0 = -(t - 1.f) * (t - 2.f) * (t - 3.f) * (1.f / 6.f);
            const float h1 = t * (t - 2.f) * (t - 3.f) * 0.5f;
            const float h2 = -t * (t - 1.f) * (t - 3.f) * 0.5f;
            const float h3 = t * (t - 1.f) * (t - 2.f) * (1.f / 6.f);
            for (int c = 0; c < 2; ++c) {
                const float* b = buf[c].data();
                const int base = pos - di;
                const float d = h0 * b[base & mask] + h1 * b[(base - 1) & mask]
                              + h2 * b[(base - 2) & mask] + h3 * b[(base - 3) & mask];
                lp[c] = fxu::flushDenorm(lp[c] + a * (d - lp[c]));
                const float v = ch[c][i] + f * satFb(lp[c]);
                buf[c][(size_t) (pos & mask)] = v;
                ch[c][i] = fxu::softLimit(v * norm);
            }
            pos = (pos + 1) & mask;
        }
    }

private:
    // linear up to |x|=1 (slope 1, continuous), then compressed; bounded by 2
    static float satFb(float x)
    {
        const float ax = std::fabs(x);
        if (ax <= 1.f) return x;
        const float y = 1.f + std::tanh(ax - 1.f);
        return x < 0.f ? -y : y;
    }

    double sampleRate = 48000.0;
    std::vector<float> buf[2];
    int size = 1, mask = 0, pos = 0;
    float lp[2] = { 0.f, 0.f };
    float dSm = 100.f, smoothCoef = 0.01f;
    bool first = true;
    fxu::Ramp fb;
};

// ---------------------------------------------------------------------------------------------
class EqFx final : public FxModule {
public:
    void prepare(double sr, int) override
    {
        sampleRate = sr;
        for (auto& t : tilt) t.setFreq(800.0, sr);
        reset();
    }
    void reset() override
    {
        hp.reset(); bell.reset();
        tilt[0].reset(); tilt[1].reset();
        first = true;
    }
    void process(float* left, float* right, int n, const float* p, const FxContext&) override
    {
        float* ch[2] = { left, right };
        const float hpG = Svf::gFromHz(std::clamp(p[0], 20.f, 2000.f), sampleRate);
        const float tl = std::clamp(p[1], -12.f, 12.f);
        const float bg = Svf::gFromHz(std::clamp(p[2], 100.f, 10000.f), sampleRate);
        const float gainDb = std::clamp(p[3], -18.f, 18.f);
        const float A = std::pow(10.f, gainDb / 40.f);
        const float bk = 1.f / (1.f * A); // Q = 1
        const float bc = bk * (A * A - 1.f);
        const float lo = fxu::dbToLin(-0.5f * tl), hi = fxu::dbToLin(0.5f * tl);
        if (first) { rhg.snap(hpG); rbg.snap(bg); rbk.snap(bk); rbc.snap(bc); rlo.snap(lo); rhi.snap(hi); first = false; }
        rhg.set(hpG, n); rbg.set(bg, n); rbk.set(bk, n); rbc.set(bc, n); rlo.set(lo, n); rhi.set(hi, n);
        constexpr float kHp = 1.41421356f;

        for (int i = 0; i < n; ++i) {
            hp.setGK(rhg.next(), kHp);
            bell.setGK(rbg.next(), rbk.next());
            const float c2 = rbc.next(), l = rlo.next(), h = rhi.next();
            for (int c = 0; c < 2; ++c) {
                float x = hp.tick(c, ch[c][i]).high;
                x = tilt[c].tick(x, l, h);
                x += c2 * bell.tick(c, x).band;
                ch[c][i] = fxu::softLimit(x);
            }
        }
    }

private:
    double sampleRate = 48000.0;
    bool first = true;
    Svf hp, bell;
    fxu::Tilt tilt[2];
    fxu::Ramp rhg, rbg, rbk, rbc, rlo, rhi;
};

} // namespace nf
