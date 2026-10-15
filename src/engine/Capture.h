#pragma once
// Capture: render a patch offline and turn it into a wavetable (pitch-synchronous slicing).
#include "Engine.h"
#include <array>
#include <memory>
#include <string>

namespace nf {

struct CaptureSettings {
    int note = 36;          // MIDI note held during the render (C2)
    double beats = 4.0;     // length of the render in beats
    double bpm = 174.0;
    int maxFrames = 128;    // wavetable frames taken evenly across the render
};

// Blocking; call from a background thread. `existing` = current capture banks (so captures of
// captures work). The sub bus is excluded from the render (mid bus only).
std::shared_ptr<const WavetableBank> renderCapture(const ParamState& state,
                                                   const std::array<std::shared_ptr<const WavetableBank>, kNumCaptures>& existing,
                                                   const CaptureSettings& settings, std::string name);

// "Next Generation": returns a patch that plays capture `slot` on Osc A with a cleared Forge,
// FM/Osc B/noise/filter off and modulation routes into cleared areas removed. Sub, envelopes,
// output and macros' other routes are kept, so the user can immediately re-process the resample.
ParamState nextGenerationState(const ParamState& current, int slot);

// Serialisation of a bank's level-0 frames (for plugin state). Compact float16-free: raw float32 bytes.
std::vector<float> bankToFrames(const WavetableBank& bank);                 // numFrames * kFrameSize
std::shared_ptr<const WavetableBank> bankFromFrames(const std::vector<float>& data, std::string name);

} // namespace nf
