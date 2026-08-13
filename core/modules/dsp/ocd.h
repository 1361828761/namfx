#pragma once

#include "modules/module_base.h"

#include <cmath>

namespace namfx {

class ModuleRegistry;

void registerMosfetOd(ModuleRegistry& registry);

// OCD-style hard-clipping overdrive. Signal chain (circuit facts in
// docs/research/ocd.md): gain stage (non-inverting, drive pot + feedback cap
// + bass cut) -> MOSFET pair clipper (2N7000 as slow-slope square-law diodes,
// asymmetric knee) -> fixed gain stage -> treble bleed tone -> volume.
class MosfetOdModule final : public ModuleBase {
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

    // MOSFET pair clipper in series with R9: i = K*(v - Vth)^2 * sign(v),
    // solved analytically from K*R9*(v - Vth)^2 + v - x = 0 (u = v - Vth,
    // u = (-1 + sqrt(1 + 4*K*R9*(x - Vth))) / (2*K*R9)).
    inline float clip(float x) const
    {
        if (x > kVthP_) {
            const float u = (-1.0f + std::sqrt(1.0f + 4.0f * kKR_ * (x - kVthP_))) / (2.0f * kKR_);
            return u + kVthP_;
        }
        if (x < -kVthN_) {
            const float u = (-1.0f + std::sqrt(1.0f + 4.0f * kKR_ * (-x - kVthN_))) / (2.0f * kKR_);
            return -(u + kVthN_);
        }
        return x;
    }

    static constexpr float kVthP_ = 2.0f;   // positive knee (MOSFET)
    static constexpr float kVthN_ = 1.4f;   // negative knee (germanium effect, v4)
    static constexpr float kKR_ = 1.0f;     // K * R9, calibrated so clipping sits ~3-5V

    double sampleRate_ = 48000.0;
    bool prepared_ = false;

    float drive_ = 0.5f;
    float tone_ = 0.5f;
    float volume_ = 0.5f;

    float smoothK_ = 0.0f;

    float gain1_ = 1.0f;
    float gain1Target_ = 1.0f;
    float lpA_ = 0.0f;        // feedback cap low-pass coefficient
    float lpATarget_ = 0.0f;
    float toneA_ = 0.0f;      // treble bleed low-pass coefficient
    float toneATarget_ = 0.0f;
    float volGain_ = 1.0f;
    float volGainTarget_ = 1.0f;

    float hpA_ = 0.0f;        // bass cut (fixed)
    float lpState_ = 0.0f;
    float hpState_ = 0.0f;
    float toneState_ = 0.0f;
};

} // namespace namfx
