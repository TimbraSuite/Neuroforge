#include "Archetypes.h"
#include "Engine.h"
#include "ModSources.h"
#include "dsp/fx/FxModule.h"
#include <algorithm>
#include <cmath>

namespace nf {

const std::vector<std::string>& archetypeNames()
{
    static const std::vector<std::string> n = { "Reese", "FM Growl", "Talker", "Metallic", "Tearer",
                                                "Hoover", "Stab", "Liquid", "Donk" };
    return n;
}

namespace {

using namespace pid;

// Table indices (see Params.cpp tableChoiceNames)
enum Tbl { tSawSquare, tBasic, tPWM, tFormant, tHarmSweep, tMetal, tReesePulse, tGrowlFM, tSyncSaw, tGrit, tHollow, tPhaseDist };
// Rate division indices (see Params.cpp kDivs)
enum Div { d8bar, d4bar, d2bar, d1bar, d2, d2T, d4d, d4, d4T, d8d, d8, d8T, d16d, d16, d16T, d32, d32T, d64 };
enum Shape { shSine, shTri, shSawUp, shSawDown, shSquare, shSH, shSmooth };
enum Mac { mGrowl, mTalk, mGrit, mMetal, mMotion, mWidth, mSub, mTone };

// Patch-building helper. All values are given in REAL units.
struct Patch {
    ParamState s;
    Rng rng;
    int nextRoute = 0;

    explicit Patch(uint32_t seed) : rng(seed * 2654435761u + 17u) {}

    void set(int id, float real) { s.setReal(id, real); }
    float rnd(float lo, float hi) { return lo + (hi - lo) * rng.uni(); }
    int pick(std::initializer_list<int> opts) { auto i = (int) (rng.uni() * (float) opts.size()); return *(opts.begin() + std::min(i, (int) opts.size() - 1)); }
    bool chance(float p) { return rng.uni() < p; }

    // FX slot with real-unit params (unused params ignored).
    void fx(int slotIdx, FxType t, float p1, float p2 = NAN, float p3 = NAN, float p4 = NAN, float mix = 1.f, float motionAmt = 1.f)
    {
        set(slot(slotIdx, sType), (float) (int) t);
        const auto& info = getFxTypeInfo(t);
        const float v[4] = { p1, p2, p3, p4 };
        for (int k = 0; k < 4; ++k) {
            const auto& pi = info.params[(size_t) k];
            if (!pi.name) continue;
            s[slot(slotIdx, sP1 + k)] = fxRealToNorm(pi, std::isnan(v[k]) ? pi.def : v[k]);
        }
        set(slot(slotIdx, sMix), mix);
        set(slot(slotIdx, sMotion), motionAmt);
    }
    void motionLfo(int ps, int div, int shape, float depth)
    {
        set(pid::motion(ps, mMode), 0); set(pid::motion(ps, mSync), 1);
        set(pid::motion(ps, mDiv), (float) div); set(pid::motion(ps, mShape), (float) shape);
        set(pid::motion(ps, mDepth), depth);
    }
    void motionSteps(int ps, int div, std::initializer_list<float> steps, float depth, float glide)
    {
        set(pid::motion(ps, mMode), 1); set(pid::motion(ps, mSync), 1);
        set(pid::motion(ps, mDiv), (float) div);
        set(pid::motion(ps, mSteps), (float) steps.size());
        int i = 0;
        for (float v : steps) set(pid::motion(ps, mStep0 + i++), v);
        set(pid::motion(ps, mDepth), depth); set(pid::motion(ps, mGlide), glide);
    }
    void lfo(int i, int div, int shape, bool retrig = false)
    {
        set(pid::lfo(i, lSync), 1); set(pid::lfo(i, lDiv), (float) div);
        set(pid::lfo(i, lShape), (float) shape); set(pid::lfo(i, lRetrig), retrig ? 1.f : 0.f);
    }
    void env(int i, float a, float d, float sus, float r, bool loop = false)
    {
        set(pid::env(i, eA), a); set(pid::env(i, eD), d); set(pid::env(i, eS), sus);
        set(pid::env(i, eR), r); set(pid::env(i, eLoop), loop ? 1.f : 0.f);
    }
    // Mod route: amount is in normalized units of the destination (-1..1).
    void route(ModSrc src, int dstParam, float amt)
    {
        if (nextRoute >= kNumMatrix) return;
        const auto& dsts = modDestinations();
        int choice = 0;
        for (int c = 1; c < (int) dsts.size(); ++c) if (dsts[(size_t) c] == dstParam) choice = c;
        if (choice == 0) return;
        set(mx(nextRoute, mxSrc), (float) (int) src);
        set(mx(nextRoute, mxDst), (float) choice);
        set(mx(nextRoute, mxAmt), amt);
        ++nextRoute;
    }
    void macro(int m, int dstParam, float amt) { route((ModSrc) ((int) ModSrc::Macro1 + m), dstParam, amt); }
    void macroValues(std::initializer_list<float> v) { int i = 0; for (float x : v) set(pid::macro(i++), x); }

    void osc(int o, int table, float level, int oct = 0, float fine = 0, int unison = 1, float detune = 0.2f,
             float stereo = 0.5f, float pos = 0.f)
    {
        set(pid::osc(o, oOn), 1); set(pid::osc(o, oTable), (float) table); set(pid::osc(o, oLevel), level);
        set(pid::osc(o, oOct), (float) oct); set(pid::osc(o, oFine), fine); set(pid::osc(o, oUni), (float) unison);
        set(pid::osc(o, oDetune), detune); set(pid::osc(o, oStereo), stereo); set(pid::osc(o, oPos), pos);
    }
    void warp(int o, WarpMode w, float amt) { set(pid::osc(o, oWarp), (float) (int) w); set(pid::osc(o, oWarpAmt), amt); }
    void fmOp(int ratioIdx, float fineCents, float fb, float shape = 0.f)
    {
        set(op + opOn, 1); set(op + opCoarse, (float) ratioIdx); set(op + opFine, fineCents);
        set(op + opFb, fb); set(op + opShape, shape);
    }
};

// Ratio indices: 0:0.25 1:0.5 2:1 3:1.5 4:2 5:3 6:4 7:5 ...
Patch base(uint32_t seed)
{
    Patch p(seed);
    p.set(voiceMode, 2);           // legato
    p.set(glide, p.rnd(0.f, 25.f));
    p.set(guardOn, 1);
    p.set(guardFreq, p.rnd(105.f, 140.f));
    p.set(subDrive, p.rnd(0.05f, 0.25f));
    p.set(subLevel, -6.f);
    p.set(midLevel, 2.f);
    p.set(midLevel, 0.f);
    p.set(masterClip, 1.f);
    p.set(quality, 1);
    // clean, tight sub that tracks the note (riffs are played around F1)
    p.set(sub + subOn, 1); p.set(sub + subOct, 0); p.set(sub + subShape, 0); p.set(sub + subLevelP, 0.7f);
    p.set(sub + subDirect, 1);
    p.env(0, 1.f, 300.f, 1.f, p.rnd(30.f, 60.f));
    p.set(osc(0, oOn), 0); p.set(osc(1, oOn), 0);
    p.set(osc(0, oRetrig), 1); p.set(osc(1, oRetrig), 1);
    return p;
}

// Macro wiring shared by most archetypes, given which slots hold which roles.
struct Roles { int growlSlot = -1, talkPass = -1, gritSlot = -1, metalSlot = -1, ottSlot = -1, toneSlot = -1; };

void wireCommonMacros(Patch& p, const Roles& r)
{
    if (r.talkPass >= 0) p.macro(mTalk, pid::motion(r.talkPass, mDepth), 0.6f);
    if (r.gritSlot >= 0) p.macro(mGrit, slot(r.gritSlot, sP1), 0.35f);
    if (r.ottSlot >= 0) p.macro(mGrit, slot(r.ottSlot, sP1), 0.4f);
    if (r.metalSlot >= 0) p.macro(mMetal, slot(r.metalSlot, sMix), 0.7f);
    for (int ps = 0; ps < kNumPasses; ++ps) p.macro(mMotion, pid::motion(ps, mDepth), 0.35f);
    p.macro(mWidth, midWidth, 0.3f);
    p.macro(mWidth, osc(0, oStereo), 0.4f);
    p.macro(mSub, subLevel, 0.15f);
    p.macro(mSub, subDrive, 0.5f);
    if (r.toneSlot >= 0) p.macro(mTone, slot(r.toneSlot, sP2), 0.35f); // EQ tilt
}

// ------------------------------------------------------------------------------------------------
Patch reese(uint32_t seed)
{
    Patch p = base(seed);
    // two free-running detuned saw stacks: the beating IS the movement
    p.osc(0, tSawSquare, 0.75f, 0, 0, p.pick({ 3, 4, 5 }), p.rnd(0.22f, 0.38f), 0.5f, p.rnd(0.f, 0.15f));
    p.osc(1, p.pick({ tSawSquare, tReesePulse }), 0.6f, p.chance(0.3f) ? 1 : 0, p.rnd(-9.f, -4.f), 2, 0.12f, 0.3f, p.rnd(0.f, 0.3f));
    p.set(osc(0, oRetrig), 0); p.set(osc(1, oRetrig), 0); p.set(osc(0, oPhaseRand), 1); p.set(osc(1, oPhaseRand), 1);
    // PASS 1 — darken + bite; the slow motion breathes the cutoff
    p.fx(0, FxType::Filter, p.rnd(500.f, 1100.f), p.rnd(0.15f, 0.35f), 0.f, p.rnd(3.f, 8.f));
    p.fx(1, FxType::Drive, p.rnd(8.f, 14.f), 0.2f, p.rnd(0.f, 0.2f), -0.2f, 1.f, 0.f);
    p.motionLfo(0, p.pick({ d2bar, d1bar, d2 }), shSine, p.rnd(0.35f, 0.55f));
    // PASS 2 — the classic phaser / flanger smear
    p.fx(4, FxType::Phaser, p.rnd(300.f, 700.f), p.rnd(0.4f, 0.7f), 8.f, 0.6f);
    p.fx(5, FxType::Flanger, p.rnd(1.5f, 4.f), p.rnd(0.3f, 0.6f), 0.3f, 0.3f, 0.35f, -0.6f);
    p.motionLfo(1, p.pick({ d1bar, d2bar, d4bar }), shTri, p.rnd(0.5f, 0.8f));
    // PASS 3 — glue
    p.fx(8, FxType::OTT, p.rnd(0.25f, 0.4f), 0.5f, 0.8f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 30.f, p.rnd(-3.f, -1.f), 350.f, -3.f, 1.f, 0.f);
    p.set(midWidth, 1.2f);
    p.macro(mGrowl, slot(0, sP1), 0.3f);
    p.macro(mGrowl, slot(1, sP1), 0.15f);
    p.macro(mMetal, slot(4, sP2), 0.3f);
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 0, .gritSlot = 1, .metalSlot = 5, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.3f, 0.3f, 0.2f, 0.3f, 0.3f, 0.5f, 0.5f });
    return p;
}

Patch fmGrowl(uint32_t seed)
{
    Patch p = base(seed);
    // Noisia-style: sine carrier, FM operator whose index is driven by a LOOPING envelope
    p.osc(0, tBasic, 0.8f, 0, 0, 1, 0.f, 0.f, 0.f);
    p.fmOp(p.pick({ 2, 2, 4, 1 }), p.rnd(-6.f, 6.f), p.rnd(0.1f, 0.35f), p.rnd(0.f, 0.3f));
    p.set(osc(0, oFm), p.rnd(0.25f, 0.4f));
    p.osc(1, tGrowlFM, 0.45f, p.chance(0.5f) ? 1 : 0, p.rnd(3.f, 9.f), 2, 0.1f, 0.4f, p.rnd(0.2f, 0.5f));
    const int loopDiv = p.pick({ 0, 1, 2 });
    const float loopMs = loopDiv == 0 ? 172.f : loopDiv == 1 ? 86.f : 115.f; // 1/8, 1/16, 1/8T at 174
    p.env(2, loopMs * 0.25f, loopMs * 0.75f, 0.f, 80.f, true);
    p.route(ModSrc::Env3, osc(0, oFm), p.rnd(0.35f, 0.55f));
    p.route(ModSrc::Env3, osc(1, oPos), 0.3f);
    p.lfo(0, p.pick({ d1bar, d2 }), shTri);
    p.route(ModSrc::Lfo1, op + opFb, 0.25f);
    // PASS 1 — fold + filter sweep
    p.fx(0, FxType::Fold, p.rnd(8.f, 13.f), p.rnd(0.3f, 0.6f), 0.f, 0.f, 0.8f, 0.f);
    p.fx(1, FxType::Filter, p.rnd(3000.f, 6000.f), 0.35f, 0.f, 3.f);
    p.motionLfo(0, p.pick({ d8, d4, d8T }), p.pick({ shSawDown, shSine, shTri }), p.rnd(0.4f, 0.6f));
    // PASS 2 — key-tracked comb resonance + hard drive
    p.fx(4, FxType::Comb, p.pick({ 100, 200, 300 }) * 1.f, p.rnd(0.5f, 0.75f), 0.4f, 1.f, p.rnd(0.25f, 0.45f));
    p.fx(5, FxType::Drive, p.rnd(6.f, 12.f), 0.8f, 0.1f, 0.f, 1.f, 0.f);
    p.motionLfo(1, p.pick({ d4, d2, d4T }), shTri, p.rnd(0.15f, 0.3f));
    // PASS 3
    p.fx(8, FxType::OTT, p.rnd(0.4f, 0.6f), 0.4f, 1.f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 30.f, 2.f, p.rnd(280.f, 450.f), -3.f, 1.f, 0.f);
    if (p.chance(0.5f)) p.fx(10, FxType::Disperse, p.rnd(500.f, 1500.f), 0.2f, 0.4f, 0.1f, 0.5f, 0.f);
    p.macro(mGrowl, osc(0, oFm), 0.35f);
    p.macro(mGrowl, op + opFb, 0.3f);
    p.macro(mGrowl, slot(0, sP1), 0.2f);
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 0, .gritSlot = 5, .metalSlot = 4, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.35f, 0.3f, 0.3f, 0.3f, 0.3f, 0.2f, 0.5f, 0.5f });
    return p;
}

Patch talker(uint32_t seed)
{
    Patch p = base(seed);
    // clean, phase-coherent source so vowels read clearly (detune smears formants)
    p.osc(0, tSawSquare, 0.8f, 0, 0, 1, 0.f, 0.f, p.rnd(0.f, 0.25f));
    p.osc(1, tFormant, 0.35f, 1, 0, 1, 0.f, 0.f, 0.3f);
    p.lfo(0, d8, shSawUp);
    p.route(ModSrc::Lfo1, osc(1, oPos), 0.3f);
    // PASS 1 � distort first (rich harmonics), THEN the stepped formant carves vowels: hard talk
    p.fx(0, FxType::Drive, p.rnd(10.f, 14.f), 0.45f, 0.1f, 0.2f, 1.f, 0.f);
    p.fx(1, FxType::Formant, 2.f, p.rnd(-1.f, 3.f), p.rnd(0.55f, 0.75f), 0.12f, 1.f, 1.f);
    const int pat = p.pick({ 0, 1, 2 });
    if (pat == 0) p.motionSteps(0, d8, { 0.f, 0.75f, 0.5f, 0.25f, 0.f, 1.f, 0.5f, 0.75f }, 1.f, 0.45f);
    else if (pat == 1) p.motionSteps(0, d16, { 0.f, 0.f, 0.75f, 0.5f, 1.f, 0.25f, 0.75f, 0.5f }, 1.f, 0.35f);
    else p.motionSteps(0, d8T, { 0.75f, 0.f, 0.5f, 1.f, 0.25f, 0.5f }, 1.f, 0.5f);
    // PASS 2 � grit after the vowel + subtle comb resonance following the note
    p.fx(4, FxType::Fold, p.rnd(4.f, 8.f), 0.3f, 0.f, 0.f, 0.5f, 0.f);
    p.fx(5, FxType::Comb, 200.f, 0.45f, 0.5f, 1.f, 0.2f, 0.f);
    p.motionLfo(1, p.pick({ d4, d2 }), shSine, 0.2f);
    // PASS 3
    p.fx(8, FxType::OTT, p.rnd(0.3f, 0.45f), 0.5f, 0.8f, 0.9f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 40.f, 0.f, 2500.f, -1.5f, 1.f, 0.f);
    p.macro(mGrowl, slot(1, sP3), 0.3f);   // formant Q: sharper talk
    p.macro(mGrowl, slot(0, sP1), 0.25f);
    p.macro(mTalk, slot(1, sP2), 0.2f);    // formant shift
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 0, .gritSlot = 0, .metalSlot = 5, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.5f, 0.3f, 0.3f, 0.3f, 0.2f, 0.5f, 0.5f });
    return p;
}

Patch metallic(uint32_t seed)
{
    Patch p = base(seed);
    // inharmonic FM (non-integer ratio) into a sine, plus Metal Bars table
    p.osc(0, tBasic, 0.7f, 0, 0, 1, 0.f, 0.f, p.rnd(0.f, 0.2f));
    p.fmOp(p.pick({ 5, 3, 7 }), p.rnd(8.f, 25.f), p.rnd(0.f, 0.2f));
    p.set(osc(0, oFm), p.rnd(0.3f, 0.5f));
    p.osc(1, tMetal, 0.55f, 0, p.rnd(-12.f, 12.f), 2, 0.15f, 0.5f, p.rnd(0.f, 0.6f));
    p.lfo(0, p.pick({ d4, d8 }), shSmooth);
    p.route(ModSrc::Lfo1, osc(1, oPos), 0.3f);
    p.env(2, 2.f, p.rnd(150.f, 400.f), 0.3f, 100.f);
    p.route(ModSrc::Env3, osc(0, oFm), 0.3f);
    // PASS 1 — drive + frequency shift (inharmonic sheen)
    p.fx(0, FxType::Drive, p.rnd(10.f, 15.f), 0.5f, 0.f, 0.2f, 1.f, 0.f);
    p.fx(1, FxType::FreqShift, p.rnd(15.f, 60.f), p.rnd(0.f, 0.3f), 0.5f, NAN, p.rnd(0.2f, 0.35f));
    p.motionLfo(0, d1bar, shTri, 0.3f);
    // PASS 2 — tuned resonant comb (the metal) + ring mod
    p.fx(4, FxType::Comb, p.pick({ 150, 300, 400 }) * 1.f, p.rnd(0.78f, 0.9f), 0.2f, 1.f, p.rnd(0.4f, 0.6f));
    p.fx(5, FxType::RingMod, p.pick({ 150, 250, 50 }) * 1.f, 1.f, 0.2f, NAN, p.rnd(0.15f, 0.3f), 0.f);
    p.motionLfo(1, p.pick({ d4T, d8T, d4 }), p.pick({ shTri, shSine }), p.rnd(0.2f, 0.35f));
    // PASS 3
    p.fx(8, FxType::OTT, p.rnd(0.5f, 0.7f), 0.4f, 1.f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 40.f, 1.f, p.rnd(2500.f, 4000.f), -4.f, 1.f, 0.f);
    p.fx(10, FxType::Disperse, p.rnd(600.f, 1200.f), p.rnd(0.2f, 0.35f), 0.5f, 0.2f, 0.6f, 0.f);
    p.macro(mGrowl, osc(0, oFm), 0.3f);
    p.macro(mMetal, slot(4, sP2), 0.12f);
    p.macro(mMetal, slot(1, sP1), 0.15f);
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 1, .gritSlot = 0, .metalSlot = 4, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.3f, 0.3f, 0.5f, 0.3f, 0.3f, 0.5f, 0.5f });
    return p;
}

Patch tearer(uint32_t seed)
{
    Patch p = base(seed);
    p.osc(0, tSyncSaw, 0.75f, 0, 0, p.pick({ 1, 2 }), 0.1f, 0.4f, 0.2f);
    p.lfo(0, p.pick({ d16, d8T, d16T }), p.pick({ shSawDown, shTri, shSH }));
    p.route(ModSrc::Lfo1, osc(0, oPos), p.rnd(0.35f, 0.6f));
    p.fmOp(1, 0.f, 0.25f); // 0.5 ratio: sub-harmonic tearing
    p.set(osc(0, oFm), p.rnd(0.15f, 0.3f));
    p.osc(1, tGrit, 0.3f, 1, 0.f, 1, 0.f, 0.f, p.rnd(0.f, 0.5f));
    // PASS 1 — fast notch sweep into hard clip
    p.fx(0, FxType::Filter, p.rnd(500.f, 1200.f), 0.5f, 3.f, 0.f);
    p.fx(1, FxType::Drive, p.rnd(12.f, 16.f), 0.85f, 0.15f, 0.f, 1.f, 0.f);
    p.motionLfo(0, p.pick({ d16, d8, d16T }), p.pick({ shSawUp, shTri }), p.rnd(0.6f, 0.85f));
    // PASS 2 — crush + fold
    p.fx(4, FxType::Crush, p.rnd(9000.f, 16000.f), p.rnd(9.f, 12.f), 0.1f, 0.f, p.rnd(0.1f, 0.2f));
    p.fx(5, FxType::Fold, p.rnd(6.f, 10.f), 0.45f, 0.1f, 0.f, 0.55f, 0.f);
    p.motionLfo(1, p.pick({ d8, d4 }), shSH, 0.3f);
    // PASS 3 — heavy OTT
    p.fx(8, FxType::OTT, p.rnd(0.45f, 0.6f), 0.3f, 0.8f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 40.f, -0.5f, 3200.f, -4.f, 1.f, 0.f);
    p.set(midWidth, 1.3f);
    p.macro(mGrowl, slot(5, sP1), 0.25f);
    p.macro(mGrowl, osc(0, oFm), 0.3f);
    p.macro(mMetal, slot(4, sMix), 0.4f);
    wireCommonMacros(p, { .growlSlot = 5, .talkPass = 0, .gritSlot = 1, .metalSlot = -1, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.4f, 0.3f, 0.2f, 0.3f, 0.3f, 0.5f, 0.5f });
    return p;
}

Patch hoover(uint32_t seed)
{
    Patch p = base(seed);
    p.osc(0, tPWM, 0.7f, 0, 0, 7, p.rnd(0.5f, 0.7f), 0.9f, 0.4f);
    p.osc(1, tSawSquare, 0.5f, 1, 0, 5, 0.45f, 0.8f, 0.f);
    p.set(osc(0, oPhaseRand), 1); p.set(osc(1, oPhaseRand), 1);
    p.lfo(0, d2, shTri);
    p.route(ModSrc::Lfo1, osc(0, oPos), 0.3f);
    p.env(1, 1.f, p.rnd(180.f, 320.f), 0.f, 100.f);
    p.set(pitchEnv, -12.f); p.set(pitchEnvSrc, 0); // swoop up into the note
    p.set(glide, 70.f);
    p.fx(0, FxType::Filter, 3000.f, 0.2f, 0.f, 4.f);
    p.fx(1, FxType::Drive, 8.f, 0.3f, 0.f, 0.f, 1.f, 0.f);
    p.motionLfo(0, d1bar, shSine, 0.3f);
    p.fx(4, FxType::Flanger, 7.f, 0.25f, 0.6f, 0.3f, 0.4f);
    p.motionLfo(1, d2bar, shTri, 0.4f);
    p.fx(8, FxType::OTT, 0.3f, 0.5f, 0.7f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 40.f, -1.f, 400.f, -3.f, 1.f, 0.f);
    p.set(midWidth, 1.4f);
    p.macro(mGrowl, slot(0, sP1), 0.3f);
    p.macro(mGrowl, pitchEnv, -0.15f);
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 0, .gritSlot = 1, .metalSlot = 4, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.2f, 0.3f, 0.3f, 0.3f, 0.4f, 0.5f, 0.5f });
    return p;
}

Patch stab(uint32_t seed)
{
    Patch p = base(seed);
    p.set(voiceMode, 1); // mono retrigger: every hit snaps
    p.env(0, 0.5f, p.rnd(140.f, 220.f), 0.15f, 50.f);
    p.osc(0, tSawSquare, 0.75f, 0, 0, 3, 0.2f, 0.5f, p.rnd(0.3f, 0.6f));
    p.osc(1, tGrowlFM, 0.5f, 1, 0, 1, 0.f, 0.f, 0.6f);
    p.env(1, 0.5f, p.rnd(90.f, 150.f), 0.f, 60.f);
    p.set(vf + vfOn, 1); p.set(vf + vfCutoff, 400.f); p.set(vf + vfReso, 0.35f); p.set(vf + vfEnv, 0.65f);
    p.env(2, 0.1f, p.rnd(25.f, 50.f), 0.f, 20.f);
    p.set(pitchEnv, p.pick({ 7, 12 }) * 1.f); p.set(pitchEnvSrc, 1);
    p.route(ModSrc::Env2, osc(1, oPos), -0.4f);
    p.fx(0, FxType::Drive, p.rnd(12.f, 16.f), 0.6f, 0.1f, 0.f, 1.f, 0.f);
    p.fx(4, FxType::Comb, 200.f, 0.55f, 0.5f, 1.f, 0.25f);
    p.fx(5, FxType::Fold, 6.f, 0.4f, 0.f, 0.f, 0.5f, 0.f);
    p.motionLfo(1, d16, shSH, 0.3f);
    p.fx(8, FxType::OTT, 0.5f, 0.3f, 1.f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 40.f, 0.5f, 350.f, -3.f, 1.f, 0.f);
    p.macro(mGrowl, vf + vfEnv, 0.25f);
    p.macro(mGrowl, slot(0, sP1), 0.2f);
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 1, .gritSlot = 0, .metalSlot = 4, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.2f, 0.3f, 0.3f, 0.2f, 0.2f, 0.5f, 0.5f });
    return p;
}

Patch liquid(uint32_t seed)
{
    Patch p = base(seed);
    p.osc(0, tHollow, 0.75f, 0, 0, 2, 0.1f, 0.3f, 0.3f);
    p.osc(1, tHarmSweep, 0.4f, 1, 4.f, 1, 0.f, 0.f, 0.2f);
    p.lfo(0, p.pick({ d2, d1bar }), shSine);
    p.route(ModSrc::Lfo1, osc(0, oPos), 0.3f);
    p.route(ModSrc::Lfo1, osc(1, oPos), 0.4f);
    p.fx(0, FxType::Filter, p.rnd(700.f, 1500.f), 0.3f, 0.f, 2.f);
    p.fx(1, FxType::Drive, 6.f, 0.1f, 0.2f, -0.3f, 1.f, 0.f);
    p.motionLfo(0, p.pick({ d4, d2 }), shSine, 0.4f);
    p.fx(4, FxType::Phaser, 500.f, 0.4f, 6.f, 0.5f, 0.6f);
    p.motionLfo(1, d2bar, shTri, 0.5f);
    p.fx(8, FxType::OTT, 0.2f, 0.6f, 0.6f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 40.f, -2.f, 600.f, -2.f, 1.f, 0.f);
    p.macro(mGrowl, slot(0, sP1), 0.3f);
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 0, .gritSlot = 1, .metalSlot = 4, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.3f, 0.2f, 0.2f, 0.3f, 0.3f, 0.5f, 0.5f });
    return p;
}

Patch donk(uint32_t seed)
{
    Patch p = base(seed);
    p.set(voiceMode, 1);
    p.env(0, 0.3f, p.rnd(180.f, 300.f), 0.f, 60.f);
    p.osc(0, tBasic, 0.85f, 0, 0, 1, 0.f, 0.f, 0.333f); // triangle
    p.fmOp(4, 0.f, 0.1f);
    p.set(osc(0, oFm), 0.15f);
    p.env(2, 0.1f, p.rnd(20.f, 40.f), 0.f, 20.f);
    p.set(pitchEnv, p.pick({ 24, 19, 31 }) * 1.f); p.set(pitchEnvSrc, 1); // the "donk" ping
    p.env(1, 0.5f, 80.f, 0.f, 30.f);
    p.route(ModSrc::Env2, osc(0, oFm), 0.4f);
    p.fx(0, FxType::Fold, 8.f, 0.5f, 0.1f, 0.f, 0.8f, 0.f);
    p.fx(1, FxType::Filter, 1200.f, 0.5f, 1.f, 0.f, 0.6f);
    p.motionLfo(0, d16, shSH, 0.3f);
    p.fx(8, FxType::OTT, 0.4f, 0.3f, 1.f, 1.f, 1.f, 0.f);
    p.fx(9, FxType::EQ, 40.f, 0.f, 500.f, -2.f, 1.f, 0.f);
    p.macro(mGrowl, pitchEnv, 0.1f);
    p.macro(mGrowl, slot(0, sP1), 0.3f);
    wireCommonMacros(p, { .growlSlot = 0, .talkPass = 0, .gritSlot = 0, .metalSlot = 1, .ottSlot = 8, .toneSlot = 9 });
    p.macroValues({ 0.3f, 0.2f, 0.3f, 0.3f, 0.2f, 0.2f, 0.5f, 0.5f });
    return p;
}

// Render a short riff of a patch and set master gain so it lands near a consistent loudness.
void autoGain(ParamState& s)
{
    constexpr double sr = 48000.0, bpm = 174.0;
    constexpr int block = 256;
    const double spb = 60.0 / bpm;
    s.setReal(pid::masterGain, 0.f);
    ParamState probe = s;
    probe.setReal(pid::quality, 0); // measure at 1x: same loudness, 2-4x cheaper
    Engine e;
    e.prepare(sr, block);
    std::vector<float> L((size_t) block), R((size_t) block);
    const int total = (int) (sr * spb * 8); // two bars
    const int hits[] = { 0, 3, 6, 8, 11, 16, 19, 22, 24, 28 }; // 16th positions
    for (int iter = 0; iter < 3; ++iter) {
        e.reset();
        Transport t; t.bpm = bpm; t.playing = true;
        double sum = 0;
        int nSamples = 0;
        for (int pos = 0; pos < total; pos += block) {
            MidiEvent ev[4]; int ne = 0;
            for (int h : hits) {
                const int on = (int) (h * 0.25 * spb * sr), off = on + (int) (0.5 * spb * sr);
                if (off >= pos && off < pos + block && ne < 4) ev[ne++] = { off - pos, 0x80, 29, 0 };
                if (on >= pos && on < pos + block && ne < 4) ev[ne++] = { on - pos, 0x90, 29, 110 };
            }
            std::sort(ev, ev + ne, [](const MidiEvent& a, const MidiEvent& b) { return a.offset < b.offset; });
            t.ppq = pos / sr / spb;
            e.render(L.data(), R.data(), block, probe.v.data(), ev, ne, t);
            for (int i = 0; i < block; ++i) { sum += L[(size_t) i] * L[(size_t) i] + R[(size_t) i] * R[(size_t) i]; nSamples += 2; }
        }
        const double rmsDb = 10.0 * std::log10(std::max(1e-12, sum / std::max(1, nSamples)));
        const double targetDb = -10.5;
        if (std::abs(targetDb - rmsDb) < 0.5) break;
        const float g = probe.real(pid::masterGain) + (float) std::clamp(targetDb - rmsDb, -20.0, 20.0) * (iter == 0 ? 1.f : 0.8f);
        probe.setReal(pid::masterGain, std::clamp(g, -24.f, 12.f));
    }
    s.setReal(pid::masterGain, probe.real(pid::masterGain));
}

} // namespace

ParamState generatePatch(Archetype a, uint32_t seed)
{
    Patch p(seed);
    switch (a) {
        case Archetype::Reese:    p = reese(seed); break;
        case Archetype::FmGrowl:  p = fmGrowl(seed); break;
        case Archetype::Talker:   p = talker(seed); break;
        case Archetype::Metallic: p = metallic(seed); break;
        case Archetype::Tearer:   p = tearer(seed); break;
        case Archetype::Hoover:   p = hoover(seed); break;
        case Archetype::Stab:     p = stab(seed); break;
        case Archetype::Liquid:   p = liquid(seed); break;
        case Archetype::Donk:     p = donk(seed); break;
        default: break;
    }
    autoGain(p.s);
    return p.s;
}

void mutatePatch(ParamState& s, float amount, uint32_t seed)
{
    using namespace pid;
    amount = std::clamp(amount, 0.f, 1.f);
    Rng r(seed * 747796405u + 1u);
    const auto& defs = paramDefs();
    auto protectedParam = [&](int i) {
        if (i < numGlobal) return true; // output, guard, voice mode, pitch env
        if (i >= sub && i < sub + numSub) return true;
        if (i == osc(0, oLevel) || i == osc(1, oLevel) || i == osc(0, oOn) || i == osc(1, oOn)) return true;
        if (i >= env0 && i < env0 + numEnv) return true; // amp env
        if (i >= macro0 && i < macro0 + kNumMacros) return true;
        if (i >= mx0) return true;
        if (i == op + opOn || i == vf + vfOn) return true;
        return false;
    };
    for (int i = 0; i < pid::count; ++i) {
        if (protectedParam(i)) continue;
        const auto& d = defs[(size_t) i];
        if (d.curve == Curve::Lin || d.curve == Curve::Log) {
            // only nudge FX params of active slots
            if (i >= slot0 && i < macro0) {
                const int sl = (i - slot0) / numSlot;
                if (s.real(slot(sl, sType)) < 0.5f) continue;
            }
            const float g = (r.bi() + r.bi() + r.bi()) / 3.f; // ~gaussian
            s[i] = std::clamp(s[i] + g * amount * 0.35f, 0.f, 1.f);
        } else if (d.curve == Curve::Disc && amount > 0.35f) {
            const bool isTable = i == osc(0, oTable) || i == osc(1, oTable);
            const bool isDiv = (i >= lfo0 && i < motion0 && (i - lfo0) % numLfo == lDiv) ||
                               (i >= motion0 && i < pass0 && (i - motion0) % numMotion == mDiv);
            const bool isWarp = i == osc(0, oWarp) || i == osc(1, oWarp);
            float prob = 0.f;
            if (isTable) prob = 0.25f * amount;
            if (isDiv) prob = 0.3f * amount;
            if (isWarp) prob = 0.2f * amount;
            if (i == op + opCoarse) prob = 0.25f * amount;
            if (prob > 0 && r.uni() < prob) {
                const int n = (int) d.max;
                int cur = (int) s.real(i);
                int step = r.uni() < 0.5f ? -1 : 1;
                if (isTable) cur = (int) (r.uni() * 12.f); // built-in tables only
                else cur = std::clamp(cur + step * (1 + (int) (r.uni() * 2.f)), 0, n);
                s.setReal(i, (float) cur);
            }
        }
    }
    // occasionally swap a character FX for a sibling when going wild
    if (amount > 0.6f) {
        static const FxType character[] = { FxType::Comb, FxType::Phaser, FxType::Flanger, FxType::Disperse,
                                            FxType::FreqShift, FxType::RingMod, FxType::Formant, FxType::Fold };
        for (int sl = 0; sl < kNumSlots; ++sl) {
            const int t = (int) s.real(slot(sl, sType));
            const bool isChar = std::find(std::begin(character), std::end(character), (FxType) t) != std::end(character);
            if (isChar && r.uni() < 0.3f * amount)
                s.setReal(slot(sl, sType), (float) (int) character[(int) (r.uni() * 7.99f)]);
        }
    }
    autoGain(s);
}

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        struct Spec { const char* name; Archetype a; uint32_t seed; };
        // Seeds chosen after listening/analysis passes; names describe the character.
        const Spec specs[] = {
            { "Wormhole Reese", Archetype::Reese, 11 },     { "Dark Matter Reese", Archetype::Reese, 4 },
            { "Reptile Growl", Archetype::FmGrowl, 2 },     { "Looped Index", Archetype::FmGrowl, 7 },
            { "Yoi Machine", Archetype::Talker, 1 },        { "Vowel Chop", Archetype::Talker, 5 },
            { "Chrome Bars", Archetype::Metallic, 3 },      { "Ring Theory", Archetype::Metallic, 8 },
            { "Rip Current", Archetype::Tearer, 2 },        { "Shredder", Archetype::Tearer, 6 },
            { "Classic Hoover", Archetype::Hoover, 1 },     { "Bounce Stab", Archetype::Stab, 3 },
            { "Rolling Liquid", Archetype::Liquid, 2 },     { "Donk Ping", Archetype::Donk, 1 },
        };
        std::vector<FactoryPreset> r;
        for (auto& sp : specs) r.push_back({ sp.name, archetypeNames()[(size_t) sp.a], sp.a, sp.seed });
        return r;
    }();
    return presets;
}

} // namespace nf
