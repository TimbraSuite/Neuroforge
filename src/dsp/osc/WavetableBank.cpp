#include "dsp/osc/OscApi.h"
#include "dsp/osc/OscFft.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace nf {

using oscdetail::cplx;

namespace {
constexpr int kN = WavetableBank::kFrameSize;
constexpr int kLevels = 11;

int levelHarmonics(int k) { return k == 0 ? 1023 : (1024 >> k); }
int levelSize(int k) { return k <= 2 ? 2048 : std::max(64, 8192 >> k); }

// Fast log2 approximation (|err| < 0.003) for positive floats.
inline float fastLog2(float x)
{
    std::uint32_t b;
    std::memcpy(&b, &x, 4);
    const float e = (float) ((int) (b >> 23) - 127);
    const float m = (float) (b & 0x7fffff) * (1.0f / 8388608.0f);
    return e + m + 0.346f * m * (1.0f - m);
}
} // namespace

struct WavetableBank::Impl {
    struct Level {
        int size = 0, mask = 0;
        std::vector<float> data;
    };
    std::string name;
    int numFrames = 0;
    Level levels[kLevels];
};

WavetableBank::WavetableBank(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
WavetableBank::~WavetableBank() = default;

const std::string& WavetableBank::getName() const { return impl->name; }
int WavetableBank::getNumFrames() const { return impl->numFrames; }
const float* WavetableBank::getFrame(int frame) const
{
    frame = std::clamp(frame, 0, impl->numFrames - 1);
    return impl->levels[0].data.data() + (std::size_t) frame * kN;
}

std::vector<float> WavetableBank::resampleCycle(const float* data, int length)
{
    std::vector<float> out((std::size_t) kN, 0.f);
    if (!data || length <= 0) return out;
    if (length == kN) { std::copy(data, data + kN, out.begin()); return out; }
    std::vector<cplx> x((std::size_t) length);
    for (int i = 0; i < length; ++i) x[(std::size_t) i] = cplx(data[i], 0.0);
    oscdetail::dft(x, false);
    std::vector<cplx> y((std::size_t) kN, cplx(0, 0));
    const int K = length >= kN ? kN / 2 - 1 : (length - 1) / 2;
    const double sc = (double) kN / (double) length;
    for (int k = 0; k <= K; ++k) {
        y[(std::size_t) k] = x[(std::size_t) k] * sc;
        if (k > 0) y[(std::size_t) (kN - k)] = x[(std::size_t) (length - k)] * sc;
    }
    oscdetail::dft(y, true);
    for (int i = 0; i < kN; ++i) out[(std::size_t) i] = (float) y[(std::size_t) i].real();
    return out;
}

std::shared_ptr<const WavetableBank> WavetableBank::fromFrames(const std::vector<std::vector<float>>& framesIn,
                                                               std::string name)
{
    std::vector<std::vector<float>> frames;
    for (auto& f : framesIn)
        frames.push_back((int) f.size() == kN ? f : resampleCycle(f.data(), (int) f.size()));
    if (frames.empty()) frames.emplace_back((std::size_t) kN, 0.f);
    const int nf = (int) frames.size();

    // spectra (bins 0..1023 kept, DC and Nyquist removed)
    std::vector<std::vector<cplx>> spec((std::size_t) nf);
    std::vector<cplx> tmp((std::size_t) kN);
    for (int f = 0; f < nf; ++f) {
        for (int i = 0; i < kN; ++i) tmp[(std::size_t) i] = cplx(frames[(std::size_t) f][(std::size_t) i], 0.0);
        oscdetail::dft(tmp, false);
        spec[(std::size_t) f].assign(tmp.begin(), tmp.begin() + kN / 2);
        spec[(std::size_t) f][0] = cplx(0, 0);
    }

    auto impl = std::make_unique<Impl>();
    impl->name = std::move(name);
    impl->numFrames = nf;

    auto renderLevel = [&](int k, int f, std::vector<cplx>& buf) {
        const int S = levelSize(k), H = levelHarmonics(k);
        buf.assign((std::size_t) S, cplx(0, 0));
        for (int h = 1; h <= H && h < S / 2; ++h) {
            buf[(std::size_t) h] = spec[(std::size_t) f][(std::size_t) h];
            buf[(std::size_t) (S - h)] = std::conj(buf[(std::size_t) h]);
        }
        oscdetail::fftPow2(buf.data(), (std::size_t) S, true);
    };

    // level 0 first: find the bank peak
    double peak = 0.0;
    std::vector<cplx> buf;
    for (int f = 0; f < nf; ++f) {
        renderLevel(0, f, buf);
        for (int i = 0; i < kN; ++i) peak = std::max(peak, std::abs(buf[(std::size_t) i].real()) / kN);
    }
    const double scale = peak > 1e-9 ? 1.0 / peak : 1.0;

    for (int k = 0; k < kLevels; ++k) {
        auto& L = impl->levels[k];
        L.size = levelSize(k);
        L.mask = L.size - 1;
        L.data.assign((std::size_t) nf * (std::size_t) L.size, 0.f);
        for (int f = 0; f < nf; ++f) {
            renderLevel(k, f, buf);
            float* dst = L.data.data() + (std::size_t) f * (std::size_t) L.size;
            for (int i = 0; i < L.size; ++i)
                dst[i] = (float) (buf[(std::size_t) i].real() / kN * scale);
        }
    }
    return std::make_shared<WavetableBank>(std::move(impl));
}

float WavetableBank::lookup(double phase, float position, double phaseInc) const
{
    const Impl& I = *impl;
    const int nf = I.numFrames;
    const float fp = std::clamp(position, 0.f, 1.f) * (float) (nf - 1);
    int f0 = std::min((int) fp, nf - 1);
    int f1 = std::min(f0 + 1, nf - 1);
    float fr = f1 == f0 ? 0.f : fp - (float) f0;

    // mip level: level k is safe when (1024>>k) * inc <= 0.45  ->  k >= 11.152 + log2(inc)
    float t = 11.152f + fastLog2((float) std::max(phaseInc, 1e-9));
    int la, lb = -1;
    float wa = 1.f;
    if (t <= 0.f) la = 0;
    else if (t >= (float) (kLevels - 1)) la = kLevels - 1;
    else {
        la = (int) t;
        wa = std::clamp(1.f - 3.f * (t - (float) la), 0.f, 1.f);
        if (wa < 1.f) lb = la + 1;
        if (wa <= 0.f) { la = lb; lb = -1; wa = 1.f; }
    }

    if (phase >= 1.0 || phase < 0.0) phase -= std::floor(phase);
    auto read = [&](const Impl::Level& L) {
        const double x = phase * L.size;
        const int i = (int) x;
        const float t2 = (float) (x - i), t3 = t2 * t2 * t2, tt = t2 * t2;
        const float w0 = -0.5f * t3 + tt - 0.5f * t2;
        const float w1 = 1.5f * t3 - 2.5f * tt + 1.f;
        const float w2 = -1.5f * t3 + 2.f * tt + 0.5f * t2;
        const float w3 = 0.5f * t3 - 0.5f * tt;
        const int i0 = (i - 1) & L.mask, i1 = i & L.mask, i2 = (i + 1) & L.mask, i3 = (i + 2) & L.mask;
        const float* a = L.data.data() + (std::size_t) f0 * (std::size_t) L.size;
        float y = w0 * a[i0] + w1 * a[i1] + w2 * a[i2] + w3 * a[i3];
        if (fr > 0.f) {
            const float* b = L.data.data() + (std::size_t) f1 * (std::size_t) L.size;
            const float yb = w0 * b[i0] + w1 * b[i1] + w2 * b[i2] + w3 * b[i3];
            y += fr * (yb - y);
        }
        return y;
    };
    float out = read(I.levels[la]);
    if (lb >= 0) {
        const float o2 = read(I.levels[lb]);
        out = o2 + wa * (out - o2);
    }
    return out;
}

} // namespace nf
