#include "modules/dsp/phaser.h"
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

// steady-state gain at one frequency; module is reset first so the LFO sits
// at phase 0 (lowest corner) and only drifts a little during the burst
float gainAt(std::unique_ptr<namfx::ModuleBase>& mod, float freq, double sampleRate)
{
    mod->reset();
    std::vector<float> in(12000, 0.0f);
    std::vector<float> out(12000, 0.0f);
    fillSine(in, freq, 0.2f, sampleRate);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    return peakOf(out, 6000, 12000) / peakOf(in, 6000, 12000);
}

} // namespace

TEST_CASE("phaser registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerPhaser(registry);
    REQUIRE(registry.has("mod.phaser"));
    REQUIRE(registry.categoryOf("mod.phaser") == "pedal");
    REQUIRE(registry.specsFor("mod.phaser").size() == 3);
    REQUIRE(registry.findParam("mod.phaser", "depth") != nullptr);
    REQUIRE(registry.findParam("mod.phaser", "rate") != nullptr);
    REQUIRE(registry.findParam("mod.phaser", "level") != nullptr);

    auto mod = registry.create("mod.phaser");
    REQUIRE(mod != nullptr);
}

TEST_CASE("phaser all-pass bank is unity gain with deep fixed notches at depth zero")
{
    namfx::ModuleRegistry registry;
    namfx::registerPhaser(registry);
    auto mod = registry.create("mod.phaser");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("depth", 0.0f);
    mod->setParameter("rate", 0.0f);
    mod->setParameter("level", 1.0f);

    float minGain = 10.0f;
    float maxGain = 0.0f;
    for (float f = 60.0f; f <= 8000.0f; f *= 1.08f) {
        const float g = gainAt(mod, f, 48000.0);
        minGain = std::min(minGain, g);
        maxGain = std::max(maxGain, g);
    }

    // the 50/50 dry/wet sum of four all-pass stages is still unity gain, but
    // phase cancellation carves notches (first near 124 Hz at fc = 300 Hz)
    REQUIRE(minGain < 0.35f);
    REQUIRE(maxGain > 0.85f);
    REQUIRE(maxGain < 1.1f);
}

TEST_CASE("phaser depth sweeps the notch and modulates the envelope")
{
    namfx::ModuleRegistry registry;
    namfx::registerPhaser(registry);

    auto envelopeRatio = [&registry](float depth, float rate) {
        auto mod = registry.create("mod.phaser");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("depth", depth);
        mod->setParameter("rate", rate);
        mod->setParameter("level", 1.0f);

        std::vector<float> in(192000, 0.0f);
        std::vector<float> out(192000, 0.0f);
        fillSine(in, 300.0f, 0.3f, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

        float lo = 10.0f;
        float hi = 0.0f;
        for (std::size_t i = 48000; i + 8000 <= out.size(); i += 8000) {
            const float p = peakOf(out, i, i + 8000);
            lo = std::min(lo, p);
            hi = std::max(hi, p);
        }
        return hi / std::max(lo, 1e-6f);
    };

    // sweeping depth modulates the amplitude at 300 Hz as the notch passes
    REQUIRE(envelopeRatio(1.0f, 0.2f) > 2.0f);
    // zero depth leaves the notch fixed: the envelope stays flat
    REQUIRE(envelopeRatio(0.0f, 0.2f) < 1.3f);
}

TEST_CASE("phaser level attenuates the mix")
{
    namfx::ModuleRegistry registry;
    namfx::registerPhaser(registry);
    auto mod = registry.create("mod.phaser");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("depth", 0.5f);
    mod->setParameter("rate", 0.3f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(24000, 0.0f);
    std::vector<float> out(24000, 0.0f);
    fillSine(in, 440.0f, 0.3f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    REQUIRE(peakOf(out, 20000, 24000) < 1e-5f);
}

TEST_CASE("phaser parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerPhaser(registry);
    auto mod = registry.create("mod.phaser");
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

TEST_CASE("phaser works at 44.1k and 96k sample rates")
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
        namfx::registerPhaser(registry);
        auto mod = registry.create("mod.phaser");
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

TEST_CASE("phaser process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerPhaser(registry);
    auto mod = registry.create("mod.phaser");
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
