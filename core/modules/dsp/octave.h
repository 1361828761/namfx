#pragma once

#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerOctave(ModuleRegistry& registry);

// Boss OC-2 style octave-down: the input's rising zero crossings flip a
// divide-by-two state; the sub-octave is the input waveform multiplied by
// that alternating +-1 flip (waveform fold, timbre preserved), shaped by a
// tone low-pass, mixed with the dry signal. Behavioural model; circuit
// facts in docs/research/octave.md.
class OctaveModule final : public ModuleBase {
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

    float mix_ = 0.5f;
    float tone_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float mixSm_ = 0.5f;
    float toneSm_ = 0.5f;
    float levelSm_ = 0.5f;

    float fs_ = 48000.0f;
    float flip_ = 1.0f;
    float prevX_ = 0.0f;

    // free-running divide-by-two: subPhase accumulates at 1/smoothed-period
    // per sample, flipping once per input period; zero crossings only update
    // the period estimate
    double prevZ_ = 0.0;
    double dSm_ = 100.0;
    float subPhase_ = 0.0f;
    long long lastCrossCounter_ = -100;
    long long counter_ = 0;

    // sub low-pass state (2nd order Butterworth, corner from tone param)
    float lpX1_ = 0.0f;
    float lpX2_ = 0.0f;
    float lpY1_ = 0.0f;
    float lpY2_ = 0.0f;
};

} // namespace namfx
