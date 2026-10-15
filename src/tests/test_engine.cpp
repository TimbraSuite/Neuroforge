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
