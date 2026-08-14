#include "modules/ir/resample.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace {

// single-bin DFT magnitude (rectangular window, steady-state segment)
double binAmplitude(const std::vector<float>& y, double sampleRate, double freq)
{
    const std::size_t m = y.size() / 2;
    std::complex<double> acc(0.0, 0.0);
    for (std::size_t i = m; i < y.size(); ++i) {
        const double ph = -2.0 * 3.14159265358979323846 * freq * static_cast<double>(i)
            / sampleRate;
        acc += std::complex<double>(std::cos(ph), std::sin(ph))
            * static_cast<double>(y[i]);
    }
    return 2.0 * std::abs(acc) / static_cast<double>(y.size() - m);
}

double energy(const std::vector<float>& x)
{
    double e = 0.0;
    for (float v : x) {
        e += static_cast<double>(v) * static_cast<double>(v);
    }
    return e;
}

} // namespace

TEST_CASE("sinc resample keeps same-rate and empty inputs untouched")
{
    const std::vector<float> in = {0.1f, -0.2f, 0.3f};
    REQUIRE(namfx::ir::resampleSinc(in, 48000.0, 48000.0) == in);
    REQUIRE(namfx::ir::resampleSinc({}, 44100.0, 48000.0).empty());
}

TEST_CASE("sinc resample preserves band-limited sine amplitudes within -60 dB")
{
    // kernel passband is flat (docs/research/resample_minphase.md); the
    // -60 dB floor here is set by the single-bin DFT measurement itself
    constexpr int kInRate = 44100;
    constexpr int kOutRate = 48000;
    for (double freq : {1000.0, 5000.0, 10000.0, 15000.0}) {
        constexpr std::size_t kLen = 44100;
        std::vector<float> in(kLen);
        for (std::size_t i = 0; i < kLen; ++i) {
            in[i] = static_cast<float>(
                0.25 * std::sin(2.0 * 3.14159265358979323846 * freq * static_cast<double>(i)
                                / kInRate));
        }
        const std::vector<float> out = namfx::ir::resampleSinc(in, kInRate, kOutRate);
        const double amp = binAmplitude(out, kOutRate, freq);
        REQUIRE(std::fabs(amp - 0.25) < 1e-3); // -60 dB
    }
}

TEST_CASE("sinc resample round-trips a band-limited IR within -60 dB")
{
    // 44.1k -> 48k -> 44.1k: interpolation consistency without any
    // external reference (signal stays below the 44.1k Nyquist)
    std::vector<float> in(4096);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        in[i] = static_cast<float>(std::exp(-30.0 * t) * std::sin(2.0 * 3.14159265358979323846 * 800.0 * t)
                                   + 0.3 * std::exp(-60.0 * t)
                                         * std::sin(2.0 * 3.14159265358979323846 * 220.0 * t));
    }
    const std::vector<float> up = namfx::ir::resampleSinc(in, 44100.0, 48000.0);
    const std::vector<float> back = namfx::ir::resampleSinc(up, 48000.0, 44100.0);
    const std::size_t expect = in.size();
    REQUIRE((back.size() == expect || back.size() == expect + 1));
    double worst = 0.0;
    for (std::size_t i = 128; i + 128 < in.size(); ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(back[i]) - in[i]));
    }
    const double peak = 1.0; // both components are < 1 in amplitude
    REQUIRE(worst / peak < 1e-3); // -60 dB
}

TEST_CASE("sinc resample of a 24k IR keeps length, energy and finiteness")
{
    std::vector<float> in(1024);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const double t = static_cast<double>(i) / 24000.0;
        in[i] = static_cast<float>(std::exp(-20.0 * t) * std::sin(0.1 * static_cast<double>(i)));
    }
    const std::vector<float> out = namfx::ir::resampleSinc(in, 24000.0, 48000.0);
    const std::size_t expect = in.size() * 2;
    REQUIRE((out.size() == expect || out.size() == expect + 1));
    REQUIRE(std::fabs(energy(out) - 2.0 * energy(in)) / (2.0 * energy(in)) < 1e-2);
    for (float v : out) {
        REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("sinc resample downsampling rejects above-Nyquist content")
{
    // 96k -> 48k: the target Nyquist is 24 kHz; a 30 kHz sine must be
    // attenuated by the anti-aliasing cutoff (not folded back), while
    // 15 kHz survives
    constexpr int kInRate = 96000;
    constexpr int kOutRate = 48000;
    auto run = [kInRate, kOutRate](double freq, double probeFreq) {
        constexpr std::size_t kLen = 96000;
        std::vector<float> in(kLen);
        for (std::size_t i = 0; i < kLen; ++i) {
            in[i] = static_cast<float>(
                0.25 * std::sin(2.0 * 3.14159265358979323846 * freq * static_cast<double>(i)
                                / kInRate));
        }
        const std::vector<float> out = namfx::ir::resampleSinc(in, kInRate, kOutRate);
        return binAmplitude(out, kOutRate, probeFreq);
    };
    REQUIRE(std::fabs(run(15000.0, 15000.0) - 0.25) < 1e-3); // preserved
    // 30 kHz is above the 24 kHz target Nyquist: gone from its own bin and
    // not folded back to 30 - 24 = 6 kHz
    REQUIRE(run(30000.0, 30000.0) < 0.25 * 1e-3);
    REQUIRE(run(30000.0, 6000.0) < 0.25 * 1e-3);
}
