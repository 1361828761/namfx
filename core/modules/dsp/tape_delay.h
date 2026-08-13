#pragma once

#include "modules/dsp/bbd_delay.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerTapeDelay(ModuleRegistry& registry);

// Maestro Echoplex EP-3 style tape delay: tape-class delay line (60-660 ms)
// with wow/flutter pitch wobble, tape saturation on the wet path, a tone
// controlled low-pass (3-8 kHz) and feedback inside the loop (repeats
// degrade). Behavioural model; circuit facts in docs/research/echoplex.md.
class TapeDelayModule final : public ModuleBase {
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
    float echo_ = 0.5f;
    float tone_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float timeSm_ = 0.5f;
    float echoSm_ = 0.5f;
    float toneSm_ = 0.5f;
    float levelSm_ = 0.5f;

    float fs_ = 48000.0f;
    float wetPrev_ = 0.0f;

    // wow / flutter phase accumulators
    float wowPhase_ = 0.0f;
    float flutPhase_ = 0.0f;

    // wet low-pass state (2nd order Butterworth, corner from tone param)
    float lpX1_ = 0.0f;
    float lpX2_ = 0.0f;
    float lpY1_ = 0.0f;
    float lpY2_ = 0.0f;

    float minDelayMs_ = 60.0f;
    float maxDelayMs_ = 660.0f;
};

} // namespace namfx
