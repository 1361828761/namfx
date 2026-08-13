#include "modules/dsp/ts808.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerTs808(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"drive", "Drive", 0.0f, 10.0f, 5.0f, "", Taper::Linear});
    specs.push_back(ParamSpec{"tone", "Tone", 0.0f, 10.0f, 5.0f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", -60.0f, 12.0f, 0.0f, "dB", Taper::Linear});
    registry.registerModule("od.ts808", "pedal", std::move(specs),
                            [] { return std::make_unique<Ts808Module>(); });
}

void Ts808Module::prepare(double sampleRate, int)
{
    clipping_.prepare(sampleRate);
    tone_.prepare(sampleRate);
    prepared_ = true;
}

void Ts808Module::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    const float level = levelGain_;
    for (int i = 0; i < n; ++i) {
        float x = clipping_.processSample(inL[i]);
        x = tone_.processSample(x);
        outL[i] = x * level;
    }
}

void Ts808Module::reset()
{
    clipping_.reset();
    tone_.reset();
}

void Ts808Module::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void Ts808Module::setMaxBlock(int)
{
}

void Ts808Module::setParameter(const std::string& id, float value)
{
    if (id == "drive") {
        clipping_.setDrive(value / 10.0f);
    } else if (id == "tone") {
        tone_.setTone(value / 10.0f);
    } else if (id == "level") {
        levelDb_ = value;
        levelGain_ = std::pow(10.0f, levelDb_ / 20.0f);
    }
}

} // namespace namfx
