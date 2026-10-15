#pragma once
// Halfband FIR decimators (Kaiser-windowed sinc) for the internal oversampling.
// Linear phase; latency is reported in output samples.
#include <cmath>
#include <vector>

namespace nf {

class HalfbandDecimator {
public:
    // halfLen M: taps run -M..M (odd M keeps the halfband zero pattern). beta: Kaiser beta.
    void design(int M, double beta)
    {
        m = M;
        const int L = 2 * M + 1;
        std::vector<double> h((size_t) L);
        auto bessel0 = [](double x) {
            double sum = 1, t = 1;
            for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); sum += t; }
            return sum;
        };
        const double i0b = bessel0(beta);
        for (int n = -M; n <= M; ++n) {
            double sinc = n == 0 ? 0.5 : std::sin(M_PI * n / 2.0) / (M_PI * n);
            double r = (double) n / M;
            double w = bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0b;
            h[(size_t) (n + M)] = sinc * w;
        }
        double sum = 0; for (double v : h) sum += v;
        // keep only non-zero taps (odd offsets from center) plus the center
        taps.clear(); offs.clear();
        for (int n = -M; n <= M; ++n) {
            if (n != 0 && (n % 2) == 0) continue;
            taps.push_back((float) (h[(size_t) (n + M)] / sum));
            offs.push_back(n + M);
        }
        hist.assign((size_t) (2 * L), 0.f);
        len = L; pos = 0;
    }
    void reset() { std::fill(hist.begin(), hist.end(), 0.f); pos = 0; }
    int latencyOut() const { return m / 2; } // M input samples = M/2 output samples

    // in: 2n samples -> out: n samples (may alias in == out)
    void process(const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i) {
            push(in[2 * i]);
            push(in[2 * i + 1]);
            const float* x = &hist[(size_t) pos]; // x[0..len-1], oldest first
            float acc = 0;
            for (size_t k = 0; k < taps.size(); ++k) acc += taps[k] * x[offs[k]];
            out[i] = acc;
        }
    }

private:
    void push(float v)
    {
        hist[(size_t) pos] = v;
        hist[(size_t) (pos + len)] = v; // mirrored so a contiguous window always exists
        pos = (pos + 1) % len;
    }
    std::vector<float> taps, hist;
    std::vector<int> offs;
    int m = 0, len = 1, pos = 0;
};

// Decimates by 1, 2 or 4 for one channel.
class Decimator {
public:
    void prepare(int factor, double hostRate)
    {
        os = factor;
        // final stage: passband ~19-20 kHz; wider transition is fine at 48k+
        int M = hostRate < 46000 ? 71 : 47;
        a.design(M, 9.5);
        b.design(15, 8.0);
        a.reset(); b.reset();
    }
    int latency() const { return os == 1 ? 0 : (os == 2 ? a.latencyOut() : a.latencyOut() + (b.latencyOut() + 1) / 2); }
    void process(float* buf, int nOut) // buf holds nOut*os samples; result in buf[0..nOut)
    {
        if (os == 4) { b.process(buf, buf, nOut * 2); a.process(buf, buf, nOut); }
        else if (os == 2) a.process(buf, buf, nOut);
    }
private:
    HalfbandDecimator a, b;
    int os = 1;
};

} // namespace nf
