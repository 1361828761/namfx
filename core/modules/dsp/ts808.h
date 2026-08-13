#pragma once

#include "modules/dsp/wdf/ts_clipping.h"
#include "modules/dsp/wdf/ts_tone.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerTs808(ModuleRegistry& registry);

class Ts808Module final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    TsClippingStage clipping_;
    TsToneStage tone_;
    float levelDb_ = 0.0f;
    float levelGain_ = 1.0f;
    bool prepared_ = false;
};

} // namespace namfx
