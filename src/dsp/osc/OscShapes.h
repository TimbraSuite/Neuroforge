#pragma once
// Band-limited (polyBLEP / polyBLAMP) basic waveforms shared by FmOperator and SubOsc.
#include <algorithm>
#include <cmath>

namespace nf::oscdetail {

inline double polyBlep(double t, double dt)
{
    if (t < dt) { const double x = t / dt; return x + x - x * x - 1.0; }
    if (t > 1.0 - dt) { const double x = (t - 1.0) / dt; return x * x + x + x + 1.0; }
    return 0.0;
}
inline double polyBlamp(double t, double dt)
{
    if (t < dt) { const double x = t / dt - 1.0; return -(1.0 / 3.0) * x * x * x; }
    if (t > 1.0 - dt) { const double x = (t - 1.0) / dt + 1.0; return (1.0 / 3.0) * x * x * x; }
    return 0.0;
}
inline double fr1(double x) { return x - std::floor(x); }

// types: 0 sine, 1 triangle, 2 saw, 3 square. ph in [0,1), dt = phase increment.
inline double basicWave(int type, double ph, double dt)
{
    dt = std::clamp(dt, 1e-9, 0.45);
    switch (type) {
    case 0: return std::sin(6.283185307179586 * ph);
    case 1: {
        double y = 4.0 * std::fabs(fr1(ph + 0.25) - 0.5) - 1.0; // peak at 0.25, zero at 0
        y += -8.0 * dt * polyBlamp(fr1(ph - 0.25), dt);
        y += 8.0 * dt * polyBlamp(fr1(ph - 0.75), dt);
        return y;
    }
    case 2: { // rising ramp, zero at 0, drop at 0.5
        double y = 2.0 * fr1(ph + 0.5) - 1.0;
        y -= polyBlep(fr1(ph - 0.5), dt);
        return y;
    }
    default: {
        double y = ph < 0.5 ? 1.0 : -1.0;
        y += polyBlep(ph, dt);
        y -= polyBlep(fr1(ph - 0.5), dt);
        return y;
    }
    }
}

// order[i] = basic type at morph stop i (4 stops), shape 0..1
inline double morphWave(const int order[4], float shape, double ph, double dt)
{
    const double s = std::clamp((double) shape, 0.0, 1.0) * 3.0;
    const int i = std::min((int) s, 2);
    const double a = s - i;
    const double y0 = basicWave(order[i], ph, dt);
    if (a <= 0.0) return y0;
    return y0 + a * (basicWave(order[i + 1], ph, dt) - y0);
}

} // namespace nf::oscdetail
