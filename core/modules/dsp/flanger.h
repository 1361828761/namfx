#pragma once

#include "modules/dsp/bbd_delay.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerFlanger(ModuleRegistry& registry);

// Electric Mistress style flanger: very short BBD delay (1-5 ms) modulated by
// a triangle LFO, with feedback into the delay input for comb-filter
// resonance, blended 50/50 with the dry signal. Circuit facts in
// docs/research/flanger.md.
class FlangerModule final : public ModuleBase {
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

    float feedback_ = 0.5f;
    float range_ = 0.5f;
    float rate_ = 0.5f;

    float smoothK_ = 0.0f;
    float feedbackSm_ = 0.5f;
    float rangeSm_ = 0.5f;
    float rateSm_ = 0.5f;

    float baseDelayMs_ = 1.0f;
    float maxModMs_ = 4.0f;
    float wetPrev_ = 0.0f;
};

} // namespace namfx
