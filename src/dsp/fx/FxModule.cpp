#include "FxModule.h"
#include "FxDistortion.h"
#include "FxFilters.h"
#include "FxModulation.h"
#include "FxDynamics.h"
#include <algorithm>
#include <cmath>

namespace nf {

namespace {
using C = ParamCurve;
constexpr FxParamInfo none { nullptr, 0.f, 1.f, 0.f, C::Linear, "" };

// Index = FxType. P1 is always the "motion" target.
const FxTypeInfo kInfos[] = {
    { "Off",       { none, none, none, none } },
    { "Drive",     {{ { "Drive",   0.f,    36.f,   12.f,  C::Linear, "dB" },
                      { "Shape",   0.f,    1.f,    0.3f,  C::Linear, "" },      // soft tanh -> hard clip
                      { "Bias",   -1.f,    1.f,    0.f,   C::Linear, "" },      // asymmetry (even harmonics)
                      { "Tone",   -1.f,    1.f,    0.f,   C::Linear, "" } }} }, // post tilt dark..bright
    { "Fold",      {{ { "Drive",   0.f,    30.f,   9.f,   C::Linear, "dB" },
                      { "Folds",   0.f,    1.f,    0.4f,  C::Linear, "" },      // sine fold <-> triangle fold blend / depth
                      { "Bias",   -1.f,    1.f,    0.f,   C::Linear, "" },
                      { "Tone",   -1.f,    1.f,    0.f,   C::Linear, "" } }} },
    { "Crush",     {{ { "Rate",    200.f,  48000.f, 12000.f, C::Log, "Hz" },    // sample-and-hold rate
                      { "Bits",    1.f,    16.f,   10.f,  C::Linear, "bit" },
                      { "Jitter",  0.f,    1.f,    0.f,   C::Linear, "" },
                      { "Tone",   -1.f,    1.f,    0.f,   C::Linear, "" } }} },
    { "Filter",    {{ { "Cutoff",  20.f,   20000.f, 1200.f, C::Log, "Hz" },
                      { "Reso",    0.f,    1.f,    0.3f,  C::Linear, "" },
                      { "Mode",    0.f,    3.f,    0.f,   C::Linear, "" },      // continuous LP(0) BP(1) HP(2) Notch(3)
                      { "Drive",   0.f,    24.f,   0.f,   C::Linear, "dB" } }} },
    { "Formant",   {{ { "Vowel",   0.f,    4.f,    0.f,   C::Linear, "" },      // A E I O U continuous
                      { "Shift",  -12.f,   12.f,   0.f,   C::Linear, "st" },
                      { "Q",       0.f,    1.f,    0.5f,  C::Linear, "" },
                      { "Spread",  0.f,    1.f,    0.f,   C::Linear, "" } }} },  // L/R vowel offset
    { "Comb",      {{ { "Freq",    20.f,   5000.f, 200.f, C::Log, "Hz" },       // with KeyTrack>0.5: ratio = Freq/100 x note
                      { "Feedback",-0.99f, 0.99f,  0.7f,  C::Linear, "" },
                      { "Damp",    0.f,    1.f,    0.3f,  C::Linear, "" },
                      { "KeyTrack",0.f,    1.f,    1.f,   C::Discrete, "" } }} },
    { "Phaser",    {{ { "Freq",    20.f,   15000.f, 600.f, C::Log, "Hz" },
                      { "Feedback",-0.95f, 0.95f,  0.5f,  C::Linear, "" },
                      { "Stages",  2.f,    12.f,   6.f,   C::Discrete, "" },
                      { "Spread",  0.f,    1.f,    0.5f,  C::Linear, "" } }} },
    { "Flanger",   {{ { "Delay",   0.05f,  15.f,   2.f,   C::Log, "ms" },
                      { "Feedback",-0.97f, 0.97f,  0.6f,  C::Linear, "" },
                      { "Stereo",  0.f,    1.f,    0.2f,  C::Linear, "" },      // R delay offset
                      { "Damp",    0.f,    1.f,    0.2f,  C::Linear, "" } }} },
    { "FreqShift", {{ { "Shift",  -1000.f, 1000.f, 30.f,  C::Linear, "Hz" },
                      { "Feedback",0.f,    0.95f,  0.f,   C::Linear, "" },
                      { "Stereo",  0.f,    1.f,    0.f,   C::Linear, "" },      // R gets negated/offset shift
                      none }} },
    { "RingMod",   {{ { "Freq",    1.f,    5000.f, 200.f, C::Log, "Hz" },       // with KeyTrack: ratio = Freq/100 x note
                      { "KeyTrack",0.f,    1.f,    1.f,   C::Discrete, "" },
                      { "Shape",   0.f,    1.f,    0.f,   C::Linear, "" },      // sine -> square
                      none }} },
    { "Disperse",  {{ { "Freq",    20.f,   10000.f, 400.f, C::Log, "Hz" },
                      { "Amount",  0.f,    1.f,    0.5f,  C::Linear, "" },      // number of allpass stages 0..64
                      { "Pinch",   0.f,    1.f,    0.5f,  C::Linear, "" },      // allpass Q
                      { "Spread",  0.f,    1.f,    0.f,   C::Linear, "" } }} },
    { "OTT",       {{ { "Depth",   0.f,    1.f,    0.5f,  C::Linear, "" },
                      { "Time",    0.f,    1.f,    0.5f,  C::Linear, "" },
                      { "Upward",  0.f,    1.f,    1.f,   C::Linear, "" },
                      { "Downward",0.f,    1.f,    1.f,   C::Linear, "" } }} },
    { "EQ",        {{ { "LowCut",  20.f,   2000.f, 20.f,  C::Log, "Hz" },
                      { "Tilt",   -12.f,   12.f,   0.f,   C::Linear, "dB" },
                      { "MidFreq", 100.f,  10000.f, 1000.f, C::Log, "Hz" },
                      { "MidGain",-18.f,   18.f,   0.f,   C::Linear, "dB" } }} },
    { "Width",     {{ { "Width",   0.f,    2.f,    1.f,   C::Linear, "" },
                      { "Haas",    0.f,    20.f,   0.f,   C::Linear, "ms" },
                      { "MonoBelow",20.f,  1000.f, 20.f,  C::Log, "Hz" },
                      none }} },
};
static_assert(sizeof(kInfos) / sizeof(kInfos[0]) == (size_t) FxType::NumTypes);
} // namespace

const FxTypeInfo& getFxTypeInfo(FxType t)
{
    auto i = std::clamp((int) t, 0, (int) FxType::NumTypes - 1);
    return kInfos[i];
}

float fxNormToReal(const FxParamInfo& info, float x)
{
    x = std::clamp(x, 0.f, 1.f);
    switch (info.curve) {
        case ParamCurve::Log:      return info.min * std::pow(info.max / info.min, x);
        case ParamCurve::Discrete: return std::round(info.min + x * (info.max - info.min));
        default:                   return info.min + x * (info.max - info.min);
    }
}

float fxRealToNorm(const FxParamInfo& info, float v)
{
    if (info.max == info.min) return 0.f;
    if (info.curve == ParamCurve::Log)
        return std::clamp(std::log(std::max(v, info.min) / info.min) / std::log(info.max / info.min), 0.f, 1.f);
    return std::clamp((v - info.min) / (info.max - info.min), 0.f, 1.f);
}

std::unique_ptr<FxModule> createFxModule(FxType t)
{
    switch (t) {
        case FxType::Drive:     return std::make_unique<DriveFx>();
        case FxType::Fold:      return std::make_unique<FoldFx>();
        case FxType::Crush:     return std::make_unique<CrushFx>();
        case FxType::Filter:    return std::make_unique<FilterFx>();
        case FxType::Formant:   return std::make_unique<FormantFx>();
        case FxType::Comb:      return std::make_unique<CombFx>();
        case FxType::EQ:        return std::make_unique<EqFx>();
        case FxType::Phaser:    return std::make_unique<PhaserFx>();
        case FxType::Flanger:   return std::make_unique<FlangerFx>();
        case FxType::FreqShift: return std::make_unique<FreqShiftFx>();
        case FxType::RingMod:   return std::make_unique<RingModFx>();
        case FxType::Disperse:  return std::make_unique<DisperseFx>();
        case FxType::OTT:       return std::make_unique<OttFx>();
        case FxType::Width:     return std::make_unique<WidthFx>();
        default:                return nullptr;
    }
}

} // namespace nf
