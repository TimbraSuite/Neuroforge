#pragma once
// Small FFT helpers (offline use only: building mipmaps / tables). Not for the audio thread.
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace nf::oscdetail {

using cplx = std::complex<double>;

inline bool isPow2(std::size_t n) { return n && (n & (n - 1)) == 0; }

// In-place iterative radix-2 FFT, no normalisation.
inline void fftPow2(cplx* a, std::size_t n, bool inverse)
{
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    const double sgn = inverse ? 1.0 : -1.0;
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = sgn * 2.0 * M_PI / (double) len;
        const cplx wlen(std::cos(ang), std::sin(ang));
        const std::size_t half = len >> 1;
        for (std::size_t i = 0; i < n; i += len) {
            cplx w(1.0, 0.0);
            for (std::size_t j = 0; j < half; ++j) {
                if ((j & 63) == 0) w = std::polar(1.0, ang * (double) j);
                const cplx u = a[i + j], v = a[i + j + half] * w;
                a[i + j] = u + v;
                a[i + j + half] = u - v;
                w *= wlen;
            }
        }
    }
}

// General-length DFT (Bluestein for non powers of two). Inverse is normalised by 1/n.
inline void dft(std::vector<cplx>& x, bool inverse)
{
    const std::size_t n = x.size();
    if (n <= 1) return;
    if (isPow2(n)) {
        fftPow2(x.data(), n, inverse);
    } else {
        std::size_t m = 1;
        while (m < 2 * n - 1) m <<= 1;
        const double sgn = inverse ? 1.0 : -1.0;
        std::vector<cplx> w(n), a(m, cplx(0, 0)), b(m, cplx(0, 0));
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t k2 = (k * k) % (2 * n);
            w[k] = std::polar(1.0, sgn * M_PI * (double) k2 / (double) n);
            a[k] = x[k] * w[k];
        }
        b[0] = std::conj(w[0]);
        for (std::size_t k = 1; k < n; ++k) b[k] = b[m - k] = std::conj(w[k]);
        fftPow2(a.data(), m, false);
        fftPow2(b.data(), m, false);
        for (std::size_t i = 0; i < m; ++i) a[i] *= b[i];
        fftPow2(a.data(), m, true);
        for (std::size_t k = 0; k < n; ++k) x[k] = a[k] * w[k] / (double) m;
    }
    if (inverse)
        for (auto& v : x) v /= (double) n;
}

} // namespace nf::oscdetail
