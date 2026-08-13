#include "modules/dsp/dm2_delay.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerDm2Delay(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"time", "Time", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"feedback", "Feedback", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("dly.dm2", "pedal", std::move(specs),
                            [] { return std::make_unique<Dm2DelayModule>(); });
}

void Dm2DelayModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    delay_.prepare(sampleRate, maxDelayMs_ + 2.0f);

    // wet low-pass: 2nd order Butterworth at ~3.5 kHz (docs/research/dm2.md)
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float fc = 3500.0f;
    const float w0 = kTwoPi * fc / f;
    const float cosW0 = std::cos(w0);
    const float alpha = std::sin(w0) / std::sqrt(2.0f);
    const float a0 = 1.0f + alpha;
    lpb0_ = (1.0f - cosW0) * 0.5f / a0;
    lpb1_ = (1.0f - cosW0) / a0;
    lpb2_ = (1.0f - cosW0) * 0.5f / a0;
    lpa1_ = -2.0f * cosW0 / a0;
    lpa2_ = (1.0f - alpha) / a0;

    reset();
    timeSm_ = time_;
    feedbackSm_ = feedback_;
    levelSm_ = level_;
    prepared_ = true;
}

void Dm2DelayModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    for (int i = 0; i < n; ++i) {
        timeSm_ += smoothK_ * (time_ - timeSm_);
        feedbackSm_ += smoothK_ * (feedback_ - feedbackSm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        // time 0..1 -> 20..300 ms; feedback 0..1 -> 0..0.9 (below self-osc)
        delay_.setDelayMs(minDelayMs_ + (maxDelayMs_ - minDelayMs_) * timeSm_);
        const float fb = 0.9f * feedbackSm_;

        const float x = inL[i];
        const float wetIn = x + fb * wetPrev_;
        const float wet = delay_.process(wetIn);

        const float lpOut = lpb0_ * wet + lpb1_ * lpX1_ + lpb2_ * lpX2_
            - lpa1_ * lpY1_ - lpa2_ * lpY2_;
        lpX2_ = lpX1_;
        lpX1_ = wet;
        lpY2_ = lpY1_;
        lpY1_ = lpOut;
        wetPrev_ = lpOut;

        outL[i] = x + lpOut * levelSm_;
    }
}

void Dm2DelayModule::reset()
{
    delay_.reset();
    wetPrev_ = 0.0f;
    lpX1_ = 0.0f;
    lpX2_ = 0.0f;
    lpY1_ = 0.0f;
    lpY2_ = 0.0f;
}

void Dm2DelayModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void Dm2DelayModule::setMaxBlock(int)
{
}

void Dm2DelayModule::setParameter(const std::string& id, float value)
{
    if (id == "time") {
        time_ = value;
    } else if (id == "feedback") {
        feedback_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
