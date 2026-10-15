#pragma once
// Envelopes, LFOs and Motion sequencers. Deterministic: same inputs -> same output.
#include "Params.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nf {

// Small deterministic RNG (xorshift32), used for S&H, per-note random, unison seeds.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.f / 16777216.f); }   // [0,1)
    float bi() { return uni() * 2.f - 1.f; }                              // [-1,1)
};

// ADSR with curved segments and optional AD loop (Noisia-style looping modulator envelope).
// Segment durations are exact (time-based), which keeps loops tempo-predictable.
class Envelope {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare(double sr) { sampleRate = sr; }
    void set(float aMs, float dMs, float s, float rMs, bool loopOn)
    {
        aSamples = std::max(1.0, aMs * 0.001 * sampleRate);
        dSamples = std::max(1.0, dMs * 0.001 * sampleRate);
        rSamples = std::max(1.0, rMs * 0.001 * sampleRate);
        sustain = std::clamp(s, 0.f, 1.f);
        loop = loopOn;
    }
    void gateOn(bool legato)
    {
        if (legato && stage != Stage::Idle && stage != Stage::Release) return;
        start = value; t = 0; stage = Stage::Attack;
    }
    void gateOff() { if (stage != Stage::Idle) { start = value; t = 0; stage = Stage::Release; } }
    void kill() { stage = Stage::Idle; value = 0.f; }
    bool isActive() const { return stage != Stage::Idle; }
    Stage getStage() const { return stage; }
    float get() const { return value; }

    float next()
    {
        switch (stage) {
            case Stage::Idle: value = 0.f; break;
            case Stage::Attack: {
                double x = t / aSamples;
                float c = 1.f - (float) ((1.0 - x) * (1.0 - x)); // fast-start convex curve
                value = start + (1.f - start) * c;
                if (++t >= aSamples) { value = 1.f; t = 0; stage = Stage::Decay; }
                break;
            }
            case Stage::Decay: {
                double x = 1.0 - t / dSamples;
                value = sustain + (1.f - sustain) * (float) (x * x * x);
                if (++t >= dSamples) {
                    value = sustain; t = 0;
                    if (loop) { start = value; stage = Stage::Attack; }
                    else stage = Stage::Sustain;
                }
                break;
            }
            case Stage::Sustain: value = sustain; break;
            case Stage::Release: {
                double x = 1.0 - t / rSamples;
                value = start * (float) (x * x * x);
                if (++t >= rSamples) { value = 0.f; stage = Stage::Idle; }
                break;
            }
        }
        return value;
    }

private:
    double sampleRate = 48000, aSamples = 1, dSamples = 1, rSamples = 1, t = 0;
    float sustain = 1.f, value = 0.f, start = 0.f;
    bool loop = false;
    Stage stage = Stage::Idle;
};

struct Transport {
    double bpm = 174.0;
    double ppq = 0.0;      // quarter-note position at block start
    bool playing = false;
};

// Periodic modulator shared by LFOs and Motion. Bipolar output -1..1.
class Oscillation {
public:
    void reset(float phaseOffset, uint32_t seed)
    {
        phase = phaseOffset - std::floor(phaseOffset);
        rng = Rng(seed);
        prevRand = rng.bi(); nextRand = rng.bi(); held = prevRand;
    }

    // Advance by `seconds`. If hostLocked, phase follows host ppq instead (rhythmically locked).
    void advance(double seconds, double hz, bool hostLocked, double ppqEnd, double beats, float phaseOffset)
    {
        double old = phase;
        if (hostLocked && beats > 0) {
            double p = ppqEnd / beats + phaseOffset;
            phase = p - std::floor(p);
        } else {
            phase += seconds * hz;
            phase -= std::floor(phase);
        }
        if (phase < old) { prevRand = nextRand; nextRand = rng.bi(); held = prevRand; ++cycle; }
    }

    float shape(LfoShape s) const
    {
        const float x = (float) phase;
        switch (s) {
            case LfoShape::Sine:        return std::sin(x * 6.2831853f);
            case LfoShape::Triangle:    return x < 0.5f ? 4.f * x - 1.f : 3.f - 4.f * x;
            case LfoShape::SawUp:       return 2.f * x - 1.f;
            case LfoShape::SawDown:     return 1.f - 2.f * x;
            case LfoShape::Square:      return x < 0.5f ? 1.f : -1.f;
            case LfoShape::SampleHold:  return held;
            case LfoShape::SmoothRandom:{
                float k = 0.5f - 0.5f * std::cos(x * 3.14159265f);
                return prevRand + (nextRand - prevRand) * k;
            }
            default: return 0.f;
        }
    }
    double getPhase() const { return phase; }
    uint32_t getCycle() const { return cycle; }

private:
    double phase = 0;
    Rng rng;
    float prevRand = 0, nextRand = 0, held = 0;
    uint32_t cycle = 0;
};

inline double modRateHz(bool sync, float hz, int div, double bpm)
{
    return sync ? (bpm / 60.0) / rateDivBeats(div) : (double) hz;
}

// Motion: LFO or step sequencer. Output bipolar -1..1 (steps map 0..1 -> -1..1), glided.
class Motion {
public:
    void reset(uint32_t seed) { osc.reset(0.f, seed); value = 0.f; }
    Oscillation osc;

    float update(const float* stepValues, int numSteps, bool stepsMode, LfoShape s, float glide, double dt, double hz)
    {
        float target;
        if (stepsMode) {
            int n = std::clamp(numSteps, 1, 16);
            int idx = std::min(n - 1, (int) (osc.getPhase() * n));
            target = stepValues[idx] * 2.f - 1.f;
            // glide time = glide * step duration
            double stepDur = hz > 0 ? 1.0 / (hz * n) : 1.0;
            double tau = std::max(1e-4, (double) glide * stepDur * 0.5);
            float a = glide <= 0.001f ? 1.f : (float) (1.0 - std::exp(-dt / tau));
            value += (target - value) * a;
        } else {
            value = osc.shape(s);
        }
        return value;
    }
    float get() const { return value; }
private:
    float value = 0.f;
};

} // namespace nf
