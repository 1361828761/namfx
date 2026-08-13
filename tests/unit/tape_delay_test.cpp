#include "modules/dsp/tape_delay.h"
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

// echo peak amplitude in a window around start + delayMs (tolerance covers
// wow); buffer long enough for the 660 ms max delay plus repeat windows
float echoPeak(std::unique_ptr<namfx::ModuleBase>& mod, float delayMs, double sampleRate)
{
    const std::size_t delay = static_cast<std::size_t>(delayMs * sampleRate / 1000.0);
    const std::size_t lo = kBurstStart + delay - 1500;
    const std::size_t hi = kBurstStart + delay + 1500;
    std::vector<float> in(60000, 0.0f);
    std::vector<float> out(60000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, sampleRate);

    mod->reset();
    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    return peakOf(out, lo, hi);
}

} // namespace

TEST_CASE("tape delay registers under unique module id with four params")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);
    REQUIRE(registry.has("dly.tape"));
    REQUIRE(registry.categoryOf("dly.tape") == "pedal");
    REQUIRE(registry.specsFor("dly.tape").size() == 4);
    REQUIRE(registry.findParam("dly.tape", "time") != nullptr);
    REQUIRE(registry.findParam("dly.tape", "echo") != nullptr);
    REQUIRE(registry.findParam("dly.tape", "tone") != nullptr);
    REQUIRE(registry.findParam("dly.tape", "level") != nullptr);

    auto mod = registry.create("dly.tape");
    REQUIRE(mod != nullptr);
}

TEST_CASE("tape delay echoes the input after the delay time")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);
    auto mod = registry.create("dly.tape");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f); // 360 ms
    mod->setParameter("echo", 0.0f);
    mod->setParameter("tone", 1.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    REQUIRE(peakOf(out, kBurstStart - 500, kBurstStart + 500) > 0.4f);
    REQUIRE(echoPeak(mod, 360.0f, 48000.0) > 0.2f);
}

TEST_CASE("tape delay echo feedback produces a decaying train of repeats")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);
    auto mod = registry.create("dly.tape");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f); // 360 ms
    mod->setParameter("echo", 0.8f);
    mod->setParameter("tone", 1.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    const float r1 = echoPeak(mod, 360.0f, 48000.0);
    const float r2 = echoPeak(mod, 720.0f, 48000.0);
    const float r3 = echoPeak(mod, 1080.0f, 48000.0);

    REQUIRE(r1 > r2);
    REQUIRE(r2 > r3);
    REQUIRE(r3 > 0.05f);
}

TEST_CASE("tape delay level zero leaves only the dry signal")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);
    auto mod = registry.create("dly.tape");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f);
    mod->setParameter("echo", 0.0f);
    mod->setParameter("tone", 0.5f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillBurst(in, 1000.0f, 0.5f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    REQUIRE(peakOf(out, kBurstStart - 500, kBurstStart + 500) > 0.4f);
    REQUIRE(peakOf(out, kBurstStart + 15000, kBurstStart + 20000) < 1e-5f);
}

TEST_CASE("tape delay time maps the full 60-660 ms range")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);

    auto echoAtTime = [&registry](float time) {
        auto mod = registry.create("dly.tape");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("time", time);
        mod->setParameter("echo", 0.0f);
        mod->setParameter("tone", 1.0f);
        mod->setParameter("level", 1.0f);
        return echoPeak(mod, 60.0f + 600.0f * time, 48000.0);
    };

    REQUIRE(echoAtTime(0.0f) > 0.2f); // ~60 ms
    REQUIRE(echoAtTime(1.0f) > 0.2f); // ~660 ms
}

TEST_CASE("tape delay tone controls the wet brightness")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);

    auto wetGain = [&registry](float tone, float freq) {
        auto mod = registry.create("dly.tape");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("time", 0.5f);
        mod->setParameter("echo", 0.0f);
        mod->setParameter("tone", tone);
        mod->setParameter("level", 1.0f);

        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        fillBurst(in, freq, 0.5f, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        const std::size_t delay = 17280;
        return peakOf(out, kBurstStart + delay - 1500, kBurstStart + delay + 1500) / 0.5f;
    };

    // dark tone (3 kHz corner) cuts 6 kHz much harder than bright tone (8 kHz)
    REQUIRE(wetGain(1.0f, 1000.0f) > 0.3f);
    REQUIRE(wetGain(0.0f, 6000.0f) < wetGain(1.0f, 6000.0f));
    REQUIRE(wetGain(1.0f, 6000.0f) > 0.4f);
}

TEST_CASE("tape delay saturation compresses loud repeats")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);

    auto wetGainAt = [&registry](float amp) {
        auto mod = registry.create("dly.tape");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("time", 0.5f);
        mod->setParameter("echo", 0.0f);
        mod->setParameter("tone", 1.0f);
        mod->setParameter("level", 1.0f);

        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        fillBurst(in, 1000.0f, amp, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        const std::size_t delay = 17280;
        return peakOf(out, kBurstStart + delay - 1500, kBurstStart + delay + 1500) / amp;
    };

    // unity-ish at low level, compressed at high level
    REQUIRE(wetGainAt(0.2f) > 0.8f);
    REQUIRE(wetGainAt(0.95f) < wetGainAt(0.2f));
}

TEST_CASE("tape delay parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);
    auto mod = registry.create("dly.tape");
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
    const float echos[] = {0.0f, 0.5f, 1.0f};
    const float tones[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float time : times) {
        for (float echo : echos) {
            for (float tone : tones) {
                for (float level : levels) {
                    mod->reset();
                    mod->setParameter("time", time);
                    mod->setParameter("echo", echo);
                    mod->setParameter("tone", tone);
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
}

TEST_CASE("tape delay works at 44.1k and 96k sample rates")
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
        namfx::registerTapeDelay(registry);
        auto mod = registry.create("dly.tape");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("time", 0.5f);
        mod->setParameter("echo", 0.5f);
        mod->setParameter("tone", 0.5f);
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

TEST_CASE("tape delay process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerTapeDelay(registry);
    auto mod = registry.create("dly.tape");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("time", 0.5f);
    mod->setParameter("echo", 0.5f);
    mod->setParameter("tone", 0.5f);
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
