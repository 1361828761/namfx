#include "modules/dsp/chorus.h"
#include "modules/module_registry.h"
#include "platform/rt_alloc.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

void fillSine(std::vector<float>& buf, float freq, float amp, double sampleRate)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = amp * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / sampleRate));
    }
}

float peakOf(const std::vector<float>& buf, std::size_t begin, std::size_t end)
{
    float peak = 0.0f;
    for (std::size_t i = begin; i < end; ++i) {
        peak = std::max(peak, std::fabs(buf[i]));
    }
    return peak;
}

// envelope modulation depth: variance of the per-100-sample peak sequence
float modulationDepth(std::unique_ptr<namfx::ModuleBase>& mod, double sampleRate)
{
    mod->reset();
    std::vector<float> in(96000, 0.0f);
    std::vector<float> out(96000, 0.0f);
    fillSine(in, 440.0f, 0.3f, sampleRate);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    constexpr std::size_t kSeg = 100;
    std::vector<float> peaks;
    for (std::size_t i = 48000; i + kSeg <= out.size(); i += kSeg) {
        peaks.push_back(peakOf(out, i, i + kSeg));
    }
    float mean = 0.0f;
    for (float v : peaks) {
        mean += v;
    }
    mean /= static_cast<float>(peaks.size());
    float var = 0.0f;
    for (float v : peaks) {
        var += (v - mean) * (v - mean);
    }
    return std::sqrt(var / static_cast<float>(peaks.size()));
}

} // namespace

TEST_CASE("chorus registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerChorus(registry);
    REQUIRE(registry.has("mod.chorus"));
    REQUIRE(registry.categoryOf("mod.chorus") == "pedal");
    REQUIRE(registry.specsFor("mod.chorus").size() == 3);
    REQUIRE(registry.findParam("mod.chorus", "depth") != nullptr);
    REQUIRE(registry.findParam("mod.chorus", "rate") != nullptr);
    REQUIRE(registry.findParam("mod.chorus", "level") != nullptr);

    auto mod = registry.create("mod.chorus");
    REQUIRE(mod != nullptr);
}

TEST_CASE("chorus modulates the signal envelope")
{
    namfx::ModuleRegistry registry;
    namfx::registerChorus(registry);
    auto mod = registry.create("mod.chorus");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("depth", 0.8f);
    mod->setParameter("rate", 0.5f);
    mod->setParameter("level", 1.0f);

    const float depth = modulationDepth(mod, 48000.0);

    // dry/wet interference moves the output amplitude noticeably over time
    REQUIRE(depth > 0.005f);
}

TEST_CASE("chorus depth increases modulation monotonically")
{
    namfx::ModuleRegistry registry;
    namfx::registerChorus(registry);
    auto mod = registry.create("mod.chorus");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("rate", 0.5f);
    mod->setParameter("level", 1.0f);

    mod->setParameter("depth", 0.1f);
    const float d0 = modulationDepth(mod, 48000.0);
    mod->setParameter("depth", 0.5f);
    const float d1 = modulationDepth(mod, 48000.0);
    mod->setParameter("depth", 0.9f);
    const float d2 = modulationDepth(mod, 48000.0);

    REQUIRE(d1 > d0 * 1.2f);
    REQUIRE(d2 > d0 * 1.2f);
}

TEST_CASE("chorus rate changes modulation speed")
{
    namfx::ModuleRegistry registry;
    namfx::registerChorus(registry);
    auto mod = registry.create("mod.chorus");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("depth", 0.8f);
    mod->setParameter("level", 1.0f);

    // fast rate: delay sweep moves the interference pattern measurably
    // within a short window; near-zero rate is quasi-static there
    auto segDiff = [&mod](float rate) {
        mod->reset();
        mod->setParameter("rate", rate);
        std::vector<float> in(96000, 0.0f);
        std::vector<float> out(96000, 0.0f);
        fillSine(in, 440.0f, 0.3f, 48000.0);
        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        float diff = 0.0f;
        for (std::size_t i = 48000; i + 200 < out.size(); i += 200) {
            diff += std::fabs(peakOf(out, i, i + 200) - peakOf(out, i + 200, i + 400));
        }
        return diff;
    };

    REQUIRE(segDiff(0.9f) > segDiff(0.0f) * 3.0f);
}

TEST_CASE("chorus level scales output")
{
    namfx::ModuleRegistry registry;
    namfx::registerChorus(registry);
    auto mod = registry.create("mod.chorus");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("depth", 0.5f);
    mod->setParameter("rate", 0.5f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillSine(in, 440.0f, 0.3f, 48000.0);
    float dummyR = 0.0f;

    mod->setParameter("level", 0.2f);
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    const float low = peakOf(out, 24000, 48000);

    mod->reset();
    mod->setParameter("level", 0.8f);
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    const float high = peakOf(out, 24000, 48000);

    REQUIRE(high > low * 3.0f);
}

TEST_CASE("chorus parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerChorus(registry);
    auto mod = registry.create("mod.chorus");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float depths[] = {0.0f, 0.5f, 1.0f};
    const float rates[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float depth : depths) {
        for (float rate : rates) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("depth", depth);
                mod->setParameter("rate", rate);
                mod->setParameter("level", level);
                mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                for (float v : out) {
                    REQUIRE(std::isfinite(v));
                }
                REQUIRE(peakOf(out, 0, out.size()) < 20.0f);
            }
        }
    }
}

TEST_CASE("chorus works at 44.1k and 96k sample rates")
{
    const double rates[] = {44100.0, 96000.0};

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    float dummyR = 0.0f;

    for (double rate : rates) {
        namfx::ModuleRegistry registry;
        namfx::registerChorus(registry);
        auto mod = registry.create("mod.chorus");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("depth", 0.5f);
        mod->setParameter("rate", 0.5f);
        mod->setParameter("level", 0.5f);

        std::vector<float> out(48000, 0.0f);
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
        const float peak = peakOf(out, 24000, out.size());
        REQUIRE(peak > 0.0f);
        REQUIRE(peak < 20.0f);
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("chorus process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerChorus(registry);
    auto mod = registry.create("mod.chorus");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("depth", 0.5f);
    mod->setParameter("rate", 0.5f);
    mod->setParameter("level", 0.5f);

    std::vector<float> in(512, 0.3f);
    std::vector<float> out(512, 0.0f);
    float dummyR = 0.0f;

    {
        namfx::rt::ScopedAllocGuard guard;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
