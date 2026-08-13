#include "audio/param_store.h"
#include "audio/spsc_queue.h"
#include "modules/module_base.h"
#include "modules/module_registry.h"
#include "platform/rt_alloc.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

class SafetyModule final : public namfx::ModuleBase {
public:
    void prepare(double, int) override {}
    void process(const float*, const float*, float*, float*, int) override {}
    void reset() override {}
    void setSampleRate(double) override {}
    void setMaxBlock(int) override {}
    namfx::ChannelMode channelMode() const override { return namfx::ChannelMode::MonoInMonoOut; }
};

} // namespace

TEST_CASE("module registry create is load path and never flags audio callback")
{
    namfx::ModuleRegistry registry;
    REQUIRE(registry.registerModule("safety.module", "pedal", {},
                                    [] { return std::make_unique<SafetyModule>(); }));

    namfx::rt::AllocCounter::reset_violation();
    std::unique_ptr<namfx::ModuleBase> module = registry.create("safety.module");
    REQUIRE(module != nullptr);
    REQUIRE_FALSE(namfx::rt::AllocCounter::violation);
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("param store set advance and get are allocation free in audio callback")
{
    std::vector<namfx::ParamSpec> specs;
    specs.push_back(namfx::ParamSpec{"gain", "Gain", -20.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear});
    namfx::ParamStore store(specs);
    store.setSampleRate(48000.0);
    const std::string id("gain");

    {
        namfx::rt::ScopedAllocGuard guard;

        store.set(id, 6.0f);
        store.advance(256);
        const float current = store.get(id);
        const float pending = store.target(id);

        REQUIRE(current > 0.0f);
        REQUIRE(current < 6.0f);
        REQUIRE(pending == 6.0f);
        REQUIRE_FALSE(guard.violated());
    }

    REQUIRE(store.isRamping());
    store.advance(480);
    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get(id) == 6.0f);
}

TEST_CASE("param store setImmediate is allocation free in audio callback")
{
    namfx::ParamStore store({namfx::ParamSpec{"gain", "Gain", -20.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear}});
    store.setSampleRate(48000.0);
    const std::string id("gain");

    {
        namfx::rt::ScopedAllocGuard guard;

        store.setImmediate(id, -9.0f);
        store.advance(64);
        const float current = store.get(id);

        REQUIRE(current == -9.0f);
        REQUIRE_FALSE(guard.violated());
    }
}

TEST_CASE("spsc queue push and pop are allocation free in audio callback")
{
    namfx::SpscQueue<int, 64> q;

    {
        namfx::rt::ScopedAllocGuard guard;

        for (int i = 0; i < 32; ++i) {
            REQUIRE(q.push(i));
        }
        int out = 0;
        int count = 0;
        while (q.pop(out)) {
            ++count;
        }

        REQUIRE(count == 32);
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
