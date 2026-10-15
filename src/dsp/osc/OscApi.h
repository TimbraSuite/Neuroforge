#pragma once
// Public oscillator API used by the engine. The OSC implementer owns this directory and may
// add private members / helper headers, but must keep these public signatures.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nf {

// ---------------------------------------------------------------------------------------------
// Immutable, band-limited wavetable: numFrames single-cycle frames of kFrameSize samples,
// each with a mipmap chain (one level per octave). Built off the audio thread.
class WavetableBank {
public:
    static constexpr int kFrameSize = 2048;

    // frames: each exactly kFrameSize samples (one cycle). Removes DC, normalizes the bank
    // (peak of the whole bank -> 1), builds mipmaps with FFT brick-wall per level.
    static std::shared_ptr<const WavetableBank> fromFrames(const std::vector<std::vector<float>>& frames,
                                                           std::string name);
    // Resample an arbitrary-length single cycle to kFrameSize (cubic/sinc) before building.
    static std::vector<float> resampleCycle(const float* data, int length);

    const std::string& getName() const;
    int getNumFrames() const;
    // Raw (full-band, level 0) frame data, for UI drawing. frame clamped.
    const float* getFrame(int frame) const;

    // Band-limited lookup. phase in [0,1). position in [0,1] scans frames with linear crossfade.
    // phaseInc = cycles per sample (selects mip level; crossfade between adjacent levels).
    float lookup(double phase, float position, double phaseInc) const;

    struct Impl;
    explicit WavetableBank(std::unique_ptr<Impl>);
    ~WavetableBank();
private:
    std::unique_ptr<Impl> impl;
};

// Built-in tables, generated procedurally at startup (no sample files needed).
namespace tables {
    // Names, in order: "Saw>Square", "Basic Shapes" (sine,tri,saw,square), "PWM", "Formant Vox",
    // "Harmonic Sweep", "Metal Bars", "Reese Pulse", "Growl FM", "Sync Saw", "Digital Grit",
    // "Hollow", "Phase Dist". Each 16-64 frames, designed to sound good swept for neuro bass.
    int getNumBuiltIn();
    std::shared_ptr<const WavetableBank> getBuiltIn(int index); // thread-safe, built once lazily
    std::vector<std::string> getBuiltInNames();
}

// ---------------------------------------------------------------------------------------------
enum class WarpMode : int {
    None = 0, Sync, BendPlus, BendMinus, PWM, Mirror, Asym, Quantize, PhaseDist, FMFromB, RingFromB, NumModes
};
const char* warpModeName(WarpMode m);

struct OscParams {
    float pitchSemis = 0.f;   // offset from note (octave*12 + semi + fine/100 + modulation)
    float level = 1.f;        // linear gain
    float pan = 0.f;          // -1..1
    float position = 0.f;     // wavetable position 0..1
    int   unison = 1;         // 1..16
    float detune = 0.2f;      // 0..1 -> 0..100 cents total spread (curve: gentle near center)
    float stereo = 0.7f;      // unison stereo spread 0..1
    float blend = 0.5f;       // center voice vs side voices 0..1 (0 = only center audible-ish, 1 = equal)
    float phase = 0.f;        // start phase 0..1
    float phaseRand = 0.f;    // 0 = coherent retrigger, 1 = fully random start phases (seeded)
    WarpMode warp = WarpMode::None;
    float warpAmount = 0.f;   // 0..1
    float fmAmount = 0.f;     // 0..1 depth of external phase modulation input (pmIn), index up to ~8
};

// Unison wavetable oscillator. Mono-per-voice engine object; renders stereo.
class WavetableOsc {
public:
    WavetableOsc();
    ~WavetableOsc();
    void prepare(double sampleRate);
    void setBank(std::shared_ptr<const WavetableBank> bank); // audio-thread safe (shared_ptr copy only)
    // retrigger: reset phases (using p.phase + seeded randomness), seed: per-note random seed
    void noteOn(bool retrigger, uint32_t seed, const OscParams& p);
    // ADDs into outL/outR. baseHz = note frequency (with glide/pitch-bend applied).
    // pmIn: optional phase-mod signal (-1..1), scaled by p.fmAmount; may be nullptr.
    // xIn:  optional cross signal from OSC B for FMFromB / RingFromB warps (-1..1); may be nullptr.
    // monoOut: optional; if non-null, writes (not adds) this osc's mono pre-pan output (for B->A).
    void process(float* outL, float* outR, int n, double baseHz, const OscParams& p,
                 const float* pmIn, const float* xIn, float* monoOut);
private:
    std::shared_ptr<const WavetableBank> bank_;
    double sr_ = 48000.0;
    double phase_[16] = {};
    double oPrev_ = 0.0, dEnv_ = 0.0;
    float prevPos_ = 0.f;
    bool havePrevPos_ = false;
};

// ---------------------------------------------------------------------------------------------
// Sine-family FM operator (modulator). Shape morphs sine -> triangle -> saw -> square (band-limited).
struct FmOpParams {
    float ratio = 1.f;     // frequency ratio to note (0.25..16), fine already included
    float feedback = 0.f;  // 0..1 self-modulation (DX7-style averaged)
    float shape = 0.f;     // 0..1
    float level = 1.f;     // output amplitude 0..1 (the engine scales index separately)
};
class FmOperator {
public:
    void prepare(double sampleRate);
    void noteOn(bool retrigger);
    // writes (not adds) n samples in -1..1
    void process(float* out, int n, double baseHz, const FmOpParams& p);
private:
    double sr_ = 48000.0, phase_ = 0.0;
    float y1_ = 0.f, y2_ = 0.f;
};

// Sub oscillator: band-limited sine -> triangle -> square -> saw morph, mono.
struct SubParams {
    float octave = -1.f;  // -2..0 relative to note
    float shape = 0.f;    // 0..1 (0 sine, 0.33 tri, 0.66 square, 1 saw)
    float level = 1.f;
};
class SubOsc {
public:
    void prepare(double sampleRate);
    void noteOn(bool retrigger);
    void process(float* out, int n, double baseHz, const SubParams& p); // ADDs into out
private:
    double sr_ = 48000.0, phase_ = 0.0;
};

} // namespace nf
