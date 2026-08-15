#include "audio/chain.h"
#include "audio/control_router.h"
#include "audio/scene_engine.h"
#include "midi/midi_events.h"
#include "midi/midi_router.h"
#include "preset/preset_model.h"
#include "test_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

using namfx::audio::Chain;
using namfx::audio::ControlRouter;
using namfx::audio::SceneEngine;
using namfx::midi::Event;
using namfx::midi::MidiRouter;

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
    g.params.push_back(namfx::ParamInit{"gain", 0.0f}); // dB, -60..24
    slots.push_back(g);
    Chain chain(std::move(slots), registry());
    chain.prepare(48000.0, 64);
    return chain;
}

void runBlocks(Chain& chain, ControlRouter& router, int blocks)
{
    std::vector<float> in(64, 0.1f);
    std::vector<float> inR(64, 0.0f);
    std::vector<float> outL(64, 0.0f);
    std::vector<float> outR(64, 0.0f);
    for (int b = 0; b < blocks; ++b) {
        router.apply(chain, 64);
        chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
    }
}

std::vector<namfx::preset::SceneDef> makeScenes()
{
    namfx::preset::SceneDef s1;
    s1.name = "Lead";
    s1.overrides.push_back(namfx::preset::SceneOverride{"gain", {{"gain", 6.0f}}, false});
    return {s1};
}

} // namespace

TEST_CASE("midi router learns a CC to a parameter and maps 14-bit values")
{
    Chain chain = makeChain();
    ControlRouter router;
    MidiRouter midi;
    REQUIRE(midi.learnBind(router, chain, 10, "gain", "gain"));
    REQUIRE(midi.ccParamCount() == 1);
    // rebind same CC is fine
    REQUIRE(midi.learnBind(router, chain, 10, "gain", "gain"));
    // conflicting bind to a scene is rejected
    REQUIRE_FALSE(midi.bindScene(10, 1));
    // unknown param / module rejected
    REQUIRE_FALSE(midi.learnBind(router, chain, 11, "gain", "nope"));
    REQUIRE_FALSE(midi.learnBind(router, chain, 11, "nope", "x"));

    // CC 10 = 0x40 (64) -> 14-bit 8192 -> normalized 0.5 -> gain = -60 + 0.5*84 = -18 dB
    Event cc;
    cc.type = Event::Type::ControlChange;
    cc.data1 = 10;
    cc.data2 = 64;
    { SceneEngine unused; midi.handleEvent(cc, router, unused, MidiRouter::Actions{}); }
    runBlocks(chain, router, 200);
    // gain target -18 dB -> 0.1 * 10^(-18/20) = 0.01259
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
    REQUIRE(std::fabs(peak - 0.01259f) < 0.002f);

    // 14-bit pair: MSB 127, LSB 127 -> 16383 -> gain +24 dB (max)
    Event lsb;
    lsb.type = Event::Type::ControlChange;
    lsb.data1 = 42; // 10 + 32: LSB
    lsb.data2 = 127;
    { SceneEngine unused; midi.handleEvent(lsb, router, unused, MidiRouter::Actions{}); }
    cc.data2 = 127;
    cc.data3 = 127;
    { SceneEngine unused; midi.handleEvent(cc, router, unused, MidiRouter::Actions{}); }
    runBlocks(chain, router, 200);
    router.apply(chain, 64);
    chain.process(in.data(), inR.data(), outL.data(), outR.data(), 64);
    peak = 0.0f;
    for (float x : outL) {
        peak = std::max(peak, std::fabs(x));
    }
    REQUIRE(std::fabs(peak - 0.1f * 15.85f) < 0.5f); // +24 dB = 15.85x

    midi.clearBind(10);
    REQUIRE(midi.ccParamCount() == 0);
}

TEST_CASE("midi router binds a CC to a scene and recalls it")
{
    Chain chain = makeChain();
    ControlRouter router;
    SceneEngine scenes;
    REQUIRE(scenes.load(makeScenes(), chain));
    MidiRouter midi;
    REQUIRE(midi.bindScene(5, 1));
    REQUIRE_FALSE(midi.learnBind(router, chain, 5, "gain", "gain")); // conflict
    REQUIRE(midi.ccSceneCount() == 1);

    Event cc;
    cc.type = Event::Type::ControlChange;
    cc.data1 = 5;
    cc.data2 = 1;
    midi.handleEvent(cc, router, scenes, MidiRouter::Actions{});
    scenes.apply(chain);
    REQUIRE(scenes.activeScene() == 0);
    midi.clearScene(5);
    REQUIRE(midi.ccSceneCount() == 0);
}

TEST_CASE("midi router exposes bindings for persistence")
{
    Chain chain = makeChain();
    ControlRouter router;
    SceneEngine scenes;
    REQUIRE(scenes.load(makeScenes(), chain));
    MidiRouter midi;
    REQUIRE(midi.learnBind(router, chain, 21, "gain", "gain"));
    REQUIRE(midi.bindScene(5, 2));

    const std::vector<MidiRouter::BindInfo> binds = midi.bindings();
    REQUIRE(binds.size() == 2);
    // params first (CC ascending), then scenes
    REQUIRE(binds[0].kind == MidiRouter::BindInfo::Kind::Param);
    REQUIRE(binds[0].cc == 21);
    REQUIRE(binds[0].moduleId == "gain");
    REQUIRE(binds[0].paramId == "gain");
    REQUIRE(binds[1].kind == MidiRouter::BindInfo::Kind::Scene);
    REQUIRE(binds[1].cc == 5);
    REQUIRE(binds[1].sceneIndex == 2);

    // clearing removes them from the snapshot
    midi.clearBind(21);
    midi.clearScene(5);
    REQUIRE(midi.bindings().empty());
}

TEST_CASE("midi router fires preset requests and toggles on program change")
{
    MidiRouter midi;
    int presetRequested = -1;
    int tunerToggles = 0;
    int bypassToggles = 0;
    MidiRouter::Actions actions;
    actions.presetRequest = [&](int p) { presetRequested = p; };
    actions.tunerToggle = [&]() { ++tunerToggles; };
    actions.globalBypassToggle = [&]() { ++bypassToggles; };

    Event pc;
    pc.type = Event::Type::ProgramChange;
    pc.data1 = 3;
    {
        ControlRouter unusedRouter;
        SceneEngine unusedScenes;
        midi.handleEvent(pc, unusedRouter, unusedScenes, actions);
    }
    REQUIRE(presetRequested == 3);

    Event note;
    note.type = Event::Type::NoteOn;
    note.data1 = 0x7F;
    {
        ControlRouter unusedRouter;
        SceneEngine unusedScenes;
        midi.handleEvent(note, unusedRouter, unusedScenes, actions);
    }
    REQUIRE(tunerToggles == 1);
}

#ifdef NAMFX_RT_ALLOC_ENABLED
#endif
