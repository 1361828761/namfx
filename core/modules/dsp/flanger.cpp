#include "modules/dsp/flanger.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerFlanger(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"feedback", "Feedback", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"range", "Range", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"rate", "Rate", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("mod.flanger", "pedal", std::move(specs),
                            [] { return std::make_unique<FlangerModule>(); });
}

void FlangerModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));

    delay_.prepare(sampleRate, baseDelayMs_ + maxModMs_ + 2.0f);
    lfo_.prepare(sampleRate);

    reset();
    feedbackSm_ = feedback_;
    rangeSm_ = range_;
    rateSm_ = rate_;
    prepared_ = true;
}

void FlangerModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        feedbackSm_ += smoothK_ * (feedback_ - feedbackSm_);
        rangeSm_ += smoothK_ * (range_ - rangeSm_);
        rateSm_ += smoothK_ * (rate_ - rateSm_);

        // rate 0..1 -> 0.05..2 Hz (slower than chorus, classic flanger sweep)
        const float hz = 0.05f + 1.95f * rateSm_;
        lfo_.setRate(hz);
        const float lfo = lfo_.process();

        // delay = base + (lfo - 0.5) * range * maxMod
        delay_.setDelayMs(baseDelayMs_ + (lfo - 0.5f) * rangeSm_ * maxModMs_);

        const float x = inL[i];
        const float wet = delay_.process(x + feedbackSm_ * 0.85f * wetPrev_);
        wetPrev_ = wet;
        outL[i] = x * 0.5f + wet * 0.5f;
    }
}

void FlangerModule::reset()
{
    delay_.reset();
    lfo_.reset();
    wetPrev_ = 0.0f;
}

void FlangerModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void FlangerModule::setMaxBlock(int)
{
}

void FlangerModule::setParameter(const std::string& id, float value)
{
    if (id == "feedback") {
        feedback_ = value;
    } else if (id == "range") {
        range_ = value;
    } else if (id == "rate") {
        rate_ = value;
    }
}

} // namespace namfx
