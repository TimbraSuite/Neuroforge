// Tests for FxDistortion.h / FxFilters.h / Svf.h
#include "TestMain.h"
#include "dsp/fx/FxModule.h"
#include "dsp/fx/Svf.h"
#include "dsp/fx/FxDistortion.h"
#include "dsp/fx/FxFilters.h"
#include <chrono>
#include <complex>
#include <cstdint>
#include <random>
#include <vector>

using namespace nf;

namespace {

struct Range { float lo, hi; bool log; };

// Mirrors the parameter table of FxModule.cpp (real units).
struct TypeDesc { const char* name; Range r[4]; };
const TypeDesc kDrive   { "Drive",   { { 0, 36, 0 }, { 0, 1, 0 }, { -1, 1, 0 }, { -1, 1, 0 } } };
const TypeDesc kFold    { "Fold",    { { 0, 30, 0 }, { 0, 1, 0 }, { -1, 1, 0 }, { -1, 1, 0 } } };
const TypeDesc kCrush   { "Crush",   { { 200, 48000, 1 }, { 1, 16, 0 }, { 0, 1, 0 }, { -1, 1, 0 } } };
const TypeDesc kFilter  { "Filter",  { { 20, 20000, 1 }, { 0, 1, 0 }, { 0, 3, 0 }, { 0, 24, 0 } } };
const TypeDesc kFormant { "Formant", { { 0, 4, 0 }, { -12, 12, 0 }, { 0, 1, 0 }, { 0, 1, 0 } } };
const TypeDesc kComb    { "Comb",    { { 20, 5000, 1 }, { -0.99f, 0.99f, 0 }, { 0, 1, 0 }, { 0, 1, 0 } } };
const TypeDesc kEq      { "EQ",      { { 20, 2000, 1 }, { -12, 12, 0 }, { 100, 10000, 1 }, { -18, 18, 0 } } };

float randParam(std::mt19937& rng, const Range& r, bool discrete = false)
{
    std::uniform_real_distribution<float> u(0.f, 1.f);
    const float pick = u(rng);
    float x;
    if (pick < 0.15f) x = 0.f; else if (pick < 0.30f) x = 1.f; else x = u(rng);
    float v = r.log ? r.lo * std::pow(r.hi / r.lo, x) : r.lo + x * (r.hi - r.lo);
    if (discrete) v = std::round(v);
    return v;
}

template <class Fx>
void fuzz(const TypeDesc& d, double sr, bool discreteLast)
{
    Fx fx;
    fx.prepare(sr, 64);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> nz(-1.f, 1.f);
    FxContext ctx; ctx.sampleRate = sr;
    float L[64], R[64];
    float maxAbs = 0.f;
    bool bad = false;
    double phase = 0.0;
    for (int set = 0; set < 200; ++set) {
        if (set % 50 == 49) fx.reset();
        for (int blk = 0; blk < 6; ++blk) {
            float p[4];
            for (int i = 0; i < 4; ++i) p[i] = randParam(rng, d.r[i], discreteLast && i == 3);
            ctx.noteHz = 40.f + 200.f * (nz(rng) * 0.5f + 0.5f);
            for (int i = 0; i < 64; ++i) {
                if (set & 1) { L[i] = nz(rng); R[i] = nz(rng); }
                else {
                    phase += (double) ctx.noteHz / sr; phase -= std::floor(phase);
                    L[i] = (float) (2.0 * phase - 1.0); R[i] = -L[i];
                }
            }
            fx.process(L, R, 64, p, ctx);
            for (int i = 0; i < 64; ++i) {
                if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad = true;
                maxAbs = std::max({ maxAbs, std::fabs(L[i]), std::fabs(R[i]) });
            }
        }
    }
    NF_INFO("fuzz %-8s sr=%6.0f max|out|=%.3f\n", d.name, sr, maxAbs);
    NF_CHECK(!bad);
    NF_CHECK(maxAbs < 8.f);
}

// --- minimal FFT ---
void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

std::vector<double> powerSpectrum(const std::vector<float>& x, bool hann)
{
    const size_t n = x.size();
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) {
        const double w = hann ? 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (double) i / (double) n) : 1.0;
        a[i] = x[i] * w;
    }
    fft(a);
    std::vector<double> ps(n / 2);
    for (size_t i = 0; i < n / 2; ++i) ps[i] = std::norm(a[i]);
    return ps;
}

double rmsTail(const std::vector<float>& x, size_t from)
{
    double s = 0; for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt(s / (double) (x.size() - from));
}

double spectralCentroid(const std::vector<float>& x, double sr)
{
    auto ps = powerSpectrum(x, true);
    double num = 0, den = 0;
    for (size_t i = 1; i < ps.size(); ++i) { const double f = (double) i * sr / (double) x.size(); num += f * ps[i]; den += ps[i]; }
    return num / den;
}

} // namespace

// ------------------------------------------------------------------------------------------------
NF_TEST(fxa_fuzz_all)
{
    for (double sr : { 48000.0, 96000.0, 192000.0 }) {
        fuzz<DriveFx>(kDrive, sr, false);
        fuzz<FoldFx>(kFold, sr, false);
        fuzz<CrushFx>(kCrush, sr, false);
        fuzz<FilterFx>(kFilter, sr, false);
        fuzz<FormantFx>(kFormant, sr, false);
        fuzz<CombFx>(kComb, sr, true);
        fuzz<EqFx>(kEq, sr, false);
    }
}

NF_TEST(fxa_svf_lowpass_and_notch)
{
    const double sr = 48000.0, fc = 1000.0;
    auto gainAt = [&](double f, float res, float mode) {
        Svf s;
        s.setParams((float) fc, res, sr);
        std::vector<float> y;
        for (int i = 0; i < 48000; ++i) {
            const float x = (float) std::sin(2.0 * 3.14159265358979323846 * f * i / sr);
            y.push_back(Svf::morph(s.tick(0, x), s.k(), mode));
        }
        return rmsTail(y, 24000) * std::sqrt(2.0);
    };
    // res chosen so k = sqrt(2) (Butterworth): -3 dB at cutoff
    const float resBw = (2.f - 1.41421356f) / 1.96f;
    const double atFc = gainAt(fc, resBw, 0.f);
    const double two = gainAt(fc * 4.0, resBw, 0.f);
    NF_INFO("svf LP: at fc %.2f dB, 2 oct above %.2f dB\n", 20 * std::log10(atFc), 20 * std::log10(two));
    NF_CHECK_NEAR(20 * std::log10(atFc), -3.0, 0.4);
    NF_CHECK(20 * std::log10(two) < -22.0);
    NF_CHECK(gainAt(100.0, resBw, 0.f) > 0.98);
    const double notch = gainAt(fc, resBw, 3.f);
    NF_INFO("svf notch depth at fc: %.1f dB\n", 20 * std::log10(notch + 1e-12));
    NF_CHECK(notch < 0.01);
    NF_CHECK(gainAt(fc * 0.25, resBw, 3.f) > 0.9);
    // resonance 1: finite, resonant peak ~ Q
    const double pk = gainAt(fc, 1.f, 0.f);
    NF_INFO("svf LP res=1 peak gain %.1f\n", pk);
    NF_CHECK(pk > 15.0 && pk < 30.0);
}

NF_TEST(fxa_svf_modulation_stable)
{
    Svf s;
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    float mx = 0;
    for (int i = 0; i < 200000; ++i) {
        s.setParams(20.f * std::pow(1000.f, u(rng)), 1.f, 96000.0);
        const SvfOut o = s.tick(0, u(rng) * 2.f - 1.f);
        mx = std::max({ mx, std::fabs(o.low), std::fabs(o.band), std::fabs(o.high) });
        NF_CHECK(std::isfinite(o.low));
    }
    NF_INFO("svf random per-sample cutoff mod, max state out %.1f\n", mx);
    NF_CHECK(mx < 500.f);
}

NF_TEST(fxa_drive_adaa_aliasing)
{
    const double sr = 48000.0;
    const size_t N = 16384;
    const int bin0 = 1025; // 3002.9 Hz, bin-centred
    const double f0 = bin0 * sr / (double) N;
    std::vector<float> in(N), outA(N), outN(N);
    for (size_t i = 0; i < N; ++i) in[i] = (float) (0.9 * std::sin(2.0 * 3.14159265358979323846 * f0 * (double) i / sr));

    DriveFx fx; fx.prepare(sr, 64);
    const float p[4] = { 36.f, 0.f, 0.f, 0.f };
    FxContext ctx; ctx.sampleRate = sr;
    outA = in; std::vector<float> r = in;
    for (size_t i = 0; i < N; i += 64) fx.process(outA.data() + i, r.data() + i, 64, p, ctx);
    const float g = std::pow(10.f, 36.f / 20.f);
    for (size_t i = 0; i < N; ++i) outN[i] = std::tanh(g * in[i]);

    auto aliasDb = [&](const std::vector<float>& x) {
        auto ps = powerSpectrum(x, true);
        std::vector<char> harm(ps.size(), 0);
        for (int h = 1; h * bin0 + 3 < (int) ps.size(); ++h)
            for (int d = -3; d <= 3; ++d) harm[(size_t) (h * bin0 + d)] = 1;
        for (int d = 0; d < 4; ++d) harm[(size_t) d] = 1;
        double a = 0, hsum = 0;
        for (size_t i = 0; i < ps.size(); ++i) (harm[i] ? hsum : a) += ps[i];
        return 10 * std::log10(a / hsum);
    };
    const double dA = aliasDb(outA);
    const double dN = aliasDb(outN);
    NF_INFO("alias energy vs harmonics: ADAA %.1f dB, naive tanh %.1f dB (improvement %.1f dB)\n", dA, dN, dN - dA);
    NF_CHECK(dA < dN - 3.0);
}

NF_TEST(fxa_drive_level_and_bias)
{
    DriveFx fx; fx.prepare(96000.0, 64);
    FxContext ctx; ctx.sampleRate = 96000.0;
    float peak = 0;
    for (float drive : { 0.f, 12.f, 36.f }) {
        fx.reset();
        const float p[4] = { drive, 0.5f, 0.f, 0.f };
        float mx = 0;
        std::vector<float> L(64), R(64);
        for (int b = 0; b < 400; ++b) {
            for (int i = 0; i < 64; ++i) L[(size_t) i] = R[(size_t) i] = (float) std::sin(2 * 3.14159265 * 110.0 * (b * 64 + i) / 96000.0);
            fx.process(L.data(), R.data(), 64, p, ctx);
            for (float v : L) mx = std::max(mx, std::fabs(v));
        }
        NF_INFO("drive %.0f dB: peak out %.3f\n", drive, mx);
        peak = std::max(peak, mx);
    }
    NF_CHECK(peak <= 1.5f);
    // bias creates even harmonics (2nd harmonic of 110 Hz) and DC is removed
    const size_t N = 16384;
    std::vector<float> y(N), z(N);
    const float pb[4] = { 18.f, 0.f, 0.6f, 0.f }, p0[4] = { 18.f, 0.f, 0.f, 0.f };
    auto run = [&](const float* p, std::vector<float>& out) {
        fx.reset();
        for (size_t i = 0; i < N; ++i) out[i] = (float) (0.7 * std::sin(2 * 3.14159265 * (96000.0 / 16384.0 * 19.0) * (double) i / 96000.0));
        std::vector<float> r = out;
        for (size_t i = 0; i < N; i += 64) fx.process(out.data() + i, r.data() + i, 64, p, ctx);
    };
    run(pb, y); run(p0, z);
    auto h2 = [&](const std::vector<float>& x) { auto ps = powerSpectrum(x, true); return 10 * std::log10(ps[38] + 1e-20) - 10 * std::log10(ps[19] + 1e-20); };
    NF_INFO("2nd/1st harmonic: bias %.1f dB, no bias %.1f dB\n", h2(y), h2(z));
    NF_CHECK(h2(y) > h2(z) + 20.0);
    double mean = 0; for (size_t i = 0; i < N; ++i) mean += y[i];
    NF_CHECK(std::fabs(mean / (double) N) < 0.03);
}

NF_TEST(fxa_fold_and_crush_basic)
{
    FxContext ctx; ctx.sampleRate = 48000.0;
    // Fold with little drive is ~ linear; with high drive stays within [-1,1]
    {
        FoldFx fx; fx.prepare(48000.0, 64);
        const float p[4] = { 30.f, 0.5f, 0.f, 0.f };
        float mx = 0;
        std::vector<float> L(64), R(64);
        for (int b = 0; b < 200; ++b) {
            for (int i = 0; i < 64; ++i) L[(size_t) i] = R[(size_t) i] = (float) std::sin(2 * 3.14159265 * 100.0 * (b * 64 + i) / 48000.0);
            fx.process(L.data(), R.data(), 64, p, ctx);
            for (float v : L) mx = std::max(mx, std::fabs(v));
        }
        NF_CHECK(mx <= 1.3f);
        NF_CHECK(mx > 0.5f);
    }
    // Crush: 4 bits -> at most 2^4+1 distinct values; rate 4800 @48k -> 10-sample holds
    {
        CrushFx fx; fx.prepare(48000.0, 64);
        const float p[4] = { 4800.f, 4.f, 0.f, 0.f };
        std::vector<float> L(64), R(64);
        std::vector<float> all;
        for (int b = 0; b < 20; ++b) {
            for (int i = 0; i < 64; ++i) L[(size_t) i] = R[(size_t) i] = (float) (0.8 * std::sin(2 * 3.14159265 * 300.0 * (b * 64 + i) / 48000.0));
            fx.process(L.data(), R.data(), 64, p, ctx);
            all.insert(all.end(), L.begin(), L.end());
        }
        int changes = 0;
        for (size_t i = 1; i < all.size(); ++i) if (all[i] != all[i - 1]) ++changes;
        NF_INFO("crush: %d value changes in %zu samples\n", changes, all.size());
        NF_CHECK(changes <= (int) (all.size() / 10) + 2);
        std::vector<float> uniq;
        for (float v : all) if (std::find(uniq.begin(), uniq.end(), v) == uniq.end()) uniq.push_back(v);
        NF_CHECK(uniq.size() <= 17);
    }
}

NF_TEST(fxa_comb_tuned_to_note)
{
    const double sr = 48000.0;
    const size_t N = 16384;
    CombFx fx; fx.prepare(sr, 64);
    FxContext ctx; ctx.sampleRate = sr; ctx.noteHz = 100.f;
    const float p[4] = { 100.f, 0.8f, 0.f, 1.f };
    std::vector<float> L(N, 0.f), R(N, 0.f);
    L[0] = R[0] = 1.f;
    for (size_t i = 0; i < N; i += 64) fx.process(L.data() + i, R.data() + i, 64, p, ctx);
    auto ps = powerSpectrum(L, false);
    const double binHz = sr / (double) N;
    auto peakNear = [&](double f) {
        const int c = (int) std::lround(f / binHz);
        double m = 0; for (int d = -4; d <= 4; ++d) m = std::max(m, ps[(size_t) (c + d)]);
        return 10 * std::log10(m);
    };
    for (int k = 1; k <= 6; ++k) {
        const double on = peakNear(100.0 * k), off = peakNear(100.0 * k + 50.0);
        NF_INFO("comb harmonic %d: on %.1f dB, between %.1f dB\n", k, on, off);
        NF_CHECK(on > off + 10.0);
    }
    // negative feedback: peaks at odd multiples of 50 Hz
    fx.reset();
    const float pn[4] = { 100.f, -0.8f, 0.f, 1.f };
    std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
    L[0] = R[0] = 1.f;
    for (size_t i = 0; i < N; i += 64) fx.process(L.data() + i, R.data() + i, 64, pn, ctx);
    ps = powerSpectrum(L, false);
    NF_CHECK(peakNear(150.0) > peakNear(200.0) + 10.0);
    NF_CHECK(peakNear(50.0) > peakNear(100.0) + 10.0);
    // key track off: Freq in Hz
    fx.reset();
    const float pf[4] = { 240.f, 0.8f, 0.f, 0.f };
    std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
    L[0] = R[0] = 1.f;
    for (size_t i = 0; i < N; i += 64) fx.process(L.data() + i, R.data() + i, 64, pf, ctx);
    ps = powerSpectrum(L, false);
    NF_CHECK(peakNear(480.0) > peakNear(600.0) + 10.0);
}

NF_TEST(fxa_formant_vowels)
{
    const double sr = 48000.0;
    const size_t N = 32768;
    std::mt19937 rng(9);
    std::uniform_real_distribution<float> nz(-1.f, 1.f);
    std::vector<float> noise(N);
    for (auto& v : noise) v = nz(rng) * 0.5f;
    FxContext ctx; ctx.sampleRate = sr;
    auto run = [&](float vowel, float spread, std::vector<float>* outR = nullptr) {
        FormantFx fx; fx.prepare(sr, 64);
        const float p[4] = { vowel, 0.f, 0.5f, spread };
        std::vector<float> L = noise, R = noise;
        for (size_t i = 0; i < N; i += 64) fx.process(L.data() + i, R.data() + i, 64, p, ctx);
        if (outR) *outR = R;
        return L;
    };
    const auto a = run(0.f, 0.f), ii = run(2.f, 0.f);
    const double ca = spectralCentroid(a, sr), ci = spectralCentroid(ii, sr);
    NF_INFO("formant centroid: A %.0f Hz, I %.0f Hz; rms in %.3f A %.3f I %.3f\n", ca, ci, rmsTail(noise, 0), rmsTail(a, 0), rmsTail(ii, 0));
    NF_CHECK(ci > ca * 1.2);
    // level roughly matched to input
    NF_CHECK(rmsTail(a, 0) > 0.3 * rmsTail(noise, 0) && rmsTail(a, 0) < 3.0 * rmsTail(noise, 0));
    // spread: right channel moves a vowel further
    std::vector<float> R2;
    auto l2 = run(0.f, 1.f, &R2);
    NF_CHECK(spectralCentroid(R2, sr) > spectralCentroid(l2, sr) * 1.1);
    // continuous: vowel 1 lies between
    const double ce = spectralCentroid(run(1.f, 0.f), sr);
    NF_CHECK(std::isfinite(ce));
}

NF_TEST(fxa_filter_and_eq_response)
{
    const double sr = 96000.0;
    FxContext ctx; ctx.sampleRate = sr;
    auto gain = [&](FxModule& fx, const float* p, double f) {
        fx.reset();
        std::vector<float> L(48000), R;
        for (size_t i = 0; i < L.size(); ++i) L[i] = (float) std::sin(2.0 * 3.14159265358979 * f * (double) i / sr);
        R = L;
        for (size_t i = 0; i < L.size(); i += 64) fx.process(L.data() + i, R.data() + i, 64, p, ctx);
        return 20 * std::log10(rmsTail(L, 24000) * std::sqrt(2.0));
    };
    FilterFx flt; flt.prepare(sr, 64);
    const float pl[4] = { 1000.f, 0.3f, 0.f, 0.f };
    NF_CHECK(gain(flt, pl, 100.0) > -1.0);
    NF_CHECK(gain(flt, pl, 4000.0) < -20.0);
    const float ph[4] = { 1000.f, 0.3f, 2.f, 0.f };
    NF_CHECK(gain(flt, ph, 100.0) < -30.0);
    NF_CHECK(gain(flt, ph, 10000.0) > -2.0);

    EqFx eq; eq.prepare(sr, 64);
    const float flat[4] = { 20.f, 0.f, 1000.f, 0.f };
    NF_CHECK_NEAR(gain(eq, flat, 1000.0), 0.0, 0.2);
    NF_CHECK_NEAR(gain(eq, flat, 100.0), 0.0, 0.3);
    const float bell[4] = { 20.f, 0.f, 1000.f, 12.f };
    NF_CHECK_NEAR(gain(eq, bell, 1000.0), 12.0, 0.5);
    NF_CHECK(std::fabs(gain(eq, bell, 80.0)) < 2.0);
    const float tilt[4] = { 20.f, 12.f, 1000.f, 0.f };
    const double tl = gain(eq, tilt, 50.0), th = gain(eq, tilt, 15000.0);
    NF_INFO("EQ tilt +12: 50 Hz %.1f dB, 15 kHz %.1f dB\n", tl, th);
    NF_CHECK(th - tl > 9.0);
    const float lc[4] = { 200.f, 0.f, 1000.f, 0.f };
    NF_CHECK(gain(eq, lc, 50.0) < -20.0);
    NF_CHECK_NEAR(gain(eq, lc, 2000.0), 0.0, 0.5);
}

NF_TEST(fxa_performance)
{
    const double sr = 96000.0;
    FxContext ctx; ctx.sampleRate = sr;
    const int total = (int) sr;
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> nz(-1.f, 1.f);
    std::vector<float> src((size_t) total);
    for (auto& v : src) v = nz(rng) * 0.5f;
    auto bench = [&](const char* name, FxModule& fx, const float* p) {
        fx.prepare(sr, 64);
        std::vector<float> L = src, R = src;
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < total; i += 64) fx.process(L.data() + i, R.data() + i, std::min(64, total - i), p, ctx);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
        NF_INFO("perf %-8s %.2f ms per second of stereo audio @96k\n", name, ms);
        NF_CHECK(std::isfinite(L[1000]));
    };
    { DriveFx f;   const float p[4] = { 18.f, 0.4f, 0.2f, 0.f };          bench("Drive", f, p); }
    { FoldFx f;    const float p[4] = { 12.f, 0.5f, 0.1f, 0.f };          bench("Fold", f, p); }
    { CrushFx f;   const float p[4] = { 12000.f, 8.f, 0.3f, 0.f };        bench("Crush", f, p); }
    { FilterFx f;  const float p[4] = { 1200.f, 0.6f, 0.5f, 12.f };       bench("Filter", f, p); }
    { FormantFx f; const float p[4] = { 1.5f, 0.f, 0.5f, 0.5f };          bench("Formant", f, p); }
    { CombFx f;    const float p[4] = { 200.f, 0.7f, 0.3f, 1.f };         bench("Comb", f, p); }
    { EqFx f;      const float p[4] = { 60.f, 3.f, 1000.f, 6.f };         bench("EQ", f, p); }
}
