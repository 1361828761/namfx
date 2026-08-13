#include "modules/dsp/octave.h"
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

// Goertzel power at one frequency over the tail
double binPower(const std::vector<float>& buf, std::size_t begin, std::size_t end, float freq,
                double sampleRate)
{
    const double w0 = 6.28318530717958647692 * freq / sampleRate;
    const double c = 2.0 * std::cos(w0);
    double s0 = 0.0;
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        s0 = buf[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

} // namespace

TEST_CASE("octave registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerOctave(registry);
    REQUIRE(registry.has("pitch.octave"));
    REQUIRE(registry.categoryOf("pitch.octave") == "pedal");
    REQUIRE(registry.specsFor("pitch.octave").size() == 3);
    REQUIRE(registry.findParam("pitch.octave", "mix") != nullptr);
    REQUIRE(registry.findParam("pitch.octave", "tone") != nullptr);
    REQUIRE(registry.findParam("pitch.octave", "level") != nullptr);

    auto mod = registry.create("pitch.octave");
    REQUIRE(mod != nullptr);
}

TEST_CASE("octave halves the fundamental frequency")
{
    namfx::ModuleRegistry registry;
    namfx::registerOctave(registry);
    auto mod = registry.create("pitch.octave");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("mix", 1.0f); // pure sub-octave
    mod->setParameter("tone", 1.0f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    // the alternating-flip waveform has its fundamental at 220 Hz and no
    // even harmonics (440 Hz), only odd ones (220/660/...)
    const double e220 = binPower(out, 24000, 48000, 220.0f, 48000.0);
    const double e440 = binPower(out, 24000, 48000, 440.0f, 48000.0);

    REQUIRE(e220 > 1.0);
    REQUIRE(e220 > e440 * 3.0);
}

TEST_CASE("octave mix zero passes the dry signal unchanged")
{
    namfx::ModuleRegistry registry;
    namfx::registerOctave(registry);
    auto mod = registry.create("pitch.octave");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("mix", 0.0f);
    mod->setParameter("tone", 0.5f);
    mod->setParameter("level", 1.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    const double e440 = binPower(out, 24000, 48000, 440.0f, 48000.0);
    const double e220 = binPower(out, 24000, 48000, 220.0f, 48000.0);
    REQUIRE(e440 > 1.0);
    REQUIRE(e220 < e440 * 0.05);
}

TEST_CASE("octave tone shapes the sub-octave harmonics")
{
    namfx::ModuleRegistry registry;
    namfx::registerOctave(registry);

    auto thirdHarmonic = [&registry](float tone) {
        auto mod = registry.create("pitch.octave");
        REQUIRE(mod != nullptr);
        mod->prepare(48000.0, 64);
        mod->setParameter("mix", 1.0f);
        mod->setParameter("tone", tone);
        mod->setParameter("level", 1.0f);

        // 880 Hz in -> 440 Hz sub with odd harmonics (1320 Hz = 3rd)
        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        fillSine(in, 880.0f, 0.4f, 48000.0);

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        return binPower(out, 24000, 48000, 1320.0f, 48000.0);
    };

    // dark tone (300 Hz corner) cuts the 1320 Hz harmonic, bright keeps it
    REQUIRE(thirdHarmonic(1.0f) > 1.0);
    REQUIRE(thirdHarmonic(1.0f) > thirdHarmonic(0.0f) * 3.0);
}

TEST_CASE("octave level attenuates the output")
{
    namfx::ModuleRegistry registry;
    namfx::registerOctave(registry);
    auto mod = registry.create("pitch.octave");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("mix", 0.5f);
    mod->setParameter("tone", 0.5f);
    mod->setParameter("level", 0.0f);

    std::vector<float> in(24000, 0.0f);
    std::vector<float> out(24000, 0.0f);
    fillSine(in, 440.0f, 0.4f, 48000.0);

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
    REQUIRE(peakOf(out, 20000, 24000) < 1e-5f);
}

TEST_CASE("octave parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerOctave(registry);
    auto mod = registry.create("pitch.octave");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float mixes[] = {0.0f, 0.5f, 1.0f};
    const float tones[] = {0.0f, 0.5f, 1.0f};
    const float levels[] = {0.0f, 0.5f, 1.0f};

    for (float mix : mixes) {
        for (float tone : tones) {
            for (float level : levels) {
                mod->reset();
                mod->setParameter("mix", mix);
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

TEST_CASE("octave works at 44.1k and 96k sample rates")
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
        namfx::registerOctave(registry);
        auto mod = registry.create("pitch.octave");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("mix", 0.5f);
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

TEST_CASE("octave process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerOctave(registry);
    auto mod = registry.create("pitch.octave");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    mod->setParameter("mix", 0.5f);
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
