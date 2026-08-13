// Global module ID uniqueness (PLAN §5: build-time duplicate ID detection).
// Every built-in registerX() must be called into one registry and every ID
// must be unique - a typo'd duplicate across module files is caught here.
#include "test_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

TEST_CASE("all built-in modules register under globally unique ids")
{
    auto registry = testx::makeRegistry();
    const std::vector<std::string> ids = registry->allIds();

    REQUIRE(ids.size() >= 17); // 16 DSP built-ins + stereo.passthrough test module

    const std::set<std::string> unique(ids.begin(), ids.end());
    REQUIRE(unique.size() == ids.size());

    for (const std::string& id : ids) {
        REQUIRE_FALSE(id.empty());
    }
}

TEST_CASE("built-in module ids match the documented set")
{
    auto registry = testx::makeRegistry();
    const std::vector<std::string> ids = registry->allIds();

    const std::vector<std::string> expected = {
        "gain", "tone", "od.ts808", "od.transparent", "od.mosfet",
        "comp.ota", "mod.chorus", "mod.flanger", "mod.phaser", "mod.wah",
        "gate.ns2", "eq.ge7", "dly.dm2", "dly.tape", "rvb.spring", "pitch.shift",
        "stereo.passthrough",
    };
    REQUIRE(ids.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(ids[i] == expected[i]);
    }
}
