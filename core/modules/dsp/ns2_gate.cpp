#include "modules/dsp/ns2_gate.h"

#include "modules/module_registry.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace namfx {

void registerNs2Gate(ModuleRegistry& registry)
{
    std::vector<ParamSpec> specs;
    specs.push_back(ParamSpec{"threshold", "Threshold", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"decay", "Decay", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    specs.push_back(ParamSpec{"level", "Level", 0.0f, 1.0f, 0.5f, "", Taper::Linear});
    registry.registerModule("gate.ns2", "pedal", std::move(specs),
                            [] { return std::make_unique<Ns2GateModule>(); });
}

void Ns2GateModule::prepare(double sampleRate, int)
{
    const float f = static_cast<float>(sampleRate);
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * f));
    fs_ = f;

    reset();
    thresholdSm_ = threshold_;
    decaySm_ = decay_;
    levelSm_ = level_;
    prepared_ = true;
}

void Ns2GateModule::process(const float* inL, const float*, float* outL, float*, int n)
{
    if (!prepared_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = inL[i];
        }
        return;
    }
    // envelope: fast attack (~1 ms), quick release (~20 ms) so the decay
    // knob controls the audible fade-to-silence time
    const float envAttackK = 1.0f - std::exp(-1.0f / (0.001f * fs_));
    const float envReleaseK = 1.0f - std::exp(-1.0f / (0.02f * fs_));
    for (int i = 0; i < n; ++i) {
        thresholdSm_ += smoothK_ * (threshold_ - thresholdSm_);
        decaySm_ += smoothK_ * (decay_ - decaySm_);
        levelSm_ += smoothK_ * (level_ - levelSm_);

        // threshold 0..1 -> -70..-10 dB
        const float threshLin = std::pow(10.0f, (-70.0f + 60.0f * thresholdSm_) / 20.0f);
        const float x = inL[i];
        const float absx = std::fabs(x);

        if (absx > env_) {
            env_ += envAttackK * (absx - env_);
        } else {
            env_ += envReleaseK * (absx - env_);
        }

        // gate gain: fast open (~1 ms), close over the decay time (10..500 ms)
        const float target = env_ > threshLin ? 1.0f : 0.0f;
        if (target > gain_) {
            gain_ += (1.0f - std::exp(-1.0f / (0.001f * fs_))) * (target - gain_);
        } else {
            const float closeT = 0.01f + 0.49f * decaySm_;
            gain_ += (1.0f - std::exp(-1.0f / (closeT * fs_))) * (target - gain_);
        }

        outL[i] = x * gain_ * levelSm_;
    }
}

void Ns2GateModule::reset()
{
    env_ = 0.0f;
    gain_ = 0.0f;
}

void Ns2GateModule::setSampleRate(double sampleRate)
{
    prepare(sampleRate, 0);
}

void Ns2GateModule::setMaxBlock(int)
{
}

void Ns2GateModule::setParameter(const std::string& id, float value)
{
    if (id == "threshold") {
        threshold_ = value;
    } else if (id == "decay") {
        decay_ = value;
    } else if (id == "level") {
        level_ = value;
    }
}

} // namespace namfx
