#include "modules/dsp/ns2_gate.h"
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

void fillSine(std::vector<float>& buf, float freq, float amp, std::size_t begin, std::size_t end,
              double sampleRate)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = begin; i < end; ++i) {
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

double energyOf(const std::vector<float>& buf, std::size_t begin, std::size_t end)
{
    double energy = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        energy += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    }
    return energy;
}

} // namespace

TEST_CASE("ns2 gate registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerNs2Gate(registry);
    REQUIRE(registry.has("gate.ns2"));
    REQUIRE(registry.categoryOf("gate.ns2") == "pedal");
    REQUIRE(registry.specsFor("gate.ns2").size() == 3);
    REQUIRE(registry.findParam("gate.ns2", "threshold") != nullptr);
    REQUIRE(registry.findParam("gate.ns2", "decay") != nullptr);
    REQUIRE(registry.findParam("gate.ns2", "level") != nullptr);

    auto mod = registry.create("gate.ns2");
    REQUIRE(mod != nullptr);
}

TEST_CASE("ns2 gate passes loud signal and silences quiet signal")
{
    namfx::ModuleRegistry registry;
    namfx::registerNs2Gate(registry);
    auto mod = registry.create("gate.ns2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("threshold", 0.5f); // -40 dB
    mod->setParameter("decay", 0.1f); // 59 ms close, gate fully shut in the tail
    mod->setParameter("level", 1.0f);

    // 0.5 s loud burst followed by 0.5 s noise floor below the threshold
    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 0, 24000, 48000.0);
    for (std::size_t i = 24000; i < in.size(); ++i) {
        in[i] = 0.006f * std::sin(0.5f * static_cast<float>(i));
    }

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // steady state of the loud burst passes essentially untouched
    REQUIRE(peakOf(out, 12000, 20000) > 0.3f);
    // the below-threshold noise floor is gated down well below its own level
    REQUIRE(peakOf(out, 40000, 48000) < 0.001f);
}

TEST_CASE("ns2 gate decay controls the fade-to-silence time")
{
    namfx::ModuleRegistry registry;
    namfx::registerNs2Gate(registry);

    auto tailEnergy = [&registry](float decay) {
        auto mod = registry.create("gate.ns2");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("threshold", 0.5f);
        mod->setParameter("decay", decay);
        mod->setParameter("level", 1.0f);

        // 0.3 s loud burst then a below-threshold noise floor; the env
        // crosses the threshold ~80 ms after the burst ends, so the decay
        // knob governs how long the residual noise stays audible
        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        fillSine(in, 220.0f, 0.5f, 0, 14400, 48000.0);
        for (std::size_t i = 14400; i < in.size(); ++i) {
            in[i] = 0.002f * std::sin(0.7f * static_cast<float>(i));
        }

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

        // window after the burst and after the env threshold crossing
        return energyOf(out, 22000, 26000);
    };

    const double fast = tailEnergy(0.0f); // 10 ms close
    const double slow = tailEnergy(1.0f); // 500 ms close

    REQUIRE(fast < 0.001);
    REQUIRE(slow > fast * 3.0);
}

TEST_CASE("ns2 gate threshold separates loud from quiet")
{
    namfx::ModuleRegistry registry;
    namfx::registerNs2Gate(registry);

    auto outputPeak = [&registry](float amp) {
        auto mod = registry.create("gate.ns2");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("threshold", 0.7f); // -28 dB
        mod->setParameter("decay", 0.3f);
        mod->setParameter("level", 1.0f);

        std::vector<float> in(24000, 0.0f);
        std::vector<float> out(24000, 0.0f);
        fillSine(in, 330.0f, amp, 0, 24000, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        return peakOf(out, 18000, 24000);
    };

    const float quiet = outputPeak(0.02f); // below -28 dB threshold
    const float loud = outputPeak(0.2f); // above threshold

    REQUIRE(quiet < 0.01f);
    REQUIRE(loud > 0.15f);
}

TEST_CASE("ns2 gate level attenuates the output")
{
    namfx::ModuleRegistry registry;
    namfx::registerNs2Gate(registry);
    auto mod = registry.create("gate.ns2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("threshold", 0.5f);
    mod->setParameter("decay", 0.5f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(24000, 0.0f);
    std::vector<float> out(24000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 0, 24000, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    REQUIRE(peakOf(out, 20000, 24000) < 1e-5f);
}

TEST_CASE("ns2 gate parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerNs2Gate(registry);
    auto mod = registry.create("gate.ns2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float thresholds[] = {0.0f, 0.5f, 1.0f};
    const float decays[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float threshold : thresholds) {
        for (float decay : decays) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("threshold", threshold);
                mod->setParameter("decay", decay);
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

TEST_CASE("ns2 gate works at 44.1k and 96k sample rates")
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
        namfx::registerNs2Gate(registry);
        auto mod = registry.create("gate.ns2");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("threshold", 0.3f);
        mod->setParameter("decay", 0.5f);
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

TEST_CASE("ns2 gate process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerNs2Gate(registry);
    auto mod = registry.create("gate.ns2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("threshold", 0.5f);
    mod->setParameter("decay", 0.5f);
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
