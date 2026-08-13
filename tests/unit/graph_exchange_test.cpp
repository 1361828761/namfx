#include "audio/audio_graph.h"
#include "audio/chain.h"
#include "audio/slot.h"
#include "modules/dsp/gain.h"
#include "modules/module_registry.h"
#include "test_registry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

namespace {

std::vector<namfx::audio::SlotDef> gainSlot(float gainDb)
{
    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "gain";
    def.params.push_back(namfx::ParamInit{"gain", gainDb});
    slots.push_back(std::move(def));
    return slots;
}

std::unique_ptr<namfx::audio::Chain> makeChain(
    const std::shared_ptr<const namfx::ModuleRegistry>& registry, float gainDb)
{
    auto chain = std::make_unique<namfx::audio::Chain>(gainSlot(gainDb), registry);
    chain->prepare(48000.0, 64);
    return chain;
}

// module whose destructor records that it ran, to prove retired chains are
// not destroyed inside processBlock (audio thread)
class DestructorFlagModule final : public namfx::ModuleBase {
public:
    explicit DestructorFlagModule(std::atomic<bool>& flag) : flag_(flag) {}
    ~DestructorFlagModule() override
    {
        flag_.store(true, std::memory_order_relaxed);
    }

    void prepare(double, int) override {}
    void process(const float* inL, const float*, float* outL, float*, int n) override
    {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
    }
    void reset() override {}
    void setSampleRate(double) override {}
    void setMaxBlock(int) override {}
    namfx::ChannelMode channelMode() const override
    {
        return namfx::ChannelMode::MonoInMonoOut;
    }

private:
    std::atomic<bool>& flag_;
};

std::shared_ptr<const namfx::ModuleRegistry> flagRegistry(std::atomic<bool>& flag)
{
    auto registry = std::make_shared<namfx::ModuleRegistry>();
    namfx::registerGain(*registry);
    registry->registerModule("test.destructor_flag", "pedal", {},
                              [&flag] { return std::make_unique<DestructorFlagModule>(flag); });
    return registry;
}

std::unique_ptr<namfx::audio::Chain> makeFlagChain(
    const std::shared_ptr<const namfx::ModuleRegistry>& registry)
{
    std::vector<namfx::audio::SlotDef> slots;
    namfx::audio::SlotDef def;
    def.slot = 0;
    def.category = "pedal";
    def.impl = "dsp";
    def.moduleId = "test.destructor_flag";
    slots.push_back(std::move(def));
    auto chain = std::make_unique<namfx::audio::Chain>(std::move(slots), registry);
    chain->prepare(48000.0, 64);
    return chain;
}

constexpr float kSixDb = 1.99526231f;

} // namespace

TEST_CASE("empty graph passes stereo samples exactly")
{
    namfx::audio::AudioGraph graph;

    constexpr int n = 1024;
    std::vector<float> inL(static_cast<std::size_t>(n));
    std::vector<float> inR(static_cast<std::size_t>(n));
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));

    for (int i = 0; i < n; ++i) {
        inL[static_cast<std::size_t>(i)] = static_cast<float>(i % 7) / 7.0f;
        inR[static_cast<std::size_t>(i)] = static_cast<float>(-(i % 5)) / 5.0f;
    }

    graph.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);

    for (int i = 0; i < n; ++i) {
        INFO("sample " << i);
        REQUIRE(outL[static_cast<std::size_t>(i)] == inL[static_cast<std::size_t>(i)]);
        REQUIRE(outR[static_cast<std::size_t>(i)] == inR[static_cast<std::size_t>(i)]);
    }
}

TEST_CASE("requestSwap takes effect at the next block boundary")
{
    auto registry = testx::makeRegistry();
    namfx::audio::AudioGraph graph;

    constexpr int n = 64;
    const std::vector<float> in(static_cast<std::size_t>(n), 1.0f);
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));

    graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
    REQUIRE(outL[0] == 1.0f);

    REQUIRE_FALSE(graph.hasPending());
    graph.requestSwap(makeChain(registry, 6.0f));
    REQUIRE(graph.hasPending());

    graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
    REQUIRE_FALSE(graph.hasPending());
    REQUIRE(outL[0] == Catch::Approx(kSixDb).epsilon(1e-4));

    graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
    REQUIRE(outL[0] == Catch::Approx(kSixDb).epsilon(1e-4));
}

TEST_CASE("concurrent swap while processing always sees a consistent chain")
{
    auto registry = testx::makeRegistry();
    namfx::audio::AudioGraph graph;

    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};

    std::thread reader([&] {
        constexpr int n = 64;
        std::vector<float> in(static_cast<std::size_t>(n), 1.0f);
        std::vector<float> outL(static_cast<std::size_t>(n));
        std::vector<float> outR(static_cast<std::size_t>(n));
        while (!stop.load(std::memory_order_acquire)) {
            graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
            for (float s : outL) {
                const bool ok = std::fabs(s - 1.0f) < 1e-4f || std::fabs(s - kSixDb) < 1e-4f;
                if (!ok) {
                    bad.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    });

    for (int i = 0; i < 5000; ++i) {
        graph.requestSwap(makeChain(registry, i % 2 == 0 ? 0.0f : 6.0f));
    }

    stop.store(true, std::memory_order_release);
    reader.join();

    REQUIRE(bad.load() == 0);
}

TEST_CASE("retired chain is destroyed by the control thread, not inside processBlock")
{
    std::atomic<bool> destroyed{false};
    auto registry = flagRegistry(destroyed);
    namfx::audio::AudioGraph graph;

    constexpr int n = 64;
    const std::vector<float> in(static_cast<std::size_t>(n), 1.0f);
    std::vector<float> outL(static_cast<std::size_t>(n));
    std::vector<float> outR(static_cast<std::size_t>(n));

    graph.requestSwap(makeChain(registry, 0.0f));
    graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);

    // swap the flag chain in: it becomes live, the previous chain retires
    graph.requestSwap(makeFlagChain(registry));
    graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
    REQUIRE_FALSE(destroyed.load());

    // next swap displaces the plain chain, not the flag chain
    graph.requestSwap(makeChain(registry, 6.0f));
    graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
    REQUIRE_FALSE(destroyed.load());

    // now the flag chain is displaced into the retire slot
    graph.requestSwap(makeChain(registry, 0.0f));
    graph.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);
    REQUIRE_FALSE(destroyed.load());

    // the next control-thread request drains the retired chain
    graph.requestSwap(makeChain(registry, 6.0f));
    REQUIRE(destroyed.load());
}
