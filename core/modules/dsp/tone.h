#pragma once

#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerTone(ModuleRegistry& registry);

class ToneModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    void updateCoefficient();

    float freq_ = 2000.0f;
    int mode_ = 0;
    double sampleRate_ = 48000.0;
    double coeff_ = 1.0;
    double stateL_ = 0.0;
    double prevInput_ = 0.0;
};

} // namespace namfx
