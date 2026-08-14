#pragma once

#include "modules/ir/fft.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace namfx {
namespace ir {

// Convert an impulse response to minimum phase (cepstral / homomorphic
// method, docs/research/resample_minphase.md): FFT -> log|H| -> causal
// cepstrum -> exp -> IFFT. Magnitude spectrum preserved (measured -74 dB
// with the 16x zero padding, -96 dB with 32x); energy front-loaded.
// Load path only (one-time cost, N = nextPow2(16 * L) FFT).
inline std::vector<float> minimumPhase(const std::vector<float>& in)
{
    if (in.empty()) {
        return in;
    }
    std::size_t n = 1;
    while (n < in.size() * 16) {
        n *= 2;
    }
    std::vector<std::complex<double>> buf(n);
    for (std::size_t i = 0; i < in.size(); ++i) {
        buf[i] = std::complex<double>(static_cast<double>(in[i]), 0.0);
    }

    // log magnitude spectrum
    fftInPlace(buf, false);
    for (std::size_t i = 0; i < n; ++i) {
        const double mag = std::abs(buf[i]);
        buf[i] = std::complex<double>(std::log(mag + 1e-30), 0.0);
    }
    // real cepstrum
    fftInPlace(buf, true);
    // causal (minimum-phase) cepstrum: c0, 2c1..2c(N/2-1), c(N/2), zeros
    for (std::size_t i = 0; i < n; ++i) {
        double v = 0.0;
        if (i == 0 || i == n / 2) {
            v = buf[i].real();
        } else if (i < n / 2) {
            v = 2.0 * buf[i].real();
        }
        buf[i] = std::complex<double>(v, 0.0);
    }
    fftInPlace(buf, false);
    for (std::size_t i = 0; i < n; ++i) {
        const double re = std::exp(buf[i].real());
        buf[i] = std::complex<double>(re * std::cos(buf[i].imag()),
                                      re * std::sin(buf[i].imag()));
    }
    fftInPlace(buf, true);

    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<float>(buf[i].real());
    }
    return out;
}

} // namespace ir
} // namespace namfx
