#pragma once

#include "modules/module_base.h"

#include <vector>

namespace namfx {

class ModuleRegistry;

void registerPitchShifter(ModuleRegistry& registry);

// Fixed-ratio monophonic pitch shifter: a delay line whose read pointer
// advances at rate r = 2^(semitones/12), with periodic crossfades that hide
// the read/write pointer wrap (DAFX-style). The shift parameter is the
// future control-source hook (whammy, M5). Algorithm facts in
// docs/research/pitch.md.
class PitchShifterModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    float readAt(float pos) const;

    bool prepared_ = false;

    float shift_ = 0.5f;
    float mix_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float shiftSm_ = 0.5f;
    float mixSm_ = 0.5f;
    float levelSm_ = 0.5f;

    float fs_ = 48000.0f;
    std::vector<float> buf_;
    int n_ = 0;
    int writePtr_ = 0;
    float readPos_ = 0.0f;
    int crossfadeLeft_ = 0;
    float targetOffset_ = 0.0f;
    int c_ = 0;
    int maxDelay_ = 0;
};

} // namespace namfx
