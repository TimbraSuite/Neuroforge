#include "TestMain.h"
#include "engine/Archetypes.h"
#include "engine/Capture.h"
#include "engine/Engine.h"
#include <algorithm>
#include <vector>

using namespace nf;

namespace {
struct Out { std::vector<float> L, R; };

Out renderNote(const ParamState& st, int samples, int note = 29, double sr = 48000)
{
    Engine e;
    e.prepare(sr, 256);
    Out o { std::vector<float>((size_t) samples), std::vector<float>((size_t) samples) };
    Transport t; t.playing = true;
    MidiEvent on { 0, 0x90, (uint8_t) note, 100 };
    for (int pos = 0; pos < samples; pos += 256) {
        int n = std::min(256, samples - pos);
        t.ppq = pos / sr * 174.0 / 60.0;
        e.render(o.L.data() + pos, o.R.data() + pos, n, st.v.data(), pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, t);
    }
    return o;
}

double rms(const std::vector<float>& x)
{
    double s = 0; for (float v : x) s += (double) v * v;
    return std::sqrt(s / std::max<size_t>(1, x.size()));
}
} // namespace

NF_TEST(engine_factory_presets_finite_and_audible)
{
    for (auto& fp : factoryPresets()) {
        auto o = renderNote(fp.state(), 48000);
        bool finite = true; float peak = 0;
        for (size_t i = 0; i < o.L.size(); ++i) {
            finite &= std::isfinite(o.L[i]) && std::isfinite(o.R[i]);
            peak = std::max({ peak, std::abs(o.L[i]), std::abs(o.R[i]) });
        }
        NF_CHECK(finite);
        NF_CHECK(peak < 1.5f);
        NF_CHECK(rms(o.L) > 0.01);
        NF_INFO("%-22s peak %.2f  rms %.3f\n", fp.name.c_str(), peak, rms(o.L));
    }
}

NF_TEST(engine_render_is_deterministic)
{
    auto st = generatePatch(Archetype::FmGrowl, 3);
    auto a = renderNote(st, 24000), b = renderNote(st, 24000);
    NF_CHECK(a.L == b.L && a.R == b.R);
}

NF_TEST(engine_generate_is_seeded)
{
    auto a = generatePatch(Archetype::Reese, 5), b = generatePatch(Archetype::Reese, 5), c = generatePatch(Archetype::Reese, 6);
    NF_CHECK(a.v == b.v);
    NF_CHECK(a.v != c.v);
}

NF_TEST(engine_mutate_keeps_patch_sane)
{
    auto st = generatePatch(Archetype::Talker, 1);
    auto m = st;
    mutatePatch(m, 0.8f, 42);
    NF_CHECK(m.v != st.v);
    NF_CHECK(m[pid::guardOn] == st[pid::guardOn]);
    auto o = renderNote(m, 24000);
    NF_CHECK(rms(o.L) > 0.01);
}

NF_TEST(engine_preset_text_roundtrip)
{
    auto st = generatePatch(Archetype::Metallic, 2);
    ParamState back;
    NF_CHECK(back.fromText(st.toText()));
    double maxErr = 0;
    for (size_t i = 0; i < st.v.size(); ++i) maxErr = std::max(maxErr, (double) std::abs(st.v[i] - back.v[i]));
    NF_CHECK(maxErr < 1e-4);
}

NF_TEST(engine_capture_builds_playable_table)
{
    auto st = generatePatch(Archetype::FmGrowl, 2);
    std::array<std::shared_ptr<const WavetableBank>, kNumCaptures> none {};
    CaptureSettings cs; cs.beats = 2; cs.maxFrames = 64;
    auto bank = renderCapture(st, none, cs, "test");
    NF_CHECK(bank != nullptr);
    if (!bank) return;
    NF_CHECK(bank->getNumFrames() == 64);
    // frames should differ (the capture holds the movement)
    double diff = 0;
    for (int i = 0; i < WavetableBank::kFrameSize; ++i) diff += std::abs(bank->getFrame(0)[i] - bank->getFrame(32)[i]);
    NF_CHECK(diff > 1.0);
    // next-generation patch plays the capture
    auto ng = nextGenerationState(st, 0);
    Engine e; e.prepare(48000, 256); e.setCapture(0, bank);
    std::vector<float> L(4800), R(4800);
    Transport t; MidiEvent on { 0, 0x90, 29, 100 };
    for (int pos = 0; pos < 4800; pos += 256) {
        int n = std::min(256, 4800 - pos);
        e.render(L.data() + pos, R.data() + pos, n, ng.v.data(), pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, t);
    }
    NF_CHECK(rms(L) > 0.01);
    auto restored = bankFromFrames(bankToFrames(*bank), "r");
    NF_CHECK(restored && restored->getNumFrames() == bank->getNumFrames());
}

NF_TEST(engine_sub_is_mono_and_low_guard_splits)
{
    auto st = generatePatch(Archetype::Reese, 1); // very wide unison
    auto o = renderNote(st, 48000);
    // crude: one-pole lowpass at ~80 Hz on L-R vs L+R
    double side = 0, mid = 0, a1[4] = {}, a2[4] = {};
    const double a = 1 - std::exp(-2 * M_PI * 80 / 48000.0);
    for (size_t i = 0; i < o.L.size(); ++i) {
        double l1 = o.L[i], l2 = o.R[i];
        for (int k = 0; k < 4; ++k) { a1[k] += (l1 - a1[k]) * a; l1 = a1[k]; a2[k] += (l2 - a2[k]) * a; l2 = a2[k]; }
        side += (l1 - l2) * (l1 - l2); mid += (l1 + l2) * (l1 + l2);
    }
    const double ratioDb = 10 * std::log10(side / std::max(1e-12, mid));
    NF_INFO("sub side/mid %.1f dB\n", ratioDb);
    NF_CHECK(ratioDb < -20);
}

NF_TEST(stepper_curves_stay_in_range)
{
    for (int s = 0; s < (int) stepCurveNames().size(); ++s)
        for (int i = 0; i <= 100; ++i) {
            const float v = stepCurve(s, i / 100.f);
            NF_CHECK(v >= -1e-6f && v <= 1.f + 1e-6f);
        }
}

NF_TEST(stepper_runs_and_follows_its_pattern)
{
    ParamState st = generatePatch(Archetype::Reese, 1);
    using namespace pid;
    // 4 steps of 1/16: hold 1, off, hold 0.5, off
    st.setReal(stepper(0, stSteps), 4);
    st.setReal(stepper(0, stSmooth), 0);
    const int shapes[4] = { 1, 0, 1, 0 };
    const float amts[4] = { 1.f, 1.f, 0.5f, 1.f };
    for (int k = 0; k < 4; ++k) { st.setReal(stepper(0, stShape0 + k), (float) shapes[k]); st[stepper(0, stAmt0 + k)] = amts[k]; }
    Engine e; e.prepare(48000, 256);
    Transport t; t.bpm = 120; t.playing = false; // 1/16 at 120 BPM = 125 ms = 6000 samples
    std::vector<float> L(256), R(256);
    MidiEvent on { 0, 0x90, 29, 100 };
    std::vector<float> seen;
    for (int b = 0; b < 100; ++b) { // 25600 samples, ~4.3 steps
        e.render(L.data(), R.data(), 256, st.v.data(), b == 0 ? &on : nullptr, b == 0 ? 1 : 0, t);
        seen.push_back(e.uiStepper[0].load());
    }
    // block 10 (~2560 smp) -> step 1 = 1.0 ; block 35 (~8960) -> step 2 = 0 ; block 60 (~15360) -> step 3 = 0.5
    NF_CHECK_NEAR(seen[10], 1.0, 1e-3);
    NF_CHECK_NEAR(seen[35], 0.0, 1e-3);
    NF_CHECK_NEAR(seen[60], 0.5, 1e-3);
}

NF_TEST(stepper_modulates_a_destination)
{
    using namespace pid;
    ParamState st = generatePatch(Archetype::Reese, 1);
    auto quiet = renderNote(st, 24000);
    // route Step 1 -> master gain down by a lot
    const auto& dsts = modDestinations();
    int c = 0; for (int i = 1; i < (int) dsts.size(); ++i) if (dsts[(size_t) i] == masterGain) c = i;
    for (int x = 0; x < kNumMatrix; ++x)
        if (st.real(mx(x, mxSrc)) == 0) { st.setReal(mx(x, mxSrc), (float) (int) ModSrc::Step1); st.setReal(mx(x, mxDst), (float) c); st.setReal(mx(x, mxAmt), -0.6f); break; }
    auto mod = renderNote(st, 24000);
    NF_INFO("rms plain %.3f, with stepper %.3f\n", rms(quiet.L), rms(mod.L));
    NF_CHECK(rms(mod.L) < rms(quiet.L) * 0.9);
}

NF_TEST(compat_v1_presets_keep_their_mod_sources)
{
    using namespace pid;
    // A v1 file (no version line) stored sources normalized over 25 entries: Motion 2 = index 9 -> 9/24
    std::string v1 = "mx1_src " + std::to_string(9.0f / 24.0f) + "\nmx2_src " + std::to_string(24.0f / 24.0f) + "\n";
    ParamState a;
    NF_CHECK(a.fromText(v1));
    NF_CHECK((int) a.real(mx(0, mxSrc)) == (int) ModSrc::Motion2);
    NF_CHECK((int) a.real(mx(1, mxSrc)) == (int) ModSrc::Random);
    // and the current format round-trips every source, including the new ones
    a.setReal(mx(2, mxSrc), (float) (int) ModSrc::Step2);
    ParamState b;
    NF_CHECK(b.fromText(a.toText()));
    for (int x = 0; x < 3; ++x) NF_CHECK((int) b.real(mx(x, mxSrc)) == (int) a.real(mx(x, mxSrc)));
}
