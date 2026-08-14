#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace namfx {
namespace ir {

namespace detail {

// Modified Bessel function I0, polynomial/continued-fraction approximation
// (Numerical Recipes bessi0). Relative error ~1e-7, far below the -100 dB
// resampling target. Load path only.
inline double besselI0(double x)
{
    const double ax = std::fabs(x);
    if (ax < 3.75) {
        const double y = x / 3.75;
        const double y2 = y * y;
        return 1.0 + y2 * (3.5156229 + y2 * (3.0899424 + y2 * (1.2067492
            + y2 * (0.2659732 + y2 * (0.360768e-1 + y2 * 0.45813e-2)))));
    }
    const double y = 3.75 / ax;
    return (std::exp(ax) / std::sqrt(ax)) * (0.39894228 + y * (0.1328592e-1
        + y * (0.225319e-2 + y * (-0.157565e-2 + y * (0.916281e-2
        + y * (-0.2057706e-1 + y * (0.2635537e-1 + y * (-0.1647633e-1
        + y * 0.392377e-2))))))));
}

} // namespace detail

// High-quality resampling (windowed-sinc, Kaiser window beta = 14,
// `zeros` zero crossings per side; docs/research/resample_minphase.md).
// Kernel passband flatness measured at double machine precision; DC gain
// normalized per output sample. On downsampling the kernel cutoff is
// scaled to min(1, toRate/fromRate) x source Nyquist to prevent aliasing.
// Out-of-range source samples are treated as zero (IR tails are silent).
// Load path only.
inline std::vector<float> resampleSinc(const std::vector<float>& in, double fromRate,
                                       double toRate, int zeros = 64)
{
    if (fromRate <= 0.0 || toRate <= 0.0 || in.empty() || fromRate == toRate) {
        return in;
    }
    if (zeros < 4) {
        zeros = 4;
    }
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kBeta = 14.0;
    const double i0b = detail::besselI0(kBeta);
    // sinc(d * cutoff) has its cutoff at cutoff/2 x source Nyquist; we need
    // min(1, toRate/fromRate) / 2, so scale d by min(1, toRate/fromRate)
    const double cutoff = (toRate < fromRate) ? toRate / fromRate : 1.0;
    const std::size_t outLen = static_cast<std::size_t>(in.size() * (toRate / fromRate)) + 1;
    std::vector<float> out(outLen);
    for (std::size_t n = 0; n < outLen; ++n) {
        const double pos = static_cast<double>(n) * fromRate / toRate;
        const long i0 = static_cast<long>(pos);
        double acc = 0.0;
        double wsum = 0.0;
        for (long k = i0 - zeros + 1; k <= i0 + zeros; ++k) {
            if (k < 0 || static_cast<std::size_t>(k) >= in.size()) {
                continue;
            }
            const double d = pos - static_cast<double>(k);
            const double ds = d * cutoff;
            if (d >= static_cast<double>(zeros) || d <= -static_cast<double>(zeros)) {
                continue;
            }
            const double s = (ds == 0.0) ? 1.0 : std::sin(kPi * ds) / (kPi * ds);
            const double w = detail::besselI0(
                kBeta * std::sqrt(1.0 - (d / static_cast<double>(zeros))
                                          * (d / static_cast<double>(zeros))))
                / i0b;
            acc += static_cast<double>(in[static_cast<std::size_t>(k)]) * s * w;
            wsum += s * w;
        }
        out[n] = static_cast<float>((wsum != 0.0) ? acc / wsum : 0.0);
    }
    return out;
}

} // namespace ir
} // namespace namfx
