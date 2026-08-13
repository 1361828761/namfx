#pragma once

#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerGe7Eq(ModuleRegistry& registry);

// Boss GE-7 style graphic EQ: seven octave-spaced bands (100 Hz - 6.4 kHz),
// each +/-15 dB; bands 0-5 are peaking biquads and band 6 (6.4 kHz) is a
// high shelf, cascaded, with a +/-15 dB master level. Behavioural model;
// circuit facts in docs/research/ge7.md.
class Ge7EqModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    static constexpr int kNumBands = 7;

    bool prepared_ = false;

    float bands_[kNumBands] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float bandsSm_[kNumBands] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float levelSm_ = 0.5f;

    float fs_ = 48000.0f;
    float x1_[kNumBands] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float x2_[kNumBands] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float y1_[kNumBands] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float y2_[kNumBands] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    float cosW0_[kNumBands] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float alpha_[kNumBands] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace namfx
