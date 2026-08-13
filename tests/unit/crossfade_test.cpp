#include "audio/chain.h"
#include "audio/slot.h"
#include "test_registry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

std::vector<namfx::audio::SlotDef> makeSlot(int index, std::string impl, float gainDb,
                                            float mix = 1.0f)
{
    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = index;
    def.category = "pedal";
    def.impl = std::move(impl);
    def.moduleId = "gain";
    def.params.push_back(namfx::ParamInit{"gain", gainDb});
    def.mix = mix;
    slots.push_back(std::move(def));
    return slots;
}

std::vector<float> constant(int n, float value)
{
    return std::vector<float>(static_cast<std::size_t>(n), value);
}

constexpr float kSixDb = 1.99526231f;

} // namespace

TEST_CASE("bypass crossfade settles on the dry signal without a click")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain(makeSlot(0, "dsp", 6.0f), registry);
    chain.prepare(48000.0, 256);

    constexpr int n = 256;
    const std::vector<float> in = constant(n, 1.0f);
    std::vector<float> out(static_cast<std::size_t>(n));
    std::vector<float> scratch(static_cast<std::size_t>(n));
    chain.process(in.data(), in.data(), out.data(), scratch.data(), n);
    REQUIRE(out[static_cast<std::size_t>(n - 1)] == Catch::Approx(kSixDb).epsilon(1e-4));

    chain.setBypass(0, true);
    chain.process(in.data(), in.data(), out.data(), scratch.data(), n);

    float maxDelta = 0.0f;
    float previous = out[0];
    for (int i = 1; i < n; ++i) {
        maxDelta = std::max(maxDelta, std::fabs(out[static_cast<std::size_t>(i)] - previous));
        previous = out[static_cast<std::size_t>(i)];
    }
    REQUIRE(maxDelta < 0.03f);
    REQUIRE(out[static_cast<std::size_t>(n - 1)] == Catch::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("dsp bypass crossfade completes within 1ms and ir within 5ms")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain dspChain(makeSlot(0, "dsp", 6.0f), registry);
    dspChain.prepare(48000.0, 64);
    dspChain.setBypass(0, true);

    std::vector<float> out(64, 0.0f);
    std::vector<float> scratch(64, 0.0f);
    const std::vector<float> in = constant(64, 1.0f);
    dspChain.process(in.data(), in.data(), out.data(), scratch.data(), 64);
    REQUIRE(out[63] == Catch::Approx(1.0f).epsilon(1e-4));

    namfx::audio::Chain irChain(makeSlot(0, "ir", 6.0f), registry);
    irChain.prepare(48000.0, 256);
    irChain.setBypass(0, true);
    std::vector<float> irOut(256, 0.0f);
    std::vector<float> irScratch(256, 0.0f);
    const std::vector<float> longIn = constant(256, 1.0f);
    irChain.process(longIn.data(), longIn.data(), irOut.data(), irScratch.data(), 256);
    REQUIRE(irOut[239] == Catch::Approx(1.0f + (kSixDb - 1.0f) / 240.0f).epsilon(1e-3));
    REQUIRE(irOut[255] == Catch::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("mix control interpolates between dry and wet")
{
    auto registry = testx::makeRegistry();

    namfx::audio::Chain dryChain(makeSlot(0, "dsp", 6.0f, 0.0f), registry);
    dryChain.prepare(48000.0, 128);
    namfx::audio::Chain wetChain(makeSlot(0, "dsp", 6.0f, 1.0f), registry);
    wetChain.prepare(48000.0, 128);
    namfx::audio::Chain halfChain(makeSlot(0, "dsp", 6.0f, 0.5f), registry);
    halfChain.prepare(48000.0, 128);

    constexpr int n = 128;
    const std::vector<float> in = constant(n, 1.0f);
    std::vector<float> out(static_cast<std::size_t>(n));
    std::vector<float> scratch(static_cast<std::size_t>(n));

    dryChain.process(in.data(), in.data(), out.data(), scratch.data(), n);
    REQUIRE(out.back() == Catch::Approx(1.0f).epsilon(1e-4));

    wetChain.process(in.data(), in.data(), out.data(), scratch.data(), n);
    REQUIRE(out.back() == Catch::Approx(kSixDb).epsilon(1e-4));

    halfChain.process(in.data(), in.data(), out.data(), scratch.data(), n);
    REQUIRE(out.back() == Catch::Approx((1.0f + kSixDb) * 0.5f).epsilon(1e-4));
}

TEST_CASE("re-engaging bypass crossfades back to wet without a click")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain(makeSlot(0, "dsp", 6.0f), registry);
    chain.prepare(48000.0, 256);

    constexpr int n = 256;
    const std::vector<float> in = constant(n, 1.0f);
    std::vector<float> out(static_cast<std::size_t>(n));
    std::vector<float> scratch(static_cast<std::size_t>(n));

    chain.setBypass(0, true);
    chain.process(in.data(), in.data(), out.data(), scratch.data(), n);
    REQUIRE(out.back() == Catch::Approx(1.0f).epsilon(1e-4));

    chain.setBypass(0, false);
    chain.process(in.data(), in.data(), out.data(), scratch.data(), n);
    REQUIRE(out.back() == Catch::Approx(kSixDb).epsilon(1e-4));
}
