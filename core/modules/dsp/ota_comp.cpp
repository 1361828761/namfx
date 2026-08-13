#include "modules/dsp/ota_comp.h"

#include "modules/module_registry.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerOtaComp(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"sustain", "Sustain", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"attack", "Attack", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"ratio", "Ratio", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("comp.ota", "pedal", std::move(specs),
                            [] { return std::make_unique<OtaCompModule>(); });
}

void OtaCompModule::prepare(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    // release time constant: R30 (47k) * C17 (10u) ~ 470 ms
    releaseA_ = 1.0f - std::exp(-1.0f / (0.47f * f));

    reset();
    updateTargets();
    sustainSm_ = sustain_;
    ratioSm_ = ratio_;
    levelSm_ = level_;
    attackA_ = attackATarget_;
    prepared_ = true;
}

void OtaCompModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        attackA_ += smoothK_ * (attackATarget_ - attackA_);
        sustainSm_ += smoothK_ * (sustain_ - sustainSm_);
        ratioSm_ += smoothK_ * (ratio_ - ratioSm_);
        levelSm_ += smoothK_ * (level_ * level_ - levelSm_);

        const float x = inL[i];
        const float absx = std::fabs(x);
        env_ += (absx > env_ ? attackA_ : releaseA_) * (absx - env_);

        const float g = 1.0f / (1.0f + sustainSm_ * 60.0f * env_);
        const float wet = x * g;
        outL[i] = (x + (wet - x) * ratioSm_) * levelSm_;
    }
}

void OtaCompModule::reset()
{
    env_ = 0.0f;
}

void OtaCompModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void OtaCompModule::setMaxBlock(int)
{
}

void OtaCompModule::setParameter(const std::string& id, float value)
{
    if (id == "sustain") {
        sustain_ = value;
    } else if (id == "attack") {
        attack_ = value;
        updateTargets();
    } else if (id == "ratio") {
        ratio_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

void OtaCompModule::updateTargets()
{
    const float f = static_cast<float>(sampleRate_);
    // ATTACK pot (250k C taper): attack time 0.5 ms .. 200 ms, inverse-log
    const float tau = 0.0005f * std::pow(400.0f, attack_);
    attackATarget_ = 1.0f - std::exp(-1.0f / (tau * f));
}

} // namespace namfx
