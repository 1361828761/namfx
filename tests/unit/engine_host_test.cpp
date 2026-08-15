// EngineHost integration: the audio-thread block boundary must consume the
// queued scene recalls and UI / control-source writes (regression for the
// M5 review finding: ControlRouter::apply / SceneEngine::apply were never
// wired into EngineHost::process, so scene recall, parameter dragging and
// MIDI CC binds were dead code in the desktop app).
#include "desktop/Engine/engine_host.h"

#include "midi/midi_events.h"
#include "platform/rt_alloc.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

using namfx::desktop::EngineHost;
using namfx::midi::Event;

constexpr double kRate = 48000.0;
constexpr int kBlock = 64;

// demo presets are copied to ${CMAKE_BINARY_DIR}/demo-presets by the test
// CMake; chain_drive.json = od.ts808 -> eq.ge7 -> cab.ir (irs/cab_clean.wav)
const std::string kDemoDir = NAMFX_DEMO_DIR;

std::string presetPath(const std::string& name)
{
    return kDemoDir + "/" + name + ".json";
}

void runBlocks(EngineHost& host, int blocks)
{
    std::array<float, kBlock> in{};
    std::array<float, kBlock> inR{};
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    for (int i = 0; i < kBlock; ++i) {
        in[static_cast<std::size_t>(i)] = 0.1f;
    }
    for (int b = 0; b < blocks; ++b) {
        host.process(in.data(), inR.data(), outL.data(), outR.data(), kBlock);
    }
}

std::unique_ptr<EngineHost> makeHost()
{
    auto host = std::make_unique<EngineHost>();
    host->prepare(kRate, kBlock);
    std::string error;
    REQUIRE(host->loadPreset(presetPath("chain_drive"), kDemoDir, error));
    runBlocks(*host, 8); // let the swap land and the fade-in finish
    return host;
}

float paramValue(const EngineHost& host, int slot, std::size_t paramIndex)
{
    const std::vector<EngineHost::SlotInfo> info = host.chainInfo();
    for (const EngineHost::SlotInfo& s : info) {
        if (s.slot == slot) {
            return s.values.at(paramIndex);
        }
    }
    return -1.0f;
}

} // namespace

TEST_CASE("engine host applies queued UI param writes at the block boundary")
{
    auto host = makeHost();
    const float before = paramValue(*host, 0, 0); // od.ts808 drive
    REQUIRE(host->uiSetParam(0, "drive", 9.0f));
    // nothing applied yet: one block boundary must consume the queued write
    runBlocks(*host, 1);
    // the write targets the store and ramps; after 10ms (kMaxSmoothingMs)
    // the value reaches its target
    runBlocks(*host, 32);
    const float after = paramValue(*host, 0, 0);
    REQUIRE(after > before);
    REQUIRE(after == Catch::Approx(9.0f).margin(0.01f));
}

TEST_CASE("engine host applies queued UI bypass writes at the block boundary")
{
    auto host = makeHost();
    const std::vector<EngineHost::SlotInfo> info = host->chainInfo();
    REQUIRE_FALSE(info.front().bypass);
    REQUIRE(host->uiSetBypass(info.front().slot, true));
    runBlocks(*host, 2); // fade runs on the audio thread
    const std::vector<EngineHost::SlotInfo> after = host->chainInfo();
    REQUIRE(after.front().bypass);
}

TEST_CASE("engine host applies scene recalls at the block boundary")
{
    auto host = makeHost();
    // snapshot the current state into scene 1, then move the param away
    std::string error;
    REQUIRE(host->saveScene(0, "Snap", error));
    const float snapped = paramValue(*host, 0, 0);
    REQUIRE(host->uiSetParam(0, "drive", 1.0f));
    runBlocks(*host, 32);
    REQUIRE(paramValue(*host, 0, 0) < snapped - 0.1f);
    // recall: the pending scene must land at the next block boundary
    host->recallScene(0);
    runBlocks(*host, 32);
    REQUIRE(paramValue(*host, 0, 0) == Catch::Approx(snapped).margin(0.01f));
}

TEST_CASE("engine host applies MIDI CC binds through the router")
{
    auto host = makeHost();
    std::string error;
    REQUIRE(host->midiLearnParam(10, "od.ts808", "drive", error));
    Event cc{};
    cc.type = Event::Type::ControlChange;
    cc.channel = 0;
    cc.data1 = 10;  // CC 10
    cc.data2 = 127; // MSB = full (7-bit mode -> 127<<7 = 16256)
    host->handleMidi(cc);
    runBlocks(*host, 32);
    // ts808 drive spec range is 0..10; 16256/16383 of that range
    constexpr float kExpected = 10.0f * (16256.0f / 16383.0f); // ~9.92
    REQUIRE(paramValue(*host, 0, 0) == Catch::Approx(kExpected).margin(0.05f));
}

TEST_CASE("engine host block boundary is allocation-free on the audio thread")
{
    auto host = makeHost();
    REQUIRE(host->uiSetParam(0, "drive", 9.0f));
    REQUIRE(host->uiSetBypass(1, true));
    host->recallScene(0);
    std::array<float, kBlock> in{};
    std::array<float, kBlock> inR{};
    std::array<float, kBlock> outL{};
    std::array<float, kBlock> outR{};
    {
        namfx::rt::ScopedAllocGuard guard;
        for (int b = 0; b < 16; ++b) {
            host->process(in.data(), inR.data(), outL.data(), outR.data(), kBlock);
        }
        REQUIRE_FALSE(guard.violated());
    }
}
