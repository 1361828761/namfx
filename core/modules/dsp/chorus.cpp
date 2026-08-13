#include "modules/dsp/chorus.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerChorus(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"depth", "Depth", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"rate", "Rate", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("mod.chorus", "pedal", std::move(specs),
                            [] { return std::make_unique<ChorusModule>(); });
}

void ChorusModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));

    delay_.prepare(sampleRate, baseDelayMs_ + maxModMs_ + 2.0f);
    lfo_.prepare(sampleRate);

    reset();
    depthSm_ = depth_;
    rateSm_ = rate_;
    levelSm_ = level_;
    prepared_ = true;
}

void ChorusModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        depthSm_ += smoothK_ * (depth_ - depthSm_);
        rateSm_ += smoothK_ * (rate_ - rateSm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        // rate 0..1 -> 0.1..5 Hz
        const float hz = 0.1f + 4.9f * rateSm_;
        lfo_.setRate(hz);
        const float lfo = lfo_.process();

        // delay = base + (lfo - 0.5) * depth * maxMod
        delay_.setDelayMs(baseDelayMs_ + (lfo - 0.5f) * depthSm_ * maxModMs_);

        const float x = inL[i];
        const float wet = delay_.process(x);
        outL[i] = (x * 0.5f + wet * 0.5f) * levelSm_;
    }
}

void ChorusModule::reset()
{
    delay_.reset();
    lfo_.reset();
}

void ChorusModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void ChorusModule::setMaxBlock(int)
{
}

void ChorusModule::setParameter(const std::string& id, float value)
{
    if (id == "depth") {
        depth_ = value;
    } else if (id == "rate") {
        rate_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
