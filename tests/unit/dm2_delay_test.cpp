#include "modules/dsp/dm2_delay.h"
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

constexpr std::size_t kBurstStart = 5000; // 104 ms into the buffer

// sine burst (0.01 s) at the given frequency, silence elsewhere
void fillBurst(std::vector<float>& buf, float freq, float amp, double sampleRate)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = 0; i < 480; ++i) {
        buf[kBurstStart + i] = amp
            * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / sampleRate));
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

// echo peak amplitude in a window around start + delayMs
float echoPeak(std::unique_ptr<namfx::ModuleBase>& mod, float delayMs, double sampleRate)
{
    const std::size_t delay = static_cast<std::size_t>(delayMs * sampleRate / 1000.0);
    const std::size_t lo = kBurstStart + delay - 1000;
    const std::size_t hi = kBurstStart + delay + 1000;
    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, sampleRate);

    mod->reset();
    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    return peakOf(out, lo, hi);
}

} // namespace

TEST_CASE("dm2 delay registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);
    REQUIRE(registry.has("dly.dm2"));
    REQUIRE(registry.categoryOf("dly.dm2") == "pedal");
    REQUIRE(registry.specsFor("dly.dm2").size() == 3);
    REQUIRE(registry.findParam("dly.dm2", "time") != nullptr);
    REQUIRE(registry.findParam("dly.dm2", "feedback") != nullptr);
    REQUIRE(registry.findParam("dly.dm2", "level") != nullptr);

    auto mod = registry.create("dly.dm2");
    REQUIRE(mod != nullptr);
}

TEST_CASE("dm2 delay echoes the input after the delay time")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);
    auto mod = registry.create("dly.dm2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f); // 160 ms
    mod->setParameter("feedback", 0.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // dry burst passes at the input position, echo appears 160 ms later
    REQUIRE(peakOf(out, kBurstStart - 500, kBurstStart + 500) > 0.4f);
    REQUIRE(echoPeak(mod, 160.0f, 48000.0) > 0.2f);
}

TEST_CASE("dm2 delay feedback produces a decaying train of repeats")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);
    auto mod = registry.create("dly.dm2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f); // 160 ms
    mod->setParameter("feedback", 0.8f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    const float r1 = echoPeak(mod, 160.0f, 48000.0);
    const float r2 = echoPeak(mod, 320.0f, 48000.0);
    const float r3 = echoPeak(mod, 480.0f, 48000.0);

    REQUIRE(r1 > r2);
    REQUIRE(r2 > r3);
    REQUIRE(r3 > 0.05f);
}

TEST_CASE("dm2 delay level zero leaves only the dry signal")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);
    auto mod = registry.create("dly.dm2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f);
    mod->setParameter("feedback", 0.0f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    REQUIRE(peakOf(out, kBurstStart - 500, kBurstStart + 500) > 0.4f);
    // no wet content at the echo position
    REQUIRE(peakOf(out, kBurstStart + 6600, kBurstStart + 8600) < 1e-5f);
}

TEST_CASE("dm2 delay time maps the full 20-300 ms range")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);

    auto echoAtTime = [&registry](float time) {
        auto mod = registry.create("dly.dm2");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("time", time);
        mod->setParameter("feedback", 0.0f);
        mod->setParameter("level", 1.0f);
        return echoPeak(mod, 20.0f + 280.0f * time, 48000.0);
    };

    REQUIRE(echoAtTime(0.0f) > 0.2f); // ~20 ms
    REQUIRE(echoAtTime(1.0f) > 0.2f); // ~300 ms
}

TEST_CASE("dm2 delay wet path darkens the repeats")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);

    auto wetGain = [&registry](float freq) {
        auto mod = registry.create("dly.dm2");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("time", 0.5f);
        mod->setParameter("feedback", 0.0f);
        mod->setParameter("level", 1.0f);

        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        fillBurst(in, freq, 0.5f, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        const std::size_t delay = 7680;
        return peakOf(out, kBurstStart + delay - 1000, kBurstStart + delay + 1000) / 0.5f;
    };

    // the ~3.5 kHz wet low-pass leaves 1 kHz nearly intact but cuts 6 kHz
    REQUIRE(wetGain(1000.0f) > 0.8f);
    REQUIRE(wetGain(6000.0f) < 0.5f * wetGain(1000.0f));
}

TEST_CASE("dm2 delay parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);
    auto mod = registry.create("dly.dm2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float times[] = {0.0f, 0.5f, 1.0f};
    const float feedbacks[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float time : times) {
        for (float feedback : feedbacks) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("time", time);
                mod->setParameter("feedback", feedback);
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

TEST_CASE("dm2 delay works at 44.1k and 96k sample rates")
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
        namfx::registerDm2Delay(registry);
        auto mod = registry.create("dly.dm2");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("time", 0.5f);
        mod->setParameter("feedback", 0.5f);
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

TEST_CASE("dm2 delay process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerDm2Delay(registry);
    auto mod = registry.create("dly.dm2");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f);
    mod->setParameter("feedback", 0.5f);
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
