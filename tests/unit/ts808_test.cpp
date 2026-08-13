#include "modules/dsp/ts808.h"
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

float peakOf(const std::vector<float>& buf)
{
    float peak = 0.0f;
    for (float v : buf) {
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

void fillSine(std::vector<float>& buf, float freq, float amp, double sampleRate)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = amp * static_cast<float>(std::sin(kTwoPi * freq * static_cast<double>(i) / sampleRate));
    }
}

// gain of the clipping stage at a given drive, measured on the final module
// output with a small-signal sine at the given frequency.
float driveGain(std::unique_ptr<namfx::ModuleBase>& mod, float drive, float freq, double sampleRate,
                float tone = 5.0f, float level = 0.0f)
{
    mod->reset();
    mod->setParameter("drive", drive);
    mod->setParameter("tone", tone);
    mod->setParameter("level", level);

    std::vector<float> in(4096, 0.0f);
    std::vector<float> out(4096, 0.0f);
    fillSine(in, freq, 0.001f, sampleRate);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    constexpr std::size_t kSettle = 512;
    float peakIn = 0.0f;
    float peakOut = 0.0f;
    for (std::size_t i = kSettle; i < in.size(); ++i) {
        peakIn = std::max(peakIn, std::fabs(in[i]));
        peakOut = std::max(peakOut, std::fabs(out[i]));
    }
    return peakOut / peakIn;
}

} // namespace

TEST_CASE("ts808 registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    REQUIRE(registry.has("od.ts808"));
    REQUIRE(registry.categoryOf("od.ts808") == "pedal");
    REQUIRE(registry.specsFor("od.ts808").size() == 3);
    REQUIRE(registry.findParam("od.ts808", "drive") != nullptr);
    REQUIRE(registry.findParam("od.ts808", "tone") != nullptr);
    REQUIRE(registry.findParam("od.ts808", "level") != nullptr);

    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
}

TEST_CASE("ts808 clipping stage gain rises with drive in the 12x-118x range")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const float gMin = driveGain(mod, 0.0f, 1000.0f, 48000.0);
    const float gMax = driveGain(mod, 10.0f, 1000.0f, 48000.0);

    // nominal drive gain range is 1 + (51k + D*500k) / 4.7k ~= 12..118;
    // reactive elements (47nF in Zi, 51pF in Zf) bend the measured ratio,
    // so assert a generous monotone window rather than the ideal numbers.
    REQUIRE(gMin >= 4.0f);
    REQUIRE(gMin <= 20.0f);
    REQUIRE(gMax >= 25.0f);
    REQUIRE(gMax <= 150.0f);
    REQUIRE(gMax > gMin * 2.0f);
}

TEST_CASE("ts808 drive gain is monotonically increasing")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    float previous = 0.0f;
    for (float drive = 0.0f; drive <= 10.0f; drive += 0.5f) {
        const float g = driveGain(mod, drive, 1000.0f, 48000.0);
        REQUIRE(g > previous);
        previous = g;
    }
}

TEST_CASE("ts808 clipping stage soft-clips large input")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("drive", 10.0f);
    mod->setParameter("tone", 5.0f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(8192, 0.0f);
    std::vector<float> out(8192, 0.0f);
    fillSine(in, 440.0f, 1.0f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // diode pair clamps the feedback node to ~+-0.6V: the raw stage output is
    // bounded well below the input amplitude, and the waveform is not a sine.
    const float peak = peakOf(out);
    REQUIRE(peak > 0.0f);
    REQUIRE(peak < 0.6f);
}

TEST_CASE("ts808 tone stage rolls off treble at tone=0 but not at tone=10")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const float low1k = driveGain(mod, 5.0f, 1000.0f, 48000.0);

    const float rolloff = driveGain(mod, 5.0f, 8000.0f, 48000.0, 0.0f);
    const float rolloff1k = driveGain(mod, 5.0f, 1000.0f, 48000.0, 0.0f);

    const float treble = driveGain(mod, 5.0f, 8000.0f, 48000.0, 10.0f);

    REQUIRE(rolloff1k > low1k * 0.5f);
    REQUIRE(rolloff < rolloff1k * 0.5f);
    REQUIRE(treble > rolloff * 4.0f);
}

TEST_CASE("ts808 level parameter applies gain in dB")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    const float base = driveGain(mod, 1.0f, 440.0f, 48000.0);

    const float plus6 = driveGain(mod, 1.0f, 440.0f, 48000.0, 5.0f, 6.0f);
    const float minus6 = driveGain(mod, 1.0f, 440.0f, 48000.0, 5.0f, -6.0f);

    REQUIRE(plus6 > base * 1.9f);
    REQUIRE(plus6 < base * 2.1f);
    REQUIRE(minus6 > base * 0.45f);
    REQUIRE(minus6 < base * 0.55f);
}

TEST_CASE("ts808 parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float drives[] = {0.0f, 1.0f, 2.5f, 5.0f, 7.5f, 10.0f};
    const float tones[] = {0.0f, 5.0f, 10.0f};
    const float levels[] = {-60.0f, -20.0f, 0.0f, 6.0f, 12.0f};

    for (float drive : drives) {
        for (float tone : tones) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("drive", drive);
                mod->setParameter("tone", tone);
                mod->setParameter("level", level);
                mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                for (float v : out) {
                    REQUIRE(std::isfinite(v));
                }
                REQUIRE(peakOf(out) < 100.0f);
            }
        }
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("ts808 process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerTs808(registry);
    auto mod = registry.create("od.ts808");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("drive", 5.0f);
    mod->setParameter("tone", 5.0f);
    mod->setParameter("level", 0.0f);

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
