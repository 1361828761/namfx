#include "modules/param_spec.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("param spec defaults match contract")
{
    namfx::ParamSpec spec;
    REQUIRE(spec.id.empty());
    REQUIRE(spec.displayName.empty());
    REQUIRE(spec.min == 0.0f);
    REQUIRE(spec.max == 1.0f);
    REQUIRE(spec.defaultValue == 0.0f);
    REQUIRE(spec.unit.empty());
    REQUIRE(spec.taper == namfx::Taper::Linear);
}

TEST_CASE("param spec fields round trip")
{
    namfx::ParamSpec spec;
    spec.id = "gain";
    spec.displayName = "Gain";
    spec.min = -20.0f;
    spec.max = 20.0f;
    spec.defaultValue = 0.0f;
    spec.unit = "dB";
    spec.taper = namfx::Taper::Log;

    REQUIRE(spec.id == "gain");
    REQUIRE(spec.displayName == "Gain");
    REQUIRE(spec.min == -20.0f);
    REQUIRE(spec.max == 20.0f);
    REQUIRE(spec.defaultValue == 0.0f);
    REQUIRE(spec.unit == "dB");
    REQUIRE(spec.taper == namfx::Taper::Log);
}

TEST_CASE("param spec supports percent and frequency style units")
{
    namfx::ParamSpec mix;
    mix.id = "mix";
    mix.unit = "%";
    mix.min = 0.0f;
    mix.max = 100.0f;
    mix.defaultValue = 50.0f;
    mix.taper = namfx::Taper::Linear;

    namfx::ParamSpec cutoff;
    cutoff.id = "cutoff";
    cutoff.unit = "Hz";
    cutoff.min = 20.0f;
    cutoff.max = 20000.0f;
    cutoff.defaultValue = 1000.0f;
    cutoff.taper = namfx::Taper::Log;

    REQUIRE(mix.unit == "%");
    REQUIRE(mix.defaultValue == 50.0f);
    REQUIRE(mix.taper == namfx::Taper::Linear);
    REQUIRE(cutoff.unit == "Hz");
    REQUIRE(cutoff.defaultValue == 1000.0f);
    REQUIRE(cutoff.taper == namfx::Taper::Log);
    REQUIRE(namfx::Taper::Linear != namfx::Taper::Log);
}
