#pragma once

#include "modules/dsp/bbd_delay.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerPhaser(ModuleRegistry& registry);

// MXR Phase 90 style phaser: four cascaded first-order all-pass stages whose
// corner frequency is swept by a triangle LFO; dry and wet are blended 50/50.
// Behavioural model; circuit facts in docs/research/phase90.md.
class PhaserModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    static constexpr int kStages = 4;

    bool prepared_ = false;

    TriLfo lfo_;

    float depth_ = 0.5f;
    float rate_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float depthSm_ = 0.5f;
    float rateSm_ = 0.5f;
    float levelSm_ = 0.5f;

    float x1_[kStages] = {0.0f, 0.0f, 0.0f, 0.0f};
    float y1_[kStages] = {0.0f, 0.0f, 0.0f, 0.0f};

    float fcMin_ = 300.0f;
    float fcMax_ = 3500.0f;
};

} // namespace namfx
