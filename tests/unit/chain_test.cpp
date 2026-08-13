#include "audio/chain.h"
#include "audio/slot.h"
#include "platform/rt_alloc.h"
#include "test_registry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(int n, double freq, double sampleRate, float amplitude)
{
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        out[static_cast<std::size_t>(i)] = amplitude
            * static_cast<float>(std::sin(2.0 * kPi * freq * static_cast<double>(i) / sampleRate));
    }
    return out;
}

double rms(const std::vector<float>& v)
{
    double sum = 0.0;
    for (float s : v) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}

std::vector<namfx::audio::SlotDef> gainSlot(int index, float gainDb, bool bypass = false,
                                            float mix = 1.0f)
{
    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = index;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "gain";
    def.params.push_back(namfx::ParamInit{"gain", gainDb});
    def.bypass = bypass;
    def.mix = mix;
    slots.push_back(std::move(def));
    return slots;
}

void render(namfx::audio::Chain& chain, const std::vector<float>& input, std::vector<float>& outL,
            std::vector<float>& outR, int blockSize)
{
    const int n = static_cast<int>(input.size());
    outL.assign(static_cast<std::size_t>(n), 0.0f);
    outR.assign(static_cast<std::size_t>(n), 0.0f);
    for (int offset = 0; offset < n; offset += blockSize) {
        const int count = std::min(blockSize, n - offset);
        std::vector<float> blockInR(static_cast<std::size_t>(count), 0.0f);
        chain.process(input.data() + offset, blockInR.data(), outL.data() + offset,
                      outR.data() + offset, count);
    }
}

} // namespace

TEST_CASE("gain module scales a sine by the expected dB")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain(gainSlot(0, 6.0f), registry);
    chain.prepare(48000.0, 256);

    constexpr int n = 4800;
    const std::vector<float> in = sine(n, 440.0, 48000.0, 0.5f);
    std::vector<float> outL;
    std::vector<float> outR;
    render(chain, in, outL, outR, 256);

    const std::vector<float> tail(in.end() - 480, in.end());
    const std::vector<float> outTail(outL.end() - 480, outL.end());
    const double expected = rms(tail) * 1.99526231;
    REQUIRE(rms(outTail) == Catch::Approx(expected).epsilon(1e-4));
}

TEST_CASE("tone lowpass attenuates a high frequency more than a low frequency")
{
    auto registry = testx::makeRegistry();

    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "tone";
    def.params.push_back(namfx::ParamInit{"freq", 200.0f});
    def.params.push_back(namfx::ParamInit{"mode", 0.0f});
    slots.push_back(std::move(def));

    namfx::audio::Chain chain(std::move(slots), registry);
    chain.prepare(48000.0, 256);

    constexpr int n = 4800;
    const std::vector<float> lowIn = sine(n, 80.0, 48000.0, 1.0f);
    const std::vector<float> highIn = sine(n, 440.0, 48000.0, 1.0f);
    std::vector<float> lowL;
    std::vector<float> highL;
    std::vector<float> scratch;
    render(chain, lowIn, lowL, scratch, 256);
    render(chain, highIn, highL, scratch, 256);

    const double lowPass = rms(std::vector<float>(lowL.end() - 480, lowL.end()));
    const double highPass = rms(std::vector<float>(highL.end() - 480, highL.end()));
    REQUIRE(highPass < lowPass * 0.7);
    REQUIRE(highPass > 0.0);
}

TEST_CASE("mono in mono out module duplicates the left channel processing to the right")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain(gainSlot(0, 0.0f), registry);
    chain.prepare(48000.0, 256);

    constexpr int n = 256;
    const std::vector<float> inL = sine(n, 440.0, 48000.0, 0.5f);
    std::vector<float> inR(static_cast<std::size_t>(n), 0.25f);
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));
    chain.process(inL.data(), inR.data(), outL.data(), outR.data(), n);

    for (int i = 0; i < n; ++i) {
        REQUIRE(outL[static_cast<std::size_t>(i)] == outR[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("stereo in stereo out module receives both channels")
{
    auto registry = testx::makeRegistry();
    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "stereo.passthrough";
    slots.push_back(std::move(def));

    namfx::audio::Chain chain(std::move(slots), registry);
    chain.prepare(48000.0, 256);

    constexpr int n = 256;
    const std::vector<float> inL = sine(n, 440.0, 48000.0, 0.5f);
    std::vector<float> inR(static_cast<std::size_t>(n), 0.25f);
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));
    chain.process(inL.data(), inR.data(), outL.data(), outR.data(), n);

    for (int i = 0; i < n; ++i) {
        REQUIRE(outL[static_cast<std::size_t>(i)] == inL[static_cast<std::size_t>(i)]);
        REQUIRE(outR[static_cast<std::size_t>(i)] == inR[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("empty chain is a passthrough")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain({}, registry);
    chain.prepare(48000.0, 256);

    constexpr int n = 128;
    const std::vector<float> inL = sine(n, 440.0, 48000.0, 0.5f);
    std::vector<float> inR(static_cast<std::size_t>(n), 0.25f);
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));
    chain.process(inL.data(), inR.data(), outL.data(), outR.data(), n);

    for (int i = 0; i < n; ++i) {
        REQUIRE(outL[static_cast<std::size_t>(i)] == inL[static_cast<std::size_t>(i)]);
        REQUIRE(outR[static_cast<std::size_t>(i)] == inR[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("chain rejects unknown module id and unknown param id")
{
    auto registry = testx::makeRegistry();
    REQUIRE_THROWS_AS(namfx::audio::Chain(gainSlot(0, 0.0f), nullptr), std::runtime_error);

    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "does.not.exist";
    slots.push_back(std::move(def));
    REQUIRE_THROWS_AS(namfx::audio::Chain(std::move(slots), registry), std::runtime_error);

    std::vector<namfx::audio::SlotDef> badParam;
    namfx::audio::SlotDef def2;
    def2.slot = 0;
    def2.category = "pedal";
    def2.impl = "dsp";
    def2.moduleId = "gain";
    def2.params.push_back(namfx::ParamInit{"not.a.param", 0.0f});
    badParam.push_back(std::move(def2));
    REQUIRE_THROWS_AS(namfx::audio::Chain(std::move(badParam), registry), std::runtime_error);
}

TEST_CASE("chain rejects duplicate and out of range slot indices")
{
    auto registry = testx::makeRegistry();
    std::vector<namfx::audio::SlotDef> dup;
    namfx::audio::SlotDef a;
    a.slot = 1;
    a.category = "pedal";
    a.impl = "dsp";
    a.moduleId = "gain";
    dup.push_back(a);
    dup.push_back(a);
    REQUIRE_THROWS_AS(namfx::audio::Chain(std::move(dup), registry), std::runtime_error);

    std::vector<namfx::audio::SlotDef> outOfRange;
    namfx::audio::SlotDef b;
    b.slot = 8;
    b.category = "pedal";
    b.impl = "dsp";
    b.moduleId = "gain";
    outOfRange.push_back(std::move(b));
    REQUIRE_THROWS_AS(namfx::audio::Chain(std::move(outOfRange), registry), std::runtime_error);
}

TEST_CASE("prepare is idempotent")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain(gainSlot(0, 3.0f), registry);
    chain.prepare(48000.0, 512);

    constexpr int n = 512;
    const std::vector<float> in = sine(n, 440.0, 48000.0, 0.5f);
    std::vector<float> firstL(static_cast<std::size_t>(n));
    std::vector<float> scratch(static_cast<std::size_t>(n));
    chain.process(in.data(), in.data(), firstL.data(), scratch.data(), n);

    chain.prepare(48000.0, 512);
    std::vector<float> secondL(static_cast<std::size_t>(n));
    chain.process(in.data(), in.data(), secondL.data(), scratch.data(), n);

    for (int i = 0; i < n; ++i) {
        REQUIRE(firstL[static_cast<std::size_t>(i)] == secondL[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("chain process is allocation free in the audio callback")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain(gainSlot(0, 3.0f), registry);
    chain.prepare(48000.0, 512);

    constexpr int n = 256;
    std::vector<float> in(static_cast<std::size_t>(n), 0.1f);
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));

    namfx::rt::ScopedAllocGuard guard;
    chain.process(in.data(), in.data(), outL.data(), outR.data(), n);
    REQUIRE_FALSE(guard.violated());
}

TEST_CASE("reset restores a chain to its initial state")
{
    auto registry = testx::makeRegistry();
    namfx::audio::Chain chain(gainSlot(0, 0.0f), registry);
    chain.prepare(48000.0, 512);

    constexpr int n = 512;
    const std::vector<float> in = sine(n, 440.0, 48000.0, 0.5f);
    std::vector<float> baseline(static_cast<std::size_t>(n));
    std::vector<float> scratch(static_cast<std::size_t>(n));
    chain.process(in.data(), in.data(), baseline.data(), scratch.data(), n);

    chain.process(in.data(), in.data(), scratch.data(), scratch.data(), n);
    chain.reset();

    std::vector<float> after(static_cast<std::size_t>(n));
    chain.process(in.data(), in.data(), after.data(), scratch.data(), n);
    for (int i = 0; i < n; ++i) {
        REQUIRE(after[static_cast<std::size_t>(i)] == baseline[static_cast<std::size_t>(i)]);
    }
}
