#pragma once

#include "modules/dsp/bbd_delay.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerSpringReverb(ModuleRegistry& registry);

// Fender-style spring reverb: three parallel spring models (Accutronics
// type-9 style), each a delay line with a dispersive all-pass chain and a
// damping low-pass inside a feedback loop; dwell drives the springs with
// soft clipping; dry plus wet mix. Behavioural model; circuit facts in
// docs/research/spring.md.
class SpringReverbModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    static constexpr int kSprings = 3;
    static constexpr int kDispersionStages = 5;

    bool prepared_ = false;

    float dwell_ = 0.5f;
    float mix_ = 0.5f;
    float damp_ = 0.5f;

    float smoothK_ = 0.0f;
    float dwellSm_ = 0.5f;
    float mixSm_ = 0.5f;
    float dampSm_ = 0.5f;

    float fs_ = 48000.0f;

    FractionalDelay delay_[kSprings];
    float lpState_[kSprings] = {0.0f, 0.0f, 0.0f};
    float apX1_[kSprings][kDispersionStages] = {};
    float apY1_[kSprings][kDispersionStages] = {};

    static constexpr float kDelayMs_[kSprings] = {26.0f, 31.0f, 36.0f};
    static constexpr float kDispersion_[kSprings] = {0.12f, 0.16f, 0.20f};
    static constexpr float kFeedback_[kSprings] = {0.85f, 0.87f, 0.89f};
};

} // namespace namfx
