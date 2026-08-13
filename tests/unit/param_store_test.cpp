#include "audio/param_store.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

namfx::ParamSpec makeSpec(const std::string& id, float min, float max, float def,
                          const std::string& unit, namfx::Taper taper)
{
    namfx::ParamSpec spec;
    spec.id = id;
    spec.displayName = id;
    spec.min = min;
    spec.max = max;
    spec.defaultValue = def;
    spec.unit = unit;
    spec.taper = taper;
    return spec;
}

} // namespace

TEST_CASE("param store initializes defaults from specs")
{
    std::vector<namfx::ParamSpec> specs;
    specs.push_back(makeSpec("gain", -20.0f, 20.0f, 3.0f, "dB", namfx::Taper::Linear));
    specs.push_back(makeSpec("tone", 0.0f, 1.0f, 0.5f, "", namfx::Taper::Log));

    namfx::ParamStore store(specs);

    REQUIRE(store.get("gain") == 3.0f);
    REQUIRE(store.get("tone") == 0.5f);
    REQUIRE(store.target("gain") == 3.0f);
    REQUIRE(store.target("tone") == 0.5f);
    REQUIRE_FALSE(store.isRamping());
}

TEST_CASE("param store clamps set values to spec range")
{
    namfx::ParamStore store({makeSpec("gain", -12.0f, 6.0f, 0.0f, "dB", namfx::Taper::Linear)});
    store.setSampleRate(48000.0);

    store.set("gain", -100.0f);
    REQUIRE(store.target("gain") == -12.0f);
    store.advance(480);
    REQUIRE(store.get("gain") == -12.0f);

    store.set("gain", 100.0f);
    REQUIRE(store.target("gain") == 6.0f);
    store.advance(480);
    REQUIRE(store.get("gain") == 6.0f);
}

TEST_CASE("param store ramp completes within 480 samples at 48k and lands on target")
{
    namfx::ParamStore store({makeSpec("gain", -20.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear)});
    store.setSampleRate(48000.0);

    store.set("gain", 6.0f);
    REQUIRE(store.isRamping());
    REQUIRE(store.get("gain") == 0.0f);

    store.advance(479);
    REQUIRE(store.isRamping());
    const float mid = store.get("gain");
    REQUIRE(mid > 0.0f);
    REQUIRE(mid < 6.0f);

    store.advance(1);
    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get("gain") == 6.0f);
    REQUIRE(store.target("gain") == 6.0f);
}

TEST_CASE("param store get reflects ramp progress")
{
    namfx::ParamStore store({makeSpec("gain", 0.0f, 10.0f, 0.0f, "", namfx::Taper::Linear)});
    store.setSampleRate(48000.0);

    store.set("gain", 10.0f);
    store.advance(240);

    REQUIRE(store.isRamping());
    REQUIRE(store.get("gain") == Catch::Approx(5.0f).margin(1e-4));
}

TEST_CASE("param store setImmediate snaps without ramp")
{
    namfx::ParamStore store({makeSpec("gain", -20.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear)});
    store.setSampleRate(48000.0);

    store.setImmediate("gain", -9.0f);
    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get("gain") == -9.0f);
    REQUIRE(store.target("gain") == -9.0f);

    store.setImmediate("gain", 30.0f);
    REQUIRE(store.get("gain") == 20.0f);
    REQUIRE_FALSE(store.isRamping());
}

TEST_CASE("param store ramps multiple params simultaneously")
{
    std::vector<namfx::ParamSpec> specs;
    specs.push_back(makeSpec("a", 0.0f, 10.0f, 0.0f, "", namfx::Taper::Linear));
    specs.push_back(makeSpec("b", 0.0f, 10.0f, 0.0f, "", namfx::Taper::Linear));
    specs.push_back(makeSpec("c", 0.0f, 10.0f, 0.0f, "", namfx::Taper::Linear));

    namfx::ParamStore store(specs);
    store.setSampleRate(48000.0);

    store.set("a", 10.0f);
    store.set("b", 5.0f);
    store.set("c", 2.0f);
    REQUIRE(store.isRamping());

    store.advance(240);
    REQUIRE(store.get("a") == Catch::Approx(5.0f).margin(1e-4));
    REQUIRE(store.get("b") == Catch::Approx(2.5f).margin(1e-4));
    REQUIRE(store.get("c") == Catch::Approx(1.0f).margin(1e-4));

    store.advance(240);
    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get("a") == 10.0f);
    REQUIRE(store.get("b") == 5.0f);
    REQUIRE(store.get("c") == 2.0f);
}

TEST_CASE("param store advance is safe with zero and oversized counts")
{
    namfx::ParamStore store({makeSpec("gain", 0.0f, 1.0f, 0.0f, "", namfx::Taper::Linear)});
    store.setSampleRate(48000.0);

    store.set("gain", 1.0f);
    store.advance(0);
    REQUIRE(store.isRamping());
    REQUIRE(store.get("gain") == 0.0f);

    store.advance(1000000);
    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get("gain") == 1.0f);
}

TEST_CASE("param store unknown id throws")
{
    namfx::ParamStore store({makeSpec("gain", 0.0f, 1.0f, 0.5f, "", namfx::Taper::Linear)});

    REQUIRE_THROWS_AS(store.set("ghost", 1.0f), std::out_of_range);
    REQUIRE_THROWS_AS(store.setImmediate("ghost", 1.0f), std::out_of_range);
    REQUIRE_THROWS_AS(store.get("ghost"), std::out_of_range);
    REQUIRE_THROWS_AS(store.target("ghost"), std::out_of_range);
}

TEST_CASE("param store retargets mid ramp from current value")
{
    namfx::ParamStore store({makeSpec("gain", 0.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear)});
    store.setSampleRate(48000.0);

    store.set("gain", 6.0f);
    store.advance(100);
    store.set("gain", 3.0f);
    store.advance(480);

    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get("gain") == 3.0f);
}

TEST_CASE("param store set to current value does not ramp")
{
    namfx::ParamStore store({makeSpec("gain", 0.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear)});
    store.setSampleRate(48000.0);

    store.set("gain", 0.0f);
    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get("gain") == 0.0f);
}

TEST_CASE("param store ramp duration scales with sample rate")
{
    namfx::ParamStore store({makeSpec("gain", 0.0f, 1.0f, 0.0f, "", namfx::Taper::Linear)});
    store.setSampleRate(44100.0);

    store.set("gain", 1.0f);
    store.advance(440);
    REQUIRE(store.isRamping());
    store.advance(1);
    REQUIRE_FALSE(store.isRamping());
    REQUIRE(store.get("gain") == 1.0f);
}

TEST_CASE("param init carries id and value")
{
    namfx::ParamInit init;
    init.id = "gain";
    init.value = 3.0f;

    REQUIRE(init.id == "gain");
    REQUIRE(init.value == 3.0f);
}
