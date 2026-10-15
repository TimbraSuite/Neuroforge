#include "TestMain.h"
#include "dsp/fx/Crossover.h"
#include "dsp/fx/FxModulation.h"
#include "dsp/fx/FxDynamics.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

using namespace nf;

namespace {
constexpr double kTwoPi = 6.283185307179586;

struct Rng {
    uint32_t s = 12345u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / (float) 0x1000000; }
    float bi() { return next() * 2.f - 1.f; }
};

double dB(double x) { return 20.0 * std::log10(std::max(x, 1e-20)); }

double rmsOf(const std::vector<float>& v, size_t from, size_t to)
{
    double a = 0;
    for (size_t i = from; i < to; ++i) a += (double) v[i] * v[i];
    return std::sqrt(a / (double) (to - from));
}

// Goertzel magnitude (amplitude) of a tone at hz over [from,to)
double toneMag(const std::vector<float>& v, size_t from, size_t to, double hz, double fs)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; ++i) {
        const double ph = kTwoPi * hz * (double) i / fs;
        re += v[i] * std::cos(ph); im += v[i] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (double) (to - from);
}

struct Range { float lo, hi; bool discrete; };
using Ranges = std::vector<Range>;

// Param ranges mirror FxModule.cpp
struct Fuzzable { const char* name; std::unique_ptr<FxModule> (*make)(); Ranges r; };

template <class T> std::unique_ptr<FxModule> mk() { return std::make_unique<T>(); }

std::vector<Fuzzable> allFx()
{
    return {
        { "phaser",    mk<PhaserFx>,    { {20, 15000, 0}, {-0.95f, 0.95f, 0}, {2, 12, 1}, {0, 1, 0} } },
        { "flanger",   mk<FlangerFx>,   { {0.05f, 15, 0}, {-0.97f, 0.97f, 0}, {0, 1, 0}, {0, 1, 0} } },
        { "freqshift", mk<FreqShiftFx>, { {-1000, 1000, 0}, {0, 0.95f, 0}, {0, 1, 0}, {0, 0, 0} } },
        { "ringmod",   mk<RingModFx>,   { {1, 5000, 0}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0} } },
        { "disperse",  mk<DisperseFx>,  { {20, 10000, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0} } },
        { "ott",       mk<OttFx>,       { {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0} } },
        { "width",     mk<WidthFx>,     { {0, 2, 0}, {0, 20, 0}, {20, 1000, 0}, {0, 0, 0} } },
    };
}

float randParam(Rng& r, const Range& g)
{
    const float u = r.next();
    float v;
    if (u < 0.15f) v = g.lo; else if (u < 0.3f) v = g.hi;
    else v = g.lo + (g.hi - g.lo) * r.next();
    // log-ish bias for wide ranges
    if (g.lo > 0 && g.hi / g.lo > 50 && u >= 0.3f) v = g.lo * std::pow(g.hi / g.lo, r.next());
    return g.discrete ? std::round(v) : v;
}

// run a module over a buffer in 64-sample blocks with given param
void runBlocks(FxModule& m, std::vector<float>& l, std::vector<float>& r, const float* p, const FxContext& ctx)
{
    for (size_t i = 0; i < l.size(); i += 64) {
        const int n = (int) std::min<size_t>(64, l.size() - i);
        m.process(l.data() + i, r.data() + i, n, p, ctx);
    }
}
} // namespace

NF_TEST(fxb_fuzz_all)
{
    for (double sr : { 44100.0, 96000.0, 192000.0 }) {
        for (auto& f : allFx()) {
            auto m = f.make();
            m->prepare(sr, 64);
            FxContext ctx; ctx.sampleRate = sr; ctx.noteHz = 55.f;
            Rng rng; rng.s = 777u + (uint32_t) f.name[0];
            float maxAbs = 0.f; bool finite = true;
            float ph = 0.f;
            for (int set = 0; set < 200; ++set) {
                float p[4];
                for (int k = 0; k < 4; ++k) p[k] = randParam(rng, f.r[(size_t) k]);
                // each param set held for 4 blocks, alternating noise / saw
                for (int blk = 0; blk < 4; ++blk) {
                    float l[64], r[64];
                    for (int i = 0; i < 64; ++i) {
                        if (set % 2 == 0) { l[i] = 0.9f * rng.bi(); r[i] = 0.9f * rng.bi(); }
                        else { ph += 55.f / (float) sr; ph -= std::floor(ph); l[i] = r[i] = 1.8f * (ph - 0.5f); }
                    }
                    m->process(l, r, 64, p, ctx);
                    for (int i = 0; i < 64; ++i) {
                        if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false;
                        maxAbs = std::max({ maxAbs, std::fabs(l[i]), std::fabs(r[i]) });
                    }
                }
            }
            NF_CHECK(finite);
            NF_CHECK(maxAbs < 8.f);
            if (!finite || maxAbs >= 8.f) NF_INFO("%s @%.0f: finite=%d maxAbs=%g\n", f.name, sr, (int) finite, maxAbs);
        }
    }
}

NF_TEST(fxb_fuzz_param_change_every_block)
{
    for (auto& f : allFx()) {
        auto m = f.make();
        m->prepare(96000.0, 64);
        FxContext ctx; ctx.sampleRate = 96000.0;
        Rng rng; rng.s = 4242u;
        float maxAbs = 0.f; bool finite = true;
        for (int blk = 0; blk < 800; ++blk) {
            float p[4];
            for (int k = 0; k < 4; ++k) p[k] = randParam(rng, f.r[(size_t) k]);
            float l[64], r[64];
            for (int i = 0; i < 64; ++i) { l[i] = 0.9f * rng.bi(); r[i] = 0.9f * rng.bi(); }
            m->process(l, r, 64, p, ctx);
            for (int i = 0; i < 64; ++i) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false;
                maxAbs = std::max({ maxAbs, std::fabs(l[i]), std::fabs(r[i]) });
            }
        }
        NF_CHECK(finite);
        NF_CHECK(maxAbs < 8.f);
        if (!finite || maxAbs >= 8.f) NF_INFO("%s: finite=%d maxAbs=%g\n", f.name, (int) finite, maxAbs);
    }
}

NF_TEST(fxb_lr4_two_band_flat)
{
    const double fs = 96000.0;
    const size_t N = (size_t) fs;             // 1 s
    const double fx = 1000.0;
    double worst = 0, lowAtX = 0;
    for (double f : { 20., 40., 80., 160., 300., 600., 900., 1000., 1100., 1500., 3000., 6000., 10000., 15000., 20000. }) {
        LR4Crossover xo; xo.prepare(fs); xo.setFrequency((float) fx, true);
        std::vector<float> in(N), lo(N), lo2(N), hi(N), hi2(N), sum(N);
        for (size_t i = 0; i < N; ++i) in[i] = (float) std::sin(kTwoPi * f * (double) i / fs);
        xo.process(in.data(), in.data(), lo.data(), lo2.data(), hi.data(), hi2.data(), (int) N);
        for (size_t i = 0; i < N; ++i) sum[i] = lo[i] + hi[i];
        const double g = dB(rmsOf(sum, N / 2, N) / rmsOf(in, N / 2, N));
        worst = std::max(worst, std::fabs(g));
        if (f == 1000.) lowAtX = dB(rmsOf(lo, N / 2, N) / rmsOf(in, N / 2, N));
    }
    NF_INFO("LR4 sum worst dev %.4f dB, low band at fc %.3f dB\n", worst, lowAtX);
    NF_CHECK(worst < 0.1);
    NF_CHECK_NEAR(lowAtX, -6.02, 0.1);
}

NF_TEST(fxb_lr4_three_band_flat)
{
    const double fs = 96000.0;
    const size_t N = (size_t) fs;
    double worst = 0;
    for (double f : { 20., 50., 88., 150., 400., 1000., 2500., 4000., 9000., 15000., 20000. }) {
        LR4ThreeBand xo; xo.prepare(fs); xo.setFrequencies(88.f, 2500.f, true);
        std::vector<float> in(N), s(N);
        for (size_t i = 0; i < N; ++i) in[i] = (float) std::sin(kTwoPi * f * (double) i / fs);
        for (size_t i = 0; i < N; ++i) {
            float lo[2], mid[2], hi[2];
            xo.tick(in[i], in[i], lo, mid, hi);
            s[i] = lo[0] + mid[0] + hi[0];
        }
        worst = std::max(worst, std::fabs(dB(rmsOf(s, N / 2, N) / rmsOf(in, N / 2, N))));
    }
    NF_INFO("LR4 3-band sum worst dev %.4f dB\n", worst);
    NF_CHECK(worst < 0.2);
}

NF_TEST(fxb_lr4_smooth_freq_change_is_bounded)
{
    LR4Crossover xo; xo.prepare(96000.0); xo.setFrequency(100.f, true);
    Rng r; float mx = 0;
    for (int b = 0; b < 500; ++b) {
        xo.setFrequency(20.f + 3000.f * r.next());
        for (int i = 0; i < 64; ++i) {
            float a, bq, c, d;
            xo.tick(r.bi(), r.bi(), a, bq, c, d);
            mx = std::max({ mx, std::fabs(a), std::fabs(bq), std::fabs(c), std::fabs(d) });
        }
    }
    NF_CHECK(mx < 5.f);
}

NF_TEST(fxb_freqshift_ssb)
{
    const double fs = 96000.0;
    const size_t N = (size_t) fs * 2;
    FreqShiftFx fx; fx.prepare(fs, 64);
    std::vector<float> l(N), r(N);
    for (size_t i = 0; i < N; ++i) l[i] = r[i] = 0.5f * (float) std::sin(kTwoPi * 1000.0 * (double) i / fs);
    const float p[4] = { 100.f, 0.f, 0.f, 0.f };
    FxContext ctx; ctx.sampleRate = fs;
    runBlocks(fx, l, r, p, ctx);
    const size_t a = N / 2, b = N;
    const double up = toneMag(l, a, b, 1100.0, fs), img = toneMag(l, a, b, 900.0, fs);
    const double orig = toneMag(l, a, b, 1000.0, fs);
    NF_INFO("shift +100: 1100Hz %.1f dB, image 900Hz %.1f dB rel, 1000Hz %.1f dB rel\n",
            dB(up), dB(img / up), dB(orig / up));
    NF_CHECK(up > 0.4);
    NF_CHECK(dB(img / up) < -35.0);

    // negative shift moves the other way
    FreqShiftFx fx2; fx2.prepare(fs, 64);
    for (size_t i = 0; i < N; ++i) l[i] = r[i] = 0.5f * (float) std::sin(kTwoPi * 1000.0 * (double) i / fs);
    const float p2[4] = { -100.f, 0.f, 0.f, 0.f };
    runBlocks(fx2, l, r, p2, ctx);
    NF_CHECK(toneMag(l, a, b, 900.0, fs) > 0.4);
    NF_CHECK(dB(toneMag(l, a, b, 1100.0, fs) / toneMag(l, a, b, 900.0, fs)) < -35.0);
}

NF_TEST(fxb_ott_levels)
{
    const double fs = 96000.0;
    const size_t N = (size_t) fs * 2;
    FxContext ctx; ctx.sampleRate = fs;
    for (float amp : { 0.01f, 1.0f }) {
        OttFx fx; fx.prepare(fs, 64);
        std::vector<float> l(N), r(N);
        for (size_t i = 0; i < N; ++i) l[i] = r[i] = amp * (float) std::sin(kTwoPi * 1000.0 * (double) i / fs);
        const std::vector<float> in = l;
        const float p[4] = { 1.f, 0.5f, 1.f, 1.f };
        runBlocks(fx, l, r, p, ctx);
        const double g = dB(rmsOf(l, N / 2, N) / rmsOf(in, N / 2, N));
        NF_INFO("OTT depth 1, amp %.3f: gain %.2f dB\n", amp, g);
        if (amp < 0.1f) NF_CHECK(g > 3.0); else NF_CHECK(g < -1.0);
    }
    // depth 0 is transparent (flat allpass sum)
    OttFx fx; fx.prepare(fs, 64);
    std::vector<float> l(N), r(N);
    for (size_t i = 0; i < N; ++i) l[i] = r[i] = 0.5f * (float) std::sin(kTwoPi * 300.0 * (double) i / fs);
    const std::vector<float> in = l;
    const float p[4] = { 0.f, 0.5f, 1.f, 1.f };
    runBlocks(fx, l, r, p, ctx);
    NF_CHECK_NEAR(dB(rmsOf(l, N / 2, N) / rmsOf(in, N / 2, N)), 0.0, 0.2);
}

NF_TEST(fxb_disperse_group_delay_grows)
{
    const double fs = 96000.0;
    const size_t N = 65536;
    FxContext ctx; ctx.sampleRate = fs;
    double prev = -1; bool mono = true;
    for (float amount : { 0.f, 0.1f, 0.3f, 0.6f, 1.f }) {
        DisperseFx fx; fx.prepare(fs, 64);
        std::vector<float> l(N, 0.f), r(N, 0.f);
        l[0] = r[0] = 1.f;
        const float p[4] = { 800.f, amount, 0.5f, 0.f };
        runBlocks(fx, l, r, p, ctx);
        double e = 0, m1 = 0, m2 = 0;
        for (size_t i = 0; i < N; ++i) { const double v = (double) l[i] * l[i]; e += v; m1 += v * (double) i; }
        const double c = m1 / e;
        for (size_t i = 0; i < N; ++i) m2 += (double) l[i] * l[i] * ((double) i - c) * ((double) i - c);
        const double spread = std::sqrt(m2 / e);
        NF_INFO("disperse amount %.1f: energy %.3f, centroid %.1f smp, rms width %.1f smp\n", amount, e, c, spread);
        if (spread < prev) mono = false;
        prev = spread;
    }
    NF_CHECK(mono);
    NF_CHECK(prev > 50.0);
}

NF_TEST(fxb_width_behaviour)
{
    const double fs = 96000.0;
    const size_t N = 24000;
    FxContext ctx; ctx.sampleRate = fs;
    std::vector<float> l(N), r(N);
    Rng rng;
    for (size_t i = 0; i < N; ++i) { l[i] = 0.5f * (float) std::sin(kTwoPi * 2000.0 * (double) i / fs); r[i] = -l[i]; }
    WidthFx fx; fx.prepare(fs, 64);
    const float p0[4] = { 0.f, 0.f, 20.f, 0.f };
    runBlocks(fx, l, r, p0, ctx);
    double d = 0; for (size_t i = N / 2; i < N; ++i) d = std::max(d, (double) std::fabs(l[i] - r[i]));
    NF_CHECK(d < 1e-3);
    // mono below: 50 Hz out-of-phase content collapses, 2 kHz stays
    WidthFx fx2; fx2.prepare(fs, 64);
    std::vector<float> a(N), b(N);
    for (size_t i = 0; i < N; ++i) { a[i] = 0.5f * (float) std::sin(kTwoPi * 40.0 * (double) i / fs); b[i] = -a[i]; }
    const float p1[4] = { 1.f, 0.f, 400.f, 0.f };
    runBlocks(fx2, a, b, p1, ctx);
    double e = 0; for (size_t i = N / 2; i < N; ++i) e = std::max(e, (double) std::fabs(a[i]));
    NF_INFO("mono-below residual at 40 Hz: %.4f\n", e);
    NF_CHECK(e < 0.05);
    (void) rng;
}

NF_TEST(fxb_ringmod_and_flanger_sanity)
{
    const double fs = 96000.0;
    const size_t N = 96000;
    FxContext ctx; ctx.sampleRate = fs; ctx.noteHz = 100.f;
    // ring mod with sine carrier (shape 0), Hz mode 500: 1 kHz in -> 500 & 1500 Hz
    RingModFx rm; rm.prepare(fs, 64);
    std::vector<float> l(N), r(N);
    for (size_t i = 0; i < N; ++i) l[i] = r[i] = (float) std::sin(kTwoPi * 1000.0 * (double) i / fs);
    const float p[4] = { 500.f, 0.f, 0.f, 0.f };
    runBlocks(rm, l, r, p, ctx);
    NF_CHECK(toneMag(l, N / 2, N, 500.0, fs) > 0.45);
    NF_CHECK(toneMag(l, N / 2, N, 1500.0, fs) > 0.45);
    NF_CHECK(toneMag(l, N / 2, N, 1000.0, fs) < 0.02);
    // keytrack: 500/100*100 = 500 Hz as well
    RingModFx rk; rk.prepare(fs, 64);
    for (size_t i = 0; i < N; ++i) l[i] = r[i] = (float) std::sin(kTwoPi * 1000.0 * (double) i / fs);
    const float pk[4] = { 500.f, 1.f, 0.f, 0.f };
    runBlocks(rk, l, r, pk, ctx);
    NF_CHECK(toneMag(l, N / 2, N, 500.0, fs) > 0.45);
}

NF_TEST(fxb_performance)
{
    const double fs = 96000.0;
    const size_t N = (size_t) fs;
    FxContext ctx; ctx.sampleRate = fs;
    Rng rng;
    std::vector<float> src(N);
    for (auto& v : src) v = 0.5f * rng.bi();
    struct Def { const char* name; std::unique_ptr<FxModule> m; float p[4]; };
    Def defs[] = {
        { "Phaser (6)",     mk<PhaserFx>(),    { 600.f, 0.5f, 6.f, 0.5f } },
        { "Phaser (12)",    mk<PhaserFx>(),    { 600.f, 0.5f, 12.f, 0.5f } },
        { "Flanger",        mk<FlangerFx>(),   { 2.f, 0.6f, 0.2f, 0.2f } },
        { "FreqShift",      mk<FreqShiftFx>(), { 30.f, 0.3f, 0.2f, 0.f } },
        { "RingMod",        mk<RingModFx>(),   { 200.f, 0.f, 0.5f, 0.f } },
        { "Disperse (50%)", mk<DisperseFx>(),  { 400.f, 0.5f, 0.5f, 0.f } },
        { "Disperse (100%)",mk<DisperseFx>(),  { 400.f, 1.f, 0.5f, 0.2f } },
        { "OTT",            mk<OttFx>(),       { 0.5f, 0.5f, 1.f, 1.f } },
        { "Width",          mk<WidthFx>(),     { 1.2f, 5.f, 120.f, 0.f } },
    };
    for (auto& d : defs) {
        d.m->prepare(fs, 64);
        std::vector<float> l = src, r = src;
        const auto t0 = std::chrono::steady_clock::now();
        runBlocks(*d.m, l, r, d.p, ctx);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        NF_INFO("%-16s %.2f ms per 1 s stereo @96k (%.2f%% of one core)\n", d.name, ms, ms / 10.0);
    }
}
