#include "modules/dsp/wah.h"
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

// steady-state gain at one frequency with fixed position/resonance
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

// peak (centre) frequency of the resonant band-pass at the current params
float centreFrequency(std::unique_ptr<namfx::ModuleBase>& mod, double sampleRate)
{
    float bestFreq = 0.0f;
    float bestGain = -1.0f;
    for (float f = 150.0f; f <= 6000.0f; f *= 1.06f) {
        const float g = gainAt(mod, f, sampleRate);
        if (g > bestGain) {
            bestGain = g;
            bestFreq = f;
        }
    }
    return bestFreq;
}

} // namespace

TEST_CASE("wah registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerWah(registry);
    REQUIRE(registry.has("mod.wah"));
    REQUIRE(registry.categoryOf("mod.wah") == "pedal");
    REQUIRE(registry.specsFor("mod.wah").size() == 3);
    REQUIRE(registry.findParam("mod.wah", "position") != nullptr);
    REQUIRE(registry.findParam("mod.wah", "resonance") != nullptr);
    REQUIRE(registry.findParam("mod.wah", "level") != nullptr);

    auto mod = registry.create("mod.wah");
    REQUIRE(mod != nullptr);
}

TEST_CASE("wah position sweeps the resonant peak up in frequency")
{
    namfx::ModuleRegistry registry;
    namfx::registerWah(registry);

    auto centreAt = [&registry](float position) {
        auto mod = registry.create("mod.wah");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("position", position);
        mod->setParameter("resonance", 0.6f);
        mod->setParameter("level", 1.0f);
        return centreFrequency(mod, 48000.0);
    };

    const float low = centreAt(0.25f);
    const float mid = centreAt(0.5f);
    const float high = centreAt(0.75f);

    // centre frequency climbs with pedal position (monotone sweep)
    REQUIRE(low > 0.0f);
    REQUIRE(mid > low * 1.2f);
    REQUIRE(high > mid * 1.2f);
}

TEST_CASE("wah resonance boosts the resonant peak")
{
    namfx::ModuleRegistry registry;
    namfx::registerWah(registry);

    auto peakGainAt = [&registry](float resonance) {
        auto mod = registry.create("mod.wah");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("position", 0.5f);
        mod->setParameter("resonance", resonance);
        mod->setParameter("level", 1.0f);
        float best = -1.0f;
        for (float f = 150.0f; f <= 6000.0f; f *= 1.06f) {
            best = std::max(best, gainAt(mod, f, 48000.0));
        }
        return best;
    };

    const float flat = peakGainAt(0.0f); // Q = 1: no boost
    const float ringing = peakGainAt(1.0f); // Q = 10: strong boost

    REQUIRE(flat < 1.6f);
    REQUIRE(flat > 0.8f);
    REQUIRE(ringing > flat * 2.0f);
}

TEST_CASE("wah level attenuates the output")
{
    namfx::ModuleRegistry registry;
    namfx::registerWah(registry);
    auto mod = registry.create("mod.wah");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("position", 0.5f);
    mod->setParameter("resonance", 0.5f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(24000, 0.0f);
    std::vector<float> out(24000, 0.0f);
    fillSine(in, 800.0f, 0.3f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    REQUIRE(peakOf(out, 20000, 24000) < 1e-5f);
}

TEST_CASE("wah parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerWah(registry);
    auto mod = registry.create("mod.wah");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float positions[] = {0.0f, 0.5f, 1.0f};
    const float resonances[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float position : positions) {
        for (float resonance : resonances) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("position", position);
                mod->setParameter("resonance", resonance);
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

TEST_CASE("wah works at 44.1k and 96k sample rates")
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
        namfx::registerWah(registry);
        auto mod = registry.create("mod.wah");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("position", 0.5f);
        mod->setParameter("resonance", 0.5f);
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

TEST_CASE("wah process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerWah(registry);
    auto mod = registry.create("mod.wah");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("position", 0.5f);
    mod->setParameter("resonance", 0.5f);
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
