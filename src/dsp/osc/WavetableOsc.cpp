#include "dsp/osc/OscApi.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nf {

const char* warpModeName(WarpMode m)
{
    switch (m) {
    case WarpMode::None: return "None";
    case WarpMode::Sync: return "Sync";
    case WarpMode::BendPlus: return "Bend+";
    case WarpMode::BendMinus: return "Bend-";
    case WarpMode::PWM: return "PWM";
    case WarpMode::Mirror: return "Mirror";
    case WarpMode::Asym: return "Asym";
    case WarpMode::Quantize: return "Quantize";
    case WarpMode::PhaseDist: return "PhaseDist";
    case WarpMode::FMFromB: return "FM from B";
    case WarpMode::RingFromB: return "Ring from B";
    default: return "?";
    }
}

namespace {
constexpr int kChunk = 256;
constexpr double kTwoPi = 6.283185307179586;

float detRandom(uint32_t seed, int i)
{
    uint32_t x = seed * 0x9E3779B9u + (uint32_t) i * 0x85EBCA6Bu + 0x1234567u;
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (float) (x >> 8) * (1.0f / 16777216.0f);
}

struct VoiceCfg {
    double inc, gain, gL, gR;
};

struct WarpCfg {
    float amt = 0;
    double e = 1, k = 1, m = 0.5, steps = 1024, mult = 1, syncDev = 0;
};

template <WarpMode M>
inline double warpPhase(double x, const WarpCfg& w)
{
    if constexpr (M == WarpMode::Sync) {
        const double y = x * w.k;
        return y - std::floor(y);
    } else if constexpr (M == WarpMode::BendPlus) {
        return std::pow(x, w.e);
    } else if constexpr (M == WarpMode::BendMinus) {
        return 1.0 - std::pow(1.0 - x, w.e);
    } else if constexpr (M == WarpMode::PWM) {
        return std::min(x * w.k, 1.0);
    } else if constexpr (M == WarpMode::Mirror) {
        const double t = x < 0.5 ? 2.0 * x : 2.0 - 2.0 * x;
        return x + w.amt * (t - x);
    } else if constexpr (M == WarpMode::Asym) {
        return x - 0.9 * w.amt * std::sin(kTwoPi * x) / kTwoPi;
    } else if constexpr (M == WarpMode::Quantize) {
        return std::floor(x * w.steps) / w.steps;
    } else if constexpr (M == WarpMode::PhaseDist) {
        return x < w.m ? 0.5 * x / w.m : 0.5 + 0.5 * (x - w.m) / (1.0 - w.m);
    } else {
        return x;
    }
}

struct Ctx {
    const WavetableBank* bank;
    const VoiceCfg* voices;
    int nv;
    const WarpCfg* warp;
    float posA, posB; // block start/end position
    int blockTotal;   // for ramp
    int blockStart;
};

template <WarpMode M>
void renderVoices(const Ctx& c, double* phases, int start, int m, const double* off, const double* dEnv,
                  const float* ring, float* L, float* R, float* mono)
{
    const WarpCfg& w = *c.warp;
    const float posStep = c.blockTotal > 0 ? (c.posB - c.posA) / (float) c.blockTotal : 0.f;
    const float pos0 = c.posA + posStep * (float) c.blockStart;
    for (int v = 0; v < c.nv; ++v) {
        const VoiceCfg& vc = c.voices[v];
        double ph = phases[v];
        for (int s = 0; s < m; ++s) {
            double x = ph + off[s];
            if (x >= 1.0 || x < 0.0) x -= std::floor(x);
            const double ph2 = warpPhase<M>(x, w);
            const double eff = vc.inc * w.mult + dEnv[s];
            const float pos = pos0 + posStep * (float) s;
            float y = c.bank->lookup(ph2, pos, eff);
            if constexpr (M == WarpMode::Sync) {
                if (w.syncDev > 0.0) {
                    const double w0 = std::min(0.05, 3.0 * vc.inc);
                    const double d = std::min(x, 1.0 - x);
                    if (d < w0) {
                        const double u = d / w0, g = u * u * (3.0 - 2.0 * u);
                        y *= (float) (1.0 - w.syncDev * (1.0 - g));
                    }
                }
            }
            y *= ring[s];
            const float ys = y * (float) vc.gain;
            mono[s] += ys;
            L[s] += ys * (float) vc.gL;
            R[s] += ys * (float) vc.gR;
            ph += vc.inc;
            if (ph >= 1.0) ph -= 1.0;
        }
        phases[v] = ph;
    }
}
} // namespace

WavetableOsc::WavetableOsc()
{
    for (int i = 0; i < 16; ++i) phase_[i] = detRandom(0, i);
}
WavetableOsc::~WavetableOsc() = default;

void WavetableOsc::prepare(double sampleRate)
{
    sr_ = sampleRate > 0 ? sampleRate : 48000.0;
    oPrev_ = dEnv_ = 0.0;
    havePrevPos_ = false;
}

void WavetableOsc::setBank(std::shared_ptr<const WavetableBank> bank) { bank_ = std::move(bank); }

void WavetableOsc::noteOn(bool retrigger, uint32_t seed, const OscParams& p)
{
    oPrev_ = dEnv_ = 0.0;
    prevPos_ = p.position;
    havePrevPos_ = true;
    if (!retrigger) return;
    for (int i = 0; i < 16; ++i) {
        const double v = (double) p.phase + (double) p.phaseRand * detRandom(seed, i);
        phase_[i] = v - std::floor(v);
    }
}

void WavetableOsc::process(float* outL, float* outR, int n, double baseHz, const OscParams& p,
                           const float* pmIn, const float* xIn, float* monoOut)
{
    if (monoOut) std::fill(monoOut, monoOut + n, 0.f);
    if (!bank_ || n <= 0) return;

    // ---- unison layout
    const int N = std::clamp(p.unison, 1, 16);
    VoiceCfg vc[16];
    double wsum = 0.0;
    double wts[16];
    const double detCents = (double) std::clamp(p.detune, 0.f, 1.f) * 50.0;
    const double baseInc = baseHz * std::exp2((double) p.pitchSemis / 12.0) / sr_;
    const double wSide = 0.1 + 0.9 * (double) std::clamp(p.blend, 0.f, 1.f);
    for (int i = 0; i < N; ++i) {
        const double pp = N == 1 ? 0.0 : -1.0 + 2.0 * i / (N - 1);
        const double cents = pp * std::sqrt(std::fabs(pp)) * detCents;
        vc[i].inc = std::clamp(baseInc * std::exp2(cents / 1200.0), 0.0, 0.49);
        const bool centre = N <= 2 || std::fabs(pp) <= 1.0 / (N - 1) + 1e-9;
        wts[i] = centre ? 1.0 : wSide;
        wsum += wts[i] * wts[i];
        const double pan = std::clamp(p.stereo * pp + p.pan, -1.0, 1.0);
        const double ang = (pan + 1.0) * 0.25 * 3.141592653589793;
        vc[i].gL = std::cos(ang);
        vc[i].gR = std::sin(ang);
    }
    const double norm = p.level / std::sqrt(wsum);
    for (int i = 0; i < N; ++i) vc[i].gain = wts[i] * norm;

    // ---- warp config
    WarpCfg w;
    const float amt = std::clamp(p.warpAmount, 0.f, 1.f);
    w.amt = amt;
    WarpMode mode = p.warp;
    if (amt <= 0.f && mode != WarpMode::FMFromB && mode != WarpMode::RingFromB) mode = WarpMode::None;
    switch (mode) {
    case WarpMode::Sync: w.k = 1.0 + 7.0 * amt; w.mult = w.k;
        w.syncDev = std::min(1.0, 2.0 * std::fabs(w.k - std::round(w.k))); break;
    case WarpMode::BendPlus:
    case WarpMode::BendMinus: w.e = 1.0 + 3.0 * amt; w.mult = w.e; break;
    case WarpMode::PWM: w.k = 1.0 / (1.0 - 0.85 * amt); w.mult = w.k; break;
    case WarpMode::Mirror: w.mult = 1.0 + amt; break;
    case WarpMode::Asym: w.mult = 1.0 + 0.9 * amt; break;
    case WarpMode::Quantize: w.steps = std::round(std::exp2(10.0 - 9.0 * amt)); break;
    case WarpMode::PhaseDist: w.m = 0.5 - 0.4 * amt; w.mult = 0.5 / w.m; break;
    default: break;
    }

    const bool usePm = pmIn && p.fmAmount > 0.f;
    const bool useFmB = mode == WarpMode::FMFromB && xIn;
    const bool useRing = mode == WarpMode::RingFromB && xIn;
    const double pmScale = (double) p.fmAmount * 8.0 / kTwoPi;
    const double fmbScale = (double) amt * 4.0 / kTwoPi;

    const float posNow = std::clamp(p.position, 0.f, 1.f);
    if (!havePrevPos_) { prevPos_ = posNow; havePrevPos_ = true; }
    const float posPrev = prevPos_;
    prevPos_ = posNow;

    double off[kChunk], dEnv[kChunk];
    float ring[kChunk], mono[kChunk], L[kChunk], R[kChunk];

    for (int start = 0; start < n; start += kChunk) {
        const int m = std::min(kChunk, n - start);
        for (int s = 0; s < m; ++s) {
            double o = 0.0;
            if (usePm) o += pmScale * (double) pmIn[start + s];
            if (useFmB) o += fmbScale * (double) xIn[start + s];
            const double d = std::fabs(o - oPrev_);
            oPrev_ = o;
            dEnv_ = std::max(d, dEnv_ * 0.9);
            off[s] = o;
            dEnv[s] = dEnv_;
            ring[s] = useRing ? 1.f + amt * (xIn[start + s] - 1.f) : 1.f;
            mono[s] = L[s] = R[s] = 0.f;
        }
        Ctx c { bank_.get(), vc, N, &w, posPrev, posNow, n, start };
        switch (mode) {
        case WarpMode::Sync: renderVoices<WarpMode::Sync>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        case WarpMode::BendPlus: renderVoices<WarpMode::BendPlus>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        case WarpMode::BendMinus: renderVoices<WarpMode::BendMinus>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        case WarpMode::PWM: renderVoices<WarpMode::PWM>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        case WarpMode::Mirror: renderVoices<WarpMode::Mirror>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        case WarpMode::Asym: renderVoices<WarpMode::Asym>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        case WarpMode::Quantize: renderVoices<WarpMode::Quantize>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        case WarpMode::PhaseDist: renderVoices<WarpMode::PhaseDist>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        default: renderVoices<WarpMode::None>(c, phase_, start, m, off, dEnv, ring, L, R, mono); break;
        }
        for (int s = 0; s < m; ++s) {
            outL[start + s] += L[s];
            outR[start + s] += R[s];
            if (monoOut) monoOut[start + s] = mono[s];
        }
    }
}

} // namespace nf
