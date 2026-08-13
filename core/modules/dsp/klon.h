#pragma once

#include "modules/dsp/wdf/klon_wdf.h"
#include "modules/module_base.h"

namespace namfx {

class ModuleRegistry;

void registerTransparent(ModuleRegistry& registry);

// Klon-style "transparent" overdrive. Signal chain (circuit facts in
// docs/research/klon.md): input buffer -> preamp (WDF) -> amp stage (biquad)
// -> clipping (WDF, 2x oversampled) -> feed-forward network (WDF) -> summing
// amp -> tone -> output stage. The dry/wet blend is inherent to the circuit
// (clipped + two feed-forward paths are summed by the op-amp).
class TransparentModule final : public ModuleBase {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) override;
    void reset() override;
    void setSampleRate(double sampleRate) override;
    void setMaxBlock(int maxBlockSize) override;
    ChannelMode channelMode() const override { return ChannelMode::MonoInMonoOut; }
    void setParameter(const std::string& id, float value) override;

private:
    struct Biquad1 {
        void prepare(double fs);
        void reset();
        void setCoefs(float b0, float b1, float a1);
        inline float process(float x)
        {
            const float y = b0_ * x + b1_ * x1_ - a1_ * y1_;
            x1_ = x;
            y1_ = y < 1.0e-15f && y > -1.0e-15f ? 0.0f : y;
            return y;
        }
        float b0_ = 0.0f;
        float b1_ = 0.0f;
        float a1_ = 0.0f;
        float x1_ = 0.0f;
        float y1_ = 0.0f;
    };

    struct Biquad2 {
        void prepare(double fs);
        void reset();
        void setCoefs(float b0, float b1, float b2, float a1, float a2);
        inline float process(float x)
        {
            const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
            x2_ = x1_;
            x1_ = x;
            y2_ = y1_;
            y1_ = y < 1.0e-15f && y > -1.0e-15f ? 0.0f : y;
            return y;
        }
        float b0_ = 0.0f;
        float b1_ = 0.0f;
        float b2_ = 0.0f;
        float a1_ = 0.0f;
        float a2_ = 0.0f;
        float x1_ = 0.0f;
        float x2_ = 0.0f;
        float y1_ = 0.0f;
        float y2_ = 0.0f;
    };

    void updateAmpStageCoefs(float r10b);
    void updateToneCoefs(float treble);
    void updateOutputCoefs(float level);

    KlonPreAmp preAmp_;
    KlonClipping clipping_;
    KlonFeedForward2 feedForward2_;

    Biquad1 inputBuffer_;
    Biquad2 ampStage_;
    Biquad1 summingAmp_;
    Biquad1 tone_;
    Biquad1 outputStage_;
    Biquad1 dcBlocker_;

    float gain_ = 0.5f;
    float treble_ = 0.5f;
    float level_ = 0.5f;
    double sampleRate_ = 48000.0;
    bool prepared_ = false;
};

} // namespace namfx
