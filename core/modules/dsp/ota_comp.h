#pragma once

#include "modules/module_base.h"

#include <cmath>

namespace namfx {

class ModuleRegistry;

void registerOtaComp(ModuleRegistry& registry);

// Dyna Comp family OTA compressor. Circuit facts in docs/research/dynacomp.md:
// the LM13700/CA3080 OTA acts as a voltage-controlled gain (gm ~ Iabc), driven
// by a feedback envelope detector (rectifier + integrating cap). Envelope ->
// VCA gain g = 1 / (1 + sustain * k * env); the result is blended with the dry
// signal (ratio) and scaled by level.
class OtaCompModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    void updateTargets();

    double sampleRate_ = 48000.0;
    bool prepared_ = false;

    float sustain_ = 0.5f;
    float attack_ = 0.5f;
    float ratio_ = 0.5f;
    float level_ = 0.5f;

    float smoothK_ = 0.0f;
    float sustainSm_ = 0.5f;
    float ratioSm_ = 0.5f;
    float levelSm_ = 0.5f;
    float attackA_ = 0.0f;      // envelope attack coefficient
    float attackATarget_ = 0.0f;
    float releaseA_ = 0.0f;

    float env_ = 0.0f;
};

} // namespace namfx
