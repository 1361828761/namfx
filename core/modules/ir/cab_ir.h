#pragma once

#include "modules/ir/wav_io.h"
#include "modules/module_base.h"

#include <vector>

namespace namfx {

class ModuleRegistry;

void registerCabIr(ModuleRegistry& registry);

// Cabinet IR convolution module: loads a WAV impulse response (load path),
// resamples it to the engine rate in prepare, and runs direct time-domain
// convolution in the callback (partitioned FFT convolution is the later
// performance path). Oversized IRs are rejected, never truncated.
// Facts in docs/research/ir.md.
class CabIrModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;
    bool loadAsset(const std::string& path) override;

private:
    bool prepared_ = false;

    // asset state (set by loadAsset, load path)
    std::vector<float> rawIr_;
    double rawRate_ = 0.0;
    bool assetLoaded_ = false;

    // engine-rate state (built in prepare)
    std::vector<float> ir_;
    std::vector<float> buf_;
    int bufPos_ = 0;

    float fs_ = 48000.0f;
    float gain_ = 0.5f;
    float lowcut_ = 0.0f;
    float highcut_ = 1.0f;
    float gainSm_ = 0.5f;
    float lowcutSm_ = 0.0f;
    float highcutSm_ = 1.0f;
    float smoothK_ = 0.0f;

    // tone filter state (2nd order Butterworth, corners from params)
    float hpX1_ = 0.0f;
    float hpX2_ = 0.0f;
    float hpY1_ = 0.0f;
    float hpY2_ = 0.0f;
    float lpX1_ = 0.0f;
    float lpX2_ = 0.0f;
    float lpY1_ = 0.0f;
    float lpY2_ = 0.0f;

    static constexpr std::size_t kMaxIrSamples = 65536;
};

} // namespace namfx
