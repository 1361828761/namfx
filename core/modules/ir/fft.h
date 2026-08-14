#pragma once

#include <complex>
#include <vector>

namespace namfx {
namespace ir {

// Iterative radix-2 FFT (in place, double precision). n must be a power of
// two. Load/prepare path and the partitioned convolution block processing
// path (no allocation); used with double precision to hold the M3 accuracy
// contract (< -100 dB vs the direct-convolution reference).
inline void fftInPlace(std::vector<std::complex<double>>& a, bool inverse)
{
    const int n = static_cast<int>(a.size());
    if (n <= 1) {
        return;
    }
    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[static_cast<std::size_t>(i)], a[static_cast<std::size_t>(j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = 6.28318530717958647692 / len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<double> u = a[static_cast<std::size_t>(i + j)];
                const std::complex<double> v
                    = a[static_cast<std::size_t>(i + j + len / 2)] * w;
                a[static_cast<std::size_t>(i + j)] = u + v;
                a[static_cast<std::size_t>(i + j + len / 2)] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (std::complex<double>& x : a) {
            x /= static_cast<double>(n);
        }
    }
}

} // namespace ir
} // namespace namfx
