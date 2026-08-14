#pragma once

#include <cstddef>
#include <vector>

namespace namfx {
namespace ir {

// Linear-interpolation resampling (v1; windowed-sinc + minimum-phase
// pre-processing is a later upgrade, docs/research/ir.md). Load path only.
inline std::vector<float> resampleLinear(const std::vector<float>& in, double fromRate,
                                         double toRate)
{
    if (fromRate <= 0.0 || toRate <= 0.0 || in.empty()) {
        return in;
    }
    if (fromRate == toRate) {
        return in;
    }
    const double ratio = fromRate / toRate;
    const std::size_t outLen = static_cast<std::size_t>(in.size() * ratio) + 1;
    std::vector<float> out(outLen);
    for (std::size_t n = 0; n < outLen; ++n) {
        const double pos = static_cast<double>(n) * ratio;
        std::size_t i0 = static_cast<std::size_t>(pos);
        const double frac = pos - static_cast<double>(i0);
        if (i0 + 1 >= in.size()) {
            out[n] = in.back();
            continue;
        }
        out[n] = static_cast<float>(in[i0] * (1.0 - frac) + in[i0 + 1] * frac);
    }
    return out;
}

} // namespace ir
} // namespace namfx
