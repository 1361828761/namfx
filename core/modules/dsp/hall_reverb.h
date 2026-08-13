#pragma once

#include "modules/module_base.h"

#include <vector>

namespace namfx {

class ModuleRegistry;

void registerHallReverb(ModuleRegistry& registry);

// Hall reverb based on the public-domain Freeverb algorithm: 8 parallel comb
// filters (feedback + in-loop damping low-pass) into 4 serial all-pass
// filters, mixed with the dry signal. Mono (mono-in/mono-out decision).
// Algorithm facts in docs/research/hall.md.
class HallReverbModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    struct Comb {
        std::vector<float> buf;
        int idx = 0;
        float filter = 0.0f;
        float feedback = 0.0f;
        float damp1 = 0.0f;
    };

    struct Allpass {
        std::vector<float> buf;
        int idx = 0;
    };

    bool prepared_ = false;

    float room_ = 0.5f;
    float damp_ = 0.5f;
    float mix_ = 0.5f;

    float smoothK_ = 0.0f;
    float roomSm_ = 0.5f;
    float dampSm_ = 0.5f;
    float mixSm_ = 0.5f;

    Comb combs_[8];
    Allpass allpasses_[4];
};

} // namespace namfx
