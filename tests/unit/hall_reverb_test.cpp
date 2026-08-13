#include "modules/dsp/hall_reverb.h"
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
    auto mod = registry.create("rvb.hall");
    REQUIRE(mod != nullptr);
    mod->prepare(48000.0, 64);
    return mod;
}

} // namespace

TEST_CASE("hall reverb registers under unique module id with three params")
{
    namfx::ModuleRegistry registry;
    namfx::registerHallReverb(registry);
    REQUIRE(registry.has("rvb.hall"));
    REQUIRE(registry.categoryOf("rvb.hall") == "pedal");
    REQUIRE(registry.specsFor("rvb.hall").size() == 3);
    REQUIRE(registry.findParam("rvb.hall", "room") != nullptr);
    REQUIRE(registry.findParam("rvb.hall", "damp") != nullptr);
    REQUIRE(registry.findParam("rvb.hall", "mix") != nullptr);
    REQUIRE(makeModule(registry) != nullptr);
}

TEST_CASE("hall reverb turns an impulse into a long decaying tail")
{
    namfx::ModuleRegistry registry;
    namfx::registerHallReverb(registry);

    auto tailAt = [&registry](float room) {
        auto mod = makeModule(registry);
        mod->setParameter("room", room);
        mod->setParameter("damp", 0.2f);
        mod->setParameter("mix", 1.0f);

        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        in[kImpulseAt] = 1.0f;

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

        const double early = energyOf(out, kImpulseAt + 1500, kImpulseAt + 10000);
        const double late = energyOf(out, kImpulseAt + 25000, kImpulseAt + 45000);
        return std::make_pair(early, late);
    };

    // room 0.3 (fb 0.51): tail dies within ~200 ms; room 0.9 (fb 0.93):
    // tail still rings past 500 ms
    const auto small = tailAt(0.3f);
    const auto big = tailAt(0.9f);

    REQUIRE(big.first > small.first);
    REQUIRE(big.second > small.second * 5.0);
    REQUIRE(big.second > 0.0);
}

TEST_CASE("hall reverb damp controls the high-frequency tail length")
{
    namfx::ModuleRegistry registry;
    namfx::registerHallReverb(registry);

    auto tailEnergy = [&registry](float damp) {
        auto mod = makeModule(registry);
        mod->setParameter("room", 0.9f);
        mod->setParameter("damp", damp);
        mod->setParameter("mix", 1.0f);

        // 4 kHz burst: the in-loop damping low-pass corner rises with damp
        // (damp 0.3 -> ~1 kHz, damp 1.0 -> ~3.9 kHz), so more 4 kHz energy
        // recirculates when damp is high
        std::vector<float> in(48000, 0.0f);
        std::vector<float> out(48000, 0.0f);
        constexpr double kTwoPi = 6.28318530717958647692;
        for (std::size_t i = 0; i < 480; ++i) {
            in[kImpulseAt + i] = 0.5f
                * static_cast<float>(std::sin(kTwoPi * 4000.0 * i / 48000.0));
        }

        float dummyR = 0.0f;
        mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));
        return energyOf(out, kImpulseAt + 5000, kImpulseAt + 30000);
    };

    REQUIRE(tailEnergy(1.0f) > tailEnergy(0.3f) * 2.0);
}

TEST_CASE("hall reverb mix zero leaves only the dry signal")
{
    namfx::ModuleRegistry registry;
    namfx::registerHallReverb(registry);
    auto mod = makeModule(registry);
    mod->setParameter("room", 0.9f);
    mod->setParameter("damp", 0.5f);
    mod->setParameter("mix", 0.0f);

    std::vector<float> in(48000, 0.0f);
    std::vector<float> out(48000, 0.0f);
    in[kImpulseAt] = 1.0f;

    float dummyR = 0.0f;
    mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

    REQUIRE(peakOf(out, kImpulseAt - 10, kImpulseAt + 10) > 0.5);
    REQUIRE(energyOf(out, kImpulseAt + 2000, kImpulseAt + 40000) < 1e-5);
}

TEST_CASE("hall reverb parameter sweep stays finite and bounded")
{
    namfx::ModuleRegistry registry;
    namfx::registerHallReverb(registry);
    auto mod = makeModule(registry);

    std::vector<float> in(48000, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) {
        const float t = static_cast<float>(i);
        in[i] = 0.5f * std::sin(0.13f * t) + 0.3f * std::sin(0.037f * t) + 0.2f * std::sin(0.007f * t);
    }
    std::vector<float> out(48000, 0.0f);
    float dummyR = 0.0f;

    const float rooms[] = {0.0f, 0.5f, 1.0f};
    const float damps[] = {0.0f, 0.5f, 1.0f};
    const float mixes[] = {0.0f, 0.5f, 1.0f};

    for (float room : rooms) {
        for (float damp : damps) {
            for (float mix : mixes) {
                mod->reset();
                mod->setParameter("room", room);
                mod->setParameter("damp", damp);
                mod->setParameter("mix", mix);
                mod->process(in.data(), &dummyR, out.data(), &dummyR, static_cast<int>(in.size()));

                for (float v : out) {
                    REQUIRE(std::isfinite(v));
                }
                REQUIRE(peakOf(out, 0, out.size()) < 20.0f);
            }
        }
    }
}

TEST_CASE("hall reverb works at 44.1k and 96k sample rates")
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
        namfx::registerHallReverb(registry);
        auto mod = registry.create("rvb.hall");
        REQUIRE(mod != nullptr);
        mod->prepare(rate, 64);
        mod->setParameter("room", 0.6f);
        mod->setParameter("damp", 0.5f);
        mod->setParameter("mix", 0.5f);

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

TEST_CASE("hall reverb process is allocation free in audio callback")
{
    namfx::ModuleRegistry registry;
    namfx::registerHallReverb(registry);
    auto mod = makeModule(registry);
    mod->setParameter("room", 0.6f);
    mod->setParameter("damp", 0.5f);
    mod->setParameter("mix", 0.5f);

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
