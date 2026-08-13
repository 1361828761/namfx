// Golden output regression: renders the ts808 scenario and compares against
// the committed golden file with a dual assertion - sample-wise relative
// error plus a frequency-domain feature (third-harmonic energy ratio).
#include "audio/chain.h"
#include "audio/slot.h"
#include "modules/dsp/ts808.h"
#include "modules/module_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifndef NAMFX_GOLDEN_DIR
#define NAMFX_GOLDEN_DIR "data"
#endif

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kSeconds = 2;
constexpr int kBlock = 64;
constexpr double kTwoPi = 6.28318530717958647692;
const char* kGoldenName = "ts808_drive5_tone5_level0.f32";

#ifdef __aarch64__
constexpr double kRmsTolerance = 1e-3; // ARM: "inconsistency is a feature"
#else
constexpr double kRmsTolerance = 1e-5;
#endif

std::vector<float> renderCurrent()
{
    auto registry = std::make_shared<namfx::ModuleRegistry>();
    namfx::registerTs808(*registry);

    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "od.ts808";
    def.params.push_back(namfx::ParamInit{"drive", 5.0f});
    def.params.push_back(namfx::ParamInit{"tone", 5.0f});
    def.params.push_back(namfx::ParamInit{"level", 0.0f});
    slots.push_back(std::move(def));

    namfx::audio::Chain chain(std::move(slots), registry);
    chain.prepare(kSampleRate, kBlock);

    const int total = kSeconds * static_cast<int>(kSampleRate);
    std::vector<float> in(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> out(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    std::vector<float> samples(static_cast<std::size_t>(total));

    int sample = 0;
    for (int offset = 0; offset < total; offset += kBlock) {
        for (int i = 0; i < kBlock; ++i) {
            const double t = static_cast<double>(sample) / kSampleRate;
            in[static_cast<std::size_t>(i)] =
                static_cast<float>(0.3 * std::sin(kTwoPi * 220.0 * t));
            ++sample;
        }
        chain.process(in.data(), inR.data(), out.data(), outR.data(), kBlock);
        std::memcpy(samples.data() + offset, out.data(), sizeof(float) * kBlock);
    }
    return samples;
}

std::vector<float> loadGolden()
{
    const std::string path = std::string(NAMFX_GOLDEN_DIR) + "/" + kGoldenName;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    file.seekg(0, std::ios::end);
    const std::streamsize bytes = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<float> samples(static_cast<std::size_t>(bytes) / sizeof(float));
    file.read(reinterpret_cast<char*>(samples.data()), bytes);
    return samples;
}

// Goertzel power at a target frequency over a window
double goertzelPower(const std::vector<float>& x, std::size_t begin, std::size_t end, double freq)
{
    const std::size_t n = end - begin;
    const double w = kTwoPi * freq / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        s0 = static_cast<double>(x[begin + i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return power * 2.0 / static_cast<double>(n);
}

} // namespace

TEST_CASE("ts808 golden output matches the committed reference within tolerance")
{
    const std::vector<float> current = renderCurrent();
    const std::vector<float> golden = loadGolden();
    REQUIRE_FALSE(golden.empty());

    const std::size_t n = golden.size() < current.size() ? golden.size() : current.size();
    REQUIRE(n > 0);

    double sumErr = 0.0;
    double sumRef = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        REQUIRE(std::isfinite(current[i]));
        const double a = static_cast<double>(golden[i]);
        const double b = static_cast<double>(current[i]);
        const double d = a - b;
        sumErr += d * d;
        sumRef += a * a;
    }
    const double rmsRel = std::sqrt(sumErr / sumRef);
    INFO("rms relative error " << rmsRel);
    REQUIRE(rmsRel < kRmsTolerance);
}

TEST_CASE("ts808 golden output keeps the same harmonic fingerprint")
{
    const std::vector<float> current = renderCurrent();
    const std::vector<float> golden = loadGolden();
    REQUIRE_FALSE(golden.empty());

    const std::size_t n = golden.size() < current.size() ? golden.size() : current.size();
    const std::size_t tailBegin = n / 2; // skip transient, use the second half

    const double fundG = goertzelPower(golden, tailBegin, n, 220.0);
    const double thirdG = goertzelPower(golden, tailBegin, n, 660.0);
    const double fundC = goertzelPower(current, tailBegin, n, 220.0);
    const double thirdC = goertzelPower(current, tailBegin, n, 660.0);

    REQUIRE(fundG > 0.0);
    REQUIRE(fundC > 0.0);
    const double ratioG = thirdG / fundG;
    const double ratioC = thirdC / fundC;
    INFO("third harmonic ratio: golden " << ratioG << " current " << ratioC);
    REQUIRE(std::fabs(ratioC - ratioG) < 0.01 * ratioG);
}
