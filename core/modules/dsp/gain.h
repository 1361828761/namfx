#pragma once

#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerGain(ModuleRegistry& registry);

class GainModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    float gainDb_ = 0.0f;
    float gainLinear_ = 1.0f;
};

} // namespace namfx
