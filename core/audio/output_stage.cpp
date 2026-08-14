#include "audio/output_stage.h"

#include <algorithm>
#include <utility>

namespace namfx {
namespace audio {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float dbFromParam(float p)
{
    return -12.0f + 24.0f * p;
}

} // namespace

void OutputStage::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    smoothK_ = 1.0f - std::exp(-1.0f / (0.01f * static_cast<float>(sampleRate)));
    reset();
    (void)maxBlockSize;
}

void OutputStage::reset()
{
    inputGainSm_ = inputGain_;
    masterSm_ = master_;
    bassSm_ = bass_;
    middleSm_ = middle_;
    trebleSm_ = treble_;
    low_ = Biquad{};
    mid_ = Biquad{};
    high_ = Biquad{};
    lowR_ = Biquad{};
    midR_ = Biquad{};
    highR_ = Biquad{};
    updateEqCoeffs();
}

void OutputStage::setInputGain(float db)
{
    inputGain_ = db;
}

void OutputStage::setMasterVolume(float db)
{
    master_ = db;
}

void OutputStage::setMute(bool mute)
{
    mute_ = mute;
}

void OutputStage::setBass(float v)
{
    bass_ = v;
}

void OutputStage::setMiddle(float v)
{
    middle_ = v;
}

void OutputStage::setTreble(float v)
{
    treble_ = v;
}

void OutputStage::updateEqCoeffs()
{
    const float fs = static_cast<float>(sampleRate_);
    const float alphaScale = 1.0f / std::sqrt(2.0f);

    auto shelf = [&](float f0, float db, Biquad& b) {
        const float A = std::pow(10.0f, db / 40.0f);
        const float w0 = 2.0f * kPi * f0 / fs;
        const float cw = std::cos(w0);
        const float alpha = std::sin(w0) * alphaScale; // S = 1
        const float a0 = (A + 1.0f) + (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha;
        b.b0 = A * ((A + 1.0f) - (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha) / a0;
        b.b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw) / a0;
        b.b2 = A * ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha) / a0;
        b.a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw) / a0;
        b.a2 = ((A + 1.0f) + (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha) / a0;
    };
    auto peak = [&](float f0, float db, Biquad& b) {
        const float A = std::pow(10.0f, db / 40.0f);
        const float w0 = 2.0f * kPi * f0 / fs;
        const float cw = std::cos(w0);
        const float alpha = std::sin(w0) * alphaScale; // Q = 1/sqrt(2)
        const float a0 = 1.0f + alpha / A;
        b.b0 = (1.0f + alpha * A) / a0;
        b.b1 = -2.0f * cw / a0;
        b.b2 = (1.0f - alpha * A) / a0;
        b.a1 = -2.0f * cw / a0;
        b.a2 = (1.0f - alpha / A) / a0;
    };

    shelf(250.0f, dbFromParam(bassSm_), low_);
    peak(1000.0f, dbFromParam(middleSm_), mid_);
    shelf(4000.0f, dbFromParam(trebleSm_), high_);
    // right channel shares the coefficients, keeps its own state
    lowR_.b0 = low_.b0;
    lowR_.b1 = low_.b1;
    lowR_.b2 = low_.b2;
    lowR_.a1 = low_.a1;
    lowR_.a2 = low_.a2;
    midR_.b0 = mid_.b0;
    midR_.b1 = mid_.b1;
    midR_.b2 = mid_.b2;
    midR_.a1 = mid_.a1;
    midR_.a2 = mid_.a2;
    highR_.b0 = high_.b0;
    highR_.b1 = high_.b1;
    highR_.b2 = high_.b2;
    highR_.a1 = high_.a1;
    highR_.a2 = high_.a2;
}

void OutputStage::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    updateEqCoeffs(); // per block; continuous in the smoothed params
    for (int i = 0; i < n; ++i) {
        inputGainSm_ += smoothK_ * (inputGain_ - inputGainSm_);
        masterSm_ += smoothK_ * (master_ - masterSm_);
        bassSm_ += smoothK_ * (bass_ - bassSm_);
        middleSm_ += smoothK_ * (middle_ - middleSm_);
        trebleSm_ += smoothK_ * (treble_ - trebleSm_);

        const float inGain = std::pow(10.0f, inputGainSm_ / 20.0f);
        const float masterLin = std::pow(10.0f, masterSm_ / 20.0f);

        float v = inL[i] * inGain;
        float shaped = v;
        high_.run(v, shaped);
        v = shaped;
        mid_.run(v, shaped);
        v = shaped;
        low_.run(v, shaped);
        outL[i] = mute_ ? 0.0f : shaped * masterLin;

        float vr = inR[i] * inGain;
        float shapedR = vr;
        highR_.run(vr, shapedR);
        vr = shapedR;
        midR_.run(vr, shapedR);
        vr = shapedR;
        lowR_.run(vr, shapedR);
        outR[i] = mute_ ? 0.0f : shapedR * masterLin;
    }
}

} // namespace audio
} // namespace namfx
