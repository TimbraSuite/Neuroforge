#include "TestMain.h"
#include "dsp/osc/OscApi.h"
#include "dsp/osc/OscFft.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace nf;

namespace {
bool finiteAll(const std::vector<float>& v)
{
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}

// Ratio (dB) of energy in bins not within +-4 bins of a harmonic of f0 vs total.
double inharmonicDb(const std::vector<float>& x, double f0, double sr)
{
    const size_t n = 65536;
    std::vector<oscdetail::cplx> a(n);
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n);
        a[i] = oscdetail::cplx(x[i + 4096] * w, 0.0);
    }
    oscdetail::fftPow2(a.data(), n, false);
    double tot = 0, bad = 0;
    for (size_t k = 5; k < n / 2; ++k) {
        const double e = std::norm(a[k]);
        tot += e;
        const double hz = (double) k * sr / (double) n;
        const double hpos = hz / f0;
        const double dist = std::fabs(hpos - std::round(hpos)) * f0 / (sr / (double) n);
        if (dist > 12.0 || std::round(hpos) < 1) bad += e;
    }
    return 10.0 * std::log10(bad / tot + 1e-30);
}

std::vector<float> renderMono(WavetableOsc& o, int n, double hz, const OscParams& p, const float* pm = nullptr,
                              const float* xin = nullptr)
{
    std::vector<float> L((size_t) n, 0.f), R((size_t) n, 0.f), M((size_t) n, 0.f);
    const int B = 512;
    for (int s = 0; s < n; s += B) {
        int m = std::min(B, n - s);
        o.process(L.data() + s, R.data() + s, m, hz, p, pm ? pm + s : nullptr, xin ? xin + s : nullptr, M.data() + s);
    }
    return M;
}

OscParams plain()
{
    OscParams p;
    p.unison = 1;
    p.detune = 0;
    return p;
}
} // namespace

NF_TEST(osc_resample_cycle)
{
    std::vector<float> s(100);
    for (int i = 0; i < 100; ++i) s[(size_t) i] = (float) std::sin(2 * M_PI * 3 * i / 100.0);
    auto r = WavetableBank::resampleCycle(s.data(), 100);
    double err = 0;
    for (int i = 0; i < 2048; ++i) err = std::max(err, std::fabs(r[(size_t) i] - std::sin(2 * M_PI * 3 * i / 2048.0)));
    NF_CHECK(err < 1e-4);
    NF_INFO("resample max err %.2e\n", err);
}

NF_TEST(osc_builtin_tables_build)
{
    NF_CHECK(tables::getNumBuiltIn() == 12);
    for (int t = 0; t < tables::getNumBuiltIn(); ++t) {
        auto b = tables::getBuiltIn(t);
        NF_CHECK(b != nullptr);
        if (!b) continue;
        NF_CHECK(b->getNumFrames() >= 16 && b->getNumFrames() <= 64);
        double peak = 0;
        bool allEnergy = true;
        for (int f = 0; f < b->getNumFrames(); ++f) {
            double e = 0, dc = 0;
            const float* d = b->getFrame(f);
            for (int i = 0; i < 2048; ++i) { e += d[i] * d[i]; dc += d[i]; peak = std::max(peak, (double) std::fabs(d[i])); }
            if (e / 2048.0 < 1e-6) allEnergy = false;
            NF_CHECK(std::fabs(dc / 2048.0) < 1e-4);
        }
        NF_CHECK(allEnergy);
        NF_CHECK_NEAR(peak, 1.0, 0.01);
        NF_INFO("%-15s frames=%d\n", b->getName().c_str(), b->getNumFrames());
    }
    NF_CHECK(tables::getBuiltInNames().size() == 12);
}

NF_TEST(osc_saw_bandlimited_1x)
{
    auto bank = tables::getBuiltIn(0); // Saw>Square, position 0 = saw
    const double sr = 48000.0;
    for (double f0 : { 4003.7, 1234.5, 311.3 }) {
        WavetableOsc o;
        o.prepare(sr);
        o.setBank(bank);
        OscParams p = plain();
        p.position = 0.f;
        o.noteOn(true, 1, p);
        auto y = renderMono(o, 65536 + 4096, f0, p);
        const double db = inharmonicDb(y, f0, sr);
        NF_INFO("saw %.1f Hz @48k: inharmonic %.1f dB\n", f0, db);
        NF_CHECK(db < -60.0);
    }
}

NF_TEST(osc_alias_info_pm_sync)
{
    auto bank = tables::getBuiltIn(0);
    const double sr = 96000.0, f0 = 440.3;
    std::vector<float> pm(65536 + 4096);
    for (size_t i = 0; i < pm.size(); ++i) pm[i] = (float) std::sin(2 * M_PI * 440.3 * 2.0 * (double) i / sr);
    {
        WavetableOsc o; o.prepare(sr); o.setBank(bank);
        OscParams p = plain(); p.fmAmount = 1.f;
        o.noteOn(true, 1, p);
        auto y = renderMono(o, 65536 + 4096, f0, p, pm.data());
        NF_INFO("PM index 8 (non-harm. includes sidebands): %.1f dB\n", inharmonicDb(y, f0, sr));
        NF_CHECK(finiteAll(y));
    }
    for (WarpMode wm : { WarpMode::Sync, WarpMode::PhaseDist, WarpMode::BendPlus }) {
        WavetableOsc o; o.prepare(sr); o.setBank(bank);
        OscParams p = plain(); p.warp = wm; p.warpAmount = 0.43f;
        o.noteOn(true, 1, p);
        auto y = renderMono(o, 65536 + 4096, f0, p);
        NF_INFO("warp %-9s 0.43 @96k 440Hz: inharmonic %.1f dB\n", warpModeName(wm), inharmonicDb(y, f0, sr));
    }
}

NF_TEST(osc_dc_peak_nan)
{
    auto bank = tables::getBuiltIn(0);
    const double sr = 48000.0;
    {
        WavetableOsc o; o.prepare(sr); o.setBank(bank);
        OscParams p = plain();
        o.noteOn(true, 1, p);
        auto y = renderMono(o, 48000, 110.0, p);
        double mx = 0, sum = 0;
        for (float v : y) { mx = std::max(mx, (double) std::fabs(v)); sum += v; }
        NF_INFO("unison1 peak %.3f dc %.5f\n", mx, sum / y.size());
        NF_CHECK(mx <= 1.1);
        NF_CHECK(std::fabs(sum / y.size()) < 0.01);
    }
    {
        WavetableOsc o; o.prepare(sr); o.setBank(bank);
        OscParams p; p.unison = 16; p.detune = 0.5f;
        o.noteOn(true, 3, p);
        auto y = renderMono(o, 48000, 110.0, p);
        double mx = 0;
        for (float v : y) mx = std::max(mx, (double) std::fabs(v));
        NF_INFO("unison16 peak %.3f\n", mx);
        NF_CHECK(finiteAll(y));
        NF_CHECK(mx <= 4.3);
    }
    std::vector<float> pm(8192), xin(8192);
    for (size_t i = 0; i < pm.size(); ++i) {
        pm[i] = (float) std::sin(0.05 * (double) i);
        xin[i] = (float) std::sin(0.31 * (double) i);
    }
    for (int wi = 0; wi < (int) WarpMode::NumModes; ++wi)
        for (float amt : { 0.f, 0.5f, 1.f })
            for (int t : { 0, 3, 5, 8 }) {
                WavetableOsc o; o.prepare(sr); o.setBank(tables::getBuiltIn(t));
                OscParams p; p.unison = 5; p.warp = (WarpMode) wi; p.warpAmount = amt; p.fmAmount = 1.f; p.position = 0.6f;
                o.noteOn(true, 9, p);
                auto y = renderMono(o, 8192, 55.0 * (1 + wi), p, pm.data(), xin.data());
                bool ok = finiteAll(y);
                double mx = 0;
                for (float v : y) mx = std::max(mx, (double) std::fabs(v));
                NF_CHECK(ok);
                NF_CHECK(mx < 6.0);
            }
}

NF_TEST(osc_retrigger_deterministic_and_beating)
{
    const double sr = 48000.0;
    OscParams p; p.unison = 7; p.phaseRand = 1.f; p.detune = 0.4f;
    auto run = [&](uint32_t seed) {
        WavetableOsc o; o.prepare(sr); o.setBank(tables::getBuiltIn(0));
        o.noteOn(true, seed, p);
        return renderMono(o, 4096, 82.4, p);
    };
    auto a = run(5), b = run(5), c = run(6);
    NF_CHECK(a == b);
    NF_CHECK(a != c);

    // beating: two sines at +-50 cents around 200 Hz -> 11.5 Hz envelope
    WavetableOsc o; o.prepare(sr); o.setBank(tables::getBuiltIn(1)); // Basic Shapes, position 0 = sine
    OscParams q; q.unison = 2; q.detune = 1.f; q.position = 0.f; q.blend = 1.f;
    o.noteOn(true, 1, q);
    auto y = renderMono(o, 48000, 200.0, q);
    std::vector<double> env;
    for (size_t s = 0; s + 240 <= y.size(); s += 240) {
        double mx = 0;
        for (size_t i = 0; i < 240; ++i) mx = std::max(mx, (double) std::fabs(y[s + i]));
        env.push_back(mx);
    }
    double emax = *std::max_element(env.begin(), env.end());
    int beats = 0; bool low = false;
    for (double e : env) {
        if (!low && e < 0.35 * emax) { low = true; ++beats; }
        if (low && e > 0.7 * emax) low = false;
    }
    NF_INFO("beats in 1 s: %d (expect ~11.5)\n", beats);
    NF_CHECK(beats >= 10 && beats <= 13);
}

NF_TEST(osc_free_running_keeps_phase)
{
    OscParams p = plain();
    WavetableOsc o; o.prepare(48000); o.setBank(tables::getBuiltIn(1));
    o.noteOn(true, 1, p);
    auto a = renderMono(o, 100, 123.0, p);
    o.noteOn(false, 1, p);
    auto b = renderMono(o, 100, 123.0, p);
    NF_CHECK(std::fabs(a[0] - b[0]) > 1e-3 || std::fabs(a[1] - b[1]) > 1e-3);
}

NF_TEST(osc_performance_and_fm_sub)
{
    const double sr = 96000.0;
    WavetableOsc o; o.prepare(sr); o.setBank(tables::getBuiltIn(4));
    OscParams p; p.unison = 8; p.detune = 0.3f; p.position = 0.4f;
    o.noteOn(true, 1, p);
    std::vector<float> L(96000, 0.f), R(96000, 0.f);
    double ms = 1e9;
    for (int rep = 0; rep < 5; ++rep) {
        auto t0 = std::chrono::steady_clock::now();
        for (int s = 0; s < 96000; s += 512) {
            int m = std::min(512, 96000 - s);
            o.process(L.data() + s, R.data() + s, m, 55.0, p, nullptr, nullptr, nullptr);
        }
        auto t1 = std::chrono::steady_clock::now();
        ms = std::min(ms, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    NF_INFO("8-voice unison 1 s @96k (best of 5): %.2f ms\n", ms);
    NF_CHECK(ms < 60.0);

    FmOperator fm; fm.prepare(48000); fm.noteOn(true);
    SubOsc sub; sub.prepare(48000); sub.noteOn(true);
    std::vector<float> a(48000), s2(48000, 0.f);
    FmOpParams fp; fp.ratio = 2.f; fp.feedback = 1.f; fp.shape = 0.8f;
    fm.process(a.data(), 48000, 100.0, fp);
    SubParams sp; sp.octave = -1; sp.shape = 0.f;
    sub.process(s2.data(), 48000, 100.0, sp);
    NF_CHECK(finiteAll(a));
    double mx = 0; for (float v : a) mx = std::max(mx, (double) std::fabs(v));
    NF_CHECK(mx <= 1.3);
    // sub: 50 Hz sine -> 50 zero-crossing pairs per second
    int zc = 0;
    for (int i = 1; i < 48000; ++i) if (s2[(size_t) i - 1] < 0 && s2[(size_t) i] >= 0) ++zc;
    NF_CHECK(zc >= 49 && zc <= 51);
    for (float sh : { 0.2f, 0.5f, 0.8f, 1.f }) {
        std::vector<float> s3(4800, 0.f); sp.shape = sh; sub.process(s3.data(), 4800, 4000.0, sp);
        NF_CHECK(finiteAll(s3));
    }
}
