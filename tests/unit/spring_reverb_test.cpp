#include "modules/dsp/spring_reverb.h"
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

constexpr std::size_t kImpulseAt = 1000;

void fillSine(std::vector<float>& buf, float freq, float amp, std::size_t begin, std::size_t end,
              double sampleRate)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = begin; i < end; ++i) {
        buf[i] = amp * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / sampleRate));
    }
}

double energyOf(const std::vector<float>& buf, std::size_t begin, std::size_t end)
{
    double energy = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        energy += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    }
    return energy;
}

float peakOf(const std::vector<float>& buf, std::size_t begin, std::size_t end)
{
    float peak = 0.0f;
    for (std::size_t i = begin; i < end; ++i) {
        peak = std::max(peak, std::fabs(buf[i]));
    }
    return peak;
}

std::unique_ptr<namfx::ModuleBase> makeModule(namfx::ModuleRegistry& registry)
{
    auto mod = registry.create("rvb.spring");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    return mod;
}

} // namespace

TEST_CASE("spring reverb registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerSpringReverb(registry);
    REQUIRE(registry.has("rvb.spring"));
    REQUIRE(registry.categoryOf("rvb.spring") == "pedal");
    REQUIRE(registry.specsFor("rvb.spring").size() == 3);
    REQUIRE(registry.findParam("rvb.spring", "dwell") != nullptr);
    REQUIRE(registry.findParam("rvb.spring", "mix") != nullptr);
    REQUIRE(registry.findParam("rvb.spring", "damp") != nullptr);
    REQUIRE(makeModule(registry) != nullptr);
}

TEST_CASE("spring reverb turns an impulse into a decaying tail")
{
    namfx::ModuleRegistry registry;
    namfx::registerSpringReverb(registry);
    auto mod = makeModule(registry);
    mod->setParameter("dwell", 0.5f);
    mod->setParameter("mix", 1.0f);
    mod->setParameter("damp", 0.5f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    in[kImpulseAt] = 1.0f;

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // echoes start shortly after the impulse and keep ringing for hundreds
    // of ms with decreasing energy (spring feedback loop)
    const double early = energyOf(out, kImpulseAt + 2000, kImpulseAt + 6000);
    const double mid = energyOf(out, kImpulseAt + 10000, kImpulseAt + 20000);
    const double late = energyOf(out, kImpulseAt + 24000, kImpulseAt + 40000);

    REQUIRE(early > 0.0);
    REQUIRE(early > mid);
    REQUIRE(mid > late * 2.0);
    REQUIRE(late > 0.0);
}

TEST_CASE("spring reverb mix zero leaves only the dry signal")
{
    namfx::ModuleRegistry registry;
    namfx::registerSpringReverb(registry);
    auto mod = makeModule(registry);
    mod->setParameter("dwell", 0.5f);
    mod->setParameter("mix", 0.0f);
    mod->setParameter("damp", 0.5f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    in[kImpulseAt] = 1.0f;

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    REQUIRE(energyOf(out, kImpulseAt - 10, kImpulseAt + 10) > 0.5);
    REQUIRE(energyOf(out, kImpulseAt + 2000, kImpulseAt + 40000) < 1e-5);
}

TEST_CASE("spring reverb dwell drives the springs harder")
{
    namfx::ModuleRegistry registry;
    namfx::registerSpringReverb(registry);

    auto wetPeak = [&registry](float dwell) {
        auto mod = makeModule(registry);
        mod->setParameter("dwell", dwell);
        mod->setParameter("mix", 1.0f);
        mod->setParameter("damp", 1.0f);

        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        fillSine(in, 1000.0f, 0.3f, 2000, 2480, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        return peakOf(out, 4000, 20000);
    };

    const float soft = wetPeak(0.0f);
    const float hard = wetPeak(1.0f);
    REQUIRE(hard > soft * 1.5f);
}

TEST_CASE("spring reverb damp darkens the wet signal")
{
    namfx::ModuleRegistry registry;
    namfx::registerSpringReverb(registry);

    auto wetPeakAt = [&registry](float damp) {
        auto mod = makeModule(registry);
        mod->setParameter("dwell", 0.5f);
        mod->setParameter("mix", 1.0f);
        mod->setParameter("damp", damp);

        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        fillSine(in, 3000.0f, 0.5f, 2000, 2480, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        return peakOf(out, 4000, 20000);
    };

    // the damping low-pass at 200 Hz cuts 3 kHz hard; at 8 kHz it passes
    REQUIRE(wetPeakAt(1.0f) > wetPeakAt(0.0f) * 10.0f);
}

TEST_CASE("spring reverb parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerSpringReverb(registry);
    auto mod = makeModule(registry);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float dwells[] = {0.0f, 0.5f, 1.0f};
    const float mixes[] = {0.0f, 0.5f, 1.0f};
    const float damps[] = {0.0f, 0.5f, 1.0f};

    for (float dwell : dwells) {
        for (float mix : mixes) {
            for (float damp : damps) {
                mod->reset();
                mod->setParameter("dwell", dwell);
                mod->setParameter("mix", mix);
                mod->setParameter("damp", damp);
                mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                for (float v : out) {
                    REQUIRE(std::isfinite(v));
                }
                REQUIRE(peakOf(out, 0, out.size()) < 20.0f);
            }
        }
    }
}

TEST_CASE("spring reverb works at 44.1k and 96k sample rates")
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
        namfx::registerSpringReverb(registry);
        auto mod = registry.create("rvb.spring");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("dwell", 0.5f);
        mod->setParameter("mix", 0.5f);
        mod->setParameter("damp", 0.5f);

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

TEST_CASE("spring reverb process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerSpringReverb(registry);
    auto mod = makeModule(registry);
    mod->setParameter("dwell", 0.5f);
    mod->setParameter("mix", 0.5f);
    mod->setParameter("damp", 0.5f);

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
