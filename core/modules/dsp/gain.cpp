#include "modules/dsp/gain.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerGain(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"gain", "Gain", -60.0f, 24.0f, 0.0f, "dB", Taper::Linear});
    registry.registerModule("gain", "pedal", std::move(specs),
                            [] { return std::make_unique<GainModule>(); });
}

void GainModule::prepare(double, int)
{
}

void GainModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    const float gain = gainLinear_;
    for (int i = 0; i < n; ++i) {
        outL[i] = inL[i] * gain;
    }
}

void GainModule::reset()
{
}

void GainModule::setSampleRate(double)
{
}

void GainModule::setMaxBlock(int)
{
}

void GainModule::setParameter(const std::string& id, float value)
{
    if (id == "gain") {
        gainDb_ = value;
        gainLinear_ = std::pow(10.0f, gainDb_ / 20.0f);
    }
}

} // namespace namfx
