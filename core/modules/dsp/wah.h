#pragma once

#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerWah(ModuleRegistry& registry);

// Dunlop Cry Baby style wah: resonant state-variable filter (low + band
// blend, ~Q boost at the swept centre) whose centre frequency is set by
// the position parameter and whose Q is set by the resonance parameter;
// the position parameter is the future control-source hook (expression
// pedal / CC, M5). Circuit facts in docs/research/crybaby.md.
class WahModule final : public ModuleBase {
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

    float position_ = 0.5f;
    float resonance_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float positionSm_ = 0.5f;
    float resonanceSm_ = 0.5f;
    float levelSm_ = 0.5f;

    float fs_ = 48000.0f;
    float ic1_ = 0.0f;
    float ic2_ = 0.0f;

    float fcMin_ = 300.0f;
    float fcMax_ = 2500.0f;
};

} // namespace namfx
