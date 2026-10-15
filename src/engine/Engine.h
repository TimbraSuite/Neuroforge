#pragma once
// Neuroforge synthesis engine. Pure C++, no JUCE. One instance per plugin, plus extra
// instances for offline Capture renders. Not thread-safe except setCapture().
#include "Decimator.h"
#include "ModSources.h"
#include "Params.h"
#include "dsp/fx/Crossover.h"
#include "dsp/fx/FxModule.h"
#include "dsp/fx/Svf.h"
#include "dsp/osc/OscApi.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace nf {

struct MidiEvent {
    int offset = 0;        // sample offset in the host block
    uint8_t status = 0, d1 = 0, d2 = 0;
};

constexpr int kMaxVoices = 8;
constexpr int kNumCaptures = 4;
constexpr int kSubBlock = 32; // modulation update interval at the internal (oversampled) rate

class Engine;

class Voice {
public:
    void prepare(double sr);
    bool active() const { return amp.isActive(); }

    int note = -1;
    float velocity = 0.f, random = 0.f;
    bool gate = false;
    uint64_t age = 0;
    double curSemis = 60, targetSemis = 60; // glide state
    float baseHz = 261.6f;

    Envelope amp, env2, env3;
    WavetableOsc oscA, oscB;
    FmOperator op;
    SubOsc sub;
    Svf filter;
    Rng noiseRng;
    float noiseLp = 0.f;
    std::shared_ptr<const WavetableBank> bankA, bankB;
    int tableA = -1, tableB = -1;

    float modOff[pid::count] {}; // per-voice modulation offsets (normalized), only voice params used
};

class Engine {
public:
    Engine();
    void prepare(double hostSampleRate, int maxHostBlock);
    void reset();
    int getLatencySamples() const { return latency; }

    // params: normalized, size pid::count; read once per call.
    void render(float* outL, float* outR, int n, const float* params, const MidiEvent* events, int numEvents,
                const Transport& transport);

    // Captured wavetables (audio-thread safe; called from any thread).
    void setCapture(int slot, std::shared_ptr<const WavetableBank> bank);
    std::shared_ptr<const WavetableBank> getCapture(int slot) const;

    // For UI metering/visualisation (written on the audio thread).
    std::atomic<float> uiLfo[pid::kNumLfos] {}, uiMotion[pid::kNumPasses] {}, uiEnv[3] {};
    std::atomic<float> uiLfoPhase[pid::kNumLfos] {}, uiMotionPhase[pid::kNumPasses] {}; // 0..1 cycle position
    std::atomic<float> uiStepper[pid::kNumSteppers] {}, uiStepperPhase[pid::kNumSteppers] {};
    std::atomic<float> uiOffsets[pid::count] {}; // last global modulation offsets for knob rings

    // Seed for deterministic renders.
    void setSeed(uint32_t s) { seed = s; }
    // Capture renders exclude the sub bus (the captured table is re-split by Low Guard on playback).
    void setExcludeSub(bool e) { excludeSub = e; }

private:
    void handleMidi(const MidiEvent& e, const float* p);
    void noteOn(int note, float vel, const float* p);
    void noteOff(int note, const float* p);
    Voice* allocVoice();
    void renderSub(int start, int len, const float* p, const Transport& t, double ppqAtStart);
    void computeGlobalSources(const float* p, double seconds, const Transport& t, double ppqEnd);
    float sourceValue(ModSrc s, const Voice* v) const;
    void renderVoice(Voice& v, const float* p, int start, int len);
    void applyMatrix(const float* p, const Voice* newest);
    std::shared_ptr<const WavetableBank> bankFor(int tableIndex) const;
    void configureOversampling(int os);
    float val(const float* p, int id) const;           // global effective real value
    float vval(const float* p, const Voice& v, int id) const; // voice effective real value

    double hostRate = 48000, rate = 96000;
    int osFactor = 2, maxHost = 512, latency = 0;
    uint32_t seed = 12345;
    uint64_t ageCounter = 0;

    std::array<Voice, kMaxVoices> voices;
    std::vector<int> noteStack; // mono/legato
    Voice* newest = nullptr;
    float lastNoteHz = 87.31f;

    // modulation state
    Oscillation lfo[pid::kNumLfos];
    Motion motion[pid::kNumPasses];
    Oscillation stepperOsc[pid::kNumSteppers];
    float lfoVal[pid::kNumLfos] {}, motionVal[pid::kNumPasses] {}, stepperVal[pid::kNumSteppers] {};
    float modWheel = 0, pitchBend = 0, aftertouch = 0;
    float globalOff[pid::count] {};
    struct ActiveSlot { ModSrc src; int dst; float amt; bool voiceSrc; bool voiceDst; };
    std::vector<ActiveSlot> activeSlots;

    // FX
    struct Slot {
        std::array<std::unique_ptr<FxModule>, (size_t) FxType::NumTypes> modules;
        int type = 0;
    };
    std::array<Slot, pid::kNumSlots> slots;
    LR4Crossover guardSplit, midClean;
    Svf subLp;
    float subDcX = 0, subDcY = 0;

    // buffers (internal rate)
    std::vector<float> bufL, bufR, bufSub, tmpL, tmpR, voiceL, voiceR, voiceSub, opBuf, bBuf, lowL, lowR, dryL, dryR;
    Decimator decL, decR;

    // Capture banks are swapped from other threads. std::atomic<std::shared_ptr> isn't available
    // in every standard library (Apple libc++), so a tiny spin lock guards the pointer copy.
    struct CaptureSlot {
        mutable std::atomic_flag busy = ATOMIC_FLAG_INIT;
        std::shared_ptr<const WavetableBank> bank;
        void store(std::shared_ptr<const WavetableBank> b)
        {
            while (busy.test_and_set(std::memory_order_acquire)) {}
            bank.swap(b);
            busy.clear(std::memory_order_release);
            // the old bank (now in b) is released here, outside the lock
        }
        std::shared_ptr<const WavetableBank> load() const
        {
            while (busy.test_and_set(std::memory_order_acquire)) {}
            auto copy = bank;
            busy.clear(std::memory_order_release);
            return copy;
        }
    };
    std::array<CaptureSlot, kNumCaptures> captures;
    bool excludeSub = false;
};

} // namespace nf
