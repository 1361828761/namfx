#pragma once

#include "modules/dsp/bbd_delay.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerChorus(ModuleRegistry& registry);

// CE-2 style chorus: BBD delay line (~20 ms base) modulated by a triangle LFO,
// blended 50/50 with the dry signal. Behavioural model; circuit facts in
// docs/research/chorus.md.
class ChorusModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    bool prepared_ = false;

    FractionalDelay delay_;
    TriLfo lfo_;

    float depth_ = 0.5f;
    float rate_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float depthSm_ = 0.5f;
    float rateSm_ = 0.5f;
    float levelSm_ = 0.5f;

    float baseDelayMs_ = 20.0f;
    float maxModMs_ = 8.0f;
};

} // namespace namfx
