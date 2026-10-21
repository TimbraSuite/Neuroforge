#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

using namespace nf;

//==============================================================================================
// Formatting helpers
//==============================================================================================
static juce::String fmtReal(float r, const juce::String& unit)
{
    // Decide the format on the ROUNDED value, so "99.97 Hz" prints as "100 Hz" (not "100.0 Hz")
    // and text -> value -> text round-trips consistently.
    auto rounded = [](float v, int dec) { const float k = std::pow(10.f, (float) dec); return std::round(v * k) / k; };
    if (unit == "Hz" || unit == "ms") {
        const juce::String big = unit == "Hz" ? " kHz" : " s";
        if (std::abs(rounded(r, 0)) >= 1000.f) return juce::String(r / 1000.f, 2) + big;
        if (std::abs(rounded(r, 1)) >= 100.f) return juce::String(rounded(r, 0), 0) + " " + unit;
        return juce::String(r, 1) + " " + unit;
    }
    if (unit == "dB") return juce::String(r, 1) + " dB";
    if (unit == "ct") return juce::String(r, 0) + " ct";
    if (unit == "st") return juce::String(r, 1) + " st";
    if (unit == "bit") return juce::String(r, 1) + " bit";
    return juce::String(r, 2) + (unit.isEmpty() ? "" : " " + unit);
}

// Inverse of fmtReal: understands the unit suffixes fmtReal prints ("1.20 kHz", "1.00 s").
static float parseReal(const juce::String& text, const juce::String& unit)
{
    const auto t = text.trim().toLowerCase();
    float v = t.getFloatValue();
    if (unit == "Hz" && t.endsWith("khz")) v *= 1000.f;
    if (unit == "ms" && t.endsWith("s") && !t.endsWith("ms")) v *= 1000.f;
    return v;
}

static bool isSlotP(int id)
{
    if (id < pid::slot0 || id >= pid::macro0) return false;
    int off = (id - pid::slot0) % pid::numSlot;
    return off >= pid::sP1 && off <= pid::sP4;
}

juce::String nfFormat(int id, float norm)
{
    const auto& d = paramDefs()[(size_t) id];
    const float r = normToReal(d, norm);
    if (d.curve == Curve::Toggle) return r >= 0.5f ? "On" : "Off";
    if (d.curve == Curve::Disc && !d.choices.empty()) {
        int i = std::clamp((int) std::lround(r), 0, (int) d.choices.size() - 1);
        return juce::String(d.choices[(size_t) i]);
    }
    if (isSlotP(id)) return juce::String(norm * 100.f, 0) + " %";
    const juce::String unit(d.unit);
    if (d.curve == Curve::Disc) return juce::String((int) std::lround(r)) + (unit.isEmpty() ? "" : " " + unit);
    return fmtReal(r, unit);
}

juce::String nfFormatFx(const FxParamInfo& info, float norm)
{
    const float r = fxNormToReal(info, norm);
    const juce::String unit(info.unit ? info.unit : "");
    if (info.curve == ParamCurve::Discrete) {
        if (info.min == 0.f && info.max == 1.f) return r >= 0.5f ? "On" : "Off";
        return juce::String((int) std::lround(r));
    }
    return fmtReal(r, unit);
}

//==============================================================================================
juce::AudioProcessorValueTreeState::ParameterLayout NeuroforgeProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& defs = paramDefs();
    for (int i = 0; i < (int) defs.size(); ++i) {
        const auto& d = defs[(size_t) i];
        float interval = 0.f;
        if ((d.curve == Curve::Disc || d.curve == Curve::Toggle) && d.max > d.min) interval = 1.f / (d.max - d.min);
        juce::NormalisableRange<float> range(0.f, 1.f, interval);
        auto attr = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([i](float v, int) { return nfFormat(i, v); })
            .withValueFromStringFunction([i](const juce::String& t) {
                const auto& d2 = paramDefs()[(size_t) i];
                for (size_t c = 0; c < d2.choices.size(); ++c)
                    if (t.trim().equalsIgnoreCase(juce::String(d2.choices[c]))) return realToNorm(d2, (float) c);
                if (d2.curve == Curve::Toggle) return (t.trim().equalsIgnoreCase("on") || t.getIntValue() > 0) ? 1.f : 0.f;
                if (isSlotP(i)) return juce::jlimit(0.f, 1.f, t.getFloatValue() / 100.f);
                return realToNorm(d2, parseReal(t, juce::String(d2.unit)));
            });
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { d.id, 1 }, d.name, range,
                                                               realToNorm(d, d.def), attr));
    }
    return layout;
}

NeuroforgeProcessor::NeuroforgeProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "NEUROFORGE", createLayout())
{
    const auto& defs = paramDefs();
    for (int i = 0; i < pid::count; ++i) {
        params[(size_t) i] = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(defs[(size_t) i].id));
        raw[(size_t) i] = apvts.getRawParameterValue(defs[(size_t) i].id);
        jassert(params[(size_t) i] && raw[(size_t) i]);
    }
    paramBuf.assign((size_t) pid::count, 0.f);
    events.resize(4096);
    chunkEvents.resize(4096);

}

// A brand-new instance opens on a real neuro bass instead of a bare init patch. This happens
// when the editor first opens (not in the constructor), so hosts that restore a project, and
// plugin validators that expect parameters to start at their defaults, are not affected.
void NeuroforgeProcessor::applyStartupPresetIfFresh()
{
    if (stateWasRestored || startupPresetApplied) return;
    startupPresetApplied = true;
    const auto& fp = factoryPresets();
    if (fp.empty()) return;
    NfPresetEntry e;
    e.name = fp.front().name; e.category = fp.front().category; e.factoryIndex = 0;
    loadPreset(e);
    undoStack.clear(); // nothing to undo back to on a fresh instance
}

NeuroforgeProcessor::~NeuroforgeProcessor()
{
    cancelPendingUpdate();
    if (captureThread.joinable()) captureThread.join();
}

bool NeuroforgeProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void NeuroforgeProcessor::prepareToPlay(double sr, int block)
{
    sampleRate = sr;
    maxBlock = std::max(block, 64);
    engine.prepare(sr, maxBlock);
    lastLatency = engine.getLatencySamples();
    setLatencySamples(lastLatency);
}

void NeuroforgeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) { buffer.clear(); return; }

    keyboardState.processNextMidiBuffer(midi, 0, n, true);

    for (int i = 0; i < pid::count; ++i) paramBuf[(size_t) i] = raw[(size_t) i]->load(std::memory_order_relaxed);

    int numEv = 0;
    for (const auto meta : midi) {
        const auto* d = meta.data;
        const int sz = meta.numBytes;
        if (sz < 1 || sz > 3 || d[0] >= 0xF0 || numEv >= (int) events.size()) continue;
        MidiEvent e;
        e.offset = std::clamp(meta.samplePosition, 0, n - 1);
        e.status = d[0]; e.d1 = sz > 1 ? d[1] : 0; e.d2 = sz > 2 ? d[2] : 0;
        events[(size_t) numEv++] = e;
    }

    Transport t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) { t.bpm = *b; hostBpm.store(*b); }
            if (auto p = pos->getPpqPosition()) t.ppq = *p;
            t.playing = pos->getIsPlaying();
        }

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    for (int start = 0, ev = 0; start < n; start += maxBlock) {
        const int len = std::min(maxBlock, n - start);
        int ce = 0;
        while (ev < numEv && events[(size_t) ev].offset < start + len) {
            chunkEvents[(size_t) ce] = events[(size_t) ev];
            chunkEvents[(size_t) ce].offset = std::max(0, events[(size_t) ev].offset - start);
            ++ce; ++ev;
        }
        Transport tc = t;
        tc.ppq += (double) start / sampleRate * t.bpm / 60.0;
        engine.render(L + start, R + start, len, paramBuf.data(), chunkEvents.data(), ce, tc);
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);

    // scope + meters
    float pl = 0.f, pr = 0.f;
    uint32_t w = scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(L[i])) L[i] = 0.f;
        if (!std::isfinite(R[i])) R[i] = 0.f;
        pl = std::max(pl, std::abs(L[i])); pr = std::max(pr, std::abs(R[i]));
        scope[(w + (uint32_t) i) & (kScopeSize - 1)] = 0.5f * (L[i] + R[i]);
    }
    scopeWrite.store(w + (uint32_t) n, std::memory_order_release);
    const float decay = std::exp(-(float) n / (0.25f * (float) sampleRate));
    peakL.store(std::max(pl, peakL.load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
    peakR.store(std::max(pr, peakR.load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);

    const int lat = engine.getLatencySamples();
    if (lat != lastLatency) { lastLatency = lat; setLatencySamples(lat); }
}

void NeuroforgeProcessor::copyScope(float* dst, int count) const
{
    const uint32_t w = scopeWrite.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        dst[i] = scope[(w - (uint32_t) count + (uint32_t) i) & (kScopeSize - 1)];
}

juce::AudioProcessorEditor* NeuroforgeProcessor::createEditor() { return new NeuroforgeEditor(*this); }

//==============================================================================================
// Parameters / presets
//==============================================================================================
void NeuroforgeProcessor::setParamNorm(int id, float norm)
{
    auto* p = params[(size_t) id];
    p->beginChangeGesture();
    p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, norm));
    p->endChangeGesture();
}

void NeuroforgeProcessor::setParamReal(int id, float real) { setParamNorm(id, realToNorm(id, real)); }

ParamState NeuroforgeProcessor::currentState() const
{
    ParamState s;
    for (int i = 0; i < pid::count; ++i) s[i] = getRaw(i);
    return s;
}

void NeuroforgeProcessor::loadState(const ParamState& s)
{
    for (int i = 0; i < pid::count; ++i) {
        if (std::abs(getRaw(i) - s[i]) < 1e-6f) continue;
        auto* p = params[(size_t) i];
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, s[i]));
        p->endChangeGesture();
    }
    sendChangeMessage();
}

juce::File NeuroforgeProcessor::userPresetDir()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support"); // JUCE returns ~/Library on macOS
   #endif
    return base.getChildFile("Neuroforge").getChildFile("Presets");
}

std::vector<NfPresetEntry> NeuroforgeProcessor::getPresetList() const
{
    std::vector<NfPresetEntry> list;
    const auto& fp = factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i) {
        NfPresetEntry e;
        e.name = fp[(size_t) i].name; e.category = fp[(size_t) i].category; e.factoryIndex = i;
        list.push_back(e);
    }
    auto files = userPresetDir().findChildFiles(juce::File::findFiles, false, "*.nfpreset");
    files.sort();
    for (auto& f : files) {
        NfPresetEntry e;
        e.name = f.getFileNameWithoutExtension(); e.category = "User"; e.user = true; e.file = f;
        list.push_back(e);
    }
    return list;
}

void NeuroforgeProcessor::pushUndo()
{
    undoStack.push_back({ currentState(), presetName });
    if (undoStack.size() > kMaxUndo) undoStack.erase(undoStack.begin());
}

void NeuroforgeProcessor::undo()
{
    if (undoStack.empty()) return;
    auto entry = std::move(undoStack.back());
    undoStack.pop_back();
    loadState(entry.state);
    presetName = entry.name;
    sendChangeMessage();
}

void NeuroforgeProcessor::loadPreset(const NfPresetEntry& e)
{
    pushUndo();
    if (e.user) {
        ParamState s;
        if (!s.fromText(e.file.loadFileAsString().toStdString())) return;
        loadState(s);
    } else {
        const auto& fp = factoryPresets();
        if (e.factoryIndex < 0 || e.factoryIndex >= (int) fp.size()) return;
        loadState(fp[(size_t) e.factoryIndex].state());
    }
    presetName = e.name;
    sendChangeMessage();
}

void NeuroforgeProcessor::initPatch()
{
    pushUndo();
    loadState(ParamState());
    presetName = "Init";
    sendChangeMessage();
}

bool NeuroforgeProcessor::saveUserPreset(const juce::String& name)
{
    auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty()) return false;
    auto dir = userPresetDir();
    if (!dir.createDirectory().wasOk()) return false;
    auto f = dir.getChildFile(clean + ".nfpreset");
    if (!f.replaceWithText(juce::String(currentState().toText()))) return false;
    presetName = clean;
    sendChangeMessage();
    return true;
}

void NeuroforgeProcessor::generate(int archetype, uint32_t seed)
{
    const int n = (int) archetypeNames().size();
    archetype = std::clamp(archetype, 0, n - 1);
    pushUndo();
    loadState(generatePatch((Archetype) archetype, seed));
    presetName = juce::String(archetypeNames()[(size_t) archetype]) + " " + juce::String((int) (seed % 1000u));
    sendChangeMessage();
}

void NeuroforgeProcessor::mutate(float amount)
{
    pushUndo();
    auto s = currentState();
    mutatePatch(s, amount, (uint32_t) juce::Random::getSystemRandom().nextInt());
    loadState(s);
    if (!presetName.endsWith("*")) presetName << "*";
    sendChangeMessage();
}

//==============================================================================================
// Capture
//==============================================================================================
int NeuroforgeProcessor::pickCaptureSlot()
{
    for (int i = 0; i < kNumCaptures; ++i)
        if (!engine.getCapture(i)) return i;
    int s = captureCycle % kNumCaptures;
    ++captureCycle;
    return s;
}

void NeuroforgeProcessor::setStatus(const juce::String& s)
{
    const juce::ScopedLock sl(statusLock);
    status = s;
}

juce::String NeuroforgeProcessor::getCaptureStatus() const
{
    const juce::ScopedLock sl(statusLock);
    return status;
}

void NeuroforgeProcessor::startCapture(bool thenNextGen)
{
    if (capturing.exchange(true)) return;
    if (captureThread.joinable()) captureThread.join();

    const int slot = pickCaptureSlot();
    ParamState state = currentState();
    std::array<std::shared_ptr<const WavetableBank>, kNumCaptures> existing;
    for (int i = 0; i < kNumCaptures; ++i) existing[(size_t) i] = engine.getCapture(i);
    CaptureSettings cs = captureSettings;
    cs.bpm = hostBpm.load();
    setStatus("Rendering...");

    captureThread = std::thread([this, slot, state, existing, cs, thenNextGen] {
        try {
            auto bank = renderCapture(state, existing, cs, "Capture " + std::to_string(slot + 1));
            if (bank) {
                engine.setCapture(slot, bank);
                captureGeneration.fetch_add(1);
                setStatus("Captured to slot " + juce::String(slot + 1));
                if (thenNextGen) pendingNextGen.store(slot);
            } else setStatus("Capture failed");
        } catch (...) { setStatus("Capture failed"); }
        capturing.store(false);
        triggerAsyncUpdate();
    });
}

void NeuroforgeProcessor::handleAsyncUpdate()
{
    const int slot = pendingNextGen.exchange(-1);
    if (slot >= 0) {
        pushUndo();
        loadState(nextGenerationState(currentState(), slot));
        presetName = "Gen from slot " + juce::String(slot + 1);
    }
    sendChangeMessage();
}

//==============================================================================================
// Mod routing
//==============================================================================================
int NeuroforgeProcessor::dstIndexOf(int paramId)
{
    const auto& d = modDestinations();
    for (int i = 1; i < (int) d.size(); ++i) if (d[(size_t) i] == paramId) return i;
    return -1;
}

std::vector<NfModRoute> NeuroforgeProcessor::getRoutesFor(int paramId) const
{
    std::vector<NfModRoute> r;
    const int di = dstIndexOf(paramId);
    if (di < 0) return r;
    for (int x = 0; x < pid::kNumMatrix; ++x) {
        const int src = (int) normToReal(pid::mx(x, pid::mxSrc), getRaw(pid::mx(x, pid::mxSrc)));
        const int dst = (int) normToReal(pid::mx(x, pid::mxDst), getRaw(pid::mx(x, pid::mxDst)));
        if (src > 0 && dst == di) r.push_back({ x, src, normToReal(pid::mx(x, pid::mxAmt), getRaw(pid::mx(x, pid::mxAmt))) });
    }
    return r;
}

int NeuroforgeProcessor::countRoutesFor(int paramId) const
{
    const int di = dstIndexOf(paramId);
    if (di < 0) return 0;
    int c = 0;
    for (int x = 0; x < pid::kNumMatrix; ++x) {
        const int src = (int) normToReal(pid::mx(x, pid::mxSrc), getRaw(pid::mx(x, pid::mxSrc)));
        const int dst = (int) normToReal(pid::mx(x, pid::mxDst), getRaw(pid::mx(x, pid::mxDst)));
        if (src > 0 && dst == di) ++c;
    }
    return c;
}

bool NeuroforgeProcessor::addRoute(int src, int paramId, float amount)
{
    const int di = dstIndexOf(paramId);
    if (di < 0 || src <= 0) return false;
    int free = -1;
    for (int x = 0; x < pid::kNumMatrix; ++x) {
        const int s = (int) normToReal(pid::mx(x, pid::mxSrc), getRaw(pid::mx(x, pid::mxSrc)));
        const int d = (int) normToReal(pid::mx(x, pid::mxDst), getRaw(pid::mx(x, pid::mxDst)));
        if (s == src && d == di) { setParamReal(pid::mx(x, pid::mxAmt), amount); return true; }
        if (free < 0 && s == 0) free = x;
    }
    if (free < 0) return false;
    setParamReal(pid::mx(free, pid::mxSrc), (float) src);
    setParamReal(pid::mx(free, pid::mxDst), (float) di);
    setParamReal(pid::mx(free, pid::mxAmt), amount);
    return true;
}

void NeuroforgeProcessor::removeRoute(int row)
{
    setParamReal(pid::mx(row, pid::mxSrc), 0.f);
    setParamReal(pid::mx(row, pid::mxDst), 0.f);
    setParamReal(pid::mx(row, pid::mxAmt), 0.f);
}

void NeuroforgeProcessor::setRouteAmount(int row, float amount) { setParamReal(pid::mx(row, pid::mxAmt), amount); }

//==============================================================================================
// State
//==============================================================================================
void NeuroforgeProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    // Write every parameter's live value ourselves: some wrappers (e.g. CLAP) change parameters
    // without notifying APVTS listeners, which would leave a stale copy in apvts.state.
    juce::ValueTree state(apvts.state.getType());
    state.setProperty("nfVersion", kStateVersion, nullptr);
    const auto& defs = paramDefs();
    for (int i = 0; i < pid::count; ++i) {
        juce::ValueTree pt("PARAM");
        pt.setProperty("id", juce::String(defs[(size_t) i].id), nullptr);
        pt.setProperty("value", params[(size_t) i]->getValue(), nullptr);
        state.appendChild(pt, nullptr);
    }
    juce::ValueTree nfTree("NF");
    nfTree.setProperty("preset", presetName, nullptr);
    nfTree.setProperty("capNote", captureSettings.note, nullptr);
    nfTree.setProperty("capBeats", captureSettings.beats, nullptr);
    nfTree.setProperty("capFrames", captureSettings.maxFrames, nullptr);
    nfTree.setProperty("editorWidth", editorWidth.load(), nullptr);
    nfTree.setProperty("editorHeight", editorHeight.load(), nullptr);
    nfTree.setProperty("archetype", selectedArchetype.load(), nullptr);
    for (int i = 0; i < kNumCaptures; ++i) {
        auto bank = engine.getCapture(i);
        if (!bank) continue;
        auto frames = bankToFrames(*bank);
        juce::MemoryBlock mb(frames.data(), frames.size() * sizeof(float));
        juce::ValueTree c("Cap");
        c.setProperty("slot", i, nullptr);
        c.setProperty("name", juce::String(bank->getName()), nullptr);
        c.setProperty("data", mb.toBase64Encoding(), nullptr);
        nfTree.appendChild(c, nullptr);
    }
    state.appendChild(nfTree, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void NeuroforgeProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid()) return;
    auto nfTree = tree.getChildWithName("NF");
    if (nfTree.isValid()) tree.removeChild(nfTree, nullptr);
    apvts.replaceState(tree);
    // ...and set each parameter explicitly, in case the APVTS cache thought it was unchanged.
    const int version = tree.getProperty("nfVersion", 1); // projects saved before versioning are v1
    for (auto pt : tree) {
        if (!pt.hasType("PARAM")) continue;
        const int i = findParam(pt.getProperty("id").toString().toStdString());
        if (i < 0) continue;
        float v = juce::jlimit(0.f, 1.f, (float) pt.getProperty("value", params[(size_t) i]->getValue()));
        if (version < 2 && isMatrixSourceParam(i)) v = remapModSrcFromV1(v); // v2 appended Step 1/2 sources
        if (params[(size_t) i]->getValue() != v) params[(size_t) i]->setValueNotifyingHost(v);
    }
    stateWasRestored = true;

    if (nfTree.isValid()) {
        presetName = nfTree.getProperty("preset", "Init").toString();
        editorWidth.store(juce::jlimit(640, 2560, (int) nfTree.getProperty("editorWidth", 1280)));
        editorHeight.store(juce::jlimit(400, 1600, (int) nfTree.getProperty("editorHeight", 800)));
        selectedArchetype.store(juce::jlimit(0, (int) archetypeNames().size() - 1,
                                             (int) nfTree.getProperty("archetype", 0)));
        captureSettings.note = nfTree.getProperty("capNote", captureSettings.note);
        captureSettings.beats = nfTree.getProperty("capBeats", captureSettings.beats);
        captureSettings.maxFrames = nfTree.getProperty("capFrames", captureSettings.maxFrames);
        for (auto c : nfTree) {
            if (!c.hasType("Cap")) continue;
            const int slot = c.getProperty("slot", -1);
            if (slot < 0 || slot >= kNumCaptures) continue;
            juce::MemoryBlock mb;
            if (!mb.fromBase64Encoding(c.getProperty("data").toString())) continue;
            std::vector<float> frames(mb.getSize() / sizeof(float));
            std::memcpy(frames.data(), mb.getData(), frames.size() * sizeof(float));
            if (auto bank = bankFromFrames(frames, c.getProperty("name").toString().toStdString()))
                engine.setCapture(slot, bank);
        }
        captureGeneration.fetch_add(1);
    }
    // Tell the host every parameter may have new values (CLAP: params rescan; VST3: values changed).
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    sendChangeMessage();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeuroforgeProcessor(); }
