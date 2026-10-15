#include "Capture.h"
#include <algorithm>
#include <cmath>

namespace nf {

std::shared_ptr<const WavetableBank> renderCapture(const ParamState& state,
                                                   const std::array<std::shared_ptr<const WavetableBank>, kNumCaptures>& existing,
                                                   const CaptureSettings& cs, std::string name)
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    auto engine = std::make_unique<Engine>();
    engine->prepare(sr, block);
    for (int i = 0; i < kNumCaptures; ++i) engine->setCapture(i, existing[(size_t) i]);
    engine->setExcludeSub(true);

    const double seconds = std::max(0.25, cs.beats * 60.0 / std::max(20.0, cs.bpm));
    const int lat = engine->getLatencySamples();
    const int total = (int) (seconds * sr) + lat + block;
    std::vector<float> L((size_t) total), R((size_t) total);

    Transport t;
    t.bpm = cs.bpm; t.playing = true; t.ppq = 0;
    MidiEvent on { 0, 0x90, (uint8_t) std::clamp(cs.note, 0, 127), 100 };
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        engine->render(L.data() + pos, R.data() + pos, n, state.v.data(), pos == 0 ? &on : nullptr, pos == 0 ? 1 : 0, t);
        t.ppq += n / sr * cs.bpm / 60.0;
    }

    // pitch-synchronous slicing: the held note's period is known exactly
    const double hz = 440.0 * std::pow(2.0, (cs.note - 69) / 12.0);
    const double period = sr / hz;
    const double startAt = lat + period; // skip the first cycle (attack)
    const int usable = (int) ((total - block - startAt) / period) - 1;
    const int frames = std::clamp(cs.maxFrames, 1, 256);
    const double span = std::max(0, usable - 1) * period; // frames may start at any point in time
    std::vector<std::vector<float>> out((size_t) frames, std::vector<float>(WavetableBank::kFrameSize));
    auto at = [&](double x) { // cubic Hermite on the mono sum
        int i = (int) x; float f = (float) (x - i);
        auto s = [&](int k) { k = std::clamp(k, 0, total - 1); return 0.5f * (L[(size_t) k] + R[(size_t) k]); };
        float y0 = s(i - 1), y1 = s(i), y2 = s(i + 1), y3 = s(i + 2);
        float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    };
    for (int f = 0; f < frames; ++f) {
        const double c0 = startAt + (frames == 1 ? 0.0 : span * f / (frames - 1));
        for (int j = 0; j < WavetableBank::kFrameSize; ++j)
            out[(size_t) f][(size_t) j] = at(c0 + j * period / WavetableBank::kFrameSize);
    }
    return WavetableBank::fromFrames(out, std::move(name));
}

ParamState nextGenerationState(const ParamState& cur, int slot)
{
    using namespace pid;
    ParamState s = cur;
    const ParamState def;
    const int tableChoice = tables::getNumBuiltIn() + std::clamp(slot, 0, kNumCaptures - 1);
    // Osc A plays the capture, everything else in the source section resets
    for (int k = 0; k < numOsc; ++k) { s[osc(0, k)] = def[osc(0, k)]; s[osc(1, k)] = def[osc(1, k)]; }
    s.setReal(osc(0, oOn), 1);
    s.setReal(osc(0, oTable), (float) tableChoice);
    s.setReal(osc(0, oLevel), 1.f);
    s.setReal(osc(1, oOn), 0);
    s.setReal(op + opOn, 0);
    s.setReal(noise + nLevel, 0);
    s.setReal(vf + vfOn, 0);
    s.setReal(pitchEnv, 0);
    for (int i = 0; i < kNumSlots * numSlot; ++i) s[slot0 + i] = def[slot0 + i];
    for (int i = 0; i < kNumPasses; ++i) s[pid::motion(i, mDepth)] = def[pid::motion(i, mDepth)];
    // drop routes whose destinations were reset
    const auto& dsts = modDestinations();
    for (int x = 0; x < kNumMatrix; ++x) {
        int c = (int) s.real(mx(x, mxDst));
        int d = (c > 0 && c < (int) dsts.size()) ? dsts[(size_t) c] : -1;
        bool cleared = d >= 0 && ((d >= oscA && d < sub) || (d >= vf && d < vf + numVf) ||
                                  (d >= slot0 && d < slot0 + kNumSlots * numSlot) || d == pitchEnv);
        if (cleared) for (int k = 0; k < numMx; ++k) s[mx(x, k)] = def[mx(x, k)];
    }
    return s;
}

std::vector<float> bankToFrames(const WavetableBank& bank)
{
    std::vector<float> r;
    r.reserve((size_t) bank.getNumFrames() * WavetableBank::kFrameSize);
    for (int f = 0; f < bank.getNumFrames(); ++f)
        r.insert(r.end(), bank.getFrame(f), bank.getFrame(f) + WavetableBank::kFrameSize);
    return r;
}

std::shared_ptr<const WavetableBank> bankFromFrames(const std::vector<float>& data, std::string name)
{
    const int n = (int) (data.size() / WavetableBank::kFrameSize);
    if (n <= 0) return nullptr;
    std::vector<std::vector<float>> frames((size_t) n);
    for (int f = 0; f < n; ++f)
        frames[(size_t) f].assign(data.begin() + (ptrdiff_t) f * WavetableBank::kFrameSize,
                                  data.begin() + (ptrdiff_t) (f + 1) * WavetableBank::kFrameSize);
    return WavetableBank::fromFrames(frames, std::move(name));
}

} // namespace nf
