#include "dsp/osc/OscApi.h"
#include "dsp/osc/OscFft.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <mutex>

namespace nf::tables {

using Frames = std::vector<std::vector<float>>;
using oscdetail::cplx;
constexpr int N = WavetableBank::kFrameSize;
constexpr double TWO_PI = 6.283185307179586;
constexpr int kH = 1023;

namespace {

double frac(double x) { return x - std::floor(x); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }

struct Rng {
    unsigned s;
    explicit Rng(unsigned seed) : s(seed) {}
    double next() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; }
};

// amp/ph indexed by harmonic 1..kH: x = sum amp*sin(2*pi*h*phi + ph)
std::vector<float> fromHarmonics(const std::vector<double>& amp, const std::vector<double>& ph)
{
    std::vector<cplx> X((std::size_t) N, cplx(0, 0));
    for (int h = 1; h <= kH; ++h) {
        const double a = amp[(std::size_t) h];
        if (a == 0.0) continue;
        // a*sin(w+p) = a*cos(w+p-pi/2)
        const cplx c = std::polar(a * 0.5 * N, ph[(std::size_t) h] - M_PI * 0.5);
        X[(std::size_t) h] = c;
        X[(std::size_t) (N - h)] = std::conj(c);
    }
    oscdetail::dft(X, true);
    std::vector<float> out((std::size_t) N);
    for (int i = 0; i < N; ++i) out[(std::size_t) i] = (float) X[(std::size_t) i].real();
    return out;
}

// time-domain function of phi in [0,1) evaluated 8x oversampled, then FFT-resampled (brick-wall)
std::vector<float> fromTime(const std::function<double(double)>& f)
{
    constexpr int M = N * 8;
    std::vector<float> y((std::size_t) M);
    for (int i = 0; i < M; ++i) y[(std::size_t) i] = (float) f((double) i / M);
    return WavetableBank::resampleCycle(y.data(), M);
}

using HarmFn = std::function<void(double t, std::vector<double>& amp, std::vector<double>& ph)>;
Frames harmonicFrames(int nf, const HarmFn& fn)
{
    Frames fr;
    std::vector<double> amp((std::size_t) kH + 1), ph((std::size_t) kH + 1);
    for (int f = 0; f < nf; ++f) {
        std::fill(amp.begin(), amp.end(), 0.0);
        std::fill(ph.begin(), ph.end(), 0.0);
        fn(nf > 1 ? (double) f / (nf - 1) : 0.0, amp, ph);
        fr.push_back(fromHarmonics(amp, ph));
    }
    return fr;
}

Frames timeFrames(int nf, const std::function<double(double t, double phi)>& fn)
{
    Frames fr;
    for (int f = 0; f < nf; ++f) {
        const double t = nf > 1 ? (double) f / (nf - 1) : 0.0;
        fr.push_back(fromTime([&](double phi) { return fn(t, phi); }));
    }
    return fr;
}

double gauss(double x, double c, double w) { const double d = (x - c) / w; return std::exp(-0.5 * d * d); }

// --- individual tables ---------------------------------------------------------------------
Frames sawSquare()
{
    return harmonicFrames(32, [](double t, auto& a, auto&) {
        for (int h = 1; h <= kH; ++h) a[(std::size_t) h] = (h & 1 ? 1.0 : 1.0 - t) / h;
    });
}

Frames basicShapes()
{
    std::vector<std::vector<float>> pure;
    for (int s = 0; s < 4; ++s)
        pure.push_back(harmonicFrames(1, [s](double, auto& a, auto& p) {
            for (int h = 1; h <= kH; ++h) {
                if (s == 0) a[(std::size_t) h] = h == 1 ? 1.0 : 0.0;
                else if (s == 1) { if (h & 1) { a[(std::size_t) h] = 1.0 / (double) (h * h); p[(std::size_t) h] = ((h / 2) & 1) ? M_PI : 0.0; } }
                else if (s == 2) a[(std::size_t) h] = 1.0 / h;
                else if (h & 1) a[(std::size_t) h] = 1.0 / h;
            }
        })[0]);
    // pure shapes need comparable level: tri/sine/saw/square peaks differ, bank normalises globally
    Frames fr;
    for (int f = 0; f < 16; ++f) {
        const int seg = std::min(f / 5, 2);
        const double u = (f - seg * 5) / 5.0;
        std::vector<float> o((std::size_t) N);
        for (int i = 0; i < N; ++i)
            o[(std::size_t) i] = (float) lerp(pure[(std::size_t) seg][(std::size_t) i], pure[(std::size_t) seg + 1][(std::size_t) i], u);
        fr.push_back(std::move(o));
    }
    return fr;
}

Frames pulseFrames(int nf, double d0, double d1)
{
    return timeFrames(nf, [=](double t, double phi) {
        const double d = lerp(d0, d1, t);
        return phi < d ? 1.0 : -1.0;
    });
}

Frames formantVox()
{
    static const double F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2600 }, { 270, 2140, 2950 },
                                    { 450, 800, 2830 },  { 325, 700, 2700 } };
    static const double G[3] = { 1.0, 0.7, 0.4 };
    const double f0 = 55.0;
    return harmonicFrames(40, [&](double t, auto& a, auto&) {
        const double v = t * 4.0;
        const int i = std::min((int) v, 3);
        double u = v - i;
        u = u * u * (3 - 2 * u);
        double fm[3];
        for (int k = 0; k < 3; ++k) fm[k] = std::exp(lerp(std::log(F[i][k]), std::log(F[i + 1][k]), u));
        for (int h = 1; h <= kH; ++h) {
            const double fh = h * f0;
            double e = 0.04;
            for (int k = 0; k < 3; ++k) e += G[k] * gauss(fh, fm[k], 60.0 + 0.07 * fm[k]);
            a[(std::size_t) h] = e / std::pow((double) h, 0.6);
        }
    });
}

Frames harmonicSweep()
{
    return harmonicFrames(48, [](double t, auto& a, auto&) {
        const double c = std::exp(lerp(std::log(1.5), std::log(80.0), t));
        const double w = std::max(0.6, 0.1 * c);
        for (int h = 1; h <= kH; ++h) a[(std::size_t) h] = (1.0 + 8.0 * gauss(h, c, w)) / h;
    });
}

Frames metalBars()
{
    static const double R[] = { 1, 2.756, 5.404, 8.933, 13.344, 18.638, 24.815, 31.87, 39.82, 48.65, 58.4, 69.0 };
    Rng rng(1234);
    double basePh[12];
    for (double& p : basePh) p = rng.next() * TWO_PI;
    return harmonicFrames(32, [&](double t, auto& a, auto& ph) {
        for (int j = 0; j < 12; ++j) {
            const double hc = R[j] * (1.0 + 0.8 * t);
            const double amp = 1.0 / std::pow(R[j], 0.6);
            const int h0 = (int) std::floor(hc);
            for (int h = std::max(1, h0 - 1); h <= std::min(kH, h0 + 2); ++h) {
                const double w = std::exp(-std::pow((h - hc) / 0.6, 2.0));
                if (w < 1e-3) continue;
                a[(std::size_t) h] += amp * w;
                ph[(std::size_t) h] = basePh[j] + t * (j + 1) * 0.7 + h * 0.37;
            }
        }
    });
}

Frames reesePulse()
{
    return timeFrames(32, [](double t, double phi) {
        const double d = lerp(0.5, 0.08, t);
        return (phi < d ? 1.0 : -1.0) + 0.5 * (2.0 * phi - 1.0);
    });
}

Frames growlFm()
{
    return timeFrames(48, [](double t, double phi) {
        const double I = 5.5 * t * t + 0.4 * t;
        const double w = TWO_PI * phi;
        return std::sin(w + I * (0.65 * std::sin(w) + 0.35 * std::sin(2 * w)));
    });
}

Frames syncSaw()
{
    return timeFrames(40, [](double t, double phi) {
        const double r = 1.0 + 7.0 * t;
        return 2.0 * frac(r * phi) - 1.0;
    });
}

Frames digitalGrit()
{
    return timeFrames(32, [](double t, double phi) {
        const double H = lerp(256.0, 12.0, t * t);
        const double ph = std::floor(phi * H) / H;
        const double base = 0.6 * (2.0 * ph - 1.0) + 0.4 * std::sin(TWO_PI * 2.0 * ph);
        const double L = std::pow(2.0, lerp(8.0, 1.2, t));
        return std::floor(base * L + 0.5) / L;
    });
}

Frames hollow()
{
    return harmonicFrames(32, [](double t, auto& a, auto&) {
        const double c = std::exp(lerp(std::log(3.0), std::log(48.0), t));
        for (int h = 1; h <= kH; ++h) {
            double v = (h & 1 ? 1.0 : 0.08) / h;
            v *= 1.0 - 0.9 * gauss(h, c, 0.15 * c + 0.5);
            v *= 1.0 - 0.5 * gauss(h, c * 2.3, 0.15 * c * 2.3 + 0.5);
            a[(std::size_t) h] = v;
        }
    });
}

Frames phaseDist()
{
    return timeFrames(32, [](double t, double phi) {
        if (t <= 0.5) {
            const double u = t / 0.5;
            const double m = lerp(0.5, 0.04, u);
            const double p = phi < m ? 0.5 * phi / m : 0.5 + 0.5 * (phi - m) / (1.0 - m);
            return std::cos(TWO_PI * p);
        }
        const double k = lerp(1.0, 14.0, (t - 0.5) / 0.5);
        return std::sin(TWO_PI * k * phi) * (1.0 - phi);
    });
}

struct Entry { const char* name; Frames (*gen)(); };
Frames pwm() { return pulseFrames(24, 0.5, 0.02); }
const Entry kEntries[] = {
    { "Saw>Square", sawSquare }, { "Basic Shapes", basicShapes }, { "PWM", pwm },
    { "Formant Vox", formantVox }, { "Harmonic Sweep", harmonicSweep }, { "Metal Bars", metalBars },
    { "Reese Pulse", reesePulse }, { "Growl FM", growlFm }, { "Sync Saw", syncSaw },
    { "Digital Grit", digitalGrit }, { "Hollow", hollow }, { "Phase Dist", phaseDist },
};
constexpr int kCount = (int) (sizeof(kEntries) / sizeof(kEntries[0]));

std::array<std::once_flag, kCount> gFlags;
std::array<std::shared_ptr<const WavetableBank>, kCount> gBanks;
} // namespace

int getNumBuiltIn() { return kCount; }

std::shared_ptr<const WavetableBank> getBuiltIn(int index)
{
    index = std::clamp(index, 0, kCount - 1);
    std::call_once(gFlags[(std::size_t) index], [index] {
        gBanks[(std::size_t) index] = WavetableBank::fromFrames(kEntries[index].gen(), kEntries[index].name);
    });
    return gBanks[(std::size_t) index];
}

std::vector<std::string> getBuiltInNames()
{
    std::vector<std::string> n;
    for (auto& e : kEntries) n.emplace_back(e.name);
    return n;
}

} // namespace nf::tables
