#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "engine/Archetypes.h"
#include "engine/Capture.h"
#include "engine/Engine.h"
#include <array>
#include <thread>

// ---- shared text formatting (used by host parameter text and the editor) ----
juce::String nfFormat(int paramId, float norm);                 // real value with unit / choice name / On-Off
juce::String nfFormatFx(const nf::FxParamInfo& info, float norm);

struct NfPresetEntry {
    juce::String name, category;
    bool user = false;
    juce::File file;
    int factoryIndex = -1;
};

struct NfModRoute { int row; int src; float amount; };

class NeuroforgeProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,   // captures / preset changed
                            private juce::AsyncUpdater {
public:
    NeuroforgeProcessor();
    ~NeuroforgeProcessor() override;

    // AudioProcessor
    const juce::String getName() const override { return "Neuroforge"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- parameters ----
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* getParam(int id) const { return params[(size_t) id]; }
    float getRaw(int id) const { return raw[(size_t) id]->load(std::memory_order_relaxed); }
    void setParamReal(int id, float real);          // message thread, wraps a gesture
    void setParamNorm(int id, float norm);

    // ---- presets (message thread) ----
    nf::ParamState currentState() const;
    void loadState(const nf::ParamState& s);
    juce::String presetName { "Init" };
    void applyStartupPresetIfFresh();
    std::vector<NfPresetEntry> getPresetList() const;
    void loadPreset(const NfPresetEntry& e);
    void initPatch();
    bool saveUserPreset(const juce::String& name);
    static juce::File userPresetDir();
    void generate(int archetype, uint32_t seed);
    void mutate(float amount);

    // ---- capture ----
    nf::CaptureSettings captureSettings;
    void startCapture(bool thenNextGen);
    bool isCapturing() const { return capturing.load(); }
    juce::String getCaptureStatus() const;
    std::atomic<int> captureGeneration { 0 };
    bool stateWasRestored = false, startupPresetApplied = false;

    // ---- modulation routing helpers ----
    std::vector<NfModRoute> getRoutesFor(int paramId) const;
    int countRoutesFor(int paramId) const;
    bool addRoute(int src, int paramId, float amount = 0.5f);
    void removeRoute(int row);
    void setRouteAmount(int row, float amount);
    static int dstIndexOf(int paramId);

    // ---- UI data ----
    nf::Engine& getEngine() { return engine; }
    void copyScope(float* dst, int count) const;   // last `count` output samples (mono)
    std::atomic<float> peakL { 0.f }, peakR { 0.f };
    std::atomic<double> hostBpm { 174.0 };
    juce::MidiKeyboardState keyboardState;
    static constexpr int kScopeSize = 8192;

private:
    void handleAsyncUpdate() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    int pickCaptureSlot();
    void setStatus(const juce::String& s);

    nf::Engine engine;
    std::array<juce::RangedAudioParameter*, (size_t) nf::pid::count> params {};
    std::array<std::atomic<float>*, (size_t) nf::pid::count> raw {};
    std::vector<float> paramBuf;
    std::vector<nf::MidiEvent> events, chunkEvents;
    double sampleRate = 48000.0;
    int maxBlock = 512, lastLatency = -1;

    std::array<float, kScopeSize> scope {};
    std::atomic<uint32_t> scopeWrite { 0 };

    std::thread captureThread;
    std::atomic<bool> capturing { false };
    std::atomic<int> pendingNextGen { -1 };
    int captureCycle = 0;
    juce::CriticalSection statusLock;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuroforgeProcessor)
};
