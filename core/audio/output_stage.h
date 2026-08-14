#pragma once

#include <cmath>
#include <cstddef>

namespace namfx {
namespace audio {

// Output stage (PLAN G3/M5b): input gain (pre), master volume + mute, and a
// global 3-band EQ (low shelf 250 Hz / peaking 1 kHz / high shelf 4 kHz,
// same RBJ design as amp.nam's post EQ). Runs at the end of the audio
// chain. Real-time safe: all state pre-allocated, per-sample parameter
// smoothing, coefficients refreshed per block (continuous in the smoothed
// parameters, no zipper).
class OutputStage {
public:
    OutputStage() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // control/UI thread (values snap through the per-sample smoother)
    void setInputGain(float db);   // -60..+24 dB
    void setMasterVolume(float db); // -60..0 dB
    void setMute(bool mute);
    void setBass(float v);   // 0..1 -> -12..+12 dB at 250 Hz
    void setMiddle(float v); // 0..1 -> -12..+12 dB at 1 kHz
    void setTreble(float v); // 0..1 -> -12..+12 dB at 4 kHz

    // audio thread
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        void run(float in, float& out)
        {
            out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
        }
    };

    void updateEqCoeffs();

    double sampleRate_ = 48000.0;
    float smoothK_ = 0.0f;
    float inputGain_ = 0.0f;
    float inputGainSm_ = 0.0f;
    float master_ = 0.0f;
    float masterSm_ = 0.0f;
    bool mute_ = false;
    float bass_ = 0.5f;
    float bassSm_ = 0.5f;
    float middle_ = 0.5f;
    float middleSm_ = 0.5f;
    float treble_ = 0.5f;
    float trebleSm_ = 0.5f;
    Biquad low_;
    Biquad mid_;
    Biquad high_;
    Biquad lowR_;
    Biquad midR_;
    Biquad highR_;
};

} // namespace audio
} // namespace namfx
