#include "preset/preset_io.h"
#include "test_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

namfx::preset::Preset makePreset()
{
    namfx::preset::Preset preset;
    preset.schema = 1;
    preset.name = "Test Preset";
    namfx::audio::SlotDef gain;
    gain.slot = 0;
    gain.category = "pedal";
    gain.impl = "dsp";
    gain.moduleId = "gain";
    gain.params.push_back(namfx::ParamInit{"gain", 6.0f});
    preset.chain.push_back(std::move(gain));
    namfx::audio::SlotDef tone;
    tone.slot = 1;
    tone.category = "pedal";
    tone.impl = "dsp";
    tone.moduleId = "tone";
    tone.params.push_back(namfx::ParamInit{"freq", 1500.0f});
    tone.params.push_back(namfx::ParamInit{"mode", 1.0f});
    tone.bypass = true;
    tone.mix = 0.5f;
    preset.chain.push_back(std::move(tone));
    namfx::preset::SceneDef scene;
    scene.name = "Solo";
    namfx::preset::SceneOverride overrideDef;
    overrideDef.moduleId = "gain";
    overrideDef.params.push_back(namfx::ParamInit{"gain", 12.0f});
    overrideDef.bypass = false;
    scene.overrides.push_back(std::move(overrideDef));
    preset.scenes.push_back(std::move(scene));
    return preset;
}

} // namespace

TEST_CASE("preset round trips through JSON")
{
    auto registry = testx::makeRegistry();
    const namfx::preset::Preset original = makePreset();
    const std::string json = namfx::preset::savePreset(original);

    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE(report.ok());
    REQUIRE(loaded == original);
}

TEST_CASE("tolerant mode bypasses unknown modules with a warning")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "schema": 1,
        "name": "Broken",
        "chain": [
            { "slot": 0, "category": "pedal", "impl": "dsp", "module": "does.not.exist", "params": {} },
            { "slot": 1, "category": "pedal", "impl": "dsp", "module": "gain", "params": {"gain": 3.0} }
        ]
    })";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Tolerant, *registry, report);
    REQUIRE(report.ok());
    REQUIRE(loaded.chain.size() == 2);
    REQUIRE(loaded.chain[0].bypass == true);
    REQUIRE(loaded.chain[0].moduleId == "does.not.exist");
    REQUIRE_FALSE(report.warnings.empty());
}

TEST_CASE("strict mode rejects unknown modules")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "schema": 1,
        "name": "Broken",
        "chain": [
            { "slot": 0, "category": "pedal", "impl": "dsp", "module": "does.not.exist", "params": {} }
        ]
    })";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE_FALSE(report.ok());
    REQUIRE_FALSE(report.errors.empty());
    REQUIRE(loaded.chain.empty());
}

TEST_CASE("unknown param is error in strict mode and warning in tolerant mode")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "schema": 1,
        "name": "BadParam",
        "chain": [
            { "slot": 0, "category": "pedal", "impl": "dsp", "module": "gain", "params": {"not.a.param": 1.0} }
        ]
    })";
    namfx::preset::LoadReport strictReport;
    namfx::preset::loadPreset(json, namfx::preset::LoadMode::Strict, *registry, strictReport);
    REQUIRE_FALSE(strictReport.ok());

    namfx::preset::LoadReport tolerantReport;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Tolerant, *registry, tolerantReport);
    REQUIRE(tolerantReport.ok());
    REQUIRE(loaded.chain[0].params.empty());
    REQUIRE_FALSE(tolerantReport.warnings.empty());
}

TEST_CASE("out of range params are clamped with a warning")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "schema": 1,
        "name": "Clamp",
        "chain": [
            { "slot": 0, "category": "pedal", "impl": "dsp", "module": "gain", "params": {"gain": 100.0} }
        ]
    })";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE(report.ok());
    REQUIRE(loaded.chain[0].params.size() == 1);
    REQUIRE(loaded.chain[0].params[0].value == 24.0f);
    REQUIRE_FALSE(report.warnings.empty());
}

TEST_CASE("missing params fall back to registry defaults")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "schema": 1,
        "name": "Defaults",
        "chain": [
            { "slot": 0, "category": "pedal", "impl": "dsp", "module": "tone", "params": {} }
        ]
    })";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE(report.ok());
    REQUIRE(loaded.chain[0].params.empty());
    const namfx::ParamSpec* freq = registry->findParam("tone", "freq");
    REQUIRE(freq != nullptr);
    REQUIRE(freq->defaultValue == 2000.0f);
}

TEST_CASE("schema newer than supported is rejected")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "schema": 99,
        "name": "Future",
        "chain": []
    })";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE_FALSE(report.ok());
    REQUIRE_FALSE(report.errors.empty());
}

TEST_CASE("malformed json is rejected without crashing")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({"schema": 1, "name": "Truncated", "chain": [)";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE_FALSE(report.ok());
}

TEST_CASE("missing required slot fields fail both modes but tolerant keeps going")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "schema": 1,
        "name": "MissingFields",
        "chain": [
            { "slot": 0, "category": "pedal", "params": {} }
        ]
    })";
    namfx::preset::LoadReport strictReport;
    namfx::preset::loadPreset(json, namfx::preset::LoadMode::Strict, *registry, strictReport);
    REQUIRE_FALSE(strictReport.ok());

    namfx::preset::LoadReport tolerantReport;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Tolerant, *registry, tolerantReport);
    REQUIRE(tolerantReport.ok());
    REQUIRE(loaded.chain.empty());
}

TEST_CASE("wrong type for chain does not crash")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({"schema": 1, "name": "BadChain", "chain": 42})";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE_FALSE(report.ok());
}

TEST_CASE("more than eight scenes are truncated with a warning")
{
    auto registry = testx::makeRegistry();
    std::string json = R"({"schema": 1, "name": "ManyScenes", "chain": [], "scenes": [)";
    for (int i = 0; i < 10; ++i) {
        if (i > 0) {
            json += ",";
        }
        json += R"({"name": "S)" + std::to_string(i) + R"(", "overrides": []})";
    }
    json += "]}";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE(report.ok());
    REQUIRE(loaded.scenes.size() == 8);
    REQUIRE_FALSE(report.warnings.empty());
}

TEST_CASE("v0 preset without schema field migrates to schema 1")
{
    auto registry = testx::makeRegistry();
    const std::string json = R"({
        "name": "Legacy",
        "chain": [
            { "slot": 0, "category": "pedal", "impl": "dsp", "module": "gain", "params": {"gain": 3.0} }
        ]
    })";
    namfx::preset::LoadReport report;
    const namfx::preset::Preset loaded = namfx::preset::loadPreset(
        json, namfx::preset::LoadMode::Strict, *registry, report);
    REQUIRE(report.ok());
    REQUIRE(loaded.schema == 1);
    REQUIRE(loaded.name == "Legacy");
    REQUIRE(loaded.chain.size() == 1);
    REQUIRE(loaded.scenes.empty());

    const std::string roundTrip = namfx::preset::savePreset(loaded);
    REQUIRE(roundTrip.find("\"schema\": 1") != std::string::npos);
}
