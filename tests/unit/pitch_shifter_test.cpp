#include "modules/dsp/pitch_shifter.h"
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

// fundamental frequency estimated from zero crossings of the tail
float zcFreq(const std::vector<float>& buf, std::size_t begin, std::size_t end, double sampleRate)
{
    int crossings = 0;
    for (std::size_t i = begin + 1; i < end; ++i) {
        if ((buf[i - 1] < 0.0f) != (buf[i] < 0.0f)) {
            ++crossings;
        }
    }
    return static_cast<float>(crossings) * static_cast<float>(sampleRate)
        / (2.0f * static_cast<float>(end - begin));
}

} // namespace

TEST_CASE("pitch shifter registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    REQUIRE(registry.has("pitch.shift"));
    REQUIRE(registry.categoryOf("pitch.shift") == "pedal");
    REQUIRE(registry.specsFor("pitch.shift").size() == 3);
    REQUIRE(registry.findParam("pitch.shift", "shift") != nullptr);
    REQUIRE(registry.findParam("pitch.shift", "mix") != nullptr);
    REQUIRE(registry.findParam("pitch.shift", "level") != nullptr);

    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
}

TEST_CASE("pitch shifter shifts up one octave")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("shift", 1.0f); // +12 semitones
    mod->setParameter("mix", 1.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(96000, 0.0f);
    std::vector<float> out(96000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    const float f = zcFreq(out, 48000, 96000, 48000.0);
    REQUIRE(f > 830.0f);
    REQUIRE(f < 930.0f);
}

TEST_CASE("pitch shifter shifts down one octave")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("shift", 0.0f); // -12 semitones
    mod->setParameter("mix", 1.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(96000, 0.0f);
    std::vector<float> out(96000, 0.0f);
    fillSine(in, 880.0f, 0.4f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    const float f = zcFreq(out, 48000, 96000, 48000.0);
    REQUIRE(f > 410.0f);
    REQUIRE(f < 470.0f);
}

TEST_CASE("pitch shifter at unity shift preserves the pitch")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("shift", 0.5f); // 0 semitones
    mod->setParameter("mix", 1.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(96000, 0.0f);
    std::vector<float> out(96000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    const float f = zcFreq(out, 48000, 96000, 48000.0);
    REQUIRE(f > 420.0f);
    REQUIRE(f < 460.0f);
}

TEST_CASE("pitch shifter at unity shift delays by one crossfade window")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("shift", 0.5f);
    mod->setParameter("mix", 1.0f);
    mod->setParameter("level", 1.0f);

    // 10 ms burst at 1 kHz; the shifted path is a 20 ms delayed copy
    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillSine(in, 1000.0f, 0.5f, 48000.0);
    for (std::size_t i = 960; i < in.size(); ++i) {
        in[i] = 0.0f;
    }

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // dry=0 at mix 1: output is the burst delayed by 20 ms (960 samples)
    REQUIRE(peakOf(out, 800, 1150) > 0.4f);
    REQUIRE(peakOf(out, 2000, 4000) < 0.01f);
}

TEST_CASE("pitch shifter mix blends dry and shifted")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("shift", 0.5f);
    mod->setParameter("mix", 0.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    in[10000] = 1.0f; // after the 10 ms parameter smoothing has settled

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    REQUIRE(peakOf(out, 9990, 10010) > 0.9f);
    REQUIRE(peakOf(out, 12000, 40000) < 1e-5f);
}

TEST_CASE("pitch shifter level attenuates the output")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("shift", 0.5f);
    mod->setParameter("mix", 1.0f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(24000, 0.0f);
    std::vector<float> out(24000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    REQUIRE(peakOf(out, 20000, 24000) < 1e-5f);
}

TEST_CASE("pitch shifter parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float shifts[] = {0.0f, 0.5f, 1.0f};
    const float mixes[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float shift : shifts) {
        for (float mix : mixes) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("shift", shift);
                mod->setParameter("mix", mix);
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

TEST_CASE("pitch shifter works at 44.1k and 96k sample rates")
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
        namfx::registerPitchShifter(registry);
        auto mod = registry.create("pitch.shift");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("shift", 0.7f);
        mod->setParameter("mix", 0.5f);
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

TEST_CASE("pitch shifter process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerPitchShifter(registry);
    auto mod = registry.create("pitch.shift");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("shift", 0.5f);
    mod->setParameter("mix", 0.5f);
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
