#include "modules/ir/fft.h"
#include "modules/ir/min_phase.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstddef>
#include <random>
#include <vector>

namespace {

std::vector<double> magnitudeSpectrum(const std::vector<float>& x, std::size_t n)
{
    std::vector<std::complex<double>> buf(n);
    for (std::size_t i = 0; i < x.size(); ++i) {
        buf[i] = std::complex<double>(static_cast<double>(x[i]), 0.0);
    }
    namfx::ir::fftInPlace(buf, false);
    std::vector<double> mag(n / 2 + 1);
    for (std::size_t i = 0; i < mag.size(); ++i) {
        mag[i] = std::abs(buf[i]);
    }
    return mag;
}

} // namespace

TEST_CASE("minimum phase keeps the magnitude spectrum within -60 dB")
{
    std::mt19937 rng(5);
    std::vector<float> ir(512);
    for (std::size_t i = 0; i < ir.size(); ++i) {
        ir[i] = static_cast<float>(std::exp(-static_cast<double>(i) / 100.0)
                                   * (static_cast<double>(rng() % 2000) / 1000.0 - 1.0));
    }
    const std::vector<float> mp = namfx::ir::minimumPhase(ir);
    REQUIRE(mp.size() == ir.size());

    const std::vector<double> m0 = magnitudeSpectrum(ir, 2048);
    const std::vector<double> m1 = magnitudeSpectrum(mp, 2048);
    double peak = 0.0;
    double worst = 0.0;
    for (std::size_t i = 0; i < m0.size(); ++i) {
        peak = std::max(peak, m0[i]);
        worst = std::max(worst, std::fabs(m0[i] - m1[i]));
    }
    REQUIRE(worst / peak < 1e-3); // -60 dB (16x pad measured -74 dB)
}

TEST_CASE("minimum phase preserves energy and front-loads it")
{
    std::mt19937 rng(6);
    std::vector<float> ir(512);
    for (std::size_t i = 0; i < ir.size(); ++i) {
        ir[i] = static_cast<float>(std::exp(-static_cast<double>(i) / 80.0)
                                   * (static_cast<double>(rng() % 2000) / 1000.0 - 1.0));
    }
    const std::vector<float> mp = namfx::ir::minimumPhase(ir);

    auto energy = [](const std::vector<float>& x, std::size_t n) {
        double e = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
        }
        return e;
    };
    const double e0 = energy(ir, ir.size());
    const double e1 = energy(mp, mp.size());
    REQUIRE(std::fabs(e0 - e1) / e0 < 1e-3);

    const double head0 = energy(ir, ir.size() / 4) / e0;
    const double head1 = energy(mp, mp.size() / 4) / e1;
    REQUIRE(head1 > head0);
}

TEST_CASE("minimum phase of a unit pulse is the pulse itself")
{
    const std::vector<float> pulse = {0.5f};
    const std::vector<float> mp = namfx::ir::minimumPhase(pulse);
    REQUIRE(mp.size() == 1);
    REQUIRE(std::fabs(mp[0] - 0.5f) < 1e-6f);
    REQUIRE(namfx::ir::minimumPhase({}).empty());
}

TEST_CASE("minimum phase output is real and finite")
{
    std::mt19937 rng(7);
    std::vector<float> ir(256);
    for (std::size_t i = 0; i < ir.size(); ++i) {
        ir[i] = static_cast<float>(rng() % 2000) / 1000.0f - 1.0f;
    }
    const std::vector<float> mp = namfx::ir::minimumPhase(ir);
    for (float v : mp) {
        REQUIRE(std::isfinite(v));
    }
}
