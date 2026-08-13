#include "modules/dsp/phaser.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerPhaser(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"depth", "Depth", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"rate", "Rate", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("mod.phaser", "pedal", std::move(specs),
                            [] { return std::make_unique<PhaserModule>(); });
}

void PhaserModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));

    lfo_.prepare(sampleRate);

    reset();
    depthSm_ = depth_;
    rateSm_ = rate_;
    levelSm_ = level_;
    prepared_ = true;
}

void PhaserModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    const float fs = static_cast<float>(lfo_.sampleRate());
    for (int i = 0; i < n; ++i) {
        depthSm_ += smoothK_ * (depth_ - depthSm_);
        rateSm_ += smoothK_ * (rate_ - rateSm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        // rate 0..1 -> 0.05..2 Hz (classic phaser sweep speed)
        const float hz = 0.05f + 1.95f * rateSm_;
        lfo_.setRate(hz);
        const float lfo = lfo_.process();

        // all-pass corner sweeps exponentially between fcMin and fcMax
        const float fc = fcMin_ * std::pow(fcMax_ / fcMin_, depthSm_ * lfo);
        const float t = std::tan(3.14159265f * fc / fs);
        const float a = (1.0f - t) / (1.0f + t);

        float x = inL[i];
        for (int k = 0; k < kStages; ++k) {
            const float y = -a * x + x1_[k] + a * y1_[k];
            x1_[k] = x;
            y1_[k] = y;
            x = y;
        }

        outL[i] = (inL[i] * 0.5f + x * 0.5f) * levelSm_;
    }
}

void PhaserModule::reset()
{
    lfo_.reset();
    for (int k = 0; k < kStages; ++k) {
        x1_[k] = 0.0f;
        y1_[k] = 0.0f;
    }
}

void PhaserModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void PhaserModule::setMaxBlock(int)
{
}

void PhaserModule::setParameter(const std::string& id, float value)
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
