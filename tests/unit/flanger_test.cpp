#include "modules/dsp/flanger.h"
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

// gain at a fixed frequency with range = 0 (fixed delay) and slow LFO
float fixedGain(std::unique_ptr<namfx::ModuleBase>& mod, float freq, double sampleRate)
{
    mod->reset();
    mod->setParameter("feedback", 0.0f);
    mod->setParameter("range", 0.0f);
    mod->setParameter("rate", 0.0f);

    std::vector<float> in(24000, 0.0f);
    std::vector<float> out(24000, 0.0f);
    fillSine(in, freq, 0.2f, sampleRate);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    return peakOf(out, 12000, 24000) / peakOf(in, 12000, 24000);
}

} // namespace

TEST_CASE("flanger registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerFlanger(registry);
    REQUIRE(registry.has("mod.flanger"));
    REQUIRE(registry.categoryOf("mod.flanger") == "pedal");
    REQUIRE(registry.specsFor("mod.flanger").size() == 3);
    REQUIRE(registry.findParam("mod.flanger", "feedback") != nullptr);
    REQUIRE(registry.findParam("mod.flanger", "range") != nullptr);
    REQUIRE(registry.findParam("mod.flanger", "rate") != nullptr);

    auto mod = registry.create("mod.flanger");
    REQUIRE(mod != nullptr);
}

TEST_CASE("flanger creates comb filter notches at fixed delay")
{
    namfx::ModuleRegistry registry;
    namfx::registerFlanger(registry);
    auto mod = registry.create("mod.flanger");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    // 1 ms fixed delay: notch at 500 Hz, pass at 1000 Hz
    const float g500 = fixedGain(mod, 500.0f, 48000.0);
    const float g1000 = fixedGain(mod, 1000.0f, 48000.0);
    const float g1500 = fixedGain(mod, 1500.0f, 48000.0);

    REQUIRE(g500 > 0.0f);
    REQUIRE(g1000 > g500 * 2.0f);
    REQUIRE(g1500 < g1000);
}

TEST_CASE("flanger feedback builds resonance at comb pass frequencies")
{
    namfx::ModuleRegistry registry;
    namfx::registerFlanger(registry);
    auto mod = registry.create("mod.flanger");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("range", 0.0f);
    mod->setParameter("rate", 0.0f);

    auto passPeak = [&mod](float feedback, double sampleRate) {
        mod->reset();
        mod->setParameter("feedback", feedback);
        std::vector<float> in(24000, 0.0f);
        std::vector<float> out(24000, 0.0f);
        fillSine(in, 1000.0f, 0.2f, sampleRate);
        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        return peakOf(out, 12000, 24000);
    };

    const float fb0 = passPeak(0.0f, 48000.0);
    const float fb8 = passPeak(0.8f, 48000.0);

    // feedback injects delayed energy back into the delay input: the comb
    // pass frequencies ring (resonance), which is the audible feedback
    // effect; the 500 Hz notch itself is already perfect at 50/50 dry/wet
    REQUIRE(fb8 > fb0 * 1.5f);
}

TEST_CASE("flanger rate sweeps the comb")
{
    namfx::ModuleRegistry registry;
    namfx::registerFlanger(registry);
    auto mod = registry.create("mod.flanger");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("range", 0.8f);
    mod->setParameter("feedback", 0.0f);

    auto sweepActivity = [&mod](float rate, double sampleRate) {
        mod->reset();
        mod->setParameter("rate", rate);
        std::vector<float> in(96000, 0.0f);
        std::vector<float> out(96000, 0.0f);
        fillSine(in, 500.0f, 0.3f, sampleRate);
        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        float diff = 0.0f;
        for (std::size_t i = 48000; i + 1000 < out.size(); i += 1000) {
            diff += std::fabs(peakOf(out, i, i + 1000) - peakOf(out, i + 1000, i + 2000));
        }
        return diff;
    };

    REQUIRE(sweepActivity(0.0f, 48000.0) > 0.0f);
    REQUIRE(sweepActivity(0.9f, 48000.0) > sweepActivity(0.0f, 48000.0));
}

TEST_CASE("flanger parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerFlanger(registry);
    auto mod = registry.create("mod.flanger");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float feedbacks[] = {0.0f, 0.5f, 1.0f};
    const float ranges[] = {0.0f, 0.5f, 1.0f};
    const float rates[] = {0.0f, 0.5f, 1.0f};

    for (float feedback : feedbacks) {
        for (float range : ranges) {
            for (float rate : rates) {
                mod->reset();
                mod->setParameter("feedback", feedback);
                mod->setParameter("range", range);
                mod->setParameter("rate", rate);
                mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                for (float v : out) {
                    REQUIRE(std::isfinite(v));
                }
                REQUIRE(peakOf(out, 0, out.size()) < 20.0f);
            }
        }
    }
}

TEST_CASE("flanger works at 44.1k and 96k sample rates")
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
        namfx::registerFlanger(registry);
        auto mod = registry.create("mod.flanger");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("feedback", 0.5f);
        mod->setParameter("range", 0.5f);
        mod->setParameter("rate", 0.5f);

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

TEST_CASE("flanger process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerFlanger(registry);
    auto mod = registry.create("mod.flanger");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("feedback", 0.5f);
    mod->setParameter("range", 0.5f);
    mod->setParameter("rate", 0.5f);

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
