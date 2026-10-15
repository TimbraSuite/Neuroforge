#pragma once
// FX module contract shared by the engine and every effect type.
// Rules for implementers:
//  * prepare() may allocate; process()/reset() must never allocate or lock.
//  * process() is called with n <= kMaxSubBlock samples. Parameter values in `p`
//    are already in real units (see FxTypeInfo) and change once per call;
//    smooth internally (one-pole ~5 ms or linear ramp across the block) to avoid zipper noise.
//  * Must stay finite and bounded for every parameter combination (feedback paths clamp/saturate).
//  * Stereo in, stereo out, in place.
#include <array>
#include <memory>

namespace nf {

constexpr int kMaxSubBlock = 64;
constexpr int kFxParams = 4;

enum class FxType : int {
    Off = 0, Drive, Fold, Crush, Filter, Formant, Comb, Phaser, Flanger,
    FreqShift, RingMod, Disperse, OTT, EQ, Width, NumTypes
};

enum class ParamCurve : int { Linear, Log, Discrete };

struct FxParamInfo {
    const char* name;   // short label, e.g. "Drive"
    float min, max, def;
    ParamCurve curve;   // how normalized 0..1 maps to [min,max]; Log requires min > 0
    const char* unit;   // "dB", "Hz", "%", "", ...
};

struct FxTypeInfo {
    const char* name;           // "Drive"
    std::array<FxParamInfo, kFxParams> params; // unused params: name == nullptr
};

const FxTypeInfo& getFxTypeInfo(FxType t);
float fxNormToReal(const FxParamInfo& info, float norm01);
float fxRealToNorm(const FxParamInfo& info, float real);

struct FxContext {
    double sampleRate = 48000.0; // the (oversampled) rate the module runs at
    float noteHz = 87.31f;        // frequency of the most recent note (for key-tracking)
    double bpm = 174.0;
};

class FxModule {
public:
    virtual ~FxModule() = default;
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void process(float* left, float* right, int n, const float* p, const FxContext& ctx) = 0;
};

std::unique_ptr<FxModule> createFxModule(FxType t); // returns nullptr for Off

} // namespace nf
