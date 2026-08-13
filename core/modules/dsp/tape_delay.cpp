#include "modules/dsp/tape_delay.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerTapeDelay(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"time", "Time", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"echo", "Echo", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"tone", "Tone", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("dly.tape", "pedal", std::move(specs),
                            [] { return std::make_unique<TapeDelayModule>(); });
}

void TapeDelayModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    delay_.prepare(sampleRate, maxDelayMs_ + 2.0f);

    reset();
    timeSm_ = time_;
    echoSm_ = echo_;
    toneSm_ = tone_;
    levelSm_ = level_;
    prepared_ = true;
}

void TapeDelayModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float wowStep = kTwoPi * 0.7f / fs_;
    const float flutStep = kTwoPi * 6.0f / fs_;
    for (int i = 0; i < n; ++i) {
        timeSm_ += smoothK_ * (time_ - timeSm_);
        echoSm_ += smoothK_ * (echo_ - echoSm_);
        toneSm_ += smoothK_ * (tone_ - toneSm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        // wow/flutter modulate the delay time (docs/research/echoplex.md)
        wowPhase_ += wowStep;
        flutPhase_ += flutStep;
        const float wobble = 0.002f * std::sin(wowPhase_) + 0.0005f * std::sin(flutPhase_);
        delay_.setDelayMs((minDelayMs_ + (maxDelayMs_ - minDelayMs_) * timeSm_) * (1.0f + wobble));
        const float fb = 0.9f * echoSm_;

        // wet low-pass corner 3..8 kHz from the tone param
        const float fc = 3000.0f * std::pow(8000.0f / 3000.0f, toneSm_);
        const float w0 = kTwoPi * fc / fs_;
        const float cosW0 = std::cos(w0);
        const float alpha = std::sin(w0) / std::sqrt(2.0f);
        const float a0 = 1.0f + alpha;
        const float b0 = (1.0f - cosW0) * 0.5f / a0;
        const float b1 = (1.0f - cosW0) / a0;
        const float b2 = b0;
        const float a1 = -2.0f * cosW0 / a0;
        const float a2 = (1.0f - alpha) / a0;

        // tape saturation on the wet path (unity at small signals,
        // compression at high levels)
        const float x = inL[i];
        const float satIn = x + fb * wetPrev_;
        const float wet = delay_.process(satIn);
        const float sat = std::tanh(1.5f * wet) / 1.5f;

        const float lpOut = b0 * sat + b1 * lpX1_ + b2 * lpX2_ - a1 * lpY1_ - a2 * lpY2_;
        lpX2_ = lpX1_;
        lpX1_ = sat;
        lpY2_ = lpY1_;
        lpY1_ = lpOut;
        wetPrev_ = lpOut;

        outL[i] = x + lpOut * levelSm_;
    }
}

void TapeDelayModule::reset()
{
    delay_.reset();
    wetPrev_ = 0.0f;
    wowPhase_ = 0.0f;
    flutPhase_ = 0.0f;
    lpX1_ = 0.0f;
    lpX2_ = 0.0f;
    lpY1_ = 0.0f;
    lpY2_ = 0.0f;
}

void TapeDelayModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void TapeDelayModule::setMaxBlock(int)
{
}

void TapeDelayModule::setParameter(const std::string& id, float value)
{
    if (id == "time") {
        time_ = value;
    } else if (id == "echo") {
        echo_ = value;
    } else if (id == "tone") {
        tone_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
