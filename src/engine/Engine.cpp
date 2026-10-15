#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nf {

namespace {
constexpr float kRatios[] = { 0.25f, 0.5f, 1, 1.5f, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16 };
inline float dbToGain(float db) { return std::pow(10.f, db * 0.05f); }
inline double noteToHz(double semis) { return 440.0 * std::pow(2.0, (semis - 69.0) / 12.0); }
} // namespace

// ---------------------------------------------------------------------------------------------
void Voice::prepare(double sr)
{
    amp.prepare(sr); env2.prepare(sr); env3.prepare(sr);
    oscA.prepare(sr); oscB.prepare(sr); op.prepare(sr); sub.prepare(sr);
    filter.reset();
}

// ---------------------------------------------------------------------------------------------
Engine::Engine()
{
    for (auto& s : slots)
        for (int t = 0; t < (int) FxType::NumTypes; ++t)
            s.modules[(size_t) t] = createFxModule((FxType) t);
}

void Engine::prepare(double hostSampleRate, int maxHostBlock)
{
    hostRate = hostSampleRate;
    maxHost = std::max(16, maxHostBlock);
    const size_t n = (size_t) maxHost * 4 + 64;
    for (auto* b : { &bufL, &bufR, &bufSub, &tmpL, &tmpR, &voiceL, &voiceR, &voiceSub, &opBuf, &bBuf, &lowL, &lowR,
                     &dryL, &dryR })
        b->assign(n, 0.f);
    configureOversampling(osFactor);
}

void Engine::configureOversampling(int os)
{
    osFactor = os;
    rate = hostRate * os;
    for (auto& v : voices) v.prepare(rate);
    for (auto& s : slots)
        for (auto& m : s.modules) if (m) m->prepare(rate, kMaxSubBlock);
    guardSplit.prepare(rate);
    midClean.prepare(rate);
    decL.prepare(os, hostRate);
    decR.prepare(os, hostRate);
    latency = decL.latency();
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) { v.amp.kill(); v.env2.kill(); v.env3.kill(); v.note = -1; v.gate = false; v.filter.reset(); }
    for (auto& s : slots) for (auto& m : s.modules) if (m) m->reset();
    guardSplit.reset(); midClean.reset(); subLp.reset();
    for (int i = 0; i < pid::kNumLfos; ++i) lfo[i].reset(0.f, seed + (uint32_t) i * 7919u);
    for (int i = 0; i < pid::kNumPasses; ++i) motion[i].reset(seed + 101u + (uint32_t) i);
    for (int i = 0; i < pid::kNumSteppers; ++i) { stepperOsc[i].reset(0.f, seed + 211u + (uint32_t) i); stepperVal[i] = 0.f; }
    noteStack.clear();
    newest = nullptr;
}

void Engine::setCapture(int slot, std::shared_ptr<const WavetableBank> bank)
{
    if (slot >= 0 && slot < kNumCaptures) captures[(size_t) slot].store(std::move(bank));
}

std::shared_ptr<const WavetableBank> Engine::getCapture(int slot) const
{
    return (slot >= 0 && slot < kNumCaptures) ? captures[(size_t) slot].load() : nullptr;
}

std::shared_ptr<const WavetableBank> Engine::bankFor(int idx) const
{
    const int builtIn = tables::getNumBuiltIn();
    if (idx < builtIn) return tables::getBuiltIn(std::max(0, idx));
    auto cap = getCapture(idx - builtIn);
    return cap ? cap : tables::getBuiltIn(0);
}

float Engine::val(const float* p, int id) const
{
    return normToReal(id, std::clamp(p[id] + globalOff[id], 0.f, 1.f));
}

float Engine::vval(const float* p, const Voice& v, int id) const
{
    return normToReal(id, std::clamp(p[id] + v.modOff[id], 0.f, 1.f));
}

// ---------------------------------------------------------------------------------------------
void Engine::render(float* outL, float* outR, int n, const float* p, const MidiEvent* events, int numEvents,
                    const Transport& t)
{
    // split oversized host blocks
    if (n > maxHost) {
        int done = 0;
        Transport tt = t;
        while (done < n) {
            int chunk = std::min(maxHost, n - done);
            // events belonging to this chunk
            std::vector<MidiEvent> ev; // rare path (offline renders only); allocation acceptable
            for (int i = 0; i < numEvents; ++i)
                if (events[i].offset >= done && events[i].offset < done + chunk) {
                    auto e = events[i]; e.offset -= done; ev.push_back(e);
                }
            render(outL + done, outR + done, chunk, p, ev.data(), (int) ev.size(), tt);
            tt.ppq += chunk / hostRate * t.bpm / 60.0;
            done += chunk;
        }
        return;
    }

    const int os = (int) normToReal(pid::quality, p[pid::quality]) == 0 ? 1
                 : (int) normToReal(pid::quality, p[pid::quality]) == 1 ? 2 : 4;
    if (os != osFactor) configureOversampling(os);

    const int N = n * osFactor;
    std::fill_n(bufL.data(), N, 0.f);
    std::fill_n(bufR.data(), N, 0.f);
    std::fill_n(bufSub.data(), N, 0.f);

    int pos = 0, ei = 0;
    while (pos < N) {
        while (ei < numEvents && events[ei].offset * osFactor <= pos) handleMidi(events[ei++], p);
        int end = std::min(pos + kSubBlock, N);
        if (ei < numEvents) end = std::min(end, std::max(pos + 1, events[ei].offset * osFactor));
        renderSub(pos, end - pos, p, t, t.ppq);
        pos = end;
    }
    while (ei < numEvents) handleMidi(events[ei++], p);

    decL.process(bufL.data(), n);
    decR.process(bufR.data(), n);
    std::memcpy(outL, bufL.data(), sizeof(float) * (size_t) n);
    std::memcpy(outR, bufR.data(), sizeof(float) * (size_t) n);
    // Decimation can overshoot the oversampled clipper by ~1 dB; catch it with a soft knee
    // that only touches samples above -0.9 dBFS (and only while the master clipper is engaged).
    if (normToReal(pid::masterClip, p[pid::masterClip]) > 0.001f)
        for (float* ch : { outL, outR })
            for (int i = 0; i < n; ++i) {
                const float a = std::abs(ch[i]);
                if (a > 0.9f) ch[i] = std::copysign(0.9f + 0.09f * std::tanh((a - 0.9f) / 0.09f), ch[i]);
            }

    for (int i = 0; i < pid::kNumLfos; ++i) {
        uiLfo[i].store(lfoVal[i], std::memory_order_relaxed);
        uiLfoPhase[i].store((float) lfo[i].getPhase(), std::memory_order_relaxed);
    }
    for (int i = 0; i < pid::kNumPasses; ++i) uiMotionPhase[i].store((float) motion[i].osc.getPhase(), std::memory_order_relaxed);
    for (int i = 0; i < pid::kNumSteppers; ++i) {
        uiStepper[i].store(stepperVal[i], std::memory_order_relaxed);
        uiStepperPhase[i].store((float) stepperOsc[i].getPhase(), std::memory_order_relaxed);
    }
    for (int i = 0; i < pid::kNumPasses; ++i) uiMotion[i].store(motionVal[i], std::memory_order_relaxed);
    if (newest) {
        uiEnv[0].store(newest->amp.get(), std::memory_order_relaxed);
        uiEnv[1].store(newest->env2.get(), std::memory_order_relaxed);
        uiEnv[2].store(newest->env3.get(), std::memory_order_relaxed);
    }
    for (int i = 0; i < pid::count; ++i) {
        float o = globalOff[i];
        if (newest && paramDefs()[(size_t) i].voice) o = newest->modOff[i];
        uiOffsets[i].store(o, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------------------------
void Engine::handleMidi(const MidiEvent& e, const float* p)
{
    const int type = e.status & 0xF0;
    if (type == 0x90 && e.d2 > 0) noteOn(e.d1, e.d2 / 127.f, p);
    else if (type == 0x80 || (type == 0x90 && e.d2 == 0)) noteOff(e.d1, p);
    else if (type == 0xB0 && e.d1 == 1) modWheel = e.d2 / 127.f;
    else if (type == 0xB0 && (e.d1 == 120 || e.d1 == 123)) { for (auto& v : voices) { v.gate = false; v.amp.gateOff(); v.env2.gateOff(); v.env3.gateOff(); } noteStack.clear(); }
    else if (type == 0xE0) pitchBend = (((e.d2 << 7) | e.d1) - 8192) / 8192.f;
    else if (type == 0xD0) aftertouch = e.d1 / 127.f;
}

Voice* Engine::allocVoice()
{
    for (auto& v : voices) if (!v.active()) return &v;
    Voice* oldest = &voices[0];
    for (auto& v : voices) if (v.age < oldest->age) oldest = &v;
    return oldest;
}


void Engine::noteOn(int note, float vel, const float* p)
{
    const int mode = (int) normToReal(pid::voiceMode, p[pid::voiceMode]); // 0 poly 1 mono 2 legato
    Voice* v;
    bool legatoMove = false;
    if (mode == 0) {
        v = allocVoice();
        v->curSemis = v->targetSemis = note;
    } else {
        v = &voices[0];
        for (size_t i = 1; i < voices.size(); ++i) if (voices[i].active()) { voices[i].amp.gateOff(); voices[i].gate = false; }
        noteStack.erase(std::remove(noteStack.begin(), noteStack.end(), note), noteStack.end());
        noteStack.push_back(note);
        legatoMove = mode == 2 && v->gate && v->active();
        const bool glideFrom = v->active() && normToReal(pid::glide, p[pid::glide]) > 0.5f;
        v->targetSemis = note;
        if (!glideFrom) v->curSemis = note;
    }
    v->note = note;
    v->gate = true;
    v->age = ++ageCounter;
    newest = v;
    if (legatoMove) return;

    v->velocity = vel;
    Rng r(seed ^ (uint32_t) (ageCounter * 2654435761u));
    v->random = r.bi();
    v->noiseRng = Rng(r.next());
    std::fill(std::begin(v->modOff), std::end(v->modOff), 0.f);

    auto envSet = [&](Envelope& e, int i) {
        e.set(val(p, pid::env(i, pid::eA)), val(p, pid::env(i, pid::eD)), val(p, pid::env(i, pid::eS)),
              val(p, pid::env(i, pid::eR)), val(p, pid::env(i, pid::eLoop)) > 0.5f);
        e.gateOn(false);
    };
    envSet(v->amp, 0); envSet(v->env2, 1); envSet(v->env3, 2);

    for (int o = 0; o < 2; ++o) {
        OscParams op;
        op.phase = val(p, pid::osc(o, pid::oPhase));
        op.phaseRand = val(p, pid::osc(o, pid::oPhaseRand));
        op.unison = (int) val(p, pid::osc(o, pid::oUni));
        auto& osc = o == 0 ? v->oscA : v->oscB;
        osc.noteOn(val(p, pid::osc(o, pid::oRetrig)) > 0.5f, r.next(), op);
    }
    v->op.noteOn(true);
    v->sub.noteOn(true);

    // retrigger LFOs / motion when not host-locked
    for (int i = 0; i < pid::kNumLfos; ++i)
        if (val(p, pid::lfo(i, pid::lRetrig)) > 0.5f) lfo[i].reset(val(p, pid::lfo(i, pid::lPhase)), r.next());
    for (int i = 0; i < pid::kNumPasses; ++i) motion[i].reset(seed + 101u + (uint32_t) i);
    for (int i = 0; i < pid::kNumSteppers; ++i)
        if (val(p, pid::stepper(i, pid::stRetrig)) > 0.5f) stepperOsc[i].reset(0.f, seed + 211u + (uint32_t) i);
}

void Engine::noteOff(int note, const float* p)
{
    const int mode = (int) normToReal(pid::voiceMode, p[pid::voiceMode]);
    if (mode == 0) {
        for (auto& v : voices)
            if (v.gate && v.note == note) { v.gate = false; v.amp.gateOff(); v.env2.gateOff(); v.env3.gateOff(); }
        return;
    }
    noteStack.erase(std::remove(noteStack.begin(), noteStack.end(), note), noteStack.end());
    Voice& v = voices[0];
    if (!noteStack.empty()) {
        if (v.note == note) { v.note = noteStack.back(); v.targetSemis = v.note; }
        return;
    }
    if (v.note == note) { v.gate = false; v.amp.gateOff(); v.env2.gateOff(); v.env3.gateOff(); }
}

// ---------------------------------------------------------------------------------------------
float Engine::sourceValue(ModSrc s, const Voice* v) const
{
    switch (s) {
        case ModSrc::Env1: return v ? v->amp.get() : 0.f;
        case ModSrc::Env2: return v ? v->env2.get() : 0.f;
        case ModSrc::Env3: return v ? v->env3.get() : 0.f;
        case ModSrc::Lfo1: case ModSrc::Lfo2: case ModSrc::Lfo3: case ModSrc::Lfo4:
            return lfoVal[(int) s - (int) ModSrc::Lfo1];
        case ModSrc::Motion1: case ModSrc::Motion2: case ModSrc::Motion3:
            return motionVal[(int) s - (int) ModSrc::Motion1];
        case ModSrc::Velocity: return v ? v->velocity : 0.f;
        case ModSrc::Note: return v ? (float) (v->curSemis - 60.0) / 60.f : 0.f;
        case ModSrc::ModWheel: return modWheel;
        case ModSrc::PitchBend: return pitchBend;
        case ModSrc::Aftertouch: return aftertouch;
        case ModSrc::Random: return v ? v->random : 0.f;
        case ModSrc::Step1: case ModSrc::Step2: return stepperVal[(int) s - (int) ModSrc::Step1];
        default: return 0.f; // macros handled by caller
    }
}

void Engine::computeGlobalSources(const float* p, double seconds, const Transport& t, double ppqEnd)
{
    for (int i = 0; i < pid::kNumLfos; ++i) {
        const bool sync = normToReal(pid::lfo(i, pid::lSync), p[pid::lfo(i, pid::lSync)]) > 0.5f;
        const int div = (int) normToReal(pid::lfo(i, pid::lDiv), p[pid::lfo(i, pid::lDiv)]);
        const double hz = modRateHz(sync, val(p, pid::lfo(i, pid::lRate)), div, t.bpm);
        const bool retrig = normToReal(pid::lfo(i, pid::lRetrig), p[pid::lfo(i, pid::lRetrig)]) > 0.5f;
        const bool locked = sync && t.playing && !retrig;
        lfo[i].advance(seconds, hz, locked, ppqEnd, rateDivBeats(div), val(p, pid::lfo(i, pid::lPhase)));
        lfoVal[i] = lfo[i].shape((LfoShape) (int) normToReal(pid::lfo(i, pid::lShape), p[pid::lfo(i, pid::lShape)]));
    }
    for (int m = 0; m < pid::kNumPasses; ++m) {
        auto P = [&](int off) { return pid::motion(m, off); };
        const bool sync = normToReal(P(pid::mSync), p[P(pid::mSync)]) > 0.5f;
        const int div = (int) normToReal(P(pid::mDiv), p[P(pid::mDiv)]);
        const double hz = modRateHz(sync, val(p, P(pid::mRate)), div, t.bpm);
        motion[m].osc.advance(seconds, hz, sync && t.playing, ppqEnd, rateDivBeats(div), 0.f);
        float steps[16];
        for (int s = 0; s < 16; ++s) steps[s] = std::clamp(p[P(pid::mStep0 + s)] + globalOff[P(pid::mStep0 + s)], 0.f, 1.f);
        const bool stepsMode = normToReal(P(pid::mMode), p[P(pid::mMode)]) > 0.5f;
        motionVal[m] = motion[m].update(steps, (int) normToReal(P(pid::mSteps), p[P(pid::mSteps)]), stepsMode,
                                        (LfoShape) (int) normToReal(P(pid::mShape), p[P(pid::mShape)]),
                                        val(p, P(pid::mGlide)), seconds, hz);
    }
    for (int st = 0; st < pid::kNumSteppers; ++st) {
        auto S = [&](int off) { return pid::stepper(st, off); };
        const bool sync = normToReal(S(pid::stSync), p[S(pid::stSync)]) > 0.5f;
        const int div = (int) normToReal(S(pid::stDiv), p[S(pid::stDiv)]);
        const int steps = std::clamp((int) normToReal(S(pid::stSteps), p[S(pid::stSteps)]), 1, pid::kStepperSteps);
        const double stepHz = sync ? (t.bpm / 60.0) / rateDivBeats(div) : (double) val(p, S(pid::stRate));
        const bool retrig = normToReal(S(pid::stRetrig), p[S(pid::stRetrig)]) > 0.5f;
        // one oscillator cycle = the whole sequence; host-locked while the transport plays
        stepperOsc[st].advance(seconds, stepHz / steps, sync && t.playing && !retrig, ppqEnd, rateDivBeats(div) * steps, 0.f);
        const double ph = stepperOsc[st].getPhase() * steps;
        const int k = std::min(steps - 1, (int) ph);
        const int shape = (int) normToReal(S(pid::stShape0 + k), p[S(pid::stShape0 + k)]);
        const float target = stepCurve(shape, (float) (ph - k)) * p[S(pid::stAmt0 + k)];
        const float smooth = val(p, S(pid::stSmooth));
        if (smooth < 0.001f) stepperVal[st] = target;
        else {
            const double tau = std::max(1e-4, (double) smooth * 0.3 / std::max(0.01, stepHz));
            stepperVal[st] += (target - stepperVal[st]) * (float) (1.0 - std::exp(-seconds / tau));
        }
    }
}

void Engine::applyMatrix(const float* p, const Voice* nv)
{
    std::fill(std::begin(globalOff), std::end(globalOff), 0.f);
    for (auto& v : voices) if (v.active()) std::fill(std::begin(v.modOff), std::end(v.modOff), 0.f);

    const auto& dsts = modDestinations();
    const auto& defs = paramDefs();
    for (int x = 0; x < pid::kNumMatrix; ++x) {
        const auto src = (ModSrc) (int) normToReal(pid::mx(x, pid::mxSrc), p[pid::mx(x, pid::mxSrc)]);
        const int dc = (int) normToReal(pid::mx(x, pid::mxDst), p[pid::mx(x, pid::mxDst)]);
        const float amt = normToReal(pid::mx(x, pid::mxAmt), p[pid::mx(x, pid::mxAmt)]);
        if (src == ModSrc::None || dc <= 0 || dc >= (int) dsts.size() || amt == 0.f) continue;
        const int dst = dsts[(size_t) dc];
        const bool isMacro = src >= ModSrc::Macro1 && src <= ModSrc::Macro8;
        const float macroVal = isMacro ? p[pid::macro((int) src - (int) ModSrc::Macro1)] : 0.f;

        if (defs[(size_t) dst].voice) {
            for (auto& v : voices)
                if (v.active()) v.modOff[dst] += amt * (isMacro ? macroVal : sourceValue(src, &v));
        } else {
            globalOff[dst] += amt * (isMacro ? macroVal : sourceValue(src, nv));
        }
    }
}

// ---------------------------------------------------------------------------------------------
void Engine::renderVoice(Voice& v, const float* p, int start, int len)
{
    using namespace pid;
    const int mode = (int) normToReal(voiceMode, p[voiceMode]);
    // glide
    const float glideMs = val(p, glide);
    if (mode != 0 && glideMs > 0.5f) {
        const double a = 1.0 - std::exp(-len / (glideMs * 0.001 * rate * 0.4));
        v.curSemis += (v.targetSemis - v.curSemis) * a;
    } else {
        v.curSemis = v.targetSemis;
    }
    const float penvSrc = vval(p, v, pitchEnvSrc) < 0.5f ? v.env2.get() : v.env3.get();
    const double semis = v.curSemis + pitchBend * val(p, bendRange) + vval(p, v, pitchEnv) * penvSrc;
    const double hz = noteToHz(semis);
    v.baseHz = (float) hz;
    if (&v == newest) lastNoteHz = (float) hz;

    float* L = voiceL.data(); float* R = voiceR.data(); float* S = voiceSub.data();
    std::fill_n(L, len, 0.f); std::fill_n(R, len, 0.f); std::fill_n(S, len, 0.f);

    // FM operator
    const bool opOnFlag = vval(p, v, op + opOn) > 0.5f;
    if (opOnFlag) {
        FmOpParams fp;
        fp.ratio = kRatios[std::clamp((int) vval(p, v, op + opCoarse), 0, 15)] * std::pow(2.f, vval(p, v, op + opFine) / 1200.f);
        fp.feedback = vval(p, v, op + opFb);
        fp.shape = vval(p, v, op + opShape);
        v.op.process(opBuf.data(), len, hz, fp);
    }

    auto oscParams = [&](int o) {
        OscParams q;
        q.pitchSemis = vval(p, v, osc(o, oOct)) * 12.f + vval(p, v, osc(o, oSemi)) + vval(p, v, osc(o, oFine)) * 0.01f;
        q.level = vval(p, v, osc(o, oLevel));
        q.pan = vval(p, v, osc(o, oPan));
        q.position = vval(p, v, osc(o, oPos));
        q.unison = (int) vval(p, v, osc(o, oUni));
        q.detune = vval(p, v, osc(o, oDetune));
        q.stereo = vval(p, v, osc(o, oStereo));
        q.blend = vval(p, v, osc(o, oBlend));
        q.phase = vval(p, v, osc(o, oPhase));
        q.phaseRand = vval(p, v, osc(o, oPhaseRand));
        q.warp = (WarpMode) (int) vval(p, v, osc(o, oWarp));
        q.warpAmount = vval(p, v, osc(o, oWarpAmt));
        q.fmAmount = opOnFlag ? vval(p, v, osc(o, oFm)) : 0.f;
        return q;
    };
    auto syncBank = [&](int o) {
        const int idx = (int) vval(p, v, osc(o, oTable));
        auto bank = bankFor(idx);
        auto& cur = o == 0 ? v.bankA : v.bankB;
        if (bank != cur) { cur = bank; (o == 0 ? v.oscA : v.oscB).setBank(bank); }
    };

    const bool bOn = vval(p, v, oscB + oOn) > 0.5f;
    if (bOn) {
        syncBank(1);
        v.oscB.process(L, R, len, hz, oscParams(1), opOnFlag ? opBuf.data() : nullptr, nullptr, bBuf.data());
    }
    if (vval(p, v, oscA + oOn) > 0.5f) {
        syncBank(0);
        v.oscA.process(L, R, len, hz, oscParams(0), opOnFlag ? opBuf.data() : nullptr, bOn ? bBuf.data() : nullptr, nullptr);
    }

    // noise
    const float nl = vval(p, v, noise + nLevel);
    if (nl > 0.f) {
        const float col = vval(p, v, noise + nColor);
        const float a = 0.05f + 0.9f * (1.f - std::abs(col));
        for (int i = 0; i < len; ++i) {
            float w = v.noiseRng.bi();
            v.noiseLp += (w - v.noiseLp) * a;
            float s = col < 0 ? v.noiseLp : (col > 0 ? w - v.noiseLp * (1.f - a) : w);
            L[i] += s * nl; R[i] += s * nl;
        }
    }

    // voice filter
    if (vval(p, v, vf + vfOn) > 0.5f) {
        float cutoff = vval(p, v, vf + vfCutoff);
        cutoff *= std::pow(2.f, vval(p, v, vf + vfKey) * (float) (semis - 60.0) / 12.f);
        cutoff *= std::pow(2.f, vval(p, v, vf + vfEnv) * v.env2.get() * 7.f);
        v.filter.setParams(std::clamp(cutoff, 20.f, 22000.f), vval(p, v, vf + vfReso), rate);
        const float driveDb = vval(p, v, vf + vfDrive);
        const float dg = std::pow(10.f, driveDb / 20.f);
        const float fmode = vval(p, v, vf + vfMode);
        const float k = v.filter.k();
        for (int i = 0; i < len; ++i) {
            float xl = L[i], xr = R[i];
            if (driveDb > 0.01f) { xl = std::tanh(xl * dg) / std::sqrt(dg); xr = std::tanh(xr * dg) / std::sqrt(dg); }
            L[i] = Svf::morph(v.filter.tick(0, xl), k, fmode);
            R[i] = Svf::morph(v.filter.tick(1, xr), k, fmode);
        }
    }

    // sub osc (pre-amp)
    if (vval(p, v, sub + subOn) > 0.5f) {
        SubParams sp;
        sp.octave = vval(p, v, sub + subOct);
        sp.shape = vval(p, v, sub + subShape);
        sp.level = vval(p, v, sub + subLevelP);
        v.sub.process(S, len, hz, sp);
    }

    // envelopes + amp
    const float velAmt = val(p, ampVel);
    const float velGain = 1.f - velAmt * (1.f - v.velocity);
    const bool direct = vval(p, v, sub + subDirect) > 0.5f;
    float* oL = bufL.data() + start; float* oR = bufR.data() + start; float* oS = bufSub.data() + start;
    for (int i = 0; i < len; ++i) {
        const float g = v.amp.next() * velGain;
        v.env2.next(); v.env3.next();
        oL[i] += L[i] * g;
        oR[i] += R[i] * g;
        const float s = S[i] * g;
        if (direct) oS[i] += s; else { oL[i] += s; oR[i] += s; }
    }
    if (!v.amp.isActive()) { v.note = -1; v.gate = false; if (newest == &v) newest = nullptr; }
}

// ---------------------------------------------------------------------------------------------
void Engine::renderSub(int start, int len, const float* p, const Transport& t, double ppqAtStart)
{
    using namespace pid;
    const double seconds = len / rate;
    const double ppqEnd = ppqAtStart + (start + len) / rate * t.bpm / 60.0;
    computeGlobalSources(p, seconds, t, ppqEnd);
    applyMatrix(p, newest);

    for (auto& v : voices) if (v.active()) renderVoice(v, p, start, len);

    float* L = bufL.data() + start; float* R = bufR.data() + start; float* S = bufSub.data() + start;

    // ---- Low Guard: split voice sum; lows join the mono sub bus ----
    const bool guard = val(p, guardOn) > 0.5f;
    const float gf = val(p, guardFreq);
    if (guard) {
        guardSplit.setFrequency(gf);
        for (int i = 0; i < len; ++i) {
            float ll, lr, hl, hr;
            guardSplit.tick(L[i], R[i], ll, lr, hl, hr);
            S[i] += 0.5f * (ll + lr);
            L[i] = hl; R[i] = hr;
        }
    }

    // ---- Sub bus: clean or warm, always mono ----
    const float drive = val(p, subDrive);
    const float subGain = dbToGain(val(p, subLevel));
    if (drive > 0.001f) {
        const float g = 1.f + drive * 6.f;
        subLp.setFreqQ(std::max(gf * 2.5f, 150.f), 0.707f, rate);
        for (int i = 0; i < len; ++i) {
            const float sat = std::tanh(S[i] * g) / g * (1.f + drive * 0.6f);
            const float wet = Svf::morph(subLp.tick(0, sat), subLp.k(), 0.f);
            S[i] = wet;
        }
    }

    // DC blocker (~3 Hz): warps like Sync/Bend can produce per-cycle DC that would land in the sub
    {
        const float a = 1.f - (float) (2.0 * M_PI * 3.0 / rate);
        for (int i = 0; i < len; ++i) { const float y = S[i] - subDcX + a * subDcY; subDcX = S[i]; subDcY = y; S[i] = y; }
    }

    // ---- The Forge: 3 passes x 4 slots ----
    FxContext ctx;
    ctx.sampleRate = rate;
    ctx.noteHz = lastNoteHz;
    ctx.bpm = t.bpm;
    for (int ps = 0; ps < kNumPasses; ++ps) {
        if (val(p, pass(ps, pOn)) < 0.5f) continue;
        const float pmix = val(p, pass(ps, pMix));
        std::memcpy(dryL.data(), L, sizeof(float) * (size_t) len);
        std::memcpy(dryR.data(), R, sizeof(float) * (size_t) len);
        const float depth = val(p, pid::motion(ps, mDepth));
        for (int k = 0; k < kSlotsPerPass; ++k) {
            const int s = ps * kSlotsPerPass + k;
            const int type = (int) normToReal(slot(s, sType), p[slot(s, sType)]);
            if (type <= 0 || type >= (int) FxType::NumTypes) continue;
            auto& sl = slots[(size_t) s];
            auto* m = sl.modules[(size_t) type].get();
            if (!m) continue;
            if (sl.type != type) { m->reset(); sl.type = type; }
            const auto& info = getFxTypeInfo((FxType) type);
            float real[kFxParams];
            for (int q = 0; q < kFxParams; ++q) {
                float nrm = p[slot(s, sP1 + q)] + globalOff[slot(s, sP1 + q)];
                if (q == 0) nrm += motionVal[ps] * depth * val(p, slot(s, sMotion)) * 0.5f;
                real[q] = info.params[(size_t) q].name ? fxNormToReal(info.params[(size_t) q], nrm) : 0.f;
            }
            const float mix = val(p, slot(s, sMix));
            std::memcpy(tmpL.data(), L, sizeof(float) * (size_t) len);
            std::memcpy(tmpR.data(), R, sizeof(float) * (size_t) len);
            m->process(tmpL.data(), tmpR.data(), len, real, ctx);
            for (int i = 0; i < len; ++i) {
                L[i] += (tmpL[(size_t) i] - L[i]) * mix;
                R[i] += (tmpR[(size_t) i] - R[i]) * mix;
            }
        }
        if (pmix < 0.999f)
            for (int i = 0; i < len; ++i) {
                L[i] = dryL[(size_t) i] + (L[i] - dryL[(size_t) i]) * pmix;
                R[i] = dryR[(size_t) i] + (R[i] - dryR[(size_t) i]) * pmix;
            }
    }

    // ---- Mid bus: re-clean lows regenerated by distortion, width, level ----
    const float midGain = dbToGain(val(p, midLevel));
    const float width = val(p, midWidth);
    if (guard) midClean.setFrequency(gf);
    for (int i = 0; i < len; ++i) {
        float l = L[i], r = R[i];
        if (guard) { float a, b, hl, hr; midClean.tick(l, r, a, b, hl, hr); l = hl; r = hr; }
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
        L[i] = (mid + side) * midGain;
        R[i] = (mid - side) * midGain;
    }

    // ---- Master: sum, clip, gain ----
    const float clip = val(p, masterClip);
    const float gain = dbToGain(val(p, masterGain));
    const float subGainEff = excludeSub ? 0.f : subGain;
    for (int i = 0; i < len; ++i) {
        float l = (L[i] + S[i] * subGainEff) * gain, r = (R[i] + S[i] * subGainEff) * gain;
        if (clip > 0.001f) { // soft ceiling at -1 dBFS (headroom for decimation overshoot), crossfaded by the Clip amount
            const float cl = 0.89f * std::tanh(l / 0.89f), cr = 0.89f * std::tanh(r / 0.89f);
            l += (cl - l) * clip; r += (cr - r) * clip;
        }
        if (!std::isfinite(l)) l = 0.f;
        if (!std::isfinite(r)) r = 0.f;
        L[i] = std::clamp(l, -4.f, 4.f);
        R[i] = std::clamp(r, -4.f, 4.f);
    }
}

} // namespace nf
