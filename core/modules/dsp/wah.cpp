#include "modules/dsp/wah.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerWah(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"position", "Position", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"resonance", "Resonance", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("mod.wah", "pedal", std::move(specs),
                            [] { return std::make_unique<WahModule>(); });
}

void WahModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    reset();
    positionSm_ = position_;
    resonanceSm_ = resonance_;
    levelSm_ = level_;
    prepared_ = true;
}

void WahModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        positionSm_ += smoothK_ * (position_ - positionSm_);
        resonanceSm_ += smoothK_ * (resonance_ - resonanceSm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        // TPT (Zavalishin) state-variable filter; centre sweeps
        // exponentially between fcMin and fcMax, Q from 1 to 10.
        // Output = low + band (resonant low-pass): unity below the peak,
        // ~Q boost at the centre (DAFx15: wah pedals commonly combine
        // band and low outputs)
        const float fc = fcMin_ * std::pow(fcMax_ / fcMin_, positionSm_);
        const float g = std::tan(3.14159265f * fc / fs_);
        const float k = 1.0f / (1.0f + 9.0f * resonanceSm_);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        const float x = inL[i];
        const float v3 = x - ic2_;
        const float v1 = a1 * ic1_ + a2 * v3;
        const float v2 = ic2_ + a2 * ic1_ + a3 * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;

        outL[i] = (v1 + v2) * levelSm_;
    }
}

void WahModule::reset()
{
    ic1_ = 0.0f;
    ic2_ = 0.0f;
}

void WahModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void WahModule::setMaxBlock(int)
{
}

void WahModule::setParameter(const std::string& id, float value)
{
    if (id == "position") {
        position_ = value;
    } else if (id == "resonance") {
        resonance_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
