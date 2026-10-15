#pragma once
// Dynamics / stereo effects: OTT (3-band up+down compressor) and Width.
#include "FxModule.h"
#include "Crossover.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace nf {

// ------------------------------------------------------------------------------------------
class OttFx final : public FxModule {
public:
    void prepare(double sr, int) override
    {
        fs_ = (float) sr;
        xo_.prepare(sr);
        xo_.setFrequencies(88.f, std::min(2500.f, 0.4f * fs_), true);
        reset();
    }
    void reset() override
    {
        xo_.reset();
        for (auto& b : band_) b = Band {};
        cnt_ = 0; first_ = true;
    }
    void process(float* L, float* R, int n, const float* p, const FxContext&) override
    {
        const float depthT = std::clamp(p[0], 0.f, 1.f);
        const float time = std::clamp(p[1], 0.f, 1.f);
        const float up = std::clamp(p[2], 0.f, 1.f);
        const float down = std::clamp(p[3], 0.f, 1.f);
        if (first_) { depthPrev_ = depthT; first_ = false; }
        static const float kScale[3] = { 1.5f, 1.f, 0.7f };
        for (int b = 0; b < 3; ++b) {
            const float atk = (1.f + 9.f * time) * kScale[b] * 0.001f;
            const float rel = 0.02f * std::pow(15.f, time) * kScale[b];
            atkC_[b] = 1.f - std::exp(-1.f / (atk * fs_));
            relC_[b] = 1.f - std::exp(-1.f / (rel * fs_));
        }
        const float makeup = 6.f * down;
        const float inv = 1.f / (float) std::max(n, 1);
        const float dStep = (depthT - depthPrev_) * inv;
        float depth = depthPrev_;
        constexpr float thD = -12.f, thU = -34.f;
        for (int i = 0; i < n; ++i) {
            depth += dStep;
            float bands[3][2];
            {
                float lo[2], mid[2], hi[2];
                xo_.tick(L[i], R[i], lo, mid, hi);
                bands[0][0] = lo[0]; bands[0][1] = lo[1];
                bands[1][0] = mid[0]; bands[1][1] = mid[1];
                bands[2][0] = hi[0]; bands[2][1] = hi[1];
            }
            const bool update = cnt_ == 0;
            float oL = 0.f, oR = 0.f;
            for (int b = 0; b < 3; ++b) {
                Band& s = band_[b];
                const float pw = 0.5f * (bands[b][0] * bands[b][0] + bands[b][1] * bands[b][1]);
                s.env += (pw - s.env) * (pw > s.env ? atkC_[b] : relC_[b]);
                if (update) {
                    const float lvl = 10.f * std::log10(2.f * s.env + 1e-12f);
                    float gdb = 0.f;
                    if (lvl > thD) gdb -= (lvl - thD) * 0.75f * down;
                    if (lvl < thU) {
                        const float gate = std::clamp((lvl + 80.f) / 20.f, 0.f, 1.f);
                        gdb += std::min((thU - lvl) * 0.75f, 30.f) * up * gate * gate * (3.f - 2.f * gate);
                    }
                    gdb += makeup;
                    const float gT = std::exp(gdb * 0.11512925f);
                    s.step = (gT - s.gain) * (1.f / (float) kUpdate);
                }
                s.gain += s.step;
                const float ge = (1.f - depth) + depth * s.gain;
                oL += bands[b][0] * ge;
                oR += bands[b][1] * ge;
            }
            cnt_ = (cnt_ + 1) % kUpdate;
            L[i] = fxb::softLimit(oL, 1.4f, 2.f);
            R[i] = fxb::softLimit(oR, 1.4f, 2.f);
        }
        depthPrev_ = depthT;
    }

private:
    static constexpr int kUpdate = 8;
    struct Band { float env = 0.f, gain = 1.f, step = 0.f; };
    LR4ThreeBand xo_;
    Band band_[3];
    float fs_ = 48000.f, atkC_[3] {}, relC_[3] {}, depthPrev_ = 0.5f;
    int cnt_ = 0;
    bool first_ = true;
};

// ------------------------------------------------------------------------------------------
class WidthFx final : public FxModule {
public:
    void prepare(double sr, int) override
    {
        fs_ = (float) sr;
        int size = 1;
        while (size < (int) (0.02 * sr) + 8) size <<= 1;
        buf_.assign((size_t) size, 0.f);
        mask_ = size - 1;
        xo_.prepare(sr);
        reset();
    }
    void reset() override
    {
        std::fill(buf_.begin(), buf_.end(), 0.f);
        xo_.reset();
        w_ = 0; first_ = true;
    }
    void process(float* L, float* R, int n, const float* p, const FxContext&) override
    {
        const float wT = std::clamp(p[0], 0.f, 2.f);
        const float hT = std::clamp(p[1], 0.f, 20.f) * 0.001f * fs_;
        const float mono = std::clamp(p[2], 20.f, 1000.f);
        xo_.setFrequency(mono, first_);
        if (first_) { wPrev_ = wT; hPrev_ = hT; first_ = false; }
        const float inv = 1.f / (float) std::max(n, 1);
        const float wStep = (wT - wPrev_) * inv, hStep = (hT - hPrev_) * inv;
        float wd = wPrev_, h = hPrev_;
        for (int i = 0; i < n; ++i) {
            wd += wStep; h += hStep;
            float lowL, lowR, hiL, hiR;
            xo_.tick(L[i], R[i], lowL, lowR, hiL, hiR);
            const float m = 0.5f * (lowL + lowR);
            const float mid = 0.5f * (hiL + hiR);
            const float side = 0.5f * (hiL - hiR) * wd;
            const float hl = mid + side, hr = mid - side;
            buf_[(size_t) (w_ & mask_)] = hr;
            const float pos = (float) w_ - h;
            const float fl = std::floor(pos);
            const float fr = pos - fl;
            const int i0 = (int) fl;
            const float a = buf_[(size_t) (i0 & mask_)], b = buf_[(size_t) ((i0 + 1) & mask_)];
            const float hd = a + fr * (b - a);
            ++w_;
            L[i] = fxb::softLimit(m + hl, 4.f, 6.f);
            R[i] = fxb::softLimit(m + hd, 4.f, 6.f);
        }
        w_ &= mask_;
        wPrev_ = wT; hPrev_ = hT;
    }

private:
    float fs_ = 48000.f, wPrev_ = 1.f, hPrev_ = 0.f;
    std::vector<float> buf_;
    int mask_ = 0, w_ = 0;
    LR4Crossover xo_;
    bool first_ = true;
};

} // namespace nf
