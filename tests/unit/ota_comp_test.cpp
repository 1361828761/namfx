#include "modules/dsp/ota_comp.h"
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

float peakOf(const std::vector<float>& buf, std::size_t begin = 0)
{
    float peak = 0.0f;
    for (std::size_t i = begin; i < buf.size(); ++i) {
        peak = std::max(peak, std::fabs(buf[i]));
    }
    return peak;
}

float rmsOf(const std::vector<float>& buf, std::size_t begin = 0, std::size_t end = 0)
{
    if (end == 0 || end > buf.size()) {
        end = buf.size();
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(buf[i]) * buf[i];
        ++count;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

void fillSine(std::vector<float>& buf, float freq, float amp, double sampleRate)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = amp * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / sampleRate));
    }
}

float runComp(std::unique_ptr<namfx::ModuleBase>& mod, float amp, double sampleRate,
              float sustain, float attack, float ratio, float level)
{
    mod->reset();
    mod->setParameter("sustain", sustain);
    mod->setParameter("attack", attack);
    mod->setParameter("ratio", ratio);
    mod->setParameter("level", level);

    std::vector<float> in(65536, 0.0f);
    std::vector<float> out(65536, 0.0f);
    fillSine(in, 440.0f, amp, sampleRate);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    return rmsOf(out, 48000);
}

} // namespace

TEST_CASE("ota comp registers under unique module id with four params")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    REQUIRE(registry.has("comp.ota"));
    REQUIRE(registry.categoryOf("comp.ota") == "pedal");
    REQUIRE(registry.specsFor("comp.ota").size() == 4);
    REQUIRE(registry.findParam("comp.ota", "sustain") != nullptr);
    REQUIRE(registry.findParam("comp.ota", "attack") != nullptr);
    REQUIRE(registry.findParam("comp.ota", "ratio") != nullptr);
    REQUIRE(registry.findParam("comp.ota", "level") != nullptr);

    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
}

TEST_CASE("ota comp compresses loud signals much more than quiet ones")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const float quietOut = runComp(mod, 0.01f, 48000.0, 0.5f, 0.5f, 1.0f, 0.5f);
    const float loudOut = runComp(mod, 0.3f, 48000.0, 0.5f, 0.5f, 1.0f, 0.5f);

    // input ratio is 30x, compressed output ratio must be far smaller (>=10:1)
    REQUIRE(quietOut > 0.0f);
    REQUIRE(loudOut > 0.0f);
    REQUIRE(loudOut / quietOut < 10.0f);
    REQUIRE(loudOut / quietOut < 5.0f);
}

TEST_CASE("ota comp sustain increases compression monotonically")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const float s0 = runComp(mod, 0.3f, 48000.0, 0.0f, 0.5f, 1.0f, 0.5f);
    const float s1 = runComp(mod, 0.3f, 48000.0, 0.5f, 0.5f, 1.0f, 0.5f);
    const float s2 = runComp(mod, 0.3f, 48000.0, 1.0f, 0.5f, 1.0f, 0.5f);

    REQUIRE(s0 > s1);
    REQUIRE(s1 > s2);
    REQUIRE(s0 > s2 * 3.0f);
}

TEST_CASE("ota comp ratio blends dry and compressed signal")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const float dry = runComp(mod, 0.3f, 48000.0, 0.8f, 0.5f, 0.0f, 0.5f);
    const float wet = runComp(mod, 0.3f, 48000.0, 0.8f, 0.5f, 1.0f, 0.5f);

    // ratio = 0 passes the dry (uncompressed) signal through
    REQUIRE(dry > wet * 2.0f);
}

TEST_CASE("ota comp attack controls envelope speed")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    // step input: fast attack (0) should reach deep compression quickly
    auto stepPeak = [&mod](float attack, std::size_t window) {
        mod->reset();
        mod->setParameter("sustain", 0.8f);
        mod->setParameter("attack", attack);
        mod->setParameter("ratio", 1.0f);
        mod->setParameter("level", 0.5f);
        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        std::vector<float> warm(48000, 0.0f);
        float dummyR = 0.0f;
        // settle the parameter smoothing before the step
        mod->process(warm.data(), &dummyR, warm.data(), &dummyR, static_cast<int>(warm.size()));
        for (std::size_t i = 1000; i < in.size(); ++i) {
            in[i] = 0.5f;
        }
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        // transient window right after the step, before both envelopes settle
        return rmsOf(out, 1000, 1000 + window);
    };

    const float fast = stepPeak(0.0f, 4000);
    const float slow = stepPeak(1.0f, 4000);

    // within the transient window the fast attack is deeply compressed,
    // the slow attack (200 ms) has barely started
    REQUIRE(slow > fast * 2.0f);
}

TEST_CASE("ota comp level scales output")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const float low = runComp(mod, 0.1f, 48000.0, 0.5f, 0.5f, 1.0f, 0.1f);
    const float high = runComp(mod, 0.1f, 48000.0, 0.5f, 0.5f, 1.0f, 0.9f);

    REQUIRE(high > low * 30.0f);
    REQUIRE(high < low * 200.0f);
}

TEST_CASE("ota comp parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float sustains[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float attacks[] = {0.0f, 0.5f, 1.0f};
    const float ratios[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float sustain : sustains) {
        for (float attack : attacks) {
            for (float ratio : ratios) {
                for (float level : levels) {
                    mod->reset();
                    mod->setParameter("sustain", sustain);
                    mod->setParameter("attack", attack);
                    mod->setParameter("ratio", ratio);
                    mod->setParameter("level", level);
                    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                    for (float v : out) {
                        REQUIRE(std::isfinite(v));
                    }
                    REQUIRE(peakOf(out) < 20.0f);
                }
            }
        }
    }
}

TEST_CASE("ota comp works at 44.1k and 96k sample rates")
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
        namfx::registerOtaComp(registry);
        auto mod = registry.create("comp.ota");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("sustain", 0.5f);
        mod->setParameter("attack", 0.5f);
        mod->setParameter("ratio", 0.5f);
        mod->setParameter("level", 0.5f);

        std::vector<float> out(48000, 0.0f);
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
        const float peak = peakOf(out);
        REQUIRE(peak > 0.0f);
        REQUIRE(peak < 20.0f);

        const float quietOut = runComp(mod, 0.01f, rate, 0.5f, 0.5f, 1.0f, 0.5f);
        const float loudOut = runComp(mod, 0.3f, rate, 0.5f, 0.5f, 1.0f, 0.5f);
        REQUIRE(loudOut / quietOut < 5.0f);
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("ota comp process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerOtaComp(registry);
    auto mod = registry.create("comp.ota");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("sustain", 0.5f);
    mod->setParameter("attack", 0.5f);
    mod->setParameter("ratio", 0.5f);
    mod->setParameter("level", 0.5f);

    std::vector<float> in(512, 0.5f);
    std::vector<float> out(512, 0.0f);
    float dummyR = 0.0f;

    {
        namfx::rt::ScopedAllocGuard guard;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
