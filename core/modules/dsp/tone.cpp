#include "modules/dsp/tone.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerTone(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"freq", "Freq", 80.0f, 8000.0f, 2000.0f, "Hz", Taper::Log});
    specs.push_back(ParamSpec{"mode", "Mode", 0.0f, 1.0f, 0.0f, "", Taper::Linear});
    registry.registerModule("tone", "pedal", std::move(specs),
                            [] { return std::make_unique<ToneModule>(); });
}

void ToneModule::prepare(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    updateCoefficient();
}

void ToneModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (mode_ == 0) {
        const double a = coeff_;
        double state = stateL_;
        for (int i = 0; i < n; ++i) {
            state += a * (static_cast<double>(inL[i]) - state);
            outL[i] = static_cast<float>(state);
        }
        stateL_ = state;
        return;
    }
    const double a = coeff_;
    double state = stateL_;
    double previous = prevInput_;
    for (int i = 0; i < n; ++i) {
        const double x = static_cast<double>(inL[i]);
        const double y = a * (state + x - previous);
        previous = x;
        state = y;
        outL[i] = static_cast<float>(y);
    }
    stateL_ = state;
    prevInput_ = previous;
}

void ToneModule::reset()
{
    stateL_ = 0.0;
    prevInput_ = 0.0;
}

void ToneModule::setSampleRate(double sampleRate)
{
    sampleRate_ = sampleRate;
    updateCoefficient();
}

void ToneModule::setMaxBlock(int)
{
}

void ToneModule::setParameter(const std::string& id, float value)
{
    if (id == "freq") {
        freq_ = value;
        updateCoefficient();
    } else if (id == "mode") {
        mode_ = value >= 0.5f ? 1 : 0;
    }
}

void ToneModule::updateCoefficient()
{
    constexpr double kTwoPi = 6.28318530717958647692;
    if (mode_ == 0) {
        coeff_ = 1.0 - std::exp(-kTwoPi * static_cast<double>(freq_) / sampleRate_);
    } else {
        coeff_ = std::exp(-kTwoPi * static_cast<double>(freq_) / sampleRate_);
    }
}

} // namespace namfx
