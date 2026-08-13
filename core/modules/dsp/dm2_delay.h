#pragma once

#include "modules/dsp/bbd_delay.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerDm2Delay(ModuleRegistry& registry);

// Boss DM-2 style analog delay: BBD-class delay line (20-300 ms) with a
// wet-path 2-pole low-pass inside the feedback loop (repeats darken), dry
// plus wet mix. Behavioural model; circuit facts in docs/research/dm2.md.
class Dm2DelayModule final : public ModuleBase {
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

    float time_ = 0.5f;
    float feedback_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float timeSm_ = 0.5f;
    float feedbackSm_ = 0.5f;
    float levelSm_ = 0.5f;

    float fs_ = 48000.0f;
    float wetPrev_ = 0.0f;

    // wet-path low-pass (2nd order Butterworth at fc, fixed coefficients)
    float lpb0_ = 0.0f;
    float lpb1_ = 0.0f;
    float lpb2_ = 0.0f;
    float lpa1_ = 0.0f;
    float lpa2_ = 0.0f;
    float lpX1_ = 0.0f;
    float lpX2_ = 0.0f;
    float lpY1_ = 0.0f;
    float lpY2_ = 0.0f;

    float minDelayMs_ = 20.0f;
    float maxDelayMs_ = 300.0f;
};

} // namespace namfx
