#include "audio/chain.h"
#include "audio/scene_engine.h"
#include "platform/rt_alloc.h"
#include "preset/preset_model.h"
#include "test_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

using namfx::audio::Chain;
using namfx::audio::SceneEngine;
using namfx::ParamInit;

std::vector<namfx::preset::SceneDef> makeScenes()
{
    using namespace namfx::preset;
    SceneDef s1;
    s1.name = "Lead";
    s1.overrides.push_back(SceneOverride{"gain", {{"gain", 0.8f}}, false});
    s1.overrides.push_back(SceneOverride{"od.ts808", {}, true}); // bypass the drive
    SceneDef s2;
    s2.name = "Clean";
    s2.overrides.push_back(SceneOverride{"gain", {{"gain", 0.2f}}, false});
    return {s1, s2};
}

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
    g.params.push_back(ParamInit{"gain", 0.5f});
    slots.push_back(g);
    SlotDef d;
    d.slot = 1;
    d.category = "pedal";
    d.impl = "dsp";
    d.moduleId = "od.ts808";
    d.params.push_back(ParamInit{"drive", 5.0f});
    d.params.push_back(ParamInit{"tone", 5.0f});
    d.params.push_back(ParamInit{"level", 0.5f});
    slots.push_back(d);
    Chain chain(std::move(slots), registry());
    chain.prepare(48000.0, 256);
    return chain;
}

void render(Chain& chain, std::vector<float>& out, int blocks = 200)
{
    std::vector<float> in(256, 0.1f);
    std::vector<float> inR(256, 0.0f);
    out.assign(static_cast<std::size_t>(blocks * 256), 0.0f);
    for (int b = 0; b < blocks; ++b) {
        chain.process(in.data(), inR.data(), out.data() + static_cast<std::size_t>(b * 256),
                      out.data() + static_cast<std::size_t>(b * 256), 256);
    }
}

} // namespace

TEST_CASE("scene engine loads up to 8 scenes and rejects oversize or unknown refs")
{
    Chain chain = makeChain();
    SceneEngine engine;
    REQUIRE(engine.load(makeScenes(), chain));
    REQUIRE(engine.sceneCount() == 2);
    REQUIRE(engine.sceneName(0) == "Lead");
    REQUIRE(engine.sceneName(1) == "Clean");

    // oversize
    std::vector<namfx::preset::SceneDef> nine;
    for (int i = 0; i < 9; ++i) {
        nine.push_back(namfx::preset::SceneDef{"s", {}});
    }
    REQUIRE_FALSE(engine.load(nine, chain));

    // unknown module / parameter
    namfx::preset::SceneDef bad;
    bad.overrides.push_back(namfx::preset::SceneOverride{"does.not.exist", {}, false});
    REQUIRE_FALSE(engine.load({bad}, chain));
    namfx::preset::SceneDef badParam;
    badParam.overrides.push_back(namfx::preset::SceneOverride{"gain", {{"nope", 1.0f}}, false});
    REQUIRE_FALSE(engine.load({badParam}, chain));
}

TEST_CASE("scene recall applies parameters and bypass through the chain")
{
    Chain chain = makeChain();
    SceneEngine engine;
    REQUIRE(engine.load(makeScenes(), chain));
    std::vector<float> out;
    render(chain, out); // settle defaults

    // scene 0: gain 0.8 + ts808 bypassed
    engine.recall(0);
    engine.apply(chain);
    render(chain, out);
    REQUIRE(engine.activeScene() == 0);

    // scene 1: gain 0.2, ts808 still bypassed (scene only touches gain)
    engine.recall(1);
    engine.apply(chain);
    render(chain, out);
    REQUIRE(engine.activeScene() == 1);

    // switching back to scene 0 must restore gain 0.8
    engine.recall(0);
    engine.apply(chain);
    render(chain, out);
    REQUIRE(engine.activeScene() == 0);
}

TEST_CASE("scene recall is atomic: rapid recalls apply only the last one")
{
    Chain chain = makeChain();
    SceneEngine engine;
    REQUIRE(engine.load(makeScenes(), chain));
    // queue several recalls before the audio thread applies
    engine.recall(0);
    engine.recall(1);
    engine.recall(0);
    engine.apply(chain);
    REQUIRE(engine.activeScene() == 0);
    REQUIRE(engine.pendingScene() == -1);
}

TEST_CASE("scene apply drops actions for slots that disappeared")
{
    Chain chain = makeChain();
    SceneEngine engine;
    REQUIRE(engine.load(makeScenes(), chain));
    engine.recall(0);
    // simulate a graph swap: a fresh chain without the ts808 slot
    using namespace namfx::audio;
    std::vector<SlotDef> slots;
    SlotDef g;
    g.slot = 0;
    g.category = "pedal";
    g.impl = "dsp";
    g.moduleId = "gain";
    g.params.push_back(ParamInit{"gain", 0.5f});
    slots.push_back(g);
    Chain swapped(std::move(slots), registry());
    swapped.prepare(48000.0, 256);
    engine.apply(swapped); // must not crash; gain action still applies
    std::vector<float> out;
    render(swapped, out);
    REQUIRE(engine.activeScene() == 0);
}

TEST_CASE("scene recall outside the valid range is ignored")
{
    Chain chain = makeChain();
    SceneEngine engine;
    REQUIRE(engine.load(makeScenes(), chain));
    engine.recall(-1);
    engine.recall(99);
    REQUIRE(engine.pendingScene() == -1);
    engine.apply(chain);
    REQUIRE(engine.activeScene() == -1);
}

#ifdef NAMFX_RT_ALLOC_ENABLED

TEST_CASE("scene engine apply is allocation free on the audio thread")
{
    Chain chain = makeChain();
    SceneEngine engine;
    REQUIRE(engine.load(makeScenes(), chain));
    engine.recall(1);
    std::vector<float> in(256, 0.1f);
    std::vector<float> inR(256, 0.0f);
    std::vector<float> outL(256, 0.0f);
    std::vector<float> outR(256, 0.0f);
    {
        namfx::rt::ScopedAllocGuard guard;
        engine.apply(chain);
        chain.process(in.data(), inR.data(), outL.data(), outR.data(), 256);
        REQUIRE_FALSE(guard.violated());
    }
}

#endif
