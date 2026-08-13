#pragma once

#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerNs2Gate(ModuleRegistry& registry);

// Boss NS-2 style noise gate: fast peak-follower envelope detection, binary
// threshold decision, and a smoothed gate gain (fast open, decay-controlled
// close) applied to the input. Detection on the input signal is the NS-2
// principle; the send/return loop is not modelled in v1 (no loop routing in
// the engine yet). Circuit facts in docs/research/ns2.md.
class Ns2GateModule final : public ModuleBase {
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

    float threshold_ = 0.5f;
    float decay_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float thresholdSm_ = 0.5f;
    float decaySm_ = 0.5f;
    float levelSm_ = 0.5f;

    float fs_ = 48000.0f;
    float env_ = 0.0f;
    float gain_ = 0.0f;
};

} // namespace namfx
