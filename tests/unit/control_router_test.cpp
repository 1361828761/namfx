#include "audio/chain.h"
#include "audio/control_router.h"
#include "platform/rt_alloc.h"
#include "test_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

using namfx::audio::Chain;
using namfx::audio::ControlRouter;

std::shared_ptr<const namfx::ModuleRegistry> registry()
{
    return testx::makeRegistry();
}

Chain makeChain()
{
    using namespace namfx::audio;
    std::vector<SlotDef> slots;
    SlotDef g;
    g.slot = 0;
    g.category = "pedal";
    g.impl = "dsp";
    g.moduleId = "gain";
    g.params.push_back(namfx::ParamInit{"gain", 0.5f});
    slots.push_back(g);
    Chain chain(std::move(slots), registry());
    chain.prepare(48000.0, 64);
    return chain;
}

void runBlocks(Chain& chain, ControlRouter& router, int blocks, int blockSize = 64)
{
    std::vector<float> in(static_cast<std::size_t>(blockSize), 0.1f);
    std::vector<float> inR(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> outL(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(blockSize), 0.0f);
    for (int b = 0; b < blocks; ++b) {
        router.apply(chain, blockSize);
        chain.process(in.data(), inR.data(), outL.data(), outR.data(), blockSize);
    }
}

} // namespace

TEST_CASE("control router binds and rejects conflicting or invalid binds")
{
    Chain chain = makeChain();
    ControlRouter router;
    REQUIRE(router.bind(chain, 1, "gain", "gain"));
    REQUIRE(router.isBound("gain", "gain"));
    REQUIRE(router.boundCount() == 1);
    // same param, different source: rejected
    REQUIRE_FALSE(router.bind(chain, 2, "gain", "gain"));
    // same source rebind: ok
    REQUIRE(router.bind(chain, 1, "gain", "gain"));
    // unknown module / param / source
    REQUIRE_FALSE(router.bind(chain, 1, "nope", "x"));
    REQUIRE_FALSE(router.bind(chain, 1, "gain", "nope"));
    REQUIRE_FALSE(router.bind(chain, 99, "gain", "gain"));
    router.unbind(chain, "gain", "gain");
    REQUIRE_FALSE(router.isBound("gain", "gain"));
    REQUIRE(router.boundCount() == 0);
}

TEST_CASE("bound parameter follows the control source value")
{
    Chain chain = makeChain();
    ControlRouter router;
    REQUIRE(router.bind(chain, 1, "gain", "gain"));
    router.setSourceValue(1, 12.0f); // +12 dB
    runBlocks(chain, router, 200);
    REQUIRE(router.isBound("gain", "gain"));
    // the chain must be driving the gain module at the source value; verify
    // indirectly: set source to -12 dB, render, compare loudness vs +12 dB
    auto peak = [&](float v) {
        router.setSourceValue(1, v);
        runBlocks(chain, router, 200);
        std::vector<float> in(64, 0.1f);
        std::vector<float> inR(64, 0.0f);
        std::vector<float> outL(64, 0.0f);
        std::vector<float> outR(64, 0.0f);
        router.apply(chain, 64);
        chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
        float p = 0.0f;
        for (float x : outL) {
            p = std::max(p, std::fabs(x));
        }
        return p;
    };
    const float loud = peak(12.0f);
    const float quiet = peak(-12.0f);
    REQUIRE(loud > quiet * 5.0f); // +12 vs -12 dB: ~16x, gain follows source
}

TEST_CASE("bound parameter eases back to the queued UI value after hang time")
{
    Chain chain = makeChain();
    ControlRouter router;
    REQUIRE(router.bind(chain, 1, "gain", "gain"));
    router.setSourceValue(1, 12.0f); // +12 dB from the source
    runBlocks(chain, router, 100);
    // UI writes while the source is active are queued (deep-1)
    REQUIRE(router.uiSet("gain", "gain", -12.0f));
    runBlocks(chain, router, 100);
    // stop the source (no more heartbeats) and let the hang time elapse:
    // 300 ms at 48k = 14400 frames; run 400 blocks of 64 = 25600 frames
    runBlocks(chain, router, 400);
    // after the fallback the gain target is the queued UI value (-12 dB):
    // 0.1 input * 10^(-12/20) = 0.02512
    std::vector<float> in(64, 0.1f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(64, 0.0f);
    std::vector<float> outR(64, 0.0f);
    router.apply(chain, 64);
    chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
    float chainedPeak = 0.0f;
    for (float x : outL) {
        chainedPeak = std::max(chainedPeak, std::fabs(x));
    }
    REQUIRE(std::fabs(chainedPeak - 0.1f * 0.25119f) < 0.01f);
}

TEST_CASE("unbound UI writes apply immediately")
{
    Chain chain = makeChain();
    ControlRouter router;
    REQUIRE(router.uiSet("gain", "gain", 12.0f)); // +12 dB
    runBlocks(chain, router, 10);
    // chain gain should now be +12 dB: 0.1 * 3.98 = 0.398
    std::vector<float> in(64, 0.1f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(64, 0.0f);
    std::vector<float> outR(64, 0.0f);
    router.apply(chain, 64);
    chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
    float peak = 0.0f;
    for (float x : outL) {
        peak = std::max(peak, std::fabs(x));
    }
    REQUIRE(peak > 0.3f); // 0.1 input * 3.98
}

TEST_CASE("control router apply is allocation free on the audio thread")
{
    Chain chain = makeChain();
    ControlRouter router;
    REQUIRE(router.bind(chain, 1, "gain", "gain"));
    router.setSourceValue(1, 0.7f);
    router.uiSet("gain", "gain", 0.3f);

    std::vector<float> in(64, 0.1f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(64, 0.0f);
    std::vector<float> outR(64, 0.0f);
    {
        namfx::rt::ScopedAllocGuard guard;
        for (int b = 0; b < 50; ++b) {
            router.apply(chain, 64);
        }
        {
            namfx::rt::ScopedAllocGuard seg;
            chain.setParamByIndex(0, 0, 0.9f);
            REQUIRE_FALSE(seg.violated());
        }
        REQUIRE_FALSE(guard.violated());
    }
}

TEST_CASE("unbound UI bypass applies at the next block boundary")
{
    Chain chain = makeChain();
    ControlRouter router;
    REQUIRE(router.uiSet("gain", "gain", 12.0f)); // +12 dB, loud path
    runBlocks(chain, router, 10);
    // bypass the gain module: output must fall back to the dry input
    REQUIRE(router.uiSetBypass("gain", true));
    runBlocks(chain, router, 200); // covers the 1 ms fade
    std::vector<float> in(64, 0.1f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(64, 0.0f);
    std::vector<float> outR(64, 0.0f);
    router.apply(chain, 64);
    chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
    float peak = 0.0f;
    for (float x : outL) {
        peak = std::max(peak, std::fabs(x));
    }
    REQUIRE(std::fabs(peak - 0.1f) < 0.01f); // dry passthrough
    // unbypass restores the loud path
    REQUIRE(router.uiSetBypass("gain", false));
    runBlocks(chain, router, 200);
    router.apply(chain, 64);
    chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
    peak = 0.0f;
    for (float x : outL) {
        peak = std::max(peak, std::fabs(x));
    }
    REQUIRE(peak > 0.3f); // 0.1 * 3.98
}

TEST_CASE("uiSetBypass queues commands and never allocates on the audio thread")
{
    Chain chain = makeChain();
    ControlRouter router;
    // module existence is validated by the host (which owns the chain); the
    // router only queues, and apply() silently drops unknown modules
    REQUIRE(router.uiSetBypass("nope", true));
    REQUIRE(router.uiSetBypass("gain", true));
    std::vector<float> in(64, 0.1f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(64, 0.0f);
    std::vector<float> outR(64, 0.0f);
    {
        namfx::rt::ScopedAllocGuard guard;
        for (int b = 0; b < 50; ++b) {
            router.apply(chain, 64);
        }
        REQUIRE_FALSE(guard.violated());
    }
}

#ifdef NAMFX_RT_ALLOC_ENABLED
#endif
