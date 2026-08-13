#include "modules/module_base.h"
#include "modules/module_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
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

class GainModule final : public namfx::ModuleBase {
public:
    void prepare(double, int) override {}
    void process(const float*, const float*, float*, float*, int) override {}
    void reset() override {}
    void setSampleRate(double) override {}
    void setMaxBlock(int) override {}
    namfx::ChannelMode channelMode() const override { return namfx::ChannelMode::MonoInMonoOut; }
};

class AmpModule final : public namfx::ModuleBase {
public:
    void prepare(double, int) override {}
    void process(const float*, const float*, float*, float*, int) override {}
    void reset() override {}
    void setSampleRate(double) override {}
    void setMaxBlock(int) override {}
    namfx::ChannelMode channelMode() const override { return namfx::ChannelMode::MonoInStereoOut; }
};

} // namespace

TEST_CASE("registry registers modules and lists ids in registration order")
{
    namfx::ModuleRegistry registry;

    REQUIRE(registry.registerModule("pedal.gain", "pedal",
                                    {makeSpec("gain", -20.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear)},
                                    [] { return std::make_unique<GainModule>(); }));
    REQUIRE(registry.registerModule("amp.clean", "amp", {},
                                    [] { return std::make_unique<AmpModule>(); }));

    REQUIRE(registry.has("pedal.gain"));
    REQUIRE(registry.has("amp.clean"));
    REQUIRE_FALSE(registry.has("ghost.module"));

    const std::vector<std::string> ids = registry.allIds();
    REQUIRE(ids.size() == 2);
    REQUIRE(ids[0] == "pedal.gain");
    REQUIRE(ids[1] == "amp.clean");
}

TEST_CASE("registry rejects duplicate module ids without overwriting")
{
    namfx::ModuleRegistry registry;

    REQUIRE(registry.registerModule("dup", "pedal", {},
                                    [] { return std::make_unique<GainModule>(); }));
    REQUIRE_FALSE(registry.registerModule("dup", "amp", {},
                                          [] { return std::make_unique<AmpModule>(); }));

    REQUIRE(registry.allIds().size() == 1);

    std::unique_ptr<namfx::ModuleBase> module = registry.create("dup");
    REQUIRE(module != nullptr);
    REQUIRE(dynamic_cast<GainModule*>(module.get()) != nullptr);
}

TEST_CASE("registry create with unknown id throws")
{
    namfx::ModuleRegistry registry;
    REQUIRE_THROWS_AS(registry.create("ghost.module"), std::out_of_range);
}

TEST_CASE("registry create returns a working module instance")
{
    namfx::ModuleRegistry registry;
    REQUIRE(registry.registerModule("amp.clean", "amp", {},
                                    [] { return std::make_unique<AmpModule>(); }));

    std::unique_ptr<namfx::ModuleBase> module = registry.create("amp.clean");
    REQUIRE(module != nullptr);
    REQUIRE(module->channelMode() == namfx::ChannelMode::MonoInStereoOut);

    module->prepare(48000.0, 256);
    module->setSampleRate(48000.0);
    module->setMaxBlock(256);
    module->reset();
}

TEST_CASE("registry findParam resolves params and rejects unknowns")
{
    namfx::ModuleRegistry registry;
    REQUIRE(registry.registerModule("pedal.gain", "pedal",
                                    {makeSpec("gain", -20.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear),
                                     makeSpec("level", 0.0f, 1.0f, 0.8f, "", namfx::Taper::Log)},
                                    [] { return std::make_unique<GainModule>(); }));

    const namfx::ParamSpec* gain = registry.findParam("pedal.gain", "gain");
    REQUIRE(gain != nullptr);
    REQUIRE(gain->min == -20.0f);
    REQUIRE(gain->max == 20.0f);
    REQUIRE(gain->defaultValue == 0.0f);
    REQUIRE(gain->unit == "dB");
    REQUIRE(gain->taper == namfx::Taper::Linear);

    const namfx::ParamSpec* level = registry.findParam("pedal.gain", "level");
    REQUIRE(level != nullptr);
    REQUIRE(level->defaultValue == 0.8f);
    REQUIRE(level->taper == namfx::Taper::Log);

    REQUIRE(registry.findParam("pedal.gain", "missing") == nullptr);
    REQUIRE(registry.findParam("ghost.module", "gain") == nullptr);
}

TEST_CASE("registry specsFor returns contiguous specs and throws for unknown module")
{
    namfx::ModuleRegistry registry;
    REQUIRE(registry.registerModule("pedal.gain", "pedal",
                                    {makeSpec("gain", -20.0f, 20.0f, 0.0f, "dB", namfx::Taper::Linear),
                                     makeSpec("level", 0.0f, 1.0f, 0.8f, "", namfx::Taper::Log)},
                                    [] { return std::make_unique<GainModule>(); }));

    const std::vector<namfx::ParamSpec>& specs = registry.specsFor("pedal.gain");
    REQUIRE(specs.size() == 2);
    REQUIRE(specs[0].id == "gain");
    REQUIRE(specs[1].id == "level");

    REQUIRE_THROWS_AS(registry.specsFor("ghost.module"), std::out_of_range);
}

TEST_CASE("registry category lookup")
{
    namfx::ModuleRegistry registry;
    REQUIRE(registry.registerModule("cab.v30", "cab", {},
                                    [] { return std::make_unique<GainModule>(); }));

    REQUIRE(registry.categoryOf("cab.v30") == "cab");
    REQUIRE_THROWS_AS(registry.categoryOf("ghost.module"), std::out_of_range);
}
