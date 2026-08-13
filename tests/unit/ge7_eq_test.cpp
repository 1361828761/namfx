#include "modules/dsp/ge7_eq.h"
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

// steady-state gain at one frequency
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

TEST_CASE("ge7 eq registers under unique module id with eight params")
{
    namfx::ModuleRegistry registry;
    namfx::registerGe7Eq(registry);
    REQUIRE(registry.has("eq.ge7"));
    REQUIRE(registry.categoryOf("eq.ge7") == "pedal");
    REQUIRE(registry.specsFor("eq.ge7").size() == 8);
    for (const char* id : {"band100", "band200", "band400", "band800",
                           "band1600", "band3200", "band6400", "level"}) {
        REQUIRE(registry.findParam("eq.ge7", id) != nullptr);
    }

    auto mod = registry.create("eq.ge7");
    REQUIRE(mod != nullptr);
}

TEST_CASE("ge7 eq is flat at default settings")
{
    namfx::ModuleRegistry registry;
    namfx::registerGe7Eq(registry);
    auto mod = registry.create("eq.ge7");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    for (float f = 60.0f; f <= 8000.0f; f *= 1.7f) {
        const float g = gainAt(mod, f, 48000.0);
        REQUIRE(g > 0.95f);
        REQUIRE(g < 1.05f);
    }
}

TEST_CASE("ge7 eq band boost lifts its own frequency region")
{
    namfx::ModuleRegistry registry;
    namfx::registerGe7Eq(registry);

    auto withBand = [&registry](const char* id, float value) {
        auto mod = registry.create("eq.ge7");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter(id, value);
        return mod;
    };

    // 6.4 kHz shelf boost lifts the highs; the RBJ shelf reaches its
    // midpoint (sqrt(A)) at f0 and approaches the full gain above it
    auto hi = withBand("band6400", 1.0f);
    const float g200Hi = gainAt(hi, 200.0f, 48000.0);
    const float g6400Hi = gainAt(hi, 6400.0f, 48000.0);
    const float g12kHi = gainAt(hi, 12000.0f, 48000.0);
    REQUIRE(g6400Hi > 1.3f * g200Hi);
    REQUIRE(g12kHi > g6400Hi);
    REQUIRE(g12kHi > 2.0f * g200Hi);

    // 100 Hz peaking boost lifts the lows
    auto lo = withBand("band100", 1.0f);
    REQUIRE(gainAt(lo, 100.0f, 48000.0) > 2.0f * gainAt(lo, 1000.0f, 48000.0));
}

TEST_CASE("ge7 eq band cut dips its own frequency region")
{
    namfx::ModuleRegistry registry;
    namfx::registerGe7Eq(registry);
    auto mod = registry.create("eq.ge7");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("band800", 0.0f); // -15 dB

    // 800 Hz cut dips only its own region; 400 Hz and 1.6 kHz (outside the
    // Q = 1.1 dip bandwidth) stay flat
    const float g400 = gainAt(mod, 400.0f, 48000.0);
    const float g800 = gainAt(mod, 800.0f, 48000.0);
    const float g1600 = gainAt(mod, 1600.0f, 48000.0);
    REQUIRE(g800 < 0.4f * g400);
    REQUIRE(g800 < 0.4f * g1600);
}

TEST_CASE("ge7 eq level scales the whole output")
{
    namfx::ModuleRegistry registry;
    namfx::registerGe7Eq(registry);

    auto gainAtLevel = [&registry](float level) {
        auto mod = registry.create("eq.ge7");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("level", level);
        return gainAt(mod, 800.0f, 48000.0);
    };

    const float low = gainAtLevel(0.0f); // -15 dB
    const float high = gainAtLevel(1.0f); // +15 dB

    REQUIRE(low < 0.3f);
    REQUIRE(high > 3.0f);
    REQUIRE(high > low * 10.0f);
}

TEST_CASE("ge7 eq parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerGe7Eq(registry);
    auto mod = registry.create("eq.ge7");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const char* ids[8] = {"band100", "band200", "band400", "band800",
                          "band1600", "band3200", "band6400", "level"};
    const float values[3] = {0.0f, 0.5f, 1.0f};
    for (int round = 0; round < 6; ++round) {
        mod->reset();
        for (int k = 0; k < 8; ++k) {
            mod->setParameter(ids[k], values[(round + k) % 3]);
        }
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
        REQUIRE(peakOf(out, 0, out.size()) < 60.0f);
    }
}

TEST_CASE("ge7 eq works at 44.1k and 96k sample rates")
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
        namfx::registerGe7Eq(registry);
        auto mod = registry.create("eq.ge7");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("band3200", 0.7f);
        mod->setParameter("band6400", 0.3f);
        mod->setParameter("level", 0.5f);

        std::vector<float> out(48000, 0.0f);
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        for (float v : out) {
            REQUIRE(std::isfinite(v));
        }
        const float peak = peakOf(out, 24000, out.size());
        REQUIRE(peak > 0.0f);
        REQUIRE(peak < 60.0f);
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("ge7 eq process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerGe7Eq(registry);
    auto mod = registry.create("eq.ge7");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("band800", 0.7f);
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
