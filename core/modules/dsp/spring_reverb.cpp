#include "modules/dsp/spring_reverb.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerSpringReverb(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"dwell", "Dwell", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"mix", "Mix", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"damp", "Damp", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("rvb.spring", "pedal", std::move(specs),
                            [] { return std::make_unique<SpringReverbModule>(); });
}

void SpringReverbModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    for (int k = 0; k < kSprings; ++k) {
        delay_[k].prepare(sampleRate, kDelayMs_[k] + 2.0f);
        delay_[k].setDelayMs(kDelayMs_[k]);
    }

    reset();
    dwellSm_ = dwell_;
    mixSm_ = mix_;
    dampSm_ = damp_;
    prepared_ = true;
}

void SpringReverbModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    const float kTwoPi = 6.2831853f;
    for (int i = 0; i < n; ++i) {
        dwellSm_ += smoothK_ * (dwell_ - dwellSm_);
        mixSm_ += smoothK_ * (mix_ - mixSm_);
        dampSm_ += smoothK_ * (damp_ - dampSm_);

        // damp 0..1 -> damping low-pass corner 200 Hz..8 kHz
        const float lpK = 1.0f - std::exp(-kTwoPi * (200.0f * std::pow(40.0f, dampSm_)) / fs_);

        // dwell drives the springs with soft clipping ("spring crunch")
        const float drive = 0.5f + 2.5f * dwellSm_;
        const float x = inL[i];
        const float sat = std::tanh(drive * x);

        float wet = 0.0f;
        for (int k = 0; k < kSprings; ++k) {
            const float wetIn = sat + kFeedback_[k] * lpState_[k];
            float d = delay_[k].process(wetIn);

            // dispersive all-pass chain (frequency-dependent delay)
            for (int s = 0; s < kDispersionStages; ++s) {
                const float y = -kDispersion_[k] * d + apX1_[k][s] + kDispersion_[k] * apY1_[k][s];
                apX1_[k][s] = d;
                apY1_[k][s] = y;
                d = y;
            }

            // damping inside the loop: repeats decay and darken
            lpState_[k] += lpK * (d - lpState_[k]);
            wet += lpState_[k];
        }

        outL[i] = x + (wet / static_cast<float>(kSprings)) * mixSm_;
    }
}

void SpringReverbModule::reset()
{
    for (int k = 0; k < kSprings; ++k) {
        delay_[k].reset();
        lpState_[k] = 0.0f;
        for (int s = 0; s < kDispersionStages; ++s) {
            apX1_[k][s] = 0.0f;
            apY1_[k][s] = 0.0f;
        }
    }
}

void SpringReverbModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void SpringReverbModule::setMaxBlock(int)
{
}

void SpringReverbModule::setParameter(const std::string& id, float value)
{
    if (id == "dwell") {
        dwell_ = value;
    } else if (id == "mix") {
        mix_ = value;
    } else if (id == "damp") {
        damp_ = value;
    }
}

} // namespace namfx
