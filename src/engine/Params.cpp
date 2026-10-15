#include "Params.h"
#include "dsp/fx/FxModule.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <unordered_map>

namespace nf {

namespace {
std::vector<std::string> numbered(const std::string& prefix, int n, int start = 1)
{
    std::vector<std::string> r;
    for (int i = 0; i < n; ++i) r.push_back(prefix + std::to_string(start + i));
    return r;
}

const std::vector<std::string>& tableChoiceNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n = { "Saw>Square", "Basic Shapes", "PWM", "Formant Vox", "Harmonic Sweep",
                                       "Metal Bars", "Reese Pulse", "Growl FM", "Sync Saw", "Digital Grit",
                                       "Hollow", "Phase Dist" };
        for (int i = 1; i <= 4; ++i) n.push_back("Capture " + std::to_string(i));
        return n;
    }();
    return names;
}

std::vector<ParamDef> buildDefs()
{
    std::vector<ParamDef> d((size_t) pid::count);
    auto set = [&](int idx, std::string id, std::string name, float mn, float mx, float def,
                   Curve c = Curve::Lin, const char* unit = "", bool voice = false,
                   std::vector<std::string> choices = {}) {
        auto& p = d[(size_t) idx];
        p.id = std::move(id); p.name = std::move(name); p.min = mn; p.max = mx; p.def = def;
        p.curve = c; p.unit = unit; p.voice = voice; p.choices = std::move(choices);
        p.modulatable = (c == Curve::Lin || c == Curve::Log);
    };
    auto choice = [&](int idx, std::string id, std::string name, std::vector<std::string> ch, int def, bool voice = false) {
        float n = (float) ch.size() - 1.f;
        set(idx, std::move(id), std::move(name), 0.f, n, (float) def, Curve::Disc, "", voice, std::move(ch));
    };
    auto toggle = [&](int idx, std::string id, std::string name, bool def, bool voice = false) {
        set(idx, std::move(id), std::move(name), 0.f, 1.f, def ? 1.f : 0.f, Curve::Toggle, "", voice);
    };

    using namespace pid;
    // ---- global ----
    set(masterGain, "master_gain", "Master Gain", -24.f, 12.f, 0.f, Curve::Lin, "dB");
    set(masterClip, "master_clip", "Master Clip", 0.f, 1.f, 0.3f);
    choice(quality, "quality", "Quality", { "1x", "2x", "4x" }, 1);
    choice(voiceMode, "voice_mode", "Voice Mode", { "Poly", "Mono", "Legato" }, 2);
    set(glide, "glide", "Glide", 0.f, 1000.f, 0.f, Curve::Lin, "ms");
    set(bendRange, "bend_range", "Bend Range", 0.f, 24.f, 2.f, Curve::Disc, "st");
    toggle(guardOn, "guard_on", "Low Guard", true);
    set(guardFreq, "guard_freq", "Low Guard Freq", 40.f, 300.f, 120.f, Curve::Log, "Hz");
    set(subDrive, "sub_drive", "Sub Warmth", 0.f, 1.f, 0.15f);
    set(subLevel, "sub_bus_level", "Sub Level", -36.f, 6.f, 0.f, Curve::Lin, "dB");
    set(midLevel, "mid_level", "Mid Level", -36.f, 6.f, 0.f, Curve::Lin, "dB");
    set(midWidth, "mid_width", "Mid Width", 0.f, 2.f, 1.f);
    set(ampVel, "amp_vel", "Velocity Sens", 0.f, 1.f, 0.f);
    set(pitchEnv, "pitch_env", "Pitch Env Amount", -48.f, 48.f, 0.f, Curve::Lin, "st", true);
    choice(pitchEnvSrc, "pitch_env_src", "Pitch Env Source", { "Env 2", "Env 3" }, 1, true);

    // ---- oscillators ----
    const auto& tables = tableChoiceNames();
    std::vector<std::string> warps = { "None", "Sync", "Bend+", "Bend-", "PWM", "Mirror", "Asym", "Quantize",
                                       "Phase Dist", "FM from B", "Ring from B" };
    for (int o = 0; o < 2; ++o) {
        std::string p = o == 0 ? "oa_" : "ob_", n = o == 0 ? "Osc A " : "Osc B ";
        auto i = [&](int off) { return pid::osc(o, off); };
        toggle(i(oOn), p + "on", n + "On", o == 0, true);
        choice(i(oTable), p + "table", n + "Table", tables, o == 0 ? 0 : 7, true);
        set(i(oOct), p + "oct", n + "Octave", -3.f, 3.f, 0.f, Curve::Disc, "oct", true);
        set(i(oSemi), p + "semi", n + "Semi", -12.f, 12.f, 0.f, Curve::Disc, "st", true);
        set(i(oFine), p + "fine", n + "Fine", -100.f, 100.f, 0.f, Curve::Lin, "ct", true);
        set(i(oLevel), p + "level", n + "Level", 0.f, 1.f, 0.8f, Curve::Lin, "", true);
        set(i(oPan), p + "pan", n + "Pan", -1.f, 1.f, 0.f, Curve::Lin, "", true);
        set(i(oPos), p + "pos", n + "Position", 0.f, 1.f, 0.f, Curve::Lin, "", true);
        set(i(oUni), p + "unison", n + "Unison", 1.f, 16.f, 1.f, Curve::Disc, "", true);
        set(i(oDetune), p + "detune", n + "Detune", 0.f, 1.f, 0.2f, Curve::Lin, "", true);
        set(i(oStereo), p + "stereo", n + "Stereo", 0.f, 1.f, 0.6f, Curve::Lin, "", true);
        set(i(oBlend), p + "blend", n + "Blend", 0.f, 1.f, 0.6f, Curve::Lin, "", true);
        set(i(oPhase), p + "phase", n + "Phase", 0.f, 1.f, 0.f, Curve::Lin, "", true);
        set(i(oPhaseRand), p + "phase_rand", n + "Phase Rand", 0.f, 1.f, 0.f, Curve::Lin, "", true);
        choice(i(oWarp), p + "warp", n + "Warp", warps, 0, true);
        set(i(oWarpAmt), p + "warp_amt", n + "Warp Amount", 0.f, 1.f, 0.f, Curve::Lin, "", true);
        set(i(oFm), p + "fm", n + "FM from Op", 0.f, 1.f, 0.f, Curve::Lin, "", true);
        toggle(i(oRetrig), p + "retrig", n + "Retrigger", true, true);
    }

    // ---- FM operator ----
    toggle(op + opOn, "op_on", "FM Op On", false, true);
    choice(op + opCoarse, "op_coarse", "FM Op Ratio",
           { "0.25", "0.5", "1", "1.5", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "16" }, 2, true);
    set(op + opFine, "op_fine", "FM Op Fine", -100.f, 100.f, 0.f, Curve::Lin, "ct", true);
    set(op + opFb, "op_fb", "FM Op Feedback", 0.f, 1.f, 0.f, Curve::Lin, "", true);
    set(op + opShape, "op_shape", "FM Op Shape", 0.f, 1.f, 0.f, Curve::Lin, "", true);

    // ---- sub / noise ----
    toggle(sub + subOn, "sub_on", "Sub On", true, true);
    set(sub + subOct, "sub_oct", "Sub Octave", -2.f, 0.f, -1.f, Curve::Disc, "oct", true);
    set(sub + subShape, "sub_shape", "Sub Shape", 0.f, 1.f, 0.f, Curve::Lin, "", true);
    set(sub + subLevelP, "sub_level", "Sub Osc Level", 0.f, 1.f, 0.8f, Curve::Lin, "", true);
    toggle(sub + subDirect, "sub_direct", "Sub Direct", true, true);
    set(noise + nLevel, "noise_level", "Noise Level", 0.f, 1.f, 0.f, Curve::Lin, "", true);
    set(noise + nColor, "noise_color", "Noise Color", -1.f, 1.f, 0.f, Curve::Lin, "", true);

    // ---- voice filter ----
    toggle(vf + vfOn, "vf_on", "Filter On", false, true);
    set(vf + vfMode, "vf_mode", "Filter Mode", 0.f, 3.f, 0.f, Curve::Lin, "", true);
    set(vf + vfCutoff, "vf_cutoff", "Filter Cutoff", 20.f, 20000.f, 2000.f, Curve::Log, "Hz", true);
    set(vf + vfReso, "vf_reso", "Filter Reso", 0.f, 1.f, 0.2f, Curve::Lin, "", true);
    set(vf + vfDrive, "vf_drive", "Filter Drive", 0.f, 24.f, 0.f, Curve::Lin, "dB", true);
    set(vf + vfKey, "vf_key", "Filter Keytrack", 0.f, 1.f, 0.f, Curve::Lin, "", true);
    set(vf + vfEnv, "vf_env", "Filter Env2", -1.f, 1.f, 0.f, Curve::Lin, "", true);

    // ---- envelopes ----
    for (int e = 0; e < kNumEnvs; ++e) {
        std::string p = "env" + std::to_string(e + 1) + "_", n = "Env " + std::to_string(e + 1) + " ";
        set(env(e, eA), p + "a", n + "Attack", 0.1f, 10000.f, e == 0 ? 1.f : 5.f, Curve::Log, "ms", true);
        set(env(e, eD), p + "d", n + "Decay", 1.f, 10000.f, e == 0 ? 300.f : 400.f, Curve::Log, "ms", true);
        set(env(e, eS), p + "s", n + "Sustain", 0.f, 1.f, e == 0 ? 1.f : 0.f, Curve::Lin, "", true);
        set(env(e, eR), p + "r", n + "Release", 1.f, 10000.f, e == 0 ? 40.f : 200.f, Curve::Log, "ms", true);
        toggle(env(e, eLoop), p + "loop", n + "Loop", false, true);
        for (int k = 0; k < numEnv; ++k) d[(size_t) env(e, k)].modulatable = false;
    }

    // ---- LFOs ----
    for (int l = 0; l < kNumLfos; ++l) {
        std::string p = "lfo" + std::to_string(l + 1) + "_", n = "LFO " + std::to_string(l + 1) + " ";
        toggle(lfo(l, lSync), p + "sync", n + "Sync", true);
        set(lfo(l, lRate), p + "rate", n + "Rate", 0.01f, 40.f, 2.f, Curve::Log, "Hz");
        choice(lfo(l, lDiv), p + "div", n + "Division", rateDivNames(), 7);
        choice(lfo(l, lShape), p + "shape", n + "Shape", lfoShapeNames(), 0);
        toggle(lfo(l, lRetrig), p + "retrig", n + "Retrigger", true);
        set(lfo(l, lPhase), p + "phase", n + "Phase", 0.f, 1.f, 0.f);
    }

    // ---- motion (one per pass) ----
    auto motionShapes = lfoShapeNames();
    for (int m = 0; m < kNumPasses; ++m) {
        std::string p = "mo" + std::to_string(m + 1) + "_", n = "Motion " + std::to_string(m + 1) + " ";
        choice(motion(m, mMode), p + "mode", n + "Mode", { "LFO", "Steps" }, 0);
        toggle(motion(m, mSync), p + "sync", n + "Sync", true);
        set(motion(m, mRate), p + "rate", n + "Rate", 0.01f, 40.f, 2.f, Curve::Log, "Hz");
        choice(motion(m, mDiv), p + "div", n + "Division", rateDivNames(), m == 0 ? 10 : (m == 1 ? 8 : 13));
        choice(motion(m, mShape), p + "shape", n + "Shape", motionShapes, 0);
        set(motion(m, mDepth), p + "depth", n + "Depth", 0.f, 1.f, 0.f);
        set(motion(m, mSteps), p + "steps", n + "Steps", 1.f, 16.f, 8.f, Curve::Disc);
        set(motion(m, mGlide), p + "glide", n + "Glide", 0.f, 1.f, 0.2f);
        for (int s = 0; s < 16; ++s)
            set(motion(m, mStep0 + s), p + "st" + std::to_string(s + 1), n + "Step " + std::to_string(s + 1),
                0.f, 1.f, 0.5f);
    }

    // ---- passes + slots ----
    std::vector<std::string> fxNames;
    for (int t = 0; t < (int) FxType::NumTypes; ++t) fxNames.push_back(getFxTypeInfo((FxType) t).name);
    for (int ps = 0; ps < kNumPasses; ++ps) {
        std::string p = "pass" + std::to_string(ps + 1) + "_", n = "Pass " + std::to_string(ps + 1) + " ";
        toggle(pass(ps, pOn), p + "on", n + "On", true);
        set(pass(ps, pMix), p + "mix", n + "Mix", 0.f, 1.f, 1.f);
    }
    for (int s = 0; s < kNumSlots; ++s) {
        std::string p = "s" + std::to_string(s + 1) + "_", n = "Slot " + std::to_string(s + 1) + " ";
        choice(slot(s, sType), p + "type", n + "Type", fxNames, 0);
        for (int k = 0; k < kFxParams; ++k)
            set(slot(s, sP1 + k), p + "p" + std::to_string(k + 1), n + "P" + std::to_string(k + 1), 0.f, 1.f, 0.5f);
        set(slot(s, sMix), p + "mix", n + "Mix", 0.f, 1.f, 1.f);
        set(slot(s, sMotion), p + "motion", n + "Motion Amt", -1.f, 1.f, 1.f);
    }

    // ---- macros ----
    static const char* macroNames[] = { "GROWL", "TALK", "GRIT", "METAL", "MOTION", "WIDTH", "SUB", "TONE" };
    for (int m = 0; m < kNumMacros; ++m) {
        set(macro(m), "macro" + std::to_string(m + 1), std::string("Macro ") + std::to_string(m + 1) + " " + macroNames[m],
            0.f, 1.f, 0.f);
        d[(size_t) macro(m)].modulatable = false;
    }

    // ---- matrix (destination choices filled below once modDestinations exists) ----
    for (int x = 0; x < kNumMatrix; ++x) {
        std::string p = "mx" + std::to_string(x + 1) + "_", n = "Mod " + std::to_string(x + 1) + " ";
        choice(mx(x, mxSrc), p + "src", n + "Source", modSrcNames(), 0);
        choice(mx(x, mxDst), p + "dst", n + "Destination", { "None" }, 0); // patched below
        set(mx(x, mxAmt), p + "amt", n + "Amount", -1.f, 1.f, 0.f);
        d[(size_t) mx(x, mxAmt)].modulatable = false;
    }
    // ---- curve steppers ----
    for (int st = 0; st < kNumSteppers; ++st) {
        std::string p = "st" + std::to_string(st + 1) + "_", n = "Step " + std::to_string(st + 1) + " ";
        auto S = [&](int off) { return stepper(st, off); };
        toggle(S(stSync), p + "sync", n + "Sync", true);
        set(S(stRate), p + "rate", n + "Rate", 0.1f, 40.f, 8.f, Curve::Log, "Hz");
        choice(S(stDiv), p + "div", n + "Step Length", rateDivNames(), 13); // 1/16 per step
        set(S(stSteps), p + "steps", n + "Steps", 1.f, (float) kStepperSteps, 8.f, Curve::Disc);
        set(S(stSmooth), p + "smooth", n + "Smooth", 0.f, 1.f, 0.1f);
        toggle(S(stRetrig), p + "retrig", n + "Retrigger", true);
        // a musical default pattern so a fresh stepper does something useful straight away
        static const int defShapes[2][16] = { { 3, 5, 3, 13, 3, 5, 9, 15, 3, 5, 3, 13, 11, 5, 9, 16 },
                                              { 9, 9, 10, 9, 14, 9, 10, 19, 9, 9, 10, 9, 14, 9, 12, 18 } };
        for (int k = 0; k < kStepperSteps; ++k) {
            choice(S(stShape0 + k), p + "shape" + std::to_string(k + 1), n + "Curve " + std::to_string(k + 1),
                   stepCurveNames(), defShapes[st][k]);
            set(S(stAmt0 + k), p + "amt" + std::to_string(k + 1), n + "Intensity " + std::to_string(k + 1), 0.f, 1.f,
                (k % 4 == 0) ? 1.f : 0.75f);
        }
        for (int k = 0; k < numStepper; ++k) d[(size_t) S(k)].modulatable = false; // keeps the destination list stable
    }
    return d;
}

std::vector<ParamDef>& mutableDefs()
{
    static std::vector<ParamDef> defs = [] {
        auto d = buildDefs();
        // destination list: every modulatable param
        std::vector<std::string> dstNames = { "None" };
        for (auto& p : d) if (p.modulatable && !p.id.empty()) dstNames.push_back(p.name);
        for (int x = 0; x < pid::kNumMatrix; ++x) {
            auto& p = d[(size_t) pid::mx(x, pid::mxDst)];
            p.choices = dstNames;
            p.max = (float) dstNames.size() - 1.f;
        }
        return d;
    }();
    return defs;
}
} // namespace

const std::vector<ParamDef>& paramDefs() { return mutableDefs(); }

const std::vector<int>& modDestinations()
{
    static const std::vector<int> dst = [] {
        std::vector<int> r = { -1 }; // index 0 = None
        const auto& d = paramDefs();
        for (int i = 0; i < (int) d.size(); ++i) if (d[(size_t) i].modulatable) r.push_back(i);
        return r;
    }();
    return dst;
}

int findParam(const std::string& id)
{
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> m;
        const auto& d = paramDefs();
        for (int i = 0; i < (int) d.size(); ++i) m[d[(size_t) i].id] = i;
        return m;
    }();
    auto it = map.find(id);
    return it == map.end() ? -1 : it->second;
}

float normToReal(const ParamDef& d, float x)
{
    x = std::clamp(x, 0.f, 1.f);
    switch (d.curve) {
        case Curve::Log:    return d.min * std::pow(d.max / d.min, x);
        case Curve::Disc:   return std::round(d.min + x * (d.max - d.min));
        case Curve::Toggle: return x >= 0.5f ? 1.f : 0.f;
        default:            return d.min + x * (d.max - d.min);
    }
}

float realToNorm(const ParamDef& d, float v)
{
    if (d.max == d.min) return 0.f;
    if (d.curve == Curve::Log)
        return std::clamp(std::log(std::max(v, d.min) / d.min) / std::log(d.max / d.min), 0.f, 1.f);
    return std::clamp((v - d.min) / (d.max - d.min), 0.f, 1.f);
}

bool modSrcIsVoice(ModSrc s)
{
    switch (s) {
        case ModSrc::Env1: case ModSrc::Env2: case ModSrc::Env3:
        case ModSrc::Velocity: case ModSrc::Note: case ModSrc::Random: return true;
        default: return false;
    }
}

const std::vector<std::string>& modSrcNames()
{
    static const std::vector<std::string> n = [] {
        std::vector<std::string> r = { "None", "Env 1", "Env 2", "Env 3", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                                       "Motion 1", "Motion 2", "Motion 3" };
        for (int i = 1; i <= 8; ++i) r.push_back("Macro " + std::to_string(i));
        for (auto s : { "Velocity", "Note", "Mod Wheel", "Pitch Bend", "Aftertouch", "Random", "Step 1", "Step 2" })
            r.push_back(s);
        return r;
    }();
    return n;
}

namespace {
struct Div { const char* name; double beats; };
const Div kDivs[] = { { "8 bars", 32 }, { "4 bars", 16 }, { "2 bars", 8 }, { "1 bar", 4 }, { "1/2", 2 },
                      { "1/2T", 4.0 / 3 }, { "1/4.", 1.5 }, { "1/4", 1 }, { "1/4T", 2.0 / 3 }, { "1/8.", 0.75 },
                      { "1/8", 0.5 }, { "1/8T", 1.0 / 3 }, { "1/16.", 0.375 }, { "1/16", 0.25 },
                      { "1/16T", 1.0 / 6 }, { "1/32", 0.125 }, { "1/32T", 1.0 / 12 }, { "1/64", 0.0625 } };
}
const std::vector<std::string>& rateDivNames()
{
    static const std::vector<std::string> n = [] {
        std::vector<std::string> r;
        for (auto& d : kDivs) r.push_back(d.name);
        return r;
    }();
    return n;
}
double rateDivBeats(int i) { return kDivs[std::clamp(i, 0, (int) (sizeof(kDivs) / sizeof(kDivs[0])) - 1)].beats; }

const std::vector<std::string>& stepCurveNames()
{
    static const std::vector<std::string> n = { "Off", "Hold", "Ramp Up", "Ramp Down", "Exp Up", "Exp Down",
                                                "Log Up", "Log Down", "Triangle", "Hump", "S-Curve", "Pulse",
                                                "Spike", "Bounce", "Double", "Trill", "Stairs Up", "Stairs Down",
                                                "Saw x2", "Wobble" };
    return n;
}

float stepCurve(int shape, float t)
{
    t = std::clamp(t, 0.f, 0.99999f); // a step covers [0,1)
    const float pi = 3.14159265f;
    switch (shape) {
        case 0:  return 0.f;                                             // Off
        case 1:  return 1.f;                                             // Hold
        case 2:  return t;                                               // Ramp Up
        case 3:  return 1.f - t;                                         // Ramp Down
        case 4:  return t * t * t;                                       // Exp Up
        case 5:  { float u = 1.f - t; return u * u * u; }                // Exp Down
        case 6:  { float u = 1.f - t; return 1.f - u * u * u; }          // Log Up
        case 7:  return 1.f - t * t * t;                                 // Log Down
        case 8:  return t < 0.5f ? 2.f * t : 2.f - 2.f * t;              // Triangle
        case 9:  return std::sin(pi * t);                                // Hump
        case 10: return t * t * (3.f - 2.f * t);                         // S-Curve
        case 11: return t < 0.5f ? 1.f : 0.f;                            // Pulse
        case 12: return std::exp(-7.f * t);                              // Spike
        case 13: return std::abs(std::sin(2.f * pi * t)) * (1.f - 0.6f * t); // Bounce
        case 14: return std::abs(std::sin(2.f * pi * t));                // Double
        case 15: { float u = std::fmod(t * 4.f, 1.f); return u < 0.5f ? 2.f * u : 2.f - 2.f * u; } // Trill
        case 16: return std::floor(t * 4.f) / 3.f;                       // Stairs Up
        case 17: return 1.f - std::floor(t * 4.f) / 3.f;                 // Stairs Down
        case 18: return 1.f - std::fmod(t * 2.f, 1.f);                   // Saw x2
        case 19: return 0.5f + 0.5f * std::sin(2.f * pi * 3.f * t) * (1.f - t); // Wobble
        default: return 0.f;
    }
}

bool isMatrixSourceParam(int id)
{
    return id >= pid::mx0 && id < pid::mx0 + pid::kNumMatrix * pid::numMx && (id - pid::mx0) % pid::numMx == pid::mxSrc;
}

float remapModSrcFromV1(float norm)
{
    const int idx = (int) std::lround(std::clamp(norm, 0.f, 1.f) * (float) (kModSrcCountV1 - 1));
    return realToNorm(pid::mx(0, pid::mxSrc), (float) idx);
}

const std::vector<std::string>& lfoShapeNames()
{
    static const std::vector<std::string> n = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rand" };
    return n;
}

ParamState::ParamState()
{
    const auto& d = paramDefs();
    v.resize(d.size());
    for (size_t i = 0; i < d.size(); ++i) v[i] = realToNorm(d[i], d[i].def);
}

std::string ParamState::toText() const
{
    std::ostringstream os;
    os << "nf_version " << kStateVersion << '\n';
    const auto& d = paramDefs();
    for (size_t i = 0; i < d.size(); ++i) {
        bool isDst = false;
        for (int x = 0; x < pid::kNumMatrix; ++x) isDst |= (int) i == pid::mx(x, pid::mxDst);
        if (isDst) { // store destinations by id so presets survive parameter-list changes
            int c = (int) normToReal(d[i], v[i]);
            int target = modDestinations()[(size_t) std::clamp(c, 0, (int) modDestinations().size() - 1)];
            os << d[i].id << " @" << (target < 0 ? "none" : d[(size_t) target].id) << '\n';
        } else if (isMatrixSourceParam((int) i)) { // sources by index: new sources are only ever appended
            os << d[i].id << " #" << (int) normToReal(d[i], v[i]) << '\n';
        } else {
            os << d[i].id << ' ' << v[i] << '\n';
        }
    }
    return os.str();
}

bool ParamState::fromText(const std::string& text)
{
    *this = ParamState();
    std::istringstream is(text);
    std::vector<std::pair<std::string, std::string>> lines;
    std::string id, val;
    int version = 1; // files without a version line come from v1
    while (is >> id >> val) {
        if (id == "nf_version") version = std::atoi(val.c_str());
        else lines.emplace_back(id, val);
    }
    bool any = false;
    const auto& dst = modDestinations();
    for (auto& [pidStr, value] : lines) {
        int i = findParam(pidStr);
        if (i < 0) continue;
        any = true;
        if (!value.empty() && value[0] == '@') {
            int target = findParam(value.substr(1)), choiceIdx = 0;
            for (int c = 1; c < (int) dst.size(); ++c) if (dst[(size_t) c] == target) choiceIdx = c;
            v[(size_t) i] = realToNorm(i, (float) choiceIdx);
        } else if (!value.empty() && value[0] == '#') {
            v[(size_t) i] = realToNorm(i, (float) std::atoi(value.c_str() + 1));
        } else {
            float f = std::clamp(std::strtof(value.c_str(), nullptr), 0.f, 1.f);
            if (version < 2 && isMatrixSourceParam(i)) f = remapModSrcFromV1(f);
            v[(size_t) i] = f;
        }
    }
    return any;
}

} // namespace nf
