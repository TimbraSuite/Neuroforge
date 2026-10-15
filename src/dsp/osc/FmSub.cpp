#include "dsp/osc/OscApi.h"
#include "dsp/osc/OscShapes.h"
#include <algorithm>
#include <cmath>

namespace nf {

void FmOperator::prepare(double sampleRate) { sr_ = sampleRate > 0 ? sampleRate : 48000.0; }
void FmOperator::noteOn(bool retrigger)
{
    if (retrigger) { phase_ = 0.0; y1_ = y2_ = 0.f; }
}

void FmOperator::process(float* out, int n, double baseHz, const FmOpParams& p)
{
    static const int order[4] = { 0, 1, 2, 3 };
    const double inc = std::clamp(baseHz * p.ratio / sr_, 0.0, 0.49);
    const double fb = std::clamp(p.feedback, 0.f, 1.f) * 0.5; // max +-0.5 cycle (pi rad)
    for (int i = 0; i < n; ++i) {
        const double pm = fb * 0.5 * ((double) y1_ + (double) y2_);
        const double x = oscdetail::fr1(phase_ + pm);
        const float y = (float) oscdetail::morphWave(order, p.shape, x, inc);
        y2_ = y1_;
        y1_ = y;
        out[i] = y * p.level;
        phase_ += inc;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }
}

void SubOsc::prepare(double sampleRate) { sr_ = sampleRate > 0 ? sampleRate : 48000.0; }
void SubOsc::noteOn(bool retrigger)
{
    if (retrigger) phase_ = 0.0;
}

void SubOsc::process(float* out, int n, double baseHz, const SubParams& p)
{
    static const int order[4] = { 0, 1, 3, 2 }; // sine, tri, square, saw
    const int oct = (int) std::lround(std::clamp(p.octave, -2.f, 0.f));
    const double inc = std::clamp(baseHz * std::ldexp(1.0, oct) / sr_, 0.0, 0.49);
    for (int i = 0; i < n; ++i) {
        out[i] += (float) oscdetail::morphWave(order, p.shape, phase_, inc) * p.level;
        phase_ += inc;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }
}

} // namespace nf
